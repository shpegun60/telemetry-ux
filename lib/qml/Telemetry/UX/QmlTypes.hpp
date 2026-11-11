/*
 * @file QmlTypes.hpp
 * @brief Registers the library's C++ QML types; a host calls it once before loading QML.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The module is a static library without a plugin, so the registration Qt
 * generates for its QML_ELEMENT types (HexView) is an object nothing else
 * refers to, and a linker drops it. Calling this keeps it, as the Qt
 * documentation prescribes; the registration then runs by itself.
 */
#ifndef TELEMETRY_UX_QML_TYPES_HPP
#define TELEMETRY_UX_QML_TYPES_HPP
#pragma once

namespace telemetry::ux {
void registerQmlTypes();
} // namespace telemetry::ux
#endif
