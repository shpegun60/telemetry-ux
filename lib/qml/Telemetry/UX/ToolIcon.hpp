/*
 * @file ToolIcon.hpp
 * @brief A toolbar pictogram drawn with QPainter: edit, save, undo, redo.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * Strokes on a 24-unit grid scaled to the item, in one colour the caller
 * gives (the Theme's text colour, its disabled colour on a disabled action).
 * Painted rather than loaded, so a host needs no SVG image plugin, no Qt
 * Quick Shapes and no files beside the executable, on every Qt 6 the module
 * supports. An unknown name draws nothing.
 */
#ifndef TELEMETRY_UX_TOOL_ICON_HPP
#define TELEMETRY_UX_TOOL_ICON_HPP
#pragma once

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

namespace telemetry::ux {

// Not final: QML derives its own element from a creatable type.
class ToolIcon : public QQuickPaintedItem {
	Q_OBJECT
	QML_ELEMENT
	// "edit", "save", "download", "upload", "refresh", "undo", "redo",
	// "warning" or "lock".
	Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
	Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
	explicit ToolIcon(QQuickItem* parent = nullptr);

	QString name() const { return name_; }
	void setName(const QString& name);
	QColor color() const { return color_; }
	void setColor(const QColor& color);

	void paint(QPainter* painter) override;

Q_SIGNALS:
	void changed();

private:
	QString name_;
	QColor color_{Qt::white};
};

} // namespace telemetry::ux
#endif
