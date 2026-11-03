// Real JavaScript snapshot/accessor validation before native DTO conversion.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include "QmlInput.hpp"
#include "QmlValues.hpp"
#include <QCoreApplication>
#include <iostream>

namespace input = sdk::device::qml::input;
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
	QJSEngine engine;
	QString error;
	auto snapshot = [&](const char* expression, QVariant& output) {
		const auto value = engine.evaluate(QString::fromLatin1(expression));
		return input::snapshot(QVariant::fromValue(value), output, error, &engine);
	};
	QVariant output;
	check(snapshot("({value:42,flag:true,nested:[1,'18446744073709551615']})", output),
	      "normal DTO snapshot");
	check(output.toMap()[QStringLiteral("nested")].toList()[1].toString() ==
	          QStringLiteral("18446744073709551615"),
	      "exact string");
	check(snapshot("var reads=0; ({get value(){ ++reads; return 42; }})", output),
	      "getter accepted");
	check(engine.evaluate(QStringLiteral("reads")).toInt() == 1, "getter once");
	check(
	    snapshot(
	        "var reads=0; Object.defineProperty([0], '0', {get:function(){ ++reads; return 42; }})",
	        output),
	    "array getter accepted");
	check(engine.evaluate(QStringLiteral("reads")).toInt() == 1 &&
	          output.toList()[0].toDouble() == 42,
	      "array getter once");
	for (const char* expression : {"({get value(){throw 42;}})",
	                               "({get value(){throw 'bad';}})",
	                               "({get value(){throw true;}})",
	                               "({get value(){throw null;}})",
	                               "({get value(){throw undefined;}})",
	                               "({get value(){throw new Error('bad');}})",
	                               "Object.defineProperty([0], '0', {get:function(){throw 42;}})",
	                               "[1,,2]",
	                               "var a=[1];a.extra=2;a",
	                               "var a=[1];a['01']=2;a",
	                               "var a=[1];Object.defineProperty(a,'extra',{value:2});a",
	                               "new Uint8Array([1,2])",
	                               "new ArrayBuffer(8)",
	                               "new Date()",
	                               "/x/",
	                               "new Number(1)",
	                               "new String('x')",
	                               "new Boolean(true)",
	                               "({callback:function(){}})",
	                               "({bad:null})",
	                               "({bad:undefined})",
	                               "var a={};a.self=a;a",
	                               "var a=[];a[0]=a;a",
	                               "Object.create({value:1})",
	                               "new Array(1000)",
	                               "({long:'123456789012345678901'})"}) {
		const QVariant sentinel(QStringLiteral("unchanged"));
		output = sentinel;
		check(!snapshot(expression, output) && output == sentinel && !engine.hasError(),
		      expression);
	}
	check(snapshot("Object.create(null)", output) && output.toMap().isEmpty(),
	      "null prototype object");
	check(snapshot("JSON.parse('{\"__proto__\":5}')", output) &&
	          output.toMap().contains(QStringLiteral("__proto__")),
	      "own proto key preserved");
	check(snapshot("-0", output) && std::signbit(output.toDouble()), "negative zero");
	check(snapshot("NaN", output) && std::isnan(output.toDouble()), "NaN");
	check(snapshot("Infinity", output) && std::isinf(output.toDouble()), "infinity");
	check(!snapshot("null", output), "root null rejected");
	check(!snapshot("undefined", output), "root undefined rejected");
	check(!snapshot("function(){}", output), "root function rejected");
	QVariant native =
	    QVariantMap{{QStringLiteral("nested"), QVariantList{true, 2.0, QStringLiteral("3")}}};
	check(input::snapshot(native, output, error) && output == native, "native no engine");
	native = QVariantMap{
	    {QStringLiteral("nested"), QVariant::fromValue(engine.evaluate(QStringLiteral("[1,2]")))}};
	check(input::snapshot(native, output, error, &engine) &&
	          output.toMap()[QStringLiteral("nested")].toList().size() == 2,
	      "native nested JS");
	check(!input::snapshot(native, output, error), "native nested JS no engine rejected");
	QJSEngine foreign;
	check(!input::snapshot(QVariant::fromValue(foreign.evaluate(QStringLiteral("({value:1})"))),
	                       output, error, &engine),
	      "foreign engine rejected");
	QObject object;
	check(!input::snapshot(QVariant::fromValue(engine.newQObject(&object)), output, error, &engine),
	      "QObject rejected");
	engine.throwError(QJSValue(42));
	check(!input::snapshot(1, output, error, &engine) && engine.hasError(),
	      "existing exception retained");
	static_cast<void>(engine.catchError());
	check(!snapshot("new Proxy({}, {ownKeys:function(){throw 42;}})", output) && !engine.hasError(),
	      "throwing proxy enumeration");
	check(!snapshot("new Proxy({}, {getPrototypeOf:function(){throw 42;}})", output) &&
	          !engine.hasError(),
	      "throwing proxy prototype");
	std::cout << "qml snapshot checks=" << passed << " failures=" << failed << '\n';
	return failed ? 1 : 0;
}
