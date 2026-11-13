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
	if (!session_ || !problems_)
		return;
	const auto device = QString::fromLatin1(scope);
	const bool ready = session_->ready() || session_->filesOnly();
	const bool recovery = session_->recoveryRequired();
	const auto why = session_->lastError();
	// Files-only connections have no same-peer recovery obligation even when
	// their link fails. The recorded error distinguishes that failure from
	// closing a working connection deliberately.
	const bool failed = !ready && !session_->connecting() && !why.isEmpty();
	if (!ready && !session_->connecting() && !recovery && !failed) {
		problems_->resolveScope(device);
		return;
	}
	const auto connectionKey = QStringLiteral("session.connection");
	if (recovery || failed) {
		problems_->raise(connectionKey, Severity::Error, tr("Connection"),
		                 why.isEmpty() ? tr("Connection dropped.") : why, device);
	} else if (ready) {
		problems_->resolve(connectionKey);
	}
	const auto limitationKey = QStringLiteral("session.limitation");
	if (session_->filesOnly() && session_->limitation() != qt::ConnectionSession::Limitation::None)
		problems_->raise(limitationKey, Severity::Warning, tr("Device"), session_->limitationText(), device);
	else if (ready)
		problems_->resolve(limitationKey);
	const auto identityKey = QStringLiteral("session.identity");
	if (const auto problem = session_->identityProblem(); !problem.isEmpty())
		problems_->raise(identityKey, Severity::Warning, tr("Device"), problem, device);
	else
		problems_->resolve(identityKey);
}
} // namespace telemetry::ux
