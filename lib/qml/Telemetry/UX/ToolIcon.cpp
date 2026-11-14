/*
 * @file ToolIcon.cpp
 * @brief The pictograms, as strokes on a 24-unit grid.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "ToolIcon.hpp"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QTransform>
#include <algorithm>
#include <array>
#include <utility>

namespace telemetry::ux {
namespace {

// A pencil, its point at the lower left, with the band behind the point.
QPainterPath pencil()
{
	QPainterPath path;
	path.moveTo(15.5, 4.5);
	path.lineTo(19.5, 8.5);
	path.lineTo(8.5, 19.5);
	path.lineTo(4.5, 19.5);
	path.lineTo(4.5, 15.5);
	path.closeSubpath();
	path.moveTo(13, 7);
	path.lineTo(17, 11);
	return path;
}

// A floppy disk: the body with its cut corner, the shutter, the label.
QPainterPath disk()
{
	QPainterPath path;
	path.moveTo(4.5, 4.5);
	path.lineTo(16, 4.5);
	path.lineTo(19.5, 8);
	path.lineTo(19.5, 19.5);
	path.lineTo(4.5, 19.5);
	path.closeSubpath();
	path.moveTo(8, 4.5);
	path.lineTo(8, 9);
	path.lineTo(15, 9);
	path.lineTo(15, 4.5);
	path.moveTo(7.5, 19.5);
	path.lineTo(7.5, 13.5);
	path.lineTo(16.5, 13.5);
	path.lineTo(16.5, 19.5);
	return path;
}

// Undo: an arrow pointing left that turns back underneath, as on a hook.
QPainterPath hookLeft()
{
	QPainterPath path;
	path.moveTo(9, 13.5);
	path.lineTo(4.5, 9);
	path.lineTo(9, 4.5);
	path.moveTo(4.5, 9);
	path.lineTo(14.5, 9);
	// A half turn of radius 5 about (14.5, 14), from the top round the right.
	path.arcTo(9.5, 9, 10, 10, 90, -180);
	path.lineTo(10, 19);
	return path;
}

// Write to the device: an arrow up out of an open tray.
QPainterPath upload()
{
	QPainterPath path;
	path.moveTo(4.5, 14);
	path.lineTo(4.5, 19.5);
	path.lineTo(19.5, 19.5);
	path.lineTo(19.5, 14);
	path.moveTo(12, 15.5);
	path.lineTo(12, 4.5);
	path.moveTo(7.5, 9);
	path.lineTo(12, 4.5);
	path.lineTo(16.5, 9);
	return path;
}

// Download: an arrow down into an open tray, upload's counterpart.
QPainterPath download()
{
	QPainterPath path;
	path.moveTo(4.5, 14);
	path.lineTo(4.5, 19.5);
	path.lineTo(19.5, 19.5);
	path.lineTo(19.5, 14);
	path.moveTo(12, 4.5);
	path.lineTo(12, 15.5);
	path.moveTo(7.5, 11);
	path.lineTo(12, 15.5);
	path.lineTo(16.5, 11);
	return path;
}
// Refresh: three quarters of a circle with an arrowhead at its open end.
QPainterPath refresh()
{
	QPainterPath path;
	// Radius 7 about (12, 12), from the upper right round to the top.
	path.arcMoveTo(5, 5, 14, 14, 45);
	path.arcTo(5, 5, 14, 14, 45, 270);
	path.moveTo(17, 3.5);
	path.lineTo(17, 7.5);
	path.lineTo(13, 7.5);
	return path;
}
// Warning: a triangle on its base with an exclamation mark, the dot a stroke
// so short its round caps make it.
QPainterPath warning()
{
	QPainterPath path;
	path.moveTo(12, 3.5);
	path.lineTo(21, 19.5);
	path.lineTo(3, 19.5);
	path.closeSubpath();
	path.moveTo(12, 9.5);
	path.lineTo(12, 13.5);
	path.moveTo(12, 16.5);
	path.lineTo(12, 16.6);
	return path;
}
// Lock: a padlock - its body, the shackle a half turn above it, the keyhole.
QPainterPath lock()
{
	QPainterPath path;
	path.moveTo(6, 11);
	path.lineTo(18, 11);
	path.lineTo(18, 19.5);
	path.lineTo(6, 19.5);
	path.closeSubpath();
	path.moveTo(8.5, 11);
	path.lineTo(8.5, 8);
	// A half turn of radius 3.5 about (12, 8), from the left over the top.
	path.arcTo(8.5, 4.5, 7, 7, 180, -180);
	path.lineTo(15.5, 11);
	path.moveTo(12, 14.5);
	path.lineTo(12, 16);
	return path;
}
// The Device files icons (the designer's set, 13 November 2026): a known
// format is its bare symbol, a page means a file of no known format.

// A folder: a tab on its top left.
QPainterPath folder()
{
	QPainterPath path;
	path.moveTo(3.5, 6.5);
	path.lineTo(9.5, 6.5);
	path.lineTo(11.5, 8.5);
	path.lineTo(20.5, 8.5);
	path.lineTo(20.5, 18.5);
	path.lineTo(3.5, 18.5);
	path.closeSubpath();
	return path;
}
// A page with its corner turned: a file of no known format.
QPainterPath page()
{
	QPainterPath path;
	path.moveTo(6, 3.5);
	path.lineTo(13.5, 3.5);
	path.lineTo(18.5, 8.5);
	path.lineTo(18.5, 20.5);
	path.lineTo(6, 20.5);
	path.closeSubpath();
	path.moveTo(13.5, 3.5);
	path.lineTo(13.5, 8.5);
	path.lineTo(18.5, 8.5);
	return path;
}
// A page with a rhombus on it: a file a host's codec reads.
QPainterPath pageWithCodec()
{
	QPainterPath path = page();
	path.moveTo(12.25, 11.5);
	path.lineTo(15.25, 14.5);
	path.lineTo(12.25, 17.5);
	path.lineTo(9.25, 14.5);
	path.closeSubpath();
	return path;
}
// Text: three lines, each shorter.
QPainterPath lines()
{
	QPainterPath path;
	path.moveTo(4.5, 6.5);
	path.lineTo(19.5, 6.5);
	path.moveTo(4.5, 12);
	path.lineTo(17, 12);
	path.moveTo(4.5, 17.5);
	path.lineTo(13, 17.5);
	return path;
}
// A table: a frame, its header row, a row line and a first column.
QPainterPath table()
{
	QPainterPath path;
	path.moveTo(4.5, 5);
	path.lineTo(19.5, 5);
	path.lineTo(19.5, 19);
	path.lineTo(4.5, 19);
	path.closeSubpath();
	path.moveTo(4.5, 9.5);
	path.lineTo(19.5, 9.5);
	path.moveTo(4.5, 14.25);
	path.lineTo(19.5, 14.25);
	path.moveTo(10.5, 9.5);
	path.lineTo(10.5, 19);
	return path;
}
// JSON: a pair of braces, the right one the left mirrored.
QPainterPath braces()
{
	QPainterPath left;
	left.moveTo(9.5, 4.5);
	left.arcTo(7.5, 4.5, 4, 4, 90, 90);
	left.lineTo(7.5, 10);
	left.arcTo(3.5, 8, 4, 4, 0, -90);
	left.arcTo(3.5, 12, 4, 4, 90, -90);
	left.lineTo(7.5, 17.5);
	left.arcTo(7.5, 15.5, 4, 4, 180, 90);
	QPainterPath path = left;
	path.addPath(QTransform(-1, 0, 0, 1, 24, 0).map(left));
	return path;
}
// The descriptor: a hierarchy - a root, its connector, two children.
QPainterPath hierarchy()
{
	QPainterPath path;
	path.moveTo(9, 3.5);
	path.lineTo(15, 3.5);
	path.lineTo(15, 8.5);
	path.lineTo(9, 8.5);
	path.closeSubpath();
	path.moveTo(12, 8.5);
	path.lineTo(12, 12);
	path.moveTo(6.5, 15);
	path.lineTo(6.5, 12);
	path.lineTo(17.5, 12);
	path.lineTo(17.5, 15);
	path.moveTo(3.5, 15);
	path.lineTo(9.5, 15);
	path.lineTo(9.5, 20.5);
	path.lineTo(3.5, 20.5);
	path.closeSubpath();
	path.moveTo(14.5, 15);
	path.lineTo(20.5, 15);
	path.lineTo(20.5, 20.5);
	path.lineTo(14.5, 20.5);
	path.closeSubpath();
	return path;
}
// Values: a gauge - a half turn of radius 8 about (12, 16), its needle, its hub.
QPainterPath gauge()
{
	QPainterPath path;
	path.arcMoveTo(4, 8, 16, 16, 180);
	path.arcTo(4, 8, 16, 16, 180, -180);
	path.moveTo(12, 16);
	path.lineTo(15.5, 11.5);
	path.arcMoveTo(10.5, 14.5, 3, 3, 0);
	path.arcTo(10.5, 14.5, 3, 3, 0, 360);
	return path;
}
// Identity: a name tag pointing right, with its hole.
QPainterPath nameTag()
{
	QPainterPath path;
	path.moveTo(3.5, 7);
	path.lineTo(14, 7);
	path.lineTo(20.5, 12);
	path.lineTo(14, 17);
	path.lineTo(3.5, 17);
	path.closeSubpath();
	path.arcMoveTo(6.25, 10.75, 2.5, 2.5, 0);
	path.arcTo(6.25, 10.75, 2.5, 2.5, 0, 360);
	return path;
}
// Session: two arrows going opposite ways.
QPainterPath exchange()
{
	QPainterPath path;
	path.moveTo(4.5, 8.5);
	path.lineTo(18.5, 8.5);
	path.moveTo(15, 5);
	path.lineTo(18.5, 8.5);
	path.lineTo(15, 12);
	path.moveTo(19.5, 15.5);
	path.lineTo(5.5, 15.5);
	path.moveTo(9, 12);
	path.lineTo(5.5, 15.5);
	path.lineTo(9, 19);
	return path;
}
// Capability: a switch, on - its rounded track and the knob at its right end.
QPainterPath toggle()
{
	QPainterPath path;
	path.moveTo(8, 7.5);
	path.lineTo(16, 7.5);
	path.arcTo(11.5, 7.5, 9, 9, 90, -180);
	path.lineTo(8, 16.5);
	path.arcTo(3.5, 7.5, 9, 9, 270, -180);
	path.closeSubpath();
	path.arcMoveTo(14.5, 10.5, 3, 3, 0);
	path.arcTo(14.5, 10.5, 3, 3, 0, 360);
	return path;
}
// The bottom panel's glyphs (the designer's, 13 November 2026).
// Error: a circle with a cross in it.
QPainterPath errorMark()
{
	QPainterPath path;
	path.arcMoveTo(3.5, 3.5, 17, 17, 0);
	path.arcTo(3.5, 3.5, 17, 17, 0, 360);
	path.moveTo(9, 9);
	path.lineTo(15, 15);
	path.moveTo(15, 9);
	path.lineTo(9, 15);
	return path;
}
// Info: a circle with an "i", its dot a stroke so short its caps make it.
QPainterPath infoMark()
{
	QPainterPath path;
	path.arcMoveTo(3.5, 3.5, 17, 17, 0);
	path.arcTo(3.5, 3.5, 17, 17, 0, 360);
	path.moveTo(12, 11);
	path.lineTo(12, 16.5);
	path.moveTo(12, 7.75);
	path.lineTo(12, 7.85);
	return path;
}
// Copy: a sheet in front of another.
QPainterPath sheets()
{
	QPainterPath path;
	path.moveTo(8.5, 8.5);
	path.lineTo(19.5, 8.5);
	path.lineTo(19.5, 19.5);
	path.lineTo(8.5, 19.5);
	path.closeSubpath();
	path.moveTo(15.5, 8.5);
	path.lineTo(15.5, 4.5);
	path.lineTo(4.5, 4.5);
	path.lineTo(4.5, 15.5);
	path.lineTo(8.5, 15.5);
	return path;
}
// Clear: lines of text with a cross after the last.
QPainterPath clearLines()
{
	QPainterPath path;
	path.moveTo(4.5, 7);
	path.lineTo(19.5, 7);
	path.moveTo(4.5, 12);
	path.lineTo(13, 12);
	path.moveTo(4.5, 17);
	path.lineTo(11, 17);
	path.moveTo(15, 13.5);
	path.lineTo(19.5, 18);
	path.moveTo(19.5, 13.5);
	path.lineTo(15, 18);
	return path;
}
// Zoom: a lens with its handle, a plus or a minus inside.
QPainterPath lens(bool plus)
{
	QPainterPath path;
	path.addEllipse(QPointF(10.5, 10.5), 6, 6);
	path.moveTo(15, 15);
	path.lineTo(19.5, 19.5);
	path.moveTo(7.5, 10.5);
	path.lineTo(13.5, 10.5);
	if (plus) {
		path.moveTo(10.5, 7.5);
		path.lineTo(10.5, 13.5);
	}
	return path;
}
// Fit: four corners of a frame, the whole range in view.
QPainterPath corners()
{
	QPainterPath path;
	path.moveTo(4.5, 9);
	path.lineTo(4.5, 4.5);
	path.lineTo(9, 4.5);
	path.moveTo(15, 4.5);
	path.lineTo(19.5, 4.5);
	path.lineTo(19.5, 9);
	path.moveTo(19.5, 15);
	path.lineTo(19.5, 19.5);
	path.lineTo(15, 19.5);
	path.moveTo(9, 19.5);
	path.lineTo(4.5, 19.5);
	path.lineTo(4.5, 15);
	return path;
}
QPainterPath shape(const QString& name)
{
	if (name == QLatin1String("zoomIn"))
		return lens(true);
	if (name == QLatin1String("zoomOut"))
		return lens(false);
	if (name == QLatin1String("fit"))
		return corners();
	if (name == QLatin1String("error"))
		return errorMark();
	if (name == QLatin1String("info"))
		return infoMark();
	if (name == QLatin1String("copy"))
		return sheets();
	if (name == QLatin1String("clear"))
		return clearLines();
	static const std::array<std::pair<QLatin1String, QPainterPath (*)()>, 11> files{{
	    {QLatin1String("folder"), folder},
	    {QLatin1String("file"), page},
	    {QLatin1String("fileCodec"), pageWithCodec},
	    {QLatin1String("fileText"), lines},
	    {QLatin1String("fileTable"), table},
	    {QLatin1String("fileJson"), braces},
	    {QLatin1String("fileDescriptor"), hierarchy},
	    {QLatin1String("fileValues"), gauge},
	    {QLatin1String("fileIdentity"), nameTag},
	    {QLatin1String("fileSession"), exchange},
	    {QLatin1String("fileCapability"), toggle},
	}};
	for (const auto& [known, draw] : files)
		if (name == known)
			return draw();
	if (name == QLatin1String("edit"))
		return pencil();
	if (name == QLatin1String("download"))
		return download();
	if (name == QLatin1String("refresh"))
		return refresh();
	if (name == QLatin1String("save"))
		return disk();
	if (name == QLatin1String("upload"))
		return upload();
	if (name == QLatin1String("undo"))
		return hookLeft();
	if (name == QLatin1String("warning"))
		return warning();
	if (name == QLatin1String("lock"))
		return lock();
	if (name == QLatin1String("redo"))
		return QTransform(-1, 0, 0, 1, 24, 0).map(hookLeft());
	return {};
}

} // namespace

ToolIcon::ToolIcon(QQuickItem* parent) : QQuickPaintedItem(parent)
{
	setAntialiasing(true);
	setImplicitSize(20, 20);
	connect(this, &ToolIcon::changed, this, [this] { update(); });
}

void ToolIcon::setName(const QString& name)
{
	if (name_ == name)
		return;
	name_ = name;
	Q_EMIT changed();
}

void ToolIcon::setColor(const QColor& color)
{
	if (color_ == color)
		return;
	color_ = color;
	Q_EMIT changed();
}

void ToolIcon::paint(QPainter* painter)
{
	const QPainterPath path = shape(name_);
	if (path.isEmpty())
		return;
	const qreal side = std::min(width(), height());
	if (side <= 0)
		return;
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->translate((width() - side) / 2, (height() - side) / 2);
	painter->scale(side / 24, side / 24);
	// About 1.6 px at 20 px: the weight of the text beside it.
	painter->setPen(QPen(color_, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(path);
}

} // namespace telemetry::ux
