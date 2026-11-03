/*
 * @file Models.cpp
 * @brief Materialize Qt-owned metadata and bounded pages of Inspector nodes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Models.hpp"

#include <QtCore/QHash>
#include <QtCore/QString>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace inspector_qml {
namespace {
namespace ins = telemetry::integration::inspector;
using Category = telemetry::descriptor::Category;
using Kind = telemetry::TypeKind;
using Code = telemetry::ScalarCode;

QString text(std::string_view value)
{
	return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

template<class T>
QString integerText(T value)
{
	std::array<char, 32> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return result.ec == std::errc{} ? QString::fromLatin1(buffer.data(), result.ptr - buffer.data())
	                                : QString{};
}

template<class T>
QString floatText(T value)
{
	using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
	constexpr int fraction = sizeof(T) == 4 ? 23 : 52;
	constexpr Bits exponent = sizeof(T) == 4 ? Bits{0xff} : Bits{0x7ff};
	const auto bits = std::bit_cast<Bits>(value);
	const auto magnitude = bits & (std::numeric_limits<Bits>::max() >> 1);
	const bool negative = (bits >> (sizeof(T) * 8 - 1)) != 0;
	if (magnitude == 0)
		return negative ? QStringLiteral("-0") : QStringLiteral("0");
	if ((magnitude >> fraction) == exponent) {
		if ((magnitude & ((Bits{1} << fraction) - 1)) != 0)
			return QStringLiteral("NaN");
		return negative ? QStringLiteral("-inf") : QStringLiteral("inf");
	}
	std::array<char, 64> buffer{};
	const auto result =
	    std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
	                  std::chars_format::general, std::numeric_limits<T>::max_digits10);
	return result.ec == std::errc{} ? QString::fromLatin1(buffer.data(), result.ptr - buffer.data())
	                                : QString{};
}

QString scalarNumberText(const ins::ScalarValue& scalar)
{
	return std::visit(
	    [](const auto& number) -> QString {
		    using T = std::decay_t<decltype(number)>;
		    if constexpr (std::is_same_v<T, bool>)
			    return number ? QStringLiteral("true") : QStringLiteral("false");
		    else if constexpr (std::is_floating_point_v<T>)
			    return floatText(number);
		    else
			    return integerText(number);
	    },
	    scalar.value);
}

QString scalarText(const ins::ScalarValue& scalar)
{
	auto value = scalarNumberText(scalar);
	if (scalar.enumValue)
		value = scalar.enumKnown
		            ? text(scalar.enumName) + QStringLiteral(" (") + value + QLatin1Char(')')
		            : value + QStringLiteral(" (unknown)");
	return value;
}

QString scalarType(Code code)
{
	switch (code) {
		case Code::Bool:
			return QStringLiteral("bool");
		case Code::U8:
			return QStringLiteral("uint8");
		case Code::S8:
			return QStringLiteral("int8");
		case Code::U16:
			return QStringLiteral("uint16");
		case Code::S16:
			return QStringLiteral("int16");
		case Code::U32:
			return QStringLiteral("uint32");
		case Code::S32:
			return QStringLiteral("int32");
		case Code::U64:
			return QStringLiteral("uint64");
		case Code::S64:
			return QStringLiteral("int64");
		case Code::F32:
			return QStringLiteral("float32");
		case Code::F64:
			return QStringLiteral("float64");
	}
	return {};
}

struct Row {
	quintptr token = 0;
	Row* parent = nullptr;
	int row = 0;
	QString name, detail, value, type, path, editorText;
	std::uint32_t endpointId = 0, totalChildren = 0;
	int category = 0;
	bool readable = false, writable = false, endpoint = false, aggregate = false;
	ins::NodeHandle handle{};
	std::vector<std::unique_ptr<Row>> children;
};

// A QModelIndex stores only an integer token. Clear never reuses a token, so an
// index retained across reset cannot dereference a deleted or replacement row.
struct Tree {
	std::vector<std::unique_ptr<Row>> roots;
	QHash<quintptr, Row*> rows;
	quintptr nextToken = 1;
	bool changing = false;

	void clear()
	{
		rows.clear();
		roots.clear();
	}

	Row* append(std::unique_ptr<Row> row, Row* parent = nullptr)
	{
		if (nextToken == 0)
			return nullptr;
		auto& siblings = parent ? parent->children : roots;
		if (siblings.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
			return nullptr;
		row->token = nextToken++;
		row->parent = parent;
		row->row = static_cast<int>(siblings.size());
		auto* result = row.get();
		rows.insert(row->token, result);
		siblings.push_back(std::move(row));
		return result;
	}

	Row* at(const QAbstractItemModel* model, const QModelIndex& index) const
	{
		if (!index.isValid() || index.model() != model || index.column() != 0)
			return nullptr;
		auto* row = rows.value(index.internalId(), nullptr);
		return row && row->row == index.row() ? row : nullptr;
	}

	const std::vector<std::unique_ptr<Row>>* children(const QAbstractItemModel* model,
	                                                  const QModelIndex& parent) const
	{
		if (!parent.isValid())
			return &roots;
		const auto* row = at(model, parent);
		return row ? &row->children : nullptr;
	}
};

// Signals from begin/end model changes run synchronously. Keep the owner gate
// active through their complete delivery, and do not nest model mutations.
struct ChangeGuard {
	bool& flag;
	const bool previous;

	explicit ChangeGuard(bool& value) noexcept : flag(value), previous(value)
	{
		flag = true;
	}

	~ChangeGuard()
	{
		flag = previous;
	}

	ChangeGuard(const ChangeGuard&) = delete;
	ChangeGuard& operator=(const ChangeGuard&) = delete;
};

// The two portable owners expose the same read navigation. This Qt-local
// borrower keeps the old read-only fixture usable without another client/FIFO.
struct Reader {
	const Inspector* inspector = nullptr;
	const Workbench* workbench = nullptr;
	bool editor = false;
	bool response = false;

	bool requestEditor() const
	{
		return editor && workbench &&
		       (workbench->commandSelected() || workbench->serviceSelected());
	}

	explicit operator bool() const noexcept
	{
		return inspector || workbench;
	}

	template<class F>
	decltype(auto) visit(F&& read) const
	{
		if (workbench)
			return read(*workbench);
		return read(*inspector);
	}

	bool isCurrent() const
	{
		return *this && visit([](const auto& model) {
			return model.isCurrent();
		});
	}

	auto snapshotInfo() const
	{
		if (response && workbench)
			return workbench->serviceSnapshotInfo();
		if (requestEditor())
			return workbench->editorSnapshotInfo();
		return visit([](const auto& model) {
			return model.snapshotInfo();
		});
	}

	auto info(const ins::NodeHandle& node) const
	{
		if (response && workbench)
			return workbench->serviceInfo(node);
		if (requestEditor())
			return workbench->editorInfo(node);
		return visit([&](const auto& model) {
			return model.info(node);
		});
	}

	auto child(const ins::NodeHandle& node, std::uint32_t index) const
	{
		if (response && workbench)
			return workbench->serviceChild(node, index);
		if (requestEditor())
			return workbench->editorChild(node, index);
		return visit([&](const auto& model) {
			return model.child(node, index);
		});
	}

	auto root() const
	{
		if (response && workbench)
			return workbench->serviceRoot();
		if (requestEditor())
			return workbench->editorRoot();
		return visit([](const auto& model) {
			return model.root();
		});
	}

	auto catalogs(Category category) const
	{
		return visit([&](const auto& model) {
			return model.catalogs(category);
		});
	}

	auto endpoints(Category category) const
	{
		return visit([&](const auto& model) {
			return model.endpoints(category);
		});
	}

	bool editing() const
	{
		return !response && workbench && workbench->editing() &&
		       workbench->phase() != telemetry::integration::workbench::Phase::Executing;
	}

	ins::Result<ins::ScalarValue> scalar(const ins::NodeHandle& node) const
	{
		if (response && workbench)
			return workbench->serviceScalar(node);
		return editing() ? workbench->editScalar(node) : visit([&](const auto& model) {
			return model.scalar(node);
		});
	}
};

std::unique_ptr<Row> valueRow(const Reader& inspector, const ins::NodeHandle& handle)
{
	const auto info = inspector.info(handle);
	if (!info)
		return {};
	auto row = std::make_unique<Row>();
	row->handle = handle;
	row->name = info.value.arrayElement
	                ? QLatin1Char('[') + integerText(info.value.index) + QLatin1Char(']')
	                : text(info.value.name);
	row->totalChildren = info.value.children;
	row->aggregate = info.value.kind == Kind::Struct || info.value.kind == Kind::Array;
	if (row->aggregate) {
		row->type =
		    info.value.kind == Kind::Struct ? QStringLiteral("struct") : QStringLiteral("array");
		row->value = integerText(info.value.children) + QStringLiteral(" children");
	} else if (info.value.kind == Kind::Void) {
		row->type = QStringLiteral("void");
	} else {
		row->type = info.value.kind == Kind::Enum
		                ? QStringLiteral("enum (") + scalarType(info.value.code) + QLatin1Char(')')
		                : scalarType(info.value.code);
		const auto scalar = inspector.scalar(handle);
		if (!scalar)
			return {};
		row->value = scalarText(scalar.value);
		row->editorText = scalarNumberText(scalar.value);
	}
	row->detail = row->type + QStringLiteral(" #") + integerText(info.value.type);
	return row;
}
} // namespace

struct EndpointModel::Impl {
	Tree tree;
	Reader inspector;
	std::uint64_t admission = 0;

	bool current() const
	{
		return inspector.isCurrent() && inspector.snapshotInfo().admission == admission;
	}
};

EndpointModel::EndpointModel(QObject* parent)
    : QAbstractItemModel(parent), impl_(std::make_unique<Impl>())
{}

EndpointModel::~EndpointModel() = default;

void EndpointModel::clear()
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	beginResetModel();
	impl_->tree.clear();
	impl_->inspector = {};
	endResetModel();
}

bool EndpointModel::changing() const noexcept
{
	return impl_->tree.changing;
}

void EndpointModel::rebuild(const Inspector& inspector)
{
	rebuildSource(&inspector, nullptr);
}

void EndpointModel::rebuild(const Workbench& workbench)
{
	rebuildSource(nullptr, &workbench);
}

void EndpointModel::rebuildSource(const Inspector* inspector, const Workbench* workbench)
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	beginResetModel();
	impl_->tree.clear();
	impl_->inspector = {inspector, workbench};
	impl_->admission = impl_->inspector.snapshotInfo().admission;
	if (impl_->current()) {
		for (const auto category : {Category::Field, Category::Command, Category::Service}) {
			auto categoryRow = std::make_unique<Row>();
			categoryRow->category = static_cast<int>(category);
			categoryRow->name = category == Category::Field     ? QStringLiteral("Fields")
			                    : category == Category::Command ? QStringLiteral("Commands")
			                                                    : QStringLiteral("Services");
			auto* parent = impl_->tree.append(std::move(categoryRow));
			if (!parent)
				break;
			const auto endpoints = impl_->inspector.endpoints(category);
			for (const auto& catalog : impl_->inspector.catalogs(category)) {
				auto catalogRow = std::make_unique<Row>();
				catalogRow->category = static_cast<int>(category);
				catalogRow->name = text(catalog.name);
				catalogRow->detail =
				    integerText(catalog.endpointCount) + QStringLiteral(" endpoints");
				auto* group = impl_->tree.append(std::move(catalogRow), parent);
				if (!group)
					break;
				for (std::uint32_t i = 0; i < catalog.endpointCount; ++i) {
					const auto at = std::uint64_t{catalog.firstEndpoint} + i;
					if (at >= endpoints.size())
						break;
					const auto& endpoint = endpoints[static_cast<std::size_t>(at)];
					auto row = std::make_unique<Row>();
					row->category = static_cast<int>(category);
					row->endpoint = true;
					row->endpointId = endpoint.id;
					row->name = text(endpoint.name);
					row->path = group->name + QLatin1Char('.') + row->name;
					row->readable =
					    category == Category::Field &&
					    (endpoint.capabilities &
					     static_cast<std::uint8_t>(telemetry::descriptor::Capability::Readable)) !=
					        0;
					row->writable =
					    category == Category::Field &&
					    (endpoint.capabilities &
					     static_cast<std::uint8_t>(telemetry::descriptor::Capability::Writable)) !=
					        0;
					row->detail = QStringLiteral("ID ") + integerText(endpoint.id);
					if (category == Category::Field)
						row->detail += row->writable ? QStringLiteral(" · writable Field")
						                             : QStringLiteral(" · read-only Field");
					impl_->tree.append(std::move(row), group);
				}
			}
		}
	}
	endResetModel();
}

QVariantMap EndpointModel::endpoint(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !row->endpoint || !impl_->current())
		return {};
	return {
	    {QStringLiteral("id"), row->endpointId},     {QStringLiteral("category"), row->category},
	    {QStringLiteral("readable"), row->readable}, {QStringLiteral("writable"), row->writable},
	    {QStringLiteral("name"), row->name},         {QStringLiteral("path"), row->path}};
}

QModelIndex EndpointModel::index(int row, int column, const QModelIndex& parent) const
{
	const auto* children = impl_->tree.children(this, parent);
	if (row < 0 || column != 0 || !children || static_cast<std::size_t>(row) >= children->size())
		return {};
	return createIndex(row, column, (*children)[static_cast<std::size_t>(row)]->token);
}

QModelIndex EndpointModel::parent(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	return row && row->parent ? createIndex(row->parent->row, 0, row->parent->token)
	                          : QModelIndex{};
}

int EndpointModel::rowCount(const QModelIndex& parent) const
{
	const auto* children = impl_->tree.children(this, parent);
	return children ? static_cast<int>(children->size()) : 0;
}

int EndpointModel::columnCount(const QModelIndex& parent) const
{
	return !parent.isValid() || impl_->tree.at(this, parent) ? 1 : 0;
}

QVariant EndpointModel::data(const QModelIndex& index, int role) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !impl_->current())
		return {};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return row->name;
		case DetailRole:
			return row->detail;
		case ValueRole:
			return row->value;
		case EndpointIdRole:
			return row->endpointId;
		case CategoryRole:
			return row->category;
		case ReadableRole:
			return row->readable;
		case IsEndpointRole:
			return row->endpoint;
		case WritableRole:
			return row->writable;
		default:
			return {};
	}
}

QHash<int, QByteArray> EndpointModel::roleNames() const
{
	return {{Qt::DisplayRole, "display"},   {NameRole, "name"},
	        {DetailRole, "detail"},         {ValueRole, "value"},
	        {EndpointIdRole, "endpointId"}, {CategoryRole, "category"},
	        {ReadableRole, "readable"},     {IsEndpointRole, "isEndpoint"},
	        {WritableRole, "writable"}};
}

struct ValueModel::Impl {
	Tree tree;
	Reader inspector;
	std::uint64_t admission = 0, revision = 0;

	bool current() const
	{
		if (!inspector.isCurrent())
			return false;
		const auto snapshot = inspector.snapshotInfo();
		return snapshot.ready && snapshot.admission == admission && snapshot.revision == revision;
	}
};

ValueModel::ValueModel(QObject* parent)
    : QAbstractItemModel(parent), impl_(std::make_unique<Impl>())
{}

ValueModel::~ValueModel() = default;

void ValueModel::clear()
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	beginResetModel();
	impl_->tree.clear();
	impl_->inspector = {};
	endResetModel();
}

bool ValueModel::changing() const noexcept
{
	return impl_->tree.changing;
}

void ValueModel::resetSnapshot(const Inspector& inspector)
{
	resetSource(&inspector, nullptr);
}

void ValueModel::resetSnapshot(const Workbench& workbench)
{
	resetSource(nullptr, &workbench);
}

void ValueModel::resetServiceResponse(const Workbench& workbench)
{
	resetSource(nullptr, &workbench, true);
}

void ValueModel::resetSource(const Inspector* inspector, const Workbench* workbench, bool response)
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	beginResetModel();
	impl_->tree.clear();
	impl_->inspector = {inspector, workbench, !response, response};
	const auto snapshot = impl_->inspector.snapshotInfo();
	impl_->admission = snapshot.admission;
	impl_->revision = snapshot.revision;
	if (impl_->current()) {
		const auto root = impl_->inspector.root();
		if (root) {
			auto row = valueRow(impl_->inspector, root.value);
			if (row) {
				if (response)
					row->name = QStringLiteral("Response");
				else if (impl_->inspector.requestEditor())
					row->name = QStringLiteral("Request");
				impl_->tree.append(std::move(row));
			}
		}
	}
	endResetModel();
}

QModelIndex ValueModel::index(int row, int column, const QModelIndex& parent) const
{
	const auto* children = impl_->tree.children(this, parent);
	if (row < 0 || column != 0 || !children || static_cast<std::size_t>(row) >= children->size())
		return {};
	return createIndex(row, column, (*children)[static_cast<std::size_t>(row)]->token);
}

QModelIndex ValueModel::parent(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	return row && row->parent ? createIndex(row->parent->row, 0, row->parent->token)
	                          : QModelIndex{};
}

int ValueModel::rowCount(const QModelIndex& parent) const
{
	const auto* children = impl_->tree.children(this, parent);
	return children ? static_cast<int>(children->size()) : 0;
}

int ValueModel::columnCount(const QModelIndex& parent) const
{
	return !parent.isValid() || impl_->tree.at(this, parent) ? 1 : 0;
}

QVariant ValueModel::data(const QModelIndex& index, int role) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !impl_->current())
		return {};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return row->name;
		case DetailRole:
			return row->detail;
		case ValueRole:
			return row->value;
		case TypeRole:
			return row->type;
		case AggregateRole:
			return row->aggregate;
		case ChildCountRole:
			return row->totalChildren;
		case LoadedCountRole:
			return static_cast<int>(row->children.size());
		case EditableRole:
			return impl_->inspector.editing() && !row->aggregate && !row->type.isEmpty() &&
			       row->type != QStringLiteral("void");
		case EditorTextRole:
			return row->editorText;
		default:
			return {};
	}
}

QHash<int, QByteArray> ValueModel::roleNames() const
{
	return {{Qt::DisplayRole, "display"},
	        {NameRole, "name"},
	        {DetailRole, "detail"},
	        {ValueRole, "value"},
	        {TypeRole, "type"},
	        {AggregateRole, "isAggregate"},
	        {ChildCountRole, "childCount"},
	        {LoadedCountRole, "loadedCount"},
	        {EditableRole, "editable"},
	        {EditorTextRole, "editorText"}};
}

ins::NodeResult ValueModel::node(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !impl_->current())
		return {ins::ModelStatus::Stale, {}};
	const auto checked = impl_->inspector.info(row->handle);
	return checked ? ins::NodeResult{ins::ModelStatus::Ok, row->handle}
	               : ins::NodeResult{checked.status, {}};
}

void ValueModel::updateScalar(const QModelIndex& index)
{
	if (changing() || !impl_->current())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	auto* row = impl_->tree.at(this, index);
	if (!row || row->aggregate)
		return;
	const auto scalar = impl_->inspector.scalar(row->handle);
	if (!scalar)
		return;
	row->value = scalarText(scalar.value);
	row->editorText = scalarNumberText(scalar.value);
	Q_EMIT dataChanged(index, index, {ValueRole, EditorTextRole});
}

bool ValueModel::canFetchMore(const QModelIndex& parent) const
{
	const auto* row = impl_->tree.at(this, parent);
	return row && impl_->current() && row->aggregate && row->children.size() < row->totalChildren;
}

void ValueModel::fetchMore(const QModelIndex& parent)
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	auto* row = impl_->tree.at(this, parent);
	if (!row || !canFetchMore(parent))
		return;
	const auto first = row->children.size();
	const auto last = std::min<std::size_t>(row->totalChildren, first + fetchBatch);
	std::vector<std::unique_ptr<Row>> pending;
	pending.reserve(last - first);
	for (auto at = first; at < last; ++at) {
		const auto handle = impl_->inspector.child(row->handle, static_cast<std::uint32_t>(at));
		if (!handle)
			return;
		auto child = valueRow(impl_->inspector, handle.value);
		if (!child)
			return;
		pending.push_back(std::move(child));
	}
	if (pending.empty() || impl_->tree.nextToken == 0 ||
	    pending.size() - 1 > std::numeric_limits<quintptr>::max() - impl_->tree.nextToken)
		return;
	beginInsertRows(parent, static_cast<int>(first), static_cast<int>(last - 1));
	for (auto& child : pending)
		impl_->tree.append(std::move(child), row);
	endInsertRows();
	Q_EMIT dataChanged(parent, parent, {LoadedCountRole});
}

bool ValueModel::hasChildren(const QModelIndex& parent) const
{
	if (!parent.isValid())
		return !impl_->tree.roots.empty();
	const auto* row = impl_->tree.at(this, parent);
	return row && (!row->children.empty() ||
	               (row->aggregate && row->totalChildren != 0 && impl_->current()));
}

} // namespace inspector_qml
