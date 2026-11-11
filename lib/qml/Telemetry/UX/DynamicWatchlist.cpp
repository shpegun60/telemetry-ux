// One pending read, bounded wire chunks, Qt-owned scalar rows. MIT.
#include "DynamicWatchlist.hpp"
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <algorithm>
#include <utility>

namespace telemetry::ux {
struct DynamicWatchlist::Impl : std::enable_shared_from_this<Impl> {
	struct Request {
		std::shared_ptr<void> storage;
		std::uint64_t revision = 0;
		int cursor = 0, count = 0;
		std::array<std::uint32_t, 32> ids{};
		std::optional<WatchlistCompletion> completion;
	};

	QPointer<QObject> context;
	WatchlistModel model;
	QTimer timer;
	WatchlistSource source;
	std::optional<WatchlistSource> desired;
	std::shared_ptr<Request> request;
	std::function<void()> notify;
	std::function<bool()> blocked;
	QString status = QStringLiteral("ReadMany; not started");
	std::uint64_t revision = 0;
	int cursor = 0;
	bool enabled = false, attached = false, running = false, cycle = false, individual = false;
	bool publishing = false, queued = false, retiring = false, clearRetiredRows = false;
	bool deferredQueued = false;

	explicit Impl(QObject* owner) : context(owner)
	{
		timer.setInterval(1000);
		timer.setTimerType(Qt::PreciseTimer);
		timer.setObjectName(QStringLiteral("dynamicConfiguratorWatchTimer"));
	}

	bool current() const
	{
		return context && attached && !retiring && source.current && source.current();
	}

	bool active() const
	{
		return running || cycle || bool(request) || retiring;
	}

	bool guarded() const
	{
		return !context || QThread::currentThread() != context->thread() || publishing ||
		       model.changing();
	}

	bool unavailable() const
	{
		return blocked && blocked();
	}

	void changed()
	{
		if (context && notify)
			notify();
	}

	void attach(WatchlistSource value)
	{
		source = std::move(value);
		attached = true;
		const std::weak_ptr<Impl> weak = shared_from_this();
		model.bind([weak] {
			const auto p = weak.lock();
			return p && p->current();
		});
		status =
		    current() ? QStringLiteral("ReadMany; not started") : QStringLiteral("Not connected");
	}

	void retire(bool clearRows)
	{
		running = cycle = false;
		timer.stop();
		++revision;
		if (clearRows)
			attached = false;
		publishing = true;
		if (clearRows)
			model.clear();
		else
			model.invalidateValues();
		publishing = false;
		if (clearRows && !request)
			source = {};
		status = request ? QStringLiteral("Stopping - draining accepted read")
		                 : QStringLiteral("Stopped - values invalidated");
	}

	void defer()
	{
		if (!context || deferredQueued)
			return;
		deferredQueued = true;
		const std::weak_ptr<Impl> weak = shared_from_this();
		QMetaObject::invokeMethod(
		    context,
		    [weak] {
			    if (auto p = weak.lock()) {
				    p->deferredQueued = false;
				    if (p->guarded()) {
					    p->defer();
					    return;
				    }
				    if (p->retiring) {
					    p->retiring = false;
					    p->retire(std::exchange(p->clearRetiredRows, false));
				    }
				    if (p->desired && !p->request) {
					    p->attach(std::move(*p->desired));
					    p->desired.reset();
				    }
				    p->poll();
				    p->changed();
			    }
		    },
		    Qt::QueuedConnection);
	}

	void continueCycle()
	{
		if (!context || !cycle || queued || request)
			return;
		queued = true;
		const std::weak_ptr<Impl> weak = shared_from_this();
		QMetaObject::invokeMethod(
		    context,
		    [weak] {
			    if (auto p = weak.lock()) {
				    p->queued = false;
				    p->next();
			    }
		    },
		    Qt::QueuedConnection);
	}

	void fail(QString message)
	{
		running = cycle = false;
		timer.stop();
		status = std::move(message);
		changed();
	}

	void next()
	{
		if (guarded() || !cycle || request || unavailable())
			return;
		if (!current()) {
			retire(true);
			changed();
			return;
		}
		if (source.busy && source.busy())
			return; // No accumulation behind foreground/other panels.
		if (cursor >= model.count()) {
			cycle = false;
			status = QStringLiteral("%1 complete - %2 Fields")
			             .arg(individual ? QStringLiteral("Individual FieldRead")
			                             : QStringLiteral("ReadMany"))
			             .arg(model.count());
			changed();
			return;
		}
		auto pending = std::make_shared<Request>();
		pending->storage = source.storage;
		pending->revision = revision;
		pending->cursor = cursor;
		if (individual) {
			if (!source.readField) {
				fail(QStringLiteral("Individual FieldRead unavailable"));
				return;
			}
			pending->ids[0] = model.id(cursor);
			pending->count = 1;
		} else {
			const auto limits = source.capability ? source.capability() : std::nullopt;
			if (!limits || !source.readMany) {
				fail(QStringLiteral(
				    "ReadMany unavailable - explicitly select Individual FieldRead"));
				return;
			}
			unsigned reply = 2;
			while (cursor + pending->count < model.count() && pending->count < 32 &&
			       unsigned(pending->count) < limits->items &&
			       2u + 4u * (unsigned(pending->count) + 1u) <= limits->requestBytes) {
				const auto id = model.id(cursor + pending->count);
				const auto bytes = source.fieldBytes ? source.fieldBytes(id) : std::nullopt;
				if (!bytes || *bytes == 0 || *bytes > 8) {
					fail(QStringLiteral("Watch Field is stale or not a readable scalar"));
					return;
				}
				if (reply + 8u + *bytes > limits->replyBytes)
					break;
				reply += 8u + *bytes;
				pending->ids[pending->count++] = id;
			}
			if (!pending->count) {
				fail(QStringLiteral(
				    "Next Field exceeds ReadMany byte budget - select Individual FieldRead"));
				return;
			}
		}
		// An aliasing shared pointer retains the immutable request and its storage.
		// The native adapter can only fill this slot, never call a QObject callback.
		auto complete = WatchlistSource::Completion{pending, &pending->completion};
		const auto submitted =
		    individual ? source.readField(pending->ids[0], std::move(complete))
		               : source.readMany(
		                     std::span<const std::uint32_t>{pending->ids}.first(pending->count),
		                     std::move(complete));
		if (!submitted) {
			if (submitted.status == client::Status::Busy ||
			    submitted.status == client::Status::QueueFull) {
				status = QStringLiteral("Waiting for the shared session");
				changed();
			} else
				fail(
				    QStringLiteral("Watch submission refused: %1").arg(unsigned(submitted.status)));
			return;
		}
		request = std::move(pending);
		status = QStringLiteral("Reading Fields %1-%2 of %3")
		             .arg(cursor + 1)
		             .arg(cursor + request->count)
		             .arg(model.count());
		changed();
	}

	void poll()
	{
		if (guarded())
			return;
		if (!current() && (running || cycle || model.count()))
			retire(true);
		if (request && request->completion) {
			auto completed = std::move(request);
			auto result = std::move(*completed->completion);
			if (completed->revision != revision || !current()) {
				status = QStringLiteral("Stopped - accepted read drained");
			} else {
				bool valid = result.status == client::Status::Ok &&
				             result.count == completed->count && result.count > 0 &&
				             result.count <= 32 && completed->cursor == cursor &&
				             result.count <= model.count() - cursor;
				for (int i = 0; valid && i < result.count; ++i)
					valid = result.rows[i].id == completed->ids[i] &&
					        model.id(cursor + i) == completed->ids[i];
				if (!valid) {
					running = cycle = false;
					timer.stop();
					publishing = true;
					model.invalidateValues();
					publishing = false;
					status = QStringLiteral("Watch read failed: %1").arg(unsigned(result.status));
				} else {
					publishing = true;
					for (int i = 0; context && i < result.count; ++i)
						model.setResult(cursor + i, std::move(result.rows[i].status),
						                std::move(result.rows[i].text), result.rows[i].sample);
					publishing = false;
					cursor += result.count;
				}
			}
			if (desired && !retiring) {
				attach(std::move(*desired));
				desired.reset();
			} else if (!current() && !retiring) {
				source = {};
			}
			changed();
		}
		continueCycle();
	}

	void tick()
	{
		if (guarded() || !running || cycle || request || unavailable() || !current() ||
		    (source.busy && source.busy()))
			return;
		cycle = true;
		cursor = 0;
		next();
	}
};

DynamicWatchlist::DynamicWatchlist(QObject* context) : d(std::make_shared<Impl>(context))
{
	const std::weak_ptr<Impl> weak = d;
	QObject::connect(&d->timer, &QTimer::timeout, context, [weak] {
		if (auto p = weak.lock())
			p->tick();
	});
}

DynamicWatchlist::~DynamicWatchlist() = default;

void DynamicWatchlist::setNotify(std::function<void()> notify, std::function<bool()> blocked)
{
	d->notify = std::move(notify);
	d->blocked = std::move(blocked);
}

void DynamicWatchlist::setSource(WatchlistSource source)
{
	auto p = d;
	if (!p->context)
		return;
	p->enabled = true;
	p->desired = std::move(source);
	if (p->guarded()) {
		p->running = p->cycle = false;
		p->timer.stop();
		++p->revision;
		p->retiring = p->clearRetiredRows = true;
		p->defer();
		return;
	}
	p->retire(true);
	if (!p->request) {
		p->retiring = p->clearRetiredRows = false;
		p->attach(std::move(*p->desired));
		p->desired.reset();
	}
	p->changed();
}

void DynamicWatchlist::retire(bool clearRows)
{
	auto p = d;
	if (p->guarded()) {
		p->retiring = true;
		p->clearRetiredRows |= clearRows;
		p->defer();
	} else {
		p->retire(clearRows);
		p->changed();
	}
}

void DynamicWatchlist::poll()
{
	auto p = d;
	p->poll();
}

void DynamicWatchlist::tick()
{
	auto p = d;
	p->tick();
}

QAbstractItemModel* DynamicWatchlist::model() const
{
	return d->enabled ? &d->model : nullptr;
}

bool DynamicWatchlist::supported() const
{
	return d->current() && d->source.capability && d->source.capability().has_value();
}

bool DynamicWatchlist::active() const
{
	return d->active();
}

bool DynamicWatchlist::pending() const
{
	return bool(d->request);
}

bool DynamicWatchlist::canAdd(quint32 id) const
{
	auto p = d;
	if (p->guarded() || !p->current() || p->active() || !p->source.fieldBytes ||
	    p->model.count() == WatchlistModel::maxRows)
		return false;
	const auto bytes = p->source.fieldBytes(id);
	if (!bytes || !*bytes || *bytes > 8)
		return false;
	for (int i = 0; i < p->model.count(); ++i)
		if (p->model.id(i) == id)
			return false;
	return true;
}

bool DynamicWatchlist::add(quint32 id, QString name)
{
	auto p = d;
	if (!canAdd(id))
		return false;
	const bool added = p->model.add(id, std::move(name));
	if (added) {
		++p->revision;
		p->changed();
	}
	return added;
}

QVariantMap DynamicWatchlist::addMany(const QVector<QPair<quint32, QString>>& entries)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")},
	                   {QStringLiteral("added"), 0}, {QStringLiteral("duplicate"), 0},
	                   {QStringLiteral("unsupported"), 0}, {QStringLiteral("capacity"), 0}};
	if (p->guarded() || !p->current() || p->active() || !p->source.fieldBytes)
		return result;
	// Block changes and defer source replacement throughout preparation and
	// publication. Once insert signals start, accepted rows are committed as a
	// single range; a reentrant retirement is handled by the existing drain.
	p->publishing = true;
	struct Publication {
		Impl& state;
		~Publication() { state.publishing = false; }
	} publication{*p};
	const auto revision = p->revision;
	QSet<quint32> seen;
	for (int i = 0; i < p->model.count(); ++i)
		seen.insert(p->model.id(i));
	QVector<QPair<quint32, QString>> accepted;
	int duplicates = 0, unsupported = 0, capacity = 0;
	for (const auto& entry : entries) {
		if (!p->current() || p->revision != revision || p->active()) {
			result[QStringLiteral("status")] = QStringLiteral("stale");
			return result;
		}
		if (seen.contains(entry.first)) {
			++duplicates;
			continue;
		}
		seen.insert(entry.first);
		const auto bytes = p->source.fieldBytes(entry.first);
		if (!p->current() || p->revision != revision || p->active()) {
			result[QStringLiteral("status")] = QStringLiteral("stale");
			return result;
		}
		if (!bytes || !*bytes || *bytes > 8 || entry.second.isEmpty()) {
			++unsupported;
			continue;
		}
		if (accepted.size() >= WatchlistModel::maxRows - p->model.count()) {
			++capacity;
			continue;
		}
		accepted.push_back(entry);
	}
	if (!p->current() || p->revision != revision || p->active()) {
		result[QStringLiteral("status")] = QStringLiteral("stale");
		return result;
	}
	if (!accepted.isEmpty() && !p->model.addMany(accepted))
		return result;
	if (!accepted.isEmpty())
		++p->revision;
	result[QStringLiteral("status")] = QStringLiteral("ok");
	result[QStringLiteral("added")] = accepted.size();
	result[QStringLiteral("duplicate")] = duplicates;
	result[QStringLiteral("unsupported")] = unsupported;
	result[QStringLiteral("capacity")] = capacity;
	// Scheduling progress and notifying observers occur after this method's
	// publication guard is released; no request is sent here.
	if (!accepted.isEmpty() && p->context) {
		const std::weak_ptr<Impl> weak = p;
		QMetaObject::invokeMethod(p->context, [weak] {
			if (const auto state = weak.lock())
				state->changed();
		}, Qt::QueuedConnection);
	}
	return result;
}

void DynamicWatchlist::remove(int row)
{
	auto p = d;
	if (!p->guarded() && !p->active() && p->model.remove(row)) {
		++p->revision;
		p->changed();
	}
}

void DynamicWatchlist::clear()
{
	auto p = d;
	if (!p->guarded() && !p->active()) {
		p->model.clear();
		const std::weak_ptr<Impl> weak = p;
		p->model.bind([weak] {
			const auto state = weak.lock();
			return state && state->current();
		});
		++p->revision;
		p->changed();
	}
}

int DynamicWatchlist::count() const
{
	return d->model.count();
}

QString DynamicWatchlist::status() const
{
	return d->status;
}

bool DynamicWatchlist::individual() const
{
	return d->individual;
}

void DynamicWatchlist::setIndividual(bool value)
{
	auto p = d;
	if (!p->guarded() && !p->active()) {
		p->individual = value;
		++p->revision;
		p->changed();
	}
}

int DynamicWatchlist::interval() const
{
	return d->timer.interval();
}

void DynamicWatchlist::setInterval(int ms)
{
	auto p = d;
	if (!p->guarded() && !p->active() && ms >= 1 && ms <= 60000) {
		p->timer.setInterval(ms);
		p->changed();
	}
}

bool DynamicWatchlist::begin(bool polling)
{
	auto p = d;
	if (p->guarded() || p->active() || !p->current() || !p->model.count())
		return false;
	if (p->unavailable()) {
		// Refused for a reason the operator can act on: say it.
		p->status = QStringLiteral("Not started: a Field edit or an operation is in progress");
		p->changed();
		return false;
	}
	p->running = polling;
	p->cycle = true;
	p->cursor = 0;
	if (polling)
		p->timer.start();
	p->next();
	return p->cycle || bool(p->request);
}

void DynamicWatchlist::stop()
{
	auto p = d;
	if (p->guarded())
		return;
	p->running = p->cycle = false;
	p->timer.stop();
	++p->revision;
	p->status = p->request ? QStringLiteral("Stopping - draining accepted read")
	                       : QStringLiteral("Stopped");
	p->changed();
}
} // namespace telemetry::ux
