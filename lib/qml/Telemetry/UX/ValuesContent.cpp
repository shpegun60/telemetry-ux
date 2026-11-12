/*
 * @file ValuesContent.cpp
 * @brief A values.bin read against its descriptor for the viewer: a tree of every Field with its value.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "ValuesContent.hpp"
#include "SchemaText.hpp"
#include <integration/telemetry/v3/ValuesReader.hpp>
#include <telemetry/runtime/DynamicValueView.hpp>
#include <telemetry/runtime/RuntimeSchema.hpp>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>
#include <algorithm>
#include <functional>
#include <utility>

namespace telemetry::ux {
namespace rt = telemetry::runtime;
namespace v3 = resource::telemetry::v3;
using Node = ValuesTreeModel::Node;

// ---- The model ------------------------------------------------------------

ValuesTreeModel::ValuesTreeModel(QObject* parent) : QAbstractItemModel(parent), root_(std::make_unique<Node>()) {}

ValuesTreeModel::~ValuesTreeModel() = default;

void ValuesTreeModel::setRoot(std::unique_ptr<Node> root)
{
	beginResetModel();
	root_ = std::move(root);
	endResetModel();
}

Node* ValuesTreeModel::node(const QModelIndex& index) const noexcept
{
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

const Node* ValuesTreeModel::nodeAt(const QModelIndex& index) const noexcept
{
	return index.isValid() && index.model() == this ? node(index) : nullptr;
}

QModelIndex ValuesTreeModel::indexOf(const Node& at) const
{
	return &at == root_.get() ? QModelIndex{} : createIndex(at.row, 0, const_cast<Node*>(&at));
}

void ValuesTreeModel::append(Node& under, std::vector<std::unique_ptr<Node>> rows)
{
	if (rows.empty())
		return;
	const auto first = static_cast<int>(under.children.size());
	beginInsertRows(indexOf(under), first, first + static_cast<int>(rows.size()) - 1);
	for (auto& row : rows) {
		row->parent = &under;
		row->row = static_cast<int>(under.children.size());
		under.children.push_back(std::move(row));
	}
	endInsertRows();
}

bool ValuesTreeModel::hasChildren(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && (!owner->children.empty() || owner->total > 0);
}

bool ValuesTreeModel::canFetchMore(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && owner->total > 0 && owner->children.empty();
}

void ValuesTreeModel::fetchMore(const QModelIndex& parentIndex)
{
	if (auto* owner = node(parentIndex); owner && canFetchMore(parentIndex) && populate)
		populate(*owner);
}

void ValuesTreeModel::touched(const std::vector<const Node*>& nodes)
{
	static const QList<int> roles{ValueRole, ChangedRole, UnavailableRole, NumericRole, ChangedFieldsRole};
	// One signal per run of neighbouring rows under one parent: an array
	// whose every element changed is one signal, not thousands.
	std::vector<const Node*> sorted;
	sorted.reserve(nodes.size());
	for (const auto* at : nodes)
		if (at && at != root_.get())
			sorted.push_back(at);
	std::sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) {
		return a->parent != b->parent ? std::less<const Node*>{}(a->parent, b->parent) : a->row < b->row;
	});
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
	for (std::size_t first = 0; first < sorted.size();) {
		auto last = first;
		while (last + 1 < sorted.size() && sorted[last + 1]->parent == sorted[first]->parent &&
		       sorted[last + 1]->row == sorted[last]->row + 1)
			++last;
		Q_EMIT dataChanged(createIndex(sorted[first]->row, 0, const_cast<Node*>(sorted[first])),
		                   createIndex(sorted[last]->row, 0, const_cast<Node*>(sorted[last])), roles);
		first = last + 1;
	}
}

QModelIndex ValuesTreeModel::index(int row, int column, const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	if (!owner || column != 0 || row < 0 || row >= static_cast<int>(owner->children.size()))
		return {};
	return createIndex(row, 0, owner->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ValuesTreeModel::parent(const QModelIndex& child) const
{
	const auto* at = child.isValid() ? node(child) : nullptr;
	if (!at || !at->parent || at->parent == root_.get())
		return {};
	return createIndex(at->parent->row, 0, at->parent);
}

int ValuesTreeModel::rowCount(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner ? static_cast<int>(owner->children.size()) : 0;
}

int ValuesTreeModel::columnCount(const QModelIndex&) const
{
	return 1;
}

QVariant ValuesTreeModel::data(const QModelIndex& index, int role) const
{
	const auto* at = index.isValid() ? node(index) : nullptr;
	if (!at)
		return {};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return at->name;
		case KindRole:
			return at->kind;
		case TypeRole:
			return at->type;
		case ValueRole:
			return at->value;
		case UnavailableRole:
			return at->unavailable;
		case ChangedRole:
			return at->changed;
		case CountRole:
			return at->count;
		case ChangedFieldsRole:
			return at->changedFields;
		case PathRole:
			// A range's rows are its array's: it has no path of its own.
			return at->kind == QStringLiteral("range") ? QString{} : at->path;
		case NumericRole:
			// Numbers and true/false: right-aligned, so a struct's members line up.
			return (at->json == 1 && at->jsonText.isEmpty()) || at->json == 2;
		case FindRole:
			return at->kind == QStringLiteral("range") ? QString{} : at->name;
		default:
			return {};
	}
}

QHash<int, QByteArray> ValuesTreeModel::roleNames() const
{
	return {{NameRole, "name"},   {KindRole, "kind"},         {TypeRole, "typeText"},
	        {ValueRole, "value"}, {UnavailableRole, "unavailable"}, {ChangedRole, "changed"},
	        {CountRole, "count"}, {PathRole, "path"},           {NumericRole, "numeric"},
	        {ChangedFieldsRole, "changedFields"}};
}

// ---- The content ----------------------------------------------------------

struct ValuesContent::Schema {
	std::vector<rt::RuntimeType> types;
	std::vector<rt::RuntimeMember> members;
	std::vector<rt::RuntimeEnumEntry> enums;
	std::vector<rt::RuntimeCatalog> catalogs;
	std::vector<rt::RuntimeEndpoint> endpoints;
	std::unique_ptr<rt::RuntimeSchemaWorkspace> workspace;
	// The schema borrows these bytes: they are kept with it.
	QByteArray bytes;
	rt::RuntimeSchema schema;
	// The shown snapshot and its tokens, which point into it: what rows made
	// later are filled from.
	QByteArray shown;
	std::vector<v3::ValuesEntry> entries;
};

namespace {
std::span<const std::byte> bytesOf(const QByteArray& bytes)
{
	return {reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

QString groupsOf(quint64 fingerprint)
{
	const auto digits = QStringLiteral("%1").arg(fingerprint, 16, 16, QLatin1Char('0')).toUpper();
	QStringList groups;
	for (qsizetype i = 0; i < digits.size(); i += 4)
		groups << digits.mid(i, 4);
	return groups.join(QLatin1Char(' '));
}

Node& attach(Node& under, std::unique_ptr<Node> child)
{
	child->parent = &under;
	child->row = static_cast<int>(under.children.size());
	under.children.push_back(std::move(child));
	return *under.children.back();
}

// How many rows a value of this type has below it: members or elements.
std::uint32_t childCount(const rt::RuntimeSchema& schema, std::uint32_t typeId)
{
	const auto* type = schema.type(typeId);
	if (!type)
		return 0;
	return type->kind == TypeKind::Struct  ? type->memberCount
	     : type->kind == TypeKind::Array ? type->elementCount
	                                     : 0;
}

// Eclipse's way with a long array: ranges of 256, and ranges of those past
// 65 536, so any element is a few levels down. The span of one range below
// count elements.
std::uint64_t rangeSpan(std::uint32_t count)
{
	std::uint64_t span = ValuesContent::rangeSize;
	while ((count + span - 1) / span > ValuesContent::rangeSize)
		span *= ValuesContent::rangeSize;
	return span;
}

bool leafType(const rt::RuntimeSchema& schema, std::uint32_t typeId)
{
	const auto* type = schema.type(typeId);
	return type && (type->kind == TypeKind::Scalar || type->kind == TypeKind::Enum);
}

// A struct on its own row: its members by name, the first four, then "…".
QString structSummary(const rt::RuntimeSchema& schema, const rt::DynamicValueView& view)
{
	const auto members = schema.members(view.typeId());
	QStringList shown;
	for (std::uint32_t i = 0; i < members.size(); ++i) {
		if (shown.size() == 4) {
			shown << QStringLiteral("…");
			break;
		}
		const auto name = schema_text::text(members[i].name);
		const auto member = view.member(i);
		if (!member) {
			shown << name + QStringLiteral(" ?");
			continue;
		}
		const auto* type = schema.type(members[i].typeId);
		shown << name + QLatin1Char(' ') +
		             (leafType(schema, members[i].typeId) ? schema_text::valueText(schema, member.value).text
		              : type && type->kind == TypeKind::Array ? QStringLiteral("[…]")
		                                                       : QStringLiteral("{…}"));
	}
	return QLatin1Char('{') + shown.join(QStringLiteral(", ")) + QLatin1Char('}');
}

// An array of plain values on its own row: the first eight, then "…".
QString arrayPreview(const rt::RuntimeSchema& schema, const rt::DynamicValueView& view)
{
	const auto* type = schema.type(view.typeId());
	if (!type || !leafType(schema, type->relatedTypeId))
		return {};
	QStringList shown;
	for (std::uint32_t i = 0; i < type->elementCount; ++i) {
		if (shown.size() == 8) {
			shown << QStringLiteral("…");
			break;
		}
		const auto element = view.index(i);
		shown << (element ? schema_text::valueText(schema, element.value).text : QStringLiteral("?"));
	}
	return QLatin1Char('[') + shown.join(QStringLiteral(", ")) + QLatin1Char(']');
}

// Writes one snapshot's values into the rows made so far; records what changed.
struct Writer {
	const rt::RuntimeSchema& schema;
	bool first = false;
	std::vector<const Node*>& touched;
	std::vector<Node*>& marked;
	int changed = 0;

	void set(Node& node, const QString& text, std::uint8_t json, const QString& jsonText = {})
	{
		node.json = json;
		node.jsonText = jsonText;
		if (node.value == text)
			return;
		node.value = text;
		touched.push_back(&node);
		if (first)
			return;
		if (!node.changed) {
			node.changed = true;
			marked.push_back(&node);
		}
		// A value, or a struct or array still folded: one change the user can see.
		if (node.total == 0 || node.children.empty())
			++changed;
	}

	void clear(Node& node)
	{
		set(node, {}, 0);
		for (auto& child : node.children)
			clear(*child);
	}

	// An array's elements made so far, through its ranges.
	void fillElements(Node& under, const rt::DynamicValueView& array)
	{
		for (auto& child : under.children) {
			if (child->kind == QStringLiteral("range"))
				fillElements(*child, array);
			else if (const auto element = array.index(child->slot))
				fill(*child, element.value);
			else
				set(*child, QStringLiteral("?"), 0);
		}
	}

	void fill(Node& node, const rt::DynamicValueView& view)
	{
		const auto* type = schema.type(view.typeId());
		if (!type) {
			set(node, QStringLiteral("?"), 0);
			return;
		}
		switch (type->kind) {
			case TypeKind::Struct:
				set(node, structSummary(schema, view), 0);
				for (auto& child : node.children) {
					if (const auto member = view.member(child->slot))
						fill(*child, member.value);
					else
						set(*child, QStringLiteral("?"), 0);
				}
				return;
			case TypeKind::Array:
				set(node, arrayPreview(schema, view), 0);
				fillElements(node, view);
				return;
			default: {
				const auto value = schema_text::valueText(schema, view);
				set(node, value.text, static_cast<std::uint8_t>(value.json), value.jsonText);
				return;
			}
		}
	}
};

QVariantMap region(qint64 offset, qint64 length, const QString& name, const QString& path, const QString& kind,
                   int depth)
{
	return {{QStringLiteral("offset"), offset}, {QStringLiteral("length"), length}, {QStringLiteral("name"), name},
	        {QStringLiteral("path"), path},     {QStringLiteral("kind"), kind},     {QStringLiteral("depth"), depth}};
}

QString problemText(v3::ValuesProblem problem)
{
	return schema_text::text(v3::describe(problem));
}

// A name or value as one cell: a tab or line break in it cannot split a row.
QString cell(QString text)
{
	text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
	text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
	text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
	return text;
}

QJsonValue jsonOfText(const QString& text, schema_text::Json kind, const QString& typeText)
{
	switch (kind) {
		case schema_text::Json::Number: {
			// A 64-bit integer is text: a JSON number would lose its low digits.
			if (typeText == QStringLiteral("u64") || typeText == QStringLiteral("i64"))
				return text;
			bool ok = false;
			const double number = text.toDouble(&ok);
			return ok ? QJsonValue(number) : QJsonValue(text);
		}
		case schema_text::Json::Boolean:
			return text == QStringLiteral("true");
		default:
			return text;
	}
}

QJsonValue jsonValue(const Node& node)
{
	return jsonOfText(node.jsonText.isEmpty() ? node.value : node.jsonText, static_cast<schema_text::Json>(node.json),
	                  node.type);
}

// A value straight from the data - for a row shown whole, opened or not.
// Every value written takes one row of the budget.
struct Data {
	const rt::RuntimeSchema& schema;
	schema_text::RowBudget& budget;

	// A range of an array's elements, each as its own row would write it.
	QString linesRange(const rt::DynamicValueView& array, std::uint32_t first, std::uint32_t count, int depth) const
	{
		QString out;
		for (auto i = first; i < first + count && !budget.cut; ++i)
			if (const auto element = array.index(i))
				out += lines(element.value, QStringLiteral("[%1]").arg(i), depth);
		return out;
	}

	void jsonRange(QJsonArray& out, const rt::DynamicValueView& array, std::uint32_t first, std::uint32_t count) const
	{
		for (auto i = first; i < first + count && !budget.cut; ++i)
			if (const auto element = array.index(i))
				if (const auto value = json(element.value); !budget.cut)
					out.push_back(value);
	}

	QString tsvRange(const rt::DynamicValueView& array, const QString& path, std::uint32_t first, std::uint32_t count) const
	{
		QString out;
		for (auto i = first; i < first + count && !budget.cut; ++i)
			if (const auto element = array.index(i))
				out += tsv(element.value, path + QStringLiteral("[%1]").arg(i));
		return out;
	}

	schema_text::Value leaf(const rt::DynamicValueView& view) const
	{
		return schema_text::valueText(schema, view);
	}

	QString lines(const rt::DynamicValueView& view, const QString& name, int depth) const
	{
		const auto* type = schema.type(view.typeId());
		const auto indent = QString(depth * 2, QLatin1Char(' '));
		if (!budget.take())
			return {};
		if (!type)
			return indent + cell(name) + QStringLiteral(" = ?\n");
		if (type->kind == TypeKind::Array && leafType(schema, type->relatedTypeId)) {
			QStringList values;
			for (std::uint32_t i = 0; i < type->elementCount && budget.take(); ++i) {
				const auto element = view.index(i);
				values << (element ? cell(leaf(element.value).text) : QStringLiteral("?"));
			}
			return indent + cell(name) + QStringLiteral(" = [") + values.join(QStringLiteral(", ")) + QStringLiteral("]\n");
		}
		if (type->kind == TypeKind::Struct) {
			QString out = indent + cell(name) + QLatin1Char('\n');
			const auto members = schema.members(view.typeId());
			for (std::uint32_t i = 0; i < members.size() && !budget.cut; ++i)
				if (const auto member = view.member(i))
					out += lines(member.value, schema_text::text(members[i].name), depth + 1);
			return out;
		}
		if (type->kind == TypeKind::Array) {
			QString out = indent + cell(name) + QLatin1Char('\n');
			for (std::uint32_t i = 0; i < type->elementCount && !budget.cut; ++i)
				if (const auto element = view.index(i))
					out += lines(element.value, QStringLiteral("[%1]").arg(i), depth + 1);
			return out;
		}
		return indent + cell(name) + QStringLiteral(" = ") + cell(leaf(view).text) + QLatin1Char('\n');
	}

	QJsonValue json(const rt::DynamicValueView& view) const
	{
		const auto* type = schema.type(view.typeId());
		// Past the bound: undefined, which a JSON object leaves out (null would read as unavailable).
		if (!budget.take())
			return QJsonValue(QJsonValue::Undefined);
		if (!type)
			return QStringLiteral("?");
		if (type->kind == TypeKind::Struct) {
			QJsonObject object;
			const auto members = schema.members(view.typeId());
			for (std::uint32_t i = 0; i < members.size() && !budget.cut; ++i)
				if (const auto member = view.member(i))
					object.insert(schema_text::text(members[i].name), json(member.value));
			return object;
		}
		if (type->kind == TypeKind::Array) {
			QJsonArray array;
			for (std::uint32_t i = 0; i < type->elementCount && !budget.cut; ++i)
				if (const auto element = view.index(i))
					if (const auto value = json(element.value); !budget.cut)
						array.push_back(value);
			return array;
		}
		const auto value = leaf(view);
		return jsonOfText(value.jsonText.isEmpty() ? value.text : value.jsonText, value.json,
		                  schema_text::typeText(schema, view.typeId()));
	}

	QString tsv(const rt::DynamicValueView& view, const QString& path) const
	{
		const auto* type = schema.type(view.typeId());
		if (!type)
			return budget.take() ? cell(path) + QStringLiteral("\t?\t?\n") : QString{};
		QString out;
		if (type->kind == TypeKind::Struct) {
			const auto members = schema.members(view.typeId());
			for (std::uint32_t i = 0; i < members.size() && !budget.cut; ++i)
				if (const auto member = view.member(i))
					out += tsv(member.value, path + QLatin1Char('.') + schema_text::text(members[i].name));
			return out;
		}
		if (type->kind == TypeKind::Array) {
			for (std::uint32_t i = 0; i < type->elementCount && !budget.cut; ++i)
				if (const auto element = view.index(i))
					out += tsv(element.value, path + QStringLiteral("[%1]").arg(i));
			return out;
		}
		if (!budget.take())
			return {};
		return cell(path) + QLatin1Char('\t') + cell(leaf(view).text) + QLatin1Char('\t') +
		       cell(schema_text::typeText(schema, view.typeId())) + QLatin1Char('\n');
	}
};
} // namespace

ValuesContent::ValuesContent(const QByteArray& bytes, const QByteArray& descriptor, QObject* parent)
    : QObject(parent), original_(bytes), tree_(this),
      shown_(ValuesTreeModel::FindRole, ValuesTreeModel::PathRole, this)
{
	shown_.setSourceModel(&tree_);
	tree_.populate = [this](Node& node) { populate(node); };
	const auto input = bytesOf(bytes);
	regions_ = headerRegions(input);
	v3::ValuesHeader header;
	const auto problem = v3::readValuesHeader(input, header);
	if (problem == v3::ValuesProblem::Short || problem == v3::ValuesProblem::Magic ||
	    problem == v3::ValuesProblem::Version) {
		error_ = problemText(problem);
		return;
	}
	fingerprint_ = header.fingerprint;
	fingerprintGroups_ = groupsOf(header.fingerprint);
	if (descriptor.isEmpty()) {
		needsDescriptor_ = true;
		return;
	}
	error_ = adopt(descriptor);
}

ValuesContent::~ValuesContent() = default;

QString ValuesContent::adopt(const QByteArray& descriptor)
{
	auto parsed = std::make_unique<Schema>();
	parsed->bytes = descriptor;
	const auto descriptorBytes = bytesOf(parsed->bytes);
	rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
	const auto required = rt::RuntimeSchema::parse(descriptorBytes, sizing);
	if (!required && required.status != rt::Status::Capacity)
		return tr("its descriptor is not one this program reads");
	parsed->types.resize(required.requirements.types);
	parsed->members.resize(required.requirements.members);
	parsed->enums.resize(required.requirements.enumEntries);
	parsed->catalogs.resize(required.requirements.catalogs);
	parsed->endpoints.resize(required.requirements.endpoints);
	parsed->workspace = std::make_unique<rt::RuntimeSchemaWorkspace>(
	    parsed->types, parsed->members, parsed->enums, parsed->catalogs, parsed->endpoints);
	const auto result = rt::RuntimeSchema::parse(descriptorBytes, *parsed->workspace);
	if (!result)
		return tr("its descriptor is not one this program reads");
	parsed->schema = result.schema;
	if (parsed->schema.fingerprint() != fingerprint_)
		return tr("That descriptor has fingerprint %1; these values need %2.")
		    .arg(groupsOf(parsed->schema.fingerprint()), fingerprintGroups_);
	const auto& schema = parsed->schema;

	// Groups and their Fields, as the descriptor orders them - the file's order.
	// What is below a Field waits for its row to be opened.
	auto root = std::make_unique<Node>();
	std::vector<Node*> fieldNodes, groupNodes;
	int groups = 0;
	const auto fields = schema.endpoints(descriptor::Category::Field);
	for (const auto& catalog : schema.catalogs(descriptor::Category::Field)) {
		++groups;
		auto groupNode = std::make_unique<Node>();
		groupNode->name = schema_text::text(catalog.name);
		groupNode->kind = QStringLiteral("group");
		groupNode->path = groupNode->name;
		groupNode->count = static_cast<int>(catalog.endpointCount);
		auto& group = attach(*root, std::move(groupNode));
		groupNodes.push_back(&group);
		for (const auto& endpoint : fields.subspan(catalog.firstEndpoint, catalog.endpointCount)) {
			auto fieldNode = std::make_unique<Node>();
			fieldNode->name = schema_text::text(endpoint.name);
			fieldNode->kind = QStringLiteral("field");
			fieldNode->path = group.path + QLatin1Char('.') + fieldNode->name;
			fieldNode->type = schema_text::typeText(schema, endpoint.typeId);
			fieldNode->typeId = endpoint.typeId;
			fieldNode->total = childCount(schema, endpoint.typeId);
			fieldNode->field = static_cast<int>(fieldNodes.size());
			fieldNodes.push_back(&attach(group, std::move(fieldNode)));
		}
	}

	// Every token's place is fixed by the descriptor: the hex view's regions.
	const auto input = bytesOf(original_);
	auto regions = headerRegions(input);
	std::size_t position = 0;
	const auto layout = v3::readValues(input, schema, [&](const v3::ValuesEntry& entry) {
		const auto& path = fieldNodes[position++]->path;
		const auto offset = static_cast<qint64>(entry.offset);
		const auto width = static_cast<qint64>(entry.payload.size());
		regions.push_back(region(offset, 1 + width, path, path, QStringLiteral("token"), 0));
		regions.push_back(region(offset, 1, tr("status"), tr("%1 › status").arg(path), QStringLiteral("field"), 1));
		if (width > 0)
			regions.push_back(region(offset + 1, width, tr("value"), tr("%1 › value").arg(path),
			                         QStringLiteral("field"), 1));
	});
	if (layout != v3::ValuesProblem::None)
		return problemText(layout);

	schema_ = std::move(parsed);
	fieldNodes_ = std::move(fieldNodes);
	groupNodes_ = std::move(groupNodes);
	marked_.clear();
	fieldCount_ = static_cast<int>(fields.size());
	groupCount_ = groups;
	regions_ = regions;
	last_.clear();
	tree_.setRoot(std::move(root));
	if (const auto shown = show(original_); !shown.isEmpty())
		return shown;
	valid_ = true;
	needsDescriptor_ = false;
	error_.clear();
	snapshots_ = 1;
	Q_EMIT shownChanged();
	Q_EMIT snapshotsChanged();
	Q_EMIT regionsChanged();
	return {};
}

void ValuesContent::setConnectedFingerprint(quint64 fingerprint)
{
	connectedGroups_ = groupsOf(fingerprint);
}

QString ValuesContent::useDescriptor(const QUrl& file)
{
	if (valid_)
		return {};
	const auto path = file.isLocalFile() ? file.toLocalFile() : file.toString();
	QFile reading(path);
	// A descriptor is small; anything far larger is not one.
	if (!reading.open(QIODevice::ReadOnly) || reading.size() > 16 * 1024 * 1024)
		return tr("Could not read %1.").arg(QDir::toNativeSeparators(path));
	return useDescriptorBytes(reading.readAll());
}

QString ValuesContent::useDescriptorBytes(const QByteArray& descriptor)
{
	if (valid_)
		return {};
	return adopt(descriptor);
}

QString ValuesContent::show(const QByteArray& bytes)
{
	const auto& schema = schema_->schema;
	std::vector<v3::ValuesEntry> entries;
	entries.reserve(fieldNodes_.size());
	const auto problem = v3::readValues(bytesOf(bytes), schema,
	                                    [&](const v3::ValuesEntry& entry) { entries.push_back(entry); });
	if (problem != v3::ValuesProblem::None)
		return tr("not a snapshot of this descriptor: %1").arg(problemText(problem));

	std::vector<const Node*> touched;
	// The previous snapshot's marks go; this one's are set as values differ.
	for (auto* node : marked_) {
		node->changed = false;
		touched.push_back(node);
	}
	marked_.clear();
	const bool first = last_.isEmpty();
	Writer writer{schema, first, touched, marked_};
	// A Field whose bytes differ from the snapshot shown before: marked even
	// when its folded summary reads the same, and counted on its group's row.
	const auto& before = schema_->entries;
	const auto differs = [&](std::size_t i) {
		if (first || before.size() != entries.size())
			return false;
		const auto& now = entries[i];
		const auto& then = before[i];
		return now.status != then.status || now.payload.size() != then.payload.size() ||
		       !std::equal(now.payload.begin(), now.payload.end(), then.payload.begin());
	};
	std::vector<int> changedInGroup(static_cast<std::size_t>(groupCount_), 0);
	int unavailable = 0;
	for (std::size_t i = 0; i < entries.size(); ++i) {
		auto& field = *fieldNodes_[i];
		const auto counted = writer.changed;
		const bool missing = entries[i].status == v3::ValueStatus::Unavailable;
		if (field.unavailable != missing) {
			field.unavailable = missing;
			touched.push_back(&field);
		}
		if (missing) {
			++unavailable;
			for (auto& child : field.children)
				writer.clear(*child);
			writer.set(field, tr("unavailable"), 0);
		} else if (const auto view = rt::DynamicValueView::create(schema, entries[i].endpoint->typeId, entries[i].payload)) {
			writer.fill(field, view.value);
		} else {
			writer.set(field, QStringLiteral("?"), 0);
		}
		if (differs(i)) {
			if (field.parent && field.parent->row >= 0 && field.parent->row < groupCount_)
				++changedInGroup[static_cast<std::size_t>(field.parent->row)];
			if (!field.changed) {
				field.changed = true;
				marked_.push_back(&field);
				touched.push_back(&field);
			}
			// Counted once when no row made below it counted the change: it
			// lies in a row not made, or past a folded summary.
			if (writer.changed == counted)
				++writer.changed;
		}
	}
	for (auto* group : groupNodes_) {
		const auto count = changedInGroup[static_cast<std::size_t>(group->row)];
		if (group->changedFields != count) {
			group->changedFields = count;
			touched.push_back(group);
		}
	}
	// The tokens point into these bytes, which the content now keeps.
	schema_->shown = bytes;
	schema_->entries = std::move(entries);
	last_ = bytes;
	shownAt_ = QDateTime::currentDateTime();
	changedCount_ = writer.changed;
	unavailable_ = unavailable;
	tree_.touched(touched);
	Q_EMIT shownChanged();
	return {};
}

bool ValuesContent::viewOf(const Node& node, rt::DynamicValueView& out) const
{
	if (!schema_ || node.field < 0 || static_cast<std::size_t>(node.field) >= schema_->entries.size())
		return false;
	const auto& entry = schema_->entries[static_cast<std::size_t>(node.field)];
	if (entry.status == v3::ValueStatus::Unavailable)
		return false;
	// The way down from the Field: member or element indexes.
	std::vector<std::uint32_t> steps;
	for (const auto* at = &node; at && at->kind != QStringLiteral("field"); at = at->parent)
		if (at->kind != QStringLiteral("range"))
			steps.push_back(at->slot);
	auto view = rt::DynamicValueView::create(schema_->schema, entry.endpoint->typeId, entry.payload);
	if (!view)
		return false;
	auto current = view.value;
	for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
		const auto* type = schema_->schema.type(current.typeId());
		if (!type)
			return false;
		const auto next = type->kind == TypeKind::Struct ? current.member(*step) : current.index(*step);
		if (!next)
			return false;
		current = next.value;
	}
	out = current;
	return true;
}

void ValuesContent::populate(Node& node)
{
	if (!schema_ || node.total == 0 || !node.children.empty())
		return;
	const auto& schema = schema_->schema;
	// A range of an array: its elements, or the ranges below it.
	if (node.kind == QStringLiteral("range")) {
		makeElements(node, node.first, node.total);
		return;
	}
	const auto* type = schema.type(node.typeId);
	if (!type)
		return;
	if (type->kind == TypeKind::Array) {
		makeElements(node, 0, node.total);
		return;
	}
	std::vector<std::unique_ptr<Node>> rows;
	const auto members = schema.members(node.typeId);
	for (std::uint32_t i = 0; i < members.size(); ++i) {
		auto child = std::make_unique<Node>();
		child->name = schema_text::text(members[i].name);
		child->kind = QStringLiteral("member");
		child->path = node.path + QLatin1Char('.') + child->name;
		child->type = schema_text::typeText(schema, members[i].typeId);
		child->typeId = members[i].typeId;
		child->total = childCount(schema, members[i].typeId);
		child->slot = i;
		child->field = node.field;
		rows.push_back(std::move(child));
	}
	// Filled from the shown snapshot before they are shown: no change to mark.
	rt::DynamicValueView view;
	if (viewOf(node, view)) {
		std::vector<const Node*> ignored;
		std::vector<Node*> noMarks;
		Writer writer{schema, true, ignored, noMarks};
		for (auto& row : rows)
			if (const auto member = view.member(row->slot))
				writer.fill(*row, member.value);
	}
	tree_.append(node, std::move(rows));
}

void ValuesContent::makeElements(Node& under, std::uint32_t first, std::uint32_t count)
{
	const auto& schema = schema_->schema;
	// An array's node or one of its ranges: both name the array's type.
	const auto* type = schema.type(under.typeId);
	if (!type || type->kind != TypeKind::Array)
		return;
	std::vector<std::unique_ptr<Node>> rows;
	// More than one range's worth: ranges, as Eclipse shows a long array.
	if (count > rangeSize) {
		const auto span = rangeSpan(count);
		for (std::uint64_t start = first; start < std::uint64_t{first} + count; start += span) {
			const auto size = static_cast<std::uint32_t>(std::min<std::uint64_t>(span, std::uint64_t{first} + count - start));
			auto range = std::make_unique<Node>();
			// Indexes, written as the elements' own names are: no grouping.
			range->name = tr("[%1 … %2]").arg(start).arg(start + size - 1);
			range->kind = QStringLiteral("range");
			range->type = size == 1 ? tr("1 item") : tr("%L1 items").arg(size);
			range->path = under.path;
			range->typeId = under.typeId;
			range->first = static_cast<std::uint32_t>(start);
			range->slot = range->first;
			range->total = size;
			range->field = under.field;
			rows.push_back(std::move(range));
		}
		tree_.append(under, std::move(rows));
		return;
	}
	// The array's view: a range is no step in the data.
	rt::DynamicValueView view;
	const bool filled = viewOf(under, view);
	std::vector<const Node*> ignored;
	std::vector<Node*> noMarks;
	Writer writer{schema, true, ignored, noMarks};
	const auto elementType = schema_text::typeText(schema, type->relatedTypeId);
	const auto elementCount = childCount(schema, type->relatedTypeId);
	for (auto i = first; i < first + count; ++i) {
		auto child = std::make_unique<Node>();
		child->name = QStringLiteral("[%1]").arg(i);
		child->kind = QStringLiteral("element");
		child->path = under.path + child->name;
		child->type = elementType;
		child->typeId = type->relatedTypeId;
		child->total = elementCount;
		child->slot = i;
		child->field = under.field;
		if (filled)
			if (const auto element = view.index(i))
				writer.fill(*child, element.value);
		rows.push_back(std::move(child));
	}
	tree_.append(under, std::move(rows));
}

Node* ValuesContent::childAt(Node& parent, std::uint32_t slot, std::size_t& made)
{
	Node* at = &parent;
	for (;;) {
		const auto before = at->children.size();
		populate(*at);
		made += at->children.size() - before;
		if (at->children.empty())
			return nullptr;
		const auto base = at->kind == QStringLiteral("range") ? at->first : 0u;
		if (slot < base)
			return nullptr;
		const auto offset = slot - base;
		// Ranges first: every one but the last holds the same number of elements.
		if (at->children.front()->kind == QStringLiteral("range")) {
			const auto index = offset / at->children.front()->total;
			if (index >= at->children.size())
				return nullptr;
			at = at->children[index].get();
			continue;
		}
		return offset < at->children.size() ? at->children[offset].get() : nullptr;
	}
}

QString ValuesContent::update(const QByteArray& bytes)
{
	if (!valid_)
		return error_;
	if (held_) {
		// Checked now, shown on release: a wrong snapshot never waits.
		const auto problem = v3::readValues(bytesOf(bytes), schema_->schema, [](const v3::ValuesEntry&) {});
		if (problem != v3::ValuesProblem::None)
			return tr("not a snapshot of this descriptor: %1").arg(problemText(problem));
		pending_ = bytes;
		++waiting_;
		++snapshots_;
		Q_EMIT snapshotsChanged();
		return {};
	}
	const auto shown = show(bytes);
	if (shown.isEmpty()) {
		++snapshots_;
		Q_EMIT snapshotsChanged();
	}
	return shown;
}

void ValuesContent::setHeld(bool held)
{
	if (held_ == held)
		return;
	held_ = held;
	if (!held_ && !pending_.isEmpty())
		(void)show(std::exchange(pending_, {}));
	waiting_ = 0;
	Q_EMIT heldChanged();
	Q_EMIT snapshotsChanged();
}

void ValuesContent::openMatches(const QString& text)
{
	const auto& schema = schema_->schema;
	const auto hit = [&](const QString& name, const QString& path) {
		return name.contains(text, Qt::CaseInsensitive) || path.contains(text, Qt::CaseInsensitive);
	};
	// The way to every match that is not inside another: steps from its Field.
	struct Route {
		Node* field;
		std::vector<std::uint32_t> steps;
	};
	std::vector<Route> routes;
	// A bound on the search, so a find in a very large file stays quick; and
	// on the matches it opens.
	schema_text::RowBudget budget{maxFindVisits};
	std::vector<std::uint32_t> steps;
	std::function<void(Node*, std::uint32_t, const QString&)> walk = [&](Node* field, std::uint32_t typeId,
	                                                                     const QString& path) {
		const auto* type = schema.type(typeId);
		if (!type || budget.cut)
			return;
		const auto visit = [&](std::uint32_t slot, const QString& name, const QString& childPath, std::uint32_t childType) {
			if (routes.size() >= maxFindMatches)
				budget.cut = true;
			if (budget.cut || !budget.take())
				return;
			steps.push_back(slot);
			if (hit(name, childPath))
				routes.push_back({field, steps});
			else
				walk(field, childType, childPath);
			steps.pop_back();
		};
		if (type->kind == TypeKind::Struct) {
			const auto members = schema.members(typeId);
			for (std::uint32_t i = 0; i < members.size(); ++i) {
				const auto name = schema_text::text(members[i].name);
				visit(i, name, path + QLatin1Char('.') + name, members[i].typeId);
			}
		} else if (type->kind == TypeKind::Array) {
			for (std::uint32_t i = 0; i < type->elementCount; ++i) {
				const auto name = QStringLiteral("[%1]").arg(i);
				visit(i, name, path + name, type->relatedTypeId);
			}
		}
	};
	for (auto* field : fieldNodes_) {
		// A Field that matches is shown with everything below it; nothing to open.
		if (hit(field->name, field->path))
			continue;
		walk(field, field->typeId, field->path);
	}
	findLimited_ = budget.cut;
	// Each route made, row by row - through an array's ranges, which make
	// only the rows on the way. Rows made stay, so their number is bounded too.
	std::size_t made = 0;
	for (const auto& route : routes) {
		if (made >= maxFindRows) {
			findLimited_ = true;
			break;
		}
		Node* at = route.field;
		for (const auto slot : route.steps)
			if (!(at = childAt(*at, slot, made)))
				break;
	}
}

int ValuesContent::setFilter(const QString& text)
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

bool ValuesContent::matches(const QModelIndex& index) const
{
	return shown_.matches(index);
}

const Node* ValuesContent::nodeOf(const QModelIndex& index) const
{
	// Only a shown row: an index of another model, or none, names nothing.
	if (!index.isValid() || index.model() != &shown_)
		return nullptr;
	return tree_.nodeAt(shown_.mapToSource(index));
}

QString ValuesContent::summary() const
{
	if (!valid_)
		return {};
	const auto fields = fieldCount_ == 1 ? tr("1 Field") : tr("%1 Fields").arg(fieldCount_);
	const auto groups = groupCount_ == 1 ? tr("1 group") : tr("%1 groups").arg(groupCount_);
	const auto base = tr("%1 in %2").arg(fields, groups);
	return unavailable_ == 0 ? base : tr("%1 · %2 unavailable").arg(base).arg(unavailable_);
}

bool ValuesContent::names(const QString& devicePath)
{
	return devicePath == QStringLiteral("/telemetry/values.bin");
}

bool ValuesContent::fingerprintOf(const QByteArray& bytes, quint64& fingerprint)
{
	v3::ValuesHeader header;
	const auto problem = v3::readValuesHeader(bytesOf(bytes), header);
	if (problem == v3::ValuesProblem::Short || problem == v3::ValuesProblem::Magic ||
	    problem == v3::ValuesProblem::Version)
		return false;
	fingerprint = header.fingerprint;
	return true;
}

QVariantList ValuesContent::headerRegions(std::span<const std::byte> bytes)
{
	QVariantList regions;
	if (bytes.size() < v3::valuesHeaderBytes)
		return regions;
	regions.push_back(region(0, v3::valuesHeaderBytes, tr("header"), tr("header"), QStringLiteral("header"), 0));
	const struct {
		int offset, length;
		const char* name;
	} fields[] = {{0, 4, "magic"},      {4, 2, "major"},       {6, 2, "minor"},
	              {8, 4, "fieldCount"}, {12, 4, "totalBytes"}, {16, 8, "fingerprint"}};
	for (const auto& field : fields) {
		const auto name = QString::fromLatin1(field.name);
		regions.push_back(
		    region(field.offset, field.length, name, tr("header › %1").arg(name), QStringLiteral("field"), 1));
	}
	return regions;
}

// ---- Copy: the data where a row is shown whole, the shown rows under Find ---

bool ValuesContent::wholeShown(const QModelIndex& index) const
{
	if (shown_.text().isEmpty())
		return true;
	// Find keeps everything below a match: a row at or below one is shown whole.
	for (auto at = index; at.isValid(); at = at.parent())
		if (shown_.matches(at))
			return true;
	return false;
}

// A row shown whole is copied from the data, so what is not opened is not
// lost. A range is no value of its own: it gives its elements, at its
// array's depth.
QString ValuesContent::linesOf(const QModelIndex& index, int depth, bool withSelf, schema_text::RowBudget& budget) const
{
	QString out;
	const auto* at = index.isValid() ? nodeOf(index) : nullptr;
	if (withSelf && at) {
		rt::DynamicValueView view;
		const Data data{schema_->schema, budget};
		const bool whole = wholeShown(index) && viewOf(*at, view);
		if (at->kind == QStringLiteral("range")) {
			if (whole)
				return data.linesRange(view, at->first, at->total, depth);
			for (int row = 0; row < shown_.rowCount(index) && !budget.cut; ++row)
				out += linesOf(shown_.index(row, 0, index), depth, true, budget);
			return out;
		}
		if (whole && !at->unavailable && at->kind != QStringLiteral("group"))
			return data.lines(view, at->name, depth);
		if (!budget.take())
			return {};
		out += QString(depth * 2, QLatin1Char(' ')) + cell(at->name);
		if (at->unavailable || (at->total == 0 && at->kind != QStringLiteral("group")))
			out += QStringLiteral(" = ") + cell(at->value);
		out += QLatin1Char('\n');
	}
	for (int row = 0; row < shown_.rowCount(index) && !budget.cut; ++row)
		out += linesOf(shown_.index(row, 0, index), withSelf ? depth + 1 : depth, true, budget);
	return out;
}

void ValuesContent::appendShown(QJsonArray& array, const QModelIndex& index, schema_text::RowBudget& budget) const
{
	for (int row = 0; row < shown_.rowCount(index) && !budget.cut; ++row) {
		const auto child = shown_.index(row, 0, index);
		if (nodeOf(child)->kind == QStringLiteral("range"))
			appendShown(array, child, budget);
		else if (const auto value = jsonOf(child, budget); !budget.cut)
			array.push_back(value);
	}
}

QJsonValue ValuesContent::jsonOf(const QModelIndex& index, schema_text::RowBudget& budget) const
{
	const auto* at = nodeOf(index);
	// Past the bound: undefined, which a JSON object leaves out (null would read as unavailable).
	if (!at || budget.cut)
		return QJsonValue(QJsonValue::Undefined);
	if (at->unavailable)
		return budget.take() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonValue::Undefined);
	rt::DynamicValueView view;
	const Data data{schema_->schema, budget};
	const bool whole = wholeShown(index) && viewOf(*at, view);
	if (at->kind == QStringLiteral("range")) {
		QJsonArray array;
		if (whole)
			data.jsonRange(array, view, at->first, at->total);
		else
			appendShown(array, index, budget);
		return array;
	}
	if (whole && at->kind != QStringLiteral("group"))
		return data.json(view);
	if (at->total == 0 && at->kind != QStringLiteral("group"))
		return budget.take() ? jsonValue(*at) : QJsonValue(QJsonValue::Undefined);
	const auto* type = schema_->schema.type(at->typeId);
	if (at->kind != QStringLiteral("group") && type && type->kind == TypeKind::Array) {
		QJsonArray array;
		appendShown(array, index, budget);
		return array;
	}
	// A group's Fields, or a struct's members, by name.
	QJsonObject object;
	for (int row = 0; row < shown_.rowCount(index) && !budget.cut; ++row) {
		const auto child = shown_.index(row, 0, index);
		object.insert(nodeOf(child)->name, jsonOf(child, budget));
	}
	return object;
}

QString ValuesContent::tsvOf(const QModelIndex& index, bool withSelf, schema_text::RowBudget& budget) const
{
	const auto* at = index.isValid() ? nodeOf(index) : nullptr;
	if (withSelf && at) {
		rt::DynamicValueView view;
		const Data data{schema_->schema, budget};
		const bool whole = wholeShown(index) && viewOf(*at, view);
		if (at->kind == QStringLiteral("range")) {
			if (whole)
				return data.tsvRange(view, at->path, at->first, at->total);
		} else if (whole && !at->unavailable && at->kind != QStringLiteral("group")) {
			return data.tsv(view, at->path);
		} else if (at->unavailable || (at->total == 0 && at->kind != QStringLiteral("group"))) {
			return budget.take() ? cell(at->path) + QLatin1Char('\t') + cell(at->value) + QLatin1Char('\t') +
			                           cell(at->type) + QLatin1Char('\n')
			                     : QString{};
		}
	}
	QString out;
	for (int row = 0; row < shown_.rowCount(index) && !budget.cut; ++row)
		out += tsvOf(shown_.index(row, 0, index), true, budget);
	return out;
}

QString ValuesContent::pathOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at && at->kind != QStringLiteral("range") ? at->path : QString{};
}

QString ValuesContent::valueOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at || at->kind == QStringLiteral("group") || at->kind == QStringLiteral("range") ||
	    (at->total > 0 && !at->unavailable))
		return {};
	return at->value;
}

QString ValuesContent::rowOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	return cell(at->name) + QLatin1Char('\t') + cell(at->value) + QLatin1Char('\t') + cell(at->type);
}

QString ValuesContent::textOf(const QModelIndex& index, const QString& format) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	schema_text::RowBudget budget{maxCopiedRows};
	if (format == QStringLiteral("json")) {
		QJsonObject object{{at->name, jsonOf(index, budget)}};
		if ((copyCut_ = budget.cut))
			object.insert(QStringLiteral("…"), cutShort());
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	// The note is added after the walk: the walk is what tells whether the bound was reached.
	auto out = format == QStringLiteral("tsv") ? QStringLiteral("path\tvalue\ttype\n") + tsvOf(index, true, budget)
	                                           : linesOf(index, 0, true, budget);
	if ((copyCut_ = budget.cut))
		out += cutShort() + QLatin1Char('\n');
	return out;
}

QString ValuesContent::textOfAll(const QString& format) const
{
	if (!valid_)
		return {};
	schema_text::RowBudget budget{maxCopiedRows};
	if (format == QStringLiteral("json")) {
		QJsonObject object;
		for (int row = 0; row < shown_.rowCount() && !budget.cut; ++row) {
			const auto child = shown_.index(row, 0);
			object.insert(nodeOf(child)->name, jsonOf(child, budget));
		}
		if ((copyCut_ = budget.cut))
			object.insert(QStringLiteral("…"), cutShort());
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
	auto out = format == QStringLiteral("tsv") ? QStringLiteral("path\tvalue\ttype\n") + tsvOf({}, false, budget)
	                                           : linesOf({}, 0, false, budget);
	if ((copyCut_ = budget.cut))
		out += cutShort() + QLatin1Char('\n');
	return out;
}

QString ValuesContent::findLimitText() const
{
	return tr("Find looks through %L1 values, opens %L2 matches and makes %L3 rows at most. Narrow the search to "
	          "find the rest.")
	    .arg(maxFindVisits)
	    .arg(maxFindMatches)
	    .arg(maxFindRows);
}

QString ValuesContent::copyNote() const
{
	return copyCut_ ? tr("Copied the first %L1 rows; the rest was not copied.").arg(maxCopiedRows) : QString{};
}

QString ValuesContent::cutShort()
{
	return tr("… copied the first %L1 rows; the rest was not copied").arg(maxCopiedRows);
}
} // namespace telemetry::ux
