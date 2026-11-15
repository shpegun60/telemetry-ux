// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#include "TraceView.hpp"
#include "WatchTraces.hpp"
#include <QtCore/QDateTime>
#include <QtCore/QLocale>
#include <QtCore/QTime>
#include <QtCore/QVector>
#include <QtGui/QFontMetricsF>
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
// breaks the line: time not read is not drawn as if it were.
constexpr qint64 pauseNs = 2'000'000'000;
// While a frame came this recently, the right edge is now; after it, the
// right edge rests on the newest reading instead of sliding over empty time.
constexpr qint64 liveNs = 10'000'000'000;

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
	frames_.setInterval(50);
	connect(&frames_, &QTimer::timeout, this, &TraceView::tick);
	connect(this, &QQuickItem::visibleChanged, this, &TraceView::schedule);
	connect(this, &TraceView::viewChanged, this, &TraceView::schedule);
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
		connect(traces_, &WatchTraces::appended, this, [this] {
			schedule();
			update();
		});
		connect(traces_, &WatchTraces::plottedChanged, this, [this] {
			schedule();
			update();
		});
	}
	Q_EMIT tracesChanged();
	schedule();
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
	const auto newest = traces_ ? traces_->newestNs() : 0;
	const auto now = WatchTraces::nowNs();
	return newest && now - newest > liveNs ? newest : now;
}

// Moving with now: a frame every 50 ms while following, visible and fed.
void TraceView::schedule()
{
	const bool live = following_ && isVisible() && traces_ && !traces_->plotted().isEmpty() &&
	                  traces_->newestNs() && WatchTraces::nowNs() - traces_->newestNs() <= liveNs;
	if (live && !frames_.isActive())
		frames_.start();
	else if (!live && frames_.isActive())
		frames_.stop();
}

void TraceView::tick()
{
	update();
	schedule();
}

void TraceView::paint(QPainter* painter)
{
	const QFontMetricsF metrics(font_);
	const qreal pad = std::max<qreal>(4, std::round(metrics.height() * 0.3));
	const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
	const qint64 right = rightNs();
	const auto span = qint64(spanSeconds_ * 1e9);
	const qint64 left = right - span;
	const auto anchor = traces_ ? traces_->anchor() : WatchTimestamp{};
	const qint64 offsetMs = qint64(QDateTime::currentDateTime().offsetFromUtc()) * 1000;
	const auto utcOf = [&anchor](qint64 ns) { return anchor.utcMs + (ns - anchor.monotonicNs) / msNs; };
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
			wanted.low = any ? std::min(wanted.low, it->value) : it->value;
			wanted.high = any ? std::max(wanted.high, it->value) : it->value;
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
	qreal labelWidth = 0;
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
		const qint64 leftLocal = utcOf(left) + offsetMs, rightLocal = utcOf(right) + offsetMs;
		qint64 previousDay = std::numeric_limits<qint64>::min();
		for (qint64 local = (leftLocal / timeStep + (leftLocal % timeStep > 0)) * timeStep; local <= rightLocal; local += timeStep) {
			const qreal x = std::round(xOf(left + (local - leftLocal) * msNs)) + 0.5;
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
	const double rawLimit = 2.0 * area.width() * ratio;
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
		QPen pen(color, 1.6);
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
		if (double(count) <= rawLimit) {
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
	if (changed || following_)
		QMetaObject::invokeMethod(this, &TraceView::drawn, Qt::QueuedConnection);
}
} // namespace telemetry::ux
