// Optional, immutable presentation hints. No SDK, client or wire validation. MIT.
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <memory>

namespace telemetry::ux {
struct PresentationHints {
	QString label, unit, help, minimum, maximum, step;
	bool hasLabel = false;
	QString displayName(const QString& original) const;
	QString displayDetail(const QString& original, const QString& detail) const;
	QString displayValue(const QString& value) const;
};

// Copies share validated, read-only Qt-owned data. Ranges and steps are advisory
// exact text; applying hints never changes the scalar editor or sends I/O.
class PresentationOverlay {
public:
	bool replace(const QByteArray& json, QString& error);
	PresentationHints hints(const QString& fingerprint, int category, quint32 id,
	                        const QString& role, const QVariantList& path = {}) const;
	QString fingerprint() const;

private:
	struct Data;
	std::shared_ptr<const Data> data_;
};
} // namespace telemetry::ux
