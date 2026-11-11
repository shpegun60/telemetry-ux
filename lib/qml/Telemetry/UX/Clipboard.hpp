/*
 * @file Clipboard.hpp
 * @brief The system clipboard for QML: Copy path, Copy value and the like.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * QML has no clipboard of its own outside a text field. The panels' Copy
 * actions call Clipboard.setText(...); a host calls registerQmlTypes() first.
 */
#ifndef TELEMETRY_UX_CLIPBOARD_HPP
#define TELEMETRY_UX_CLIPBOARD_HPP
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

namespace telemetry::ux {
class Clipboard : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
public:
	using QObject::QObject;
	Q_INVOKABLE void setText(const QString& text);
	Q_INVOKABLE QString text() const;
};
} // namespace telemetry::ux
#endif
