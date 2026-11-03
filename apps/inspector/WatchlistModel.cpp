/*
 * @file WatchlistModel.cpp
 * @brief Materialize exact scalar watch values as bounded Qt-owned rows.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "WatchlistModel.hpp"
#include <charconv>
#include <limits>
#include <type_traits>

namespace inspector_qml {
namespace {
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
		return value ? QStringLiteral("true") : QStringLiteral("false");
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

WatchlistModel::WatchlistModel(QObject* parent) : QAbstractListModel(parent)
{}

void WatchlistModel::bind(const Workbench& owner) noexcept
{
	if (!changing_) {
		owner_ = &owner;
		session_ = owner.session();
		admission_ = owner.snapshotInfo().admission;
	}
}

bool WatchlistModel::add(quint32 endpoint, QString name)
{
	if (changing_ || count_ == maxRows || nextToken_ == 0 || name.isEmpty())
		return false;
	for (int row = 0; row < count_; ++row)
		if (rows_[row].id == endpoint)
			return false;
	Mutation mutation{changing_};
	beginInsertRows({}, count_, count_);
	rows_[count_++] = {endpoint, nextToken_++, std::move(name), QStringLiteral("Not read"), {}};
	endInsertRows();
	return true;
}

bool WatchlistModel::remove(int row)
{
	if (changing_ || row < 0 || row >= count_)
		return false;
	Mutation mutation{changing_};
	beginRemoveRows({}, row, row);
	for (int next = row + 1; next < count_; ++next)
		rows_[next - 1] = std::move(rows_[next]);
	rows_[--count_] = {};
	endRemoveRows();
	return true;
}

void WatchlistModel::clear()
{
	if (changing_)
		return;
	if (count_ == 0) {
		owner_ = nullptr;
		session_ = 0;
		admission_ = 0;
		return;
	}
	Mutation mutation{changing_};
	beginResetModel();
	for (int row = 0; row < count_; ++row)
		rows_[row] = {};
	count_ = 0;
	endResetModel();
	owner_ = nullptr;
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
	Q_EMIT dataChanged(index(0, 0), index(count_ - 1, 0), {StatusRole, TextRole});
}

bool WatchlistModel::setResult(int row, QString status, QString text)
{
	if (changing_ || row < 0 || row >= count_)
		return false;
	Mutation mutation{changing_};
	rows_[row].status = std::move(status);
	rows_[row].text = std::move(text);
	Q_EMIT dataChanged(index(row, 0), index(row, 0), {StatusRole, TextRole});
	return true;
}

quint32 WatchlistModel::id(int row) const noexcept
{
	return row >= 0 && row < count_ ? rows_[row].id : 0;
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
	if (owner_ && (!owner_->isCurrent() || owner_->session() != session_ ||
	               owner_->snapshotInfo().admission != admission_)) {
		if (role == StatusRole)
			return QStringLiteral("Stale");
		if (role == TextRole)
			return QString{};
	}
	switch (role) {
		case EndpointIdRole:
			return row.id;
		case NameRole:
		case Qt::DisplayRole:
			return row.name;
		case StatusRole:
			return row.status;
		case TextRole:
			return row.text;
		default:
			return {};
	}
}

QHash<int, QByteArray> WatchlistModel::roleNames() const
{
	return {{EndpointIdRole, "endpointId"},
	        {NameRole, "name"},
	        {StatusRole, "status"},
	        {TextRole, "text"}};
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
} // namespace inspector_qml
