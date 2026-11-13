/*
 * @file SessionProblems.hpp
 * @brief The connection's problems, kept in a ProblemsModel while they last.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The one adapter between a ConnectionSession and Problems, so nothing about
 * the connection is reported twice:
 * - "session.connection" (error): opening or using the connection failed;
 *   resolved when the device answers again, including in files-only mode.
 * - "session.limitation" (warning): files only - no descriptor, or one this
 *   program cannot use - in the session's own words.
 * - "session.identity" (warning): the identity file could not be read.
 * All of scope "device": closing a working connection deliberately clears
 * them. A recorded failure or recovery obligation remains until reconnect.
 */
#ifndef TELEMETRY_UX_SESSION_PROBLEMS_HPP
#define TELEMETRY_UX_SESSION_PROBLEMS_HPP
#pragma once

#include "EventLog.hpp"
#include <integration/qt/ConnectionSession.hpp>
#include <QtCore/QObject>
#include <QtCore/QPointer>

namespace telemetry::ux {
class SessionProblems final : public QObject {
	Q_OBJECT
public:
	SessionProblems(integration::qt::ConnectionSession* session, ProblemsModel* problems, QObject* parent = nullptr);

	static constexpr const char* scope = "device";

private:
	void update();

	QPointer<integration::qt::ConnectionSession> session_;
	QPointer<ProblemsModel> problems_;
};
} // namespace telemetry::ux
#endif
