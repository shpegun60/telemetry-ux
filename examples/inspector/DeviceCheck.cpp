/*
 * @file DeviceCheck.cpp
 * @brief Exercise discovery and every demo Field without a second execution queue.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "DeviceCheck.hpp"
#include "InspectorController.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <cstdio>
#include <bit>
#include <cstdint>
#include <functional>
#include <vector>

namespace inspector_qml {
int checkSerialDevice(QCoreApplication& app, InspectorController& controller,
                      const QString& reportPath)
{
	QObject context;
	QTimer deadline;
	deadline.setSingleShot(true);
	std::vector<QPersistentModelIndex> fields;
	QJsonArray snapshots;
	std::size_t next = 0;
	unsigned scalarCount = 0;
	bool started = false, waiting = false, ended = false;
	bool voltage = false, counter = false, demonstration = false, probe = false;
	bool ua = false, ub = false, uc = false;
	auto finish = [&](bool passed, QString error) {
		if (ended)
			return;
		ended = true;
		deadline.stop();
		QJsonObject report{{"passed", passed},
		                   {"error", error},
		                   {"profile", "nucleo-h7s3l8-demo"},
		                   {"port", controller.serialPort()},
		                   {"baud", controller.serialBaud()},
		                   {"fields", snapshots},
		                   {"scalarCount", int(scalarCount)},
		                   {"voltage", voltage},
		                   {"probe", probe},
		                   {"exactCounter", counter},
		                   {"demonstration", demonstration}};
		QSaveFile output(reportPath);
		const auto encoded = QJsonDocument(report).toJson(QJsonDocument::Indented);
		if (!output.open(QIODevice::WriteOnly) || output.write(encoded) != encoded.size() ||
		    !output.commit()) {
			passed = false;
			error = QStringLiteral("Could not commit device-check report");
		}
		controller.disconnectDevice();
		std::printf(
		    "Inspector serial device: passed=%d fields=%d scalars=%u exactCounter=%d voltage=%d demo=%d\n",
		    int(passed), int(snapshots.size()), scalarCount, int(counter), int(voltage),
		    int(demonstration));
		if (!passed)
			std::fprintf(stderr, "%s\n", qPrintable(error));
		app.exit(passed ? 0 : 9);
	};
	std::function<void()> readNext;
	readNext = [&] {
		if (ended || waiting)
			return;
		if (next == fields.size()) {
			const bool passed =
			    snapshots.size() >= 4 && voltage && counter && demonstration && probe;
			finish(passed, passed ? QString{} : QStringLiteral("Demo Field value checks failed"));
			return;
		}
		if (!fields[next].isValid() || !controller.selectEndpoint(fields[next])) {
			finish(false, QStringLiteral("Field metadata became stale"));
			return;
		}
		waiting = true;
		if (!controller.refresh()) {
			waiting = false;
			finish(false, controller.statusText());
		}
	};
	auto start = [&] {
		if (ended || started || !controller.connected())
			return;
		started = true;
		auto* endpoints = controller.endpoints();
		unsigned visited = 0;
		const auto enumerate = [&](auto&& self, QModelIndex parent, unsigned depth) -> bool {
			if (depth > 64)
				return false;
			for (int row = 0; row < endpoints->rowCount(parent); ++row) {
				if (++visited > 4096)
					return false;
				const auto index = endpoints->index(row, 0, parent);
				if (endpoints->data(index, EndpointModel::IsEndpointRole).toBool() &&
				    endpoints->data(index, EndpointModel::CategoryRole).toInt() == 1 &&
				    endpoints->data(index, EndpointModel::ReadableRole).toBool())
					fields.emplace_back(index);
				if (!self(self, index, depth + 1))
					return false;
			}
			return true;
		};
		if (!enumerate(enumerate, {}, 0) || fields.empty()) {
			finish(false, QStringLiteral("No bounded readable Field catalog"));
			return;
		}
		QTimer::singleShot(0, &context, readNext);
	};
	QObject::connect(&controller, &InspectorController::stateChanged, &context, [&] {
		if (ended)
			return;
		if (controller.connected())
			start();
		else if (!controller.busy())
			finish(false, controller.statusText());
	});
	QObject::connect(
	    &controller, &InspectorController::readFinished, &context, [&](bool published) {
		    if (ended || !waiting)
			    return;
		    waiting = false;
		    if (!published || next >= fields.size() || !fields[next].isValid()) {
			    finish(false, controller.statusText());
			    return;
		    }
		    const auto metadata =
		        static_cast<EndpointModel*>(controller.endpoints())->endpoint(fields[next]);
		    const auto field = metadata.value(QStringLiteral("path")).toString();
		    QJsonArray values;
		    auto* model = controller.values();
		    unsigned visited = 0;
		    const auto collect = [&](auto&& self, QModelIndex parent, QString path,
		                             unsigned depth) -> bool {
			    if (depth > 64)
				    return false;
			    while (model->canFetchMore(parent)) {
				    const auto before = model->rowCount(parent);
				    model->fetchMore(parent);
				    if (model->rowCount(parent) <= before || model->rowCount(parent) > 4096)
					    return false;
			    }
			    for (int row = 0; row < model->rowCount(parent); ++row) {
				    if (++visited > 4096)
					    return false;
				    const auto index = model->index(row, 0, parent);
				    const auto name = model->data(index, ValueModel::NameRole).toString();
				    const auto location = path + QLatin1Char('.') + name;
				    if (!model->data(index, ValueModel::AggregateRole).toBool()) {
					    const auto value = model->data(index, ValueModel::ValueRole).toString();
					    values.append(QJsonObject{
					        {"path", location},
					        {"value", value},
					        {"type", model->data(index, ValueModel::TypeRole).toString()}});
					    ++scalarCount;
					    if (field == QStringLiteral("measure.Voltage") &&
					        model->data(index, ValueModel::TypeRole).toString() ==
					            QStringLiteral("float32")) {
						    bool valid = false;
						    const auto number = value.toFloat(&valid);
						    const auto matches = [&](float expected) {
							    return valid && std::bit_cast<std::uint32_t>(number) ==
							                        std::bit_cast<std::uint32_t>(expected);
						    };
						    if (name == QStringLiteral("Ua"))
							    ua = matches(230.1f);
						    if (name == QStringLiteral("Ub"))
							    ub = matches(229.8f);
						    if (name == QStringLiteral("Uc"))
							    uc = matches(231.2f);
						    voltage = ua && ub && uc;
					    }
					    counter |= field == QStringLiteral("device.Counter") &&
					               value == QStringLiteral("18446744073709551615");
					    demonstration |= field == QStringLiteral("device.Status") &&
					                     name == QStringLiteral("demonstration") &&
					                     value == QStringLiteral("true");
				    }
				    if (!self(self, index, location, depth + 1))
					    return false;
			    }
			    return true;
		    };
		    if (!collect(collect, {}, field, 0) || values.isEmpty()) {
			    finish(false, QStringLiteral("Snapshot traversal failed"));
			    return;
		    }
		    if (field == QStringLiteral("device.Probe")) {
			    const QStringList expected{QStringLiteral("1"),    QStringLiteral("-2"),
			                               QStringLiteral("3"),    QStringLiteral("4"),
			                               QStringLiteral("5"),    QStringLiteral("-6"),
			                               QStringLiteral("true"), QStringLiteral("-2 (unknown)")};
			    const QStringList suffixes{
			        QStringLiteral(".Matrix.[0].[0]"), QStringLiteral(".Matrix.[0].[1]"),
			        QStringLiteral(".Matrix.[0].[2]"), QStringLiteral(".Matrix.[1].[0]"),
			        QStringLiteral(".Matrix.[1].[1]"), QStringLiteral(".Matrix.[1].[2]"),
			        QStringLiteral(".Enabled"),        QStringLiteral(".mode")};
			    probe = values.size() == expected.size();
			    for (qsizetype i = 0; probe && i < values.size(); ++i) {
				    const auto item = values[i].toObject();
				    const auto type = i < 6    ? QStringLiteral("int32")
				                      : i == 6 ? QStringLiteral("bool")
				                               : QStringLiteral("enum (int16)");
				    probe = item.value(QStringLiteral("value")).toString() == expected[i] &&
				            item.value(QStringLiteral("type")).toString() == type &&
				            item.value(QStringLiteral("path")).toString().endsWith(suffixes[i]);
			    }
		    }
		    snapshots.append(QJsonObject{{"field", field}, {"values", values}});
		    ++next;
		    QTimer::singleShot(0, &context, readNext);
	    });
	QObject::connect(&deadline, &QTimer::timeout, &context, [&] {
		finish(false, QStringLiteral("Device-check deadline expired"));
	});
	deadline.start(60000);
	QTimer::singleShot(0, &context, start);
	return app.exec();
}
} // namespace inspector_qml
