/*
 * @file DescriptorContent.cpp
 * @brief A descriptor.bin parsed for the viewer: its identity, its counts and its endpoint tree.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "DescriptorContent.hpp"
#include "SchemaText.hpp"
#include <telemetry/descriptor/Format.hpp>
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cstdint>
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

void DescriptorTreeModel::append(Node& under, std::vector<std::unique_ptr<Node>> rows)
{
	if (rows.empty())
		return;
	const auto first = static_cast<int>(under.children.size());
	const auto at = &under == root_.get() ? QModelIndex{} : createIndex(under.row, 0, &under);
	beginInsertRows(at, first, first + static_cast<int>(rows.size()) - 1);
	for (auto& row : rows) {
		row->parent = &under;
		row->row = static_cast<int>(under.children.size());
		under.children.push_back(std::move(row));
	}
	under.expandable = false;
	endInsertRows();
}

bool DescriptorTreeModel::hasChildren(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && (!owner->children.empty() || owner->expandable);
}

bool DescriptorTreeModel::canFetchMore(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && owner->expandable && owner->children.empty();
}

void DescriptorTreeModel::fetchMore(const QModelIndex& parentIndex)
{
	if (auto* owner = node(parentIndex); owner && canFetchMore(parentIndex) && populate)
		populate(*owner);
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

using schema_text::text;
using schema_text::typeText;

// The type whose members a row shows: a struct's own, an array's element's.
TypeId membersOf(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	return type && type->kind == TypeKind::Array ? type->relatedTypeId : id;
}

bool hasMembers(const rt::RuntimeSchema& schema, TypeId id)
{
	return !schema.members(membersOf(schema, id)).empty();
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

struct DescriptorContent::Schema {
	std::vector<rt::RuntimeType> types;
	std::vector<rt::RuntimeMember> members;
	std::vector<rt::RuntimeEnumEntry> enums;
	std::vector<rt::RuntimeCatalog> catalogs;
	std::vector<rt::RuntimeEndpoint> endpoints;
	std::unique_ptr<rt::RuntimeSchemaWorkspace> workspace;
	// The schema borrows these bytes: they are kept with it.
	QByteArray bytes;
	rt::RuntimeSchema schema;
};

DescriptorContent::DescriptorContent(const QByteArray& bytes, QObject* parent)
    : QObject(parent), tree_(this), shown_(DescriptorTreeModel::NameRole, DescriptorTreeModel::PathRole, this)
{
	shown_.setSourceModel(&tree_);
	tree_.populate = [this](Node& node) { populate(node); };
	auto parsedSchema = std::make_unique<Schema>();
	parsedSchema->bytes = bytes;
	const std::span<const std::byte> input{reinterpret_cast<const std::byte*>(parsedSchema->bytes.constData()),
	                                       static_cast<std::size_t>(parsedSchema->bytes.size())};
	rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
	const auto required = rt::RuntimeSchema::parse(input, sizing);
	if (!required && required.status != rt::Status::Capacity) {
		error_ = tr("not a descriptor this program reads (status %1, error %2)")
		             .arg(static_cast<unsigned>(required.status))
		             .arg(static_cast<unsigned>(required.error));
		return;
	}
	parsedSchema->types.resize(required.requirements.types);
	parsedSchema->members.resize(required.requirements.members);
	parsedSchema->enums.resize(required.requirements.enumEntries);
	parsedSchema->catalogs.resize(required.requirements.catalogs);
	parsedSchema->endpoints.resize(required.requirements.endpoints);
	parsedSchema->workspace = std::make_unique<rt::RuntimeSchemaWorkspace>(
	    parsedSchema->types, parsedSchema->members, parsedSchema->enums, parsedSchema->catalogs,
	    parsedSchema->endpoints);
	const auto parsed = rt::RuntimeSchema::parse(input, *parsedSchema->workspace);
	if (!parsed) {
		error_ = tr("not a descriptor this program reads (status %1, error %2)")
		             .arg(static_cast<unsigned>(parsed.status))
		             .arg(static_cast<unsigned>(parsed.error));
		return;
	}
	parsedSchema->schema = parsed.schema;
	schema_ = std::move(parsedSchema);
	const auto& schema = schema_->schema;
	valid_ = true;
	fingerprint_ = QStringLiteral("0x") +
	               QStringLiteral("%1").arg(schema.fingerprint(), 16, 16, QLatin1Char('0')).toUpper();
	types_ = static_cast<int>(required.requirements.types);
	// Categories, groups and endpoints now; members when a row is opened.
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
				item->category = static_cast<int>(c);
				item->typeId = endpoint.typeId;
				item->expandable = hasMembers(schema, endpoint.typeId);
				group->children.push_back(std::move(item));
			}
			categoryNode->children.push_back(std::move(group));
		}
		root->children.push_back(std::move(categoryNode));
	}
	tree_.setRoot(std::move(root));
	regions_ = headerRegions(input);
}

DescriptorContent::~DescriptorContent() = default;

void DescriptorContent::populate(Node& node)
{
	if (!schema_ || !node.expandable || !node.children.empty())
		return;
	const auto& schema = schema_->schema;
	std::vector<std::unique_ptr<Node>> rows;
	for (const auto& member : schema.members(membersOf(schema, node.typeId))) {
		auto child = std::make_unique<Node>();
		child->name = text(member.name);
		child->path = node.path + QLatin1Char('.') + child->name;
		child->kind = QStringLiteral("member");
		child->type = typeText(schema, member.typeId);
		child->category = node.category;
		child->typeId = member.typeId;
		child->expandable = hasMembers(schema, member.typeId);
		rows.push_back(std::move(child));
	}
	tree_.append(node, std::move(rows));
}

void DescriptorContent::openMatches(const QString& filter)
{
	if (!schema_)
		return;
	const auto& schema = schema_->schema;
	const auto hit = [&](const QString& name, const QString& path) {
		return name.contains(filter, Qt::CaseInsensitive) || path.contains(filter, Qt::CaseInsensitive);
	};
	// The way to every match not inside another: member indexes from its endpoint.
	struct Route {
		Node* endpoint;
		std::vector<std::uint32_t> steps;
	};
	std::vector<Route> routes;
	// A bound on the search, so a find in a large descriptor - or one whose
	// types nest and repeat - stays quick; and on the matches it opens.
	schema_text::RowBudget budget{maxFindVisits};
	std::vector<std::uint32_t> steps;
	// The schema bounds the depth: a type nests at most Limits::maxTypeDepth.
	std::function<void(Node*, TypeId, const QString&)> walk = [&](Node* endpoint, TypeId id, const QString& path) {
		const auto members = schema.members(membersOf(schema, id));
		for (std::uint32_t i = 0; i < members.size(); ++i) {
			if (routes.size() >= maxFindMatches)
				budget.cut = true;
			if (budget.cut || !budget.take())
				return;
			const auto name = text(members[i].name);
			const auto childPath = path + QLatin1Char('.') + name;
			steps.push_back(i);
			if (hit(name, childPath))
				routes.push_back({endpoint, steps});
			else
				walk(endpoint, members[i].typeId, childPath);
			steps.pop_back();
		}
	};
	std::function<void(Node&)> endpoints = [&](Node& node) {
		for (auto& child : node.children) {
			if (child->kind == QStringLiteral("endpoint")) {
				// A match shows everything below it: nothing to open.
				if (!hit(child->name, child->path))
					walk(child.get(), child->typeId, child->path);
			} else {
				endpoints(*child);
			}
		}
	};
	const auto* root = tree_.nodeAt({});
	if (!root)
		return;
	endpoints(const_cast<Node&>(*root));
	findLimited_ = budget.cut;
	// Rows made stay, so their number is bounded too.
	std::size_t made = 0;
	for (const auto& route : routes) {
		if (made >= maxFindRows) {
			findLimited_ = true;
			break;
		}
		Node* at = route.endpoint;
		for (const auto slot : route.steps) {
			const auto before = at->children.size();
			populate(*at);
			made += at->children.size() - before;
			if (slot >= at->children.size())
				break;
			at = at->children[slot].get();
		}
	}
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

// The shown rows below an index, with the node each stands for. Members not
// made into rows yet are read from the schema: a row shown but never opened
// shows all its members, so they are copied whole.
struct Walk {
	const QAbstractItemModel& model;
	const rt::RuntimeSchema& schema;
	std::function<const Node*(const QModelIndex&)> node;
	// Rows a copy writes at most: types that nest and repeat can expand past
	// anything worth copying.
	schema_text::RowBudget& budget;

	bool take() const { return budget.take(); }

	static bool unmade(const Node* at) { return at && at->expandable && at->children.empty(); }

	QString lines(const QModelIndex& index, int depth, bool withSelf) const
	{
		QString out;
		const auto* at = node(index);
		if (withSelf) {
			if (!take())
				return out;
			out += QString(depth * 2, QLatin1Char(' ')) + cell(at->name);
			if (!at->type.isEmpty())
				out += QStringLiteral(": ") + cell(at->type);
			if (!at->access.isEmpty())
				out += QStringLiteral(" (") + cell(at->access) + QLatin1Char(')');
			out += QLatin1Char('\n');
		}
		const auto below = withSelf ? depth + 1 : depth;
		if (unmade(at))
			return out + memberLines(at->typeId, below);
		for (int row = 0; row < model.rowCount(index); ++row)
			out += lines(model.index(row, 0, index), below, true);
		return out;
	}

	QString memberLines(TypeId id, int depth) const
	{
		QString out;
		for (const auto& member : schema.members(membersOf(schema, id))) {
			if (!take())
				break;
			out += QString(depth * 2, QLatin1Char(' ')) + cell(text(member.name)) + QStringLiteral(": ") +
			       cell(typeText(schema, member.typeId)) + QLatin1Char('\n');
			out += memberLines(member.typeId, depth + 1);
		}
		return out;
	}

	// A category or group: its children by name. An endpoint or member: its
	// type and access, and its members under "members" - no name can collide.
	QJsonValue json(const QModelIndex& index) const
	{
		const auto* at = node(index);
		QJsonObject children;
		if (unmade(at)) {
			children = memberJson(at->typeId);
		} else {
			for (int row = 0; row < model.rowCount(index); ++row) {
				const auto child = model.index(row, 0, index);
				if (!take())
					break;
				children.insert(node(child)->name, json(child));
			}
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

	QJsonObject memberJson(TypeId id) const
	{
		QJsonObject out;
		for (const auto& member : schema.members(membersOf(schema, id))) {
			if (!take())
				break;
			QJsonObject object{{QStringLiteral("type"), typeText(schema, member.typeId)}};
			if (const auto members = memberJson(member.typeId); !members.isEmpty())
				object.insert(QStringLiteral("members"), members);
			out.insert(text(member.name), object);
		}
		return out;
	}

	QString tsv(const QModelIndex& index, bool withSelf) const
	{
		QString out;
		const auto* at = node(index);
		if (withSelf) {
			if (!take())
				return out;
			out += cell(at->path) + QLatin1Char('\t') + at->kind + QLatin1Char('\t') + cell(at->type) +
			       QLatin1Char('\t') + cell(at->access) + QLatin1Char('\n');
		}
		if (unmade(at))
			return out + memberTsv(at->typeId, at->path);
		for (int row = 0; row < model.rowCount(index); ++row)
			out += tsv(model.index(row, 0, index), true);
		return out;
	}

	QString memberTsv(TypeId id, const QString& path) const
	{
		QString out;
		for (const auto& member : schema.members(membersOf(schema, id))) {
			if (!take())
				break;
			const auto childPath = path + QLatin1Char('.') + text(member.name);
			out += cell(childPath) + QStringLiteral("\tmember\t") + cell(typeText(schema, member.typeId)) +
			       QStringLiteral("\t\n");
			out += memberTsv(member.typeId, childPath);
		}
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
	if (!valid_)
		return 0;
	const auto trimmed = text.trimmed();
	// The same find again (Enter after the typing paused): nothing to redo.
	if (trimmed == shown_.text())
		return shown_.setText(trimmed);
	findLimited_ = false;
	if (!trimmed.isEmpty()) {
		// Rows are made with no find shown: a proxy that filters looks through
		// every made row below a parent for each row added there.
		shown_.setText({});
		openMatches(trimmed);
	}
	return shown_.setText(trimmed);
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
	if (!at || !schema_)
		return {};
	schema_text::RowBudget budget{maxCopiedRows};
	const Walk walk{shown_, schema_->schema, [this](const QModelIndex& shown) { return nodeOf(shown); }, budget};
	if (format == QStringLiteral("json")) {
		QJsonObject object{{at->name, walk.json(index)}};
		if ((copyCut_ = budget.cut))
			object.insert(QStringLiteral("…"), cutShort());
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	// The note is added after the walk: the walk is what tells whether the bound was reached.
	auto out = format == QStringLiteral("tsv") ? QStringLiteral("path\tkind\ttype\taccess\n") + walk.tsv(index, true)
	                                           : walk.lines(index, 0, true);
	if ((copyCut_ = budget.cut))
		out += cutShort() + QLatin1Char('\n');
	return out;
}

QString DescriptorContent::textOfAll(const QString& format) const
{
	if (!schema_)
		return {};
	schema_text::RowBudget budget{maxCopiedRows};
	const Walk walk{shown_, schema_->schema, [this](const QModelIndex& shown) { return nodeOf(shown); }, budget};
	if (format == QStringLiteral("json")) {
		QJsonObject object;
		for (int row = 0; row < shown_.rowCount(); ++row) {
			const auto child = shown_.index(row, 0);
			object.insert(nodeOf(child)->name, walk.json(child));
		}
		if ((copyCut_ = budget.cut))
			object.insert(QStringLiteral("…"), cutShort());
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	auto out = format == QStringLiteral("tsv") ? QStringLiteral("path\tkind\ttype\taccess\n") + walk.tsv({}, false)
	                                           : walk.lines({}, 0, false);
	if ((copyCut_ = budget.cut))
		out += cutShort() + QLatin1Char('\n');
	return out;
}

QString DescriptorContent::findLimitText() const
{
	return tr("Find looks through %L1 members and opens %L2 matches at most. Narrow the search to find the rest.")
	    .arg(maxFindVisits)
	    .arg(maxFindMatches);
}

QString DescriptorContent::copyNote() const
{
	return copyCut_ ? tr("Copied the first %L1 rows; the rest was not copied.").arg(maxCopiedRows) : QString{};
}

QString DescriptorContent::cutShort()
{
	return tr("… copied the first %L1 rows; the rest was not copied").arg(maxCopiedRows);
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
