/*
 * Present native endpoint types and values in the Qt playground.
 *
 * Keep labels, units and editing policy in the application while the Model
 * describes native types. The window owns its widgets and timer; every refresh
 * reads the current demo state on the GUI thread.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
QString text(std::string_view value)
{
	return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

// Labels and units are presentation policy, outside the telemetry descriptor.
QString unit(telemetry::PackedId id)
{
	switch (id) {
		case telemetry::makeId<0, 0>():
		case telemetry::makeId<0, 4>():
			return "V";
		case telemetry::makeId<0, 1>():
			return "A";
		case telemetry::makeId<0, 2>():
			return "kW";
		case telemetry::makeId<1, 0>():
			return "degC";
		default:
			return {};
	}
}

template<class T>
QString typeText();

template<class T, std::size_t... I>
QString memberTypes(std::index_sequence<I...>)
{
	QStringList members;
	((members << text(telemetry::reflection::memberName<I, T>()) + ": " +
	                 typeText<telemetry::reflection::MemberType<I, T>>()),
	 ...);
	return "{" + members.join(", ") + "}";
}

template<class T>
QString typeText()
{
	using Type = telemetry::Type<T>;
	if constexpr (Type::kind == telemetry::TypeKind::Void) {
		return "void";
	} else if constexpr (Type::kind == telemetry::TypeKind::Scalar) {
		switch (Type::code) {
			case telemetry::ScalarCode::Bool:
				return "bool";
			case telemetry::ScalarCode::U8:
				return "u8";
			case telemetry::ScalarCode::S8:
				return "s8";
			case telemetry::ScalarCode::U16:
				return "u16";
			case telemetry::ScalarCode::S16:
				return "s16";
			case telemetry::ScalarCode::U32:
				return "u32";
			case telemetry::ScalarCode::S32:
				return "s32";
			case telemetry::ScalarCode::U64:
				return "u64";
			case telemetry::ScalarCode::S64:
				return "s64";
			case telemetry::ScalarCode::F32:
				return "f32";
			case telemetry::ScalarCode::F64:
				return "f64";
		}
		return "unknown";
	} else if constexpr (Type::kind == telemetry::TypeKind::Enum) {
		return "enum<" + typeText<std::underlying_type_t<T>>() + ">";
	} else if constexpr (Type::kind == telemetry::TypeKind::Array) {
		return "array<" + typeText<typename Type::Element>() + ", " +
		       QString::number(static_cast<qulonglong>(Type::count)) + ">";
	} else {
		return memberTypes<T>(std::make_index_sequence<telemetry::reflection::memberCount<T>>{});
	}
}

template<class T>
QString nativeText(const T& value);

template<class E, std::size_t... I>
QString enumText(E value, std::index_sequence<I...>)
{
	QString named;
	((value == telemetry::reflection::Enum<E>::template entryValue<I>()
	      ? (named = text(telemetry::reflection::Enum<E>::template entryName<I>()), void())
	      : void()),
	 ...);
	// A representable unnamed code is still a value, not a fake zero.
	return named.isEmpty() ? nativeText(static_cast<std::underlying_type_t<E>>(value)) : named;
}

template<class T, std::size_t... I>
QString memberValues(const T& value, std::index_sequence<I...>)
{
	QStringList members;
	((members << text(telemetry::reflection::memberName<I, T>()) + ": " +
	                 nativeText(telemetry::reflection::get<I>(value))),
	 ...);
	return "{" + members.join(", ") + "}";
}

template<class T>
QString nativeText(const T& value)
{
	if constexpr (std::is_same_v<T, bool>) {
		return value ? "true" : "false";
	} else if constexpr (std::is_integral_v<T>) {
		// Never route U64/S64 through floating point or a QJson number.
		if constexpr (std::is_signed_v<T>)
			return QString::number(static_cast<qlonglong>(value));
		else
			return QString::number(static_cast<qulonglong>(value));
	} else if constexpr (std::is_floating_point_v<T>) {
		return std::isfinite(value)
		           ? QString::number(value, 'g', std::numeric_limits<T>::max_digits10)
		           : "n/a";
	} else if constexpr (std::is_enum_v<T>) {
		return enumText(value,
		                std::make_index_sequence<telemetry::reflection::Enum<T>::entryCount>{});
	} else if constexpr (telemetry::Type<T>::kind == telemetry::TypeKind::Array) {
		QStringList elements;
		for (const auto& element : value)
			elements << nativeText(element);
		return "[" + elements.join(", ") + "]";
	} else {
		return memberValues(value,
		                    std::make_index_sequence<telemetry::reflection::memberCount<T>>{});
	}
}

template<std::size_t... I>
void addModes(QComboBox& box, std::index_sequence<I...>)
{
	using Enum = telemetry::reflection::Enum<demo::Mode>;
	(box.addItem(text(Enum::template entryName<I>()),
	             static_cast<qulonglong>(Enum::template entryValue<I>())),
	 ...);
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	setWindowTitle("Telemetry playground");
	auto* layout = new QVBoxLayout(ui->centralwidget);
	layout->addWidget(
	    new QLabel("Native fields and commands: C++ signatures define their types.", this));

	fields_ = new QTableWidget(this);
	fields_->setObjectName("telemetryFields");
	fields_->setColumnCount(5);
	fields_->setHorizontalHeaderLabels({"ID", "Field", "Type", "Unit", "Value"});
	fields_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	demo::fields.forEach(
	    [&]<std::size_t Group, std::size_t Entry>(std::string_view catalog, const auto& endpoint) {
		    constexpr auto id = telemetry::makeId<Group, Entry>();
		    using Value = typename std::remove_cvref_t<decltype(endpoint)>::Value;
		    const int row = fields_->rowCount();
		    fields_->insertRow(row);
		    fields_->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
		    fields_->setItem(row, 1, new QTableWidgetItem(text(catalog) + "." + endpoint.name()));
		    fields_->setItem(row, 2, new QTableWidgetItem(typeText<Value>()));
		    fields_->setItem(row, 3, new QTableWidgetItem(unit(id)));
		    fields_->setItem(row, 4, new QTableWidgetItem);
	    });
	layout->addWidget(fields_);

	values_ = new QPlainTextEdit(this);
	values_->setReadOnly(true);
	values_->setMaximumHeight(90);
	layout->addWidget(new QLabel("Current values", this));
	layout->addWidget(values_);

	QStringList commandLines;
	demo::commands.forEach([&](std::string_view catalog, const auto& endpoint) {
		using Request = typename std::remove_cvref_t<decltype(endpoint)>::Request;
		commandLines << text(catalog) + "." + endpoint.name() + "(" + typeText<Request>() + ")";
	});
	auto* commands = new QPlainTextEdit(this);
	commands->setReadOnly(true);
	commands->setMaximumHeight(90);
	commands->setPlainText(commandLines.join('\n'));
	layout->addWidget(new QLabel("Commands and reflected request members", this));
	layout->addWidget(commands);

	auto* controls = new QHBoxLayout;
	auto* reset = new QPushButton("Reset counter", this);
	auto* limit = new QDoubleSpinBox(this);
	limit->setRange(demo::minimumVoltageLimit, demo::maximumVoltageLimit);
	limit->setValue(demo::meter.threshold);
	limit->setSuffix(" V");
	auto* mode = new QComboBox(this);
	addModes(*mode,
	         std::make_index_sequence<telemetry::reflection::Enum<demo::Mode>::entryCount>{});
	mode->setCurrentIndex(mode->findData(static_cast<qulonglong>(demo::meter.mode)));
	auto* apply = new QPushButton("Configure meter", this);
	auto* result = new QLabel(this);
	controls->addWidget(reset);
	controls->addWidget(limit);
	controls->addWidget(mode);
	controls->addWidget(apply);
	controls->addWidget(result);
	layout->addLayout(controls);
	connect(reset, &QPushButton::clicked, this, [this, result] {
		const auto status = demo::meterCommands.call<demo::MeterCommand::Reset>();
		result->setText(status == telemetry::CommandResult::Executed ? "Executed" : "Not executed");
		refreshValues();
	});
	connect(apply, &QPushButton::clicked, this, [this, result, limit, mode] {
		// GUI policy bounds the double before its explicit native conversion.
		const demo::ConfigureRequest request{
		    static_cast<float>(limit->value()),
		    static_cast<demo::Mode>(mode->currentData().toULongLong())};
		const auto status = demo::commands.call<telemetry::makeId<0, 1>()>(request);
		result->setText(status == telemetry::CommandResult::Executed ? "Executed"
		                                                             : "Invalid arguments");
		refreshValues();
	});

	auto* timer = new QTimer(this);
	timer->setInterval(500);
	connect(timer, &QTimer::timeout, this, [this] {
		demo::advance();
		refreshValues();
	});
	auto* pause = new QPushButton("Pause", this);
	connect(pause, &QPushButton::clicked, this, [timer, pause] {
		if (timer->isActive()) {
			timer->stop();
			pause->setText("Resume");
		} else {
			timer->start();
			pause->setText("Pause");
		}
	});
	layout->addWidget(pause);
	refreshValues();
	timer->start();
}

void MainWindow::refreshValues()
{
	QStringList lines;
	int row = 0;
	demo::fields.forEach([&](std::string_view catalog, const auto& endpoint) {
		const auto value = endpoint.read();
		const QString display = value ? nativeText(*value) : "n/a";
		if (auto* item = fields_->item(row++, 4))
			item->setText(display);
		lines << text(catalog) + "." + endpoint.name() + " = " + display;
	});
	values_->setPlainText(lines.join('\n'));
}

MainWindow::~MainWindow()
{
	delete ui;
}
