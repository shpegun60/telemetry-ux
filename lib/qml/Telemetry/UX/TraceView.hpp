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
#include <utility>

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
//
// Moving it (LP-1b): a drag pans, the wheel zooms about the pointer, Left and
// Right pan a tenth, + and - zoom about the middle, Home goes to the oldest
// reading kept, End follows again, Space and H hold or follow, 1 to 4 choose
// 10 s, 1 min, 10 min or All. Panning, zooming and Home stop following;
// reading goes on.
class TraceView : public QQuickPaintedItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(QObject* traces READ traces WRITE setTraces NOTIFY tracesChanged)
	Q_PROPERTY(qreal spanSeconds READ spanSeconds WRITE setSpanSeconds NOTIFY viewChanged)
	Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY viewChanged)
	// Everything kept, oldest to newest, growing while following.
	Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY viewChanged)
	// The chosen span: 10, 60 or 600 seconds, 0 for All, -1 after a zoom.
	Q_PROPERTY(int preset READ preset NOTIFY viewChanged)
	// Held, how far the right edge is behind the newest reading.
	Q_PROPERTY(qreal behindSeconds READ behindSeconds NOTIFY drawn)
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
	bool showAll() const { return showAll_; }
	void setShowAll(bool all);
	int preset() const;
	qreal behindSeconds() const { return behindSeconds_; }

	// 10, 60 or 600 seconds, or 0 for All; following the newest reading.
	Q_INVOKABLE void choosePreset(int seconds);
	// The span times factor (below 1 zooms in), about a point of the width
	// (0 left, 1 right); stops following.
	Q_INVOKABLE void zoom(qreal factor, qreal at = 0.5);
	// By a part of the span (negative: back in time); stops following.
	Q_INVOKABLE void pan(qreal part);
	// The oldest reading kept at the left edge; stops following.
	Q_INVOKABLE void toOldest();
	Q_INVOKABLE void follow() { setFollowing(true); }
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

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

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
	qint64 spanNs() const;
	// The oldest and newest reading of the plotted values; 0 when none.
	std::pair<qint64, qint64> kept() const;
	// Stops following with the right edge at right, inside what is kept.
	void hold(qint64 right);
	void tick();
	void schedule();

	QPointer<WatchTraces> traces_;
	QTimer frames_;
	qreal spanSeconds_ = 60;
	bool following_ = true, showAll_ = false;
	// All, held: its span when it was held, so new readings do not
	// rescale a picture that should stand still.
	qint64 heldAllNs_ = 0;
	qreal behindSeconds_ = 0;
	// A drag: where it started and the right edge then.
	qreal pressX_ = 0;
	qint64 pressRightNs_ = 0;
	bool dragging_ = false;
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
