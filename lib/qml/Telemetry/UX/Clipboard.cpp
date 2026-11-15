/*
 * @file Clipboard.cpp
 * @brief The system clipboard for QML.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "Clipboard.hpp"
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

namespace telemetry::ux {
void Clipboard::setText(const QString& text)
{
	if (auto* clipboard = QGuiApplication::clipboard())
		clipboard->setText(text);
}

bool Clipboard::setImage(const QVariant& image)
{
	auto* clipboard = QGuiApplication::clipboard();
	const auto picture = image.value<QImage>();
	if (!clipboard || picture.isNull())
		return false;
	clipboard->setImage(picture);
	return true;
}

bool Clipboard::hasImage() const
{
	const auto* clipboard = QGuiApplication::clipboard();
	return clipboard && !clipboard->image().isNull();
}

QString Clipboard::text() const
{
	const auto* clipboard = QGuiApplication::clipboard();
	return clipboard ? clipboard->text() : QString{};
}
} // namespace telemetry::ux
