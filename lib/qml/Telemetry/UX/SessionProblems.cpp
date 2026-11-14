/*
 * @file SessionProblems.cpp
 * @brief The connection's problems, kept in a ProblemsModel while they last.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "SessionProblems.hpp"

namespace telemetry::ux {
namespace qt = integration::qt;

SessionProblems::SessionProblems(qt::ConnectionSession* session, ProblemsModel* problems, QObject* parent)
    : QObject(parent), session_(session), problems_(problems)
{
	if (session_) {
		connect(session_, &qt::ConnectionSession::stateChanged, this, &SessionProblems::update);
		connect(session_, &qt::ConnectionSession::identityChanged, this, &SessionProblems::update);
	}
	update();
}

void SessionProblems::update()
{
	const QPointer<SessionProblems> alive(this);
	const auto session = session_;
	const auto problems = problems_;
	if (!session || !problems)
		return;
	const auto device = QString::fromLatin1(scope);
	const bool ready = session->ready() || session->filesOnly();
	const bool recovery = session->recoveryRequired();
	const auto why = session->lastError();
	// Files-only connections have no same-peer recovery obligation even when
	// their link fails. The recorded error distinguishes that failure from
	// closing a working connection deliberately.
	const bool failed = !ready && !session->connecting() && !why.isEmpty();
	if (!ready && !session->connecting() && !recovery && !failed) {
		problems->resolveScope(device);
		return;
	}
	const auto connectionKey = QStringLiteral("session.connection");
	if (recovery || failed) {
		problems->raise(connectionKey, Severity::Error, tr("Connection"),
		                 why.isEmpty() ? tr("Connection dropped.") : why, device);
	} else if (ready) {
		problems->resolve(connectionKey);
	}
	// Model publication calls the host synchronously. It can close this
	// adapter, the connection or Problems; later stages need live objects and
	// fresh session facts when that publication also changed the connection.
	if (!alive || !session || !problems)
		return;
	const auto limitationKey = QStringLiteral("session.limitation");
	if (session->filesOnly() && session->limitation() != qt::ConnectionSession::Limitation::None)
		problems->raise(limitationKey, Severity::Warning, tr("Device"), session->limitationText(), device);
	else if (session->ready() || session->filesOnly())
		problems->resolve(limitationKey);
	if (!alive || !session || !problems)
		return;
	const auto identityKey = QStringLiteral("session.identity");
	if (const auto problem = session->identityProblem(); !problem.isEmpty())
		problems->raise(identityKey, Severity::Warning, tr("Device"), problem, device);
	else
		problems->resolve(identityKey);
}
} // namespace telemetry::ux
