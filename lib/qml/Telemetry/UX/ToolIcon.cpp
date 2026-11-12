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
#include <algorithm>

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

QPainterPath shape(const QString& name)
{
	if (name == QLatin1String("edit"))
		return pencil();
	if (name == QLatin1String("save"))
		return disk();
	if (name == QLatin1String("upload"))
		return upload();
	if (name == QLatin1String("undo"))
		return hookLeft();
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
