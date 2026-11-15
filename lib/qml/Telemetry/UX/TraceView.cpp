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
	setActiveFocusOnTab(true);
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
	if (!following)
		frozenNs_ = rightNs();
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
	if (showAll_) {
		const auto [oldest, newest] = kept();
		// All: everything kept, and a second at the least.
		return std::max<qint64>(1'000'000'000, newest - oldest);
	}
	return qint64(spanSeconds_ * 1e9);
}

void TraceView::hold(qint64 right)
{
	// Never past the newest reading nor before the oldest: there is nothing
	// to see there.
	const auto [oldest, newest] = kept();
	if (newest)
		right = std::clamp(right, oldest, newest);
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
	showAll_ = false;
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

void TraceView::mousePressEvent(QMouseEvent* event)
{
	forceActiveFocus(Qt::MouseFocusReason);
	pressX_ = event->position().x();
	pressRightNs_ = rightNs();
	dragging_ = false;
	event->accept();
}

void TraceView::mouseMoveEvent(QMouseEvent* event)
{
	const qreal moved = event->position().x() - pressX_;
	// A drag of more than a few pixels pans; less is a click.
	if (!dragging_ && std::abs(moved) < 4)
		return;
	dragging_ = true;
	const qreal widthPx = std::max<qreal>(1, plotArea_.width());
	hold(pressRightNs_ - qint64(double(spanNs()) * moved / widthPx));
	event->accept();
}

void TraceView::mouseReleaseEvent(QMouseEvent* event)
{
	dragging_ = false;
	event->accept();
}

void TraceView::wheelEvent(QWheelEvent* event)
{
	const qreal steps = event->angleDelta().y() / 120.0;
	if (steps == 0) {
		event->ignore();
		return;
	}
	const qreal widthPx = std::max<qreal>(1, plotArea_.width());
	zoom(std::pow(0.8, steps), (event->position().x() - plotArea_.left()) / widthPx);
	event->accept();
}

void TraceView::keyPressEvent(QKeyEvent* event)
{
	switch (event->key()) {
		case Qt::Key_Left:
			pan(-0.1);
			break;
		case Qt::Key_Right:
			pan(0.1);
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

	// The value range of what is in view.
	Range wanted;
	bool any = false;
	for (const auto& path : plotted) {
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
			const double v = std::clamp(it->value, -valueLimit, valueLimit);
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
	const Range values = range_.valid() ? range_ : Range{0, 1};

	// The value labels first, so the plot starts after the widest.
	const qreal labelsHeight = metrics.height() * 2 + pad;
	const qreal top = pad, bottom = std::max<qreal>(top + 1, height() - labelsHeight);
	const double rows = std::max<qreal>(1, (bottom - top) / 40.0);
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
	const auto yOf = [&](double v) { return area.bottom() - (v - values.low) / (values.high - values.low) * area.height(); };

	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setFont(font_);
	// The grid and the value axis.
	QPen grid(gridColor_, 1);
	grid.setCosmetic(true);
	painter->setPen(grid);
	painter->drawRect(area);
	for (int i = 0; i < valueTicks.size(); ++i) {
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
	int points = 0, raw = 0, curves = 0;
	const double pixels = area.width() * ratio;
	painter->save();
	painter->setClipRect(area);
	for (const auto& path : plotted) {
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
		const int colorAt = traces_->colorIndex(path);
		const QColor color = colorAt >= 0 && colorAt < colors_.size() ? colors_[colorAt].value<QColor>() : labelColor_;
		QPen pen(color, 1.8);
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
				const QPointF at(xOf(it->ns), yOf(it->value));
				if (step && !line.isEmpty())
					line.push_back(QPointF(at.x(), line.back().y()));
				line.push_back(at);
				gapAt = previous ? it->ns - previous : 0;
				previous = it->ns;
			}
			flush();
		} else {
			// Per pixel column: its first, lowest, highest and last.
			const int columns = std::max(1, int(std::ceil(area.width() * ratio)));
			int column = -1;
			double first = 0, low = 0, high = 0, last = 0;
			const auto emitColumn = [&] {
				if (column < 0)
					return;
				const qreal x = area.left() + (column + 0.5) / ratio;
				line.push_back(QPointF(x, yOf(first)));
				line.push_back(QPointF(x, yOf(low)));
				line.push_back(QPointF(x, yOf(high)));
				line.push_back(QPointF(x, yOf(last)));
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
	painter->restore();

	const bool changed = timeLabels != timeLabels_ || valueLabels != valueLabels_ || points != drawnPoints_ ||
	                     raw != rawCurves_ || curves != drawnCurves_ || area != plotArea_;
	timeLabels_ = timeLabels;
	valueLabels_ = valueLabels;
	drawnPoints_ = points;
	rawCurves_ = raw;
	drawnCurves_ = curves;
	plotArea_ = area;
	rightUtcMs_ = qreal(utcOf(right));
	const auto newest = traces_ ? traces_->newestNs() : 0;
	const qreal behind = !following_ && newest > right ? qreal(newest - right) / 1e9 : 0;
	const bool behindChanged = !qFuzzyCompare(behind + 1, behindSeconds_ + 1);
	behindSeconds_ = behind;
	// A path no longer plotted forgets how it was drawn.
	for (auto it = aggregated_.begin(); it != aggregated_.end();)
		it = plotted.contains(it.key()) ? std::next(it) : aggregated_.erase(it);
	if (changed || following_ || behindChanged)
		QMetaObject::invokeMethod(this, &TraceView::drawn, Qt::QueuedConnection);
}
} // namespace telemetry::ux
