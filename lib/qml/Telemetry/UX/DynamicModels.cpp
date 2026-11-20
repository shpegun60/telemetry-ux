/*
 * @file DynamicModels.cpp
 * @brief Materialize Qt-owned metadata and bounded pages of Inspector nodes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <QtCore/QRegularExpression>
#include "DynamicModels.hpp"
#include "EndpointPreviews.hpp"
#include "QmlTypes.hpp"
#include "SchemaText.hpp"
#include "ScalarEditorMetadata.hpp"
#include "ValueWords.hpp"
#include "WatchLeaves.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtCore/QPointer>
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

namespace telemetry::ux {
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

QString scalarNumberText(const ins::ScalarValue& scalar)
{
	return std::visit([](const auto& number) { return detail::scalarEditorText(number); },
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
			return QStringLiteral("u8");
		case Code::S8:
			return QStringLiteral("i8");
		case Code::U16:
			return QStringLiteral("u16");
		case Code::S16:
			return QStringLiteral("i16");
		case Code::U32:
			return QStringLiteral("u32");
		case Code::S32:
			return QStringLiteral("i32");
		case Code::U64:
			return QStringLiteral("u64");
		case Code::S64:
			return QStringLiteral("i64");
		case Code::F32:
			return QStringLiteral("f32");
		case Code::F64:
			return QStringLiteral("f64");
	}
	return {};
}

struct Row {
	quintptr token = 0;
	Row* parent = nullptr;
	int row = 0;
	QString name, detail, value, type, path, editorText, accessText, responseTypeText;
	// A Command's arguments or a Service's signature: "(bool)", "(f32, f32) -> f32".
	QString signature;
	QString canonicalText;
	QString editorKind = QStringLiteral("text");
	QVariantList valuePath;
	std::uint32_t endpointId = 0, totalChildren = 0;
	// A value array's element count (its children are ranges of 128 when it
	// is longer); and a range row, whose children are the elements
	// [rangeFirst, rangeFirst + rangeCount) of its array.
	std::uint32_t elements = 0;
	bool valueRange = false;
	TypeId typeId = 0, responseTypeId = 0;
	std::uint32_t wireBytes = 0, responseWireBytes = 0;
	std::uint32_t previewFieldId = 0, previewOffset = 0, previewFieldBytes = 0;
	std::uint32_t rangeFirst = 0, rangeCount = 0;
	// The row's values as leaf ordinals of its Field: [leafFirst, leafFirst + leafCount).
	std::uint32_t leafFirst = 0, leafCount = 0;
	// A category's or group's endpoints.
	std::uint32_t count = 0;
	int category = 0;
	bool readable = false, writable = false, endpoint = false, aggregate = false;
	bool previewOwner = false, previewReadable = false, previewChild = false, previewRange = false;
	// A value row's scalar: its code, and whether it is an enum's.
	Code code = Code::Bool;
	bool scalar = false, enumeration = false;
	// A structure-only row's type in words ("struct (3)", "u16[300]"), made
	// from the schema when the rows were; empty on other rows.
	QString label;
	// The Device tree's kind of row (KindRole) and an enum's names (TypeNamesRole).
	QString kind, typeNames;
	ins::NodeHandle handle{};
	std::vector<std::unique_ptr<Row>> children;
};

struct TypeMetadata {
	QString label;
	std::uint32_t wireBytes = 0;
};

constexpr std::uint32_t previewRangeSize = 256;
constexpr std::size_t previewNodeLimit = 4096;

void previewShape(Row& row, const telemetry::runtime::RuntimeSchema& schema)
{
	const auto* type = schema.type(row.typeId);
	row.aggregate = row.previewOwner && type &&
	                (type->kind == Kind::Struct || type->kind == Kind::Array);
	row.totalChildren = !row.aggregate ? 0
	                    : row.previewRange ? row.rangeCount
	                    : type->kind == Kind::Struct ? type->memberCount
	                    : type->elementCount <= previewRangeSize ? type->elementCount
	                    : (type->elementCount - 1) / previewRangeSize + 1;
}

// A Field's or a value's kind by its type: "struct", "array" or "value".
QString valueKind(const telemetry::runtime::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	return !type                        ? QStringLiteral("value")
	       : type->kind == Kind::Struct ? QStringLiteral("struct")
	       : type->kind == Kind::Array  ? QStringLiteral("array")
	                                    : QStringLiteral("value");
}

bool previewEligible(const Row& row)
{
	return row.previewOwner && row.previewReadable && !row.type.isEmpty() &&
	       row.previewFieldBytes <= EndpointPreviews::fieldBytesLimit;
}

// The row in words, for its tooltip and a screen reader - never internal ids:
// "Field · f32 · 4 bytes · read only", "Command · (bool) · 1-byte argument",
// "Service · (f32, f32) -> f32 · 8-byte request".
QString endpointDetail(const Row& row)
{
	const auto bytes = [](std::uint64_t count, const char* one, const char* many) {
		return count == 1 ? QCoreApplication::translate("EndpointModel", one)
		                  : QCoreApplication::translate("EndpointModel", many).arg(count);
	};
	QStringList parts;
	if (row.category == static_cast<int>(Category::Field)) {
		parts << QCoreApplication::translate("EndpointModel", "Field");
		if (!row.type.isEmpty())
			parts << row.type << bytes(row.wireBytes, "1 byte", "%1 bytes");
		parts << (row.readable && row.writable ? QCoreApplication::translate("EndpointModel", "read and write")
		          : row.readable               ? QCoreApplication::translate("EndpointModel", "read only")
		          : row.writable               ? QCoreApplication::translate("EndpointModel", "write only")
		                                       : QCoreApplication::translate("EndpointModel", "no access"));
	} else {
		const bool service = row.category == static_cast<int>(Category::Service);
		parts << (service ? QCoreApplication::translate("EndpointModel", "Service")
		                  : QCoreApplication::translate("EndpointModel", "Command"));
		// Until its types are known a Command says nothing of its arguments.
		// With none, "Command · ()": the section under it says "No arguments."
		if (!row.signature.isEmpty()) {
			parts << row.signature;
			if (row.wireBytes != 0)
				parts << (service ? bytes(row.wireBytes, "1-byte request", "%1-byte request")
				                  : bytes(row.wireBytes, "1-byte argument", "%1-byte argument"));
		}
	}
	return parts.join(QStringLiteral(" \u00b7 "));
}

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
	const WorkbenchView* view = nullptr;
	const ins::SnapshotStore* snapshot = nullptr;
	// A request editor's draft, read and written by its owner.
	const telemetry::integration::workbench::RequestEditor* draft = nullptr;

	template<class F>
	decltype(auto) edit(F&& read) const
	{
		if (view)
			return read(*view);
		return read(*workbench);
	}

	bool requestEditor() const
	{
		if (draft)
			return true;
		return editor && (workbench || view) && edit([](const auto& model) {
			       return model.commandSelected() || model.serviceSelected();
		       });
	}

	explicit operator bool() const noexcept
	{
		return inspector || workbench || view || snapshot || draft;
	}

	template<class F>
	decltype(auto) visit(F&& read) const
	{
		if (view)
			return read(*view);
		if (workbench)
			return read(*workbench);
		return read(*inspector);
	}

	bool isCurrent() const
	{
		if (draft)
			return draft->current();
		if (snapshot)
			return snapshot->isCurrent();
		return *this && visit([](const auto& model) {
			return model.isCurrent();
		});
	}

	auto snapshotInfo() const
	{
		if (draft)
			return draft->snapshotInfo();
		if (snapshot)
			return snapshot->snapshotInfo();
		if (response && (workbench || view))
			return edit([](const auto& model) {
				return model.serviceSnapshotInfo();
			});
		if (requestEditor())
			return edit([](const auto& model) {
				return model.editorSnapshotInfo();
			});
		return visit([](const auto& model) {
			return model.snapshotInfo();
		});
	}

	auto info(const ins::NodeHandle& node) const
	{
		if (draft)
			return draft->info(node);
		if (snapshot)
			return snapshot->info(node);
		if (response && (workbench || view))
			return edit([&](const auto& model) {
				return model.serviceInfo(node);
			});
		if (requestEditor())
			return edit([&](const auto& model) {
				return model.editorInfo(node);
			});
		return visit([&](const auto& model) {
			return model.info(node);
		});
	}

	auto child(const ins::NodeHandle& node, std::uint32_t index) const
	{
		if (draft)
			return draft->child(node, index);
		if (snapshot)
			return snapshot->child(node, index);
		if (response && (workbench || view))
			return edit([&](const auto& model) {
				return model.serviceChild(node, index);
			});
		if (requestEditor())
			return edit([&](const auto& model) {
				return model.editorChild(node, index);
			});
		return visit([&](const auto& model) {
			return model.child(node, index);
		});
	}

	auto root() const
	{
		if (draft)
			return draft->root();
		if (snapshot)
			return snapshot->root();
		if (response && (workbench || view))
			return edit([](const auto& model) {
				return model.serviceRoot();
			});
		if (requestEditor())
			return edit([](const auto& model) {
				return model.editorRoot();
			});
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
		if (draft)
			return true;
		return !response && (workbench || view) && edit([](const auto& model) {
			return model.editing() &&
			       model.phase() != telemetry::integration::workbench::Phase::Executing;
		});
	}

	// The value read, under a Field's draft.
	ins::Result<ins::ScalarValue> readScalar(const ins::NodeHandle& node) const
	{
		if (snapshot || response || !(workbench || view))
			return {ins::ModelStatus::NoSnapshot, {}};
		return visit([&](const auto& model) {
			return model.scalar(node);
		});
	}

	ins::Result<ins::ScalarValue> scalar(const ins::NodeHandle& node) const
	{
		if (draft)
			return draft->scalar(node);
		if (snapshot)
			return snapshot->scalar(node);
		if (response && (workbench || view))
			return edit([&](const auto& model) {
				return model.serviceScalar(node);
			});
		return editing() ? edit([&](const auto& model) {
			return model.editScalar(node);
		})
		                 : visit([&](const auto& model) {
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
	row->typeId = info.value.type;
	row->editorKind = info.value.kind == Kind::Enum ? QStringLiteral("enum")
	                  : info.value.kind == Kind::Scalar && info.value.code == Code::Bool
	                      ? QStringLiteral("bool") : QStringLiteral("text");
	row->name = info.value.arrayElement
	                ? QLatin1Char('[') + integerText(info.value.index) + QLatin1Char(']')
	                : text(info.value.name);
	row->totalChildren = info.value.children;
	row->elements = info.value.children;
	row->aggregate = info.value.kind == Kind::Struct || info.value.kind == Kind::Array;
	// An array longer than a batch shows ranges of 128, each fetched when
	// opened, as a debugger's variables view does.
	if (info.value.kind == Kind::Array && info.value.children > 128)
		row->totalChildren = (info.value.children + 127) / 128;
	if (row->aggregate) {
		row->type =
		    info.value.kind == Kind::Struct ? QStringLiteral("struct") : QStringLiteral("array");
		row->value = integerText(info.value.children) +
		             (info.value.children == 1 ? QStringLiteral(" child") : QStringLiteral(" children"));
	} else if (info.value.kind == Kind::Void) {
		row->type = QStringLiteral("void");
	} else {
		row->scalar = true;
		row->code = info.value.code;
		row->enumeration = info.value.kind == Kind::Enum;
		row->type = info.value.kind == Kind::Enum
		                ? QStringLiteral("enum (") + scalarType(info.value.code) + QLatin1Char(')')
		                : scalarType(info.value.code);
		const auto scalar = inspector.scalar(handle);
		if (!scalar)
			return {};
		row->value = scalarText(scalar.value);
		row->editorText = scalarNumberText(scalar.value);
		row->canonicalText = row->editorText;
	}
	// The type in words; an internal type id says nothing to a person.
	row->detail = row->type;
	return row;
}
} // namespace

struct EndpointModel::Impl {
	Tree tree;
	Reader inspector;
	std::uint64_t admission = 0;
	PresentationOverlay presentation;
	QString fingerprint;
	QPointer<EndpointPreviews> previews;
	QMetaObject::Connection previewChanged, previewSource, previewDestroyed;
	QHash<TypeId, TypeMetadata> types;
	WatchLeaves leaves;
	std::size_t previewNodes = 0;
	QString widestFieldType;
	// A host's schema where no previews give one (setSchemaSource).
	std::function<telemetry::runtime::RuntimeSchema()> schemaSource;

	bool current() const
	{
		return inspector.isCurrent() && inspector.snapshotInfo().admission == admission;
	}

	// The schema the rows under a Field are made from: the previews', else
	// the host's.
	telemetry::runtime::RuntimeSchema typeSchema() const
	{
		if (previews)
			return previews->schema();
		return schemaSource ? schemaSource() : telemetry::runtime::RuntimeSchema{};
	}

	bool matchesSchema(const telemetry::runtime::RuntimeSchema& schema) const
	{
		if (!current() || !schema.valid())
			return false;
		for (const auto category : {Category::Field, Category::Command, Category::Service}) {
			const auto admitted = inspector.endpoints(category);
			const auto supplied = schema.endpoints(category);
			if (admitted.size() != supplied.size() ||
			    (!admitted.empty() && admitted.data() != supplied.data()))
				return false;
		}
		return true;
	}

	bool previewCurrent() const
	{
		return previews && matchesSchema(previews->schema());
	}

	TypeMetadata metadata(const telemetry::runtime::RuntimeSchema& schema, TypeId id)
	{
		const auto found = types.constFind(id);
		if (found != types.cend())
			return found.value();
		const auto* type = schema.type(id);
		const TypeMetadata value{schema_text::typeText(schema, id), type ? type->wireBytes : 0};
		types.insert(id, value);
		return value;
	}

	bool expansionLimited(const Row& row) const
	{
		return row.aggregate && row.children.size() < row.totalChildren &&
		       previewNodes >= previewNodeLimit;
	}
};

EndpointModel::EndpointModel(QObject* parent)
    : QAbstractItemModel(parent), impl_(std::make_unique<Impl>())
{
	// The Device tree shows this model through the module's C++ types (its
	// filter, its glyphs): a host that makes one has them registered.
	registerQmlTypes();
}

EndpointModel::~EndpointModel() = default;

void EndpointModel::clear()
{
	if (changing())
		return;
	impl_->tree.changing = true;
	const QPointer<EndpointModel> alive(this);
	beginResetModel();
	if (!alive)
		return;
	impl_->tree.clear();
	impl_->types.clear();
	impl_->leaves.clear();
	impl_->previewNodes = 0;
	impl_->inspector = {};
	endResetModel();
	if (!alive)
		return;
	impl_->tree.changing = false;
	setWidestFieldType({});
}

QString EndpointModel::widestFieldType() const
{
	return impl_->widestFieldType;
}

void EndpointModel::setWidestFieldType(const QString& type)
{
	if (impl_->widestFieldType == type)
		return;
	impl_->widestFieldType = type;
	Q_EMIT widestFieldTypeChanged();
}

bool EndpointModel::changing() const noexcept
{
	return impl_->tree.changing;
}

void EndpointModel::setPresentation(const PresentationOverlay& overlay, const QString& fingerprint,
                                    bool notify)
{
	impl_->presentation = overlay;
	impl_->fingerprint = fingerprint;
	if (!notify)
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : impl_->tree.rows)
		indices.append(createIndex(row->row, 0, row->token));
	const QPointer<EndpointModel> alive(this);
	for (const auto& index : indices) {
		Q_EMIT dataChanged(index, index, {Qt::DisplayRole, NameRole, DetailRole, UnitRole});
		if (!alive)
			return;
	}
}

void EndpointModel::setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema)
{
	if (changing() || !impl_->matchesSchema(schema))
		return;
	impl_->leaves.reset(schema);
	QVector<QModelIndex> indices;
	QString widest;
	for (auto* row : std::as_const(impl_->tree.rows)) {
		if (!row->endpoint)
			continue;
		const auto value = impl_->metadata(schema, row->typeId);
		row->type = value.label;
		if (row->category == static_cast<int>(Category::Field)) {
			row->kind = valueKind(schema, row->typeId);
			row->typeNames = schema_text::enumNames(schema, row->typeId);
			// A fixed face: the most characters is the widest.
			if (row->type.size() > widest.size())
				widest = row->type;
		}
		row->wireBytes = value.wireBytes;
		if (row->category == static_cast<int>(Category::Service)) {
			const auto response = impl_->metadata(schema, row->responseTypeId);
			row->responseTypeText = response.label;
			row->responseWireBytes = response.wireBytes;
		}
		if (row->category != static_cast<int>(Category::Field))
			row->signature = schema_text::signatureText(schema, row->typeId, row->responseTypeId,
			                                            row->category == static_cast<int>(Category::Service));
		if (row->category == static_cast<int>(Category::Field)) {
			row->previewOwner = true;
			row->previewFieldId = row->endpointId;
			row->previewReadable = row->readable;
			row->previewFieldBytes = row->wireBytes;
			row->leafFirst = 0;
			row->leafCount = impl_->leaves.count(row->typeId);
			previewShape(*row, schema);
		}
		row->detail = endpointDetail(*row);
		indices.append(createIndex(row->row, 0, row->token));
	}
	const auto admission = impl_->admission;
	const QPointer<EndpointModel> alive(this);
	for (const auto& index : indices) {
		if (!alive || !impl_->current() || impl_->admission != admission)
			return;
		if (!impl_->tree.at(this, index))
			continue;
		Q_EMIT dataChanged(index, index, {DetailRole, TypeTextRole, WireBytesRole,
		                                 ResponseTypeTextRole, PreviewEligibleRole,
		                                 PreviewFieldIdRole, PreviewAggregateRole,
		                                 WatchLeafCountRole, KindRole, TypeNamesRole});
	}
	if (alive && impl_->current() && impl_->admission == admission)
		setWidestFieldType(widest);
}

void EndpointModel::setPreviews(EndpointPreviews* previews)
{
	if (impl_->previews == previews)
		return;
	QObject::disconnect(impl_->previewChanged);
	QObject::disconnect(impl_->previewSource);
	QObject::disconnect(impl_->previewDestroyed);
	impl_->previews = previews;
	if (previews) {
		impl_->previewChanged = connect(previews, &EndpointPreviews::changed,
		                                this, &EndpointModel::previewValuesChanged);
		impl_->previewSource = connect(previews, &EndpointPreviews::sourceChanged,
		                               this, &EndpointModel::previewSourceChanged);
		impl_->previewDestroyed = connect(previews, &QObject::destroyed,
		                                  this, &EndpointModel::previewValuesChanged);
	}
	previewSourceChanged();
}

void EndpointModel::previewSourceChanged()
{
	const QPointer<EndpointModel> alive(this);
	if (impl_->previews)
		setSchemaMetadata(impl_->previews->schema());
	if (alive)
		previewValuesChanged();
}

void EndpointModel::previewValuesChanged()
{
	if (changing())
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row->previewOwner)
			indices.append(createIndex(row->row, 0, row->token));
	const QPointer<EndpointModel> alive(this);
	for (const auto& index : indices) {
		if (!alive)
			return;
		if (impl_->tree.at(this, index))
			Q_EMIT dataChanged(index, index, {PreviewValueRole, PreviewStateRole,
			                                 PreviewAgeMsRole, PreviewReasonRole});
	}
}

QVariant EndpointModel::previewDemandField(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	return row && impl_->previewCurrent() && previewEligible(*row)
	           ? QVariant::fromValue(row->previewFieldId) : QVariant{};
}

bool EndpointModel::previewOnlyChange(const QList<int>& roles) const
{
	return !roles.isEmpty() && std::all_of(roles.cbegin(), roles.cend(), [](int role) {
		return role == PreviewValueRole || role == PreviewStateRole ||
		       role == PreviewAgeMsRole || role == PreviewReasonRole;
	});
}

void EndpointModel::rebuild(const Inspector& inspector)
{
	rebuildSource(&inspector, nullptr);
}

void EndpointModel::rebuild(const Workbench& workbench)
{
	rebuildSource(nullptr, &workbench);
}

void EndpointModel::rebuild(const WorkbenchView& view)
{
	rebuildSource(nullptr, nullptr, &view);
}

void EndpointModel::rebuildSource(const Inspector* inspector, const Workbench* workbench,
                                  const WorkbenchView* view)
{
	if (changing())
		return;
	impl_->tree.changing = true;
	const QPointer<EndpointModel> alive(this);
	beginResetModel();
	if (!alive)
		return;
	impl_->tree.clear();
	impl_->types.clear();
	impl_->leaves.clear();
	impl_->previewNodes = 0;
	impl_->inspector = {inspector, workbench, false, false, view};
	impl_->admission = impl_->inspector.snapshotInfo().admission;
	if (impl_->current()) {
		for (const auto category : {Category::Field, Category::Command, Category::Service}) {
			auto categoryRow = std::make_unique<Row>();
			categoryRow->category = static_cast<int>(category);
			categoryRow->kind = QStringLiteral("category");
			categoryRow->name = category == Category::Field     ? QStringLiteral("Fields")
			                    : category == Category::Command ? QStringLiteral("Commands")
			                                                    : QStringLiteral("Services");
			auto* parent = impl_->tree.append(std::move(categoryRow));
			if (!parent)
				break;
			const auto endpoints = impl_->inspector.endpoints(category);
			parent->count = static_cast<std::uint32_t>(endpoints.size());
			for (const auto& catalog : impl_->inspector.catalogs(category)) {
				auto catalogRow = std::make_unique<Row>();
				catalogRow->category = static_cast<int>(category);
				catalogRow->kind = QStringLiteral("group");
				catalogRow->name = text(catalog.name);
				catalogRow->count = catalog.endpointCount;
				const bool one = catalog.endpointCount == 1;
				const auto words = category == Category::Field
				    ? (one ? QCoreApplication::translate("EndpointModel", "1 Field")
				           : QCoreApplication::translate("EndpointModel", "%1 Fields"))
				    : category == Category::Command
				    ? (one ? QCoreApplication::translate("EndpointModel", "1 Command")
				           : QCoreApplication::translate("EndpointModel", "%1 Commands"))
				    : (one ? QCoreApplication::translate("EndpointModel", "1 Service")
				           : QCoreApplication::translate("EndpointModel", "%1 Services"));
				catalogRow->detail = one ? words : words.arg(integerText(catalog.endpointCount));
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
					// A Field's kind follows its type once the schema gives it.
					row->kind = category == Category::Command   ? QStringLiteral("command")
					            : category == Category::Service ? QStringLiteral("service")
					                                            : QStringLiteral("value");
					row->endpointId = endpoint.id;
					row->typeId = endpoint.typeId;
					row->responseTypeId = endpoint.responseTypeId;
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
					row->accessText = category == Category::Command ? QStringLiteral("Execute")
					                  : category == Category::Service ? QStringLiteral("Call")
					                  : row->readable && row->writable ? QStringLiteral("Read / Write")
					                  : row->readable ? QStringLiteral("Read-only")
					                  : row->writable ? QStringLiteral("Write-only")
					                                  : QStringLiteral("Unavailable");
					row->detail = endpointDetail(*row);
					impl_->tree.append(std::move(row), group);
				}
			}
		}
	}
	endResetModel();
	if (!alive)
		return;
	impl_->tree.changing = false;
	setWidestFieldType({});
	if (!alive)
		return;
	// The registry can publish its source before the provider rebuilds rows;
	// a host's own schema stands in for it.
	if (impl_->previews || impl_->schemaSource)
		setSchemaMetadata(impl_->typeSchema());
}

void EndpointModel::setSchemaSource(std::function<telemetry::runtime::RuntimeSchema()> source)
{
	impl_->schemaSource = std::move(source);
	if (!impl_->previews && impl_->schemaSource)
		setSchemaMetadata(impl_->typeSchema());
}

QVariantMap EndpointModel::endpoint(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !row->endpoint || !impl_->current())
		return {};
	const auto unit = row->category == static_cast<int>(Category::Field)
	                      ? impl_->presentation.hints(impl_->fingerprint, row->category,
	                                                  row->endpointId, QStringLiteral("value")).unit
	                      : QString{};
	return {
	    {QStringLiteral("id"), row->endpointId},     {QStringLiteral("category"), row->category},
	    {QStringLiteral("readable"), row->readable}, {QStringLiteral("writable"), row->writable},
	    {QStringLiteral("name"), row->name},         {QStringLiteral("path"), row->path},
	    {QStringLiteral("typeId"), row->typeId},     {QStringLiteral("typeText"), row->type},
	    {QStringLiteral("wireBytes"), row->wireBytes},
	    // Details' line 2: "Field · f32 · 4 bytes · read only".
	    {QStringLiteral("detail"), endpointDetail(*row)},
	    {QStringLiteral("accessText"), row->accessText}, {QStringLiteral("unit"), unit},
	    {QStringLiteral("responseTypeId"), row->responseTypeId},
	    {QStringLiteral("responseTypeText"), row->responseTypeText},
	    {QStringLiteral("responseWireBytes"), row->responseWireBytes}};
}

bool EndpointModel::isGroup(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	return row && impl_->current() && !row->endpoint && !row->previewOwner &&
	       !row->children.empty();
}

bool EndpointModel::fieldWritable(std::uint32_t id) const
{
	if (!impl_->current() || impl_->tree.changing)
		return false;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row && row->endpoint && row->category == 1 && row->endpointId == id)
			return row->writable;
	return false;
}

std::uint32_t EndpointModel::fieldWireBytes(std::uint32_t id) const
{
	if (!impl_->current() || impl_->tree.changing)
		return 0;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row && row->endpoint && row->category == 1 && row->endpointId == id)
			return row->wireBytes;
	return 0;
}

QString EndpointModel::fieldPath(std::uint32_t id) const
{
	if (!impl_->current() || impl_->tree.changing)
		return {};
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row && row->endpoint && row->category == 1 && row->endpointId == id)
			return row->path;
	return {};
}

QVector<EndpointEntry> EndpointModel::endpointList() const
{
	QVector<EndpointEntry> list;
	if (!impl_->current() || impl_->tree.changing)
		return list;
	// Depth first from the roots: the tree's own order. Endpoints sit under
	// their group; a value row below one is not an endpoint and is skipped.
	std::vector<const Row*> stack;
	for (auto at = impl_->tree.roots.crbegin(); at != impl_->tree.roots.crend(); ++at)
		stack.push_back(at->get());
	while (!stack.empty()) {
		const auto* row = stack.back();
		stack.pop_back();
		if (!row)
			continue;
		if (!row->endpoint) {
			for (auto at = row->children.crbegin(); at != row->children.crend(); ++at)
				stack.push_back(at->get());
			continue;
		}
		EndpointEntry entry;
		entry.category = row->category;
		entry.id = row->endpointId;
		entry.group = row->parent ? row->parent->name : QString{};
		entry.name = row->name;
		entry.path = row->path;
		entry.typeText = row->category == static_cast<int>(Category::Field) || row->signature.isEmpty()
		                     ? row->type : row->signature;
		entry.requestBytes = row->category == static_cast<int>(Category::Field) ? 0 : row->wireBytes;
		entry.readable = row->readable;
		entry.writable = row->writable;
		entry.aggregate = row->aggregate;
		list.push_back(std::move(entry));
	}
	return list;
}

QModelIndex EndpointModel::endpointIndex(int category, std::uint32_t id) const
{
	if (!impl_->current() || impl_->tree.changing)
		return {};
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row && row->endpoint && row->category == category && row->endpointId == id)
			return createIndex(row->row, 0, row->token);
	return {};
}

bool EndpointModel::matchesEndpoint(const QModelIndex& index, int category, std::uint32_t id) const
{
	if (!impl_->current() || impl_->tree.changing)
		return false;
	const auto* row = impl_->tree.at(this, index);
	return row && row->endpoint && row->category == category && row->endpointId == id;
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
	const auto hints =
	    row->previewChild
	        ? impl_->presentation.hints(impl_->fingerprint, static_cast<int>(Category::Field),
	                                    row->previewFieldId, QStringLiteral("value"), row->valuePath)
	    : row->endpoint
	        ? impl_->presentation.hints(impl_->fingerprint, row->category, row->endpointId,
	                                    row->category == 1 ? QStringLiteral("value")
	                                                       : QStringLiteral("request"))
	        : PresentationHints{};
	const auto previewInfo = [&]() {
		return row->previewOwner && impl_->previewCurrent()
		           ? impl_->previews->info(row->previewFieldId) : QVariantMap{};
	};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return hints.displayName(row->name);
		case DetailRole:
			return hints.displayDetail(row->name, row->detail);
		case ValueRole:
			return row->value;
		case EndpointIdRole:
			return row->endpointId;
		case CategoryRole:
			return row->category;
		case CountRole:
			return row->count;
		case PathRole:
			// A range row folds part of its array: data.Samples[0 … 255].
			return row->path.isEmpty() ? row->name : row->previewRange ? row->path + row->name : row->path;
		case WatchLeafFirstRole:
			return row->leafFirst;
		case WatchLeafCountRole:
			return row->leafCount;
		case WatchLeafKindRole: {
			const bool values = row->previewOwner && row->aggregate;
			if (!values)
				return QString{};
			switch (impl_->leaves.kind(row->typeId)) {
				case WatchLeaves::Kind::Members:
					return QStringLiteral("members");
				case WatchLeaves::Kind::Elements:
					return QStringLiteral("elements");
				case WatchLeaves::Kind::Values:
					break;
			}
			return QStringLiteral("values");
		}
		case ReadableRole:
			return row->readable;
		case IsEndpointRole:
			return row->endpoint;
		case WritableRole:
			return row->writable;
		case TypeTextRole:
			return row->endpoint && row->category != static_cast<int>(Category::Field) && !row->signature.isEmpty()
			           ? row->signature : row->type;
		case AccessTextRole:
			return row->accessText;
		case UnitRole:
			return row->previewOwner || (row->endpoint && row->category == static_cast<int>(Category::Field))
			           ? hints.unit : QString{};
		case WireBytesRole:
			return row->wireBytes;
		case TypeIdRole:
			return row->typeId;
		case ResponseTypeTextRole:
			return row->responseTypeText;
		case PreviewEligibleRole:
			return previewEligible(*row);
		case PreviewFieldIdRole:
			return row->previewFieldId;
		case PreviewAggregateRole:
			return row->previewOwner && row->aggregate;
		case PreviewChildRole:
			return row->previewChild;
		case PreviewValueRole: {
			if (!row->previewOwner || !impl_->previewCurrent())
				return QString{};
			if (row->previewRange)
				return previewInfo().value(QStringLiteral("hasValue")).toBool()
				           ? tr("%1 elements").arg(row->rangeCount) : QString{};
			const auto value = impl_->previews->value(row->previewFieldId, row->typeId,
			                                           row->previewOffset);
			return value; // UnitRole is separate; never append it twice in a row.
		}
		case PreviewStateRole:
			return !row->previewOwner ? QString{}
			       : impl_->expansionLimited(*row) ? QStringLiteral("Open Details")
			       : !impl_->previews ? QStringLiteral("Not read")
			       : !impl_->previewCurrent() ? QStringLiteral("Stale")
			       : previewInfo().value(QStringLiteral("state")).toString();
		case PreviewAgeMsRole:
			return previewInfo().value(QStringLiteral("ageMs"), qint64{-1});
		case KindRole:
			return row->kind;
		case TypeNamesRole:
			return row->typeNames;
		case PreviewReasonRole:
			return impl_->expansionLimited(*row)
			           ? tr("Tree preview expansion is limited to 4096 rows. Open Details to inspect this value.")
			           : previewInfo().value(QStringLiteral("reason"), QString{});
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
	        {WritableRole, "writable"},     {TypeTextRole, "typeText"},
	        {AccessTextRole, "accessText"}, {UnitRole, "unit"},
	        {WireBytesRole, "wireBytes"},   {TypeIdRole, "typeId"},
	        {ResponseTypeTextRole, "responseTypeText"},
	        {PreviewEligibleRole, "previewEligible"}, {PreviewFieldIdRole, "previewFieldId"},
	        {PreviewAggregateRole, "previewAggregate"}, {PreviewChildRole, "previewChild"},
	        {PreviewValueRole, "previewValue"}, {PreviewStateRole, "previewState"},
	        {PreviewAgeMsRole, "previewAgeMs"}, {PreviewReasonRole, "previewReason"},
	        {CountRole, "count"},           {PathRole, "path"},
	        {WatchLeafFirstRole, "watchLeafFirst"}, {WatchLeafCountRole, "watchLeafCount"},
	        {WatchLeafKindRole, "watchLeafKind"}, {KindRole, "kind"},
	        {TypeNamesRole, "typeNames"}};
}

QString EndpointModel::pathOf(const QModelIndex& index) const
{
	return data(index, PathRole).toString();
}

QModelIndex EndpointModel::indexOfPath(const QString& path)
{
	if (path.isEmpty() || changing() || !impl_->current())
		return {};
	QModelIndex at;
	for (const auto& name : path.split(QLatin1Char('/'))) {
		QModelIndex next;
		for (int row = 0;; ++row) {
			// A Field's values come in batches: fetch while the name is not among them.
			if (row >= rowCount(at)) {
				if (!canFetchMore(at))
					break;
				fetchMore(at);
				if (row >= rowCount(at))
					break;
			}
			const auto child = index(row, 0, at);
			if (data(child, NameRole).toString() == name) {
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

bool EndpointModel::canFetchMore(const QModelIndex& parent) const
{
	const auto* row = impl_->tree.at(this, parent);
	return row && impl_->current() && (impl_->previews || impl_->schemaSource) && row->previewOwner &&
	       row->aggregate && row->children.size() < row->totalChildren &&
	       impl_->previewNodes < previewNodeLimit &&
	       impl_->matchesSchema(impl_->typeSchema());
}

void EndpointModel::fetchMore(const QModelIndex& parent)
{
	if (changing() || !canFetchMore(parent))
		return;
	auto* row = impl_->tree.at(this, parent);
	const auto schema = impl_->typeSchema();
	const auto* type = schema.type(row->typeId);
	if (!type)
		return;
	if (!impl_->leaves.matches(schema))
		impl_->leaves.reset(schema);
	const auto& leaves = impl_->leaves;
	const auto first = row->children.size();
	// Leaves before the first new member: the struct's earlier members.
	std::uint64_t memberLeaf = row->leafFirst;
	if (type->kind == Kind::Struct) {
		const auto members = schema.members(row->typeId);
		for (std::size_t at = 0; at < first && at < members.size(); ++at)
			memberLeaf += leaves.count(members[at].typeId);
	}
	const auto count = (std::min)({std::size_t{128}, std::size_t{row->totalChildren} - first,
	                             previewNodeLimit - impl_->previewNodes});
	std::vector<std::unique_ptr<Row>> pending;
	pending.reserve(count);
	for (std::size_t at = first; at < first + count; ++at) {
		auto child = std::make_unique<Row>();
		child->category = static_cast<int>(Category::Field);
		child->previewOwner = true;
		child->previewReadable = row->previewReadable;
		// A value inside a Field reads and writes as its Field: the tree marks its
		// access on every row of the Field (the owner, 20 November 2026).
		child->readable = row->readable;
		child->writable = row->writable;
		child->previewChild = true;
		child->previewFieldId = row->previewFieldId;
		child->previewFieldBytes = row->previewFieldBytes;
		child->valuePath = row->valuePath;
		std::uint64_t offset = row->previewOffset;
		if (type->kind == Kind::Struct) {
			const auto members = schema.members(row->typeId);
			if (at >= members.size())
				return;
			const auto& member = members[at];
			child->name = text(member.name);
			child->typeId = member.typeId;
			child->valuePath.append(child->name);
			offset += member.wireOffset;
			child->leafFirst = static_cast<std::uint32_t>(memberLeaf);
			child->leafCount = leaves.count(member.typeId);
			memberLeaf += child->leafCount;
		} else if (type->kind == Kind::Array) {
			if (!row->previewRange && type->elementCount > previewRangeSize) {
				const auto rangeFirst = static_cast<std::uint32_t>(at * previewRangeSize);
				child->previewRange = true;
				child->rangeFirst = rangeFirst;
				child->rangeCount = (std::min)(previewRangeSize, type->elementCount - rangeFirst);
				child->typeId = row->typeId;
				child->name = QStringLiteral("[%1 … %2]").arg(rangeFirst)
				                  .arg(rangeFirst + child->rangeCount - 1);
				const auto each = std::uint64_t{leaves.count(type->relatedTypeId)};
				child->leafFirst = static_cast<std::uint32_t>(row->leafFirst + rangeFirst * each);
				child->leafCount = static_cast<std::uint32_t>(child->rangeCount * each);
			} else {
				const auto elementIndex = static_cast<std::uint32_t>(at) +
				                          (row->previewRange ? row->rangeFirst : 0);
				const auto* element = schema.type(type->relatedTypeId);
				if (!element || elementIndex >= type->elementCount)
					return;
				child->name = QStringLiteral("[%1]").arg(elementIndex);
				child->typeId = type->relatedTypeId;
				child->valuePath.append(elementIndex);
				offset += std::uint64_t{elementIndex} * element->wireBytes;
				const auto each = std::uint64_t{leaves.count(type->relatedTypeId)};
				const auto within = elementIndex - (row->previewRange ? row->rangeFirst : 0);
				child->leafFirst = static_cast<std::uint32_t>(row->leafFirst + within * each);
				child->leafCount = static_cast<std::uint32_t>(each);
			}
		} else {
			return;
		}
		if (offset > (std::numeric_limits<std::uint32_t>::max)())
			return;
		child->previewOffset = static_cast<std::uint32_t>(offset);
		const auto metadata = impl_->metadata(schema, child->typeId);
		child->type = metadata.label;
		child->wireBytes = metadata.wireBytes;
		// A range row only folds its elements: it has its array's path, and
		// an element under it reads data.Samples[300].
		child->path = child->previewRange ? row->path
		              : row->path + (type->kind == Kind::Struct ? QStringLiteral(".") : QString{}) +
		                    child->name;
		child->kind = child->previewRange ? QStringLiteral("array") : valueKind(schema, child->typeId);
		child->typeNames = child->previewRange ? QString{} : schema_text::enumNames(schema, child->typeId);
		// In words, as a Field's: its type, then its bytes; never an internal type id.
		child->detail = child->type + QStringLiteral(" \u00b7 ") +
		                (child->wireBytes == 1 ? QCoreApplication::translate("EndpointModel", "1 byte")
		                                       : QCoreApplication::translate("EndpointModel", "%1 bytes")
		                                             .arg(child->wireBytes));
		previewShape(*child, schema);
		pending.push_back(std::move(child));
	}
	if (pending.empty() || impl_->tree.nextToken == 0 ||
	    pending.size() - 1 > (std::numeric_limits<quintptr>::max)() - impl_->tree.nextToken)
		return;
	impl_->tree.changing = true;
	const QPointer<EndpointModel> alive(this);
	beginInsertRows(parent, static_cast<int>(first), static_cast<int>(first + pending.size() - 1));
	if (!alive)
		return;
	for (auto& child : pending) {
		impl_->tree.append(std::move(child), row);
		++impl_->previewNodes;
	}
	endInsertRows();
	if (!alive)
		return;
	impl_->tree.changing = false;
	if (impl_->previewNodes == previewNodeLimit)
		previewValuesChanged();
}

bool EndpointModel::hasChildren(const QModelIndex& parent) const
{
	if (!parent.isValid())
		return !impl_->tree.roots.empty();
	const auto* row = impl_->tree.at(this, parent);
	return row && (!row->children.empty() || canFetchMore(parent));
}

struct ValueModel::Impl {
	Tree tree;
	Reader inspector;
	std::uint64_t admission = 0, revision = 0;
	PresentationOverlay presentation;
	QString fingerprint, role;
	int category = 0;
	quint32 endpointId = 0;
	detail::ScalarEditorMetadata metadata;
	bool offer = false, stale = false;
	QString staleNote;
	int deepestDepth = 0;
	// The values sent last, by path ("" for a scalar root, "factor", "a.b[2]").
	QHash<QString, QString> baseline;
	// A structure-only state (resetShape): Qt-owned rows, always current,
	// nothing to fetch; and which table it is shown in.
	bool shape = false;
	ValueModel::Section shapeSection = ValueModel::Section::Value;
	// The table the rows are shown in when their source does not say (setSection).
	std::optional<ValueModel::Section> sectionOverride;
	// The values shown before the action that just finished (noteUpdate),
	// by path, for UpdatedRole; empty when the rows compare nothing.
	QHash<QString, QString> previous;
	bool updatePending = false;

	// Takes the values noted by noteUpdate() for the rows about to be made;
	// any other reset compares nothing.
	void beginRows()
	{
		if (!std::exchange(updatePending, false))
			previous.clear();
	}

	bool updated(const Row& row) const
	{
		if (!row.scalar || previous.isEmpty())
			return false;
		const auto found = previous.constFind(pathKey(row.valuePath));
		return found != previous.cend() && *found != row.value;
	}

	static QString dash()
	{
		return QString(QChar(0x2013));
	}

	// One row of a type and everything under it, from the schema alone,
	// appended under parent; budget counts the rows still allowed.
	void shapeRows(const telemetry::runtime::RuntimeSchema& schema, TypeId id, QString name, QVariantList path,
	               Row* parent, const QHash<QString, QString>& shown, std::size_t& budget, int depth)
	{
		const auto* type = schema.type(id);
		if (!type || budget == 0)
			return;
		--budget;
		auto made = std::make_unique<Row>();
		made->typeId = id;
		made->name = std::move(name);
		made->valuePath = std::move(path);
		made->label = schema_text::typeText(schema, id);
		made->detail = made->label;
		deepestDepth = (std::max)(deepestDepth, depth);
		if (type->kind == Kind::Struct || type->kind == Kind::Array) {
			made->aggregate = true;
			made->type = type->kind == Kind::Struct ? QStringLiteral("struct") : QStringLiteral("array");
			made->elements = type->kind == Kind::Struct ? type->memberCount : type->elementCount;
		} else if (type->kind == Kind::Void) {
			made->type = QStringLiteral("void");
		} else {
			const auto* underlying = type->kind == Kind::Enum ? schema.type(type->relatedTypeId) : type;
			made->scalar = true;
			made->enumeration = type->kind == Kind::Enum;
			made->code = underlying ? underlying->scalarCode : Code::Bool;
			made->type = made->enumeration ? QStringLiteral("enum (") + scalarType(made->code) + QLatin1Char(')')
			                               : scalarType(made->code);
			made->editorKind = made->enumeration ? QStringLiteral("enum")
			                   : made->code == Code::Bool ? QStringLiteral("bool") : QStringLiteral("text");
			const auto found = shown.constFind(pathKey(made->valuePath));
			made->value = found != shown.cend() ? *found : dash();
		}
		auto* row = tree.append(std::move(made), parent);
		if (!row || !row->aggregate)
			return;
		if (type->kind == Kind::Struct) {
			for (const auto& member : schema.members(id)) {
				auto steps = row->valuePath;
				steps.append(text(member.name));
				shapeRows(schema, member.typeId, text(member.name), std::move(steps), row, shown, budget, depth + 1);
			}
		} else if (row->elements > 128) {
			// As the values show a long array: ranges of 128, then their elements.
			for (std::uint32_t first = 0; first < row->elements && budget > 0; first += 128) {
				--budget;
				auto range = std::make_unique<Row>();
				range->typeId = id;
				range->aggregate = true;
				range->valueRange = true;
				range->type = QStringLiteral("array");
				range->rangeFirst = first;
				range->rangeCount = (std::min)(std::uint32_t{128}, row->elements - first);
				range->elements = range->rangeCount;
				range->name = QStringLiteral("[%1…%2]").arg(first).arg(first + range->rangeCount - 1);
				range->valuePath = row->valuePath;
				range->detail = range->name;
				auto* held = tree.append(std::move(range), row);
				if (!held)
					break;
				for (std::uint32_t at = 0; at < held->rangeCount; ++at) {
					auto steps = row->valuePath;
					steps.append(QVariant::fromValue(first + at));
					shapeRows(schema, type->relatedTypeId, QStringLiteral("[%1]").arg(first + at), std::move(steps),
					          held, shown, budget, depth + 2);
				}
				held->totalChildren = std::uint32_t(held->children.size());
			}
		} else {
			for (std::uint32_t at = 0; at < row->elements; ++at) {
				auto steps = row->valuePath;
				steps.append(QVariant::fromValue(at));
				shapeRows(schema, type->relatedTypeId, QStringLiteral("[%1]").arg(at), std::move(steps), row, shown,
				          budget, depth + 1);
			}
		}
		row->totalChildren = std::uint32_t(row->children.size());
	}

	static QString pathKey(const QVariantList& path)
	{
		QStringList parts;
		for (const auto& step : path)
			parts << (step.typeId() == QMetaType::QString ? step.toString()
			                                              : QLatin1Char('[') + step.toString() + QLatin1Char(']'));
		return parts.join(QLatin1Char('.')).replace(QStringLiteral(".["), QStringLiteral("["));
	}

	// A draft's value against the value read, or a value against the one sent;
	// a branch is unsent when any loaded value under it is.
	bool unsent(const Row& row) const
	{
		return unsentCount(row) > 0;
	}

	int unsentCount(const Row& row) const
	{
		if (row.aggregate) {
			int count = 0;
			for (const auto& child : row.children)
				count += unsentCount(*child);
			return count;
		}
		return unsentLeaf(row) ? 1 : 0;
	}

	bool unsentLeaf(const Row& row) const
	{
		if (!row.scalar || shape)
			return false;
		if (!baseline.isEmpty()) {
			const auto found = baseline.constFind(pathKey(row.valuePath));
			return found != baseline.cend() && *found != row.editorText;
		}
		if (!inspector.editing() || inspector.requestEditor())
			return false;
		const auto read = inspector.readScalar(row.handle);
		return read && scalarNumberText(read.value) != row.editorText;
	}

	// A row takes text: in a draft or a request editor, or a Field's value
	// while its owner offers an edit.
	bool editable(const Row& row) const
	{
		if (shape || row.aggregate || row.type.isEmpty() || row.type == QStringLiteral("void"))
			return false;
		return inspector.editing() ||
		       (offer && !inspector.response && !inspector.snapshot && !inspector.requestEditor());
	}

	bool current() const
	{
		// A structure-only state owns its rows: nothing to go stale under them.
		if (shape)
			return true;
		if (!inspector.isCurrent())
			return false;
		const auto snapshot = inspector.snapshotInfo();
		return snapshot.ready && snapshot.admission == admission && snapshot.revision == revision;
	}

	// How far below a node the deepest value lies: every member of a struct,
	// the first element of an array (the rest have its shape).
	int depthBelow(const ins::NodeHandle& handle, int guard) const
	{
		if (guard <= 0)
			return 0;
		const auto info = inspector.info(handle);
		if (!info || info.value.children == 0 ||
		    (info.value.kind != Kind::Struct && info.value.kind != Kind::Array))
			return 0;
		const std::uint32_t count = info.value.kind == Kind::Array ? 1 : info.value.children;
		int deepest = info.value.kind == Kind::Array && info.value.children > 128 ? 1 : 0;
		for (std::uint32_t at = 0; at < count; ++at) {
			const auto child = inspector.child(handle, at);
			if (child)
				deepest = (std::max)(deepest, depthBelow(child.value, guard - 1) + 1);
		}
		return deepest;
	}

	// A scalar as a summary shows it: an enum by its name only.
	QString leafSummary(const ins::NodeHandle& handle, const ins::NodeInfo& info) const
	{
		const auto scalar = inspector.scalar(handle);
		if (!scalar)
			return QStringLiteral("?");
		if (info.kind == Kind::Enum)
			return scalar.value.enumKnown && !scalar.value.enumName.empty() ? text(scalar.value.enumName)
			                                                              : scalarNumberText(scalar.value);
		return scalarText(scalar.value);
	}

	// A structure-only row's summary: from the rows themselves; none while
	// no value under it is known, so a folded branch reads "–".
	bool shapeKnown(const Row& row) const
	{
		if (!row.aggregate)
			return row.scalar && row.value != dash();
		return std::any_of(row.children.cbegin(), row.children.cend(),
		                   [this](const auto& child) { return shapeKnown(*child); });
	}

	QString summaryKind(const Row& row) const
	{
		if (!row.aggregate)
			return {};
		if (shape && !shapeKnown(row))
			return {};
		if (row.type == QStringLiteral("struct"))
			return QStringLiteral("struct");
		if (row.elements == 0)
			return QStringLiteral("array");
		if (shape) {
			const auto* first = row.children.empty() ? nullptr : row.children.front().get();
			return first && first->aggregate && !first->valueRange ? QStringLiteral("arrayOfAggregates")
			                                                       : QStringLiteral("array");
		}
		const auto first = inspector.child(row.handle, row.valueRange ? row.rangeFirst : 0);
		if (!first)
			return QStringLiteral("array");
		const auto info = inspector.info(first.value);
		return info && (info.value.kind == Kind::Struct || info.value.kind == Kind::Array)
		           ? QStringLiteral("arrayOfAggregates") : QStringLiteral("array");
	}

	QStringList summaryParts(const Row& row) const
	{
		QStringList parts;
		const auto kind = summaryKind(row);
		if (kind.isEmpty() || kind == QStringLiteral("arrayOfAggregates"))
			return parts;
		if (shape) {
			// A long array's elements lie under its first range.
			const auto* held = &row;
			if (!row.children.empty() && row.children.front()->valueRange)
				held = row.children.front().get();
			for (const auto& child : held->children) {
				if (parts.size() == 8)
					break;
				const auto shown = child->aggregate ? (child->type == QStringLiteral("struct") ? QStringLiteral("{…}")
				                                                                                : QStringLiteral("[…]"))
				                                    : child->value;
				parts << (kind == QStringLiteral("struct") ? child->name + QLatin1Char(' ') + shown : shown);
			}
			return parts;
		}
		const auto count = (std::min)(row.elements, std::uint32_t{8});
		const auto base = row.valueRange ? row.rangeFirst : 0;
		for (std::uint32_t at = 0; at < count; ++at) {
			const auto child = inspector.child(row.handle, base + at);
			if (!child)
				break;
			const auto info = inspector.info(child.value);
			if (!info)
				break;
			const auto shown = info.value.kind == Kind::Struct ? QStringLiteral("{…}")
			                   : info.value.kind == Kind::Array ? QStringLiteral("[…]")
			                                                    : leafSummary(child.value, info.value);
			parts << (kind == QStringLiteral("struct") ? text(info.value.name) + QLatin1Char(' ') + shown : shown);
		}
		return parts;
	}

	bool metadataCurrent() const
	{
		// A draft's owner sets the schema's metadata before each draft it shows.
		if (inspector.draft)
			return inspector.isCurrent();
		if (!inspector || inspector.snapshot || !inspector.isCurrent())
			return false;
		detail::ScalarEditorMetadata::EndpointIdentity identity{};
		std::size_t n = 0;
		for (const auto category : {Category::Field, Category::Command, Category::Service}) {
			const auto endpoints = inspector.endpoints(category);
			identity[n++] = {reinterpret_cast<quintptr>(endpoints.data()), endpoints.size()};
		}
		return metadata.matches(identity);
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
	const bool wasStale = impl_->stale;
	const bool wasShape = impl_->shape;
	beginResetModel();
	impl_->tree.clear();
	impl_->inspector = {};
	impl_->baseline.clear();
	impl_->stale = false;
	impl_->staleNote.clear();
	impl_->shape = false;
	// What noteUpdate() kept waits for the rows it was noted for.
	if (!impl_->updatePending)
		impl_->previous.clear();
	endResetModel();
	if (wasStale)
		Q_EMIT staleChanged();
	if (wasShape)
		Q_EMIT shapeChanged();
	if (impl_->deepestDepth != 0) {
		impl_->deepestDepth = 0;
		Q_EMIT deepestChanged();
	}
}

bool ValueModel::changing() const noexcept
{
	return impl_->tree.changing;
}

void ValueModel::setPresentation(const PresentationOverlay& overlay, const QString& fingerprint,
                                 int category, quint32 id, const QString& role, bool notify)
{
	impl_->presentation = overlay;
	impl_->fingerprint = fingerprint;
	impl_->category = category;
	impl_->endpointId = id;
	impl_->role = role;
	if (!notify)
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : impl_->tree.rows)
		indices.append(createIndex(row->row, 0, row->token));
	const QPointer<ValueModel> alive(this);
	for (const auto& index : indices) {
		Q_EMIT dataChanged(index, index, {Qt::DisplayRole, NameRole, DetailRole, ValueRole});
		if (!alive)
			return;
	}
}

void ValueModel::setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema)
{
	if (changing())
		return;
	impl_->metadata.reset(schema);
	QVector<QModelIndex> indices;
	for (const auto* row : std::as_const(impl_->tree.rows))
		indices.append(createIndex(row->row, 0, row->token));
	const QPointer<ValueModel> alive(this);
	for (const auto& index : indices) {
		if (!alive)
			return;
		if (impl_->tree.at(this, index))
			Q_EMIT dataChanged(index, index, {TypeRole, TypeTextRole, EditorKindRole,
			                                 EditorChoicesRole, EditorChoicesTruncatedRole});
	}
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

void ValueModel::resetSnapshot(const WorkbenchView& view)
{
	resetSource(nullptr, nullptr, false, &view);
}

void ValueModel::resetSnapshot(const ins::SnapshotStore& snapshot)
{
	resetSource(nullptr, nullptr, false, nullptr, &snapshot);
}

void ValueModel::resetDraft(const telemetry::integration::workbench::RequestEditor& editor)
{
	resetSource(nullptr, nullptr, false, nullptr, nullptr, &editor);
}

void ValueModel::resetServiceResponse(const WorkbenchView& view)
{
	resetSource(nullptr, nullptr, true, &view);
}

void ValueModel::resetSource(const Inspector* inspector, const Workbench* workbench, bool response,
                             const WorkbenchView* view, const ins::SnapshotStore* snapshotSource,
                             const telemetry::integration::workbench::RequestEditor* draft)
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	const bool wasStale = impl_->stale;
	const bool wasShape = impl_->shape;
	beginResetModel();
	impl_->tree.clear();
	impl_->baseline.clear();
	impl_->stale = false;
	impl_->staleNote.clear();
	impl_->shape = false;
	impl_->beginRows();
	impl_->inspector = {inspector, workbench, !response, response, view, snapshotSource, draft};
	const auto snapshot = impl_->inspector.snapshotInfo();
	impl_->admission = snapshot.admission;
	impl_->revision = snapshot.revision;
	if (impl_->current()) {
		const auto root = impl_->inspector.root();
		if (root) {
			auto row = valueRow(impl_->inspector, root.value);
			if (row) {
				const auto section = impl_->sectionOverride ? *impl_->sectionOverride
				                     : response                        ? Section::Response
				                     : impl_->inspector.requestEditor() ? Section::Request
				                                                       : Section::Value;
				if (section == Section::Response)
					row->name = QStringLiteral("Response");
				else if (section == Section::Request)
					row->name = QStringLiteral("Request");
				impl_->tree.append(std::move(row));
			}
		}
	}
	endResetModel();
	if (wasStale)
		Q_EMIT staleChanged();
	if (wasShape)
		Q_EMIT shapeChanged();
	int deepest = 0;
	if (!impl_->tree.roots.empty() && impl_->current())
		deepest = impl_->depthBelow(impl_->tree.roots.front()->handle, 32);
	if (deepest != impl_->deepestDepth) {
		impl_->deepestDepth = deepest;
		Q_EMIT deepestChanged();
	}
}

void ValueModel::resetShape(const telemetry::runtime::RuntimeSchema& schema, TypeId type, Section section,
                            const QHash<QString, QString>& shown)
{
	if (changing())
		return;
	const ChangeGuard guard{impl_->tree.changing};
	const bool wasStale = impl_->stale;
	const bool wasShape = impl_->shape;
	const int deepestBefore = impl_->deepestDepth;
	beginResetModel();
	impl_->tree.clear();
	impl_->baseline.clear();
	impl_->stale = false;
	impl_->staleNote.clear();
	impl_->beginRows();
	impl_->inspector = {};
	impl_->shape = true;
	impl_->shapeSection = section;
	impl_->deepestDepth = 0;
	if (const auto* root = schema.type(type); root && root->kind != Kind::Void) {
		std::size_t budget = shapeRowLimit;
		// The root row is named as the live rows name it.
		const auto name = section == Section::Response  ? QStringLiteral("Response")
		                  : section == Section::Request ? QStringLiteral("Request")
		                                                : QString{};
		impl_->shapeRows(schema, type, name, {}, nullptr, shown, budget, 0);
	}
	endResetModel();
	if (wasStale)
		Q_EMIT staleChanged();
	if (!wasShape)
		Q_EMIT shapeChanged();
	if (deepestBefore != impl_->deepestDepth)
		Q_EMIT deepestChanged();
}

bool ValueModel::shape() const noexcept
{
	return impl_->shape;
}

QHash<QString, QString> ValueModel::shownTexts()
{
	// What a table shows at once: the root's own rows.
	if (impl_->tree.roots.size() == 1 && !changing()) {
		const auto top = createIndex(0, 0, impl_->tree.roots.front()->token);
		if (canFetchMore(top))
			fetchMore(top);
	}
	// The rows hold their texts: what showed stays readable after the
	// snapshot under them was replaced.
	QHash<QString, QString> out;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row->scalar && row->value != Impl::dash())
			out.insert(Impl::pathKey(row->valuePath), row->value);
	return out;
}

void ValueModel::noteUpdate()
{
	impl_->previous = shownTexts();
	impl_->updatePending = true;
}

void ValueModel::dropUpdate()
{
	if (std::exchange(impl_->updatePending, false))
		impl_->previous.clear();
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
	const auto hints = impl_->presentation.hints(impl_->fingerprint, impl_->category,
	                                             impl_->endpointId, impl_->role, row->valuePath);
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return hints.displayName(row->name);
		case DetailRole:
			return hints.displayDetail(row->name, row->detail);
		case ValueRole:
			// "–" stands for no value, never with a unit after it.
			return row->aggregate || (impl_->shape && row->value == Impl::dash()) ? row->value
			                                                                       : hints.displayValue(row->value);
		case UpdatedRole:
			return impl_->updated(*row);
		case TypeRole:
		case TypeTextRole:
			if (row->valueRange)
				return QString{};
			if (const auto* type = impl_->metadataCurrent() ? impl_->metadata.type(row->typeId) : nullptr)
				return type->label;
			return row->label.isEmpty() ? row->type : row->label;
		case AggregateRole:
			return row->aggregate;
		case ChildCountRole:
			return row->totalChildren;
		case ItemCountRole:
			return row->elements;
		case LoadedCountRole:
			return static_cast<int>(row->children.size());
		case EditableRole:
			return impl_->editable(*row);
		case UnsentRole:
			return impl_->unsent(*row);
		case UnsentCountRole:
			return impl_->unsentCount(*row);
		case PathRole:
			return Impl::pathKey(row->valuePath) + (row->valueRange ? row->name : QString{});
		case SummaryKindRole:
			return impl_->summaryKind(*row);
		case SummaryPartsRole:
			return impl_->summaryParts(*row);
		case StaleRole:
			return impl_->stale;
		case StaleNoteRole:
			return impl_->staleNote;
		case TakesRole: {
			if (!row->scalar)
				return QString{};
			QStringList names;
			if (const auto* type = row->enumeration && impl_->metadataCurrent() ? impl_->metadata.type(row->typeId) : nullptr)
				for (const auto& choice : type->choices) {
					const auto label = choice.toMap().value(QStringLiteral("label")).toString();
					const auto open = label.lastIndexOf(QStringLiteral(" ("));
					names << (open > 0 ? label.left(open) : label);
				}
			return value_words::takes(int(row->code), row->enumeration, names);
		}
		case EditorTextRole:
			return row->editorText;
		case CanonicalTextRole:
			return row->canonicalText;
		case EditorKindRole:
			return row->editorKind == QStringLiteral("enum") &&
			               (!impl_->metadataCurrent() || !impl_->metadata.type(row->typeId))
			           ? QStringLiteral("text") : row->editorKind;
		case EditorChoicesRole:
			if (row->editorKind == QStringLiteral("bool")) return detail::booleanEditorChoices();
			if (const auto* type = impl_->metadataCurrent() ? impl_->metadata.type(row->typeId) : nullptr)
				return type->choices;
			return QVariantList{};
		case EditorChoicesTruncatedRole:
			if (const auto* type = impl_->metadataCurrent() ? impl_->metadata.type(row->typeId) : nullptr)
				return type->truncated;
			return false;
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
	        {ItemCountRole, "itemCount"},
	        {UnsentCountRole, "unsentCount"},
	        {LoadedCountRole, "loadedCount"},
	        {EditableRole, "editable"},
	        {EditorTextRole, "editorText"},
	        {EditorKindRole, "editorKind"},
	        {EditorChoicesRole, "editorChoices"},
	        {EditorChoicesTruncatedRole, "editorChoicesTruncated"},
	        {CanonicalTextRole, "canonicalText"},
	        {TypeTextRole, "typeText"},
	        {TakesRole, "takes"},
	        {UnsentRole, "unsent"},
	        {StaleRole, "stale"},
	        {StaleNoteRole, "staleNote"},
	        {PathRole, "path"},
	        {SummaryPartsRole, "summaryParts"},
	        {SummaryKindRole, "summaryKind"},
	        {UpdatedRole, "updated"}};
}

QHash<QString, QString> ValueModel::texts() const
{
	QHash<QString, QString> out;
	// A structure-only state holds no editor's texts.
	if (!impl_->current() || impl_->shape)
		return out;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row->scalar)
			out.insert(Impl::pathKey(row->valuePath), row->editorText);
	return out;
}

void ValueModel::setBaseline(const QHash<QString, QString>& sent)
{
	impl_->baseline = sent;
	if (changing())
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (row->scalar || row->aggregate)
			indices.append(createIndex(row->row, 0, row->token));
	const QPointer<ValueModel> alive(this);
	for (const auto& index : indices) {
		if (!alive)
			return;
		if (impl_->tree.at(this, index))
			Q_EMIT dataChanged(index, index, {UnsentRole, UnsentCountRole, SummaryPartsRole});
	}
}

void ValueModel::clearBaseline()
{
	impl_->baseline.clear();
}

bool ValueModel::hasBaseline() const noexcept
{
	return !impl_->baseline.isEmpty();
}

QString ValueModel::baselineLine() const
{
	QStringList parts;
	const auto* roots = impl_->tree.children(this, {});
	const std::vector<std::unique_ptr<Row>>* top = roots;
	// A struct's members, not its root row.
	if (roots && roots->size() == 1 && (*roots)[0]->aggregate)
		top = &(*roots)[0]->children;
	if (!top)
		return {};
	for (const auto& row : *top) {
		const auto found = impl_->baseline.constFind(Impl::pathKey(row->valuePath));
		if (found == impl_->baseline.cend())
			continue;
		parts << (row->valuePath.isEmpty() ? *found : row->name + QLatin1Char('=') + *found);
		if (parts.size() == 4)
			break;
	}
	return parts.join(QLatin1Char(' '));
}

int ValueModel::unsentCount() const
{
	if (!impl_->current())
		return 0;
	int count = 0;
	for (const auto* row : std::as_const(impl_->tree.rows))
		count += impl_->unsent(*row) ? 1 : 0;
	return count;
}

void ValueModel::setStale(bool stale, const QString& note)
{
	if (impl_->stale == stale && impl_->staleNote == note)
		return;
	impl_->stale = stale;
	impl_->staleNote = note;
	Q_EMIT staleChanged();
	if (changing())
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : std::as_const(impl_->tree.rows))
		indices.append(createIndex(row->row, 0, row->token));
	const QPointer<ValueModel> alive(this);
	for (const auto& index : indices) {
		if (!alive)
			return;
		if (impl_->tree.at(this, index))
			Q_EMIT dataChanged(index, index, {StaleRole, StaleNoteRole});
	}
}

bool ValueModel::stale() const noexcept
{
	return impl_->stale;
}

QString ValueModel::staleNote() const
{
	return impl_->staleNote;
}

QModelIndex ValueModel::contentRoot() const
{
	if (impl_->tree.roots.size() != 1 || !impl_->tree.roots.front()->aggregate || !impl_->current())
		return {};
	return createIndex(0, 0, impl_->tree.roots.front()->token);
}

QString ValueModel::pathOf(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	return row ? Impl::pathKey(row->valuePath) + (row->valueRange ? row->name : QString{}) : QString{};
}

bool ValueModel::sectioned() const
{
	if (impl_->shape)
		return impl_->shapeSection != Section::Value;
	if (impl_->sectionOverride)
		return *impl_->sectionOverride != Section::Value;
	return impl_->inspector.requestEditor() || impl_->inspector.response;
}

void ValueModel::setSection(std::optional<Section> section)
{
	impl_->sectionOverride = section;
}

int ValueModel::deepest() const noexcept
{
	return impl_->deepestDepth;
}

QModelIndex ValueModel::indexOfPath(const QString& path)
{
	if (impl_->tree.roots.empty() || !impl_->current())
		return {};
	auto* row = impl_->tree.roots.front().get();
	static const QRegularExpression step(QStringLiteral("([^.\\[\\]]+)|(\\[[^\\]]+\\])"));
	auto steps = step.globalMatch(path);
	while (steps.hasNext()) {
		const auto found = steps.next();
		const auto name = found.captured(found.capturedLength(1) > 0 ? 1 : 2);
		Row* match = nullptr;
		// An element of a long array lies under its range.
		if (row->aggregate && !row->valueRange && row->type == QStringLiteral("array") && row->elements > 128 &&
		    !name.contains(QChar(0x2026))) {
			bool whole = false;
			const auto index = name.mid(1, name.size() - 2).toUInt(&whole);
			if (!whole || index >= row->elements)
				return {};
			const auto first = index / 128 * 128;
			const auto rangeName = QStringLiteral("[%1…%2]").arg(first).arg((std::min)(first + 127, row->elements - 1));
			for (;;) {
				for (const auto& child : row->children)
					if (child->name == rangeName)
						row = child.get();
				if (row->valueRange)
					break;
				const auto here = createIndex(row->row, 0, row->token);
				const auto before = row->children.size();
				if (!canFetchMore(here))
					return {};
				fetchMore(here);
				if (row->children.size() == before)
					return {};
			}
		}
		for (;;) {
			for (const auto& child : row->children)
				if (child->name == name) {
					match = child.get();
					break;
				}
			if (match)
				break;
			const auto here = createIndex(row->row, 0, row->token);
			const auto before = row->children.size();
			if (!canFetchMore(here))
				break;
			fetchMore(here);
			if (row->children.size() == before)
				break;
		}
		if (!match)
			return {};
		row = match;
	}
	return createIndex(row->row, 0, row->token);
}

void ValueModel::setEditOffer(bool offered)
{
	if (impl_->offer == offered)
		return;
	impl_->offer = offered;
	// While the rows change a reset follows, and data() reads the offer then.
	if (changing())
		return;
	QVector<QModelIndex> indices;
	for (const auto* row : std::as_const(impl_->tree.rows))
		if (!row->aggregate)
			indices.append(createIndex(row->row, 0, row->token));
	const QPointer<ValueModel> alive(this);
	for (const auto& index : indices) {
		if (!alive)
			return;
		if (impl_->tree.at(this, index))
			Q_EMIT dataChanged(index, index, {EditableRole});
	}
}

bool ValueModel::editOffer() const noexcept
{
	return impl_->offer;
}

ins::NodeResult ValueModel::node(const QModelIndex& index) const
{
	const auto* row = impl_->tree.at(this, index);
	if (!row || !impl_->current() || impl_->shape)
		return {ins::ModelStatus::Stale, {}};
	const auto checked = impl_->inspector.info(row->handle);
	return checked ? ins::NodeResult{ins::ModelStatus::Ok, row->handle}
	               : ins::NodeResult{checked.status, {}};
}

void ValueModel::updateScalar(const QModelIndex& index)
{
	if (changing() || !impl_->current() || impl_->shape)
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
	row->canonicalText = row->editorText;
	Q_EMIT dataChanged(index, index, {ValueRole, EditorTextRole, CanonicalTextRole, UnsentRole});
	// A folded branch shows what is under it: its summary and its mark.
	const QPointer<ValueModel> alive(this);
	for (const auto* up = row->parent; up && alive; up = up->parent) {
		const auto ancestor = createIndex(up->row, 0, up->token);
		Q_EMIT dataChanged(ancestor, ancestor, {UnsentRole, UnsentCountRole, SummaryPartsRole});
	}
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
	// A long array's children are its ranges; nothing is read from the
	// device's values until a range is opened.
	if (row->aggregate && !row->valueRange && row->type == QStringLiteral("array") && row->elements > 128) {
		for (auto at = first; at < last; ++at) {
			auto range = std::make_unique<Row>();
			range->handle = row->handle;
			range->typeId = row->typeId;
			range->aggregate = true;
			range->valueRange = true;
			range->type = QStringLiteral("array");
			range->rangeFirst = static_cast<std::uint32_t>(at * 128);
			range->rangeCount = (std::min)(std::uint32_t{128}, row->elements - range->rangeFirst);
			range->totalChildren = range->elements = range->rangeCount;
			range->name = QStringLiteral("[%1…%2]").arg(range->rangeFirst).arg(range->rangeFirst + range->rangeCount - 1);
			range->valuePath = row->valuePath;
			range->detail = range->name;
			pending.push_back(std::move(range));
		}
		if (pending.empty() || impl_->tree.nextToken == 0)
			return;
		beginInsertRows(parent, static_cast<int>(first), static_cast<int>(last - 1));
		for (auto& child : pending)
			impl_->tree.append(std::move(child), row);
		endInsertRows();
		Q_EMIT dataChanged(parent, parent, {LoadedCountRole});
		return;
	}
	for (auto at = first; at < last; ++at) {
		const auto handle = impl_->inspector.child(row->handle, static_cast<std::uint32_t>(at + (row->valueRange ? row->rangeFirst : 0)));
		if (!handle)
			return;
		auto child = valueRow(impl_->inspector, handle.value);
		if (!child)
			return;
		child->valuePath = row->valuePath;
		const auto info = impl_->inspector.info(handle.value);
		if (!info)
			return;
		child->valuePath.append(info.value.arrayElement ? QVariant::fromValue(info.value.index)
		                                                : QVariant(text(info.value.name)));
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

} // namespace telemetry::ux
