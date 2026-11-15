// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#include "TraceView.hpp"
#include "WatchTraces.hpp"
#include <QtCore/QDateTime>
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
	if (traces_) {
		connect(traces_, &WatchTraces::appended, this, &TraceView::schedule);
		connect(traces_, &WatchTraces::plottedChanged, this, [this] { update(); });
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
	referenceValues_.clear();
	Q_EMIT cursorChanged();
	update();
}

QVariant TraceView::valueAt(const QString& path) const
{
	if (!cursorShown_ || !traces_)
		return {};
	const auto* readings = traces_->readings(path);
	const auto* at = readings ? atOrBefore(*readings, cursorNs_) : nullptr;
	return at && !std::isnan(at->value) ? QVariant(at->value) : QVariant();
}

QVariant TraceView::referenceValue(const QString& path) const
{
	const auto it = referenceValues_.constFind(path);
	return referenceShown_ && it != referenceValues_.cend() ? QVariant(*it) : QVariant();
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

void TraceView::zoom(qreal factor, qreal at)
{
	if (!(factor > 0))
		return;
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
	hold(rightNs() + qint64(double(spanNs()) * part));
}

void TraceView::toOldest()
{
	const auto [oldest, newest] = kept();
	if (!newest)
		return;
	hold(oldest + spanNs());
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
	if (valueDrag_) {
		// Over the value labels: the values follow the pointer up and down.
		const qreal down = event->position().y() - pressY_;
		if (!dragging_ && std::abs(down) < 4)
			return;
		if (!dragging_)
			setCursor(Qt::ClosedHandCursor);
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
	if (!dragging_)
		setCursor(Qt::ClosedHandCursor);
	dragging_ = true;
	const qreal widthPx = std::max<qreal>(1, plotArea_.width());
	hold(pressRightNs_ - qint64(double(spanNs()) * moved / widthPx));
	event->accept();
}

void TraceView::mouseReleaseEvent(QMouseEvent* event)
{
	if (dragging_) {
		setCursor(Qt::CrossCursor);
	} else if (!valueDrag_ && plotArea_.contains(event->position())) {
		// A click: on the reference takes it away, anywhere else pins it
		// where the cursor snapped.
		setCursorAt(event->position().x());
		if (referenceShown_ && std::abs(event->position().x() - referenceX_) <= 4)
			clearReference();
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
		case Qt::Key_Left:
			if (shift)
				stepCursor(-1);
			else
				pan(-0.1);
			break;
		case Qt::Key_Right:
			if (shift)
				stepCursor(1);
			else
				pan(0.1);
			break;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (!cursorShown_)
				stepCursor(0);
			pinReference();
			break;
		case Qt::Key_Escape:
			// The reference first, then the keyboard's cursor.
			if (referenceShown_)
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
	if (anchor.monotonicNs) {
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
			timeLabels.push_back(text);
			painter->setPen(labelColor_);
			const qreal w = metrics.horizontalAdvance(text.section(QLatin1Char('\n'), 0, 0)) + pad;
			painter->drawText(QRectF(x - w / 2, area.bottom() + pad / 2, w, metrics.height() * 2),
			                  Qt::AlignHCenter | Qt::AlignTop, text);
			if (timeLabels.size() > 400)
				break;
		}
	}

	// The lines: raw while two readings or fewer fall on each device pixel,
	// else each pixel's first, lowest, highest and last.
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
			// Per pixel column: its first, lowest, highest and last.
			const int columns = std::max(1, int(std::ceil(area.width() * ratio)));
			int column = -1;
			double first = 0, low = 0, high = 0, last = 0;
			const auto emitColumn = [&] {
				if (column < 0)
					return;
				const qreal x = area.left() + (column + 0.5) / ratio;
				line.push_back(QPointF(x, yOf(drawn(first))));
				line.push_back(QPointF(x, yOf(drawn(low))));
				line.push_back(QPointF(x, yOf(drawn(high))));
				line.push_back(QPointF(x, yOf(drawn(last))));
			};
			for (auto it = from; it != to; ++it) {
				if (std::isnan(it->value)) {
					emitColumn();
					column = -1;
					flush();
					continue;
				}
				// Readings beyond either edge belong to no pixel in view.
				if (it->ns < left || it->ns > right)
					continue;
				const int at = std::clamp(int((xOf(it->ns) - area.left()) * ratio), 0, columns - 1);
				if (at != column) {
					emitColumn();
					column = at;
					first = low = high = last = it->value;
				} else {
					low = std::min(low, it->value);
					high = std::max(high, it->value);
					last = it->value;
				}
			}
			emitColumn();
			flush();
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
	qreal cursorX = -1, referenceX = -1;
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

	const bool changed = timeLabels != timeLabels_ || valueLabels != valueLabels_ || points != drawnPoints_ ||
	                     raw != rawCurves_ || curves != drawnCurves_ || area != plotArea_ ||
	                     laneLabels != laneLabels_ || laneStates != laneStates_;
	laneLabels_ = laneLabels;
	laneStates_ = laneStates;
	timeLabels_ = timeLabels;
	valueLabels_ = valueLabels;
	drawnPoints_ = points;
	rawCurves_ = raw;
	drawnCurves_ = curves;
	plotArea_ = area;
	const bool dotsChanged = dots != drawnDots_;
	drawnDots_ = dots;
	valuesArea_ = numbersArea;
	rightUtcMs_ = qreal(utcOf(right));
	const auto newest = traces_ ? traces_->newestNs() : 0;
	const qreal behind = !following_ && newest > right ? qreal(newest - right) / 1e9 : 0;
	const bool behindChanged = !qFuzzyCompare(behind + 1, behindSeconds_ + 1);
	behindSeconds_ = behind;
	const bool linesMoved = !qFuzzyCompare(cursorX + 2, cursorX_ + 2) || !qFuzzyCompare(referenceX + 2, referenceX_ + 2);
	cursorX_ = cursorX;
	referenceX_ = referenceX;
	// A path no longer plotted forgets how it was drawn.
	for (auto it = aggregated_.begin(); it != aggregated_.end();)
		it = plotted.contains(it.key()) ? std::next(it) : aggregated_.erase(it);
	if (changed || following_ || behindChanged || linesMoved || dotsChanged)
		QMetaObject::invokeMethod(this, &TraceView::drawn, Qt::QueuedConnection);
}
} // namespace telemetry::ux
