/*
 * @file WatchCoverage.cpp
 * @brief The Device tree's watch boxes: one walk, then lookups.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchCoverage.hpp"

namespace telemetry::ux {
void WatchCoverage::reset() noexcept
{
	slots_.clear();
	index_.clear();
	built_ = false;
	counted_ = false;
}

bool WatchCoverage::built() const noexcept
{
	return built_;
}

int WatchCoverage::size() const noexcept
{
	return static_cast<int>(slots_.size());
}

void WatchCoverage::build(const QAbstractItemModel& model, const HoldableOf& holdableOf)
{
	reset();
	struct Frame {
		QModelIndex parent;
		int row = 0, rows = 0, slot = -1;
	};
	QVector<Frame> stack;
	stack.push_back({QModelIndex{}, 0, model.rowCount(), -1});
	while (!stack.isEmpty()) {
		auto& frame = stack.last();
		if (frame.row >= frame.rows || slots_.size() >= maxSlots) {
			if (frame.slot >= 0)
				slots_[frame.slot].end = static_cast<int>(slots_.size());
			stack.removeLast();
			continue;
		}
		const auto child = model.index(frame.row++, 0, frame.parent);
		if (!child.isValid())
			continue;
		const int slot = static_cast<int>(slots_.size());
		Slot entry;
		entry.end = slot + 1;
		entry.field = holdableOf ? holdableOf(child) : std::nullopt;
		slots_.push_back(std::move(entry));
		index_.insert(child, slot);
		const int rows = model.rowCount(child);
		if (rows > 0 && stack.size() < maxDepth)
			stack.push_back({child, 0, rows, slot});
	}
	// Holdable counts, children before parents: a node's children are the
	// slots from slot + 1, each one's subtree skipped by its end.
	for (int slot = static_cast<int>(slots_.size()) - 1; slot >= 0; --slot) {
		auto& node = slots_[slot];
		int total = node.field ? 1 : 0;
		for (int child = slot + 1; child < node.end; child = slots_[child].end)
			total += slots_[child].holdable;
		node.holdable = total;
	}
	built_ = true;
}

void WatchCoverage::count(const QSet<quint64>& watched)
{
	for (int slot = static_cast<int>(slots_.size()) - 1; slot >= 0; --slot) {
		auto& node = slots_[slot];
		int total = node.field && watched.contains(node.field->key) ? 1 : 0;
		for (int child = slot + 1; child < node.end; child = slots_[child].end)
			total += slots_[child].watched;
		node.watched = total;
	}
	counted_ = true;
}

int WatchCoverage::state(const QModelIndex& index, const QSet<quint64>& watched,
                         quint64 membership)
{
	if (!built_)
		return -1;
	const auto found = index_.constFind(index);
	if (found == index_.constEnd())
		return -1;
	if (!counted_ || membership != membership_) {
		count(watched);
		membership_ = membership;
	}
	const auto& node = slots_[*found];
	if (node.holdable == 0)
		return -1;
	if (node.watched == 0)
		return Qt::Unchecked;
	return node.watched == node.holdable ? Qt::Checked : Qt::PartiallyChecked;
}

QVector<WatchCoverage::Holdable> WatchCoverage::holdableUnder(const QModelIndex& index) const
{
	QVector<Holdable> found;
	const auto at = index_.constFind(index);
	if (!built_ || at == index_.constEnd())
		return found;
	const auto& node = slots_[*at];
	found.reserve(node.holdable);
	for (int slot = *at; slot < node.end; ++slot)
		if (slots_[slot].field)
			found.push_back(*slots_[slot].field);
	return found;
}
} // namespace telemetry::ux
