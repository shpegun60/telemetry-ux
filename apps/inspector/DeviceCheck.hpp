/*
 * @file DeviceCheck.hpp
 * @brief Read-only serial qualification of the Nucleo demonstration through Qt models.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_DEVICE_CHECK_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_DEVICE_CHECK_HPP
#pragma once
#include <QtCore/QString>
class QCoreApplication;

namespace inspector_qml {
class InspectorController;
int checkSerialDevice(QCoreApplication& application, InspectorController& controller,
                      const QString& reportPath);
} // namespace inspector_qml
#endif
