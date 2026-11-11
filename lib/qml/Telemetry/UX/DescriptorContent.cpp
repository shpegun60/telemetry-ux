/*
 * @file DescriptorContent.cpp
 * @brief A descriptor.bin parsed for the viewer: its identity, its counts and its endpoint tree.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DescriptorContent.hpp"
#include <telemetry/descriptor/Format.hpp>
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <span>

namespace telemetry::ux {
namespace rt = telemetry::runtime;

int DescriptorTreeModel::Node::row() const noexcept
{
	if (!parent)
		return 0;
	for (std::size_t i = 0; i < parent->children.size(); ++i)
		if (parent->children[i].get() == this)
			return static_cast<int>(i);
	return 0;
}

DescriptorTreeModel::DescriptorTreeModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_unique<Node>())
{}

DescriptorTreeModel::~DescriptorTreeModel() = default;

void DescriptorTreeModel::setRoot(std::unique_ptr<Node> root)
{
	beginResetModel();
	root_ = std::move(root);
	endResetModel();
}

DescriptorTreeModel::Node* DescriptorTreeModel::node(const QModelIndex& index) const noexcept
{
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex DescriptorTreeModel::index(int row, int column, const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	if (!owner || column != 0 || row < 0 || row >= static_cast<int>(owner->children.size()))
		return {};
	return createIndex(row, 0, owner->children[static_cast<std::size_t>(row)].get());
}

QModelIndex DescriptorTreeModel::parent(const QModelIndex& child) const
{
	const auto* at = node(child);
	if (!child.isValid() || !at || !at->parent || at->parent == root_.get())
		return {};
	return createIndex(at->parent->row(), 0, at->parent);
}

int DescriptorTreeModel::rowCount(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner ? static_cast<int>(owner->children.size()) : 0;
}

int DescriptorTreeModel::columnCount(const QModelIndex&) const
{
	return 1;
}

QVariant DescriptorTreeModel::data(const QModelIndex& index, int role) const
{
	const auto* at = node(index);
	if (!index.isValid() || !at)
		return {};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return at->name;
		case KindRole:
			return at->kind;
		case TypeRole:
			return at->type;
		case AccessRole:
			return at->access;
		case CountRole:
			return at->count;
		case CategoryRole:
			return at->category;
		default:
			return {};
	}
}

QHash<int, QByteArray> DescriptorTreeModel::roleNames() const
{
	return {{NameRole, "name"},     {KindRole, "kind"},   {TypeRole, "typeText"}, {AccessRole, "access"},
	        {CountRole, "count"},   {CategoryRole, "category"}};
}

namespace {
using Node = DescriptorTreeModel::Node;

QString text(std::string_view view)
{
	return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QString scalarName(ScalarCode code)
{
	switch (code) {
		case ScalarCode::Bool:
			return QStringLiteral("bool");
		case ScalarCode::U8:
			return QStringLiteral("u8");
		case ScalarCode::S8:
			return QStringLiteral("i8");
		case ScalarCode::U16:
			return QStringLiteral("u16");
		case ScalarCode::S16:
			return QStringLiteral("i16");
		case ScalarCode::U32:
			return QStringLiteral("u32");
		case ScalarCode::S32:
			return QStringLiteral("i32");
		case ScalarCode::U64:
			return QStringLiteral("u64");
		case ScalarCode::S64:
			return QStringLiteral("i64");
		case ScalarCode::F32:
			return QStringLiteral("f32");
		case ScalarCode::F64:
			return QStringLiteral("f64");
	}
	return QStringLiteral("?");
}

// The type in words, short enough for one column.
QString typeText(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type)
		return QStringLiteral("?");
	switch (type->kind) {
		case TypeKind::Void:
			return DescriptorContent::tr("nothing");
		case TypeKind::Scalar:
			return scalarName(type->scalarCode);
		case TypeKind::Enum: {
			QStringList names;
			for (const auto& entry : schema.enumEntries(id))
				names << text(entry.name);
			const auto shown = names.mid(0, 4).join(QStringLiteral(", "));
			return names.size() > 4 ? DescriptorContent::tr("enum %1, …").arg(shown)
			                        : DescriptorContent::tr("enum %1").arg(shown);
		}
		case TypeKind::Struct:
			return type->memberCount == 1 ? DescriptorContent::tr("struct, 1 member")
			                              : DescriptorContent::tr("struct, %1 members").arg(type->memberCount);
		case TypeKind::Array:
			return QStringLiteral("%1[%2]").arg(typeText(schema, type->relatedTypeId)).arg(type->elementCount);
	}
	return QStringLiteral("?");
}

// A struct's members below the node that has the type, and theirs below them.
void addMembers(const rt::RuntimeSchema& schema, TypeId id, Node& under, int depth)
{
	auto target = id;
	if (const auto* type = schema.type(id); type && type->kind == TypeKind::Array)
		target = type->relatedTypeId;
	if (depth > 8)
		return;
	for (const auto& member : schema.members(target)) {
		auto child = std::make_unique<Node>();
		child->name = text(member.name);
		child->path = under.path + QLatin1Char('.') + child->name;
		child->kind = QStringLiteral("member");
		child->type = typeText(schema, member.typeId);
		child->parent = &under;
		addMembers(schema, member.typeId, *child, depth + 1);
		under.children.push_back(std::move(child));
	}
}

QString accessText(descriptor::Category category, const rt::RuntimeSchema& schema, const rt::RuntimeEndpoint& endpoint)
{
	if (category == descriptor::Category::Field) {
		const bool read = endpoint.capabilities & static_cast<std::uint8_t>(descriptor::Capability::Readable);
		const bool write = endpoint.capabilities & static_cast<std::uint8_t>(descriptor::Capability::Writable);
		return read && write ? DescriptorContent::tr("read, write")
		     : read          ? DescriptorContent::tr("read")
		     : write         ? DescriptorContent::tr("write")
		                     : DescriptorContent::tr("no access");
	}
	return DescriptorContent::tr("%1 → %2").arg(typeText(schema, endpoint.typeId),
	                                            typeText(schema, endpoint.responseTypeId));
}
} // namespace

bool DescriptorContent::names(const QString& devicePath)
{
	return devicePath == QStringLiteral("/telemetry/descriptor.bin");
}

DescriptorContent::DescriptorContent(const QByteArray& bytes, QObject* parent) : QObject(parent), tree_(this)
{
	const std::span<const std::byte> input{reinterpret_cast<const std::byte*>(bytes.constData()),
	                                       static_cast<std::size_t>(bytes.size())};
	rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
	const auto required = rt::RuntimeSchema::parse(input, sizing);
	if (!required && required.status != rt::Status::Capacity) {
		error_ = tr("not a descriptor this program reads (status %1, error %2)")
		             .arg(static_cast<unsigned>(required.status))
		             .arg(static_cast<unsigned>(required.error));
		return;
	}
	std::vector<rt::RuntimeType> types(required.requirements.types);
	std::vector<rt::RuntimeMember> members(required.requirements.members);
	std::vector<rt::RuntimeEnumEntry> enums(required.requirements.enumEntries);
	std::vector<rt::RuntimeCatalog> catalogs(required.requirements.catalogs);
	std::vector<rt::RuntimeEndpoint> endpoints(required.requirements.endpoints);
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto parsed = rt::RuntimeSchema::parse(input, workspace);
	if (!parsed) {
		error_ = tr("not a descriptor this program reads (status %1, error %2)")
		             .arg(static_cast<unsigned>(parsed.status))
		             .arg(static_cast<unsigned>(parsed.error));
		return;
	}
	const auto& schema = parsed.schema;
	valid_ = true;
	fingerprint_ = QStringLiteral("0x") +
	               QStringLiteral("%1").arg(schema.fingerprint(), 16, 16, QLatin1Char('0')).toUpper();
	types_ = static_cast<int>(required.requirements.types);
	// Everything the tree shows is copied out of the schema: the workspace
	// and the bytes go when this constructor returns.
	auto root = std::make_unique<Node>();
	const struct {
		descriptor::Category category;
		QString name;
	} categories[] = {{descriptor::Category::Field, tr("Fields")},
	                  {descriptor::Category::Command, tr("Commands")},
	                  {descriptor::Category::Service, tr("Services")}};
	for (std::size_t c = 0; c < 3; ++c) {
		const auto category = categories[c].category;
		const auto all = schema.endpoints(category);
		counts_[c] = static_cast<int>(all.size());
		if (all.empty())
			continue;
		auto categoryNode = std::make_unique<Node>();
		categoryNode->name = categories[c].name;
		categoryNode->path = categoryNode->name;
		categoryNode->category = static_cast<int>(c);
		categoryNode->kind = QStringLiteral("category");
		categoryNode->count = counts_[c];
		categoryNode->parent = root.get();
		for (const auto& catalog : schema.catalogs(category)) {
			++groups_;
			auto group = std::make_unique<Node>();
			group->name = text(catalog.name);
			group->path = group->name;
			group->category = static_cast<int>(c);
			group->kind = QStringLiteral("group");
			group->count = static_cast<int>(catalog.endpointCount);
			group->parent = categoryNode.get();
			for (const auto& endpoint : all.subspan(catalog.firstEndpoint, catalog.endpointCount)) {
				auto item = std::make_unique<Node>();
				item->name = text(endpoint.name);
				item->path = group->path + QLatin1Char('.') + item->name;
				item->kind = QStringLiteral("endpoint");
				item->type = typeText(schema, endpoint.typeId);
				item->access = accessText(category, schema, endpoint);
				item->parent = group.get();
				addMembers(schema, endpoint.typeId, *item, 0);
				group->children.push_back(std::move(item));
			}
			categoryNode->children.push_back(std::move(group));
		}
		root->children.push_back(std::move(categoryNode));
	}
	full_ = std::move(root);
	setFilter({});
	regions_ = headerRegions(input);
}

QString DescriptorContent::fingerprintGroups() const
{
	auto digits = fingerprint_.mid(2);
	QStringList groups;
	for (qsizetype i = 0; i < digits.size(); i += 4)
		groups << digits.mid(i, 4);
	return groups.join(QLatin1Char(' '));
}

namespace {
// A copy of what matches below `from`, with the ancestors that lead there;
// null when nothing does. Counts the matching nodes.
std::unique_ptr<Node> keep(const Node& from, Node* parent, const QString& filter, int& matches)
{
	auto copy = std::make_unique<Node>();
	copy->name = from.name;
	copy->kind = from.kind;
	copy->type = from.type;
	copy->access = from.access;
	copy->path = from.path;
	copy->count = from.count;
	copy->category = from.category;
	copy->parent = parent;
	const bool hit = !filter.isEmpty() && (from.name.contains(filter, Qt::CaseInsensitive) ||
	                                       from.path.contains(filter, Qt::CaseInsensitive));
	if (hit && from.kind != QStringLiteral("category"))
		++matches;
	for (const auto& child : from.children)
		if (auto kept = keep(*child, copy.get(), filter, matches))
			copy->children.push_back(std::move(kept));
	if (filter.isEmpty() || hit || !copy->children.empty())
		return copy;
	return nullptr;
}

QString lines(const Node& node, int depth, bool withSelf)
{
	QString out;
	if (withSelf) {
		out += QString(depth * 2, QLatin1Char(' ')) + node.name;
		if (!node.type.isEmpty())
			out += QStringLiteral(": ") + node.type;
		if (!node.access.isEmpty())
			out += QStringLiteral(" (") + node.access + QLatin1Char(')');
		out += QLatin1Char('\n');
	}
	for (const auto& child : node.children)
		out += lines(*child, withSelf ? depth + 1 : depth, true);
	return out;
}

QJsonValue json(const Node& node)
{
	if (node.children.empty() && !node.type.isEmpty()) {
		QJsonObject leaf{{QStringLiteral("type"), node.type}};
		if (!node.access.isEmpty())
			leaf.insert(QStringLiteral("access"), node.access);
		return leaf;
	}
	QJsonObject object;
	for (const auto& child : node.children)
		object.insert(child->name, json(*child));
	if (!node.type.isEmpty())
		object.insert(QStringLiteral("_type"), node.type);
	return object;
}

QString tsv(const Node& node, bool withSelf)
{
	QString out;
	if (withSelf)
		out += node.path + QLatin1Char('\t') + node.kind + QLatin1Char('\t') + node.type + QLatin1Char('\t') +
		       node.access + QLatin1Char('\n');
	for (const auto& child : node.children)
		out += tsv(*child, true);
	return out;
}

quint32 readU32(std::span<const std::byte> bytes, std::size_t at)
{
	quint32 value = 0;
	for (std::size_t i = 0; i < 4 && at + i < bytes.size(); ++i)
		value |= static_cast<quint32>(bytes[at + i]) << (8 * i);
	return value;
}
} // namespace

// The 64-byte header's fields (doc/WireV3.md) and the sections that follow.
QVariantList DescriptorContent::headerRegions(std::span<const std::byte> bytes)
{
	QVariantList regions;
	if (bytes.size() < 64)
		return regions;
	const auto region = [&](qint64 offset, qint64 length, const QString& name, const QString& kind, int depth) {
		regions.push_back(QVariantMap{{QStringLiteral("offset"), offset},
		                              {QStringLiteral("length"), length},
		                              {QStringLiteral("name"), name},
		                              {QStringLiteral("path"), depth == 0 ? name : tr("header › %1").arg(name)},
		                              {QStringLiteral("kind"), kind},
		                              {QStringLiteral("depth"), depth}});
	};
	region(0, 64, tr("header"), QStringLiteral("header"), 0);
	const struct {
		int offset, length;
		const char* name;
	} fields[] = {{0, 4, "magic"},
	              {4, 2, "major"},
	              {6, 2, "minor"},
	              {8, 2, "headerBytes"},
	              {10, 2, "flags"},
	              {12, 4, "totalBytes"},
	              {16, 8, "fingerprint"},
	              {24, 4, "typeCount"},
	              {28, 4, "fieldCatalogCount"},
	              {32, 4, "fieldCount"},
	              {36, 4, "commandCatalogCount"},
	              {40, 4, "commandCount"},
	              {44, 4, "serviceCatalogCount"},
	              {48, 4, "serviceCount"},
	              {52, 4, "typesOffset"},
	              {56, 4, "catalogsOffset"},
	              {60, 4, "endpointsOffset"}};
	for (const auto& field : fields)
		region(field.offset, field.length, QString::fromLatin1(field.name), QStringLiteral("field"), 1);
	const qint64 total = std::min<qint64>(readU32(bytes, 12), static_cast<qint64>(bytes.size()));
	const qint64 types = readU32(bytes, 52), catalogs = readU32(bytes, 56), endpoints = readU32(bytes, 60);
	if (types >= 64 && types <= catalogs && catalogs <= endpoints && endpoints <= total) {
		if (catalogs > types)
			region(types, catalogs - types, tr("types"), QStringLiteral("section"), 0);
		if (endpoints > catalogs)
			region(catalogs, endpoints - catalogs, tr("catalogs"), QStringLiteral("section"), 0);
		if (total > endpoints)
			region(endpoints, total - endpoints, tr("endpoints"), QStringLiteral("section"), 0);
	}
	return regions;
}

int DescriptorContent::setFilter(const QString& text)
{
	if (!full_)
		return 0;
	int matches = 0;
	const auto filter = text.trimmed();
	auto shown = keep(*full_, nullptr, filter, matches);
	tree_.setRoot(shown ? std::move(shown) : std::make_unique<Node>());
	return matches;
}

QString DescriptorContent::pathOf(const QModelIndex& index) const
{
	const auto* at = index.isValid() ? tree_.nodeAt(index) : nullptr;
	return at ? at->path : QString{};
}

QString DescriptorContent::rowOf(const QModelIndex& index) const
{
	const auto* at = index.isValid() ? tree_.nodeAt(index) : nullptr;
	return at ? at->name + QLatin1Char('\t') + at->type + QLatin1Char('\t') + at->access : QString{};
}

QString DescriptorContent::textOf(const QModelIndex& index, const QString& format) const
{
	const auto* at = index.isValid() ? tree_.nodeAt(index) : tree_.nodeAt({});
	if (!at)
		return {};
	const bool whole = !index.isValid();
	if (format == QStringLiteral("json")) {
		// A subtree under its own name; the whole tree as it is.
		const auto object = whole ? json(*at).toObject() : QJsonObject{{at->name, json(*at)}};
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	if (format == QStringLiteral("tsv"))
		return QStringLiteral("path\tkind\ttype\taccess\n") + tsv(*at, !whole);
	return lines(*at, 0, !whole);
}

QString DescriptorContent::summary() const
{
	if (!valid_)
		return {};
	const auto count = [](int n, const QString& one, const QString& many) {
		return n == 1 ? one : many.arg(n);
	};
	return tr("%1 · %2 · %3 · %4")
	    .arg(count(counts_[0], tr("1 Field"), tr("%1 Fields")), count(counts_[1], tr("1 Command"), tr("%1 Commands")),
	         count(counts_[2], tr("1 Service"), tr("%1 Services")), count(types_, tr("1 type"), tr("%1 types")));
}
} // namespace telemetry::ux
