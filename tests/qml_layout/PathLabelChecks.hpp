// PL-1 (the owner's screenshots of 21 November 2026): one rule for every
// path label - the Batch's rows, the Watchlist's List, Details' title and
// the Device tree's context line. A path is whole whenever it fits its
// room; otherwise only whole segments drop under "…", the leaf whole until
// nothing more can drop, and only then cut in its middle. The room is
// measured here with the label's own fonts by QFontMetricsF, not by the
// label's own sums, so a label that misjudges its room fails; a label is
// allowed the 1 px its measure tolerates. Each host calls it at several
// widths, after a resize both ways, with the pointer on the row and off.
// MIT.
#ifndef TELEMETRY_UX_TESTS_PATH_LABEL_CHECKS_HPP
#define TELEMETRY_UX_TESTS_PATH_LABEL_CHECKS_HPP
#pragma once

#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtQml/QJSValue>
#include <QtQuick/QQuickItem>

namespace path_label_checks {

inline QVariant plain(const QVariant& value)
{
	return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
inline QQuickItem* named(QQuickItem* root, const QString& name)
{
	if (!root)
		return nullptr;
	if (root->objectName() == name)
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name))
			return found;
	return nullptr;
}
// A shape's width as the fonts give it: its prefix, then its leaf.
inline qreal natural(QQuickItem* label, const QVariantMap& shape)
{
	const QFontMetricsF prefix(label->property("font").value<QFont>());
	const QFontMetricsF leaf(label->property("leafFont").value<QFont>());
	return prefix.horizontalAdvance(shape.value(QStringLiteral("prefix")).toString()) +
	       leaf.horizontalAdvance(shape.value(QStringLiteral("leaf")).toString());
}
inline QVariantMap shapeOf(QQuickItem* label, int drops)
{
	QVariant result;
	QMetaObject::invokeMethod(label, "shape", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, drops));
	return plain(result).toMap();
}

// What is wrong with the label as it stands, or an empty string.
inline QString fault(QQuickItem* label)
{
	if (!label)
		return QStringLiteral("no path label");
	const auto path = label->property("path").toString();
	if (path.isEmpty() || !label->isVisible())
		return {};
	const qreal room = label->width();
	const auto say = [&](const QString& what) {
		return QStringLiteral("%1 at %2 px: %3").arg(path).arg(room, 0, 'f', 2).arg(what);
	};
	const auto steps = plain(label->property("steps")).toList();
	const auto order = plain(label->property("dropOrder")).toList();
	const int drops = label->property("drops").toInt();
	const auto shown = plain(label->property("shown")).toMap();
	const auto full = shapeOf(label, 0);
	auto* leafText = named(label, label->property("leafObjectName").toString());
	if (steps.isEmpty() || !leafText)
		return say(QStringLiteral("no steps or no leaf"));
	const auto leaf = steps.last().toMap().value(QStringLiteral("text")).toString();
	// The leaf's text is always the whole leaf; only its drawing may cut it.
	if (leafText->property("text").toString() != leaf)
		return say(QStringLiteral("the leaf reads \"%1\"").arg(leafText->property("text").toString()));
	const bool cut = leafText->property("truncated").toBool();
	if (!label->property("interactive").toBool()) {
		auto* prefixText = named(label, label->property("prefixObjectName").toString());
		const auto prefix = prefixText && prefixText->isVisible() ? prefixText->property("text").toString() : QString{};
		if (prefix != shown.value(QStringLiteral("prefix")).toString())
			return say(QStringLiteral("the prefix reads \"%1\"").arg(prefix));
	}
	// Whole whenever it fits.
	if (natural(label, full) <= room + 1) {
		if (drops != 0 || cut || label->property("elided").toBool())
			return say(QStringLiteral("fits (%1 px) but shows \"%2%3\"%4")
			               .arg(natural(label, full), 0, 'f', 2)
			               .arg(shown.value(QStringLiteral("prefix")).toString(), leaf, cut ? QStringLiteral(", its leaf cut") : QString{}));
		return {};
	}
	// Otherwise whole segments drop, the fewest that fit.
	if (!order.isEmpty() && drops == 0)
		return say(QStringLiteral("does not fit and drops nothing"));
	const auto parts = shown.value(QStringLiteral("parts")).toList();
	int at = 0;
	for (const auto& entry : parts) {
		const auto part = entry.toMap();
		if (part.value(QStringLiteral("kind")) == QStringLiteral("cut")) {
			if (part.value(QStringLiteral("text")) != QStringLiteral("…"))
				return say(QStringLiteral("a cut reads \"%1\"").arg(part.value(QStringLiteral("text")).toString()));
			continue;
		}
		while (at < steps.size() - 1 && steps[at].toMap().value(QStringLiteral("text")) != part.value(QStringLiteral("text")))
			++at;
		if (at >= steps.size() - 1)
			return say(QStringLiteral("shows a piece of a segment, \"%1\"").arg(part.value(QStringLiteral("text")).toString()));
		++at;
	}
	if (drops > 0 && natural(label, shapeOf(label, drops - 1)) <= room + 0.25)
		return say(QStringLiteral("drops %1 segments where %2 fit").arg(drops).arg(drops - 1));
	// The leaf is cut only when nothing more can drop and still it does not fit.
	if (cut && (drops < order.size() || natural(label, shown) <= room + 0.25))
		return say(QStringLiteral("cuts its leaf with %1 of %2 segments dropped").arg(drops).arg(order.size()));
	if (label->property("usedWidth").toReal() > room + 1)
		return say(QStringLiteral("runs %1 px past its room").arg(label->property("usedWidth").toReal() - room, 0, 'f', 2));
	return {};
}

} // namespace path_label_checks
#endif
