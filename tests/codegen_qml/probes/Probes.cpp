// Strict QVariant scalar/aggregate conversion, independent of the QML facade.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include "QmlValues.hpp"
#include <QCoreApplication>
#include <iostream>

using namespace sdk::device;
namespace v = sdk::device::qml::values;
static int passed = 0, failed = 0;

static void check(bool ok, const char* label)
{
	if (ok)
		++passed;
	else {
		++failed;
		std::cerr << "FAIL " << label << '\n';
	}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	detail::Type_1 boolean{};
	check(v::from_1(true, boolean, error) && boolean, "bool");
	check(!v::from_1(1, boolean, error), "numeric bool rejected");
	check(!v::from_1(QStringLiteral("true"), boolean, error), "string bool rejected");
	detail::Type_6 u32{};
	check(v::from_6(4294967295.0, u32, error) && u32 == 0xffffffffu, "u32 max");
	check(!v::from_6(4294967296.0, u32, error), "u32 overflow");
	check(!v::from_6(-1, u32, error), "u32 negative");
	check(!v::from_6(0.5, u32, error), "fraction rejected");
	check(!v::from_6(true, u32, error), "bool integer rejected");
	check(!v::from_6(QStringLiteral("12"), u32, error), "string integer rejected");
	check(!v::from_6(QByteArray("12"), u32, error), "byte string integer rejected");
	check(v::to_6(0xffffffffu).typeId() == QMetaType::Double &&
	          v::to_6(0xffffffffu).toDouble() == 4294967295.0,
	      "u32 QML double exact");
	detail::Type_7 i32{};
	check(v::from_7(-2147483648.0, i32, error) && i32 == INT32_MIN, "i32 min");
	check(v::from_7(2147483647.0, i32, error) && i32 == INT32_MAX, "i32 max");
	check(!v::from_7(2147483648.0, i32, error), "i32 overflow");
	detail::Type_8 u64{};
	detail::Type_9 i64{};
	check(v::from_8(QStringLiteral("18446744073709551615"), u64, error) && u64 == UINT64_MAX,
	      "u64 max");
	check(v::from_9(QStringLiteral("-9223372036854775808"), i64, error) && i64 == INT64_MIN,
	      "i64 min");
	check(v::from_9(QStringLiteral("9223372036854775807"), i64, error) && i64 == INT64_MAX,
	      "i64 max");
	check(v::from_8(QStringLiteral("0"), u64, error) && u64 == 0, "u64 zero");
	check(v::from_9(QStringLiteral("0"), i64, error) && i64 == 0, "i64 zero");
	for (const auto& bad :
	     {"", "-", "+1", " 1", "1 ", "00", "01", "-0", "-01", "1.0", "1e3", "18446744073709551616"})
		check(!v::from_8(QString::fromLatin1(bad), u64, error), bad);
	check(!v::from_9(QStringLiteral("9223372036854775808"), i64, error), "i64 positive overflow");
	check(!v::from_9(QStringLiteral("-9223372036854775809"), i64, error), "i64 negative overflow");
	check(!v::from_8(QVariant::fromValue(qulonglong(UINT64_MAX)), u64, error),
	      "u64 typed numeric rejected");
	check(!v::from_9(-1.0, i64, error), "i64 double rejected");
	check(!v::from_8(QString::fromUtf8("\xd9\xa1"), u64, error), "unicode digit rejected");
	check(v::to_8(UINT64_MAX).toString() == QStringLiteral("18446744073709551615"), "u64 output");
	check(v::to_9(INT64_MIN).toString() == QStringLiteral("-9223372036854775808"), "i64 output");
	detail::Type_10 single{};
	detail::Type_11 real{};
	check(v::from_10(-0.0, single, error) && std::signbit(single), "f32 negative zero");
	check(std::signbit(v::to_10(single).toDouble()), "f32 negative zero output");
	check(v::from_11(-0.0, real, error) && std::signbit(real), "f64 negative zero");
	check(v::from_10(std::numeric_limits<double>::quiet_NaN(), single, error) && std::isnan(single),
	      "f32 NaN");
	check(v::from_10(std::numeric_limits<double>::infinity(), single, error) && std::isinf(single),
	      "f32 infinity");
	check(!v::from_10(double(std::numeric_limits<float>::max()) * 2, single, error),
	      "f32 finite overflow");
	check(v::from_11(std::numeric_limits<double>::quiet_NaN(), real, error) && std::isnan(real),
	      "f64 NaN");
	check(!v::from_10(QStringLiteral("1.0"), single, error), "string float rejected");
	detail::Type_13 unsignedEnum{};
	detail::Type_14 signedEnum{};
	check(v::from_13(QStringLiteral("18446744073709551614"), unsignedEnum, error) &&
	          static_cast<std::uint64_t>(unsignedEnum) == UINT64_MAX - 1,
	      "open u64 enum");
	check(v::from_14(QStringLiteral("-9223372036854775808"), signedEnum, error) &&
	          static_cast<std::int64_t>(signedEnum) == INT64_MIN,
	      "enum i64 min");
	check(v::to_13(unsignedEnum).toString() == QStringLiteral("18446744073709551614"),
	      "enum64 output");
	detail::Type_18 aggregate{};
	aggregate.proto = {true,      255,        -128,      65535, -32768, UINT32_MAX,
	                   INT32_MIN, UINT64_MAX, INT64_MIN, -0.0f, 1.5};
	aggregate.unsignedEnum = unsignedEnum;
	aggregate.signedEnum = signedEnum;
	aggregate.samples[0] = aggregate.samples[1] = aggregate.proto;
	auto value = v::to_18(aggregate);
	check(value.typeId() == QMetaType::QVariantMap &&
	          value.toMap().contains(QStringLiteral("__proto__")),
	      "raw hostile key");
	detail::Type_18 roundtrip{};
	check(v::from_18(value, roundtrip, error) && roundtrip.proto.u64 == UINT64_MAX &&
	          roundtrip.proto.i64 == INT64_MIN && roundtrip.samples[1].u32 == UINT32_MAX,
	      "aggregate roundtrip");
	check(std::signbit(roundtrip.proto.signals), "nested negative zero");
	auto fields = value.toMap();
	fields.remove(QStringLiteral("empty"));
	check(!v::from_18(fields, roundtrip, error), "missing member");
	fields.insert(QStringLiteral("unknown"), QVariantMap{});
	check(!v::from_18(fields, roundtrip, error), "same count unknown member");
	fields = value.toMap();
	auto samples = fields[QStringLiteral("samples")].toList();
	samples.removeLast();
	fields[QStringLiteral("samples")] = samples;
	check(!v::from_18(fields, roundtrip, error) && error.contains(QStringLiteral("samples")),
	      "fixed array length");
	fields = value.toMap();
	samples = fields[QStringLiteral("samples")].toList();
	auto scalarFields = samples[1].toMap();
	scalarFields[QStringLiteral("u64")] = 1.0;
	samples[1] = scalarFields;
	fields[QStringLiteral("samples")] = samples;
	check(!v::from_18(fields, roundtrip, error) &&
	          error.contains(QStringLiteral("samples: [1]: u64")),
	      "nested error path");
	check(!v::from_18(QVariantList{}, roundtrip, error), "wrong struct container");
	detail::Type_16 empty{};
	check(v::from_16(QVariantMap{}, empty, error), "empty struct");
	check(!v::from_16(QVariantList{}, empty, error), "empty struct not array");
	detail::Type_17 zero{};
	check(v::from_17(QVariantList{}, zero, error), "zero array");
	check(!v::from_17(QVariantList{QVariantMap{}}, zero, error), "zero array wrong length");
	std::cout << "qml values checks=" << passed << " failures=" << failed << '\n';
	return failed ? 1 : 0;
}
