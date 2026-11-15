// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#include "TraceView.hpp"
#include "WatchTraces.hpp"
#include <QtCore/QDateTime>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtCore/QLocale>
#include <QtCore/QTime>
#include <QtCore/QVector>
#include <QtGui/QFontMetricsF>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtQuick/QQuickWindow>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace telemetry::ux {
namespace {
constexpr qint64 msNs = 1'000'000;
constexpr qint64 dayMs = 86'400'000;
// A pause in reading longer than this, and ten times the step before it,
// breaks the line: time not read is not drawn as if it were. A stop and a
// disconnect break it for certain (WatchTraces::breakAll); this catches what
// they do not say.
constexpr qint64 pauseNs = 2'000'000'000;
// The value axis only spans this much, so far-apart doubles never overflow.
constexpr double valueLimit = 1e300;

// Time-of-day tick steps, ms: the smallest whose labels stand far enough
// apart is used.
constexpr std::array<qint64, 26> timeSteps{
    1,      2,      5,       10,      20,      50,      100,     200,      500,      1000,     2000,     5000,    10000,
    15000,  30000,  60000,   120000,  300000,  600000,  900000,  1800000,  3600000,  7200000,  10800000, 21600000, 43200000};

QString clockText(qint64 localMs, qint64 step)
{
	const auto time = QTime::fromMSecsSinceStartOfDay(int(((localMs % dayMs) + dayMs) % dayMs));
	if (step >= 60000)
		return time.toString(QStringLiteral("HH:mm"));
	if (step >= 1000)
		return time.toString(QStringLiteral("HH:mm:ss"));
	auto text = time.toString(QStringLiteral("HH:mm:ss.zzz"));
	// The decimals the step needs: 0.1 s one, 0.01 s two, below that three.
	const int drop = step >= 100 ? 2 : step >= 10 ? 1 : 0;
	text.chop(drop);
	return text;
}

// Time since the start, to the step's precision: "12 s", "12.5 s", "1:30",
// "1:02:00", "-0:12".
QString elapsedText(qint64 ms, qint64 step)
{
	const QString sign = ms < 0 ? QStringLiteral("\u2212") : QString();
	const qint64 whole = ms < 0 ? -ms : ms;
	const int decimals = step >= 1000 ? 0 : step >= 100 ? 1 : step >= 10 ? 2 : 3;
	const auto fraction = [&](qint64 msPart) {
		if (!decimals)
			return QString();
		return QLatin1Char('.') + QString::number(msPart).rightJustified(3, QLatin1Char('0')).left(decimals);
	};
	if (whole < 60000)
		return sign + QString::number(whole / 1000) + fraction(whole % 1000) + QStringLiteral(" s");
	const qint64 seconds = whole / 1000, minutes = seconds / 60, hours = minutes / 60;
	const QString ss = QString::number(seconds % 60).rightJustified(2, QLatin1Char('0')) + fraction(whole % 1000);
	if (hours == 0)
		return sign + QString::number(minutes) + QLatin1Char(':') + ss;
	return sign + QString::number(hours) + QLatin1Char(':') + QString::number(minutes % 60).rightJustified(2, QLatin1Char('0')) +
	       QLatin1Char(':') + ss;
}

// 1, 2 or 5 times a power of ten, at least raw.
double niceStep(double raw)
{
	if (!(raw > 0) || !std::isfinite(raw))
		return 1;
	const double power = std::pow(10.0, std::floor(std::log10(raw)));
	for (const double m : {1.0, 2.0, 5.0, 10.0})
		if (m * power >= raw)
			return m * power;
	return 10 * power;
}

QString valueText(double value, double step)
{
	const int decimals = step >= 1 ? 0 : std::clamp(int(std::ceil(-std::log10(step) - 1e-9)), 0, 9);
	if (std::abs(value) < step * 1e-6)
		value = 0;
	return QString::number(value, 'f', decimals);
}
} // namespace

TraceView::TraceView(QQuickItem* parent) : QQuickPaintedItem(parent)
{
	setAntialiasing(true);
	setOpaquePainting(false);
	setAcceptedMouseButtons(Qt::LeftButton);
	setAcceptHoverEvents(true);
	setActiveFocusOnTab(true);
	setCursor(Qt::CrossCursor);
	// New readings mark the picture; it is drawn again at most every 50 ms,
	// however fast they come.
	frames_.setInterval(50);
	frames_.setSingleShot(true);
	connect(&frames_, &QTimer::timeout, this, &TraceView::tick);
	connect(this, &QQuickItem::visibleChanged, this, [this] { update(); });
}

QObject* TraceView::traces() const
{
	return traces_.data();
}

void TraceView::setTraces(QObject* traces)
{
	auto* typed = qobject_cast<WatchTraces*>(traces);
	if (typed == traces_)
		return;
	if (traces_)
		disconnect(traces_, nullptr, this, nullptr);
	traces_ = typed;
	// Other readings: the values fit them afresh, and nothing pinned on the
	// old ones stays.
	range_ = {};
	aggregated_.clear();
	if (traces_) {
		connect(traces_, &WatchTraces::appended, this, &TraceView::schedule);
		connect(traces_, &WatchTraces::plottedChanged, this, [this] {
			// A curve on or off: the values fit what is plotted now.
			range_ = {};
			update();
		});
		connect(traces_, &WatchTraces::appearanceChanged, this, [this] {
			// A new multiplier or offset is a new fit.
			range_ = {};
			update();
		});
	}
	Q_EMIT tracesChanged();
	update();
}

void TraceView::setSpanSeconds(qreal seconds)
{
	seconds = std::clamp(seconds, 0.01, 86400.0);
	if (qFuzzyCompare(seconds, spanSeconds_))
		return;
	spanSeconds_ = seconds;
	Q_EMIT viewChanged();
	update();
}

void TraceView::setFollowing(bool following)
{
	if (following == following_)
		return;
	// Unfollowing keeps the picture where it is now.
	if (!following) {
		heldAllNs_ = showAll_ ? spanNs() : 0;
		frozenNs_ = rightNs();
	} else {
		heldAllNs_ = 0;
	}
	following_ = following;
	Q_EMIT viewChanged();
	update();
}

void TraceView::setShowAll(bool all)
{
	if (all == showAll_)
		return;
	showAll_ = all;
	Q_EMIT viewChanged();
	update();
}

int TraceView::preset() const
{
	if (showAll_)
		return 0;
	for (const int seconds : {10, 60, 600})
		if (qFuzzyCompare(spanSeconds_, qreal(seconds)))
			return seconds;
	return -1;
}

void TraceView::choosePreset(int seconds)
{
	remember();
	heldAllNs_ = 0;
	if (seconds <= 0) {
		showAll_ = true;
	} else {
		showAll_ = false;
		spanSeconds_ = seconds;
	}
	following_ = true;
	Q_EMIT viewChanged();
	update();
}

namespace {
// The reading at or before ns, or none.
const TraceReading* atOrBefore(const std::deque<TraceReading>& readings, qint64 ns)
{
	auto it = std::upper_bound(readings.begin(), readings.end(), ns, [](qint64 at, const TraceReading& r) { return at < r.ns; });
	if (it == readings.begin())
		return nullptr;
	return &*std::prev(it);
}
} // namespace

qreal TraceView::cursorUtcMs() const
{
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	return qreal(anchor.utcMs) + qreal(cursorNs_ - anchor.monotonicNs) / 1e6;
}

qreal TraceView::cursorAgoSeconds() const
{
	const auto newest = traces_ ? traces_->newestNs() : 0;
	return newest > cursorNs_ ? qreal(newest - cursorNs_) / 1e9 : 0;
}

qreal TraceView::referenceUtcMs() const
{
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	return qreal(anchor.utcMs) + qreal(referenceNs_ - anchor.monotonicNs) / 1e6;
}

void TraceView::setCursorColor(const QColor& color)
{
	cursorColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setReferenceColor(const QColor& color)
{
	referenceColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setLaneHeight(qreal height)
{
	laneHeight_ = std::max<qreal>(6, height);
	Q_EMIT styleChanged();
	update();
}

void TraceView::setLaneFillColor(const QColor& color)
{
	laneFillColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setLaneTextColor(const QColor& color)
{
	laneTextColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setBottomInset(qreal inset)
{
	if (qFuzzyCompare(inset + 1, bottomInset_ + 1))
		return;
	bottomInset_ = std::max<qreal>(0, inset);
	Q_EMIT styleChanged();
	update();
}

void TraceView::setDotRingColor(const QColor& color)
{
	dotRingColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setCursorAt(qreal x)
{
	if (!traces_ || plotArea_.width() <= 0)
		return;
	const auto span = spanNs();
	const auto right = rightNs();
	const auto left = right - span;
	const double perPx = double(span) / plotArea_.width();
	const qint64 at = left + qint64((std::clamp(x, plotArea_.left(), plotArea_.right()) - plotArea_.left()) * perPx);
	// The nearest reading of any plotted value within 8 px, else here.
	qint64 best = at, distance = qint64(8 * perPx);
	for (const auto& path : traces_->plotted()) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		auto it = std::lower_bound(readings->begin(), readings->end(), at, [](const TraceReading& r, qint64 t) { return r.ns < t; });
		for (auto near : {it, it == readings->begin() ? it : std::prev(it)}) {
			if (near == readings->end() || std::isnan(near->value))
				continue;
			const auto d = std::abs(near->ns - at);
			if (d <= distance) {
				distance = d;
				best = near->ns;
			}
		}
	}
	if (cursorShown_ && cursorNs_ == best)
		return;
	cursorShown_ = true;
	cursorNs_ = best;
	Q_EMIT cursorChanged();
	update();
}

void TraceView::hideCursor()
{
	if (!cursorShown_)
		return;
	cursorShown_ = false;
	Q_EMIT cursorChanged();
	update();
}

void TraceView::stepCursor(int direction)
{
	if (!traces_)
		return;
	// No cursor yet: it starts on the newest reading in view.
	if (!cursorShown_) {
		const auto right = rightNs();
		qint64 newest = 0;
		for (const auto& path : traces_->plotted())
			if (const auto* readings = traces_->readings(path))
				if (const auto* at = atOrBefore(*readings, right))
					newest = std::max(newest, at->ns);
		if (!newest)
			return;
		cursorShown_ = true;
		cursorNs_ = newest;
		Q_EMIT cursorChanged();
		update();
		return;
	}
	const auto start = cursorNs_;
	qint64 next = 0;
	for (const auto& path : traces_->plotted()) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		if (direction > 0) {
			auto it = std::upper_bound(readings->begin(), readings->end(), start, [](qint64 t, const TraceReading& r) { return t < r.ns; });
			if (it != readings->end() && (!next || it->ns < next))
				next = it->ns;
		} else {
			auto it = std::lower_bound(readings->begin(), readings->end(), start, [](const TraceReading& r, qint64 t) { return r.ns < t; });
			if (it != readings->begin() && (!next || std::prev(it)->ns > next))
				next = std::prev(it)->ns;
		}
	}
	if (!next)
		return;
	cursorShown_ = true;
	cursorNs_ = next;
	// Stepping out of the picture brings the picture along.
	const auto right = rightNs(), span = spanNs();
	if (next > right || next < right - span)
		hold(next + span / 2);
	Q_EMIT cursorChanged();
	update();
}

void TraceView::pinReference()
{
	if (!cursorShown_ || !traces_)
		return;
	referenceShown_ = true;
	referenceNs_ = cursorNs_;
	referenceValues_.clear();
	for (const auto& path : traces_->plotted())
		if (const auto* readings = traces_->readings(path))
			if (const auto* at = atOrBefore(*readings, referenceNs_); at && !std::isnan(at->value))
				referenceValues_.insert(path, at->value);
	Q_EMIT cursorChanged();
	update();
}

void TraceView::clearReference()
{
	if (!referenceShown_)
		return;
	referenceShown_ = false;
	endShown_ = false;
	referenceValues_.clear();
	Q_EMIT cursorChanged();
	update();
}

void TraceView::pinEnd()
{
	if (!cursorShown_ || !traces_)
		return;
	// The reference first: a range needs both.
	if (!referenceShown_) {
		pinReference();
		return;
	}
	endShown_ = true;
	endNs_ = cursorNs_;
	Q_EMIT cursorChanged();
	update();
}

void TraceView::clearEnd()
{
	if (!endShown_)
		return;
	endShown_ = false;
	Q_EMIT cursorChanged();
	update();
}

qreal TraceView::endUtcMs() const
{
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	return qreal(anchor.utcMs) + qreal(endNs_ - anchor.monotonicNs) / 1e6;
}

std::pair<qint64, qint64> TraceView::measuredNs() const
{
	if (referenceShown_ && endShown_)
		return {std::min(referenceNs_, endNs_), std::max(referenceNs_, endNs_)};
	const auto right = rightNs();
	return {right - spanNs(), right};
}

QVariantMap TraceView::measureRange() const
{
	const auto [from, to] = measuredNs();
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	const auto utc = [&](qint64 ns) { return double(anchor.utcMs) + double(ns - anchor.monotonicNs) / 1e6; };
	return {{QStringLiteral("fromUtcMs"), utc(from)}, {QStringLiteral("toUtcMs"), utc(to)},
	        {QStringLiteral("seconds"), double(to - from) / 1e9}, {QStringLiteral("pinned"), referenceShown_ && endShown_},
	        {QStringLiteral("readings"), readingsInRange()}};
}

QVariantMap TraceView::statistics(const QString& path) const
{
	const auto* readings = traces_ ? traces_->readings(path) : nullptr;
	if (!readings)
		return {};
	const auto [from, to] = measuredNs();
	const auto anchor = traces_->anchor();
	const auto utc = [&](qint64 ns) { return double(anchor.utcMs) + double(ns - anchor.monotonicNs) / 1e6; };
	auto it = std::lower_bound(readings->begin(), readings->end(), from,
	                           [](const TraceReading& r, qint64 at) { return r.ns < at; });
	int count = 0, failed = 0;
	if (traces_->categorical(path)) {
		// Shares by time: each reading holds until the next one, or the end.
		QHash<qint64, qint64> held;
		QList<qint64> order;
		int changes = 0;
		std::optional<double> previous;
		qint64 total = 0;
		for (; it != readings->end() && it->ns <= to; ++it) {
			if (std::isnan(it->value)) {
				++failed;
				previous.reset();
				continue;
			}
			++count;
			const auto next = std::next(it);
			const qint64 until = next != readings->end() && next->ns <= to ? next->ns : to;
			const auto key = qint64(it->value);
			if (!held.contains(key))
				order << key;
			held[key] += std::max<qint64>(0, until - it->ns);
			total += std::max<qint64>(0, until - it->ns);
			if (previous && *previous != it->value)
				++changes;
			previous = it->value;
		}
		std::sort(order.begin(), order.end(), [&](qint64 a, qint64 b) { return held.value(a) > held.value(b); });
		QVariantList shares;
		for (const auto key : order)
			shares << QVariantMap{{QStringLiteral("name"), traces_->stateText(path, double(key))},
			                      {QStringLiteral("share"), total > 0 ? double(held.value(key)) / double(total) : 0.0}};
		return {{QStringLiteral("count"), count}, {QStringLiteral("failed"), failed},
		        {QStringLiteral("changes"), changes}, {QStringLiteral("shares"), shares}};
	}
	// One pass: the extremes with their times, and Welford's mean and spread.
	double low = 0, high = 0, mean = 0, squares = 0;
	qint64 lowAt = 0, highAt = 0;
	for (; it != readings->end() && it->ns <= to; ++it) {
		if (std::isnan(it->value)) {
			++failed;
			continue;
		}
		++count;
		if (count == 1 || it->value < low) {
			low = it->value;
			lowAt = it->ns;
		}
		if (count == 1 || it->value > high) {
			high = it->value;
			highAt = it->ns;
		}
		const double delta = it->value - mean;
		mean += delta / count;
		squares += delta * (it->value - mean);
	}
	if (!count)
		return {{QStringLiteral("count"), 0}, {QStringLiteral("failed"), failed}};
	return {{QStringLiteral("count"), count}, {QStringLiteral("failed"), failed},
	        {QStringLiteral("min"), low}, {QStringLiteral("max"), high},
	        {QStringLiteral("minUtcMs"), utc(lowAt)}, {QStringLiteral("maxUtcMs"), utc(highAt)},
	        {QStringLiteral("mean"), mean}, {QStringLiteral("peakToPeak"), high - low},
	        {QStringLiteral("deviation"), std::sqrt(squares / count)}};
}

void TraceView::putCursorAt(qreal utcMs)
{
	if (!traces_)
		return;
	const auto anchor = traces_->anchor();
	const auto ns = anchor.monotonicNs + qint64((double(utcMs) - double(anchor.utcMs)) * 1e6);
	const auto right = rightNs(), left = right - spanNs();
	// Out of view: centred, held.
	if (ns < left || ns > right) {
		remember();
		hold(ns + spanNs() / 2);
	}
	cursorShown_ = true;
	cursorNs_ = ns;
	Q_EMIT cursorChanged();
	update();
}

bool TraceView::stepChange(int direction)
{
	if (!traces_ || direction == 0)
		return false;
	QStringList lanes;
	if (!focusedPath_.isEmpty() && traces_->categorical(focusedPath_))
		lanes << focusedPath_;
	else
		for (const auto& path : traces_->plotted())
			if (traces_->categorical(path))
				lanes << path;
	const auto right = rightNs(), left = right - spanNs();
	const qint64 from = cursorShown_ ? cursorNs_ : direction > 0 ? left : right;
	std::optional<qint64> found;
	for (const auto& path : std::as_const(lanes)) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->size() < 2)
			continue;
		// A change is a reading whose value differs from the reading before it.
		std::optional<double> previous;
		std::optional<qint64> best;
		for (const auto& reading : *readings) {
			if (std::isnan(reading.value)) {
				previous.reset();
				continue;
			}
			if (previous && *previous != reading.value) {
				if (direction > 0 && reading.ns > from) {
					best = reading.ns;
					break;
				}
				if (direction < 0 && reading.ns < from)
					best = reading.ns;
			}
			previous = reading.value;
		}
		if (best && (!found || (direction > 0 ? *best < *found : *best > *found)))
			found = best;
	}
	const auto anchor = traces_->anchor();
	const auto utc = [&](qint64 ns) { return double(anchor.utcMs) + double(ns - anchor.monotonicNs) / 1e6; };
	if (!found) {
		const auto at = QDateTime::fromMSecsSinceEpoch(qint64(utc(from))).toString(QStringLiteral("HH:mm:ss.zzz"));
		const auto what = lanes.size() == 1 ? lanes.first() : tr("any state");
		Q_EMIT findFailed(lanes.isEmpty() ? tr("No booleans or enums are plotted to step through.")
		                  : direction > 0 ? tr("No change of %1 after %2 in the readings kept.").arg(what, at)
		                                  : tr("No change of %1 before %2 in the readings kept.").arg(what, at));
		return false;
	}
	putCursorAt(utc(*found));
	Q_EMIT changeFound();
	return true;
}

QVariant TraceView::valueAt(const QString& path) const
{
	if (!cursorShown_ || !traces_)
		return {};
	const auto* readings = traces_->readings(path);
	const auto* at = readings ? atOrBefore(*readings, cursorNs_) : nullptr;
	return at && !std::isnan(at->value) ? QVariant(at->value) : QVariant();
}

QVariant TraceView::valueAtRight(const QString& path) const
{
	if (!traces_)
		return {};
	// The edge in the readings' own time, never through a time of day: a
	// double of milliseconds since the epoch cannot hold a nanosecond.
	const auto* readings = traces_->readings(path);
	const auto* at = readings ? atOrBefore(*readings, rightNs()) : nullptr;
	return at && !std::isnan(at->value) ? QVariant(at->value) : QVariant();
}

QVariant TraceView::referenceValue(const QString& path) const
{
	const auto it = referenceValues_.constFind(path);
	return referenceShown_ && it != referenceValues_.cend() ? QVariant(*it) : QVariant();
}

QVariant TraceView::endValue(const QString& path) const
{
	if (!endShown_ || !traces_)
		return {};
	const auto* readings = traces_->readings(path);
	const auto* at = readings ? atOrBefore(*readings, endNs_) : nullptr;
	return at && !std::isnan(at->value) ? QVariant(at->value) : QVariant();
}

void TraceView::hoverMoveEvent(QHoverEvent* event)
{
	// Qt Quick repeats a hover each frame the scene changes, and when the
	// graph moves under a still pointer (the legend gaining a row); only a
	// pointer that moved in the window moves the cursor, so the keys' cursor
	// stays where they put it.
	if (event->scenePosition() == hoverAt_)
		return;
	hoverAt_ = event->scenePosition();
	if (plotArea_.contains(event->position()))
		setCursorAt(event->position().x());
	else
		hideCursor();
}

void TraceView::hoverLeaveEvent(QHoverEvent*)
{
	hoverAt_ = QPointF(-1, -1);
	hideCursor();
}

std::pair<qint64, qint64> TraceView::kept() const
{
	qint64 oldest = 0, newest = 0;
	if (!traces_)
		return {0, 0};
	for (const auto& path : traces_->plotted()) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		oldest = oldest ? std::min(oldest, readings->front().ns) : readings->front().ns;
		newest = std::max(newest, readings->back().ns);
	}
	return {oldest, newest};
}

qint64 TraceView::spanNs() const
{
	if (showAll_ && !following_ && heldAllNs_)
		return heldAllNs_;
	if (showAll_) {
		const auto [oldest, newest] = kept();
		// All: everything kept, and a second at the least.
		return std::max<qint64>(1'000'000'000, newest - oldest);
	}
	return qint64(spanSeconds_ * 1e9);
}

void TraceView::hold(qint64 right)
{
	if (following_ && showAll_)
		heldAllNs_ = spanNs();
	// The right edge never past the newest reading, and the left edge never
	// before the oldest while more is kept than the span: there is nothing
	// to see out there.
	const auto [oldest, newest] = kept();
	if (newest)
		right = std::clamp(right, std::min(newest, oldest + spanNs()), newest);
	frozenNs_ = right;
	following_ = false;
	Q_EMIT viewChanged();
	update();
}

void TraceView::remember()
{
	// A run of wheel steps or keys is one step back: 600 ms of quiet closes it.
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const bool sameGesture = rememberedAtMs_ && now - rememberedAtMs_ < 600;
	rememberedAtMs_ = now;
	if (sameGesture && !back_.empty())
		return;
	back_.push_back({following_, showAll_, valueAuto_, heldAllNs_, following_ ? 0 : rightNs(), spanSeconds_, manual_.low, manual_.high});
	if (back_.size() > 50)
		back_.erase(back_.begin());
	Q_EMIT viewChanged();
}

void TraceView::goBack()
{
	if (back_.empty())
		return;
	const auto state = back_.back();
	back_.pop_back();
	rememberedAtMs_ = 0;
	following_ = state.following;
	showAll_ = state.showAll;
	heldAllNs_ = state.heldAllNs;
	spanSeconds_ = state.spanSeconds;
	if (!state.following)
		frozenNs_ = state.frozenNs;
	valueAuto_ = state.valueAuto;
	manual_ = {state.manualLow, state.manualHigh};
	if (valueAuto_)
		range_ = {};
	Q_EMIT viewChanged();
	update();
}

namespace {
// The rows of the readings between left and right: every reading time of any
// path, and each path's reading at that time, if it has one.
struct ValueRows {
	std::vector<qint64> times;
	std::vector<std::vector<std::optional<double>>> cells; // [path][row]
};
ValueRows rowsBetween(const WatchTraces& traces, const QStringList& paths, qint64 left, qint64 right)
{
	ValueRows out;
	for (const auto& path : paths) {
		const auto* readings = traces.readings(path);
		if (!readings)
			continue;
		for (auto it = std::lower_bound(readings->begin(), readings->end(), left,
		                                [](const TraceReading& r, qint64 at) { return r.ns < at; });
		     it != readings->end() && it->ns <= right; ++it)
			out.times.push_back(it->ns);
	}
	std::sort(out.times.begin(), out.times.end());
	out.times.erase(std::unique(out.times.begin(), out.times.end()), out.times.end());
	for (const auto& path : paths) {
		std::vector<std::optional<double>> column(out.times.size());
		if (const auto* readings = traces.readings(path)) {
			auto it = std::lower_bound(readings->begin(), readings->end(), left,
			                           [](const TraceReading& r, qint64 at) { return r.ns < at; });
			for (std::size_t row = 0; row < out.times.size() && it != readings->end(); ++row) {
				while (it != readings->end() && it->ns < out.times[row])
					++it;
				if (it != readings->end() && it->ns == out.times[row] && !std::isnan(it->value))
					column[row] = it->value;
			}
		}
		out.cells.push_back(std::move(column));
	}
	return out;
}
} // namespace

int TraceView::readingsInRange() const
{
	if (!traces_)
		return 0;
	const auto [left, right] = measuredNs();
	return int(rowsBetween(*traces_, traces_->plotted(), left, right).times.size());
}

QString TraceView::valuesText(bool csv) const
{
	if (!traces_)
		return {};
	const auto paths = traces_->plotted();
	const auto [left, right] = measuredNs();
	const auto rows = rowsBetween(*traces_, paths, left, right);
	const auto anchor = traces_->anchor();
	const QLocale locale = csv ? QLocale::c() : QLocale();
	const QChar separator = csv ? QLatin1Char(',') : QLatin1Char('\t');
	// A cell with the separator or a quote is quoted, as CSV wants.
	const auto cell = [&](const QString& text) {
		if (!csv || !(text.contains(separator) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n'))))
			return text;
		auto quoted = text;
		quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
		return QLatin1Char('"') + quoted + QLatin1Char('"');
	};
	QStringList lines;
	QStringList header{tr("Time of day"), tr("Seconds from start")};
	for (const auto& path : paths)
		header << cell(path);
	lines << header.join(separator);
	for (std::size_t row = 0; row < rows.times.size(); ++row) {
		const auto ns = rows.times[row];
		const auto utc = double(anchor.utcMs) + double(ns - anchor.monotonicNs) / 1e6;
		QStringList fields{QDateTime::fromMSecsSinceEpoch(qint64(utc)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
		                   locale.toString(double(ns - anchor.monotonicNs) / 1e9, 'f', 3)};
		for (int column = 0; column < paths.size(); ++column) {
			const auto& value = rows.cells[std::size_t(column)][row];
			if (!value) {
				fields << QString();
				continue;
			}
			const auto words = traces_->categorical(paths[column]) ? traces_->stateText(paths[column], *value) : QString();
			fields << cell(words.isEmpty() ? locale.toString(*value, 'g', 15) : words);
		}
		lines << fields.join(separator);
	}
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString TraceView::saveValues(const QUrl& url) const
{
	const auto path = url.isLocalFile() ? url.toLocalFile() : url.toString();
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return tr("%1 could not be written: %2").arg(path, file.errorString());
	file.write(valuesText(true).toUtf8());
	if (!file.commit())
		return tr("%1 could not be written: %2").arg(path, file.errorString());
	return {};
}

void TraceView::zoomToBox(QPointF from, QPointF to)
{
	// Clipped to the plot by hand: QRectF::intersected makes a box with no
	// height (a level drag, which zooms the time only) an empty one.
	const QRectF drawn = QRectF(from, to).normalized();
	const QRectF box(QPointF(std::max(drawn.left(), plotArea_.left()), std::max(drawn.top(), plotArea_.top())),
	                 QPointF(std::min(drawn.right(), plotArea_.right()), std::min(drawn.bottom(), plotArea_.bottom())));
	if (box.width() < 4 || plotArea_.width() <= 0)
		return;
	remember();
	const auto span = spanNs();
	const auto right = rightNs();
	const auto left = right - span;
	const auto nsAt = [&](qreal x) { return left + qint64(double(span) * (x - plotArea_.left()) / plotArea_.width()); };
	const auto newLeft = nsAt(box.left()), newRight = nsAt(box.right());
	showAll_ = false;
	heldAllNs_ = 0;
	spanSeconds_ = std::max(0.01, double(newRight - newLeft) / 1e9);
	// The values too, from a box over the numbers at least 8 px tall.
	if (box.height() >= 8 && shown_.valid() && valuesArea_.height() > 0 && box.top() >= valuesArea_.top() - 1 &&
	    box.top() <= valuesArea_.bottom()) {
		const auto valueAt = [&](qreal y) {
			return shown_.high - (y - valuesArea_.top()) / valuesArea_.height() * (shown_.high - shown_.low);
		};
		const double high = valueAt(std::max(box.top(), valuesArea_.top())), low = valueAt(std::min(box.bottom(), valuesArea_.bottom()));
		if (high > low) {
			valueAuto_ = false;
			manual_ = {low, high};
		}
	}
	following_ = false;
	frozenNs_ = newRight;
	Q_EMIT viewChanged();
	update();
}

void TraceView::zoom(qreal factor, qreal at)
{
	if (!(factor > 0))
		return;
	remember();
	const auto span = spanNs();
	const auto right = rightNs();
	at = std::clamp<qreal>(at, 0, 1);
	const auto pinned = right - span + qint64(double(span) * at);
	const double next = std::clamp(double(span) * factor, 1e7, 86400e9);
	// Out as far as what is kept, and no further: that is All.
	const auto [oldest, newest] = kept();
	if (newest && factor > 1 && next >= double(newest - oldest)) {
		showAll_ = true;
		heldAllNs_ = std::max<qint64>(1'000'000'000, newest - oldest);
		frozenNs_ = newest;
		following_ = false;
		Q_EMIT viewChanged();
		update();
		return;
	}
	showAll_ = false;
	heldAllNs_ = 0;
	spanSeconds_ = next / 1e9;
	// The time under the pointer stays under it.
	hold(pinned + qint64((double(right - pinned)) * next / double(span)));
}

void TraceView::pan(qreal part)
{
	remember();
	hold(rightNs() + qint64(double(spanNs()) * part));
}

void TraceView::toOldest()
{
	const auto [oldest, newest] = kept();
	if (!newest)
		return;
	remember();
	hold(oldest + spanNs());
}

void TraceView::setFromStart(bool fromStart)
{
	if (fromStart == fromStart_)
		return;
	fromStart_ = fromStart;
	Q_EMIT viewChanged();
	update();
}

void TraceView::setChipColor(const QColor& color)
{
	if (color == chipColor_)
		return;
	chipColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setChipHeight(qreal height)
{
	height = std::max<qreal>(0, height);
	if (qFuzzyCompare(height + 1, chipHeight_ + 1))
		return;
	chipHeight_ = height;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setGapColor(const QColor& color)
{
	if (color == gapColor_)
		return;
	gapColor_ = color;
	Q_EMIT styleChanged();
	update();
}

QColor TraceView::curveColor(const QString& path) const
{
	if (traces_) {
		if (const auto own = traces_->color(path); !own.isEmpty())
			return QColor(own);
		const int at = traces_->colorIndex(path);
		if (at >= 0 && at < colors_.size())
			return colors_[at].value<QColor>();
	}
	return labelColor_;
}

void TraceView::setFocusedPath(const QString& path)
{
	if (path == focusedPath_)
		return;
	focusedPath_ = path;
	// Focusing a curve, or leaving it, fits the values afresh.
	valueAuto_ = true;
	range_ = {};
	Q_EMIT viewChanged();
	update();
}

void TraceView::zoomValues(qreal factor, qreal at)
{
	if (!(factor > 0) || !shown_.valid())
		return;
	remember();
	const double pinned = shown_.high - std::clamp<qreal>(at, 0, 1) * (shown_.high - shown_.low);
	Range next{pinned - (pinned - shown_.low) * factor, pinned + (shown_.high - pinned) * factor};
	if (!(next.high - next.low > std::abs(pinned) * 1e-12 + 1e-300) || next.low < -valueLimit || next.high > valueLimit)
		return;
	valueAuto_ = false;
	manual_ = next;
	Q_EMIT viewChanged();
	update();
}

void TraceView::panValues(qreal part)
{
	if (!shown_.valid())
		return;
	remember();
	const double shift = (shown_.high - shown_.low) * part;
	valueAuto_ = false;
	manual_ = {shown_.low + shift, shown_.high + shift};
	Q_EMIT viewChanged();
	update();
}

void TraceView::autoValues()
{
	if (valueAuto_)
		return;
	remember();
	valueAuto_ = true;
	range_ = {};
	Q_EMIT viewChanged();
	update();
}

void TraceView::mousePressEvent(QMouseEvent* event)
{
	forceActiveFocus(Qt::MouseFocusReason);
	pressX_ = event->position().x();
	pressY_ = event->position().y();
	pressRightNs_ = rightNs();
	pressRange_ = shown_;
	valueDrag_ = event->position().x() < plotArea_.left();
	dragging_ = false;
	boxing_ = !valueDrag_ && (event->modifiers() & Qt::ControlModifier) && plotArea_.contains(event->position());
	boxFrom_ = boxTo_ = event->position();
	event->accept();
}

void TraceView::mouseDoubleClickEvent(QMouseEvent* event)
{
	// On the value labels: fitted again.
	if (event->position().x() < plotArea_.left())
		autoValues();
	event->accept();
}

void TraceView::mouseMoveEvent(QMouseEvent* event)
{
	if (boxing_) {
		boxTo_ = event->position();
		Q_EMIT cursorChanged();
		update();
		event->accept();
		return;
	}
	if (valueDrag_) {
		// Over the value labels: the values follow the pointer up and down.
		const qreal down = event->position().y() - pressY_;
		if (!dragging_ && std::abs(down) < 4)
			return;
		if (!dragging_) {
			setCursor(Qt::ClosedHandCursor);
			remember();
		}
		dragging_ = true;
		if (pressRange_.valid() && valuesArea_.height() > 0) {
			const double shift = (pressRange_.high - pressRange_.low) * down / valuesArea_.height();
			valueAuto_ = false;
			manual_ = {pressRange_.low + shift, pressRange_.high + shift};
			Q_EMIT viewChanged();
			update();
		}
		event->accept();
		return;
	}
	const qreal moved = event->position().x() - pressX_;
	// A drag of more than a few pixels pans; less is a click.
	if (!dragging_ && std::abs(moved) < 4)
		return;
	if (!dragging_) {
		setCursor(Qt::ClosedHandCursor);
		remember();
	}
	dragging_ = true;
	const qreal widthPx = std::max<qreal>(1, plotArea_.width());
	hold(pressRightNs_ - qint64(double(spanNs()) * moved / widthPx));
	event->accept();
}

void TraceView::mouseReleaseEvent(QMouseEvent* event)
{
	if (boxing_) {
		boxing_ = false;
		zoomToBox(boxFrom_, event->position());
		Q_EMIT cursorChanged();
		update();
		event->accept();
		return;
	}
	if (dragging_) {
		setCursor(Qt::CrossCursor);
	} else if (!valueDrag_ && plotArea_.contains(event->position())) {
		// A click: on the reference takes it away, anywhere else pins it
		// where the cursor snapped.
		setCursorAt(event->position().x());
		if (endShown_ && std::abs(event->position().x() - endX_) <= 4)
			clearEnd();
		else if (referenceShown_ && std::abs(event->position().x() - referenceX_) <= 4)
			clearReference();
		else if (event->modifiers() & Qt::ShiftModifier)
			pinEnd();
		else
			pinReference();
	}
	dragging_ = false;
	valueDrag_ = false;
	event->accept();
}

void TraceView::wheelEvent(QWheelEvent* event)
{
	const qreal steps = event->angleDelta().y() / 120.0;
	if (steps == 0) {
		event->ignore();
		return;
	}
	// Ctrl over the graph, or over the value labels: the values.
	if ((event->modifiers() & Qt::ControlModifier) || event->position().x() < plotArea_.left()) {
		const qreal heightPx = std::max<qreal>(1, valuesArea_.height());
		zoomValues(std::pow(0.8, steps), (event->position().y() - valuesArea_.top()) / heightPx);
		event->accept();
		return;
	}
	const qreal widthPx = std::max<qreal>(1, plotArea_.width());
	zoom(std::pow(0.8, steps), (event->position().x() - plotArea_.left()) / widthPx);
	event->accept();
}

void TraceView::keyPressEvent(QKeyEvent* event)
{
	const bool shift = event->modifiers() & Qt::ShiftModifier;
	const bool control = event->modifiers() & Qt::ControlModifier;
	switch (event->key()) {
		case Qt::Key_Up:
			if (control)
				zoomValues(0.8);
			else
				panValues(0.1);
			break;
		case Qt::Key_Down:
			if (control)
				zoomValues(1.25);
			else
				panValues(-0.1);
			break;
		case Qt::Key_A:
			autoValues();
			break;
		case Qt::Key_Backspace:
			goBack();
			break;
		case Qt::Key_Left:
			if (event->modifiers() & Qt::AltModifier)
				goBack();
			else if (control)
				stepChange(-1);
			else if (shift)
				stepCursor(-1);
			else
				pan(-0.1);
			break;
		case Qt::Key_Right:
			if (control)
				stepChange(1);
			else if (shift)
				stepCursor(1);
			else
				pan(0.1);
			break;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (!cursorShown_)
				stepCursor(0);
			if (shift)
				pinEnd();
			else
				pinReference();
			break;
		case Qt::Key_Escape:
			// The end first, then the reference, then the keyboard's cursor.
			if (endShown_)
				clearEnd();
			else if (referenceShown_)
				clearReference();
			else if (cursorShown_)
				hideCursor();
			else
				event->ignore();
			if (!event->isAccepted())
				return;
			break;
		case Qt::Key_Plus:
		case Qt::Key_Equal:
			zoom(0.8);
			break;
		case Qt::Key_Minus:
		case Qt::Key_Underscore:
			zoom(1.25);
			break;
		case Qt::Key_Home:
			toOldest();
			break;
		case Qt::Key_End:
			follow();
			break;
		case Qt::Key_Space:
		case Qt::Key_H:
			if (following_)
				hold(rightNs());
			else
				follow();
			break;
		case Qt::Key_1:
			choosePreset(10);
			break;
		case Qt::Key_2:
			choosePreset(60);
			break;
		case Qt::Key_3:
			choosePreset(600);
			break;
		case Qt::Key_4:
			choosePreset(0);
			break;
		default:
			event->ignore();
			return;
	}
	event->accept();
}

void TraceView::setColors(const QVariantList& colors)
{
	colors_ = colors;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setGridColor(const QColor& color)
{
	if (color == gridColor_)
		return;
	gridColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setLabelColor(const QColor& color)
{
	if (color == labelColor_)
		return;
	labelColor_ = color;
	Q_EMIT styleChanged();
	update();
}

void TraceView::setFont(const QFont& font)
{
	if (font == font_)
		return;
	font_ = font;
	Q_EMIT styleChanged();
	update();
}

qint64 TraceView::rightNs() const
{
	if (!following_)
		return frozenNs_;
	// Following, the right edge is the newest reading: it moves when one
	// comes and rests when reading stops, never sliding over empty time.
	const auto newest = traces_ ? traces_->newestNs() : 0;
	return newest ? newest : WatchTraces::nowNs();
}

void TraceView::schedule()
{
	// Held, the picture stays but how far behind it is still changes.
	if (!frames_.isActive() && isVisible() && traces_ && !traces_->plotted().isEmpty())
		frames_.start();
}

void TraceView::tick()
{
	update();
}

void TraceView::paint(QPainter* painter)
{
	const QFontMetricsF metrics(font_);
	const qreal pad = std::max<qreal>(4, std::round(metrics.height() * 0.3));
	const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
	const qint64 right = rightNs();
	const auto span = spanNs();
	const qint64 left = right - span;
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	// This PC's clock: the anchor's UTC plus the steady clock's difference,
	// kept in fractions of a millisecond so short spans place ticks exactly.
	const auto utcOf = [&anchor](qint64 ns) { return double(anchor.utcMs) + double(ns - anchor.monotonicNs) / double(msNs); };
	// The local time of the time in view, not of now.
	const qint64 offsetMs =
	    qint64(QDateTime::fromMSecsSinceEpoch(qint64(utcOf(right))).offsetFromUtc()) * 1000;
	const QStringList plotted = traces_ ? traces_->plotted() : QStringList{};
	// Numbers share the value axis; a bool or an enum has a lane.
	QStringList numbers, lanes;
	for (const auto& path : plotted)
		(traces_->categorical(path) ? lanes : numbers) << path;

	// The value range of the numbers in view - only the focused curve's
	// while one is focused.
	const QStringList fitted = numbers.contains(focusedPath_) ? QStringList{focusedPath_} : numbers;
	Range wanted;
	bool any = false;
	for (const auto& path : std::as_const(fitted)) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		auto from = std::lower_bound(readings->begin(), readings->end(), left,
		                             [](const TraceReading& r, qint64 at) { return r.ns < at; });
		if (from != readings->begin())
			--from;
		for (auto it = from; it != readings->end() && it->ns <= right; ++it) {
			if (std::isnan(it->value))
				continue;
			const double v = std::clamp(traces_->multiplier(path) * it->value + traces_->offset(path), -valueLimit, valueLimit);
			wanted.low = any ? std::min(wanted.low, v) : v;
			wanted.high = any ? std::max(wanted.high, v) : v;
			any = true;
		}
	}
	if (any) {
		// A flat line gets a band around it; otherwise five per cent above
		// and below.
		if (wanted.high - wanted.low <= std::abs(wanted.high) * 1e-12) {
			const double half = wanted.high == 0 ? 1 : std::abs(wanted.high) * 0.01;
			wanted.low -= half;
			wanted.high += half;
		} else {
			const double margin = (wanted.high - wanted.low) * 0.05;
			wanted.low -= margin;
			wanted.high += margin;
		}
		const auto now = WatchTraces::nowNs();
		if (!range_.valid()) {
			range_ = wanted;
		} else if (wanted.low < range_.low || wanted.high > range_.high) {
			range_ = {std::min(range_.low, wanted.low), std::max(range_.high, wanted.high)};
			narrowSinceNs_ = 0;
		} else if (wanted.high - wanted.low < (range_.high - range_.low) * 0.8) {
			if (!narrowSinceNs_)
				narrowSinceNs_ = now;
			if (now - narrowSinceNs_ >= 2'000'000'000 || !following_) {
				range_ = wanted;
				narrowSinceNs_ = 0;
			}
		} else {
			narrowSinceNs_ = 0;
		}
	}
	const Range values = !valueAuto_ && manual_.valid() ? manual_ : range_.valid() ? range_ : Range{0, 1};
	shown_ = values;

	// The value labels first, so the plot starts after the widest.
	const qreal labelsHeight = metrics.height() * 2 + pad + bottomInset_;
	const qreal top = pad, bottom = std::max<qreal>(top + 1, height() - labelsHeight);
	// The lanes under the numbers: a row each, at most 40 % of the height
	// (all of it when there are no numbers).
	const qreal laneShare = numbers.isEmpty() ? 1.0 : 0.4;
	const qreal laneH = lanes.isEmpty() ? 0
	                                    : std::max<qreal>(6, std::min(laneHeight_, (bottom - top) * laneShare / lanes.size()));
	const qreal lanesTop = bottom - laneH * lanes.size();
	const double rows = std::max<qreal>(1, (lanesTop - top) / 40.0);
	const double valueStep = niceStep((values.high - values.low) / rows);
	QStringList valueLabels;
	QVector<double> valueTicks;
	for (double v = std::ceil(values.low / valueStep) * valueStep; v <= values.high + valueStep * 1e-9; v += valueStep) {
		valueTicks.push_back(v);
		valueLabels.push_back(valueText(v, valueStep));
		if (valueTicks.size() > 200)
			break;
	}
	// At least a usual label's width, so the plot does not move sideways
	// whenever the range gains a digit.
	qreal labelWidth = metrics.horizontalAdvance(QStringLiteral("-000.00"));
	for (const auto& text : std::as_const(valueLabels))
		labelWidth = std::max(labelWidth, metrics.horizontalAdvance(text));
	const QRectF area(QPointF(std::ceil(labelWidth + pad * 2), top), QPointF(std::max<qreal>(labelWidth + pad * 3, width() - pad), bottom));
	const auto xOf = [&](qint64 ns) { return area.left() + (double(ns - left) / double(span)) * area.width(); };
	// The numbers' part of the area, above the lanes.
	const QRectF numbersArea(area.topLeft(), QPointF(area.right(), lanesTop));
	const auto yOf = [&](double v) {
		return numbersArea.bottom() - (v - values.low) / (values.high - values.low) * numbersArea.height();
	};

	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setFont(font_);
	// The grid and the value axis.
	QPen grid(gridColor_, 1);
	grid.setCosmetic(true);
	painter->setPen(grid);
	painter->drawRect(area);
	for (int i = 0; i < valueTicks.size() && !numbers.isEmpty(); ++i) {
		const qreal y = std::round(yOf(valueTicks[i])) + 0.5;
		painter->setPen(grid);
		painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
		painter->setPen(labelColor_);
		painter->drawText(QRectF(0, y - metrics.height() / 2, area.left() - pad, metrics.height()),
		                  Qt::AlignRight | Qt::AlignVCenter, valueLabels[i]);
	}

	// Time of day: ticks on whole clock times, labels far enough apart.
	const double pxPerMs = area.width() / (double(span) / msNs);
	qint64 timeStep = timeSteps.back();
	for (const auto step : timeSteps)
		if (step * pxPerMs >= (step < 1000 ? 100.0 : 85.0)) {
			timeStep = step;
			break;
		}
	QStringList timeLabels;
	QVariantList timeLabelRects;
	// A label under its tick, drawn only whole: one the edge would cut reads
	// as another time ("14:14:4"). Its grid line stays either way.
	const auto timeLabel = [&](qreal x, const QString& text, const QString& widest) {
		const qreal w = metrics.horizontalAdvance(widest) + pad;
		if (x - w / 2 < 0 || x + w / 2 > width())
			return;
		const QRectF box(x - w / 2, area.bottom() + pad / 2, w, metrics.height() * 2);
		timeLabels.push_back(text);
		timeLabelRects.push_back(box);
		painter->setPen(labelColor_);
		painter->drawText(box, Qt::AlignHCenter | Qt::AlignTop, text);
	};
	// From the start: the first reading of this run, or a recording's start -
	// the anchor, so a reading has one time from start in every view, across
	// any stop, in the CSV as on the axis.
	const qint64 originNs = anchor.monotonicNs;
	originUtcMs_ = anchor.monotonicNs ? qreal(anchor.utcMs) : 0;
	int ticks = 0;
	if (anchor.monotonicNs && fromStart_) {
		const double leftMs = double(left - originNs) / double(msNs), rightMs = double(right - originNs) / double(msNs);
		for (qint64 at = qint64(std::ceil(leftMs / double(timeStep))) * timeStep; double(at) <= rightMs; at += timeStep) {
			const qreal x = std::round(area.left() + (double(at) - leftMs) * pxPerMs) + 0.5;
			painter->setPen(grid);
			painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
			const auto text = elapsedText(at, timeStep);
			timeLabel(x, text, text);
			if (++ticks > 400)
				break;
		}
	} else if (anchor.monotonicNs) {
		const double leftLocal = utcOf(left) + double(offsetMs), rightLocal = utcOf(right) + double(offsetMs);
		qint64 previousDay = std::numeric_limits<qint64>::min();
		for (qint64 local = qint64(std::ceil(leftLocal / double(timeStep))) * timeStep; double(local) <= rightLocal; local += timeStep) {
			const qreal x = std::round(area.left() + (double(local) - leftLocal) * pxPerMs) + 0.5;
			painter->setPen(grid);
			painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
			auto text = clockText(local, timeStep);
			// Past midnight, the day under the time.
			const qint64 day = local >= 0 ? local / dayMs : (local - dayMs + 1) / dayMs;
			if (previousDay != std::numeric_limits<qint64>::min() && day != previousDay)
				text += QLatin1Char('\n') +
				        QLocale().toString(QDateTime::fromMSecsSinceEpoch(local - offsetMs).date(), QStringLiteral("d MMM"));
			previousDay = day;
			timeLabel(x, text, text.section(QLatin1Char('\n'), 0, 0));
			if (++ticks > 400)
				break;
		}
	}

	// The lines: raw while two readings or fewer fall on each device pixel,
	// else each pixel's first, lowest, highest and last.
	// Where the Watchlist was stopped or the device gone: a band under the
	// curves, captioned with what and when where the words fit (LP-1g).
	int gaps = 0, ends = 0;
	if (traces_) {
		painter->save();
		painter->setClipRect(area);
		for (const auto& pause : traces_->pauses()) {
			const qint64 to = pause.toNs ? pause.toNs : std::max(right, pause.fromNs);
			if (to <= left || pause.fromNs >= right)
				continue;
			const qreal x0 = xOf(std::max(pause.fromNs, left)), x1 = xOf(std::min(to, right));
			if (x1 - x0 < 1)
				continue;
			const QRectF band(x0, area.top(), x1 - x0, area.height());
			painter->fillRect(band, gapColor_);
			++gaps;
			const auto at = QDateTime::fromMSecsSinceEpoch(qint64(utcOf(pause.fromNs))).toString(QStringLiteral("HH:mm:ss"));
			const auto words = pause.reason == WatchTraces::PauseReason::Disconnected ? tr("Disconnected %1").arg(at)
			                                                                          : tr("Watchlist stopped %1").arg(at);
			if (metrics.horizontalAdvance(words) + 2 * pad <= band.width()) {
				painter->setPen(labelColor_);
				painter->setFont(font_);
				painter->drawText(QPointF(x0 + pad, area.top() + pad + metrics.ascent()), words);
			}
		}
		painter->restore();
	}
	int points = 0, raw = 0, curves = 0, dots = 0;
	const double pixels = area.width() * ratio;
	painter->save();
	painter->setClipRect(numbersArea);
	for (const auto& path : std::as_const(numbers)) {
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		auto from = std::lower_bound(readings->begin(), readings->end(), left,
		                             [](const TraceReading& r, qint64 at) { return r.ns < at; });
		if (from != readings->begin())
			--from;
		auto to = std::upper_bound(from, readings->end(), right, [](qint64 at, const TraceReading& r) { return at < r.ns; });
		if (to != readings->end())
			++to;
		const auto count = std::distance(from, to);
		if (count <= 0)
			continue;
		const QColor color = curveColor(path);
		// Drawn as k x value + b; the readings stay as they were read.
		const double k = traces_->multiplier(path), b = traces_->offset(path);
		const auto drawn = [k, b](double v) { return std::clamp(k * v + b, -valueLimit, valueLimit); };
		// The focused curve stands out.
		QPen pen(color, path == focusedPath_ ? 2.6 : 1.8);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		painter->setPen(pen);
		const bool step = traces_->categorical(path);
		++curves;
		QVector<QPointF> line;
		const auto flush = [&] {
			if (line.size() == 1) {
				// A lone reading: a dot, so it is seen.
				painter->drawPoint(line.front());
			} else if (line.size() > 1) {
				painter->drawPolyline(line.constData(), int(line.size()));
			}
			points += int(line.size());
			line.clear();
		};
		// Per pixel above twice the width, raw again below one and a half
		// times it, so a following line does not flicker at the boundary.
		bool& perPixel = aggregated_[path];
		perPixel = perPixel ? double(count) > 1.5 * pixels : double(count) > 2.0 * pixels;
		if (!perPixel) {
			++raw;
			qint64 previous = 0, gapAt = 0;
			for (auto it = from; it != to; ++it) {
				if (std::isnan(it->value)) {
					flush();
					previous = 0;
					continue;
				}
				if (previous && it->ns - previous > std::max(pauseNs, 10 * gapAt))
					flush();
				const QPointF at(xOf(it->ns), yOf(drawn(it->value)));
				if (step && !line.isEmpty())
					line.push_back(QPointF(at.x(), line.back().y()));
				line.push_back(at);
				gapAt = previous ? it->ns - previous : 0;
				previous = it->ns;
			}
			flush();
			// Measurement points: a 5 px dot ringed in the background at each
			// reading, never closer than 6 px to the last one - a dot the
			// line's own width would vanish into it.
			if (traces_->pointsShown(path)) {
				painter->save();
				painter->setPen(QPen(dotRingColor_, 1));
				painter->setBrush(color);
				qreal lastX = -1e9;
				for (auto it = from; it != to; ++it) {
					if (std::isnan(it->value) || it->ns < left || it->ns > right)
						continue;
					const qreal x = xOf(it->ns);
					if (x - lastX < 6)
						continue;
					painter->drawEllipse(QPointF(x, yOf(drawn(it->value))), 2.5, 2.5);
					lastX = x;
					++dots;
				}
				painter->restore();
			}
		} else {
			// Per pixel column: from its lowest to its highest reading, joined
			// to the last of the column before - a bar the pen's width, filled.
			// It covers what a line through each column's first, lowest,
			// highest and last covers, at a small part of the cost: stroking
			// such a line, thousands of columns long, took a second a curve.
			const int columns = std::max(1, int(std::ceil(area.width() * ratio)));
			const qreal thick = pen.widthF();
			int column = -1;
			double low = 0, high = 0, last = 0;
			std::optional<qreal> joined; // the column before's last, none after a break
			QVector<QRectF> bars;
			const auto emitColumn = [&] {
				if (column < 0)
					return;
				const qreal x = area.left() + (column + 0.5) / ratio;
				qreal top = yOf(drawn(high)), bottom = yOf(drawn(low));
				if (top > bottom)
					std::swap(top, bottom);
				if (joined) {
					top = std::min(top, *joined);
					bottom = std::max(bottom, *joined);
				}
				bars.push_back(QRectF(x - thick / 2, top - thick / 2, thick, bottom - top + thick));
				joined = yOf(drawn(last));
				points += 4;
			};
			const auto flushBars = [&] {
				if (!bars.isEmpty()) {
					painter->save();
					painter->setRenderHint(QPainter::Antialiasing, false);
					painter->setPen(Qt::NoPen);
					painter->setBrush(color);
					painter->drawRects(bars.constData(), int(bars.size()));
					painter->restore();
					bars.clear();
				}
				joined.reset();
			};
			for (auto it = from; it != to; ++it) {
				if (std::isnan(it->value)) {
					emitColumn();
					column = -1;
					flushBars();
					continue;
				}
				// Readings beyond either edge belong to no pixel in view.
				if (it->ns < left || it->ns > right)
					continue;
				const int at = std::clamp(int((xOf(it->ns) - area.left()) * ratio), 0, columns - 1);
				if (at != column) {
					emitColumn();
					column = at;
					low = high = last = it->value;
				} else {
					low = std::min(low, it->value);
					high = std::max(high, it->value);
					last = it->value;
				}
			}
			emitColumn();
			flushBars();
		}
		// A value no longer watched ends in an open circle at its last reading.
		if (!traces_->watched(path)) {
			auto last = to;
			while (last != from) {
				--last;
				if (!std::isnan(last->value))
					break;
			}
			if (last != to && !std::isnan(last->value) && last->ns >= left && last->ns <= right) {
				painter->save();
				painter->setPen(QPen(color, 1.5));
				painter->setBrush(Qt::NoBrush);
				painter->drawEllipse(QPointF(xOf(last->ns), yOf(drawn(last->value))), 2.5, 2.5);
				painter->restore();
				++ends;
			}
		}
	}
	painter->setClipRect(area);
	QStringList laneLabels, laneStates;
	const qreal inset = std::max<qreal>(2, std::round(laneH * 0.15));
	for (int lane = 0; lane < lanes.size(); ++lane) {
		const auto& path = lanes[lane];
		const QRectF row(area.left(), lanesTop + lane * laneH, area.width(), laneH);
		QPen separator(gridColor_, 1);
		separator.setCosmetic(true);
		painter->setPen(separator);
		painter->drawLine(QPointF(row.left(), std::round(row.top()) + 0.5), QPointF(row.right(), std::round(row.top()) + 0.5));
		// Its leaf name in the value labels' column.
		const auto leaf = path.section(QLatin1Char('.'), -1);
		laneLabels << leaf;
		painter->save();
		painter->setClipping(false);
		painter->setPen(labelColor_);
		painter->drawText(QRectF(0, row.top(), area.left() - pad, row.height()), Qt::AlignRight | Qt::AlignVCenter,
		                  metrics.elidedText(leaf, Qt::ElideMiddle, area.left() - pad));
		painter->restore();
		const auto* readings = traces_->readings(path);
		if (!readings || readings->empty())
			continue;
		++curves;
		++raw;
		const QColor color = curveColor(path);
		const bool flag = traces_->stateText(path, 1.0) == QStringLiteral("true");
		auto from = std::lower_bound(readings->begin(), readings->end(), left,
		                             [](const TraceReading& r, qint64 at) { return r.ns < at; });
		if (from != readings->begin())
			--from;
		const auto newest = traces_->newestNs();
		// Runs of one state: from a reading to the next, the last held to
		// the newest reading of any value.
		for (auto it = from; it != readings->end() && it->ns <= right; ) {
			if (std::isnan(it->value)) {
				++it;
				continue;
			}
			auto end = std::next(it);
			while (end != readings->end() && !std::isnan(end->value) && end->value == it->value)
				++end;
			const qint64 until = end == readings->end() ? newest : end->ns;
			const qreal x0 = std::max(area.left(), xOf(it->ns)), x1 = std::min(area.right(), xOf(until));
			if (x1 > area.left() && x0 < area.right()) {
				const QRectF band(x0, row.top() + inset, std::max<qreal>(1, x1 - x0), laneH - 2 * inset);
				if (flag) {
					if (it->value != 0) {
						painter->fillRect(band, color);
					} else {
						QPen low(color, 1);
						low.setCosmetic(true);
						painter->setPen(low);
						painter->drawLine(QPointF(band.left(), band.bottom() - 0.5), QPointF(band.right(), band.bottom() - 0.5));
					}
				} else {
					painter->fillRect(band, laneFillColor_);
					painter->fillRect(QRectF(band.left(), band.top(), band.width(), 2), color);
					painter->fillRect(QRectF(band.left(), band.top(), 1, band.height()), color);
					const auto name = traces_->stateText(path, it->value);
					if (band.width() >= 24 && laneH >= 12) {
						painter->setPen(laneTextColor_);
						painter->drawText(band.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
						                  metrics.elidedText(name, Qt::ElideRight, band.width() - 6));
						if (!laneStates.contains(name))
							laneStates << name;
					}
				}
			}
			it = end;
		}
	}

	// The reference, dashed, and the cursor with a dot on each number's
	// reading at or before it.
	const auto lineAt = [&](qint64 ns) { return std::round(xOf(ns)) + 0.5; };
	qreal cursorX = -1, referenceX = -1, endX = -1;
	if (referenceShown_ && endShown_) {
		const auto from = std::max(std::min(referenceNs_, endNs_), left), to = std::min(std::max(referenceNs_, endNs_), right);
		if (to > from) {
			QColor band = referenceColor_;
			band.setAlphaF(0.1);
			painter->fillRect(QRectF(QPointF(xOf(from), area.top()), QPointF(xOf(to), area.bottom())), band);
		}
		if (endNs_ >= left && endNs_ <= right) {
			endX = lineAt(endNs_);
			QPen dashed(referenceColor_, 1, Qt::DashLine);
			dashed.setCosmetic(true);
			painter->setPen(dashed);
			painter->drawLine(QPointF(endX, area.top()), QPointF(endX, area.bottom()));
		}
	}
	if (referenceShown_ && referenceNs_ >= left && referenceNs_ <= right) {
		referenceX = lineAt(referenceNs_);
		QPen dashed(referenceColor_, 1, Qt::DashLine);
		dashed.setCosmetic(true);
		painter->setPen(dashed);
		painter->drawLine(QPointF(referenceX, area.top()), QPointF(referenceX, area.bottom()));
	}
	if (cursorShown_ && cursorNs_ >= left && cursorNs_ <= right) {
		cursorX = lineAt(cursorNs_);
		QPen line(cursorColor_, 1);
		line.setCosmetic(true);
		painter->setPen(line);
		painter->drawLine(QPointF(cursorX, area.top()), QPointF(cursorX, area.bottom()));
		for (const auto& path : std::as_const(numbers)) {
			const auto* readings = traces_->readings(path);
			const auto* at = readings ? atOrBefore(*readings, cursorNs_) : nullptr;
			if (!at || std::isnan(at->value) || at->ns < left)
				continue;
			const QColor color = curveColor(path);
			painter->setPen(QPen(dotRingColor_, 1));
			painter->setBrush(color);
			const double drawnAt = traces_->multiplier(path) * at->value + traces_->offset(path);
			painter->drawEllipse(QPointF(xOf(at->ns), yOf(std::clamp(drawnAt, -valueLimit, valueLimit))), 3.5, 3.5);
		}
		painter->setBrush(Qt::NoBrush);
	}
	painter->restore();

	bool changed = timeLabels != timeLabels_ || valueLabels != valueLabels_ || points != drawnPoints_ ||
	               raw != rawCurves_ || curves != drawnCurves_ || area != plotArea_ ||
	               laneLabels != laneLabels_ || laneStates != laneStates_ || timeLabelRects != timeLabelRects_;
	laneLabels_ = laneLabels;
	laneStates_ = laneStates;
	timeLabels_ = timeLabels;
	timeLabelRects_ = timeLabelRects;
	valueLabels_ = valueLabels;
	drawnPoints_ = points;
	rawCurves_ = raw;
	drawnCurves_ = curves;
	// The box being drawn (Ctrl and a drag): its frame, and its size in time
	// and values.
	QRectF boxCaption;
	if (boxing_) {
		const QRectF box = QRectF(boxFrom_, boxTo_).normalized().intersected(area);
		painter->save();
		QColor fill = referenceColor_;
		fill.setAlphaF(0.12);
		painter->fillRect(box, fill);
		QPen frame(referenceColor_, 1, Qt::DashLine);
		frame.setCosmetic(true);
		painter->setPen(frame);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(box);
		const double seconds = double(span) / 1e9 * box.width() / std::max<qreal>(1, area.width());
		QString size = QString::number(seconds, 'f', seconds < 10 ? 3 : 1) + QStringLiteral(" s");
		if (box.height() >= 8 && shown_.valid() && valuesArea_.height() > 0)
			size += QStringLiteral(" × ") +
			        QString::number((shown_.high - shown_.low) * box.height() / valuesArea_.height(), 'g', 3);
		// On a chip, as the cursor's time is: framed in the reference colour,
		// at the corner under the pointer - outside the box where it fits,
		// always inside the plot area - where the eye already is.
		painter->setFont(font_);
		const qreal w = metrics.horizontalAdvance(size) + pad, h = chipHeight_ > 0 ? chipHeight_ : metrics.height() + pad / 2;
		const QPointF corner(std::clamp(boxTo_.x(), area.left(), area.right()), std::clamp(boxTo_.y(), area.top(), area.bottom()));
		const bool rightward = boxTo_.x() >= boxFrom_.x(), downward = boxTo_.y() >= boxFrom_.y();
		const qreal gap = pad / 2;
		const qreal outX = rightward ? corner.x() + gap : corner.x() - gap - w, inX = rightward ? corner.x() - w : corner.x();
		const qreal outY = downward ? corner.y() + gap : corner.y() - gap - h, inY = downward ? corner.y() - h : corner.y();
		QPointF at(inX, inY);
		for (const QPointF place : {QPointF(outX, outY), QPointF(inX, outY), QPointF(outX, inY)})
			if (place.x() >= area.left() && place.x() + w <= area.right() && place.y() >= area.top() && place.y() + h <= area.bottom()) {
				at = place;
				break;
			}
		at = QPointF(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() - w)),
		             std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() - h)));
		const QRectF chip(at, QSizeF(w, h));
		QPen edge(referenceColor_, 1);
		edge.setCosmetic(true);
		painter->setPen(edge);
		painter->setBrush(chipColor_);
		// Type.radius.
		painter->drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
		painter->setPen(laneTextColor_);
		painter->drawText(chip, Qt::AlignCenter, size);
		painter->restore();
		boxCaption = chip;
	}
	changed = changed || boxCaption != boxCaptionRect_;
	boxCaptionRect_ = boxCaption;
	plotArea_ = area;
	const bool dotsChanged = dots != drawnDots_ || gaps != drawnGaps_ || ends != drawnEnds_;
	drawnDots_ = dots;
	drawnGaps_ = gaps;
	drawnEnds_ = ends;
	valuesArea_ = numbersArea;
	rightUtcMs_ = qreal(utcOf(right));
	const auto newest = traces_ ? traces_->newestNs() : 0;
	const qreal behind = !following_ && newest > right ? qreal(newest - right) / 1e9 : 0;
	const bool behindChanged = !qFuzzyCompare(behind + 1, behindSeconds_ + 1);
	behindSeconds_ = behind;
	const bool linesMoved = !qFuzzyCompare(cursorX + 2, cursorX_ + 2) || !qFuzzyCompare(referenceX + 2, referenceX_ + 2) ||
	                        !qFuzzyCompare(endX + 2, endX_ + 2);
	cursorX_ = cursorX;
	referenceX_ = referenceX;
	endX_ = endX;
	// A path no longer plotted forgets how it was drawn.
	for (auto it = aggregated_.begin(); it != aggregated_.end();)
		it = plotted.contains(it.key()) ? std::next(it) : aggregated_.erase(it);
	if (changed || following_ || behindChanged || linesMoved || dotsChanged)
		QMetaObject::invokeMethod(this, &TraceView::drawn, Qt::QueuedConnection);
}
} // namespace telemetry::ux
