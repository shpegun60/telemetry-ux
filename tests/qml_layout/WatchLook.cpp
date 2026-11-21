// The Watchlist design of 20 November 2026 (WL-9, section H): values that say
// what they are, writes that say what happened, a held refused text, one
// last-action line, the Name cap, the kinds' glyphs and guides, the change
// mark. A real WatchlistPanel and WatchRows over the fixture of section F - the
// demo device and the enum device.Mode - with a fake provider that formats
// with the real watch_format, refuses Gain above 10, answers when told and
// counts reads. At 100 % and 125 %; exact pixels at 100 %. No network or device
// I/O. MIT.
#include "ScriptSteps.hpp"
#include <Telemetry/UX/QmlTypes.hpp>
#include <Telemetry/UX/WatchFormat.hpp>
#include <Telemetry/UX/WatchKey.hpp>
#include <Telemetry/UX/WatchRows.hpp>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QTime>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QStandardItemModel>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlExpression>
#include <QtQml/QQmlProperty>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtTest/QTest>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
int checks = 0, warnings = 0;
void check(bool condition, const char* expression, int line)
{
	++checks;
	if (!condition)
		throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(x) check(bool(x), #x, __LINE__)

namespace ux = telemetry::ux;
namespace wf = telemetry::ux::watch_format;
using ux::WatchNumberFormat;
using ux::WatchScalar;

enum Role {
	Name = Qt::UserRole + 1, Status, Text, Writable, EditorKind, EditorText, EditorChoices, TypeText, Key, PathTypes,
	Access, ShownText, Format, ValueWord, ValueCode, HeldText, HeldReason, HeldKind, WriteState, WrittenText,
	WrittenAt, ChangedAt, WriteScope
};

// One watched value of the fixture: its path, where it is on the device, its
// type and its bits.
struct Value {
	QString path;
	quint32 field = 0, leaf = 0;
	ux::WatchValueType type;
	quint64 bits = 0;
	bool writable = false;
	QStringList types;
	QString kind = QStringLiteral("text");
	std::optional<WatchNumberFormat> own;
	QString heldText, heldReason, heldKind, writeState, writtenText;
	double writtenAt = 0, changedAt = 0;
	bool ownChange = false;
};

ux::WatchValueType typeOf(WatchScalar scalar, quint8 width, bool enumeration = false)
{
	return ux::WatchValueType{1, scalar, width, enumeration};
}
quint64 floatBits(float value)
{
	quint32 bits = 0;
	std::memcpy(&bits, &value, 4);
	return bits;
}
quint64 doubleBits(double value)
{
	quint64 bits = 0;
	std::memcpy(&bits, &value, 8);
	return bits;
}

class Model final : public QStandardItemModel {
public:
	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"},           {Status, "status"},         {Text, "text"},
		        {Writable, "writable"},   {EditorKind, "editorKind"}, {EditorText, "editorText"},
		        {EditorChoices, "editorChoices"}, {TypeText, "typeText"}, {Key, "watchKey"},
		        {PathTypes, "pathTypes"}, {Access, "access"},         {ShownText, "shownText"},
		        {Format, "format"},       {ValueWord, "valueWord"},   {ValueCode, "valueCode"},
		        {HeldText, "heldText"},   {HeldReason, "heldReason"}, {HeldKind, "heldKind"},
		        {WriteState, "writeState"}, {WrittenText, "writtenText"}, {WrittenAt, "writtenAt"},
		        {ChangedAt, "changedAt"}, {WriteScope, "writeScope"}};
	}
};

// The provider as the panel reads it, formatting with the real watch_format.
class Look final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* watchlist READ watchlist CONSTANT)
	Q_PROPERTY(QObject* watchRows READ watchRows CONSTANT)
	Q_PROPERTY(int watchCount READ watchCount NOTIFY changed)
	Q_PROPERTY(bool connected READ yes CONSTANT)
	Q_PROPERTY(bool watchActive MEMBER active NOTIFY changed)
	Q_PROPERTY(bool watchPending MEMBER pending NOTIFY changed)
	Q_PROPERTY(bool canWriteWatch READ canWrite NOTIFY changed)
	Q_PROPERTY(bool allowMutations MEMBER allow NOTIFY changed)
	Q_PROPERTY(bool watchWriting MEMBER writing NOTIFY changed)
	Q_PROPERTY(bool canAddWatch READ no CONSTANT)
	Q_PROPERTY(bool watchReadManySupported READ yes CONSTANT)
	Q_PROPERTY(bool watchRequiresExplicitIndividual READ no CONSTANT)
	Q_PROPERTY(bool watchIndividual MEMBER individual NOTIFY changed)
	Q_PROPERTY(int watchIntervalMs MEMBER interval NOTIFY changed)
	Q_PROPERTY(int watchIntervalMinimumMs READ one CONSTANT)
	Q_PROPERTY(QString watchStatus MEMBER status NOTIFY changed)
	Q_PROPERTY(QVariantMap watchPace MEMBER pace NOTIFY changed)
	Q_PROPERTY(QString watchWriteOutcome READ outcome NOTIFY changed)
	Q_PROPERTY(QString watchWriteKind MEMBER kind NOTIFY changed)
	Q_PROPERTY(double watchWriteAt MEMBER at NOTIFY changed)
	Q_PROPERTY(QString watchFormat READ format WRITE setFormat NOTIFY changed)
	Q_PROPERTY(int watchRevision MEMBER revision NOTIFY changed)
	Q_PROPERTY(telemetry::ux::UndoHistory* history MEMBER history NOTIFY changed)
public:
	Model model;
	ux::WatchRows rows;
	QVector<Value> values;
	QHash<quint64, QString> modeNames{{0, QStringLiteral("Off")}, {1, QStringLiteral("Run")}, {2, QStringLiteral("Idle")}};
	bool active = false, pending = false, allow = true, writing = false, individual = false;
	int interval = 1000, revision = 0;
	QString status, kind, base;
	QVariantMap pace;
	double at = 0;
	WatchNumberFormat form = WatchNumberFormat::Original;
	ux::UndoHistory* history = nullptr;
	ScriptSteps* steps = nullptr;
	// How the device answers: "auto" (Gain above 10 refused), "differs"
	// (clamped to 10), "lost" (no reply); replyMs < 0 waits for answer().
	QString mode = QStringLiteral("auto");
	int replyMs = 0;
	std::function<void()> held;
	// What the panel asked for.
	QStringList writes;
	QHash<quint32, int> reads;
	bool holds = false;  // the outcome line adds what the device holds

	Look()
	{
		const auto add = [this](const char* path, quint32 field, quint32 leaf, ux::WatchValueType type, quint64 bits,
		                        bool writable, QStringList types, const char* kind = "text") {
			Value value;
			value.path = QString::fromLatin1(path);
			value.field = field;
			value.leaf = leaf;
			value.type = type;
			value.bits = bits;
			value.writable = writable;
			value.types = std::move(types);
			value.kind = QString::fromLatin1(kind);
			values.append(value);
		};
		const QStringList config{QStringLiteral("struct (3)")};
		add("measure.Voltage", 1, 0, typeOf(WatchScalar::F32, 4), floatBits(230), false, {QStringLiteral("f32")});
		add("measure.Current", 2, 0, typeOf(WatchScalar::F32, 4), floatBits(2), false, {QStringLiteral("f32")});
		add("measure.Power", 3, 0, typeOf(WatchScalar::F32, 4), floatBits(460), false, {QStringLiteral("f32")});
		add("measure.Counter", 4, 0, typeOf(WatchScalar::U64, 8), 9096, false, {QStringLiteral("u64")});
		add("device.Config.Gain", 5, 1, typeOf(WatchScalar::F32, 4), floatBits(3), true, config + QStringList{QStringLiteral("f32")});
		add("device.Config.Enabled", 5, 2, typeOf(WatchScalar::Boolean, 1), 0, true, config + QStringList{QStringLiteral("bool")}, "bool");
		add("device.Config.SamplePeriodMs", 5, 3, typeOf(WatchScalar::U32, 4), 333, true, config + QStringList{QStringLiteral("u32")});
		add("device.Mode", 6, 0, typeOf(WatchScalar::U8, 1, true), 2, true, {QStringLiteral("enum (3)")}, "enum");
		add("device.Threshold", 7, 0, typeOf(WatchScalar::U8, 1), 100, true, {QStringLiteral("u8")});
		add("signals.Signal01", 8, 0, typeOf(WatchScalar::F64, 8), doubleBits(230), false, {QStringLiteral("f64")});
		add("signals.Signal02", 9, 0, typeOf(WatchScalar::F64, 8), doubleBits(231), false, {QStringLiteral("f64")});
		for (int row = 0; row < values.size(); ++row)
			model.appendRow(new QStandardItem);
		refill();
		rows.setSource(&model);
		connect(&tick, &QTimer::timeout, this, [this] {
			const int row = rowOf(QStringLiteral("measure.Counter"));
			if (row >= 0)
				device(row, values[row].bits + 1);
		});
		tick.setInterval(100);
	}
	QAbstractItemModel* watchlist() { return &model; }
	QObject* watchRows() { return &rows; }
	int watchCount() const { return int(values.size()); }
	bool yes() const { return true; }
	bool no() const { return false; }
	int one() const { return 1; }
	bool canWrite() const { return allow && !writing; }
	QString format() const { return wf::name(form); }
	void setFormat(const QString& name)
	{
		if (const auto parsed = wf::parse(name); parsed && *parsed != form) {
			form = *parsed;
			++revision;
			refill();
			Q_EMIT changed();
		}
	}
	int rowOf(const QString& path) const
	{
		for (int row = 0; row < values.size(); ++row)
			if (values[row].path == path)
				return row;
		return -1;
	}
	ux::WatchValueBits bitsOf(const Value& value) const
	{
		ux::WatchValueBits out;
		out.type = value.type;
		std::array<std::byte, 8> bytes{};
		for (unsigned i = 0; i < value.type.width; ++i)
			bytes[i] = std::byte((value.bits >> (8 * i)) & 0xff);
		out.assign(bytes.data(), value.type.width);
		return out;
	}
	WatchNumberFormat formOf(const Value& value) const { return value.own.value_or(form); }
	const QHash<quint64, QString>* namesOf(const Value& value) const
	{
		return value.type.enumeration ? &modeNames : nullptr;
	}
	QString shown(int row) const
	{
		const auto& value = values[row];
		return wf::text(bitsOf(value), formOf(value), namesOf(value));
	}
	// What a typed text writes, in the row's form.
	QString shownFor(int row, const QString& text) const
	{
		const auto& value = values[row];
		const auto typed = wf::isHex(text) ? wf::fromHex(text, value.type).canonical : text;
		const auto encoded = wf::encode(typed, value.type, formOf(value));
		return encoded.error.isEmpty() ? wf::text(encoded.value, formOf(value), namesOf(value)) : text;
	}
	void fill(int row) { fillItem(model.item(row), values[row]); }
	// A new row comes whole, as the real model's do: its path and key at once.
	QStandardItem* made(const Value& value)
	{
		auto* item = new QStandardItem;
		fillItem(item, value);
		return item;
	}
	void fillItem(QStandardItem* item, const Value& value)
	{
		const auto bits = bitsOf(value);
		const auto parts = wf::parts(bits, formOf(value), namesOf(value));
		const auto original = wf::text(bits, WatchNumberFormat::Original, namesOf(value));
		QVariantMap scope;
		if (value.leaf)
			scope = {{QStringLiteral("field"), QStringLiteral("Config")}, {QStringLiteral("others"), 2},
			         {QStringLiteral("kind"), QStringLiteral("members")}};
		QVariantList choices;
		if (value.kind == QLatin1String("bool"))
			choices = {QVariantMap{{QStringLiteral("label"), QStringLiteral("false (0)")}, {QStringLiteral("text"), QStringLiteral("false")}},
			           QVariantMap{{QStringLiteral("label"), QStringLiteral("true (1)")}, {QStringLiteral("text"), QStringLiteral("true")}}};
		if (value.kind == QLatin1String("enum"))
			for (auto it = modeNames.cbegin(); it != modeNames.cend(); ++it)
				choices.append(QVariantMap{{QStringLiteral("label"), it.value() + QStringLiteral(" (%1)").arg(it.key())},
				                           {QStringLiteral("text"), QString::number(it.key())}});
		const auto editor = value.kind == QLatin1String("text") && formOf(value) == WatchNumberFormat::Hex
		                        ? wf::text(bits, WatchNumberFormat::Hex)
		                        : value.kind == QLatin1String("bool") ? QString(value.bits ? "true" : "false")
		                        : value.kind == QLatin1String("enum") ? QString::number(value.bits)
		                                                              : wf::parts(bits, WatchNumberFormat::Original).code;
		item->setData(value.path, Name);
		item->setData(QStringLiteral("Ok"), Status);
		item->setData(original, Text);
		item->setData(value.writable, Writable);
		item->setData(value.kind, EditorKind);
		item->setData(editor, EditorText);
		item->setData(choices, EditorChoices);
		item->setData(value.types.last(), TypeText);
		item->setData(qulonglong(value.leaf ? ux::watch_key::member(value.field, value.leaf) : ux::watch_key::whole(value.field)), Key);
		item->setData(value.types, PathTypes);
		item->setData(value.writable ? QStringLiteral("rw") : QStringLiteral("r-"), Access);
		item->setData(wf::text(bits, formOf(value), namesOf(value)), ShownText);
		item->setData(value.own ? wf::name(*value.own) : QString{}, Format);
		item->setData(parts.word, ValueWord);
		item->setData(parts.word.isEmpty() ? QString{} : parts.code, ValueCode);
		item->setData(value.heldText, HeldText);
		item->setData(value.heldReason, HeldReason);
		item->setData(value.heldKind, HeldKind);
		item->setData(value.writeState, WriteState);
		item->setData(value.writtenText, WrittenText);
		item->setData(value.writtenAt, WrittenAt);
		item->setData(value.changedAt, ChangedAt);
		item->setData(scope, WriteScope);
	}
	void refill()
	{
		for (int row = 0; row < values.size(); ++row)
			fill(row);
	}
	// The device's own change of a value: marked, unless it is the user's write.
	void device(int row, quint64 bits)
	{
		auto& value = values[row];
		if (value.bits != bits && !std::exchange(value.ownChange, false))
			value.changedAt = double(QDateTime::currentMSecsSinceEpoch());
		value.bits = bits;
		fill(row);
	}
	QString outcome() const
	{
		if (!holds || base.isEmpty())
			return base;
		const int row = rowOf(lastPath);
		return row >= 0 && !pending ? base + QStringLiteral(" · it holds ") + shown(row) : base;
	}
	QString lastPath;
	QTimer tick;
	void readOnce(quint32 field, int ms)
	{
		++reads[field];
		pending = true;
		Q_EMIT changed();
		QTimer::singleShot(ms, this, [this] {
			pending = false;
			Q_EMIT changed();
		});
	}
	Q_INVOKABLE QVariantMap watchEditPreview(int row, const QString& text) const
	{
		if (row < 0 || row >= values.size())
			return {};
		const auto& value = values[row];
		const auto typed = wf::isHex(text) ? wf::fromHex(text, value.type, formOf(value)) : wf::Parsed{text, {}};
		if (!typed.error.isEmpty())
			return {{QStringLiteral("error"), typed.error}};
		const auto encoded = wf::encode(typed.canonical, value.type, formOf(value));
		if (!encoded.error.isEmpty())
			return {{QStringLiteral("error"), encoded.error}};
		if (wf::isHex(text))
			return {{QStringLiteral("preview"), QStringLiteral("= ") + wf::text(encoded.value, WatchNumberFormat::Original, namesOf(value))}};
		return {};
	}
	Q_INVOKABLE bool writeWatch(int row, const QString& text)
	{
		if (row < 0 || row >= values.size() || !canWrite())
			return false;
		auto& value = values[row];
		const auto typed = wf::isHex(text) ? wf::fromHex(text, value.type).canonical : text.trimmed();
		const auto encoded = wf::encode(typed, value.type);
		if (!encoded.error.isEmpty())
			return false;
		writes << value.path + QStringLiteral(" = ") + text;
		quint64 bits = 0;
		for (unsigned i = 0; i < value.type.width; ++i)
			bits |= quint64(std::to_integer<unsigned char>(encoded.value.bytes[i])) << (8 * i);
		const auto written = shownFor(row, text);
		value.writeState = QStringLiteral("pending");
		value.writtenText = written;
		value.writtenAt = double(QDateTime::currentMSecsSinceEpoch());
		value.ownChange = true;
		fill(row);
		writing = true;
		kind = QStringLiteral("writing");
		at = 0;
		holds = false;
		lastPath = value.path;
		base = QStringLiteral("Writing %1 = %2…").arg(value.path, written);
		Q_EMIT changed();
		held = [this, row, text, bits, written] { answerWrite(row, text, bits, written); };
		if (replyMs >= 0)
			QTimer::singleShot(replyMs, this, [this] { answer(); });
		return true;
	}
	void answer()
	{
		if (auto run = std::exchange(held, {}))
			run();
	}
	void answerWrite(int row, const QString& text, quint64 bits, const QString& written)
	{
		auto& value = values[row];
		const auto time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
		float number = 0;
		std::memcpy(&number, &bits, 4);
		writing = false;
		at = double(QDateTime::currentMSecsSinceEpoch());
		const bool gain = value.path == QLatin1String("device.Config.Gain");
		if (mode == QLatin1String("lost")) {
			value.writeState = QStringLiteral("unconfirmed");
			kind = QStringLiteral("unconfirmed");
			base = QStringLiteral("Write not confirmed %1 · %2 = %3 · the reply was lost; it may have been written")
			           .arg(time, value.path, written);
			holds = true;
			fill(row);
			if (!active)
				readOnce(value.field, 0);
		} else if (mode == QLatin1String("differs") && gain && number > 10) {
			value.writeState = QStringLiteral("differs");
			value.heldText.clear();
			device(row, floatBits(10));
			kind = QStringLiteral("differs");
			base = QStringLiteral("Wrote %1 · %2 = %3 · the device reads back %4").arg(time, value.path, written, shown(row));
		} else if (gain && number > 10) {
			value.writeState.clear();
			value.ownChange = false;
			value.heldText = text;
			value.heldReason = QStringLiteral("Refused: the device does not take this value");
			value.heldKind = QStringLiteral("refused");
			fill(row);
			kind = QStringLiteral("refused");
			base = QStringLiteral("Write refused %1 · %2 = %3 · the device does not take this value").arg(time, value.path, written);
			holds = true;
			if (!active)
				readOnce(value.field, readMs);
		} else {
			value.writeState = QStringLiteral("confirmed");
			value.heldText.clear();
			value.heldReason.clear();
			value.heldKind.clear();
			device(row, bits);
			kind = QStringLiteral("confirmed");
			base = QStringLiteral("Wrote %1 · %2 = %3 · read back %4").arg(time, value.path, written, shown(row));
		}
		Q_EMIT changed();
	}
	int readMs = 0;
	Q_INVOKABLE bool holdWatchText(int row, const QString& text, const QString& reason)
	{
		if (row < 0 || row >= values.size())
			return false;
		values[row].heldText = text;
		values[row].heldReason = reason;
		values[row].heldKind = QStringLiteral("invalid");
		fill(row);
		return true;
	}
	Q_INVOKABLE bool dropWatchHeld(int row)
	{
		if (row < 0 || row >= values.size())
			return false;
		auto& value = values[row];
		const bool had = !value.heldText.isEmpty() || value.writeState == QLatin1String("differs");
		value.heldText.clear();
		value.heldReason.clear();
		value.heldKind.clear();
		if (value.writeState == QLatin1String("differs"))
			value.writeState.clear();
		fill(row);
		return had;
	}
	Q_INVOKABLE bool chooseWatchFormat(const QString& name)
	{
		setFormat(name);
		return true;
	}
	Q_INVOKABLE bool setWatchRowFormat(const QVariantList& list, const QString& name)
	{
		for (const auto& row : list) {
			const int at = row.toInt();
			if (at >= 0 && at < values.size()) {
				values[at].own = name.isEmpty() ? std::nullopt : wf::parse(name);
				fill(at);
			}
		}
		++revision;
		Q_EMIT changed();
		return true;
	}
	Q_INVOKABLE QVariantMap removeWatchRows(const QVariantList& list)
	{
		QList<int> sorted;
		for (const auto& row : list)
			sorted << row.toInt();
		std::sort(sorted.begin(), sorted.end(), std::greater<int>());
		QVector<QPair<int, Value>> gone;
		for (const int row : sorted) {
			gone.prepend({row, values[row]});
			values.removeAt(row);
			model.removeRow(row);
		}
		++revision;
		Q_EMIT changed();
		const int serial = steps && history ? steps->record(history, QStringLiteral("Remove %1 values from the Watchlist").arg(gone.size()),
		                                                     [this, gone] { restore(gone); })
		                                    : 0;
		return {{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("changed"), int(gone.size())},
		        {QStringLiteral("serial"), serial}};
	}
	void restore(const QVector<QPair<int, Value>>& gone)
	{
		for (const auto& [row, value] : gone) {
			values.insert(row, value);
			model.insertRow(row, made(value));
		}
		++revision;
		Q_EMIT changed();
	}
	// More rows, for a list that scrolls; and the one f64 whose Dec is longest.
	void addRow(const QString& path, quint32 field, ux::WatchValueType type, quint64 bits, const QString& typeText)
	{
		Value value;
		value.path = path;
		value.field = field;
		value.type = type;
		value.bits = bits;
		value.types = {typeText};
		values.append(value);
		model.appendRow(made(value));
		Q_EMIT changed();
	}
	void removeFrom(int row)
	{
		while (values.size() > row) {
			values.removeLast();
			model.removeRow(model.rowCount() - 1);
		}
		Q_EMIT changed();
	}
	Q_INVOKABLE void removeWatch(int row) { removeWatchRows({row}); }
	Q_INVOKABLE void refreshWatch() {}
	Q_INVOKABLE void startWatch()
	{
		active = true;
		Q_EMIT changed();
	}
	Q_INVOKABLE void stopWatch()
	{
		active = false;
		tick.stop();
		Q_EMIT changed();
	}
	Q_INVOKABLE void clearWatch() {}
	Q_INVOKABLE void addWatch() {}
Q_SIGNALS:
	void changed();
};

// The window and what the checks look at.
struct View {
	QQuickWindow* window = nullptr;
	QQuickItem* content = nullptr;
	QQuickItem* panel = nullptr;
	QQuickItem* list = nullptr;
};

void settle(int ms = 40)
{
	QCoreApplication::processEvents();
	QTest::qWait(ms);
}
QQuickItem* named(QQuickItem* root, const char* name, bool visibleOnly = true)
{
	if (!root || (visibleOnly && !root->isVisible()))
		return nullptr;
	if (root->objectName() == QLatin1String(name))
		return root;
	for (auto* child : root->childItems())
		if (auto* found = named(child, name, visibleOnly))
			return found;
	return nullptr;
}
QQuickItem* shown(QQuickItem* root, const char* name)
{
	auto* found = named(root, name);
	if (!found)
		throw std::runtime_error(std::string("not shown: ") + name);
	return found;
}
void collect(QQuickItem* root, const char* name, QList<QQuickItem*>& out)
{
	if (!root || !root->isVisible())
		return;
	if (root->objectName() == QLatin1String(name))
		out.append(root);
	for (auto* child : root->childItems())
		collect(child, name, out);
}
QList<QQuickItem*> all(QQuickItem* root, const char* name)
{
	QList<QQuickItem*> out;
	collect(root, name, out);
	return out;
}
QString text(QQuickItem* item) { return item ? item->property("text").toString() : QString{}; }
double sx(QQuickItem* item, double x = 0) { return item->mapToScene(QPointF(x, 0)).x(); }
double sy(QQuickItem* item, double y = 0) { return item->mapToScene(QPointF(0, y)).y(); }
bool near(double a, double b, double tolerance = 0.75) { return std::abs(a - b) <= tolerance; }
QColor color(QQuickItem* item, const char* property = "color")
{
	return QQmlProperty::read(item, QString::fromLatin1(property)).value<QColor>();
}
QVariant eval(QObject* scope, const QString& expression)
{
	QQmlExpression e(qmlContext(scope), scope, expression);
	const auto value = e.evaluate();
	if (e.hasError())
		throw std::runtime_error(e.error().toString().toStdString());
	return value;
}
QQuickItem* rowAt(QQuickItem* list, int row)
{
	const std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* root) -> QQuickItem* {
		if (root->objectName() == QLatin1String("watchlistRow") && root->property("index").toInt() == row && root->isVisible())
			return root;
		for (auto* child : root->childItems())
			if (auto* found = find(child))
				return found;
		return nullptr;
	};
	return find(list);
}
// The shown row whose name is path (a value's whole path, a node's path).
int rowIndex(QQuickItem* list, const QString& path)
{
	const int count = list->property("count").toInt();
	for (int row = 0; row < count; ++row) {
		QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 0));
		list->setProperty("contentY", list->property("contentY"));
		settle(0);
		if (auto* item = rowAt(list, row); item && item->property("name").toString() == path)
			return row;
	}
	return -1;
}
QQuickItem* rowOf(QQuickItem* list, const QString& path)
{
	const int row = rowIndex(list, path);
	if (row < 0)
		throw std::runtime_error("no row: " + path.toStdString());
	QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 2));
	settle(0);
	return rowAt(list, row);
}
void key(QQuickWindow* window, Qt::Key k, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QTest::keyClick(window, k, modifiers);
	settle();
}
void typeText(QQuickWindow* window, const char* typed)
{
	for (const char* p = typed; *p; ++p)
		QTest::keyClick(window, *p);
	settle(60);
}
void click(QQuickWindow* window, QQuickItem* target)
{
	CHECK(target && target->isVisible());
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
	                  target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
	settle();
}
void shot(const View& v, const QString& output, const QString& name)
{
	if (output.isEmpty())
		return;
	QDir().mkpath(output);
	CHECK(v.window->grabWindow().save(QDir(output).filePath(name)));
}
void resize(View& v, int width, int height = 520)
{
	v.window->resize(width, height);
	settle(80);
}
void showTree(View& v, bool tree)
{
	QMetaObject::invokeMethod(v.panel, "showTree", Q_ARG(QVariant, tree));
	settle(80);
}
double prop(QObject* object, const char* name) { return object->property(name).toDouble(); }
// Makes a row current and selected, as a click does.
void current(View& v, QQuickItem* row)
{
	QMetaObject::invokeMethod(v.panel, "moveTo", Q_ARG(QVariant, row->property("index")),
	                          Q_ARG(QVariant, QStringLiteral("select")));
}
// Opens a row's editor by F2 with the keys on the list.
QQuickItem* edit(View& v, const QString& path)
{
	auto* item = rowOf(v.list, path);
	current(v, item);
	v.list->forceActiveFocus();
	settle();
	key(v.window, Qt::Key_F2);
	CHECK(item->property("editing").toBool());
	return item;
}
QObject* cellOf(QQuickItem* row)
{
	auto* cell = named(row, "valueCell", false);
	if (!cell)
		throw std::runtime_error("no value cell");
	return cell;
}

// Line 2 whole (check 5): the row as tall as its caption's laid-out lines,
// the caption above the focus frame, uncut, inside the column group.
void lineTwoWhole(View& v, QQuickItem* row, int lines, bool exact)
{
	auto* caption = shown(row, "valueCaption");
	const double contentHeight = std::ceil(prop(caption, "contentHeight"));
	const double rowHeight = eval(v.window, QStringLiteral("token('rowHeight')")).toDouble();
	const double tight = eval(v.window, QStringLiteral("token('tight')")).toDouble();
	const double spacing = eval(v.window, QStringLiteral("token('spacing')")).toDouble();
	// 22 + 3 + the caption's laid-out lines + 4 (45, 61, 77 px where a
	// caption line is 16 px; the line's height is the font's).
	CHECK(near(row->height(), rowHeight + tight + contentHeight + spacing, 1.01));
	// At 100 % the lines the design counts; larger, up to three, then cut
	// with the whole in its tooltip.
	const int count = caption->property("lineCount").toInt();
	if (exact)
		CHECK(count == lines && rowHeight == 22 && tight == 3 && spacing == 4);
	else
		CHECK(count >= lines && count <= 3);
	const double bottom = caption->mapToItem(row, QPointF(0, contentHeight)).y();
	CHECK(bottom <= row->height() - spacing + 0.5);
	CHECK(!caption->property("truncated").toBool() || (!exact && count == 3));
	auto* frame = named(row, "watchRowFocusFrame");
	if (frame) {
		// The frame's border is 2 px at a 1 px inset: its bottom edge lies in
		// the row's last 3 px, which the caption never reaches.
		const double frameTop = frame->mapToItem(row, QPointF(0, frame->height() - 2)).y();
		CHECK(bottom <= frameTop + 0.5);
	}
	CHECK(sx(caption, caption->width()) <= sx(v.list) + prop(v.panel, "groupRight") + 1);
}

void run(const QString& output, double points, int& screens)
{
	const bool exact = points == 10;
	const QString suffix = exact ? QString{} : QStringLiteral("-125");
	Look look;
	ScriptSteps steps;
	look.steps = &steps;
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("lookProvider"), &look);
	engine.load(QUrl(QStringLiteral("qrc:/WatchLook.qml")));
	CHECK(!engine.rootObjects().isEmpty());
	View v;
	v.window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
	CHECK(v.window && QTest::qWaitForWindowExposed(v.window));
	QMetaObject::invokeMethod(v.window, "setBody", Q_ARG(QVariant, points));
	settle(120);
	v.content = v.window->contentItem();
	v.panel = shown(v.content, "lookPanel");
	v.list = shown(v.content, "watchlistView");
	const auto tok = [&](const char* name) { return eval(v.window, QStringLiteral("token('%1')").arg(QLatin1String(name))).toDouble(); };
	const auto theme = [&](const char* name) { return QColor(eval(v.window, QStringLiteral("color('%1')").arg(QLatin1String(name))).toString()); };
	const double padding = tok("padding"), gap = tok("columnGap"), indent = tok("indent"), body = tok("bodyPixels");
	const double nameFloor = tok("nameFloor");

	// ---- 11. Grid: one left line, fills that bleed ----
	resize(v, 560);
	CHECK(near(sx(shown(v.content, "refreshWatchButton")), padding));
	{
		auto* name = shown(rowOf(v.list, QStringLiteral("measure.Voltage")), "watchRowName");
		CHECK(near(sx(name), padding));
		auto* header = shown(v.content, "watchColumnName");
		CHECK(near(sx(header) + prop(header, "leftPadding"), padding));
		auto* fill = shown(rowOf(v.list, QStringLiteral("measure.Voltage")), "watchRowFill");
		CHECK(near(sx(fill), 0) && near(sx(fill, fill->width()), v.window->width()));
		CHECK(!named(v.list, "kindIcon"));  // 13: the list has no glyph
	}
	shot(v, output, QStringLiteral("watch-wl9-list-560%1.png").arg(suffix));
	showTree(v, true);
	{
		auto* measure = rowOf(v.list, QStringLiteral("measure"));
		CHECK(near(sx(shown(measure, "watchRowChevron")), padding));
		auto* header = shown(v.content, "watchColumnName");
		const double depth0 = sx(shown(measure, "watchRowName"));
		CHECK(near(sx(header) + prop(header, "leftPadding"), depth0));
		if (exact)
			CHECK(near(depth0, 42));
	}

	// ---- 1. rw on every row of a Field, in the Field's colour ----
	{
		auto* config = rowOf(v.list, QStringLiteral("device.Config"));
		const double rwX = sx(shown(config, "watchRowAccess"));
		for (const char* path : {"device.Config.Gain", "device.Config.Enabled", "device.Config.SamplePeriodMs"}) {
			auto* row = rowOf(v.list, QString::fromLatin1(path));
			auto* access = shown(row, "watchRowAccess");
			CHECK(text(access) == QStringLiteral("rw") && near(sx(access), rwX));
		}
		CHECK(!named(rowOf(v.list, QStringLiteral("device")), "watchRowAccess"));
		const auto spoken = eval(rowOf(v.list, QStringLiteral("device.Config.Gain")), QStringLiteral("Accessible.name")).toString();
		CHECK(spoken.contains(QStringLiteral("member of Config")) && spoken.contains(QStringLiteral("read and write")));
	}

	// ---- 13. Kinds: glyphs per kind, weight, guides ----
	{
		const auto icon = [&](const char* path) { return shown(rowOf(v.list, QString::fromLatin1(path)), "kindIcon"); };
		CHECK(icon("measure")->property("name").toString() == QStringLiteral("group"));
		CHECK(icon("device.Config")->property("name").toString() == QStringLiteral("struct"));
		CHECK(icon("device.Config.Gain")->property("name").toString() == QStringLiteral("value"));
		CHECK(icon("device.Mode")->property("name").toString() == QStringLiteral("value"));
		CHECK(near(sx(icon("device.Config")), sx(icon("device.Mode"))));
		CHECK(near(sx(icon("measure")), sx(icon("device"))));
		CHECK(near(sx(icon("device.Config")) - sx(icon("device")), indent));
		const auto weight = [&](const char* path) {
			return shown(rowOf(v.list, QString::fromLatin1(path)), "watchRowLeaf")->property("font").value<QFont>().weight();
		};
		CHECK(weight("measure") == QFont::DemiBold && weight("device") == QFont::DemiBold);
		for (const char* path : {"device.Config", "device.Config.Gain", "device.Mode", "measure.Voltage"})
			CHECK(weight(path) == QFont::Normal);
		auto* gainRow = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		const auto guides = all(gainRow, "indentGuide");
		CHECK(guides.size() == 2);
		for (int a = 0; a < guides.size(); ++a)
			CHECK(near(sx(guides[a]), std::round(padding + a * indent + indent / 2), 0.01));
		if (exact)
			CHECK(near(sx(guides[0]), 14, 0.01) && near(sx(guides[1]), 30, 0.01));
		CHECK(all(rowOf(v.list, QStringLiteral("measure")), "indentGuide").isEmpty());
		// Config current: its members carry the brighter guide.
		auto* config = rowOf(v.list, QStringLiteral("device.Config"));
		current(v, config);
		v.list->forceActiveFocus();
		settle(80);
		const auto outline = theme("outline");
		for (const char* path : {"device.Config.Gain", "device.Config.SamplePeriodMs"}) {
			const auto lines = all(rowOf(v.list, QString::fromLatin1(path)), "indentGuide");
			CHECK(lines.size() == 2 && color(lines[1]) == outline && color(lines[0]) != outline);
		}
		const auto mode = all(rowOf(v.list, QStringLiteral("device.Mode")), "indentGuide");
		CHECK(mode.size() == 1 && color(mode[0]) != outline);
	}
	shot(v, output, QStringLiteral("watch-wl9-tree-560%1.png").arg(suffix));

	// ---- 2. Words and codes; 3. floats; 4. the folded summary ----
	{
		const auto word = [&](const char* path) { return text(shown(rowOf(v.list, QString::fromLatin1(path)), "watchValueWord")); };
		const auto code = [&](const char* path) { return text(shown(rowOf(v.list, QString::fromLatin1(path)), "watchValueCode")); };
		CHECK(word("device.Config.Enabled") + code("device.Config.Enabled") == QStringLiteral("false (0)"));
		CHECK(word("device.Mode") + code("device.Mode") == QStringLiteral("Idle (2)"));
		look.setFormat(QStringLiteral("dec"));
		settle(80);
		CHECK(word("device.Config.Enabled") + code("device.Config.Enabled") == QStringLiteral("false (0)"));
		CHECK(word("device.Mode") + code("device.Mode") == QStringLiteral("Idle (2)"));
		look.setFormat(QStringLiteral("hex"));
		settle(80);
		CHECK(word("device.Config.Enabled") + code("device.Config.Enabled") == QStringLiteral("false (0x00)"));
		CHECK(word("device.Mode") + code("device.Mode") == QStringLiteral("Idle (0x02)"));
		look.setFormat(QStringLiteral("original"));
		settle(80);
		auto* modeRow = rowOf(v.list, QStringLiteral("device.Mode"));
		auto* codeText = shown(modeRow, "watchValueCode");
		CHECK(color(codeText) == theme("textSecondary"));
		CHECK(near(sx(codeText, codeText->width()), sx(shown(modeRow, "watchValueBox"), prop(v.panel, "valueColumn"))));
		current(v, modeRow);
		v.list->forceActiveFocus();
		QMetaObject::invokeMethod(&look.rows, "select", Q_ARG(int, modeRow->property("index").toInt()), Q_ARG(bool, false));
		settle();
		CHECK(color(shown(modeRow, "watchValueCode")) == theme("selectedForeground"));
		// A long word in a narrow Value column: the word elides, its code never.
		// The dock's narrowest gives the column about 100 px (0.4 of the
		// list), less than the word and its code need.
		{
			look.modeNames[2] = QStringLiteral("IdleWaitingForTrigger");
			look.fill(look.rowOf(QStringLiteral("device.Mode")));
			showTree(v, false);
			QMetaObject::invokeMethod(v.panel, "forgetMeasured");
			resize(v, 120);
			settle(120);
			CHECK(prop(v.panel, "valueColumn") <= prop(v.panel, "listWidth") * 0.4 + 1 &&
			      prop(v.panel, "valueColumn") + 20 < prop(v.panel, "valueSeen"));
			auto* narrow = rowOf(v.list, QStringLiteral("device.Mode"));
			auto* longWord = shown(narrow, "watchValueWord");
			auto* wholeCode = shown(narrow, "watchValueCode");
			CHECK(longWord->property("truncated").toBool() && text(wholeCode) == QStringLiteral(" (2)"));
			CHECK(near(wholeCode->width(), wholeCode->property("implicitWidth").toDouble(), 0.5));
			CHECK(near(sx(wholeCode, wholeCode->width()), sx(shown(narrow, "watchValueBox"), prop(v.panel, "valueColumn"))));
			look.modeNames[2] = QStringLiteral("Idle");
			look.fill(look.rowOf(QStringLiteral("device.Mode")));
			QMetaObject::invokeMethod(v.panel, "forgetMeasured");
			resize(v, 560);
			showTree(v, true);
		}
		// 3. A float in every form.
		const int gain = look.rowOf(QStringLiteral("device.Config.Gain"));
		look.values[gain].bits = floatBits(0.1f);
		look.fill(gain);
		settle();
		const auto value = [&]() { return text(shown(rowOf(v.list, QStringLiteral("device.Config.Gain")), "watchValueText")); };
		CHECK(value() == QStringLiteral("0.1"));
		look.setFormat(QStringLiteral("dec"));
		settle();
		CHECK(value() == QStringLiteral("0.100000001"));
		look.setFormat(QStringLiteral("hex"));
		settle();
		CHECK(value() == QStringLiteral("0x3DCCCCCD"));
		look.setFormat(QStringLiteral("original"));
		look.values[gain].bits = floatBits(3);
		look.fill(gain);
		settle();
		// 4. Folded Config: words without codes.
		auto* config = rowOf(v.list, QStringLiteral("device.Config"));
		QMetaObject::invokeMethod(&look.rows, "setExpanded", Q_ARG(int, config->property("index").toInt()), Q_ARG(bool, false));
		settle(80);
		CHECK(text(shown(rowOf(v.list, QStringLiteral("device.Config")), "watchRowSummary"))
		      == QStringLiteral("{Gain 3, Enabled false, SamplePeriodMs 333}"));
		QMetaObject::invokeMethod(&look.rows, "setExpanded", Q_ARG(int, config->property("index").toInt()), Q_ARG(bool, true));
		settle(80);
	}

	// ---- 5. Line 2 whole; 19. Enter refused, no trap ----
	{
		auto* period = edit(v, QStringLiteral("device.Config.SamplePeriodMs"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "123455555555555");
		auto* cell = cellOf(period);
		const int before = cell->property("announced").toInt();
		key(v.window, Qt::Key_Return);
		CHECK(period->property("editing").toBool() && look.writes.isEmpty());
		auto* caption = shown(period, "valueCaption");
		CHECK(text(caption) == QStringLiteral("u32 takes 0 to 4294967295 (0x00000000 to 0xFFFFFFFF)."));
		CHECK(color(caption) == theme("error"));
		CHECK(color(named(period, "scalarEditorBorder", false), "border.color") == theme("error"));
		CHECK(cell->property("announced").toInt() == before + 1);
		key(v.window, Qt::Key_Return, Qt::ControlModifier);
		CHECK(period->property("editing").toBool() && look.writes.isEmpty() && cell->property("announced").toInt() == before + 2);
		shot(v, output, QStringLiteral("watch-wl9-edit-error-560%1.png").arg(suffix));
		// Esc always leaves, writing nothing.
		key(v.window, Qt::Key_Escape);
		CHECK(!period->property("editing").toBool() && look.writes.isEmpty() && v.list->hasActiveFocus());
		// Again, and a click on another row leaves it: held, struck.
		period = edit(v, QStringLiteral("device.Config.SamplePeriodMs"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "123455555555555");
		click(v.window, shown(rowOf(v.list, QStringLiteral("measure.Voltage")), "watchRowLeaf"));
		period = rowOf(v.list, QStringLiteral("device.Config.SamplePeriodMs"));
		CHECK(!period->property("editing").toBool() && look.writes.isEmpty());
		auto* held = shown(period, "valueHeldText");
		CHECK(text(held) == QStringLiteral("123455555555555") && held->property("font").value<QFont>().strikeOut());
		CHECK(text(shown(period, "valueCaption")).startsWith(QStringLiteral("Not written: u32 takes 0 to 4294967295 (0x00000000 to 0xFFFFFFFF) · device holds 333")));
		current(v, period);
		v.list->forceActiveFocus();
		settle(80);
		lineTwoWhole(v, period, 2, exact);
		shot(v, output, QStringLiteral("watch-wl9-held-560%1.png").arg(suffix));
		resize(v, 320);
		period = rowOf(v.list, QStringLiteral("device.Config.SamplePeriodMs"));
		lineTwoWhole(v, period, 3, exact);
		shot(v, output, QStringLiteral("watch-wl9-held-320%1.png").arg(suffix));
		resize(v, 1200);
		period = rowOf(v.list, QStringLiteral("device.Config.SamplePeriodMs"));
		lineTwoWhole(v, period, 2, exact);
		resize(v, 560);
		// Esc on the row drops it.
		period = rowOf(v.list, QStringLiteral("device.Config.SamplePeriodMs"));
		current(v, period);
		v.list->forceActiveFocus();
		settle();
		key(v.window, Qt::Key_Escape);
		CHECK(!named(rowOf(v.list, QStringLiteral("device.Config.SamplePeriodMs")), "valueHeldText"));
		// 19 again in a u8 row: "300", Enter and Ctrl+Enter refused, then Tab
		// leaves, holds it and opens the next writable cell.
		auto* threshold = edit(v, QStringLiteral("device.Threshold"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "300");
		key(v.window, Qt::Key_Return);
		key(v.window, Qt::Key_Return, Qt::ControlModifier);
		CHECK(threshold->property("editing").toBool() && look.writes.isEmpty());
		key(v.window, Qt::Key_Tab);
		threshold = rowOf(v.list, QStringLiteral("device.Threshold"));
		CHECK(!threshold->property("editing").toBool() && look.writes.isEmpty());
		CHECK(text(shown(threshold, "valueHeldText")) == QStringLiteral("300"));
		CHECK(!threshold->hasActiveFocus() || !named(threshold, "scalarEditor"));
		if (v.window->activeFocusItem())
			CHECK(v.window->activeFocusItem()->objectName() != QLatin1String("scalarEditor") ||
			      !threshold->isAncestorOf(v.window->activeFocusItem()));
		key(v.window, Qt::Key_Escape);
		look.dropWatchHeld(look.rowOf(QStringLiteral("device.Threshold")));
		settle();
	}

	// ---- 7. Commit rules ----
	{
		const auto writes = [&]() { return look.writes.size(); };
		int base = writes();
		auto* gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "2");
		key(v.window, Qt::Key_Return);
		CHECK(writes() == base + 1 && look.writes.last() == QStringLiteral("device.Config.Gain = 2"));
		// The same text again writes nothing; Esc writes nothing.
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		key(v.window, Qt::Key_Return);
		CHECK(writes() == base + 1);
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "4");
		key(v.window, Qt::Key_Escape);
		CHECK(writes() == base + 1 && !gain->property("editing").toBool());
		// Tab writes once and opens the next writable row.
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "5");
		key(v.window, Qt::Key_Tab);
		CHECK(writes() == base + 2);
		CHECK(rowOf(v.list, QStringLiteral("device.Config.Enabled"))->property("editing").toBool());
		// A choice writes once.
		key(v.window, Qt::Key_Space);
		key(v.window, Qt::Key_Down);
		key(v.window, Qt::Key_Return);
		CHECK(writes() == base + 3 && look.writes.last() == QStringLiteral("device.Config.Enabled = true"));
		// A click away writes once.
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "6");
		click(v.window, shown(rowOf(v.list, QStringLiteral("measure.Voltage")), "watchRowLeaf"));
		CHECK(writes() == base + 4 && look.writes.last() == QStringLiteral("device.Config.Gain = 6"));
		// Tab with "abc" in a u8 row writes nothing, holds it and moves on.
		auto* threshold = edit(v, QStringLiteral("device.Threshold"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "abc");
		key(v.window, Qt::Key_Tab);
		threshold = rowOf(v.list, QStringLiteral("device.Threshold"));
		CHECK(writes() == base + 4 && text(shown(threshold, "valueHeldText")) == QStringLiteral("abc"));
		key(v.window, Qt::Key_Escape);
		look.dropWatchHeld(look.rowOf(QStringLiteral("device.Threshold")));
		const int enabled = look.rowOf(QStringLiteral("device.Config.Enabled"));
		look.values[enabled].bits = 0;
		look.values[enabled].writeState.clear();
		look.fill(enabled);
		const int gainRow = look.rowOf(QStringLiteral("device.Config.Gain"));
		look.values[gainRow].bits = floatBits(3);
		look.values[gainRow].writeState.clear();
		look.values[gainRow].changedAt = 0;
		look.fill(gainRow);
		settle();
	}

	// ---- 6. A refused value is held; 8. write feedback; 9. one line ----
	{
		look.readMs = 0;
		auto* gain = edit(v, QStringLiteral("device.Config.Gain"));
		// The cell and the line count their flashes; earlier writes flashed too.
		const int flashed = cellOf(gain)->property("flashes").toInt();
		const double timeFlashed = prop(v.panel, "writeTimeFlashes");
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "345");
		key(v.window, Qt::Key_Return);
		settle(80);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		auto* held = shown(gain, "valueHeldText");
		CHECK(text(held) == QStringLiteral("345") && held->property("font").value<QFont>().strikeOut());
		CHECK(color(held) == theme("error") && named(gain, "valueHeldGlyph"));
		CHECK(text(shown(gain, "valueCaption")) == QStringLiteral("Refused: the device does not take this value · device holds 3"));
		CHECK(look.reads.value(5) == 1);
		// 8: refused - no flash, the error glyph in the line, words primary.
		CHECK(cellOf(gain)->property("flashes").toInt() == flashed);
		auto* line = shown(v.content, "watchWriteOutcome");
		CHECK(text(line).startsWith(QStringLiteral("Write refused ")) &&
		      text(line).endsWith(QStringLiteral(" · device.Config.Gain = 345 · the device does not take this value · it holds 3")));
		CHECK(named(v.content, "watchWriteGlyph") && color(line) == theme("textPrimary"));
		CHECK(prop(v.panel, "writeTimeFlashes") == timeFlashed);
		// One line for one refused write: a row at one line 2 (560 px).
		current(v, gain);
		v.list->forceActiveFocus();
		settle();
		lineTwoWhole(v, gain, 1, exact);
		// Polling: no read of its own.
		look.startWatch();
		settle();
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		CHECK(text(named(gain, "scalarEditor")) == QStringLiteral("345"));
		key(v.window, Qt::Key_Return);  // the held text written again, refused again
		settle(80);
		CHECK(look.reads.value(5) == 1);
		look.stopWatch();
		settle();
		// Out of view and back: still held.
		for (int at = 0; at < 40; ++at)
			look.addRow(QStringLiteral("signals.Extra%1").arg(at, 2, 10, QLatin1Char('0')), 100 + quint32(at),
			            typeOf(WatchScalar::F64, 8), doubleBits(at), QStringLiteral("f64"));
		resize(v, 560, 300);
		QMetaObject::invokeMethod(v.list, "positionViewAtEnd");
		settle(80);
		QMetaObject::invokeMethod(v.list, "positionViewAtBeginning");
		settle(80);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		CHECK(text(shown(gain, "valueHeldText")) == QStringLiteral("345"));
		look.removeFrom(11);
		resize(v, 560);
		// F2 opens it selected; Esc keeps it; Esc on the row drops it.
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		auto* editor = shown(gain, "scalarEditor");
		CHECK(text(editor) == QStringLiteral("345") && editor->property("selectedText").toString() == QStringLiteral("345"));
		key(v.window, Qt::Key_Escape);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		CHECK(text(shown(gain, "valueHeldText")) == QStringLiteral("345"));
		shot(v, output, QStringLiteral("watch-wl9-refused-560%1.png").arg(suffix));
		key(v.window, Qt::Key_Escape);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		CHECK(!named(gain, "valueHeldText") && near(gain->height(), tok("rowHeight"), 0.5));
		CHECK(text(shown(gain, "watchValueText")) == QStringLiteral("3"));

		// 8. Feedback with the reply held a second: the typed value at once, quiet,
		// a progress line from 400 ms, then a flash and the time.
		look.replyMs = 1000;
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "2");
		key(v.window, Qt::Key_Return);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		auto* pending = shown(gain, "valuePending");
		// Quiet: the secondary colour, or the selection's on a focused one.
		CHECK(text(pending) == QStringLiteral("2"));
		CHECK(color(pending) == theme(gain->property("active").toBool() ? "selectedForeground" : "textSecondary"));
		shown(v.content, "watchMoreButton")->forceActiveFocus(Qt::TabFocusReason);
		settle(0);
		CHECK(color(pending) == theme("textSecondary"));
		CHECK(text(shown(v.content, "watchWriteOutcome")) == QStringLiteral("Writing device.Config.Gain = 2…"));
		auto* progress = named(gain, "valueProgress");
		CHECK(progress && !progress->property("shown").toBool());
		QTest::qWait(520);
		CHECK(progress->property("shown").toBool());
		QTest::qWait(600);
		settle(80);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		CHECK(cellOf(gain)->property("flashes").toInt() == flashed + 1);
		line = shown(v.content, "watchWriteOutcome");
		{
			const auto words = text(line);
			CHECK(words.startsWith(QStringLiteral("Wrote ")) && words.endsWith(QStringLiteral(" · device.Config.Gain = 2 · read back 2")));
			CHECK(QTime::fromString(words.mid(6, 8), QStringLiteral("HH:mm:ss")).isValid());
		}
		CHECK(prop(v.panel, "writeTimeFlashes") == timeFlashed + 1 && color(line) == theme("textSecondary"));
		CHECK(!named(v.content, "watchWriteGlyph") && !named(v.content, "watchWriteWarning"));
		// The user's change takes no change mark.
		CHECK(!named(gain, "watchChangeMark"));
		look.replyMs = 0;
		QTest::qWait(650);  // past the flash
		// Read back otherwise: the warning glyph in the cell and the line, no flash.
		look.mode = QStringLiteral("differs");
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "345");
		key(v.window, Qt::Key_Return);
		settle(80);
		gain = rowOf(v.list, QStringLiteral("device.Config.Gain"));
		CHECK(named(gain, "watchReadBackGlyph") && text(shown(gain, "watchValueText")) == QStringLiteral("10"));
		CHECK(cellOf(gain)->property("flashes").toInt() == flashed + 1);
		CHECK(named(v.content, "watchWriteWarning") && text(shown(v.content, "watchWriteOutcome")).endsWith(QStringLiteral(" · the device reads back 10")));
		CHECK(prop(v.panel, "writeTimeFlashes") == timeFlashed + 1);
		look.mode = QStringLiteral("auto");
		// Esc on the row lets go of the glyph.
		current(v, gain);
		v.list->forceActiveFocus();
		settle();
		key(v.window, Qt::Key_Escape);
		CHECK(!named(rowOf(v.list, QStringLiteral("device.Config.Gain")), "watchReadBackGlyph"));
		// With reduced motion nothing flashes; the words change.
		QMetaObject::invokeMethod(v.window, "reduce", Q_ARG(QVariant, true));
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "3");
		key(v.window, Qt::Key_Return);
		settle(80);
		CHECK(cellOf(rowOf(v.list, QStringLiteral("device.Config.Gain")))->property("flashes").toInt() == flashed + 1);
		CHECK(prop(v.panel, "writeTimeFlashes") == timeFlashed + 1);
		CHECK(text(shown(v.content, "watchWriteOutcome")).endsWith(QStringLiteral(" · device.Config.Gain = 3 · read back 3")));
		QMetaObject::invokeMethod(v.window, "reduce", Q_ARG(QVariant, false));

		// 9. One last-action line: a removal after a write, then a write.
		const auto both = [&]() { return named(v.content, "watchWriteOutcome") && named(v.content, "watchNotice"); };
		CHECK(named(v.content, "watchWriteOutcome") && !both());
		current(v, rowOf(v.list, QStringLiteral("signals.Signal02")));
		v.list->forceActiveFocus();
		settle();
		key(v.window, Qt::Key_Delete);
		CHECK(!both() && text(shown(v.content, "watchNotice")) == QStringLiteral("Removed signals.Signal02"));
		CHECK(named(v.content, "undoNoticeLink"));
		click(v.window, shown(v.content, "undoNoticeLink"));
		CHECK(!both() && look.rowOf(QStringLiteral("signals.Signal02")) >= 0);
		gain = edit(v, QStringLiteral("device.Config.Gain"));
		QTest::keyClick(v.window, Qt::Key_A, Qt::ControlModifier);
		typeText(v.window, "2");
		key(v.window, Qt::Key_Return);
		settle(80);
		CHECK(!both() && text(shown(v.content, "watchWriteOutcome")).endsWith(QStringLiteral("read back 2")));
		const int g = look.rowOf(QStringLiteral("device.Config.Gain"));
		look.values[g].bits = floatBits(3);
		look.values[g].writeState.clear();
		look.values[g].changedAt = 0;
		look.fill(g);
		settle();
	}

	// ---- 10. The pace: in the bar when it fits whole, else under it ----
	{
		QTest::qWait(650);  // the last write's flashes past, for the pictures
		QVariantMap pace{{QStringLiteral("cycles"), 3},        {QStringLiteral("polling"), true},
		                 {QStringLiteral("routine"), true},     {QStringLiteral("values"), 72},
		                 {QStringLiteral("readMany"), 1},       {QStringLiteral("batch"), 0},
		                 {QStringLiteral("fieldRead"), 0},      {QStringLiteral("again"), 0},
		                 {QStringLiteral("lastCycleMs"), 16.0}, {QStringLiteral("medianCycleMs"), 16.0},
		                 {QStringLiteral("medianPeriodMs"), 1000.0}, {QStringLiteral("achievedPeriodMs"), 1000.0},
		                 {QStringLiteral("intervalMs"), 1000},  {QStringLiteral("behind"), false}};
		look.active = true;
		look.pace = pace;
		Q_EMIT look.changed();
		resize(v, 1200);
		settle(150);
		auto* inBar = shown(v.content, "watchStatusLabel");
		auto* bar = shown(v.content, "watchToolbar");
		CHECK(inBar->parentItem() == bar);
		CHECK(sy(inBar) >= sy(bar) - 0.5 && sy(inBar, inBar->height()) <= sy(bar, bar->height()) + 0.5);
		const auto words = text(inBar);
		CHECK(words == QStringLiteral("A cycle of 72 values takes 16 ms"));
		shot(v, output, QStringLiteral("watch-wl9-tree-1200%1.png").arg(suffix));
		resize(v, 560);
		settle(150);
		auto* under = shown(v.content, "watchStatusLabel");
		CHECK(under->parentItem() != bar && text(under) == words);
		CHECK(sy(under) > sy(bar, bar->height()));
		CHECK(near(sx(under), padding));
		// It leaves the bar before "Every" does.
		for (int width = 1200; width >= 400; width -= 20) {
			resize(v, width);
			if (v.panel->property("paceInBar").toBool())
				CHECK(v.panel->property("everyShown").toBool());
		}
		look.active = false;
		look.pace = {};
		Q_EMIT look.changed();
		resize(v, 560);
	}

	// ---- 12. The Name cap; 14. give-way; 21. Dec width ----
	{
		resize(v, 1200);
		auto* voltage = rowOf(v.list, QStringLiteral("measure.Voltage"));
		const double nameSeen = prop(v.panel, "nameSeen");
		const double valueLeft = sx(shown(voltage, "watchValueBox")) - sx(v.list);
		CHECK(near(valueLeft, std::max(nameSeen + gap, 24 * body) + gap, 1));
		auto* remove = named(voltage, "removeWatchButton", false)->parentItem();
		CHECK(near(remove->x(), prop(v.panel, "groupRight") + gap, 0.5));
		shot(v, output, QStringLiteral("watch-wl9-list-1200%1.png").arg(suffix));
		showTree(v, false);
		look.setFormat(QStringLiteral("hex"));
		settle(120);
		shot(v, output, QStringLiteral("watch-wl9-hex-1200%1.png").arg(suffix));
		look.setFormat(QStringLiteral("original"));
		showTree(v, true);
		// At 560 px the cap binds or the Name column fills: Value stands at the
		// nearer of the two, and × ends on R only while Name fills.
		for (const int width : {560, 420}) {
			resize(v, width);
			const double fill = eval(v.panel, QStringLiteral("fillValueX(accessColumn, statusColumn, typeColumn)")).toDouble();
			const double cap = prop(v.panel, "nameCap") + gap;
			CHECK(near(prop(v.panel, "valueX"), std::min(fill, cap), 0.5));
			const double right = prop(v.panel, "removeX") + prop(v.panel, "removeColumnWidth");
			if (fill <= cap)
				CHECK(near(right, prop(v.panel, "listWidth"), 1));
			else
				CHECK(near(prop(v.panel, "removeX"), prop(v.panel, "groupRight") + gap, 0.5) &&
				      right < prop(v.panel, "listWidth"));
		}
		for (const int width : {1200, 900, 560, 420}) {
			resize(v, width);
			const double spare = prop(v.panel, "listWidth") - prop(v.panel, "removeX") - prop(v.panel, "removeColumnWidth");
			if (spare > 0.5)
				for (auto* leaf : all(v.list, "watchRowLeaf"))
					CHECK(!leaf->property("truncated").toBool());
		}
		// 14. At 360 px rw hides and every type is whole; at 320 a type gives
		// way only for a name at its floor.
		resize(v, 360);
		CHECK(prop(v.panel, "accessColumn") == 0);
		for (auto* type : all(v.list, "watchRowType"))
			CHECK(!type->property("truncated").toBool());
		resize(v, 320);
		int types = 0;
		for (int row = 0; row < v.list->property("count").toInt(); ++row) {
			QMetaObject::invokeMethod(v.list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 2));
			settle(0);
			auto* item = rowAt(v.list, row);
			if (!item || item->property("isGroup").toBool())
				continue;
			auto* type = shown(item, "watchRowType");
			++types;
			if (type->property("truncated").toBool()) {
				auto* name = shown(item, "watchRowName");
				const double natural = name->property("naturalWidth").toDouble();
				CHECK(name->width() >= std::min(natural, nameFloor) - 1);
			}
		}
		CHECK(types >= 9);
		// 21. The longest Dec an f64 has, at 560 px: whole, names at their floor.
		resize(v, 560);
		look.addRow(QStringLiteral("signals.Tiny"), 60, typeOf(WatchScalar::F64, 8), doubleBits(-2.2250738585072014e-308),
		            QStringLiteral("f64"));
		look.setFormat(QStringLiteral("dec"));
		settle(150);
		auto* tiny = rowOf(v.list, QStringLiteral("signals.Tiny"));
		auto* tinyValue = shown(tiny, "watchValueText");
		CHECK(text(tinyValue) == QStringLiteral("-2.2250738585072014e-308") && !tinyValue->property("truncated").toBool());
		for (int row = 0; row < v.list->property("count").toInt(); ++row) {
			QMetaObject::invokeMethod(v.list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 2));
			settle(0);
			if (auto* item = rowAt(v.list, row)) {
				auto* name = shown(item, "watchRowName");
				CHECK(name->width() >= std::min(name->property("naturalWidth").toDouble(), nameFloor) - 1);
			}
		}
		CHECK(v.list->property("contentWidth").toDouble() <= v.list->width() + 0.5 ||
		      v.list->property("contentWidth").toDouble() < 0);
		look.setFormat(QStringLiteral("original"));
		look.removeFrom(11);
		QMetaObject::invokeMethod(v.panel, "forgetMeasured");
		settle(120);
	}

	// ---- 22. Paths (the designer's review of 20 November 2026, 14.3) ----
	// The List's name is the one path label: whole segments drop under one
	// "…", the nearest parent lasts longest and the leaf elides last; the
	// Name column's cap has a ceiling of 40 body heights (amending WL-9's
	// "the cap never elides a name"); the tree joins a chain of single-child
	// groups into one row and splits it when a second child comes.
	{
		const QString longPath = QStringLiteral(
		    "plant/area/drive/line/converter/inverter/metrology/harmonics/phaseA/band_from_2000_Hz_to_4000_Hz_above_the_carrier/H37");
		const QString h3 = QStringLiteral("metrology/harmonics/phaseA/H3");
		const QString window = QStringLiteral("metrology/harmonics/phaseA/Spectrum.Window");
		showTree(v, false);
		resize(v, 1200);
		look.addRow(h3, 61, typeOf(WatchScalar::F32, 4), floatBits(0.031f), QStringLiteral("f32"));
		look.addRow(window, 62, typeOf(WatchScalar::U8, 1), 1, QStringLiteral("u8"));
		look.addRow(longPath, 63, typeOf(WatchScalar::F32, 4), floatBits(0.004f), QStringLiteral("f32"));
		QMetaObject::invokeMethod(v.panel, "forgetMeasured");
		settle(150);
		// The rows measure their names as they are laid out: bring the long one in.
		rowOf(v.list, longPath);
		settle(120);
		// The long path is past the ceiling: the cap stops there, Value one gap after it.
		const double ceiling = 40 * body;
		CHECK(prop(v.panel, "nameSeen") + gap > ceiling);
		CHECK(near(prop(v.panel, "nameCap"), ceiling, 0.5));
		CHECK(near(prop(v.panel, "valueX"), ceiling + gap, 0.5));
		auto* longRow = rowOf(v.list, longPath);
		auto* longLabel = shown(longRow, "watchRowPath");
		CHECK(longLabel->property("implicitWidth").toDouble() > longLabel->width());
		CHECK(longLabel->property("elided").toBool());
		// The branch stays while middle groups go.
		CHECK(text(shown(longRow, "watchRowPrefix")).startsWith(QStringLiteral("plant/…/")));
		const auto prefix = text(shown(longRow, "watchRowPrefix"));
		// One "…", whole segments around it, the nearest parent kept, the leaf whole.
		CHECK(prefix.count(QStringLiteral("…")) == 1 && prefix.endsWith(QLatin1Char('/')));
		CHECK(prefix.endsWith(QStringLiteral("band_from_2000_Hz_to_4000_Hz_above_the_carrier/")));
		const auto segments = longPath.split(QLatin1Char('/'));
		for (const auto& part : prefix.split(QLatin1Char('/'), Qt::SkipEmptyParts))
			CHECK(part == QStringLiteral("…") || segments.contains(part));
		auto* longLeaf = shown(longRow, "watchRowLeaf");
		CHECK(text(longLeaf) == QStringLiteral("H37") && !longLeaf->property("truncated").toBool());
		// Prefix muted, leaf bright; heard leaf first.
		CHECK(color(shown(longRow, "watchRowPrefix")) == theme("textMuted"));
		CHECK(color(longLeaf) == theme("textPrimary"));
		CHECK(longLabel->property("spokenName").toString().startsWith(QStringLiteral("H37, in plant, area, drive, line")));
		CHECK(eval(longRow, QStringLiteral("Accessible.name")).toString().startsWith(QStringLiteral("H37, in plant")));
		// A short path stays whole, its prefix muted.
		auto* h3Row = rowOf(v.list, h3);
		CHECK(!shown(h3Row, "watchRowPath")->property("elided").toBool());
		CHECK(text(shown(h3Row, "watchRowPrefix")) == QStringLiteral("metrology/harmonics/phaseA/"));
		shot(v, output, QStringLiteral("watch-list-long-path-1200%1.png").arg(suffix));
		// The drop order on the label alone: the middle groups nearest the
		// branch first, then the branch; the nearest parent last.
		auto* h3Label = shown(h3Row, "watchRowPath");
		CHECK(eval(h3Label, QStringLiteral("shape(1).prefix")).toString() == QStringLiteral("metrology/…/phaseA/"));
		CHECK(eval(h3Label, QStringLiteral("shape(2).prefix")).toString() == QStringLiteral("…/phaseA/"));
		CHECK(eval(h3Label, QStringLiteral("shape(3).prefix")).toString() == QStringLiteral("…/"));
		CHECK(eval(shown(rowOf(v.list, window), "watchRowPath"), QStringLiteral("shape(2).prefix")).toString() ==
		      QStringLiteral("metrology/…/Spectrum."));
		// Narrow: what is cut is whole segments under one "…", the leaf whole.
		resize(v, 420);
		for (const auto& path : {h3, window, longPath}) {
			auto* row = rowOf(v.list, path);
			auto* label = shown(row, "watchRowPath");
			const auto cut = text(shown(row, "watchRowPrefix"));
			CHECK(!label->property("elided").toBool() || cut.count(QStringLiteral("…")) == 1 ||
			      shown(row, "watchRowLeaf")->property("truncated").toBool());
			CHECK(label->property("usedWidth").toDouble() <= label->width() + 0.5);
		}
		shot(v, output, QStringLiteral("watch-list-long-path-420%1.png").arg(suffix));
		// The tree: metrology/harmonics/phaseA is one row (its box and count
		// the last group's); the long chain is one row too.
		showTree(v, true);
		resize(v, 560);
		auto* chain = rowOf(v.list, QStringLiteral("metrology/harmonics/phaseA"));
		CHECK(chain->property("isGroup").toBool() && chain->property("depth").toInt() == 0);
		CHECK(text(shown(chain, "watchRowLeaf")) == QStringLiteral("metrology/harmonics/phaseA"));
		CHECK(shown(chain, "watchGroupCount")->property("count").toString() == QStringLiteral("2 watched"));
		CHECK(rowIndex(v.list, QStringLiteral("metrology")) < 0 && rowIndex(v.list, QStringLiteral("metrology/harmonics")) < 0);
		CHECK(rowOf(v.list, h3)->property("depth").toInt() == 1);
		CHECK(rowOf(v.list, QStringLiteral("plant/area/drive/line/converter/inverter/metrology/harmonics/phaseA/band_from_2000_Hz_to_4000_Hz_above_the_carrier"))
		          ->property("isGroup").toBool());
		shot(v, output, QStringLiteral("watch-tree-chains-560%1.png").arg(suffix));
		// A fold an older project saved in the dotted form names its node in
		// the "/" form once the node is there; the node is folded.
		{
			const QString spectrum = QStringLiteral("metrology/harmonics/phaseA/Spectrum");
			look.rows.setFolded({QStringLiteral("metrology.harmonics.phaseA.Spectrum")});
			settle(80);
			CHECK(look.rows.folded() == QStringList{spectrum});
			CHECK(!rowOf(v.list, spectrum)->property("expanded").toBool());
			look.rows.setFolded({});
			settle(80);
		}
		// A second child splits the chain where it branches.
		look.addRow(QStringLiteral("metrology/levels/L1"), 64, typeOf(WatchScalar::F32, 4), floatBits(1), QStringLiteral("f32"));
		settle(120);
		CHECK(rowIndex(v.list, QStringLiteral("metrology")) >= 0);
		auto* split = rowOf(v.list, QStringLiteral("metrology/harmonics/phaseA"));
		CHECK(split->property("depth").toInt() == 1 && text(shown(split, "watchRowLeaf")) == QStringLiteral("harmonics/phaseA"));
		look.removeFrom(11);
		QMetaObject::invokeMethod(v.panel, "forgetMeasured");
		settle(120);
	}

	// ---- 15. The change mark ----
	{
		resize(v, 560);
		shown(v.content, "watchMoreButton")->forceActiveFocus(Qt::TabFocusReason);
		look.startWatch();
		look.tick.start();
		QTest::qWait(350);
		auto* counter = rowOf(v.list, QStringLiteral("measure.Counter"));
		CHECK(named(counter, "watchChangeMark") && color(shown(counter, "watchValueText")) == theme("info"));
		look.tick.stop();
		QTest::qWait(1350);
		CHECK(!named(rowOf(v.list, QStringLiteral("measure.Counter")), "watchChangeMark"));
		// One change: marked for a second, clear by 1.3 s.
		{
			const int at = look.rowOf(QStringLiteral("measure.Counter"));
			look.device(at, look.values[at].bits + 1);
		}
		QTest::qWait(900);
		CHECK(named(rowOf(v.list, QStringLiteral("measure.Counter")), "watchChangeMark"));
		QTest::qWait(400);
		CHECK(!named(rowOf(v.list, QStringLiteral("measure.Counter")), "watchChangeMark"));
		CHECK(!named(rowOf(v.list, QStringLiteral("measure")), "watchChangeMark"));
		// Folded measure carries it, with how many change.
		auto* measure = rowOf(v.list, QStringLiteral("measure"));
		QMetaObject::invokeMethod(&look.rows, "setExpanded", Q_ARG(int, measure->property("index").toInt()), Q_ARG(bool, false));
		settle(60);
		const int row = look.rowOf(QStringLiteral("measure.Counter"));
		look.device(row, look.values[row].bits + 1);
		settle(80);
		measure = rowOf(v.list, QStringLiteral("measure"));
		CHECK(named(measure, "watchChangeMark"));
		CHECK(shown(measure, "watchGroupCount")->property("count").toString() == QStringLiteral("4 watched") &&
		      shown(measure, "watchGroupCount")->property("moving").toString() == QStringLiteral("1 changing"));
		QMetaObject::invokeMethod(&look.rows, "setExpanded", Q_ARG(int, measure->property("index").toInt()), Q_ARG(bool, true));
		settle(60);
		// Never on the row being edited.
		auto* period = edit(v, QStringLiteral("device.Config.SamplePeriodMs"));
		const int p = look.rowOf(QStringLiteral("device.Config.SamplePeriodMs"));
		look.device(p, 334);
		settle(60);
		CHECK(!named(period, "watchChangeMark"));
		key(v.window, Qt::Key_Escape);
		look.stopWatch();
		QTest::qWait(1100);
	}
	// ---- Part B (the review's 14.2): a named type in two tones, the scope
	// muted; the scopes give way third, after rw and Status, for the whole
	// column, the header saying so and a type's tooltip holding the whole ----
	{
		const int base = int(look.values.size());
		const auto path = QStringLiteral("metrology/harmonics/phaseA/Spectrum.window");
		look.addRow(path, 40, typeOf(WatchScalar::U8, 1, true), 1, QStringLiteral("metrology::Window"));
		showTree(v, false);
		resize(v, 1200);
		// The columns measured afresh from these rows (earlier checks left a
		// long unscoped type, which no cut of scopes would narrow).
		QMetaObject::invokeMethod(v.panel, "forgetMeasured");
		settle(120);
		auto* row = rowOf(v.list, path);
		CHECK(row);
		auto* type = shown(row, "watchRowType");
		CHECK(type->property("typeText").toString() == QStringLiteral("metrology::Window") &&
		      type->property("shown").toString() == QStringLiteral("metrology::Window"));
		CHECK(!v.panel->property("typeScopesCut").toBool());
		const auto toned = [&](const char* token, const char* text) {
			return QStringLiteral("color=\"%1\">%2</font>").arg(theme(token).name(QColor::HexRgb), QString::fromUtf8(text));
		};
		CHECK(type->property("rich").toString().contains(toned("textMuted", "metrology::")) &&
		      type->property("rich").toString().contains(toned("textSecondary", "Window")));
		resize(v, 320);
		row = rowOf(v.list, path);
		type = shown(row, "watchRowType");
		CHECK(prop(v.panel, "typeNarrowColumn") < prop(v.panel, "typeWideColumn"));
		CHECK(v.panel->property("typeScopesCut").toBool() && prop(v.panel, "accessColumn") == 0);
		CHECK(type->property("shown").toString() == QStringLiteral("…::Window") && type->property("cutShown").toBool());
		auto* tip = type->findChild<QObject*>(QStringLiteral("watchRowTypeToolTip"));
		CHECK(tip && tip->property("text").toString() == QStringLiteral("metrology::Window"));
		auto* header = v.panel->findChild<QObject*>(QStringLiteral("watchTypeHeaderToolTip"));
		CHECK(header && header->property("text").toString() ==
		                    QStringLiteral("Scopes are hidden for room; hover a type for its full name."));
		shot(v, output, QStringLiteral("watch-scoped-types-320%1.png").arg(suffix));
		look.removeFrom(base);
		resize(v, 560);
		showTree(v, true);
	}
	screens += 1;
	v.window->close();
	settle();
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	telemetry::ux::registerQmlTypes();
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(text));
	});
	const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{};
	try {
		int screens = 0;
		run(output, 10, screens);
		run(output, 12.5, screens);
		settle();
		CHECK(warnings == 0);
		qInfo().noquote() << QStringLiteral("Watch look checks=%1 warnings=%2").arg(checks).arg(warnings);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Watch look failure: %s\n", error.what());
		return 1;
	}
}
#include "WatchLook.moc"
