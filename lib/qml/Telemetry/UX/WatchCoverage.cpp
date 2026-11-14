/*
 * @file WatchCoverage.cpp
 * @brief The Device tree's watch boxes: one walk, then lookups.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchCoverage.hpp"
#include "WatchKey.hpp"
#include <algorithm>

namespace telemetry::ux {
namespace {
int boxOf(qint64 watched, qint64 holdable)
{
	if (holdable <= 0)
		return -1;
	if (watched == 0)
		return Qt::Unchecked;
	return watched >= holdable ? Qt::Checked : Qt::PartiallyChecked;
}
} // namespace

void WatchCoverage::reset() noexcept
{
	slots_.clear();
	index_.clear();
	aggregates_.clear();
	members_.clear();
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

void WatchCoverage::build(const QAbstractItemModel& model, const HoldableOf& holdableOf, const Descend& descend)
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
		if (entry.field && entry.field->aggregate)
			aggregates_.insert(watch_key::field(entry.field->key), slot);
		slots_.push_back(std::move(entry));
		index_.insert(child, slot);
		if (descend && !descend(child))
			continue;
		const int rows = model.rowCount(child);
		if (rows > 0 && stack.size() < maxDepth)
			stack.push_back({child, 0, rows, slot});
	}
	// Holdable counts, children before parents: a node's children are the
	// slots from slot + 1, each one's subtree skipped by its end.
	for (int slot = static_cast<int>(slots_.size()) - 1; slot >= 0; --slot) {
		auto& node = slots_[slot];
		qint64 total = node.field ? node.field->leaves : 0;
		int aggregates = node.field && node.field->aggregate ? 1 : 0;
		for (int child = slot + 1; child < node.end; child = slots_[child].end) {
			total += slots_[child].holdable;
			aggregates += slots_[child].aggregates;
		}
		node.holdable = total;
		node.aggregates = aggregates;
	}
	built_ = true;
}

void WatchCoverage::count(const QSet<quint64>& watched, quint64 membership)
{
	members_.clear();
	for (const auto key : watched)
		if (!watch_key::isWhole(key) && aggregates_.contains(watch_key::field(key)))
			members_[watch_key::field(key)].push_back(watch_key::ordinal(key));
	for (auto& ordinals : members_)
		std::sort(ordinals.begin(), ordinals.end());
	for (int slot = static_cast<int>(slots_.size()) - 1; slot >= 0; --slot) {
		auto& node = slots_[slot];
		qint64 total = 0;
		if (node.field)
			total = node.field->aggregate
			            ? qint64(members_.value(watch_key::field(node.field->key)).size())
			            : watched.contains(node.field->key) ? 1 : 0;
		for (int child = slot + 1; child < node.end; child = slots_[child].end)
			total += slots_[child].watched;
		node.watched = total;
	}
	counted_ = true;
	membership_ = membership;
}

int WatchCoverage::state(const QModelIndex& index, const QSet<quint64>& watched,
                         quint64 membership)
{
	const auto counted = tally(index, watched, membership);
	return counted ? boxOf(counted->watched, counted->total) : -1;
}

std::optional<WatchCoverage::Tally> WatchCoverage::tally(const QModelIndex& index, const QSet<quint64>& watched,
                                                         quint64 membership)
{
	if (!built_)
		return std::nullopt;
	const auto found = index_.constFind(index);
	if (found == index_.constEnd())
		return std::nullopt;
	if (!counted_ || membership != membership_)
		count(watched, membership);
	const auto& node = slots_[*found];
	if (node.holdable == 0)
		return std::nullopt;
	return Tally{node.watched, node.holdable, node.aggregates};
}

std::vector<quint32> WatchCoverage::watchedIn(quint32 field, quint32 first, quint32 count,
                                              const QSet<quint64>& watched, quint64 membership)
{
	if (!built_ || !aggregates_.contains(field) || count == 0)
		return {};
	if (!counted_ || membership != membership_)
		this->count(watched, membership);
	const auto found = members_.constFind(field);
	if (found == members_.cend())
		return {};
	const auto& ordinals = *found;
	const auto last = quint64{first} + count;
	const auto from = std::lower_bound(ordinals.begin(), ordinals.end(), first);
	const auto to = std::lower_bound(from, ordinals.end(), last,
	                                 [](quint32 ordinal, quint64 end) { return ordinal < end; });
	return {from, to};
}

qint64 WatchCoverage::countIn(quint32 field, quint32 first, quint32 count, const QSet<quint64>& watched,
                              quint64 membership)
{
	if (!built_ || !aggregates_.contains(field) || count == 0)
		return 0;
	if (!counted_ || membership != membership_)
		this->count(watched, membership);
	const auto found = members_.constFind(field);
	if (found == members_.cend())
		return 0;
	const auto& ordinals = *found;
	const auto from = std::lower_bound(ordinals.begin(), ordinals.end(), first);
	const auto to = std::lower_bound(from, ordinals.end(), quint64{first} + count,
	                                 [](quint32 ordinal, quint64 end) { return ordinal < end; });
	return qint64(to - from);
}

int WatchCoverage::memberState(quint32 field, quint32 first, quint32 count,
                               const QSet<quint64>& watched, quint64 membership)
{
	if (!built_ || count == 0 || !aggregates_.contains(field))
		return -1;
	return boxOf(countIn(field, first, count, watched, membership), count);
}

std::optional<WatchCoverage::Holdable> WatchCoverage::aggregate(quint32 field) const
{
	const auto found = aggregates_.constFind(field);
	if (!built_ || found == aggregates_.cend())
		return std::nullopt;
	return slots_[*found].field;
}

bool WatchCoverage::contains(const QModelIndex& index) const
{
	return built_ && index_.contains(index);
}

QVector<WatchCoverage::Holdable> WatchCoverage::holdableUnder(const QModelIndex& index) const
{
	QVector<Holdable> found;
	const auto at = index_.constFind(index);
	if (!built_ || at == index_.constEnd())
		return found;
	const auto& node = slots_[*at];
	for (int slot = *at; slot < node.end; ++slot)
		if (slots_[slot].field)
			found.push_back(*slots_[slot].field);
	return found;
}
} // namespace telemetry::ux
