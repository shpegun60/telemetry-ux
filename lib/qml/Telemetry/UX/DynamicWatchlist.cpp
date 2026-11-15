// One pending read, bounded wire chunks, Qt-owned scalar rows. MIT.
#include "DynamicWatchlist.hpp"
#include "ReadPortions.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QScopedValueRollback>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace telemetry::ux {
struct DynamicWatchlist::Impl : std::enable_shared_from_this<Impl> {
	// One read of a cycle, planned when the cycle starts (ReadPortions.hpp).
	using Portion = read_portions::Portion;

	struct Request {
		std::shared_ptr<void> storage;
		std::uint64_t revision = 0;
		int step = -1, count = 0; // step: the plan portion; -1 when urgent
		qint64 sequence = 0, cycle = 0, portion = 0, sourceBinding = 0, startedNs = 0;
		Portion::Kind kind = Portion::Kind::Many;
		quint32 field = 0;
		bool urgent = false; // a written Field read again, outside the cycle's order
		quint64 membership = 0; // the rows the plan was built from
		std::array<std::uint32_t, 32> ids{};
		std::vector<std::uint32_t> leaves;
		std::optional<WatchlistCompletion> completion;
	};

	QPointer<QObject> context;
	WatchlistModel model;
	QTimer timer;
	// A cycle that waits for a busy session or panel looks again shortly. The
	// session's progress usually resumes it first, but the last completion can
	// arrive while the cycle still saw it busy, and then nothing else would.
	QTimer retry;
	WatchlistSource source;
	std::optional<WatchlistSource> desired;
	std::shared_ptr<Request> request;
	std::function<void()> notify;
	std::function<void(const WatchFrame&)> completed;
	qint64 sequence = 0, cycleNumber = 0, portion = 0, sourceBinding = 0;
	bool reportingCompletion = false;
	std::function<bool()> blocked;
	QString status = QStringLiteral("ReadMany; not started");
	std::uint64_t revision = 0;
	// The cycle's plan; cursor is the next portion and done the rows read.
	// replan: the rows changed since the plan was built; the cycle is planned
	// again at the next portion boundary, never under a read in flight.
	std::vector<Portion> plan;
	bool replan = false, startSoon = false;
	quint64 planMembership = 0;
	QHash<quint32, int> planOf;
	int cursor = 0, done = 0;
	bool hasMembers = false;
	// The source's schema, copied on attachment: what a member key names.
	WatchLeaves leaves;
	struct FieldShape {
		TypeId type = 0;
		quint32 bytes = 0;
		bool aggregate = false; // a struct or an array: watched value by value
		QString name;           // the Field's own name: "Config"
		QString group;          // its catalog's name: "device"
		bool readable = true;   // a write-only Field can't be watched
	};
	QHash<quint32, FieldShape> fields;
	// Rows whose path has no device yet are numbered from here.
	mutable quint64 nextUnresolved = 1;
	// The Watchlist itself, while it lives: rows are found again from here.
	DynamicWatchlist* owner = nullptr;
	// The Commands and Services by path, for a typed path's words.
	QHash<QString, descriptor::Category> others;
	bool enabled = false, attached = false, running = false, cycle = false, individual = false;
	bool publishing = false, queued = false, retiring = false, clearRetiredRows = false;
	bool deferredQueued = false;
	// Written Fields to read again before the cycle goes on, and whether the
	// next cycle follows this one at once.
	std::vector<quint32> urgent;
	bool rerun = false;
	// 0 reads continuously: each cycle follows the last, still one portion at
	// a time and only while nothing else is in flight. The timer then only
	// backs it up.
	int intervalMs = 1000;

	explicit Impl(QObject* owner) : context(owner)
	{
		timer.setInterval(1000);
		timer.setTimerType(Qt::PreciseTimer);
		timer.setObjectName(QStringLiteral("dynamicConfiguratorWatchTimer"));
		retry.setSingleShot(true);
		retry.setInterval(10);
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

	QString rowsWord() const
	{
		return hasMembers ? QStringLiteral("values") : QStringLiteral("Fields");
	}

	bool startCycle()
	{
		if (cycleNumber == (std::numeric_limits<qint64>::max)()) {
			fail(QStringLiteral("Watch cycle identities exhausted"));
			return false;
		}
		++cycleNumber;
		portion = 0;
		cycle = true;
		cursor = 0;
		done = 0;
		buildPlan();
		return true;
	}

	// The rows of a cycle in portions (ReadPortions.hpp), planned from the
	// keys in row order.
	void buildPlan()
	{
		replan = false;
		planMembership = model.membership();
		std::vector<WatchKey> keys;
		keys.reserve(std::size_t(model.count()));
		for (int row = 0; row < model.count(); ++row)
			keys.push_back(model.key(row));
		read_portions::Source can;
		can.individual = individual;
		can.readMany = bool(source.readMany);
		can.readField = bool(source.readField);
		can.readMembers = bool(source.readMembers);
		if (source.capability)
			can.budget = [this]() -> std::optional<read_portions::Budget> {
				const auto limits = source.capability();
				if (!limits)
					return std::nullopt;
				return read_portions::Budget{limits->items, limits->requestBytes, limits->replyBytes};
			};
		can.fieldBytes = source.fieldBytes;
		auto planned = read_portions::plan(keys, can);
		plan = std::move(planned.portions);
		planOf = std::move(planned.of);
		hasMembers = planned.members;
	}

	// A plan's stop in words.
	static QString failureText(read_portions::Failure failure)
	{
		switch (failure) {
			case read_portions::Failure::NoMembers:
				return QStringLiteral("This connection can't read struct members.");
			case read_portions::Failure::NoFieldRead:
				return QStringLiteral("Individual FieldRead unavailable");
			case read_portions::Failure::NoReadMany:
				return QStringLiteral(
				    "This device can't read several Fields at once: choose Read each Field with FieldRead in the Watchlist's menu.");
			case read_portions::Failure::NotScalar:
				return QStringLiteral("Watch Field is stale or not a readable scalar");
			case read_portions::Failure::ExceedsReadMany:
				return QStringLiteral("Next Field exceeds ReadMany byte budget - select Individual FieldRead");
			case read_portions::Failure::None:
				break;
		}
		return {};
	}

	// The rows changed (added, removed, put back, cleared). Mid-cycle the plan
	// follows at the next portion; between two cycles of polling an addition
	// starts the next cycle now, a removal waits for the timer.
	void membershipChanged(bool added)
	{
		if (cycle) {
			replan = true;
		} else if (running && added && context) {
			const std::weak_ptr<Impl> weak = shared_from_this();
			QMetaObject::invokeMethod(
			    context,
			    [weak] {
				    const auto p = weak.lock();
				    if (p && p->running && !p->cycle && !p->request && !p->guarded() && p->current() &&
				        p->startCycle())
					    p->continueCycle();
			    },
			    Qt::QueuedConnection);
		}
		changed();
	}

	bool prepare(Request& value)
	{
		if (sequence == (std::numeric_limits<qint64>::max)()) {
			fail(QStringLiteral("Watch completion identities exhausted"));
			return false;
		}
		value.sequence = sequence + 1;
		value.cycle = cycleNumber;
		value.portion = portion;
		value.sourceBinding = sourceBinding;
		value.startedNs = WatchTimestamp::now().monotonicNs;
		value.membership = planMembership;
		return true;
	}

	void accepted(const Request& value)
	{
		sequence = value.sequence;
		++portion;
	}

	client::Submission submit(Request& pending, WatchlistSource::Completion complete)
	{
		switch (pending.kind) {
			case Portion::Kind::Many:
				return source.readMany(std::span<const std::uint32_t>{pending.ids}.first(std::size_t(pending.count)),
				                       std::move(complete));
			case Portion::Kind::Field:
				return source.readField(pending.field, std::move(complete));
			case Portion::Kind::Members:
				return source.readMembers(pending.field, pending.leaves, std::move(complete));
			case Portion::Kind::Fail:
				break;
		}
		return {client::Status::InvalidArgument, {}};
	}

	bool copySchemaMetadata()
	{
		const auto binding = sourceBinding;
		const auto epoch = revision;
		// A user-supplied callback can retire/replace its source. Retain the
		// callable itself, then reject its returned schema if that happened.
		const auto schema = source.schema;
		const auto metadata = current() && schema ? schema() : runtime::RuntimeSchema{};
		if (!context || binding != sourceBinding || epoch != revision) return false;
		leaves.reset(metadata);
		fields.clear();
		others.clear();
		if (metadata.valid()) {
			for (const auto category : {descriptor::Category::Command, descriptor::Category::Service}) {
				const auto groups = metadata.catalogs(category);
				for (const auto& endpoint : metadata.endpoints(category))
					if (endpoint.catalogIndex < groups.size())
						others.insert(QString::fromUtf8(groups[endpoint.catalogIndex].name.data(),
						                                qsizetype(groups[endpoint.catalogIndex].name.size())) +
						                  QLatin1Char('.') +
						                  QString::fromUtf8(endpoint.name.data(), qsizetype(endpoint.name.size())),
						              category);
			}
			const auto catalogs = metadata.catalogs(descriptor::Category::Field);
			for (const auto& endpoint : metadata.endpoints(descriptor::Category::Field)) {
				const auto* type = metadata.type(endpoint.typeId);
				const auto group = endpoint.catalogIndex < catalogs.size()
				                       ? QString::fromUtf8(catalogs[endpoint.catalogIndex].name.data(),
				                                           qsizetype(catalogs[endpoint.catalogIndex].name.size()))
				                       : QString{};
				fields.insert(endpoint.id, {endpoint.typeId, type ? type->wireBytes : 0u,
				                            type && (type->kind == TypeKind::Struct || type->kind == TypeKind::Array),
				                            QString::fromUtf8(endpoint.name.data(), qsizetype(endpoint.name.size())),
				                            group,
				                            (endpoint.capabilities &
				                             static_cast<std::uint8_t>(descriptor::Capability::Readable)) != 0});
			}
		}
		model.setSchemaMetadata(metadata);
		return context && binding == sourceBinding && epoch == revision;
	}

	// Whether the schema says this Field is a struct or an array: watched by
	// its values only, never whole, whatever fieldBytes says of its size.
	bool aggregateField(quint32 id) const
	{
		const auto shape = fields.constFind(id);
		return shape != fields.cend() && shape->aggregate;
	}

	// A value inside a struct or array Field this source can read: its member
	// text and type filled in from the key.
	bool memberEntry(WatchEntry& entry) const
	{
		if (!source.readMembers)
			return false;
		const auto shape = fields.constFind(watch_key::field(entry.key));
		if (shape == fields.cend() || !shape->aggregate || shape->bytes == 0 ||
		    shape->bytes > workbenchSnapshotBytes)
			return false;
		const auto leaf = leaves.locate(shape->type, watch_key::ordinal(entry.key));
		if (!leaf || (entry.type && entry.type != leaf->type))
			return false;
		entry.type = leaf->type;
		entry.steps = QVector<TypeId>(leaf->types.cbegin(), leaf->types.cbegin() + leaf->depth);
		if (entry.member.isEmpty())
			entry.member = leaf->member();
		scope(entry, *shape);
		return entry.member == leaf->member();
	}

	// What a write of this value covers: the whole Field and its other values.
	void scope(WatchEntry& entry, const FieldShape& shape) const
	{
		entry.scope = shape.name;
		const auto leaves = this->leaves.count(shape.type);
		entry.others = leaves ? leaves - 1 : 0;
		switch (this->leaves.kind(shape.type)) {
			case WatchLeaves::Kind::Members:
				entry.kind = WatchEntry::Kind::Members;
				break;
			case WatchLeaves::Kind::Elements:
				entry.kind = WatchEntry::Kind::Elements;
				break;
			case WatchLeaves::Kind::Values:
				entry.kind = WatchEntry::Kind::Values;
				break;
		}
	}

	void attach(WatchlistSource value)
	{
		if (sourceBinding == (std::numeric_limits<qint64>::max)()) {
			attached = false;
			source = {};
			fail(QStringLiteral("Watch source identities exhausted"));
			return;
		}
		++sourceBinding;
		source = std::move(value);
		attached = true;
		const std::weak_ptr<Impl> weak = shared_from_this();
		model.bind([weak] {
			const auto p = weak.lock();
			return p && p->current();
		});
		if (!copySchemaMetadata()) return;
		status = current()           ? QStringLiteral("ReadMany; not started")
		         : model.count() > 0 ? keptWithoutDevice()
		                             : QStringLiteral("Not connected");
		if (owner && current())
			owner->rebindRows();
		// Start is off then; the status says why.
		if (current() && model.count() > 0 && !model.hasBound())
			status = QStringLiteral("Nothing to read: none of these paths is on this device.");
	}

	// One line for the whole Watchlist while it has no device (WL-4c), rather
	// than a warning on every row.
	static QString keptWithoutDevice()
	{
		return QStringLiteral("No device. The values are the last read; each row is read again on a device that has it.");
	}

	void retire(bool clearRows)
	{
		running = cycle = false;
		urgent.clear();
		rerun = false;
		timer.stop();
		++revision;
		if (clearRows)
			attached = false;
		publishing = true;
		if (clearRows)
			nextUnresolved = model.unbindAll(nextUnresolved);
		else
			model.invalidateValues();
		publishing = false;
		if (clearRows && !request)
			source = {};
		status = request                           ? QStringLiteral("Stopping - draining accepted read")
		         : clearRows && model.count() > 0 ? keptWithoutDevice()
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

	void wake(std::optional<quint32> written)
	{
		// A Read once in progress still reads a written Field again; only
		// polling starts or repeats cycles.
		if (guarded() || !current() || (!running && !cycle))
			return;
		if (!cycle) {
			// Between cycles: the next one starts now, not on the timer. It sends
			// nothing while the session is busy (a write that caused this wake is
			// often still queued); the session's progress drives it on.
			if (startCycle())
				continueCycle();
			return;
		}
		if (written) {
			// Portions before the cursor were read before the write; so was a
			// portion in flight, sent while the write waited. Later ones are read
			// after it.
			const auto at = planOf.value(*written, -1);
			const int reached = cursor + (request && !request->urgent ? 1 : 0);
			if (at >= 0 && at < reached &&
			    std::find(urgent.begin(), urgent.end(), *written) == urgent.end())
				urgent.push_back(*written);
		} else if (running) {
			rerun = true;
		}
		continueCycle();
	}

	void dataChanged()
	{
		if (guarded()) {
			// Called inside a publication: try again once it has returned.
			const std::weak_ptr<Impl> weak = shared_from_this();
			if (context)
				QMetaObject::invokeMethod(
				    context,
				    [weak] {
					    if (auto p = weak.lock())
						    p->dataChanged();
				    },
				    Qt::QueuedConnection);
			return;
		}
		if (running) {
			wake(std::nullopt);
			return;
		}
		// A Read once in progress drops a portion sent before the change (its
		// completion is drained) and reads those rows again; rows it has
		// already read are shown as stale.
		if (cycle && request)
			++revision;
		publishing = true;
		model.invalidateValues();
		publishing = false;
		changed();
	}

	int rowOf(quint32 id) const
	{
		return model.rowOf(watch_key::whole(id));
	}

	// One written Field per request: a whole Field in the cycle's own mode,
	// a struct or array Field as its values' read. True when a read went out
	// or the session asked to wait; the cycle then waits for it.
	bool submitUrgent()
	{
		while (!urgent.empty()) {
			const auto id = urgent.front();
			const auto at = planOf.value(id, -1);
			const bool members = at >= 0 && at < int(plan.size()) &&
			                     plan[std::size_t(at)].kind == Portion::Kind::Members;
			if (!members && rowOf(id) < 0) {
				urgent.erase(urgent.begin());
				continue;
			}
			if (members ? !source.readMembers
			    : individual ? !source.readField
			                 : (!source.readMany || !source.capability || !source.capability())) {
				urgent.clear(); // the cycle's own read reports the missing capability
				return false;
			}
			auto pending = std::make_shared<Request>();
			pending->storage = source.storage;
			pending->revision = revision;
			pending->urgent = true;
			pending->field = id;
			if (members) {
				pending->kind = Portion::Kind::Members;
				pending->leaves = plan[std::size_t(at)].leaves;
				pending->count = int(pending->leaves.size());
			} else {
				pending->kind = individual ? Portion::Kind::Field : Portion::Kind::Many;
				pending->ids[0] = id;
				pending->count = 1;
			}
			if (!prepare(*pending)) return true;
			auto complete = WatchlistSource::Completion{pending, &pending->completion};
			const auto submitted = submit(*pending, std::move(complete));
			if (!submitted) {
				if (submitted.status == client::Status::Busy ||
				    submitted.status == client::Status::QueueFull) {
					retry.start(); // tried again shortly
					return true;
				}
				urgent.erase(urgent.begin());
				continue;
			}
			urgent.erase(urgent.begin());
			accepted(*pending);
			request = std::move(pending);
			status = QStringLiteral("Reading a written Field again");
			changed();
			return true;
		}
		return false;
	}

	void fail(QString message)
	{
		urgent.clear();
		rerun = false;
		running = cycle = false;
		timer.stop();
		status = std::move(message);
		changed();
	}

	void next()
	{
		if (guarded() || reportingCompletion || !cycle || request)
			return;
		if (unavailable()) {
			retry.start();
			return;
		}
		if (!current()) {
			retire(true);
			changed();
			return;
		}
		if (source.busy && source.busy()) {
			retry.start(); // No accumulation behind foreground/other panels.
			return;
		}
		if (replan) {
			// The rows changed: a new cycle over the rows shown now, which
			// reads every row, so written Fields need no read of their own.
			urgent.clear();
			rerun = false;
			if (!startCycle())
				return;
		}
		if (submitUrgent())
			return;
		if (cursor >= int(plan.size())) {
			cycle = false;
			// Rows kept for paths this device lacks are never read (WL-4c).
			const bool readable = model.hasBound();
			status = model.count() == 0
			             ? QStringLiteral("Nothing to read. Add a value, or Stop.")
			             : !readable
			             ? QStringLiteral("Nothing to read: none of these paths is on this device.")
			             : QStringLiteral("%1 complete - %2 %3")
			                   .arg(individual ? QStringLiteral("Individual FieldRead")
			                                   : QStringLiteral("ReadMany"))
			                   .arg(model.count())
			                   .arg(rowsWord());
			changed();
			// A list with nothing to read waits for the timer rather than spin.
			if ((rerun || intervalMs == 0) && running && !guarded() && readable) {
				rerun = false;
				if (startCycle())
					continueCycle();
			}
			return;
		}
		const auto& step = plan[std::size_t(cursor)];
		if (step.kind == Portion::Kind::Fail) {
			fail(failureText(step.failure));
			return;
		}
		auto pending = std::make_shared<Request>();
		pending->storage = source.storage;
		pending->revision = revision;
		pending->step = cursor;
		pending->kind = step.kind;
		pending->field = step.field;
		pending->count = step.rows();
		if (step.kind == Portion::Kind::Many)
			std::copy(step.ids.begin(), step.ids.end(), pending->ids.begin());
		else if (step.kind == Portion::Kind::Field)
			pending->ids[0] = step.field;
		else
			pending->leaves = step.leaves;
		if (!prepare(*pending)) return;
		// An aliasing shared pointer retains the immutable request and its storage.
		// The native adapter can only fill this slot, never call a QObject callback.
		auto complete = WatchlistSource::Completion{pending, &pending->completion};
		const auto submitted = submit(*pending, std::move(complete));
		if (!submitted) {
			if (submitted.status == client::Status::Busy ||
			    submitted.status == client::Status::QueueFull) {
				status = QStringLiteral("Waiting for the shared session");
				retry.start();
				changed();
			} else
				fail(
				    QStringLiteral("Watch submission refused: %1").arg(unsigned(submitted.status)));
			return;
		}
		accepted(*pending);
		request = std::move(pending);
		status = QStringLiteral("Reading %1 %2-%3 of %4")
		             .arg(rowsWord())
		             .arg(done + 1)
		             .arg(done + request->count)
		             .arg(model.count());
		changed();
	}

	WatchFrame frameOf(const Request& completed, const WatchlistCompletion& result) const
	{
		WatchFrame frame;
		frame.sequence = completed.sequence;
		frame.cycle = completed.cycle;
		frame.portion = completed.portion;
		frame.sourceBinding = completed.sourceBinding;
		frame.startedNs = completed.startedNs;
		frame.completed = result.completed;
		frame.source = result.source;
		frame.native = result.native;
		frame.presentationStatus = qint32(result.status);
		frame.operation = completed.kind == Portion::Kind::Many ? WatchOperation::ReadMany
		                                                        : WatchOperation::FieldRead;
		frame.urgent = completed.urgent;
		frame.membership = completed.membership;
		return frame;
	}

	static void copyRow(WatchFrameRow& row, const WatchlistCompletion::Row& value)
	{
		row.itemStatus = value.itemStatus;
		row.valueStatus = value.valueStatus;
		row.value = value.value;
	}

	static bool exactRow(const WatchFrameRow& row)
	{
		return row.value.type.valid() &&
		       !(row.itemStatus == 0 && row.valueStatus == 0 && row.value.size != row.value.type.width);
	}

	// Publishes a frame to the observer; false when the observer stopped,
	// rebound or retired this Watchlist (the caller then returns at once).
	bool report(const WatchFrame& frame)
	{
		const auto before = revision;
		const auto observer = this->completed;
		reportingCompletion = true;
		if (observer) observer(frame);
		reportingCompletion = false;
		return context && before == revision;
	}

	void stopFailed(client::Status status)
	{
		running = cycle = false;
		timer.stop();
		publishing = true;
		model.invalidateValues();
		publishing = false;
		this->status = QStringLiteral("Watch read failed: %1").arg(unsigned(status));
	}

	// A struct or array Field's values: published as frames of at most 32 rows,
	// all of the same cycle and portion, then shown row by row.
	bool completeMembers(Request& completed, WatchlistCompletion& result)
	{
		const auto n = completed.leaves.size();
		bool correlated = result.count == int(n) && result.members.size() == n && n > 0;
		for (std::size_t i = 0; correlated && i < n; ++i)
			correlated = result.members[i].id == completed.field &&
			             result.members[i].leaf == completed.leaves[i] + 1;
		bool applicable = result.status == client::Status::Ok && correlated;
		if (completed.urgent)
			applicable = applicable && planOf.value(completed.field, -1) >= 0;
		else
			applicable = applicable && completed.step == cursor;
		const auto phase = completed.revision != revision || !current() ? WatchCompletionPhase::Drained
		                   : applicable ? WatchCompletionPhase::Current : WatchCompletionPhase::Failed;
		for (std::size_t first = 0; first < n; first += 32) {
			auto frame = frameOf(completed, result);
			if (first) {
				// A frame of its own: the next completion identity.
				if (sequence == (std::numeric_limits<qint64>::max)()) {
					fail(QStringLiteral("Watch completion identities exhausted"));
					return false;
				}
				frame.sequence = ++sequence;
			}
			frame.phase = phase;
			frame.count = int((std::min)(std::size_t{32}, n - first));
			frame.correlated = correlated;
			frame.exact = true;
			for (int i = 0; i < frame.count; ++i) {
				auto& row = frame.rows[std::size_t(i)];
				row.id = completed.field;
				row.leaf = completed.leaves[first + std::size_t(i)] + 1;
				if (correlated)
					copyRow(row, result.members[first + std::size_t(i)]);
				if (!exactRow(row))
					frame.exact = false;
			}
			if (!report(frame))
				return false;
		}
		if (completed.revision != revision || !current()) {
			status = QStringLiteral("Stopped - accepted read drained");
			return true;
		}
		if (applicable) {
			publishing = true;
			for (std::size_t i = 0; context && i < n; ++i) {
				const auto row = model.rowOf(watch_key::member(completed.field, completed.leaves[i]));
				if (row < 0)
					continue;
				auto& value = result.members[i];
				model.setResult(row, std::move(value.status), std::move(value.text), value.sample, value.value);
			}
			publishing = false;
			if (!completed.urgent) {
				++cursor;
				done += int(n);
			}
		} else if (completed.urgent) {
			if (running)
				rerun = true; // the next cycle reads it again at once
		} else {
			stopFailed(result.status);
		}
		return true;
	}

	void poll()
	{
		if (guarded() || reportingCompletion)
			return;
		// Rows kept across a disconnect are unbound once, not on every poll.
		if (!current() && (running || cycle || model.hasBound()))
			retire(true);
		if (request && request->completion) {
			auto completed = std::move(request);
			auto result = std::move(*completed->completion);
			if (completed->kind == Portion::Kind::Members) {
				if (!completeMembers(*completed, result))
					return;
			} else {
				// The accepted Request, not a possibly empty failure body, owns the IDs.
				auto frame = frameOf(*completed, result);
				frame.count = completed->count;
				frame.correlated = result.count == completed->count && result.count > 0 && result.count <= 32;
				frame.exact = true;
				for (int i = 0; i < frame.count; ++i) {
					auto& row = frame.rows[std::size_t(i)];
					const auto& value = result.rows[std::size_t(i)];
					row.id = completed->ids[std::size_t(i)];
					if (value.id == row.id && value.leaf == 0)
						copyRow(row, value);
					else
						frame.correlated = false;
					if (!exactRow(row)) frame.exact = false;
				}
				// Rows that left the list while the read was out are skipped below;
				// the read itself still answered what it was asked.
				bool applicable = result.status == client::Status::Ok && frame.correlated;
				if (!completed->urgent)
					applicable = applicable && completed->step == cursor;
				frame.phase = completed->revision != revision || !current() ? WatchCompletionPhase::Drained
				              : applicable ? WatchCompletionPhase::Current : WatchCompletionPhase::Failed;
				// No native callback or row notification is on this stack. A receiver can
				// stop/rebind/delete the panel; retain Impl and revalidate before applying.
				if (!report(frame)) return;
				if (completed->revision != revision || !current()) {
					status = QStringLiteral("Stopped - accepted read drained");
				} else if (completed->urgent) {
					// Matched by id: rows may have moved since it was sent. A failed
					// read leaves the row to the next cycle.
					const auto row = rowOf(completed->ids[0]);
					if (result.status == client::Status::Ok && result.count == 1 && row >= 0 &&
					    result.rows[0].id == completed->ids[0]) {
						publishing = true;
						model.setResult(row, std::move(result.rows[0].status),
						                std::move(result.rows[0].text), result.rows[0].sample,
						                result.rows[0].value);
						publishing = false;
					} else if (running) {
						rerun = true; // the next cycle reads it again at once
					}
				} else {
					bool valid = result.status == client::Status::Ok &&
					             result.count == completed->count && result.count > 0 &&
					             result.count <= 32 && completed->step == cursor;
					for (int i = 0; valid && i < result.count; ++i)
						valid = result.rows[i].id == completed->ids[i];
					if (!valid) {
						stopFailed(result.status);
					} else {
						publishing = true;
						for (int i = 0; context && i < result.count; ++i) {
							const auto row = rowOf(completed->ids[std::size_t(i)]);
							if (row >= 0)
								model.setResult(row, std::move(result.rows[i].status),
								                std::move(result.rows[i].text), result.rows[i].sample,
								                result.rows[i].value);
						}
						publishing = false;
						++cursor;
						done += result.count;
					}
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

	// A tick starts the next cycle even while the session or this panel is
	// busy: next() sends nothing until it is free, and the session's progress
	// resumes it. Skipping the tick instead would starve a Watchlist that
	// shares the session with a continuous one.
	void tick()
	{
		if (guarded() || reportingCompletion || !running || cycle || request || !current())
			return;
		if (startCycle())
			next();
	}
};

DynamicWatchlist::DynamicWatchlist(QObject* context) : d(std::make_shared<Impl>(context))
{
	d->owner = this;
	const std::weak_ptr<Impl> weak = d;
	QObject::connect(&d->timer, &QTimer::timeout, context, [weak] {
		if (auto p = weak.lock())
			p->tick();
	});
	QObject::connect(&d->retry, &QTimer::timeout, context, [weak] {
		if (auto p = weak.lock())
			p->next();
	});
}

DynamicWatchlist::~DynamicWatchlist()
{
	d->owner = nullptr;
}

WatchEntry DynamicWatchlist::missingEntry(const QString& path) const
{
	return WatchEntry::of(watch_key::unresolved(d->nextUnresolved++), path.trimmed());
}

bool DynamicWatchlist::readable() const
{
	return d->model.hasBound();
}

int DynamicWatchlist::unboundRow(const QString& path) const
{
	const auto wanted = path.trimmed();
	for (int row = 0; row < d->model.count(); ++row)
		if (watch_key::isUnresolved(d->model.key(row)) &&
		    d->model.data(d->model.index(row, 0), WatchlistModel::NameRole).toString() == wanted)
			return row;
	return -1;
}

void DynamicWatchlist::rebindRows()
{
	auto p = d;
	if (p->guarded() || !canTellHoldable())
		return;
	// Every row's values first, from the top: a value two rows name goes to the
	// upper one, so a struct typed whole below its own members takes the rest;
	// a row whose values do not fit in the room left claims none of them.
	struct Found {
		int row = 0;
		QString path;
		bool missing = false;
		QString reason;  // on the device, but nothing to watch
		QVector<WatchEntry> entries;
	};
	QVector<Found> found;
	auto claimed = held();
	int room = WatchlistModel::maxRows - p->model.count();
	for (int row = 0; row < p->model.count(); ++row) {
		if (!watch_key::isUnresolved(p->model.key(row)))
			continue;
		const auto path = p->model.data(p->model.index(row, 0), WatchlistModel::NameRole).toString();
		const auto resolved = resolvePath(path);
		Found next{row, path, resolved.missing || !resolved.error.isEmpty(),
		           resolved.missing ? QString{} : resolved.error, {}};
		if (!next.missing) {
			QVector<WatchEntry> mine;
			for (const auto& entry : resolved.entries)
				if (!claimed.contains(entry.key))
					mine.push_back(entry);
			// The row itself becomes the first value.
			const int grows = static_cast<int>(mine.size()) - 1;
			if (grows > room) {
				next.missing = true;
				next.reason = QStringLiteral("Its %L1 values do not fit: the Watchlist holds %L2.")
				                  .arg(mine.size())
				                  .arg(WatchlistModel::maxRows);
			} else {
				room -= grows;
				for (const auto& entry : std::as_const(mine))
					claimed.insert(entry.key);
				next.entries = std::move(mine);
			}
		}
		found.push_back(std::move(next));
	}
	// Then from the last row up, so a row that becomes several moves none above
	// it; publishing meanwhile, so a slot can only ask for a change afterwards.
	QScopedValueRollback<bool> publishing(p->publishing, true);
	for (auto at = found.crbegin(); at != found.crend(); ++at) {
		if (at->missing)
			p->model.markMissing(at->row, at->path, at->reason);
		else
			p->model.rebindRow(at->row, at->path, at->entries);
	}
}

void DynamicWatchlist::setNotify(std::function<void()> notify, std::function<bool()> blocked)
{
	d->notify = std::move(notify);
	d->blocked = std::move(blocked);
}

void DynamicWatchlist::setCompletionObserver(std::function<void(const WatchFrame&)> observer)
{
	d->completed = std::move(observer);
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
	if (p->guarded() || p->reportingCompletion || !p->current() || !p->source.fieldBytes ||
	    p->model.count() == WatchlistModel::maxRows)
		return false;
	const auto bytes = p->source.fieldBytes(id);
	if (!bytes || !*bytes || *bytes > 8 || p->aggregateField(id))
		return false;
	return p->model.rowOf(watch_key::whole(id)) < 0;
}

bool DynamicWatchlist::add(quint32 id, QString name)
{
	auto p = d;
	if (!canAdd(id))
		return false;
	const bool added = p->model.add(id, std::move(name));
	if (added)
		p->membershipChanged(true);
	return added;
}

QVariantMap DynamicWatchlist::addMany(const QVector<QPair<quint32, QString>>& entries)
{
	QVector<WatchEntry> whole;
	whole.reserve(entries.size());
	for (const auto& entry : entries)
		whole.push_back(WatchEntry::of(watch_key::whole(entry.first), entry.second));
	return addEntries(whole);
}

QVariantMap DynamicWatchlist::addEntries(const QVector<WatchEntry>& entries)
{
	auto p = d;
	QVariantMap result{{QStringLiteral("status"), QStringLiteral("refused")},
	                   {QStringLiteral("added"), 0}, {QStringLiteral("duplicate"), 0},
	                   {QStringLiteral("unsupported"), 0}, {QStringLiteral("capacity"), 0}};
	if (p->guarded() || p->reportingCompletion || !p->current() || !p->source.fieldBytes)
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
	QSet<WatchKey> seen;
	for (int i = 0; i < p->model.count(); ++i)
		seen.insert(p->model.key(i));
	QVector<WatchEntry> accepted;
	int duplicates = 0, unsupported = 0, capacity = 0;
	for (auto entry : entries) {
		if (!p->current() || p->revision != revision) {
			result[QStringLiteral("status")] = QStringLiteral("stale");
			return result;
		}
		if (seen.contains(entry.key)) {
			++duplicates;
			continue;
		}
		seen.insert(entry.key);
		bool holdable = false;
		if (watch_key::isUnresolved(entry.key)) {
			// A path this device lacks: kept as a row that says so.
			holdable = true;
		} else if (watch_key::isWhole(entry.key)) {
			const auto bytes = p->source.fieldBytes(watch_key::field(entry.key));
			if (!p->current() || p->revision != revision) {
				result[QStringLiteral("status")] = QStringLiteral("stale");
				return result;
			}
			holdable = bytes && *bytes && *bytes <= 8 && !p->aggregateField(watch_key::field(entry.key));
			entry.member.clear();
			entry.type = 0;
		} else {
			holdable = p->memberEntry(entry);
		}
		if (!holdable || entry.name.isEmpty()) {
			++unsupported;
			continue;
		}
		if (accepted.size() >= WatchlistModel::maxRows - p->model.count()) {
			++capacity;
			continue;
		}
		accepted.push_back(std::move(entry));
	}
	if (!p->current() || p->revision != revision) {
		result[QStringLiteral("status")] = QStringLiteral("stale");
		return result;
	}
	if (!accepted.isEmpty() && !p->model.addMany(accepted))
		return result;
	if (!accepted.isEmpty()) {
		if (p->cycle)
			p->replan = true;
		else if (p->running)
			p->startSoon = true;
	}
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
			if (const auto state = weak.lock()) {
				// Polling between two cycles: the added rows are read now.
				if (std::exchange(state->startSoon, false))
					state->membershipChanged(true);
				else
					state->changed();
			}
		}, Qt::QueuedConnection);
	}
	return result;
}

void DynamicWatchlist::remove(int row)
{
	auto p = d;
	if (!p->guarded() && !p->reportingCompletion && p->model.remove(row))
		p->membershipChanged(false);
}

void DynamicWatchlist::clear()
{
	auto p = d;
	if (!p->guarded() && !p->reportingCompletion) {
		p->model.clear();
		const std::weak_ptr<Impl> weak = p;
		p->model.bind([weak] {
			const auto state = weak.lock();
			return state && state->current();
		});
		if (!p->copySchemaMetadata()) return;
		p->membershipChanged(false);
	}
}

bool DynamicWatchlist::canTellHoldable() const
{
	return !d->guarded() && d->current() && d->source.fieldBytes;
}

std::optional<QHash<quint32, WatchHolding>> DynamicWatchlist::holdable(const QSet<quint32>& ids) const
{
	auto p = d;
	if (!canTellHoldable())
		return std::nullopt;
	QHash<quint32, WatchHolding> found;
	if (ids.isEmpty())
		return found;
	if (p->source.holdable) {
		const auto all = p->source.holdable();
		for (const auto id : ids)
			if (all.contains(id) && !p->aggregateField(id))
				found.insert(id, {});
	} else {
		for (const auto id : ids) {
			const auto bytes = p->source.fieldBytes(id);
			if (bytes && *bytes && *bytes <= 8 && !p->aggregateField(id))
				found.insert(id, {});
		}
	}
	if (!p->source.readMembers)
		return found;
	for (const auto id : ids) {
		const auto shape = p->fields.constFind(id);
		if (found.contains(id) || shape == p->fields.cend() || !shape->aggregate || shape->bytes == 0 ||
		    shape->bytes > workbenchSnapshotBytes)
			continue;
		if (const auto leaves = p->leaves.count(shape->type); leaves > 0)
			found.insert(id, {true, leaves});
	}
	return found;
}

std::optional<QVector<WatchEntry>> DynamicWatchlist::entries(quint32 field, quint32 first, quint32 count,
                                                             const QString& fieldPath) const
{
	auto p = d;
	if (!canTellHoldable() || !p->source.readMembers)
		return std::nullopt;
	const auto shape = p->fields.constFind(field);
	if (shape == p->fields.cend() || !shape->aggregate ||
	    quint64{first} + count > p->leaves.count(shape->type))
		return std::nullopt;
	QVector<WatchEntry> found;
	for (quint64 ordinal = first; ordinal < quint64{first} + count; ++ordinal) {
		const auto key = watch_key::member(field, quint32(ordinal));
		if (p->model.rowOf(key) >= 0)
			continue;
		const auto leaf = p->leaves.locate(shape->type, quint32(ordinal));
		if (!leaf)
			return std::nullopt;
		auto entry = WatchEntry::of(key, fieldPath + leaf->suffix, leaf->member(), leaf->type);
		p->scope(entry, *shape);
		found.push_back(std::move(entry));
	}
	return found;
}

std::optional<WatchLeaves::Leaf> DynamicWatchlist::leaf(WatchKey key) const
{
	auto p = d;
	if (watch_key::isWhole(key) || !p->current())
		return std::nullopt;
	const auto shape = p->fields.constFind(watch_key::field(key));
	if (shape == p->fields.cend() || !shape->aggregate)
		return std::nullopt;
	return p->leaves.locate(shape->type, watch_key::ordinal(key));
}

bool DynamicWatchlist::readsMembers() const
{
	return bool(d->source.readMembers);
}

std::pair<DynamicWatchlist::Resolution, WatchEntry>
DynamicWatchlist::resolve(const QString& group, const QString& name, const QString& member) const
{
	auto p = d;
	if (!canTellHoldable())
		return {Resolution::Missing, {}};
	quint32 found = 0;
	int matches = 0;
	for (auto it = p->fields.cbegin(); it != p->fields.cend(); ++it)
		if (it->group == group && it->name == name) {
			found = it.key();
			++matches;
		}
	if (matches == 0)
		return {Resolution::Missing, {}};
	if (matches > 1)
		return {Resolution::Ambiguous, {}};
	const auto& shape = p->fields[found];
	const auto path = group.isEmpty() ? name : group + QLatin1Char('.') + name;
	if (member.isEmpty()) {
		const auto bytes = p->source.fieldBytes(found);
		if (!bytes || !*bytes || *bytes > 8)
			return {Resolution::Unwatchable, {}};
		return {Resolution::Ok, WatchEntry::of(watch_key::whole(found), path)};
	}
	const auto ordinal = shape.aggregate ? p->leaves.ordinal(shape.type, member) : std::nullopt;
	if (!ordinal)
		return {Resolution::Missing, {}};
	auto entry = WatchEntry::of(watch_key::member(found, *ordinal), {}, member);
	if (!p->memberEntry(entry))
		return {Resolution::Unwatchable, {}};
	const auto leaf = p->leaves.locate(shape.type, *ordinal);
	entry.name = path + (leaf ? leaf->suffix : QString{});
	return {Resolution::Ok, entry};
}

namespace {
QString words(const char* text)
{
	return QCoreApplication::translate("DynamicWatchlist", text);
}
// "device.Config.Gain" as group, Field and member text; nullopt for text
// that is not a path.
struct PathParts {
	QString group, name, member;
};
std::optional<PathParts> splitPath(const QString& text)
{
	const auto dot = text.indexOf(QLatin1Char('.'));
	if (dot <= 0 || dot + 1 >= text.size())
		return std::nullopt;
	PathParts parts;
	parts.group = text.left(dot);
	auto end = dot + 1;
	while (end < text.size() && text.at(end) != QLatin1Char('.') && text.at(end) != QLatin1Char('['))
		++end;
	parts.name = text.mid(dot + 1, end - dot - 1);
	if (parts.name.isEmpty())
		return std::nullopt;
	parts.member = end < text.size() && text.at(end) == QLatin1Char('.') ? text.mid(end + 1) : text.mid(end);
	if (end < text.size() && text.at(end) == QLatin1Char('.') && parts.member.isEmpty())
		return std::nullopt;
	return parts;
}
} // namespace

DynamicWatchlist::PathResolution DynamicWatchlist::resolvePath(const QString& text, bool build) const
{
	auto p = d;
	PathResolution out;
	const auto typed = text.trimmed();
	const auto parts = splitPath(typed);
	if (!parts) {
		out.error = words("Write a path such as device.Config.Gain or data.Samples[3].");
		return out;
	}
	if (!canTellHoldable()) {
		out.error = words("Connect to a device to watch %1.").arg(typed);
		return out;
	}
	quint32 found = 0;
	int matches = 0;
	for (auto it = p->fields.cbegin(); it != p->fields.cend(); ++it)
		if (it->group == parts->group && it->name == parts->name) {
			found = it.key();
			++matches;
		}
	if (matches == 0) {
		const auto head = parts->group + QLatin1Char('.') + parts->name;
		const auto other = p->others.constFind(head);
		if (other != p->others.cend()) {
			out.error = other.value() == descriptor::Category::Command
			                ? words("%1 is a Command. Only Fields can be watched.").arg(head)
			                : words("%1 is a Service. Only Fields can be watched.").arg(head);
			return out;
		}
		out.missing = true;
		out.error = words("%1 is not on this device.").arg(typed);
		return out;
	}
	if (matches > 1) {
		out.error = words("This device has two Fields named %1.%2.").arg(parts->group, parts->name);
		return out;
	}
	const auto& shape = p->fields[found];
	const auto fieldPath = parts->group + QLatin1Char('.') + parts->name;
	const auto whole = watch_key::whole(found);
	if (!shape.readable) {
		out.error = words("%1 is write only, so it can't be watched.").arg(fieldPath);
		return out;
	}
	if (!shape.aggregate) {
		if (!parts->member.isEmpty()) {
			out.error = words("%1 is not a struct or an array, so it has no %2.").arg(fieldPath, parts->member);
			return out;
		}
		const auto bytes = p->source.fieldBytes ? p->source.fieldBytes(found) : std::nullopt;
		if (!bytes || !*bytes || *bytes > 8) {
			out.error = words("%1 can't be watched on this device.").arg(fieldPath);
			return out;
		}
		if (p->model.rowOf(whole) >= 0) {
			out.already = 1;
			out.firstWatched = whole;
		} else {
			out.adds = 1;
			if (build)
				out.entries.push_back(WatchEntry::of(whole, fieldPath));
		}
		return out;
	}
	if (!p->source.readMembers) {
		out.error = words("This connection can't read the values inside %1.").arg(fieldPath);
		return out;
	}
	if (shape.bytes > workbenchSnapshotBytes) {
		out.error = words("%1 is larger than the Watchlist reads at once (64 KiB), so it can't be watched.").arg(fieldPath);
		return out;
	}
	const auto range = p->leaves.range(shape.type, parts->member, fieldPath);
	if (!range.error.isEmpty()) {
		out.error = range.error;
		return out;
	}
	for (quint32 n = range.first; n < range.first + range.count; ++n)
		if (p->model.rowOf(watch_key::member(found, n)) >= 0) {
			if (!out.already)
				out.firstWatched = watch_key::member(found, n);
			++out.already;
		}
	out.adds = range.count - out.already;
	if (!build)
		return out;
	const auto added = entries(found, range.first, range.count, fieldPath);
	if (!added) {
		out.error = words("%1 can't be watched on this device.").arg(typed);
		out.adds = 0;
		return out;
	}
	out.entries = *added;
	return out;
}

QVector<DynamicWatchlist::PathCandidate> DynamicWatchlist::pathCandidates(const QString& text, int limit) const
{
	auto p = d;
	QVector<PathCandidate> out;
	if (!canTellHoldable() || limit <= 0)
		return out;
	const auto typed = text.trimmed();
	const auto starts = [](const QString& name, QStringView word) {
		return name.startsWith(word, Qt::CaseInsensitive);
	};
	// The Fields in descriptor order.
	QList<quint32> ids = p->fields.keys();
	std::sort(ids.begin(), ids.end());
	const auto heldAll = [&](quint32 id, quint32 first, quint32 count) {
		if (count == 0)
			return false;
		for (quint32 n = first; n < first + count; ++n)
			if (p->model.rowOf(watch_key::member(id, n)) < 0)
				return false;
		return true;
	};
	const auto nextOf = [&](TypeId type) {
		const auto kids = p->leaves.children(type, QString{});
		return kids.isEmpty() ? QString{} : kids.first().append == QLatin1String("[") ? QStringLiteral("[") : QStringLiteral(".");
	};
	const auto fieldCandidate = [&](quint32 id) {
		const auto& shape = p->fields[id];
		PathCandidate c;
		c.next = shape.aggregate ? nextOf(shape.type) : QString{};
		c.path = shape.group + QLatin1Char('.') + shape.name;
		c.typeText = p->leaves.typeText(shape.type);
		c.container = shape.aggregate;
		c.values = shape.aggregate ? p->leaves.count(shape.type) : 1;
		c.watched = shape.aggregate ? heldAll(id, 0, c.values) : p->model.rowOf(watch_key::whole(id)) >= 0;
		return c;
	};
	const auto dot = typed.indexOf(QLatin1Char('.'));
	if (dot < 0) {
		// The groups, then the Fields whose name holds the word.
		QStringList groups;
		for (const auto id : std::as_const(ids)) {
			const auto& group = p->fields[id].group;
			if (p->fields[id].readable && !groups.contains(group) && starts(group, typed))
				groups.append(group);
		}
		for (const auto& group : std::as_const(groups)) {
			PathCandidate c;
			c.path = group + QLatin1Char('.');
			c.container = true;
			for (const auto id : std::as_const(ids))
				if (p->fields[id].group == group && p->fields[id].readable)
					++c.values;
			out.append(c);
			if (out.size() >= limit)
				return out;
		}
	} else {
		const auto group = typed.left(dot);
		const auto rest = typed.mid(dot + 1);
		auto end = 0;
		while (end < rest.size() && rest.at(end) != QLatin1Char('.') && rest.at(end) != QLatin1Char('['))
			++end;
		if (end == rest.size()) {
			// A group's Fields.
			for (const auto id : std::as_const(ids))
				if (p->fields[id].group == group && p->fields[id].readable && starts(p->fields[id].name, rest)) {
					out.append(fieldCandidate(id));
					if (out.size() >= limit)
						return out;
				}
		} else {
			// Inside a Field: the next step below what is typed in full.
			const auto name = rest.left(end);
			quint32 found = 0;
			for (const auto id : std::as_const(ids))
				if (p->fields[id].group == group && p->fields[id].name == name)
					found = id;
			const auto shape = p->fields.constFind(found);
			if (shape != p->fields.cend() && shape->aggregate) {
				auto member = rest.mid(end);
				if (member.startsWith(QLatin1Char('.')))
					member.remove(0, 1);
				// What is typed in full, and the word after it.
				qsizetype cut = member.size();
				while (cut > 0 && member.at(cut - 1) != QLatin1Char('.') && member.at(cut - 1) != QLatin1Char('['))
					--cut;
				auto complete = member.left(cut);
				const auto word = member.mid(cut);
				// "[" typed: the elements; "." or the Field itself: the members.
				const bool index = complete.endsWith(QLatin1Char('['));
				if (complete.endsWith(QLatin1Char('.')) || index)
					complete.chop(1);
				// The path of what is typed in full: "device.Config", "device.Calib.Phase[2]".
				auto base = group + QLatin1Char('.') + name;
				if (!complete.isEmpty())
					base += (complete.startsWith(QLatin1Char('[')) ? QString{} : QStringLiteral(".")) + complete;
				for (const auto& child : p->leaves.children(shape->type, complete)) {
					const bool elements = child.append == QLatin1String("[");
					if (index != elements || (!index && !starts(child.label, word)))
						continue;
					PathCandidate c;
					c.path = elements ? base + child.label : base + QLatin1Char('.') + child.label;
					c.typeText = p->leaves.typeText(child.type);
					c.container = child.container;
					c.values = child.leaves;
					c.next = child.container && !elements ? nextOf(child.type) : QString{};
					out.append(c);
					if (out.size() >= limit)
						return out;
				}
			}
		}
	}
	// Then the paths whose last step holds the word: Fields and the members
	// of struct Fields, arrays not opened, at most 4096 steps walked.
	const auto last = typed.mid(std::max(typed.lastIndexOf(QLatin1Char('.')), typed.lastIndexOf(QLatin1Char('['))) + 1);
	if (last.size() < 2)
		return out;
	int walked = 0;
	QSet<QString> shown;
	for (const auto& c : std::as_const(out))
		shown.insert(c.path);
	for (const auto id : std::as_const(ids)) {
		const auto& shape = p->fields[id];
		if (!shape.readable)
			continue;
		if (shape.name.contains(last, Qt::CaseInsensitive)) {
			const auto c = fieldCandidate(id);
			if (!shown.contains(c.path)) {
				out.append(c);
				shown.insert(c.path);
			}
		}
		if (!shape.aggregate)
			continue;
		// Members, breadth first, struct inside struct only.
		QVector<std::pair<TypeId, QString>> queue{{shape.type, shape.group + QLatin1Char('.') + shape.name}};
		while (!queue.isEmpty() && walked < 4096 && out.size() < limit) {
			const auto [type, path] = queue.takeFirst();
			for (const auto& child : p->leaves.children(type)) {
				if (++walked > 4096 || child.append == QLatin1String("["))
					break;
				const auto childPath = path + QLatin1Char('.') + child.label;
				if (child.label.contains(last, Qt::CaseInsensitive) && !shown.contains(childPath)) {
					PathCandidate c;
					c.path = childPath;
					c.typeText = p->leaves.typeText(child.type);
					c.container = child.container;
					c.values = child.leaves;
					c.next = child.container ? nextOf(child.type) : QString{};
					out.append(c);
					shown.insert(childPath);
				}
				if (child.container)
					queue.append({child.type, childPath});
			}
		}
		if (out.size() >= limit)
			break;
	}
	return out;
}

QVariantMap DynamicWatchlist::binding(int row) const
{
	auto p = d;
	const auto key = p->model.key(row);
	if (row >= 0 && row < p->model.count() && watch_key::isUnresolved(key)) {
		// A row without a device is saved by the path it was given.
		const auto parts = splitPath(p->model.data(p->model.index(row, 0), WatchlistModel::NameRole).toString());
		if (!parts)
			return {};
		QVariantMap saved{{QStringLiteral("group"), parts->group}, {QStringLiteral("name"), parts->name}};
		if (!parts->member.isEmpty())
			saved.insert(QStringLiteral("member"), parts->member);
		return saved;
	}
	const auto shape = p->fields.constFind(watch_key::field(key));
	if (row < 0 || row >= p->model.count() || shape == p->fields.cend())
		return {};
	QVariantMap saved{{QStringLiteral("group"), shape->group}, {QStringLiteral("name"), shape->name}};
	if (!watch_key::isWhole(key))
		saved.insert(QStringLiteral("member"), p->model.member(row));
	return saved;
}

QSet<WatchKey> DynamicWatchlist::held() const
{
	QSet<WatchKey> keys;
	for (int row = 0; row < d->model.count(); ++row)
		if (!watch_key::isUnresolved(d->model.key(row)))
			keys.insert(d->model.key(row));
	return keys;
}

quint64 DynamicWatchlist::membership() const
{
	return d->model.membership();
}

QVector<WatchlistModel::Removed> DynamicWatchlist::removeMany(const QSet<WatchKey>& keys)
{
	auto p = d;
	// No device is needed: rows kept across a disconnect go as well (WL-4c).
	if (p->guarded() || p->reportingCompletion)
		return {};
	const auto removed = p->model.removeIds(keys);
	if (!removed.isEmpty())
		p->membershipChanged(false);
	return removed;
}

bool DynamicWatchlist::restore(const QVector<WatchlistModel::Removed>& rows)
{
	auto p = d;
	if (p->guarded() || p->reportingCompletion || !p->current() || !p->model.restore(rows))
		return false;
	p->membershipChanged(true);
	return true;
}

void DynamicWatchlist::setWritable(std::function<bool(WatchKey)> writable)
{
	d->model.setWritable(std::move(writable));
}

void DynamicWatchlist::setWriteRefusal(std::function<QString(WatchKey)> refusal)
{
	d->model.setWriteRefusal(std::move(refusal));
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
	return d->intervalMs;
}

void DynamicWatchlist::setInterval(int ms)
{
	auto p = d;
	if (!p->guarded() && !p->active() && ms >= 0 && ms <= 60000) {
		p->intervalMs = ms;
		p->timer.setInterval(ms ? ms : 1000);
		p->changed();
	}
}

bool DynamicWatchlist::begin(bool polling)
{
	auto p = d;
	if (p->reportingCompletion) return false;
	if (polling && p->cycle && !p->running && !p->guarded() && p->current()) {
		// Start while a Read once runs: that cycle becomes the first one of
		// polling instead of the Start being refused.
		p->running = true;
		p->timer.start();
		p->changed();
		return true;
	}
	if (p->guarded() || p->active() || !p->current() || !p->model.count())
		return false;
	if (!p->model.hasBound()) {
		// Only rows for paths this device lacks (WL-4c): nothing would be read.
		p->status = QStringLiteral("Nothing to read: none of these paths is on this device.");
		p->changed();
		return false;
	}
	if (p->unavailable()) {
		// Refused for a reason the operator can act on: say it.
		p->status = QStringLiteral("Not started: another operation is in progress");
		p->changed();
		return false;
	}
	p->urgent.clear();
	p->rerun = false;
	p->running = polling;
	if (!p->startCycle()) return false;
	if (polling)
		p->timer.start();
	p->next();
	return p->cycle || bool(p->request);
}

void DynamicWatchlist::wake(std::optional<quint32> written)
{
	auto p = d;
	p->wake(written);
}

void DynamicWatchlist::dataChanged()
{
	auto p = d;
	p->dataChanged();
}

void DynamicWatchlist::stop()
{
	auto p = d;
	if (p->guarded())
		return;
	p->running = p->cycle = false;
	p->urgent.clear();
	p->rerun = false;
	p->timer.stop();
	++p->revision;
	p->status = p->request ? QStringLiteral("Stopping - draining accepted read")
	                       : QStringLiteral("Stopped");
	p->changed();
}
} // namespace telemetry::ux
