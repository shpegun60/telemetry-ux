/*
 * @file WatchLeaves.cpp
 * @brief The values inside a Field's type, numbered as the Watchlist keys them.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "WatchLeaves.hpp"
#include <QtCore/QByteArray>
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
