/*
 * @file WatchLeaves.cpp
 * @brief The values inside a Field's type, numbered as the Watchlist keys them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchLeaves.hpp"
#include "SchemaText.hpp"
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QVector>
#include <algorithm>
#include <limits>
#include <string_view>

namespace telemetry::ux {
namespace {
bool isLeaf(const runtime::RuntimeType* type) noexcept
{
	return type && (type->kind == TypeKind::Scalar || type->kind == TypeKind::Enum);
}

quint32 clamped(std::uint64_t value) noexcept
{
	return value > (std::numeric_limits<quint32>::max)() ? (std::numeric_limits<quint32>::max)()
	                                                    : static_cast<quint32>(value);
}
} // namespace

QString WatchLeaves::Leaf::member() const
{
	return suffix.startsWith(QLatin1Char('.')) ? suffix.mid(1) : suffix;
}

void WatchLeaves::clear()
{
	schema_ = {};
	counts_.clear();
}

void WatchLeaves::reset(const runtime::RuntimeSchema& schema)
{
	clear();
	if (!schema.valid())
		return;
	schema_ = schema;
	for (TypeId id = 0;; ++id) {
		const auto* type = schema.type(id);
		if (!type)
			break;
		std::uint64_t n = 0;
		switch (type->kind) {
			case TypeKind::Scalar:
			case TypeKind::Enum:
				n = 1;
				break;
			case TypeKind::Struct:
				for (const auto& member : schema.members(id))
					n += count(member.typeId);
				break;
			case TypeKind::Array:
				n = std::uint64_t{type->elementCount} * count(type->relatedTypeId);
				break;
			case TypeKind::Void:
				break;
		}
		counts_.append(clamped(n));
	}
}

bool WatchLeaves::matches(const runtime::RuntimeSchema& schema) const noexcept
{
	return schema.valid() && schema_.valid() && schema.owner() == schema_.owner() &&
	       schema.generation() == schema_.generation();
}

quint32 WatchLeaves::count(TypeId type) const noexcept
{
	return type < static_cast<std::size_t>(counts_.size()) ? counts_[static_cast<qsizetype>(type)] : 0;
}

std::optional<WatchLeaves::Leaf> WatchLeaves::locate(TypeId root, quint32 ordinal) const
{
	if (!schema_.valid() || ordinal >= count(root))
		return std::nullopt;
	Leaf leaf;
	leaf.type = root;
	std::uint64_t offset = 0;
	for (;;) {
		const auto* type = schema_.type(leaf.type);
		if (!type)
			return std::nullopt;
		if (isLeaf(type))
			break;
		if (leaf.depth >= leaf.steps.size())
			return std::nullopt;
		Step step;
		if (type->kind == TypeKind::Struct) {
			const auto members = schema_.members(leaf.type);
			std::size_t at = 0;
			for (; at < members.size(); ++at) {
				const auto n = count(members[at].typeId);
				if (ordinal < n)
					break;
				ordinal -= n;
			}
			if (at == members.size())
				return std::nullopt;
			const auto& member = members[at];
			step = {false, static_cast<quint32>(at)};
			offset += member.wireOffset;
			leaf.suffix += QLatin1Char('.') +
			               QString::fromUtf8(member.name.data(), static_cast<qsizetype>(member.name.size()));
			leaf.type = member.typeId;
		} else if (type->kind == TypeKind::Array) {
			const auto n = count(type->relatedTypeId);
			const auto* element = schema_.type(type->relatedTypeId);
			if (n == 0 || !element)
				return std::nullopt;
			const auto index = ordinal / n;
			ordinal %= n;
			if (index >= type->elementCount)
				return std::nullopt;
			step = {true, index};
			offset += std::uint64_t{index} * element->wireBytes;
			leaf.suffix += QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
			leaf.type = type->relatedTypeId;
		} else {
			return std::nullopt;
		}
		leaf.types[leaf.depth] = leaf.type;
		leaf.steps[leaf.depth++] = step;
	}
	if (offset > (std::numeric_limits<quint32>::max)())
		return std::nullopt;
	leaf.offset = static_cast<quint32>(offset);
	return leaf;
}

std::optional<quint32> WatchLeaves::ordinal(TypeId root, QStringView member) const
{
	if (!schema_.valid() || member.isEmpty())
		return std::nullopt;
	const QByteArray utf8 = member.toUtf8();
	const std::string_view path(utf8.constData(), static_cast<std::size_t>(utf8.size()));
	TypeId current = root;
	std::uint64_t at = 0;
	std::size_t pos = 0;
	while (pos < path.size()) {
		const auto* type = schema_.type(current);
		if (!type)
			return std::nullopt;
		if (path[pos] == '[') {
			++pos;
			const auto begin = pos;
			std::uint64_t index = 0;
			while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9' && index <= 0xffffffffu)
				index = index * 10 + static_cast<unsigned>(path[pos++] - '0');
			if (pos == begin || pos == path.size() || path[pos++] != ']')
				return std::nullopt;
			if (type->kind != TypeKind::Array || index >= type->elementCount)
				return std::nullopt;
			at += index * count(type->relatedTypeId);
			current = type->relatedTypeId;
		} else {
			const auto begin = pos;
			while (pos < path.size() && path[pos] != '.' && path[pos] != '[' && path[pos] != ']')
				++pos;
			if (begin == pos || type->kind != TypeKind::Struct)
				return std::nullopt;
			const auto name = path.substr(begin, pos - begin);
			bool found = false;
			for (const auto& m : schema_.members(current)) {
				if (m.name == name) {
					current = m.typeId;
					found = true;
					break;
				}
				at += count(m.typeId);
			}
			if (!found)
				return std::nullopt;
		}
		if (pos < path.size() && path[pos] != '[') {
			if (path[pos++] != '.' || pos == path.size() || path[pos] == '[')
				return std::nullopt;
		}
	}
	if (!isLeaf(schema_.type(current)) || at >= count(root))
		return std::nullopt;
	return static_cast<quint32>(at);
}

namespace {
// Edit distance, for "Did you mean": short names, so the plain table.
int distance(QStringView a, QStringView b)
{
	QVector<int> row(b.size() + 1);
	for (qsizetype j = 0; j <= b.size(); ++j)
		row[j] = static_cast<int>(j);
	for (qsizetype i = 1; i <= a.size(); ++i) {
		int diagonal = row[0];
		row[0] = static_cast<int>(i);
		for (qsizetype j = 1; j <= b.size(); ++j) {
			const int above = row[j];
			row[j] = std::min({row[j] + 1, row[j - 1] + 1,
			                   diagonal + (a.at(i - 1).toCaseFolded() == b.at(j - 1).toCaseFolded() ? 0 : 1)});
			diagonal = above;
		}
	}
	return row[b.size()];
}

QString tr(const char* text)
{
	return QCoreApplication::translate("WatchLeaves", text);
}
} // namespace

WatchLeaves::Range WatchLeaves::range(TypeId root, QStringView member, const QString& path) const
{
	Range out;
	const auto syntax = [&] {
		out = {};
		out.error = tr("Write a path such as device.Config.Gain or data.Samples[3].");
		return out;
	};
	if (!schema_.valid() || !schema_.type(root)) {
		out.error = tr("%1 is not on this device.").arg(path);
		return out;
	}
	TypeId current = root;
	std::uint64_t at = 0;
	QString here = path;
	qsizetype pos = 0;
	while (pos < member.size()) {
		const auto* type = schema_.type(current);
		if (!type)
			return syntax();
		if (member.at(pos) == u'[') {
			const auto close = member.indexOf(u']', pos);
			if (close < 0)
				return syntax();
			bool ok = false;
			const auto index = member.mid(pos + 1, close - pos - 1).toULongLong(&ok);
			if (!ok)
				return syntax();
			if (type->kind != TypeKind::Array) {
				out.error = tr("%1 is not an array.").arg(here);
				return out;
			}
			if (index >= type->elementCount) {
				out.error = tr("%1 has %L2 elements, [0] to [%3].").arg(here).arg(type->elementCount).arg(type->elementCount - 1);
				return out;
			}
			at += index * count(type->relatedTypeId);
			current = type->relatedTypeId;
			here += QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
			pos = close + 1;
		} else {
			auto end = pos;
			while (end < member.size() && member.at(end) != u'.' && member.at(end) != u'[' && member.at(end) != u']')
				++end;
			if (end == pos)
				return syntax();
			const auto name = member.mid(pos, end - pos);
			if (type->kind != TypeKind::Struct) {
				out.error = tr("%1 is not a struct, so it has no member %2.").arg(here, name.toString());
				return out;
			}
			bool found = false;
			QString nearest;
			int best = 3;
			std::uint64_t skipped = 0;
			for (const auto& m : schema_.members(current)) {
				const auto text = QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size()));
				if (text == name) {
					current = m.typeId;
					found = true;
					break;
				}
				skipped += count(m.typeId);
				const int d = distance(name, text);
				if (d < best) {
					best = d;
					nearest = text;
				}
			}
			if (!found) {
				out.error = nearest.isEmpty()
				                ? tr("%1 has no member %2.").arg(here, name.toString())
				                : tr("%1 has no member %2. Did you mean %3?").arg(here, name.toString(), nearest);
				return out;
			}
			at += skipped;
			here += QLatin1Char('.') + name.toString();
			pos = end;
		}
		if (pos < member.size() && member.at(pos) != u'[') {
			if (member.at(pos) != u'.' || pos + 1 == member.size() || member.at(pos + 1) == u'[')
				return syntax();
			++pos;
		}
	}
	const auto values = count(current);
	if (values == 0) {
		out.error = tr("%1 has no values to watch.").arg(here);
		return out;
	}
	if (at + values > count(root))
		return syntax();
	out.first = static_cast<quint32>(at);
	out.count = values;
	return out;
}

QVector<WatchLeaves::Child> WatchLeaves::children(TypeId root, QStringView member) const
{
	QVector<Child> out;
	if (!schema_.valid())
		return out;
	// The container's type: the first value under member, walked to member's depth.
	TypeId type = root;
	if (!member.isEmpty()) {
		const auto at = range(root, member, QString{});
		if (!at.error.isEmpty())
			return out;
		int depth = 0;
		for (qsizetype i = 0; i < member.size();) {
			if (member.at(i) == u'[') {
				const auto close = member.indexOf(u']', i);
				i = close < 0 ? member.size() : close + 1;
				++depth;
			} else if (member.at(i) == u'.') {
				++i;
			} else {
				while (i < member.size() && member.at(i) != u'.' && member.at(i) != u'[')
					++i;
				++depth;
			}
		}
		const auto first = locate(root, at.first);
		if (!first || depth < 1 || depth > first->depth)
			return out;
		type = first->types[static_cast<std::size_t>(depth - 1)];
	}
	const auto* shape = schema_.type(type);
	if (!shape)
		return out;
	if (shape->kind == TypeKind::Struct) {
		for (const auto& m : schema_.members(type)) {
			const auto* inner = schema_.type(m.typeId);
			out.append({QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size())),
			            QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size())), m.typeId,
			            count(m.typeId), !isLeaf(inner)});
		}
	} else if (shape->kind == TypeKind::Array && shape->elementCount > 0) {
		out.append({QStringLiteral("[0 … %1]").arg(shape->elementCount - 1), QStringLiteral("["), shape->relatedTypeId,
		            count(shape->relatedTypeId), true});
	}
	return out;
}

QString WatchLeaves::typeText(TypeId type) const
{
	return schema_.valid() ? schema_text::typeText(schema_, type) : QString{};
}

WatchLeaves::Kind WatchLeaves::kind(TypeId root) const noexcept
{
	const auto* type = schema_.type(root);
	if (!type)
		return Kind::Values;
	if (type->kind == TypeKind::Array)
		return isLeaf(schema_.type(type->relatedTypeId)) ? Kind::Elements : Kind::Values;
	if (type->kind != TypeKind::Struct)
		return Kind::Values;
	for (const auto& member : schema_.members(root))
		if (!isLeaf(schema_.type(member.typeId)))
			return Kind::Values;
	return Kind::Members;
}

} // namespace telemetry::ux
