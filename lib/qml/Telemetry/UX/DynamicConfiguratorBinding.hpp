// Native view lifetime and host progress boundary. Ruslan Kovtun, codexAi. MIT.
#pragma once
#include "DynamicModels.hpp"
#include <QtCore/QObject>
#include <memory>

namespace telemetry::ux {
class DynamicConfiguratorProvider;
class DynamicViewState;

// One binding borrows one admitted Workbench. It creates no client, transport,
// timer or execution queue. The host calls poll() AFTER progressing the owner.
// ownerStorage retains the Workbench, Descriptor and caller-owned buffers.
// Retire this binding before closing the owner. Destroy only after drained().
class DynamicConfiguratorBinding final : public QObject {
	Q_OBJECT
public:
	DynamicConfiguratorBinding(Workbench& owner, std::shared_ptr<void> ownerStorage,
	                           QObject* parent = nullptr, QString fingerprint = {});
	~DynamicConfiguratorBinding() override;
	bool current() const;
	QString fingerprint() const;
	bool drained() const;
	bool mutationsAllowed() const;
	void setMutationsAllowed(bool allowed);
	void poll();
	void retire();
	// Direct/headless owner mutations must call this after accepted submission.
	// This invalidates presentation data, without revoking core admission.
	void invalidateData();

Q_SIGNALS:
	void changed();
	void managedMutationAccepted();
	// Delivered once after native progress, including panels removed in flight.
	// Owns operation facts only; values remain in each view's guarded snapshot.
	void operationFinished(QVariantMap facts);

private:
	friend class DynamicConfiguratorProvider;
	struct Impl;
	std::unique_ptr<Impl> d;
	std::shared_ptr<DynamicViewState> acquire();
	void mutationAccepted(const std::shared_ptr<DynamicViewState>& origin);
};
} // namespace telemetry::ux
