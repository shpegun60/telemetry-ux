// The Watchlist's readings kept in memory for the live Plot (LP-1). MIT.
#include "WatchTraces.hpp"
#include <QtCore/QMetaObject>
#include <QtCore/QSet>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace telemetry::ux {
namespace {
constexpr double noValue = std::numeric_limits<double>::quiet_NaN();

bool isSigned(WatchScalar scalar) noexcept
{
	return scalar == WatchScalar::S8 || scalar == WatchScalar::S16 || scalar == WatchScalar::S32 ||
	       scalar == WatchScalar::S64;
}

// A read the device answered whole: the recording's own rule for a sample
// that is not a gap.
bool answered(const WatchFrame& frame, const WatchFrameRow& row) noexcept
{
	return frame.phase == WatchCompletionPhase::Current && !frame.native.client && !frame.native.dispatch &&
	       !frame.native.endpoint && !frame.native.codec && !frame.native.value && !row.itemStatus &&
	       !row.valueStatus && row.value.size > 0;
}
} // namespace

std::optional<double> plotValue(const WatchValueBits& value) noexcept
{
	if (!value.type.valid() || value.size != value.type.width)
		return std::nullopt;
	quint64 bits = 0;
	for (unsigned i = 0; i < value.size; ++i)
		bits |= quint64(std::to_integer<quint8>(value.bytes[i])) << (8 * i);
	switch (value.type.scalar) {
		case WatchScalar::Boolean:
			return bits <= 1 ? std::optional<double>(double(bits)) : std::nullopt;
		case WatchScalar::F32: {
			static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
			const auto narrow = quint32(bits);
			float number;
			std::memcpy(&number, &narrow, sizeof(number));
			return std::isfinite(number) ? std::optional<double>(double(number)) : std::nullopt;
		}
		case WatchScalar::F64: {
			static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
			double number;
			std::memcpy(&number, &bits, sizeof(number));
			return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
		}
		case WatchScalar::Unknown:
			return std::nullopt;
		default:
			break;
	}
	constexpr quint64 exact = (quint64{1} << 53) - 1;
	const unsigned width = value.size * 8u;
	if (isSigned(value.type.scalar) && (bits & (quint64{1} << (width - 1)))) {
		const quint64 mask = width == 64 ? ~quint64{0} : (quint64{1} << width) - 1;
		const quint64 magnitude = ((~bits) & mask) + 1;
		return magnitude <= exact ? std::optional<double>(-double(magnitude)) : std::nullopt;
	}
	return bits <= exact ? std::optional<double>(double(bits)) : std::nullopt;
}

WatchTraces::WatchTraces(QObject* parent) : QObject(parent) {}

qint64 WatchTraces::nowNs() noexcept
{
	return WatchTimestamp::now().monotonicNs;
}

void WatchTraces::setPaths(quint64 membership, QHash<quint64, QString> paths, QHash<QString, bool> categorical)
{
	// A value that leaves the Watchlist, or comes back to it, breaks its
	// line there: the time it was not read is not drawn as if it were.
	QSet<QString> before, after;
	for (const auto& path : std::as_const(paths_))
		before.insert(path);
	for (const auto& path : std::as_const(paths))
		after.insert(path);
	const auto now = nowNs();
	bool broke = false;
	for (auto it = traces_.begin(); it != traces_.end(); ++it)
		if (before.contains(it.key()) != after.contains(it.key()) && !it->readings.empty() &&
		    !std::isnan(it->readings.back().value)) {
			push(*it, {std::max(now, it->readings.back().ns), noValue});
			broke = true;
		}
	membership_ = membership;
	paths_ = std::move(paths);
	categorical_ = std::move(categorical);
	if (broke) {
		++revision_;
		announce();
	}
}

void WatchTraces::append(const WatchFrame& frame)
{
	if (!anchor_.monotonicNs && frame.completed.monotonicNs)
		anchor_ = frame.completed;
	const auto ns = frame.completed.monotonicNs;
	const int rows = std::clamp(frame.count, 0, int(frame.rows.size()));
	bool any = false;
	for (int i = 0; i < rows; ++i) {
		const auto& row = frame.rows[std::size_t(i)];
		const auto path = paths_.value(quint64(row.id) | quint64(row.leaf) << 32);
		if (path.isEmpty())
			continue;
		auto& trace = traces_[path];
		const auto& type = row.value.type;
		trace.categorical = categorical_.value(path, type.scalar == WatchScalar::Boolean || type.enumeration);
		// Another type for the same path (another device): a new line.
		if (type.valid() && trace.type.valid() &&
		    (type.typeId != trace.type.typeId || type.scalar != trace.type.scalar || type.enumeration != trace.type.enumeration) &&
		    !trace.readings.empty() && !std::isnan(trace.readings.back().value))
			push(trace, {ns, noValue});
		if (type.valid())
			trace.type = type;
		const auto number = answered(frame, row) ? plotValue(row.value) : std::nullopt;
		push(trace, {ns, number ? *number : noValue});
		any = true;
	}
	if (!any)
		return;
	newestNs_ = std::max(newestNs_, ns);
	++revision_;
	announce();
}

void WatchTraces::push(Trace& trace, const TraceReading& reading)
{
	auto& readings = trace.readings;
	// Readings are kept in time order; one out of order joins at its place.
	if (readings.empty() || readings.back().ns <= reading.ns)
		readings.push_back(reading);
	else
		readings.insert(std::upper_bound(readings.begin(), readings.end(), reading.ns,
		                                 [](qint64 at, const TraceReading& r) { return at < r.ns; }),
		                reading);
	while (readings.size() > keptReadings || (readings.size() > 1 && readings.back().ns - readings.front().ns > keptNs))
		readings.pop_front();
}

void WatchTraces::announce()
{
	if (announcing_)
		return;
	announcing_ = true;
	QMetaObject::invokeMethod(
	    this,
	    [this] {
		    announcing_ = false;
		    Q_EMIT appended();
	    },
	    Qt::QueuedConnection);
}

const std::deque<TraceReading>* WatchTraces::readings(const QString& path) const
{
	const auto it = traces_.constFind(path);
	return it == traces_.cend() ? nullptr : &it->readings;
}

bool WatchTraces::categorical(const QString& path) const
{
	const auto it = traces_.constFind(path);
	return it != traces_.cend() && it->categorical;
}

QString WatchTraces::plot(const QString& path)
{
	if (path.isEmpty() || plotted_.contains(path))
		return {};
	if (plotted_.size() >= limit)
		return tr("The Plot holds %1 curves. Remove one to plot %2.").arg(limit).arg(path);
	if (!colors_.contains(path)) {
		QSet<int> used;
		for (const auto& shown : std::as_const(plotted_))
			used.insert(colors_.value(shown, -1));
		int color = 0;
		while (used.contains(color) && color < limit)
			++color;
		colors_.insert(path, color % limit);
	}
	plotted_.append(path);
	Q_EMIT plottedChanged();
	return {};
}

void WatchTraces::unplot(const QString& path)
{
	if (plotted_.removeAll(path) > 0)
		Q_EMIT plottedChanged();
}

QVariant WatchTraces::latest(const QString& path) const
{
	const auto* found = readings(path);
	if (!found || found->empty() || std::isnan(found->back().value))
		return {};
	return found->back().value;
}

int WatchTraces::count(const QString& path) const
{
	const auto* found = readings(path);
	return found ? int(found->size()) : 0;
}

void WatchTraces::clear()
{
	traces_.clear();
	newestNs_ = 0;
	++revision_;
	announce();
}
} // namespace telemetry::ux
