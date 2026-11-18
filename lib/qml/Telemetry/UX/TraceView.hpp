// The live Plot's drawing: plotted values over time of day (LP-1). MIT.
#ifndef TELEMETRY_UX_TRACE_VIEW_HPP
#define TELEMETRY_UX_TRACE_VIEW_HPP
#pragma once
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVector>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>
#include <optional>
#include <utility>
#include <vector>

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
// Moving it (LP-1b, the gestures slice): a left drag moves the picture in
// time and values - sideways only the time, up or down only the values,
// diagonal both, the other axis joining once the pointer has gone 24 px along
// it; a middle drag moves both with no lock; a right drag or Ctrl and a drag
// zoom to a box; over the value labels or the time labels a drag, the wheel
// and a double-click act on that axis alone (a drag across the time labels,
// up or down, stretches or squeezes the time). The wheel zooms the time about
// the pointer, Ctrl and the wheel the values; Shift, Alt or a sideways wheel
// move instead of zoom. A double-click, or F, fits everything. Esc cancels a
// gesture. Left and Right pan a tenth, + and - zoom about the middle, Home
// goes to the oldest reading kept, End follows again, Space and H hold or
// follow, 1 to 4 choose 10 s, 1 min, 10 min or All. Moving the time by hand
// stops following, and at All leaves All for the span of everything kept;
// reading goes on. The picture may run past either end of the readings until
// only one reading is in view.
//
// The cursor (LP-1c): under the pointer, snapped to the nearest reading within
// 8 px, else free; Shift+Left and Shift+Right step it reading by reading. A
// click, or Enter, pins a reference there - its readings copied, so a ring
// trimmed meanwhile never changes them - and Esc takes it away. valueAt() and
// referenceValue() give each value at the cursor and at the reference, for
// the chips and their differences.
//
// Lanes (LP-1d): a bool or an enum is drawn under the numbers in a lane of its
// own, a row high, never on their axis - a bool as a band while true and a
// line while false, an enum as a band per state with its name where it fits
// and a mark where it changes. Lanes take at most 40 % of the height.
//
// The values (LP-1e): Auto fits every number in view, or only the focused
// curve while one is focused; Manual starts with a value zoom or pan - Ctrl
// and the wheel over the graph, or the wheel over the value labels, zoom about
// the pointer; a drag over the labels or up and down on the graph pans; Up
// and Down pan a tenth, Ctrl+Up and Ctrl+Down zoom - and stays through
// Follow, the presets and time zoom. A double-click on the labels, or A,
// returns to Auto. Auto widens at once and never narrows while a button is
// held or a run of wheel steps goes on.
//
// A curve's look (LP-1f): its own colour, if it has one, and its multiplier
// and offset - a number is drawn and fitted as k x value + b, while every
// value the chips and the cursor give stays the reading - and its
// measurement points: a 5 px dot at each reading while the curve is drawn raw,
// never closer than 6 px to the last.
//
// Stacked (analysis, L): with WatchTraces::stacked, each number is drawn in a
// lane of its own, one above another over the bool and enum lanes, with its
// own values - fitted to it alone while Auto - its leaf name and a colour bar.
// Only while every lane can be at least 40 px tall (stackFits); otherwise the
// numbers share one axis as before. A value zoom or pan - Ctrl and the wheel,
// the wheel or a drag over a lane's labels, Up and Down, a box - acts on the
// lane under the pointer (the keys: else the focused curve's, else every
// lane), a drag on the lane it began in; each lane is Auto or Manual on its
// own; A fits every lane, a double-click on a lane's labels that lane, its
// Auto chip (autoLane) that lane. A click on a lane's name focuses its curve.
// The cursor's dot on each number is in that number's lane.
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
	// Auto (fitted) or Manual (zoomed or panned by hand) values.
	Q_PROPERTY(bool valueAuto READ valueAuto NOTIFY viewChanged)
	// The curve the values are fitted to while Auto; empty for every one.
	Q_PROPERTY(QString focusedPath READ focusedPath WRITE setFocusedPath NOTIFY viewChanged)
	// The numbers' part of the plot area, above the lanes.
	Q_PROPERTY(QRectF valuesArea READ valuesArea NOTIFY drawn)
	// Measurement points the last paint drew, for its checks.
	Q_PROPERTY(int drawnDots READ drawnDots NOTIFY drawn)
	// Bands the last paint drew where nothing was read, and the open circles
	// that end a value no longer watched.
	Q_PROPERTY(int drawnGaps READ drawnGaps NOTIFY drawn)
	// The words of the too-narrow gap band under the pointer, and where.
	Q_PROPERTY(QString hoverGap READ hoverGap NOTIFY hoverGapChanged)
	Q_PROPERTY(qreal hoverGapX READ hoverGapX NOTIFY hoverGapChanged)
	Q_PROPERTY(int drawnEnds READ drawnEnds NOTIFY drawn)
	// A band where the Watchlist was stopped or the device gone.
	Q_PROPERTY(QColor gapColor READ gapColor WRITE setGapColor NOTIFY styleChanged)
	// The cursor: shown, its time on this PC's clock (ms since the epoch),
	// how long before the newest reading, and where it is drawn (item x;
	// outside the plot area when it has scrolled out).
	Q_PROPERTY(bool cursorShown READ cursorShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorUtcMs READ cursorUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorAgoSeconds READ cursorAgoSeconds NOTIFY cursorChanged)
	Q_PROPERTY(qreal cursorX READ cursorX NOTIFY drawn)
	// The pinned reference, likewise.
	Q_PROPERTY(bool referenceShown READ referenceShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal referenceUtcMs READ referenceUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal referenceX READ referenceX NOTIFY drawn)
	Q_PROPERTY(QColor cursorColor READ cursorColor WRITE setCursorColor NOTIFY styleChanged)
	Q_PROPERTY(QColor referenceColor READ referenceColor WRITE setReferenceColor NOTIFY styleChanged)
	Q_PROPERTY(QColor dotRingColor READ dotRingColor WRITE setDotRingColor NOTIFY styleChanged)
	// Room kept under the time labels for the cursor's and the reference's
	// chips, so they never cover a label.
	Q_PROPERTY(qreal bottomInset READ bottomInset WRITE setBottomInset NOTIFY styleChanged)
	// A lane's height, and its colours: an enum state's band and its text.
	Q_PROPERTY(qreal laneHeight READ laneHeight WRITE setLaneHeight NOTIFY styleChanged)
	Q_PROPERTY(QColor laneFillColor READ laneFillColor WRITE setLaneFillColor NOTIFY styleChanged)
	Q_PROPERTY(QColor laneTextColor READ laneTextColor WRITE setLaneTextColor NOTIFY styleChanged)
	// The lanes the last paint drew, by their leaf names, and the states it
	// named in them.
	Q_PROPERTY(QStringList laneLabels READ laneLabels NOTIFY drawn)
	Q_PROPERTY(QStringList laneStates READ laneStates NOTIFY drawn)
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
	// The time axis (LP-1h): the time of day, or the time since the start -
	// the first reading of this run, or a recording's start (the anchor),
	// one fixed moment whatever the view or a stop - whose time of day is
	// originUtcMs.
	Q_PROPERTY(bool fromStart READ fromStart WRITE setFromStart NOTIFY viewChanged)
	// Box zoom and Back (analysis): a right drag, or Ctrl and a drag, draw a
	// box whose width becomes the time in view and whose height the values;
	// a right click opens the host's menu (contextMenuRequested); every move of the
	// view by hand can be gone back on (Backspace, Alt+Left), one gesture a
	// step, the last 50. Not the Undo of edits: views are not changes.
	Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY viewChanged)
	// The end pin (analysis): with the reference, the range measured. Shift
	// and a click, or Shift+Enter, pin it at the cursor - the reference first
	// when there is none; a click on it, or Esc, takes it away.
	Q_PROPERTY(bool endShown READ endShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal endUtcMs READ endUtcMs NOTIFY cursorChanged)
	Q_PROPERTY(qreal endX READ endX NOTIFY drawn)
	Q_PROPERTY(bool boxShown READ boxShown NOTIFY cursorChanged)
	Q_PROPERTY(qreal originUtcMs READ originUtcMs NOTIFY drawn)
	// A chip's fill and height, as the Plot's own chips have them: the box's
	// size is said on one, framed in referenceColor, in laneTextColor.
	Q_PROPERTY(QColor chipColor READ chipColor WRITE setChipColor NOTIFY styleChanged)
	Q_PROPERTY(qreal chipHeight READ chipHeight WRITE setChipHeight NOTIFY styleChanged)
	// Where the last paint drew each time label and the box's size (item
	// coordinates; an empty rect with no box), for its checks.
	Q_PROPERTY(QVariantList timeLabelRects READ timeLabelRects NOTIFY drawn)
	Q_PROPERTY(QRectF boxCaptionRect READ boxCaptionRect NOTIFY drawn)
	// Stacked: the traces' setting; whether every number's lane has room at
	// this height; the lanes the last paint drew, top down - {path, rect,
	// low, high, labels, nameRect, cursorY (-1 with no cursor dot)}, empty
	// while the numbers share one axis.
	Q_PROPERTY(bool stacked READ stacked NOTIFY viewChanged)
	Q_PROPERTY(bool stackFits READ stackFits NOTIFY drawn)
	// Each entry also says whether that lane's values are by hand (manual).
	Q_PROPERTY(QVariantList numberLanes READ numberLanes NOTIFY drawn)
	// Follow is offered (the live Plot, a run being written): Space and H
	// hold or follow only then.
	Q_PROPERTY(bool followable READ followable WRITE setFollowable NOTIFY viewChanged)
	// The part under the pointer: "graph", "values" (the labels beside the
	// numbers), "time" (the labels under the plot), "laneName" (a stacked
	// lane's name), or "" (elsewhere, and while a button is held).
	Q_PROPERTY(QString hoverRegion READ hoverRegion NOTIFY hoverRegionChanged)
	// The shared axis' values as last drawn.
	Q_PROPERTY(qreal valueLow READ valueLow NOTIFY drawn)
	Q_PROPERTY(qreal valueHigh READ valueHigh NOTIFY drawn)
	// The words on the box's chip while a box is drawn; empty with none.
	Q_PROPERTY(QString boxCaption READ boxCaption NOTIFY drawn)
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
	// Fit all (double-click, F): the time to All, following again, every
	// value fitted, no curve focused; one step back. Pins stay.
	Q_INVOKABLE void fitAll();
	bool followable() const { return followable_; }
	void setFollowable(bool followable);
	QString hoverRegion() const { return hoverRegion_; }
	qreal valueLow() const { return shown_.low; }
	qreal valueHigh() const { return shown_.high; }
	QString boxCaption() const { return boxCaption_; }

	bool valueAuto() const { return numberLanes_.empty() ? valueAuto_ : laneManual_.isEmpty(); }
	QString focusedPath() const { return focusedPath_; }
	void setFocusedPath(const QString& path);
	QRectF valuesArea() const { return valuesArea_; }
	int drawnDots() const { return drawnDots_; }
	int drawnGaps() const { return drawnGaps_; }
	QString hoverGap() const { return hoverGap_; }
	qreal hoverGapX() const { return hoverGapX_; }
	int drawnEnds() const { return drawnEnds_; }
	QColor gapColor() const { return gapColor_; }
	void setGapColor(const QColor& color);
	// The colour a path is drawn in: its own, else its palette place's.
	Q_INVOKABLE QColor curveColor(const QString& path) const;
	// The values times factor (below 1 zooms in) about a point of the
	// height (0 top, 1 bottom); Manual.
	Q_INVOKABLE void zoomValues(qreal factor, qreal at = 0.5);
	// The values by a part of their range (positive: higher ones); Manual.
	Q_INVOKABLE void panValues(qreal part);
	// Fitted again: Auto.
	Q_INVOKABLE void autoValues();
	// One stacked lane fitted again (its Auto chip).
	Q_INVOKABLE void autoLane(const QString& path);

	bool cursorShown() const { return cursorShown_; }
	qreal cursorUtcMs() const;
	qreal cursorAgoSeconds() const;
	qreal cursorX() const { return cursorX_; }
	bool referenceShown() const { return referenceShown_; }
	qreal referenceUtcMs() const;
	qreal referenceX() const { return referenceX_; }
	QColor cursorColor() const { return cursorColor_; }
	void setCursorColor(const QColor& color);
	QColor referenceColor() const { return referenceColor_; }
	void setReferenceColor(const QColor& color);
	QColor dotRingColor() const { return dotRingColor_; }
	void setDotRingColor(const QColor& color);
	qreal bottomInset() const { return bottomInset_; }
	void setBottomInset(qreal inset);
	qreal laneHeight() const { return laneHeight_; }
	void setLaneHeight(qreal height);
	QColor laneFillColor() const { return laneFillColor_; }
	void setLaneFillColor(const QColor& color);
	QColor laneTextColor() const { return laneTextColor_; }
	void setLaneTextColor(const QColor& color);
	QStringList laneLabels() const { return laneLabels_; }
	QStringList laneStates() const { return laneStates_; }
	// The cursor at item x, snapped to the nearest reading within 8 px.
	Q_INVOKABLE void setCursorAt(qreal x);
	Q_INVOKABLE void hideCursor();
	// The cursor to the previous (-1) or next (1) reading of any value.
	Q_INVOKABLE void stepCursor(int direction);
	// A reference where the cursor is; taken away again.
	Q_INVOKABLE void pinReference();
	Q_INVOKABLE void clearReference();
	// path's reading at or before the cursor, and at the reference: its
	// number, or undefined when there is none or it brought none.
	Q_INVOKABLE QVariant valueAt(const QString& path) const;
	Q_INVOKABLE QVariant referenceValue(const QString& path) const;
	// path's reading at or before the end pin, likewise; undefined while no
	// end is pinned.
	Q_INVOKABLE QVariant endValue(const QString& path) const;
	// path's reading at or before the right edge of the view: what a recorded
	// run's chip gives with no cursor (TR-2). Undefined likewise.
	Q_INVOKABLE QVariant valueAtRight(const QString& path) const;
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
	bool fromStart() const { return fromStart_; }
	bool canGoBack() const { return !back_.empty(); }
	bool boxShown() const { return boxing_; }
	Q_INVOKABLE void goBack();
	bool endShown() const { return endShown_; }
	qreal endUtcMs() const;
	qreal endX() const { return endX_; }
	Q_INVOKABLE void pinEnd();
	Q_INVOKABLE void clearEnd();
	// The range measured, copied and saved: the reference to the end when
	// both are pinned, else the time in view - {fromUtcMs, toUtcMs, seconds,
	// pinned, readings}.
	// Also {failed, keptFromUtcMs}: the failed reads in it, and when a pinned
	// range starts before the oldest reading kept, the time of that reading.
	Q_INVOKABLE QVariantMap measureRange() const;
	// The words of a gap band too narrow to carry its caption, at x (the
	// view's own pixels); empty elsewhere.
	Q_INVOKABLE QString gapCaptionAt(qreal x) const;
	// A plotted value over that range, from its readings as read (never
	// k x value + b). A number: {count, failed, min, max, minUtcMs, maxUtcMs,
	// mean, peakToPeak, deviation}; a state: {count, failed, changes,
	// shares: [{name, share}]}, the shares by time.
	Q_INVOKABLE QVariantMap statistics(const QString& path) const;
	// Puts the cursor at utcMs, bringing it into view (Go to minimum).
	Q_INVOKABLE void putCursorAt(qreal utcMs);
	// The cursor to the next (direction 1) or previous (-1) change of a state:
	// the focused one's if it is a state, else any lane's (Ctrl+Right,
	// Ctrl+Left). True, and changeFound; false, and findFailed says why,
	// when there is none.
	Q_INVOKABLE bool stepChange(int direction);
	// The readings of every plotted value in the time in view, one row per
	// reading time: "Time of day", "Seconds from start" (from originUtcMs, as
	// the axis and the cursor's chip count them) and a column per
	// path; a missing or failed read is an empty cell, a state is its word,
	// a number is the reading (never k x value + b). For the clipboard (tabs,
	// this PC's decimal sign) or a CSV file ("." and ","). readingsInRange
	// counts its rows.
	Q_INVOKABLE QString valuesText(bool csv) const;
	Q_INVOKABLE int readingsInRange() const;
	// Writes valuesText(true) to the file at url; empty, or why not.
	Q_INVOKABLE QString saveValues(const QUrl& url) const;
	// Zooms to the box between two points of the item: the time to its width
	// when it is at least 8 px wide, the values to its height when it is at
	// least 8 px tall and over the numbers. A box only tall keeps following.
	Q_INVOKABLE void zoomToBox(QPointF from, QPointF to);
	void setFromStart(bool fromStart);
	qreal originUtcMs() const { return originUtcMs_; }
	QColor chipColor() const { return chipColor_; }
	void setChipColor(const QColor& color);
	qreal chipHeight() const { return chipHeight_; }
	void setChipHeight(qreal height);
	QVariantList timeLabelRects() const { return timeLabelRects_; }
	QRectF boxCaptionRect() const { return boxCaptionRect_; }
	bool stacked() const;
	bool stackFits() const { return stackFits_; }
	QVariantList numberLanes() const { return numberLanesShown_; }

	void paint(QPainter* painter) override;

protected:
	bool event(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mouseUngrabEvent() override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void hoverMoveEvent(QHoverEvent* event) override;
	void hoverLeaveEvent(QHoverEvent* event) override;

Q_SIGNALS:
	// A right click (under 4 px) at position, in item coordinates: the host
	// opens the graph's menu there.
	void contextMenuRequested(QPointF position);
	void hoverRegionChanged();
	void tracesChanged();
	// A find that found nothing, in words for the host to say; one that found
	// its change, so the host can take those words away.
	void findFailed(const QString& words);
	void changeFound();
	void viewChanged();
	void cursorChanged();
	void styleChanged();
	void drawn();
	void hoverGapChanged();

private:
	struct Range {
		double low = 0, high = 0;
		bool valid() const { return high > low; }
	};
	// range times factor about a point of its height (0 top, 1 bottom); an
	// invalid one when that is too narrow or too wide to draw.
	static Range zoomed(const Range& range, qreal factor, qreal at);
	qint64 rightNs() const;
	qint64 spanNs() const;
	// The oldest and newest reading of the plotted values; 0 when none.
	std::pair<qint64, qint64> kept() const;
	// Stops following with the right edge at right: between the oldest
	// reading kept and the newest plus one span, so one reading stays in view.
	// All stays All (Space and H).
	void hold(qint64 right);
	// A time move by hand: leaves All for the span of everything kept, then
	// holds at right.
	void moveTime(qint64 right);
	void leaveAll();
	void tick();
	void schedule();

	QPointer<WatchTraces> traces_;
	QTimer frames_;
	qreal spanSeconds_ = 60;
	bool following_ = true, showAll_ = false;
	// All, held: its span when it was held, so new readings do not
	// rescale a picture that should stand still.
	qint64 heldAllNs_ = 0;
	// The cursor's time, and the reference's with each value's reading then.
	bool cursorShown_ = false, referenceShown_ = false;
	qint64 cursorNs_ = 0, referenceNs_ = 0;
	QHash<QString, double> referenceValues_;
	qreal cursorX_ = -1, referenceX_ = -1;
	QPointF hoverAt_{-1, -1};
	QString hoverGap_;
	qreal hoverGapX_ = 0;
	qreal bottomInset_ = 0, laneHeight_ = 22;
	QColor laneFillColor_{0x2a, 0x2f, 0x37}, laneTextColor_{0xe6, 0xed, 0xf3};
	QStringList laneLabels_, laneStates_;
	QColor cursorColor_{0x8b, 0x94, 0x9e}, referenceColor_{0x58, 0xa6, 0xff}, dotRingColor_{0x0d, 0x11, 0x17};
	qreal behindSeconds_ = 0;
	// A press, from the press to the release of its button. Its kind is fixed
	// at the press: a plain left drag, a box (right, or Ctrl and left), a
	// middle drag, a drag on an axis' labels, a right press off the graph (the
	// menu only), or one cancelled (Esc, a double-click) whose rest does
	// nothing.
	enum class Gesture { None, Plain, Box, Middle, ValueAxis, TimeAxis, MenuOnly, Cancelled };
	Gesture gesture_ = Gesture::None;
	Qt::MouseButton gestureButton_ = Qt::NoButton;
	// A second button pressed during a gesture: nothing until every one is up.
	bool ignoreUntilUp_ = false;
	QPointF pressAt_, lastPointer_;
	qint64 pressRightNs_ = 0, pressSpanNs_ = 0;
	bool pressAll_ = false;
	// Past the 4 px that tell a drag from a click.
	bool dragging_ = false;
	// The axes the drag moves, and where each took hold: the pointer's x or y
	// then, the right edge or the values then.
	bool timeEngaged_ = false, valuesEngaged_ = false;
	// Across the time labels, up or down: the time stretched or squeezed.
	bool timeStretch_ = false;
	qreal timeBaseX_ = 0, valueBaseY_ = 0;
	qint64 timeBaseRightNs_ = 0;
	// The values a press can move: the lane it began in while stacked, or
	// the shared axis over the numbers; none over a bool or enum lane.
	bool pressHasValues_ = false;
	// The gesture's one step back, taken when the view first changes.
	bool stepTaken_ = false, stepPushed_ = false;
	void takeStep();
	void startDrag(QPointF moved);
	void dragTo(QPointF at);
	void engageTime(qreal baseX, qint64 baseRight);
	void engageValues(qreal baseY);
	void endGesture(QPointF at);
	void cancelGesture();
	// While a button is held the value-label column keeps its width.
	bool columnFrozen_ = false;
	qreal frozenLeft_ = 0;
	// A run of wheel steps: Auto does not narrow until 600 ms of quiet.
	qint64 lastWheelMs_ = 0;
	QTimer wheelQuiet_;
	bool gestureHolds() const;
	// The pins and the focus at the last press and the one before: a
	// double-click puts back what its first click changed.
	struct Pins {
		bool reference = false, end = false;
		qint64 referenceNs = 0, endNs = 0;
		QHash<QString, double> referenceValues;
		QString focus;
	};
	Pins pressPins_, previousPins_;
	Pins pins() const;
	void restorePins(const Pins& pins);
	bool followable_ = true;
	QString hoverRegion_;
	void setHoverRegion(const QString& region);
	QString regionAt(QPointF at) const;
	static Qt::CursorShape shapeOf(const QString& region);
	// The values under a point by the wheel, Ctrl and the wheel, or a pinch:
	// stacked, the lane there; otherwise the shared axis.
	void zoomValuesAt(qreal factor, QPointF at);
	void panValuesAt(qreal part, QPointF at);
	bool hasNumbers_ = false;
	QString boxCaption_;
	qint64 frozenNs_ = 0;
	QVariantList colors_;
	QColor gridColor_{0x3a, 0x3f, 0x47}, labelColor_{0x9a, 0xa4, 0xb0};
	QFont font_;
	// The value axis widens at once and narrows after two seconds, so a
	// following plot does not jump with every reading.
	Range range_;
	// Manual values, the range last drawn, the one a value drag moves from.
	bool valueAuto_ = true;
	Range manual_, shown_, valueBase_;
	QString focusedPath_;
	QRectF valuesArea_;
	int drawnDots_ = 0, drawnGaps_ = 0, drawnEnds_ = 0;
	struct NarrowGap { qreal x0, x1; QString words; };
	std::vector<NarrowGap> narrowGaps_;
	QColor gapColor_{0x15, 0x1d, 0x27};
	qint64 narrowSinceNs_ = 0;
	// Per path: drawn per pixel rather than raw, for the hysteresis.
	QHash<QString, bool> aggregated_;
	QStringList timeLabels_, valueLabels_;
	int drawnPoints_ = 0, rawCurves_ = 0, drawnCurves_ = 0;
	QRectF plotArea_;
	qreal rightUtcMs_ = 0;
	bool fromStart_ = false;
	// What a view by hand was, to go back to. The focus only from Fit all,
	// which unfocuses.
	struct ViewState {
		bool following = true, showAll = false, valueAuto = true;
		qint64 heldAllNs = 0, frozenNs = 0;
		qreal spanSeconds = 60;
		double manualLow = 0, manualHigh = 0;
		QHash<QString, Range> laneManual;
		std::optional<QString> focus;
	};
	std::vector<ViewState> back_;
	qint64 rememberedAtMs_ = 0;
	ViewState viewState() const;
	void applyState(const ViewState& state);
	// The view at the press, for Esc.
	ViewState pressState_;
	// Before a view changes by hand: what it was, once a gesture (alone: a
	// step of its own, never merged with a run). True when a step was taken.
	bool remember(bool alone = false, bool withFocus = false);
	bool boxing_ = false;
	bool endShown_ = false;
	qint64 endNs_ = 0;
	qreal endX_ = -1;
	// The range measured, in the readings' time.
	std::pair<qint64, qint64> measuredNs() const;
	QPointF boxFrom_, boxTo_;
	qreal originUtcMs_ = 0;
	QColor chipColor_{0x16, 0x1b, 0x22};
	qreal chipHeight_ = 0;
	QVariantList timeLabelRects_;
	QRectF boxCaptionRect_;
	// Stacked: a number's lane as the last paint drew it.
	struct NumberLane {
		QString path;
		// The whole lane, the part its values span (clear of a label's half
		// height at either end, so every label is whole), its name.
		QRectF rect, valueRect, nameRect;
		Range shown;
		QVector<double> ticks;
		QStringList labels;
		qreal cursorY = -1;
	};
	std::vector<NumberLane> numberLanes_;
	const NumberLane* numberLane(const QString& path) const;
	QVariantList numberLanesShown_;
	// Per number: its fitted values, since when narrower, and its values by
	// hand (a lane in Manual).
	QHash<QString, Range> laneRange_, laneManual_;
	QHash<QString, qint64> laneNarrowSince_;
	bool stackFits_ = true, stackedSeen_ = false;
	// The lane drawn at item y, or -1.
	int laneAt(qreal y) const;
	// The lanes a value zoom or pan by the keys acts on.
	QStringList valueLanes() const;
	// The lane a press moves the values of; the pointer's item y.
	QString pressLane_;
	qreal pointerY_ = -1;
};
} // namespace telemetry::ux
#endif
