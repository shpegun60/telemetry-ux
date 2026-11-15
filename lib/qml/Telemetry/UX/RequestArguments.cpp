// A Command's or Service's input typed as name=value, checked and encoded. MIT.
#include "RequestArguments.hpp"
#include "SchemaText.hpp"
#include "WatchFormat.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtCore/QRegularExpression>
#include <algorithm>
#include <cstring>

namespace telemetry::ux::request_arguments {
namespace rt = telemetry::runtime;
namespace {
QString words(const char* text)
{
	return QCoreApplication::translate("RequestArguments", text);
}

bool isSigned(WatchScalar scalar)
{
	return scalar == WatchScalar::S8 || scalar == WatchScalar::S16 || scalar == WatchScalar::S32 ||
	       scalar == WatchScalar::S64;
}

// An enum code's bits, zero-extended, as the number they are at width.
qint64 codeOf(quint64 bits, const WatchValueType& type)
{
	if (!isSigned(type.scalar) || type.width >= 8)
		return qint64(bits);
	const auto shift = 64 - 8 * type.width;
	return qint64(bits << shift) >> shift;
}

int distance(const QString& a, const QString& b)
{
	std::vector<int> row(std::size_t(b.size()) + 1);
	for (int j = 0; j <= b.size(); ++j)
		row[std::size_t(j)] = j;
	for (int i = 1; i <= a.size(); ++i) {
		int previous = row[0];
		row[0] = i;
		for (int j = 1; j <= b.size(); ++j) {
			const int kept = row[std::size_t(j)];
			row[std::size_t(j)] = std::min({row[std::size_t(j)] + 1, row[std::size_t(j - 1)] + 1,
			                                previous + (a[i - 1].toCaseFolded() == b[j - 1].toCaseFolded() ? 0 : 1)});
			previous = kept;
		}
	}
	return row[std::size_t(b.size())];
}

// "a, b and c".
QString listed(const QStringList& parts, const QString& last)
{
	if (parts.size() < 2)
		return parts.join(QString{});
	return parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + last + parts.last();
}

QString nameOf(const Argument& argument)
{
	return argument.name.isEmpty() ? words("the value") : argument.name;
}

// What an argument takes, as the line under the row says it.
QString takes(const Argument& argument)
{
	const auto who = QStringLiteral("%1 (%2)").arg(nameOf(argument), argument.typeText);
	if (argument.value.scalar == WatchScalar::Boolean && !argument.value.enumeration)
		return words("%1 takes true or false").arg(who);
	if (argument.value.enumeration) {
		auto names = argument.choices.mid(0, 4);
		if (argument.choices.size() > 4)
			return words("%1 takes %2, … or a code; Down lists them").arg(who, names.join(QStringLiteral(", ")));
		return words("%1 takes %2, or a code").arg(who, listed(names, words(" or ")));
	}
	if (argument.value.scalar == WatchScalar::F32 || argument.value.scalar == WatchScalar::F64)
		return words("%1 takes a number").arg(who);
	auto range = watch_format::range(argument.value);
	if (range.startsWith(QStringLiteral("from ")))
		range = range.mid(5);
	return range.isEmpty() ? words("%1 takes a number").arg(who) : words("%1 takes %2").arg(who, range);
}
} // namespace

WatchValueType valueType(const rt::RuntimeSchema& schema, TypeId id)
{
	const auto* type = schema.type(id);
	if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) || type->wireBytes == 0 ||
	    type->wireBytes > 8)
		return {};
	const auto* scalar = type->kind == TypeKind::Enum ? schema.type(type->relatedTypeId) : type;
	if (!scalar || scalar->kind != TypeKind::Scalar)
		return {};
	WatchScalar code = WatchScalar::Unknown;
	switch (scalar->scalarCode) {
		case ScalarCode::Bool: code = WatchScalar::Boolean; break;
		case ScalarCode::U8: code = WatchScalar::U8; break;
		case ScalarCode::S8: code = WatchScalar::S8; break;
		case ScalarCode::U16: code = WatchScalar::U16; break;
		case ScalarCode::S16: code = WatchScalar::S16; break;
		case ScalarCode::U32: code = WatchScalar::U32; break;
		case ScalarCode::S32: code = WatchScalar::S32; break;
		case ScalarCode::U64: code = WatchScalar::U64; break;
		case ScalarCode::S64: code = WatchScalar::S64; break;
		case ScalarCode::F32: code = WatchScalar::F32; break;
		case ScalarCode::F64: code = WatchScalar::F64; break;
	}
	WatchValueType result{type->id, code, quint8(type->wireBytes), type->kind == TypeKind::Enum};
	return result.valid() ? result : WatchValueType{};
}

std::vector<Argument> arguments(const rt::RuntimeSchema& schema, TypeId request)
{
	std::vector<Argument> out;
	const auto* type = schema.type(request);
	if (!type || type->kind == TypeKind::Void)
		return out;
	const auto one = [&schema](QString name, TypeId id, quint32 offset) {
		Argument argument;
		argument.name = std::move(name);
		const auto* inner = schema.type(id);
		argument.type = id;
		argument.offset = offset;
		argument.bytes = inner ? inner->wireBytes : 0;
		argument.value = valueType(schema, id);
		argument.scalar = argument.value.valid();
		argument.typeText = inner && inner->kind == TypeKind::Enum ? QStringLiteral("enum") : schema_text::typeText(schema, id);
		if (argument.value.scalar == WatchScalar::Boolean && !argument.value.enumeration)
			argument.choices = {QStringLiteral("true"), QStringLiteral("false")};
		if (argument.value.enumeration)
			for (const auto& entry : schema.enumEntries(id)) {
				argument.choices << schema_text::text(entry.name);
				argument.codes << codeOf(entry.codeBits, argument.value);
			}
		return argument;
	};
	if (type->kind != TypeKind::Struct) {
		out.push_back(one({}, request, 0));
		return out;
	}
	for (const auto& member : schema.members(request))
		out.push_back(one(schema_text::text(member.name), member.typeId, member.wireOffset));
	return out;
}

QString signatureText(const rt::RuntimeSchema& schema, TypeId request)
{
	const auto inputs = arguments(schema, request);
	QStringList parts;
	for (std::size_t i = 0; i < inputs.size() && i < 4; ++i)
		parts << (inputs[i].name.isEmpty() ? inputs[i].typeText
		                                   : QStringLiteral("%1: %2").arg(inputs[i].name, inputs[i].typeText));
	if (inputs.size() > 4)
		parts << QStringLiteral("…");
	return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", ")));
}

std::vector<Given> parse(QStringView tail)
{
	const auto tokens = tail.toString().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	std::vector<Given> out;
	for (qsizetype i = 0; i < tokens.size(); ++i) {
		const auto& token = tokens[i];
		const auto equals = token.indexOf(QLatin1Char('='));
		if (equals < 0) {
			// "name = value" or "name =value": the name, then its value.
			if (i + 1 < tokens.size() && tokens[i + 1].startsWith(QLatin1Char('='))) {
				auto value = tokens[++i].mid(1);
				if (value.isEmpty() && i + 1 < tokens.size() && !tokens[i + 1].contains(QLatin1Char('=')))
					value = tokens[++i];
				out.push_back({token, value});
			} else {
				out.push_back({{}, token});
			}
			continue;
		}
		auto value = token.mid(equals + 1);
		// "name= value": the value is the next word.
		if (value.isEmpty() && i + 1 < tokens.size() && !tokens[i + 1].contains(QLatin1Char('=')))
			value = tokens[++i];
		out.push_back({token.left(equals), value});
	}
	return out;
}

Result check(const rt::RuntimeSchema& schema, TypeId request, const QString& path, QStringView tail)
{
	Result out;
	const auto inputs = arguments(schema, request);
	auto given = parse(tail);
	for (const auto& input : inputs)
		out.items.push_back({Item::Status::Missing, input.name, input.typeText, {}, {}, {}});
	if (inputs.empty()) {
		if (!given.empty())
			out.error = words("%1 takes no input; remove \"%2\".").arg(path, tail.trimmed().toString());
		else
			out.request = QByteArray("");
		out.message = out.error;
		return out;
	}
	// Values with no names: one input may take one; otherwise Enter writes
	// the names in, so a pair of the same type is never swapped unseen.
	const bool anyNamed = std::any_of(given.begin(), given.end(), [](const Given& g) { return !g.name.isEmpty(); });
	const bool anyUnnamed = std::any_of(given.begin(), given.end(), [](const Given& g) { return g.name.isEmpty(); });
	if (anyUnnamed && !anyNamed && inputs.size() == 1 && given.size() == 1) {
		given[0].name = inputs[0].name.isEmpty() ? QStringLiteral("\x1f") : inputs[0].name;
	} else if (anyUnnamed) {
		QStringList example;
		for (std::size_t i = 0; i < inputs.size(); ++i)
			example << QStringLiteral("%1=%2").arg(inputs[i].name,
			                                       !anyNamed && i < given.size() ? given[i].text : QStringLiteral("…"));
		out.unnamed = true;
		if (!anyNamed && given.size() <= inputs.size())
			out.named = path + QLatin1Char(' ') + example.join(QLatin1Char(' ')).replace(QStringLiteral("=…"), QStringLiteral("="));
		out.error = words("%1 takes named values: %2. Enter writes the names in.").arg(path, example.join(QLatin1Char(' ')));
		out.message = out.error;
		return out;
	}
	QByteArray bytes;
	if (const auto* type = schema.type(request))
		bytes = QByteArray(qsizetype(type->wireBytes), '\0');
	for (const auto& one : given) {
		int at = -1;
		for (std::size_t i = 0; i < inputs.size() && at < 0; ++i)
			if (one.name == QStringLiteral("\x1f") ? inputs[i].name.isEmpty()
			                                       : inputs[i].name.compare(one.name, Qt::CaseInsensitive) == 0)
				at = int(i);
		if (at < 0) {
			QStringList names;
			QString closest;
			int best = 3;
			for (const auto& input : inputs) {
				names << input.name;
				const auto d = distance(one.name, input.name);
				if (d < best) {
					best = d;
					closest = input.name;
				}
			}
			out.error = closest.isEmpty()
			                ? words("%1 has no argument %2; it takes %3.").arg(path, one.name, listed(names, words(" and ")))
			                : words("%1 has no argument %2. Did you mean %3?").arg(path, one.name, closest);
			out.message = out.error;
			return out;
		}
		auto& item = out.items[std::size_t(at)];
		const auto& input = inputs[std::size_t(at)];
		if (item.status != Item::Status::Missing) {
			out.error = words("%1 is given twice; keep one.").arg(nameOf(input));
			out.message = out.error;
			return out;
		}
		item.text = one.text;
		if (!input.scalar) {
			item.status = Item::Status::Aggregate;
			out.aggregate = true;
			continue;
		}
		if (one.text.isEmpty()) {
			item.status = Item::Status::Error;
			item.error = takes(input) + QLatin1Char('.');
			continue;
		}
		// An enum's name is its code; a name matches exactly, then in any case.
		auto text = one.text;
		if (input.value.enumeration) {
			auto found = input.choices.indexOf(text);
			if (found < 0)
				for (qsizetype i = 0; i < input.choices.size() && found < 0; ++i)
					if (input.choices[i].compare(text, Qt::CaseInsensitive) == 0)
						found = i;
			if (found >= 0)
				text = QString::number(input.codes[found]);
		}
		const auto encoded = watch_format::encode(text, input.value);
		if (!encoded.error.isEmpty() || encoded.value.size != input.value.width) {
			item.status = Item::Status::Error;
			item.error = words("%1, not \"%2\".").arg(takes(input), one.text);
			continue;
		}
		item.status = Item::Status::Ok;
		QHash<quint64, QString> names;
		for (qsizetype i = 0; i < input.choices.size() && input.value.enumeration; ++i)
			names.insert(quint64(input.codes[i]) & (input.value.width >= 8 ? ~quint64{0} : ((quint64{1} << (8 * input.value.width)) - 1)),
			             input.choices[i]);
		item.canonical = watch_format::text(encoded.value, WatchNumberFormat::Original, input.value.enumeration ? &names : nullptr);
		// A code no name has is said as the code.
		if (input.value.enumeration && !input.choices.contains(item.canonical))
			item.canonical = text;
		if (qsizetype(input.offset) + encoded.value.size <= bytes.size())
			std::memcpy(bytes.data() + input.offset, encoded.value.bytes.data(), encoded.value.size);
	}
	// What is still needed, or what is wrong, in the line's words.
	QStringList missing;
	int firstMissing = -1;
	for (std::size_t i = 0; i < inputs.size(); ++i)
		if (out.items[i].status == Item::Status::Missing && inputs[i].scalar) {
			if (firstMissing < 0)
				firstMissing = int(i);
			missing << QStringLiteral("%1 (%2)").arg(nameOf(inputs[i]), inputs[i].typeText);
		}
	for (std::size_t i = 0; i < inputs.size(); ++i)
		if (out.items[i].status == Item::Status::Error) {
			out.message = out.items[i].error;
			return out;
		}
	if (out.aggregate || std::any_of(inputs.begin(), inputs.end(), [](const Argument& a) { return !a.scalar; })) {
		for (const auto& input : inputs)
			if (!input.scalar) {
				out.aggregate = true;
				out.message = words("%1 takes %2 (%3), which is filled in Details. Enter selects it there with the "
				                    "values typed here; then Add selected draft.")
				                  .arg(path, nameOf(input), input.typeText);
				break;
			}
		return out;
	}
	if (!missing.isEmpty()) {
		const bool none = std::none_of(out.items.begin(), out.items.end(),
		                               [](const Item& item) { return item.status == Item::Status::Ok; });
		const auto needs = missing.size() > 3
		                       ? words("%1 and %L2 more").arg(missing.mid(0, 3).join(QStringLiteral(", "))).arg(missing.size() - 3)
		                       : listed(missing, words(" and "));
		const auto next = inputs[std::size_t(firstMissing)].name;
		out.message = none ? words("%1 needs %2. Enter writes %3=; Ctrl+Enter fills it in Details.").arg(path, needs, next)
		                   : words("%1 needs %2 too. Enter writes %3=.").arg(path, needs, next);
		return out;
	}
	if (!schema.view(request, rt::Input{reinterpret_cast<const std::byte*>(bytes.constData()), std::size_t(bytes.size())})) {
		out.error = words("These values do not make a valid request for %1.").arg(path);
		out.message = out.error;
		return out;
	}
	out.request = bytes;
	return out;
}
} // namespace telemetry::ux::request_arguments
