/*
 * @file TreeFilter.cpp
 * @brief Find in a tree: what matches by name or path, its parents, and all below it.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "TreeFilter.hpp"

#include <QtCore/QStringList>

namespace telemetry::ux {
namespace {
void configure(QSortFilterProxyModel& proxy)
{
	proxy.setRecursiveFilteringEnabled(true);
	proxy.setAutoAcceptChildRows(true);
	// Names never change while shown: a value changing below is no reason to filter again.
	proxy.setDynamicSortFilter(false);
}

// A text split at every "/" and "." a backslash does not escape, each part
// unescaped and trimmed: "metrology / harm.H3" -> metrology, harm, H3.
// Empty parts dropped.
QStringList segmentsOf(const QString& text)
{
	QStringList out;
	QString part;
	const auto flush = [&] {
		const auto trimmed = part.trimmed();
		if (!trimmed.isEmpty())
			out.append(trimmed);
		part.clear();
	};
	for (qsizetype i = 0; i < text.size(); ++i) {
		const auto c = text.at(i);
		if (c == QLatin1Char('\\') && i + 1 < text.size())
			part.append(text.at(++i));
		else if (c == QLatin1Char('/') || c == QLatin1Char('.'))
			flush();
		else
			part.append(c);
	}
	flush();
	return out;
}
} // namespace

TreeFilter::TreeFilter(QObject* parent) : QSortFilterProxyModel(parent), byName_(true)
{
	configure(*this);
}

TreeFilter::TreeFilter(int nameRole, int pathRole, QObject* parent)
    : QSortFilterProxyModel(parent), nameRole_(nameRole), pathRole_(pathRole)
{
	configure(*this);
}

void TreeFilter::findRoles()
{
	if (!byName_)
		return;
	nameRole_ = pathRole_ = childRole_ = kindRole_ = categoryRole_ = endpointRole_ = typeNameRole_ = -1;
	const auto* source = sourceModel();
	if (!source)
		return;
	const auto roles = source->roleNames();
	for (auto at = roles.cbegin(); at != roles.cend(); ++at) {
		if (at.value() == "name")
			nameRole_ = at.key();
		else if (at.value() == "path")
			pathRole_ = at.key();
		else if (at.value() == "previewChild")
			childRole_ = at.key();
		else if (at.value() == "kind")
			kindRole_ = at.key();
		else if (at.value() == "category")
			categoryRole_ = at.key();
		else if (at.value() == "isEndpoint")
			endpointRole_ = at.key();
		else if (at.value() == "typeName")
			typeNameRole_ = at.key();
	}
}

void TreeFilter::setSourceModel(QAbstractItemModel* model)
{
	for (const auto& connection : std::as_const(sourceConnections_))
		disconnect(connection);
	sourceConnections_.clear();
	QSortFilterProxyModel::setSourceModel(model);
	findRoles();
	if (model) {
		// After the proxy's own handlers: its rows are the source's by then.
		sourceConnections_ << connect(model, &QAbstractItemModel::modelReset, this, [this] {
			findRoles();
			recount();
		});
		// A match added under a row this find hides: the proxy shows a new
		// row's parents only when they are filtered again.
		sourceConnections_ << connect(model, &QAbstractItemModel::rowsInserted, this,
		                              [this](const QModelIndex& parent, int first, int last) {
			if (active() && parent.isValid() && !mapFromSource(parent).isValid()) {
				for (int row = first; row <= last; ++row) {
					const auto added = sourceModel()->index(row, 0, parent);
					if (hit(added) || tally(added, false) > 0) {
						beginFilterChange();
						endFilterChange(Direction::Rows);
						break;
					}
				}
			}
			recount();
		});
		sourceConnections_ << connect(model, &QAbstractItemModel::rowsRemoved, this, &TreeFilter::recount);
		sourceConnections_ << connect(model, &QAbstractItemModel::layoutChanged, this, &TreeFilter::recount);
	}
	recount();
}

QStringList TreeFilter::pathSegments(const QModelIndex& source) const
{
	if (pathRole_ >= 0) {
		const auto path = source.data(pathRole_).toString();
		if (!path.isEmpty())
			return segmentsOf(path);
	}
	QStringList names;
	for (auto at = source; at.isValid(); at = at.parent())
		names.prepend(at.data(nameRole_).toString());
	return names;
}

std::optional<TreeFilter::Run> TreeFilter::textHit(const QModelIndex& source) const
{
	if (text_.isEmpty() || !source.isValid() || nameRole_ < 0)
		return std::nullopt;
	const auto name = source.data(nameRole_).toString();
	if (!byName_) {
		// A device file's view: the name or the path holds the text.
		if (const auto at = name.indexOf(text_, 0, Qt::CaseInsensitive); at >= 0)
			return Run{false, at, text_.size()};
		if (pathRole_ >= 0)
			return source.data(pathRole_).toString().contains(text_, Qt::CaseInsensitive)
			           ? std::optional<Run>(Run{false, -1, 0}) : std::nullopt;
		QStringList names;
		for (auto at = source; at.isValid(); at = at.parent())
			names.prepend(at.data(nameRole_).toString());
		return names.join(QLatin1Char('.')).contains(text_, Qt::CaseInsensitive)
		           ? std::optional<Run>(Run{false, -1, 0}) : std::nullopt;
	}
	if (segments_.size() <= 1) {
		// One segment: the name, else a named type as a whole.
		const auto& needle = segments_.isEmpty() ? text_ : segments_.first();
		if (const auto at = name.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0)
			return Run{false, at, needle.size()};
		if (typeNameRole_ >= 0) {
			const auto type = source.data(typeNameRole_).toString();
			if (const auto at = type.isEmpty() ? -1 : type.indexOf(text_, 0, Qt::CaseInsensitive); at >= 0)
				return Run{true, at, text_.size()};
		}
		return std::nullopt;
	}
	// Several: the last in the row's own name, the others in order, with
	// gaps, in the segments of the path above it.
	const auto& last = segments_.last();
	const auto at = name.indexOf(last, 0, Qt::CaseInsensitive);
	if (at < 0)
		return std::nullopt;
	auto path = pathSegments(source);
	if (!path.isEmpty())
		path.removeLast();
	qsizetype k = 0;
	for (qsizetype s = 0; s + 1 < segments_.size(); ++s) {
		while (k < path.size() && !path.at(k).contains(segments_.at(s), Qt::CaseInsensitive))
			++k;
		if (k == path.size())
			return std::nullopt;
		++k;
	}
	return Run{false, at, last.size()};
}

bool TreeFilter::hit(const QModelIndex& source) const
{
	if (!active() || !source.isValid() || nameRole_ < 0)
		return false;
	if (childRole_ >= 0 && source.data(childRole_).toBool())
		return false;
	// An older model's category row ("Fields") names every row below it.
	const auto kind = kindRole_ >= 0 ? source.data(kindRole_).toString() : QString{};
	if (kind == QLatin1String("category"))
		return false;
	if (kindsOn()) {
		// Only the endpoints of the kinds chosen; a group never.
		const int family = categoryRole_ >= 0 ? source.data(categoryRole_).toInt()
		                   : kind == QLatin1String("command") ? 2
		                   : kind == QLatin1String("service") ? 3 : 0;
		const bool endpoint = endpointRole_ >= 0 ? source.data(endpointRole_).toBool() : family != 0;
		if (!endpoint)
			return false;
		const int bit = family == 2 ? Commands : family == 3 ? Services : Fields;
		if ((kinds_ & bit) == 0)
			return false;
		if (text_.isEmpty())
			return true;
	}
	return textHit(source).has_value();
}

bool TreeFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (!active())
		return true;
	return hit(sourceModel()->index(sourceRow, 0, sourceParent));
}

int TreeFilter::tally(const QModelIndex& parent, bool underMatch) const
{
	int found = 0;
	const auto* source = sourceModel();
	for (int row = 0; row < source->rowCount(parent); ++row) {
		const auto child = source->index(row, 0, parent);
		const bool match = hit(child);
		if (match && !underMatch)
			++found;
		found += tally(child, underMatch || match);
	}
	return found;
}

void TreeFilter::recount()
{
	const int counted = !active() || !sourceModel() || nameRole_ < 0 ? 0 : tally({}, false);
	if (counted == count_)
		return;
	count_ = counted;
	Q_EMIT countChanged();
}

void TreeFilter::refilter()
{
	beginFilterChange();
	endFilterChange(Direction::Rows);
}

int TreeFilter::setText(const QString& text)
{
	const auto trimmed = text.trimmed();
	// The same text again: the rows shown stay as they are, only counted.
	if (trimmed != text_) {
		const bool was = active();
		beginFilterChange();
		text_ = trimmed;
		segments_ = segmentsOf(text_);
		endFilterChange(Direction::Rows);
		Q_EMIT textChanged();
		if (was != active())
			Q_EMIT activeChanged();
	}
	recount();
	return count_;
}

void TreeFilter::setKinds(int kinds)
{
	kinds &= AllKinds;
	if (kinds == kinds_)
		return;
	const bool was = active();
	beginFilterChange();
	kinds_ = kinds;
	endFilterChange(Direction::Rows);
	Q_EMIT kindsChanged();
	if (was != active())
		Q_EMIT activeChanged();
	recount();
}

bool TreeFilter::matches(const QModelIndex& index) const
{
	return index.isValid() && index.model() == this && hit(mapToSource(index));
}

QVariantMap TreeFilter::run(const QModelIndex& index) const
{
	const auto source = index.isValid() && index.model() == this ? mapToSource(index) : index;
	if (!source.isValid() || source.model() != sourceModel() || !hit(source))
		return {};
	const auto found = textHit(source);
	if (!found || found->start < 0)
		return {};
	return {{QStringLiteral("in"), found->type ? QStringLiteral("type") : QStringLiteral("name")},
	        {QStringLiteral("start"), found->start},
	        {QStringLiteral("length"), found->length}};
}

void TreeFilter::collect(const QModelIndex& parent, QModelIndexList& out) const
{
	for (int row = 0; row < rowCount(parent); ++row) {
		const auto shown = index(row, 0, parent);
		// Below a match nothing more is counted.
		if (hit(mapToSource(shown)))
			out.append(shown);
		else
			collect(shown, out);
	}
}

QModelIndexList TreeFilter::found() const
{
	QModelIndexList out;
	if (active() && sourceModel())
		collect({}, out);
	return out;
}

QString TreeFilter::nameOf(const QModelIndex& index) const
{
	if (nameRole_ < 0 || !index.isValid())
		return {};
	const auto source = index.model() == this ? mapToSource(index) : index;
	return source.isValid() && source.model() == sourceModel() ? source.data(nameRole_).toString() : QString{};
}

QString TreeFilter::namePath(const QModelIndex& index, const QString& separator) const
{
	auto at = index.isValid() && index.model() == this ? mapToSource(index) : index;
	if (nameRole_ < 0 || !at.isValid() || at.model() != sourceModel())
		return {};
	QStringList names;
	for (; at.isValid(); at = at.parent())
		names.prepend(at.data(nameRole_).toString());
	return names.join(separator);
}

QModelIndex TreeFilter::sourceAt(const QString& namePath, const QString& separator)
{
	auto* source = sourceModel();
	if (!source || nameRole_ < 0 || namePath.isEmpty())
		return {};
	QModelIndex at;
	for (const auto& name : namePath.split(separator)) {
		QModelIndex next;
		for (int row = 0;; ++row) {
			if (row >= source->rowCount(at)) {
				if (!source->canFetchMore(at))
					break;
				source->fetchMore(at);
				if (row >= source->rowCount(at))
					break;
			}
			const auto child = source->index(row, 0, at);
			if (child.data(nameRole_).toString() == name) {
				next = child;
				break;
			}
		}
		if (!next.isValid())
			return {};
		at = next;
	}
	return at;
}

QVariant TreeFilter::value(const QModelIndex& index, const QString& role) const
{
	const auto source = index.isValid() && index.model() == this ? mapToSource(index) : index;
	if (!source.isValid() || source.model() != sourceModel())
		return {};
	const auto roles = sourceModel()->roleNames();
	for (auto at = roles.cbegin(); at != roles.cend(); ++at)
		if (at.value() == role.toUtf8())
			return source.data(at.key());
	return {};
}

QModelIndex TreeFilter::mapToSource(const QModelIndex& proxyIndex) const
{
	if (proxyIndex.isValid() && proxyIndex.model() != this)
		return {};
	return QSortFilterProxyModel::mapToSource(proxyIndex);
}

QModelIndex TreeFilter::mapFromSource(const QModelIndex& sourceIndex) const
{
	if (!sourceModel() || (sourceIndex.isValid() && sourceIndex.model() != sourceModel()))
		return {};
	return QSortFilterProxyModel::mapFromSource(sourceIndex);
}
} // namespace telemetry::ux
