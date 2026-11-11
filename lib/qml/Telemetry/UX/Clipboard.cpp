/*
 * @file Clipboard.cpp
 * @brief The system clipboard for QML.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "Clipboard.hpp"
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace telemetry::ux {
void Clipboard::setText(const QString& text)
{
	if (auto* clipboard = QGuiApplication::clipboard())
		clipboard->setText(text);
}

QString Clipboard::text() const
{
	const auto* clipboard = QGuiApplication::clipboard();
	return clipboard ? clipboard->text() : QString{};
}
} // namespace telemetry::ux
