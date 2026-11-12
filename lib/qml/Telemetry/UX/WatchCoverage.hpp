/*
 * @file WatchCoverage.hpp
 * @brief Which Fields of an endpoint tree the Watchlist can hold, and how many of
 *        them below each node it holds: the Device tree's watch boxes.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * One pre-order walk of the tree when it is first asked after a change of the
 * tree; then a box's state is one hash lookup, and a change of the Watchlist's
 * membership one pass over the slots, made when a box next asks. No signal is
 * sent from here, so a box that asks inside a binding never loops. A provider
 * owns one, resets it when its endpoint model changes, and passes the keys the
 * Watchlist holds with a counter that moves whenever they change.
 */
#ifndef TELEMETRY_UX_WATCH_COVERAGE_HPP
#define TELEMETRY_UX_WATCH_COVERAGE_HPP
#pragma once

#include <QtCore/QAbstractItemModel>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <functional>
#include <optional>

namespace telemetry::ux {
class WatchCoverage final {
public:
	// A Field the Watchlist can hold: its key in the Watchlist and the name a
	// row is added under.
	struct Holdable {
		quint64 key = 0;
		QString name;
	};
	// nullopt for a group, a Command, a Service or a Field the Watchlist cannot
	// hold.
	using HoldableOf = std::function<std::optional<Holdable>(const QModelIndex&)>;

	// At most this many nodes and this depth are walked; a tree past either is
	// covered as far as the walk went.
	static constexpr int maxSlots = 1 << 18;
	static constexpr int maxDepth = 64;

	// The tree changed: built again on the next question.
	void reset() noexcept;
	[[nodiscard]] bool built() const noexcept;
	void build(const QAbstractItemModel& model, const HoldableOf& holdableOf);
	// -1 when there is no box (nothing below can be held); otherwise
	// Qt::Unchecked, Qt::PartiallyChecked or Qt::Checked as an int. watched is
	// the set of keys the Watchlist holds; membership moves whenever it changes.
	[[nodiscard]] int state(const QModelIndex& index, const QSet<quint64>& watched,
	                        quint64 membership);
	// Whether index is a node of the tree as walked.
	[[nodiscard]] bool contains(const QModelIndex& index) const;
	// The Fields it can hold at or under index, in tree order.
	[[nodiscard]] QVector<Holdable> holdableUnder(const QModelIndex& index) const;
	[[nodiscard]] int size() const noexcept;

private:
	struct Slot {
		int end = 0; // one past the last slot of this node's subtree
		int holdable = 0, watched = 0;
		std::optional<Holdable> field;
	};

	void count(const QSet<quint64>& watched);

	QVector<Slot> slots_;
	QHash<QModelIndex, int> index_;
	bool built_ = false;
	bool counted_ = false;
	quint64 membership_ = 0;
};
} // namespace telemetry::ux
#endif
