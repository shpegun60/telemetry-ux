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
#include <functional>
#include <span>

namespace telemetry::ux {
namespace rt = telemetry::runtime;

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
	return createIndex(at->parent->row, 0, at->parent);
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
		case PathRole:
			return at->path;
		default:
			return {};
	}
}

QHash<int, QByteArray> DescriptorTreeModel::roleNames() const
{
	return {{NameRole, "name"},   {KindRole, "kind"},         {TypeRole, "typeText"}, {AccessRole, "access"},
	        {CountRole, "count"}, {CategoryRole, "category"}, {PathRole, "path"}};
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
		child->row = static_cast<int>(under.children.size());
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

DescriptorContent::DescriptorContent(const QByteArray& bytes, QObject* parent)
    : QObject(parent), tree_(this), shown_(DescriptorTreeModel::NameRole, DescriptorTreeModel::PathRole, this)
{
	shown_.setSourceModel(&tree_);
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
		categoryNode->row = static_cast<int>(root->children.size());
		for (const auto& catalog : schema.catalogs(category)) {
			++groups_;
			auto group = std::make_unique<Node>();
			group->name = text(catalog.name);
			group->path = group->name;
			group->category = static_cast<int>(c);
			group->kind = QStringLiteral("group");
			group->count = static_cast<int>(catalog.endpointCount);
			group->parent = categoryNode.get();
			group->row = static_cast<int>(categoryNode->children.size());
			for (const auto& endpoint : all.subspan(catalog.firstEndpoint, catalog.endpointCount)) {
				auto item = std::make_unique<Node>();
				item->name = text(endpoint.name);
				item->path = group->path + QLatin1Char('.') + item->name;
				item->kind = QStringLiteral("endpoint");
				item->type = typeText(schema, endpoint.typeId);
				item->access = accessText(category, schema, endpoint);
				item->parent = group.get();
				item->row = static_cast<int>(group->children.size());
				addMembers(schema, endpoint.typeId, *item, 0);
				group->children.push_back(std::move(item));
			}
			categoryNode->children.push_back(std::move(group));
		}
		root->children.push_back(std::move(categoryNode));
	}
	tree_.setRoot(std::move(root));
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
// A name or type as one cell: a tab or line break in it cannot split a row.
QString cell(QString text)
{
	text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
	text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
	text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
	return text;
}

// The shown rows below an index, with the node each stands for.
struct Walk {
	const QAbstractItemModel& model;
	std::function<const Node*(const QModelIndex&)> node;

	QString lines(const QModelIndex& index, int depth, bool withSelf) const
	{
		QString out;
		if (withSelf) {
			const auto* at = node(index);
			out += QString(depth * 2, QLatin1Char(' ')) + cell(at->name);
			if (!at->type.isEmpty())
				out += QStringLiteral(": ") + cell(at->type);
			if (!at->access.isEmpty())
				out += QStringLiteral(" (") + cell(at->access) + QLatin1Char(')');
			out += QLatin1Char('\n');
		}
		for (int row = 0; row < model.rowCount(index); ++row)
			out += lines(model.index(row, 0, index), withSelf ? depth + 1 : depth, true);
		return out;
	}

	// A category or group: its children by name. An endpoint or member: its
	// type and access, and its members under "members" - no name can collide.
	QJsonValue json(const QModelIndex& index) const
	{
		const auto* at = node(index);
		QJsonObject children;
		for (int row = 0; row < model.rowCount(index); ++row) {
			const auto child = model.index(row, 0, index);
			children.insert(node(child)->name, json(child));
		}
		if (at->type.isEmpty())
			return children;
		QJsonObject object{{QStringLiteral("type"), at->type}};
		if (!at->access.isEmpty())
			object.insert(QStringLiteral("access"), at->access);
		if (!children.isEmpty())
			object.insert(QStringLiteral("members"), children);
		return object;
	}

	QString tsv(const QModelIndex& index, bool withSelf) const
	{
		QString out;
		if (withSelf) {
			const auto* at = node(index);
			out += cell(at->path) + QLatin1Char('\t') + at->kind + QLatin1Char('\t') + cell(at->type) +
			       QLatin1Char('\t') + cell(at->access) + QLatin1Char('\n');
		}
		for (int row = 0; row < model.rowCount(index); ++row)
			out += tsv(model.index(row, 0, index), true);
		return out;
	}
};

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
	return valid_ ? shown_.setText(text) : 0;
}

bool DescriptorContent::matches(const QModelIndex& index) const
{
	return shown_.matches(index);
}

const DescriptorTreeModel::Node* DescriptorContent::nodeOf(const QModelIndex& index) const
{
	// Only a shown row: an index of another model, or none, names nothing.
	if (!index.isValid() || index.model() != &shown_)
		return nullptr;
	return tree_.nodeAt(shown_.mapToSource(index));
}

QString DescriptorContent::pathOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at ? at->path : QString{};
}

QString DescriptorContent::rowOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at ? cell(at->name) + QLatin1Char('\t') + cell(at->type) + QLatin1Char('\t') + cell(at->access)
	          : QString{};
}

QString DescriptorContent::textOf(const QModelIndex& index, const QString& format) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	const Walk walk{shown_, [this](const QModelIndex& shown) { return nodeOf(shown); }};
	if (format == QStringLiteral("json"))
		return QString::fromUtf8(
		    QJsonDocument(QJsonObject{{at->name, walk.json(index)}}).toJson(QJsonDocument::Indented));
	if (format == QStringLiteral("tsv"))
		return QStringLiteral("path\tkind\ttype\taccess\n") + walk.tsv(index, true);
	return walk.lines(index, 0, true);
}

QString DescriptorContent::textOfAll(const QString& format) const
{
	const Walk walk{shown_, [this](const QModelIndex& shown) { return nodeOf(shown); }};
	if (format == QStringLiteral("json")) {
		QJsonObject object;
		for (int row = 0; row < shown_.rowCount(); ++row) {
			const auto child = shown_.index(row, 0);
			object.insert(nodeOf(child)->name, walk.json(child));
		}
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	if (format == QStringLiteral("tsv"))
		return QStringLiteral("path\tkind\ttype\taccess\n") + walk.tsv({}, false);
	return walk.lines({}, 0, false);
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
