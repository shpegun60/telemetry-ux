// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#ifndef TELEMETRY_UX_TRACE_VIEW_HPP
#define TELEMETRY_UX_TRACE_VIEW_HPP
#pragma once
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

namespace telemetry::ux {
class WatchTraces;

// Draws the plotted values of a WatchTraces: the grid, the time of day under
// it, one value axis every number shares, and a line per value - its raw
// readings while there are at most two for each device pixel of the width,
// else each pixel's first, lowest, highest and last reading, so no peak is
// lost. A reading that brought no number, or a pause in reading, breaks the
// line. Following, the right edge is now and moves with it; otherwise it
// stays where it was. Painted by QPainter, so it draws alike on every Qt Quick
// backend, the software one included.
class TraceView : public QQuickPaintedItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(QObject* traces READ traces WRITE setTraces NOTIFY tracesChanged)
	Q_PROPERTY(qreal spanSeconds READ spanSeconds WRITE setSpanSeconds NOTIFY viewChanged)
	Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY viewChanged)
	// A colour per palette place (WatchTraces::colorIndex).
	Q_PROPERTY(QVariantList colors READ colors WRITE setColors NOTIFY styleChanged)
	Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
	Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY styleChanged)
	Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged)
	// What the last paint drew, for its checks and its readers.
	Q_PROPERTY(QStringList timeLabels READ timeLabels NOTIFY drawn)
	Q_PROPERTY(QStringList valueLabels READ valueLabels NOTIFY drawn)
	Q_PROPERTY(int drawnPoints READ drawnPoints NOTIFY drawn)
	Q_PROPERTY(int rawCurves READ rawCurves NOTIFY drawn)
	Q_PROPERTY(int drawnCurves READ drawnCurves NOTIFY drawn)
	Q_PROPERTY(QRectF plotArea READ plotArea NOTIFY drawn)
	// The time at the right edge, ms since the epoch on this PC's clock.
	Q_PROPERTY(qreal rightUtcMs READ rightUtcMs NOTIFY drawn)
public:
	explicit TraceView(QQuickItem* parent = nullptr);

	QObject* traces() const;
	void setTraces(QObject* traces);
	qreal spanSeconds() const { return spanSeconds_; }
	void setSpanSeconds(qreal seconds);
	bool following() const { return following_; }
	void setFollowing(bool following);
	QVariantList colors() const { return colors_; }
	void setColors(const QVariantList& colors);
	QColor gridColor() const { return gridColor_; }
	void setGridColor(const QColor& color);
	QColor labelColor() const { return labelColor_; }
	void setLabelColor(const QColor& color);
	QFont font() const { return font_; }
	void setFont(const QFont& font);

	QStringList timeLabels() const { return timeLabels_; }
	QStringList valueLabels() const { return valueLabels_; }
	int drawnPoints() const { return drawnPoints_; }
	int rawCurves() const { return rawCurves_; }
	int drawnCurves() const { return drawnCurves_; }
	QRectF plotArea() const { return plotArea_; }
	qreal rightUtcMs() const { return rightUtcMs_; }

	void paint(QPainter* painter) override;

Q_SIGNALS:
	void tracesChanged();
	void viewChanged();
	void styleChanged();
	void drawn();

private:
	struct Range {
		double low = 0, high = 0;
		bool valid() const { return high > low; }
	};
	qint64 rightNs() const;
	void tick();
	void schedule();

	QPointer<WatchTraces> traces_;
	QTimer frames_;
	qreal spanSeconds_ = 60;
	bool following_ = true;
	qint64 frozenNs_ = 0;
	QVariantList colors_;
	QColor gridColor_{0x3a, 0x3f, 0x47}, labelColor_{0x9a, 0xa4, 0xb0};
	QFont font_;
	// The value axis widens at once and narrows after two seconds, so a
	// following plot does not jump with every reading.
	Range range_;
	qint64 narrowSinceNs_ = 0;
	// Per path: drawn per pixel rather than raw, for the hysteresis.
	QHash<QString, bool> aggregated_;
	QStringList timeLabels_, valueLabels_;
	int drawnPoints_ = 0, rawCurves_ = 0, drawnCurves_ = 0;
	QRectF plotArea_;
	qreal rightUtcMs_ = 0;
};
} // namespace telemetry::ux
#endif
