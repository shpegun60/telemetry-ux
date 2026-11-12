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
#include <cmath>
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

void ValuesTreeModel::touched(const std::vector<const Node*>& nodes)
{
	static const QList<int> roles{ValueRole, ChangedRole, UnavailableRole, NumericRole};
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
		case PathRole:
			return at->path;
		case NumericRole:
			// Numbers and true/false: right-aligned, so a struct's members line up.
			return (at->json == 1 && at->jsonText.isEmpty()) || at->json == 2;
		default:
			return {};
	}
}

QHash<int, QByteArray> ValuesTreeModel::roleNames() const
{
	return {{NameRole, "name"},   {KindRole, "kind"},         {TypeRole, "typeText"},
	        {ValueRole, "value"}, {UnavailableRole, "unavailable"}, {ChangedRole, "changed"},
	        {CountRole, "count"}, {PathRole, "path"},           {NumericRole, "numeric"}};
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

Node& append(Node& under, std::unique_ptr<Node> child)
{
	child->parent = &under;
	child->row = static_cast<int>(under.children.size());
	under.children.push_back(std::move(child));
	return *under.children.back();
}

// A Field's value below its node: struct members by name, array elements by index.
void addValue(const rt::RuntimeSchema& schema, TypeId id, Node& under, int depth)
{
	const auto* type = schema.type(id);
	if (!type || depth > 32)
		return;
	if (type->kind == TypeKind::Struct) {
		for (const auto& member : schema.members(id)) {
			auto child = std::make_unique<Node>();
			child->name = schema_text::text(member.name);
			child->kind = QStringLiteral("member");
			child->path = under.path + QLatin1Char('.') + child->name;
			child->type = schema_text::typeText(schema, member.typeId);
			addValue(schema, member.typeId, append(under, std::move(child)), depth + 1);
		}
	} else if (type->kind == TypeKind::Array) {
		for (std::uint32_t i = 0; i < type->elementCount; ++i) {
			auto child = std::make_unique<Node>();
			child->name = QStringLiteral("[%1]").arg(i);
			child->kind = QStringLiteral("element");
			child->path = under.path + child->name;
			child->type = schema_text::typeText(schema, type->relatedTypeId);
			addValue(schema, type->relatedTypeId, append(under, std::move(child)), depth + 1);
		}
	}
}

// A struct on its own row: its plain members by name, the first four, then "…".
QString summary(const Node& structure)
{
	QStringList shown;
	for (const auto& member : structure.children) {
		if (shown.size() == 4) {
			shown << QStringLiteral("…");
			break;
		}
		shown << member->name + QLatin1Char(' ') + (member->children.empty() ? member->value : QStringLiteral("{…}"));
	}
	return QLatin1Char('{') + shown.join(QStringLiteral(", ")) + QLatin1Char('}');
}

// An array of plain values on its own row: the first eight, then "…".
QString preview(const Node& array)
{
	QStringList shown;
	for (const auto& element : array.children) {
		if (!element->children.empty())
			return {};
		if (shown.size() == 8) {
			shown << QStringLiteral("…");
			break;
		}
		shown << element->value;
	}
	return QLatin1Char('[') + shown.join(QStringLiteral(", ")) + QLatin1Char(']');
}

// Writes one snapshot's values into the nodes; records what changed.
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
		if (node.children.empty())
			++changed;
	}

	void clear(Node& node)
	{
		set(node, {}, 0);
		for (auto& child : node.children)
			clear(*child);
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
				for (std::uint32_t i = 0; i < node.children.size(); ++i) {
					if (const auto member = view.member(i))
						fill(*node.children[i], member.value);
					else
						set(*node.children[i], QStringLiteral("?"), 0);
				}
				set(node, summary(node), 0);
				return;
			case TypeKind::Array:
				for (std::uint32_t i = 0; i < node.children.size(); ++i) {
					if (const auto element = view.index(i))
						fill(*node.children[i], element.value);
					else
						set(*node.children[i], QStringLiteral("?"), 0);
				}
				set(node, preview(node), 0);
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
} // namespace

ValuesContent::ValuesContent(const QByteArray& bytes, const QByteArray& descriptor, QObject* parent)
    : QObject(parent), original_(bytes), tree_(this),
      shown_(ValuesTreeModel::NameRole, ValuesTreeModel::PathRole, this)
{
	shown_.setSourceModel(&tree_);
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
	auto root = std::make_unique<Node>();
	std::vector<Node*> fieldNodes;
	int groups = 0;
	const auto fields = schema.endpoints(descriptor::Category::Field);
	for (const auto& catalog : schema.catalogs(descriptor::Category::Field)) {
		++groups;
		auto groupNode = std::make_unique<Node>();
		groupNode->name = schema_text::text(catalog.name);
		groupNode->kind = QStringLiteral("group");
		groupNode->path = groupNode->name;
		groupNode->count = static_cast<int>(catalog.endpointCount);
		auto& group = append(*root, std::move(groupNode));
		for (const auto& endpoint : fields.subspan(catalog.firstEndpoint, catalog.endpointCount)) {
			auto fieldNode = std::make_unique<Node>();
			fieldNode->name = schema_text::text(endpoint.name);
			fieldNode->kind = QStringLiteral("field");
			fieldNode->path = group.path + QLatin1Char('.') + fieldNode->name;
			fieldNode->type = schema_text::typeText(schema, endpoint.typeId);
			auto& field = append(group, std::move(fieldNode));
			addValue(schema, endpoint.typeId, field, 0);
			fieldNodes.push_back(&field);
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
	const auto problem = adopt(descriptor);
	if (!problem.isEmpty())
		return problem;
	return {};
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
	Writer writer{schema, last_.isEmpty(), touched, marked_};
	int unavailable = 0;
	for (std::size_t i = 0; i < entries.size(); ++i) {
		auto& field = *fieldNodes_[i];
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
			continue;
		}
		const auto view = rt::DynamicValueView::create(schema, entries[i].endpoint->typeId, entries[i].payload);
		if (view)
			writer.fill(field, view.value);
		else
			writer.set(field, QStringLiteral("?"), 0);
	}
	last_ = bytes;
	shownAt_ = QDateTime::currentDateTime();
	changedCount_ = writer.changed;
	unavailable_ = unavailable;
	tree_.touched(touched);
	Q_EMIT shownChanged();
	return {};
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

int ValuesContent::setFilter(const QString& text)
{
	return valid_ ? shown_.setText(text) : 0;
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

namespace {
// A name or value as one cell: a tab or line break in it cannot split a row.
QString cell(QString text)
{
	text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
	text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
	text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
	text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
	return text;
}

QJsonValue jsonValue(const Node& node)
{
	const auto& text = node.jsonText.isEmpty() ? node.value : node.jsonText;
	switch (node.json) {
		case 1: {
			// A 64-bit integer is text: a JSON number would lose its low digits.
			if (node.type == QStringLiteral("u64") || node.type == QStringLiteral("i64"))
				return text;
			bool ok = false;
			const double number = text.toDouble(&ok);
			return ok ? QJsonValue(number) : QJsonValue(text);
		}
		case 2:
			return text == QStringLiteral("true");
		default:
			return text;
	}
}

// The shown rows below an index, with the node each stands for.
struct Walk {
	const QAbstractItemModel& model;
	std::function<const Node*(const QModelIndex&)> node;

	// An array whose shown elements are plain values: written whole on its line.
	bool plainArray(const QModelIndex& index) const
	{
		const auto* at = node(index);
		const int rows = model.rowCount(index);
		if (!at || at->kind == QStringLiteral("group") || rows == 0 || at->unavailable)
			return false;
		for (int row = 0; row < rows; ++row) {
			const auto* child = node(model.index(row, 0, index));
			if (child->kind != QStringLiteral("element") || !child->children.empty())
				return false;
		}
		return true;
	}

	QString wholeArray(const QModelIndex& index) const
	{
		QStringList values;
		for (int row = 0; row < model.rowCount(index); ++row)
			values << cell(node(model.index(row, 0, index))->value);
		return QLatin1Char('[') + values.join(QStringLiteral(", ")) + QLatin1Char(']');
	}

	QString lines(const QModelIndex& index, int depth, bool withSelf) const
	{
		QString out;
		const bool plain = withSelf && plainArray(index);
		if (withSelf) {
			const auto* at = node(index);
			out += QString(depth * 2, QLatin1Char(' ')) + cell(at->name);
			if (plain)
				out += QStringLiteral(" = ") + wholeArray(index);
			else if (at->unavailable || (at->children.empty() && at->kind != QStringLiteral("group")))
				out += QStringLiteral(" = ") + cell(at->value);
			out += QLatin1Char('\n');
		}
		if (plain)
			return out;
		for (int row = 0; row < model.rowCount(index); ++row)
			out += lines(model.index(row, 0, index), withSelf ? depth + 1 : depth, true);
		return out;
	}

	QJsonValue json(const QModelIndex& index) const
	{
		const auto* at = node(index);
		if (at->unavailable)
			return QJsonValue::Null;
		const int rows = model.rowCount(index);
		if (at->children.empty())
			return at->kind == QStringLiteral("group") ? QJsonValue(QJsonObject{}) : jsonValue(*at);
		bool elements = rows > 0;
		for (int row = 0; row < rows; ++row)
			elements = elements && node(model.index(row, 0, index))->kind == QStringLiteral("element");
		if (elements) {
			QJsonArray array;
			for (int row = 0; row < rows; ++row)
				array.push_back(json(model.index(row, 0, index)));
			return array;
		}
		QJsonObject object;
		for (int row = 0; row < rows; ++row) {
			const auto child = model.index(row, 0, index);
			object.insert(node(child)->name, json(child));
		}
		return object;
	}

	QString tsv(const QModelIndex& index, bool withSelf) const
	{
		QString out;
		if (withSelf) {
			const auto* at = node(index);
			if (at->unavailable || (at->children.empty() && at->kind != QStringLiteral("group"))) {
				out += cell(at->path) + QLatin1Char('\t') + cell(at->value) + QLatin1Char('\t') + cell(at->type) +
				       QLatin1Char('\n');
				return out;
			}
		}
		for (int row = 0; row < model.rowCount(index); ++row)
			out += tsv(model.index(row, 0, index), true);
		return out;
	}
};
} // namespace

QString ValuesContent::pathOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at ? at->path : QString{};
}

QString ValuesContent::valueOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at || at->kind == QStringLiteral("group") || (!at->children.empty() && !at->unavailable))
		return {};
	return at->value;
}

QString ValuesContent::rowOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at ? cell(at->name) + QLatin1Char('\t') + cell(at->value) + QLatin1Char('\t') + cell(at->type)
	          : QString{};
}

QString ValuesContent::textOf(const QModelIndex& index, const QString& format) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	const Walk walk{shown_, [this](const QModelIndex& shown) { return nodeOf(shown); }};
	if (format == QStringLiteral("json"))
		return QString::fromUtf8(
		    QJsonDocument(QJsonObject{{at->name, walk.json(index)}}).toJson(QJsonDocument::Indented));
	if (format == QStringLiteral("tsv"))
		return QStringLiteral("path\tvalue\ttype\n") + walk.tsv(index, true);
	return walk.lines(index, 0, true);
}

QString ValuesContent::textOfAll(const QString& format) const
{
	if (!valid_)
		return {};
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
		return QStringLiteral("path\tvalue\ttype\n") + walk.tsv({}, false);
	return walk.lines({}, 0, false);
}
} // namespace telemetry::ux
