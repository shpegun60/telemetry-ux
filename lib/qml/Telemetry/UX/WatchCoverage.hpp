/*
 * @file WatchCoverage.hpp
 * @brief Which values of an endpoint tree the Watchlist can hold, and how many of
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
 *
 * Boxes count values. A scalar Field is one value, held by its own key; a
 * struct or array Field holds its leaves, each held by WatchKey's member key,
 * so a Field's box is three-state over all of them whether or not its rows
 * are open, and a member row's box over its own range of leaf ordinals.
 * Member rows are never walked: opening a struct changes no slot.
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
#include <vector>

namespace telemetry::ux {
class WatchCoverage final {
public:
	// A Field the Watchlist can hold: its key in the Watchlist (the Field's id,
	// also for a struct or array), the name a row is added under, and how many
	// values it holds: 1 for a scalar, its leaves for an aggregate.
	struct Holdable {
		quint64 key = 0;
		QString name;
		quint32 leaves = 1;
		bool aggregate = false;
	};
	// nullopt for a group, a Command, a Service or a Field the Watchlist cannot
	// hold.
	using HoldableOf = std::function<std::optional<Holdable>(const QModelIndex&)>;
	// Whether the walk goes into a node's children; every node without one.
	using Descend = std::function<bool(const QModelIndex&)>;

	// At most this many nodes and this depth are walked; a tree past either is
	// covered as far as the walk went.
	static constexpr int maxSlots = 1 << 18;
	static constexpr int maxDepth = 64;

	// The tree changed: built again on the next question.
	void reset() noexcept;
	[[nodiscard]] bool built() const noexcept;
	void build(const QAbstractItemModel& model, const HoldableOf& holdableOf, const Descend& descend = {});
	// -1 when there is no box (nothing below can be held); otherwise
	// Qt::Unchecked, Qt::PartiallyChecked or Qt::Checked as an int. watched is
	// the set of keys the Watchlist holds; membership moves whenever it changes.
	[[nodiscard]] int state(const QModelIndex& index, const QSet<quint64>& watched,
	                        quint64 membership);
	// The box of the values [first, first + count) of a struct or array Field:
	// -1 unless that Field is holdable and count > 0.
	[[nodiscard]] int memberState(quint32 field, quint32 first, quint32 count,
	                              const QSet<quint64>& watched, quint64 membership);
	// What a box counts: the values watched of those it can hold, and the
	// struct or array Fields among them.
	struct Tally {
		qint64 watched = 0, total = 0;
		int aggregates = 0;
	};
	[[nodiscard]] std::optional<Tally> tally(const QModelIndex& index, const QSet<quint64>& watched,
	                                         quint64 membership);
	// How many values of [first, first + count) of a Field are watched.
	[[nodiscard]] qint64 countIn(quint32 field, quint32 first, quint32 count, const QSet<quint64>& watched,
	                             quint64 membership);
	// The leaf ordinals watched in [first, first + count) of a Field, ascending.
	[[nodiscard]] std::vector<quint32> watchedIn(quint32 field, quint32 first, quint32 count,
	                                             const QSet<quint64>& watched, quint64 membership);
	// The struct or array Field with this id, when it is holdable.
	[[nodiscard]] std::optional<Holdable> aggregate(quint32 field) const;
	// Whether index is a node of the tree as walked.
	[[nodiscard]] bool contains(const QModelIndex& index) const;
	// The Fields it can hold at or under index, in tree order.
	[[nodiscard]] QVector<Holdable> holdableUnder(const QModelIndex& index) const;
	[[nodiscard]] int size() const noexcept;

private:
	struct Slot {
		int end = 0; // one past the last slot of this node's subtree
		qint64 holdable = 0, watched = 0;
		int aggregates = 0;
		std::optional<Holdable> field;
	};

	void count(const QSet<quint64>& watched, quint64 membership);

	QVector<Slot> slots_;
	QHash<QModelIndex, int> index_;
	QHash<quint32, int> aggregates_; // Field id -> its slot
	// Per struct or array Field, the ordinals watched, ascending.
	QHash<quint32, std::vector<quint32>> members_;
	bool built_ = false;
	bool counted_ = false;
	quint64 membership_ = 0;
};
} // namespace telemetry::ux
#endif
