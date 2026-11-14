/*
 * @file WatchlistModel.cpp
 * @brief Materialize exact scalar watch values as bounded Qt-owned rows.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "WatchlistModel.hpp"
#include "ScalarEditorMetadata.hpp"
#include <QtCore/QSet>
#include <array>
#include <algorithm>
#include <charconv>
#include <limits>
#include <type_traits>

namespace telemetry::ux {
namespace {
std::optional<ScalarCode> scalarCode(WatchScalar code)
{
	switch (code) {
		case WatchScalar::Unknown: return {};
		case WatchScalar::Boolean: return ScalarCode::Bool;
		case WatchScalar::U8: return ScalarCode::U8;
		case WatchScalar::S8: return ScalarCode::S8;
		case WatchScalar::U16: return ScalarCode::U16;
		case WatchScalar::S16: return ScalarCode::S16;
		case WatchScalar::U32: return ScalarCode::U32;
		case WatchScalar::S32: return ScalarCode::S32;
		case WatchScalar::U64: return ScalarCode::U64;
		case WatchScalar::S64: return ScalarCode::S64;
		case WatchScalar::F32: return ScalarCode::F32;
		case WatchScalar::F64: return ScalarCode::F64;
	}
	return {};
}

QString canonicalText(const WatchValueBits& value)
{
	if (!value.type.valid() || value.size != value.type.width) return {};
	const auto code = scalarCode(value.type.scalar);
	if (!code) return {};
	std::uint64_t bits = 0;
	for (unsigned i = 0; i < value.size; ++i)
		bits |= std::uint64_t(std::to_integer<unsigned char>(value.bytes[i])) << (8 * i);
	return detail::scalarEditorBits(bits, *code);
}

QString fallbackTypeText(const WatchValueType& type)
{
	QString text;
	switch (type.scalar) {
		case WatchScalar::Unknown: return {};
		case WatchScalar::Boolean: text = QStringLiteral("bool"); break;
		case WatchScalar::U8: text = QStringLiteral("u8"); break;
		case WatchScalar::S8: text = QStringLiteral("s8"); break;
		case WatchScalar::U16: text = QStringLiteral("u16"); break;
		case WatchScalar::S16: text = QStringLiteral("s16"); break;
		case WatchScalar::U32: text = QStringLiteral("u32"); break;
		case WatchScalar::S32: text = QStringLiteral("s32"); break;
		case WatchScalar::U64: text = QStringLiteral("u64"); break;
		case WatchScalar::S64: text = QStringLiteral("s64"); break;
		case WatchScalar::F32: text = QStringLiteral("f32"); break;
		case WatchScalar::F64: text = QStringLiteral("f64"); break;
	}
	return type.enumeration ? QStringLiteral("enum ") + text : text;
}

struct Mutation {
	bool& changing;

	explicit Mutation(bool& value) : changing(value)
	{
		changing = true;
	}

	~Mutation()
	{
		changing = false;
	}
};

template<class T>
QString number(T value)
{
	if constexpr (std::is_same_v<T, bool>)
		return value ? QStringLiteral("true (1)") : QStringLiteral("false (0)");
	else if constexpr (std::is_signed_v<T>)
		return QString::number(static_cast<qlonglong>(value));
	else
		return QString::number(static_cast<qulonglong>(value));
}

template<class T>
QString floating(T value, telemetry::runtime::Input bytes)
{
	std::uint64_t bits = 0;
	for (std::size_t i = 0; i < bytes.size(); ++i)
		bits |= std::uint64_t(std::to_integer<unsigned char>(bytes[i])) << (8 * i);
	constexpr unsigned width = sizeof(T) * 8, fraction = sizeof(T) == 4 ? 23 : 52;
	constexpr auto exponent = sizeof(T) == 4 ? UINT64_C(0xff) : UINT64_C(0x7ff);
	const auto magnitude = bits & ((UINT64_C(1) << (width - 1)) - 1);
	const bool negative = (bits >> (width - 1)) != 0;
	const auto hex =
	    QStringLiteral(" (0x%1)").arg(qulonglong(bits), int(sizeof(T) * 2), 16, QLatin1Char('0'));
	if (magnitude == 0)
		return negative ? QStringLiteral("-0") + hex : QStringLiteral("0");
	if ((magnitude >> fraction) == exponent) {
		if ((magnitude & ((UINT64_C(1) << fraction) - 1)) != 0)
			return QStringLiteral("NaN") + hex;
		return negative ? QStringLiteral("-inf") : QStringLiteral("inf");
	}
	std::array<char, 64> buffer{};
	const auto result =
	    std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
	                  std::chars_format::general, std::numeric_limits<T>::max_digits10);
	return result.ec == std::errc{} ? QString::fromLatin1(buffer.data(), result.ptr - buffer.data())
	                                : QString{};
}
} // namespace

WatchlistModel::WatchlistModel(QObject* parent)
    : QAbstractListModel(parent), metadata_(std::make_unique<detail::ScalarEditorMetadata>())
{}

WatchlistModel::~WatchlistModel() = default;

void WatchlistModel::setSchemaMetadata(const telemetry::runtime::RuntimeSchema& schema)
{
	if (changing_) return;
	metadata_->reset(schema);
	if (count_)
		Q_EMIT dataChanged(index(0, 0), index(count_ - 1, 0),
		                   {EditorKindRole, EditorChoicesRole, EditorChoicesTruncatedRole,
		                    TypeTextRole, CategoricalRole});
}

void WatchlistModel::bind(const Workbench& owner) noexcept
{
	if (!changing_) {
		++sampleEpoch_;
		metadata_->reset({});
		current_ = {};
		owner_ = &owner;
		session_ = owner.session();
		admission_ = owner.snapshotInfo().admission;
	}
}

void WatchlistModel::bind(std::function<bool()> current)
{
	if (!changing_) {
		++sampleEpoch_;
		metadata_->reset({});
		owner_ = nullptr;
		current_ = std::move(current);
	}
}

void WatchlistModel::setWritable(std::function<bool(WatchKey)> writable)
{
	writable_ = std::move(writable);
}

void WatchlistModel::setWriteRefusal(std::function<QString(WatchKey)> refusal)
{
	refusal_ = std::move(refusal);
}

WatchlistModel::Row WatchlistModel::makeRow(const WatchEntry& entry)
{
	Row row;
	row.key = entry.key;
	row.token = nextToken_++;
	row.name = entry.name;
	row.member = watch_key::isWhole(entry.key) ? QString{} : entry.member;
	row.type = watch_key::isWhole(entry.key) ? TypeId{0} : entry.type;
	row.status = QStringLiteral("Not read");
	if (!watch_key::isWhole(entry.key)) {
		row.scope = entry.scope;
		row.others = entry.others;
		row.kind = entry.kind;
	}
	row.writable = writable_ && writable_(entry.key);
	row.refusal = !row.writable && refusal_ ? refusal_(entry.key) : QString{};
	return row;
}

bool WatchlistModel::insertable(const QVector<WatchEntry>& entries) const
{
	if (changing_ || entries.isEmpty() || entries.size() > maxRows - count_ || nextToken_ == 0 ||
	    static_cast<quintptr>(entries.size() - 1) > std::numeric_limits<quintptr>::max() - nextToken_)
		return false;
	QSet<WatchKey> seen;
	for (int row = 0; row < count_; ++row)
		seen.insert(rows_[row].key);
	for (const auto& entry : entries) {
		if (entry.name.isEmpty() || seen.contains(entry.key))
			return false;
		seen.insert(entry.key);
	}
	return true;
}

bool WatchlistModel::add(quint32 endpoint, QString name)
{
	return add(WatchEntry::of(watch_key::whole(endpoint), std::move(name)));
}

bool WatchlistModel::add(const WatchEntry& entry)
{
	if (!insertable({entry}))
		return false;
	if (rows_.size() == rows_.capacity())
		rows_.reserve(rows_.capacity() >= maxRows / 2
		                  ? qsizetype(maxRows)
		                  : std::max(qsizetype(4), rows_.capacity() * 2));
	Mutation mutation{changing_};
	beginInsertRows({}, count_, count_);
	rows_.push_back(makeRow(entry));
	++count_;
	++membership_;
	endInsertRows();
	return true;
}

bool WatchlistModel::addMany(const QVector<QPair<quint32, QString>>& entries)
{
	QVector<WatchEntry> whole;
	whole.reserve(entries.size());
	for (const auto& entry : entries)
		whole.push_back(WatchEntry::of(watch_key::whole(entry.first), entry.second));
	return addMany(whole);
}

bool WatchlistModel::addMany(const QVector<WatchEntry>& entries)
{
	if (!insertable(entries))
		return false;
	rows_.reserve(qsizetype(count_) + entries.size());
	const int first = count_;
	const int last = first + static_cast<int>(entries.size()) - 1;
	Mutation mutation{changing_};
	beginInsertRows({}, first, last);
	for (const auto& entry : entries)
		rows_.push_back(makeRow(entry));
	count_ += static_cast<int>(entries.size());
	++membership_;
	endInsertRows();
	return true;
}

bool WatchlistModel::remove(int row)
{
	if (changing_ || row < 0 || row >= count_)
		return false;
	Mutation mutation{changing_};
	beginRemoveRows({}, row, row);
	rows_.removeAt(row);
	--count_;
	++membership_;
	endRemoveRows();
	return true;
}

QVector<WatchlistModel::Removed> WatchlistModel::removeIds(const QSet<WatchKey>& keys)
{
	QVector<Removed> removed;
	if (changing_ || keys.isEmpty())
		return removed;
	for (int row = 0; row < count_; ++row)
		if (keys.contains(rows_[row].key))
			removed.push_back({row, {rows_[row].key, rows_[row].name, rows_[row].member, rows_[row].type,
			                         rows_[row].scope, rows_[row].others, rows_[row].kind}});
	if (removed.isEmpty())
		return removed;
	Mutation mutation{changing_};
	// From the last run to the first, so the rows of the runs before stay put.
	for (auto end = removed.size(); end > 0;) {
		auto start = end - 1;
		while (start > 0 && removed[start - 1].row == removed[start].row - 1)
			--start;
		const int first = removed[start].row, last = removed[end - 1].row;
		beginRemoveRows({}, first, last);
		rows_.remove(first, last - first + 1);
		count_ -= last - first + 1;
		endRemoveRows();
		end = start;
	}
	++membership_;
	return removed;
}

bool WatchlistModel::restore(const QVector<Removed>& rows)
{
	if (changing_ || rows.isEmpty() || rows.size() > maxRows - count_ || nextToken_ == 0 ||
	    static_cast<quintptr>(rows.size() - 1) > std::numeric_limits<quintptr>::max() - nextToken_)
		return false;
	QSet<WatchKey> held;
	for (int row = 0; row < count_; ++row)
		held.insert(rows_[row].key);
	// In removeIds' order, each row's place is where it was among the rows
	// put back before it, so the places must rise and fit.
	int previous = -1;
	for (int i = 0; i < rows.size(); ++i) {
		const auto& entry = rows[i];
		if (entry.entry.name.isEmpty() || held.contains(entry.entry.key) || entry.row <= previous ||
		    entry.row > count_ + i)
			return false;
		held.insert(entry.entry.key);
		previous = entry.row;
	}
	Mutation mutation{changing_};
	for (auto start = qsizetype(0); start < rows.size();) {
		auto end = start + 1;
		while (end < rows.size() && rows[end].row == rows[end - 1].row + 1)
			++end;
		const int first = rows[start].row, last = rows[end - 1].row;
		beginInsertRows({}, first, last);
		for (auto i = start; i < end; ++i) {
			rows_.insert(rows[i].row, makeRow(rows[i].entry));
			++count_;
		}
		endInsertRows();
		start = end;
	}
	++membership_;
	return true;
}

quint64 WatchlistModel::membership() const noexcept
{
	return membership_;
}

void WatchlistModel::clear()
{
	if (changing_)
		return;
	if (count_ == 0) {
		owner_ = nullptr;
		current_ = {};
		session_ = 0;
		admission_ = 0;
		return;
	}
	Mutation mutation{changing_};
	beginResetModel();
	rows_.clear();
	count_ = 0;
	++membership_;
	endResetModel();
	owner_ = nullptr;
	current_ = {};
	session_ = 0;
	admission_ = 0;
}

void WatchlistModel::invalidateValues()
{
	if (changing_ || count_ == 0)
		return;
	Mutation mutation{changing_};
	for (int row = 0; row < count_; ++row) {
		rows_[row].status = QStringLiteral("Stale");
		rows_[row].text.clear();
	}
	Q_EMIT dataChanged(index(0, 0), index(count_ - 1, 0),
	                   {StatusRole, TextRole, SampleTextRole, SampleValueRole, SampleExactRole,
	                    StatusCodeRole, EditorTextRole});
}

bool WatchlistModel::setResult(int row, QString status, QString text, WatchSample sample,
                               WatchValueBits value)
{
	if (changing_ || row < 0 || row >= count_)
		return false;
	Mutation mutation{changing_};
	rows_[row].status = std::move(status);
	rows_[row].text = std::move(text);
	if (rows_[row].status == QStringLiteral("Ok") &&
	    rows_[row].sampleRevision != std::numeric_limits<quint64>::max()) {
		rows_[row].sampleText = rows_[row].text;
		rows_[row].sample = sample;
		rows_[row].value = value;
		++rows_[row].sampleRevision;
	}
	Q_EMIT dataChanged(index(row, 0), index(row, 0),
	                   {StatusRole, TextRole, SampleRevisionRole, SampleTextRole, SampleEpochRole,
	                    SampleValueRole, SampleExactRole, StatusCodeRole, EditorTextRole,
	                    EditorKindRole, EditorChoicesRole, EditorChoicesTruncatedRole,
	                    TypeTextRole, CategoricalRole});
	return true;
}

quint32 WatchlistModel::id(int row) const noexcept
{
	return row >= 0 && row < count_ ? watch_key::field(rows_[row].key) : 0;
}

WatchKey WatchlistModel::key(int row) const noexcept
{
	return row >= 0 && row < count_ ? rows_[row].key : 0;
}

QString WatchlistModel::member(int row) const
{
	return row >= 0 && row < count_ ? rows_[row].member : QString{};
}

int WatchlistModel::rowOf(WatchKey key) const
{
	// Every change of the rows moves membership_ once it is done; rebuilt on
	// first use after. While rows are changing, looked up row by row.
	if (changing_) {
		for (int row = 0; row < count_; ++row)
			if (rows_[row].key == key)
				return row;
		return -1;
	}
	if (rowOfMembership_ != membership_) {
		rowOf_.clear();
		rowOf_.reserve(count_);
		for (int row = 0; row < count_; ++row)
			rowOf_.insert(rows_[row].key, row);
		rowOfMembership_ = membership_;
	}
	return rowOf_.value(key, -1);
}

int WatchlistModel::count() const noexcept
{
	return count_;
}

bool WatchlistModel::changing() const noexcept
{
	return changing_;
}

QModelIndex WatchlistModel::index(int row, int column, const QModelIndex& parent) const
{
	return !parent.isValid() && column == 0 && row >= 0 && row < count_
	           ? createIndex(row, column, rows_[row].token)
	           : QModelIndex{};
}

int WatchlistModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : count_;
}

QVariant WatchlistModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
	    index.row() >= count_ || index.internalId() != rows_[index.row()].token)
		return {};
	const auto& row = rows_[index.row()];
	if ((current_ && !current_()) ||
	    (owner_ && (!owner_->isCurrent() || owner_->session() != session_ ||
	                owner_->snapshotInfo().admission != admission_))) {
		if (role == StatusRole)
			return QStringLiteral("Stale");
		if (role == StatusCodeRole)
			return static_cast<int>(WatchStatus::Stale);
		if (role == TextRole)
			return QString{};
		if (role == SampleTextRole || role == SampleValueRole)
			return role == SampleTextRole ? QVariant(QString{}) : QVariant();
		if (role == SampleExactRole)
			return false;
		if (role >= EditorTextRole && role <= CategoricalRole)
			return {};
	}
	const auto* metadata = row.type ? metadata_->type(row.type) : metadata_->field(watch_key::field(row.key));
	const auto kind = metadata ? metadata->kind
	                  : row.value.type.enumeration ? QStringLiteral("enum")
	                  : row.value.type.scalar == WatchScalar::Boolean ? QStringLiteral("bool")
	                  : QStringLiteral("text");
	switch (role) {
		case EndpointIdRole:
			return watch_key::field(row.key);
		case WatchKeyRole:
			return qulonglong(row.key);
		case MemberRole:
			return row.member;
		case WriteScopeRole:
			if (watch_key::isWhole(row.key) || row.scope.isEmpty())
				return QVariantMap{};
			return QVariantMap{{QStringLiteral("field"), row.scope},
			                   {QStringLiteral("others"), row.others},
			                   {QStringLiteral("kind"), row.kind == WatchEntry::Kind::Members ? QStringLiteral("members")
			                                            : row.kind == WatchEntry::Kind::Elements
			                                                ? QStringLiteral("elements")
			                                                : QStringLiteral("values")}};
		case WriteRefusalRole:
			return row.refusal;
		case NameRole:
		case Qt::DisplayRole:
			return row.name;
		case StatusRole:
			return row.status;
		case TextRole:
			return row.text;
		case SampleRevisionRole:
			return QString::number(row.sampleRevision);
		case SampleTextRole:
			return row.status == QStringLiteral("Ok") ? row.sampleText : QString{};
		case SampleEpochRole:
			return QString::number(sampleEpoch_);
		case SampleValueRole:
			return row.status == QStringLiteral("Ok") ? row.sample.role() : QVariant();
		case SampleExactRole:
			return row.status == QStringLiteral("Ok") && row.sample.exact;
		case StatusCodeRole:
			return static_cast<int>(watchStatusCode(row.status));
		case WritableRole:
			return row.writable;
		case EditorTextRole:
			return row.status == QStringLiteral("Ok") ? canonicalText(row.value) : QString{};
		case EditorKindRole:
			return kind;
		case EditorChoicesRole:
			if (metadata) return metadata->choices;
			return kind == QStringLiteral("bool") ? detail::booleanEditorChoices() : QVariantList{};
		case EditorChoicesTruncatedRole:
			return metadata && metadata->truncated;
		case TypeTextRole:
			return metadata ? metadata->label : fallbackTypeText(row.value.type);
		case CategoricalRole:
			return kind == QStringLiteral("bool") || kind == QStringLiteral("enum");
		default:
			return {};
	}
}

QHash<int, QByteArray> WatchlistModel::roleNames() const
{
	return {{EndpointIdRole, "endpointId"},
	        {NameRole, "name"},
	        {StatusRole, "status"},
	        {TextRole, "text"},
	        {SampleRevisionRole, "sampleRevision"},
	        {SampleTextRole, "sampleText"},
	        {SampleEpochRole, "sampleEpoch"},
	        {SampleValueRole, "sampleValue"},
	        {SampleExactRole, "sampleExact"},
	        {StatusCodeRole, "statusCode"},
	        {WritableRole, "writable"},
	        {EditorTextRole, "editorText"},
	        {EditorKindRole, "editorKind"},
	        {EditorChoicesRole, "editorChoices"},
	        {EditorChoicesTruncatedRole, "editorChoicesTruncated"},
	        {TypeTextRole, "typeText"},
	        {CategoricalRole, "categorical"},
	        {WatchKeyRole, "watchKey"},
	        {MemberRole, "member"},
	        {WriteScopeRole, "writeScope"},
	        {WriteRefusalRole, "writeRefusal"}};
}

QString WatchlistModel::formatValue(const telemetry::runtime::DynamicValueView& value)
{
	if (!value.valid())
		return {};
#define WATCH_INTEGER(T)                                                                           \
	if (const auto result = value.get<T>(); result)                                                \
	return number(result.value)
	WATCH_INTEGER(bool);
	WATCH_INTEGER(std::uint8_t);
	WATCH_INTEGER(std::int8_t);
	WATCH_INTEGER(std::uint16_t);
	WATCH_INTEGER(std::int16_t);
	WATCH_INTEGER(std::uint32_t);
	WATCH_INTEGER(std::int32_t);
	WATCH_INTEGER(std::uint64_t);
	WATCH_INTEGER(std::int64_t);
#undef WATCH_INTEGER
	const auto bytes = value.bytes();
	if (!bytes)
		return {};
	if (const auto result = value.get<float>(); result)
		return floating(result.value, bytes.value);
	if (const auto result = value.get<double>(); result)
		return floating(result.value, bytes.value);
	const auto name = value.enumName();
	if (!name)
		return {};
	QString digits;
#define WATCH_ENUM(T)                                                                              \
	if (const auto result = value.enumUnderlying<T>(); result)                                     \
	digits = number(result.value)
	WATCH_ENUM(std::uint8_t);
	WATCH_ENUM(std::int8_t);
	WATCH_ENUM(std::uint16_t);
	WATCH_ENUM(std::int16_t);
	WATCH_ENUM(std::uint32_t);
	WATCH_ENUM(std::int32_t);
	WATCH_ENUM(std::uint64_t);
	WATCH_ENUM(std::int64_t);
#undef WATCH_ENUM
	if (digits.isEmpty())
		return {};
	return name.value.known
	           ? QString::fromUtf8(name.value.name.data(), qsizetype(name.value.name.size())) +
	                 QStringLiteral(" (") + digits + QLatin1Char(')')
	           : digits + QStringLiteral(" (unknown)");
}

WatchSample WatchlistModel::sampleOf(const telemetry::runtime::DynamicValueView& value)
{
	if (!value.valid())
		return {};
	if (const auto result = value.get<bool>(); result)
		return WatchSample::ofUnsigned(result.value ? 1u : 0u);
#define WATCH_SIGNED(T)                                                                            \
	if (const auto result = value.get<T>(); result)                                                \
	return WatchSample::ofSigned(result.value)
#define WATCH_UNSIGNED(T)                                                                          \
	if (const auto result = value.get<T>(); result)                                                \
	return WatchSample::ofUnsigned(result.value)
	WATCH_UNSIGNED(std::uint8_t);
	WATCH_SIGNED(std::int8_t);
	WATCH_UNSIGNED(std::uint16_t);
	WATCH_SIGNED(std::int16_t);
	WATCH_UNSIGNED(std::uint32_t);
	WATCH_SIGNED(std::int32_t);
	WATCH_UNSIGNED(std::uint64_t);
	WATCH_SIGNED(std::int64_t);
	if (const auto result = value.get<float>(); result)
		return WatchSample::ofFloating(result.value);
	if (const auto result = value.get<double>(); result)
		return WatchSample::ofFloating(result.value);
#undef WATCH_SIGNED
#undef WATCH_UNSIGNED
#define WATCH_ENUM_SIGNED(T)                                                                       \
	if (const auto result = value.enumUnderlying<T>(); result)                                     \
	return WatchSample::ofSigned(result.value)
#define WATCH_ENUM_UNSIGNED(T)                                                                     \
	if (const auto result = value.enumUnderlying<T>(); result)                                     \
	return WatchSample::ofUnsigned(result.value)
	WATCH_ENUM_UNSIGNED(std::uint8_t);
	WATCH_ENUM_SIGNED(std::int8_t);
	WATCH_ENUM_UNSIGNED(std::uint16_t);
	WATCH_ENUM_SIGNED(std::int16_t);
	WATCH_ENUM_UNSIGNED(std::uint32_t);
	WATCH_ENUM_SIGNED(std::int32_t);
	WATCH_ENUM_UNSIGNED(std::uint64_t);
	WATCH_ENUM_SIGNED(std::int64_t);
#undef WATCH_ENUM_SIGNED
#undef WATCH_ENUM_UNSIGNED
	return {};
}
} // namespace telemetry::ux
