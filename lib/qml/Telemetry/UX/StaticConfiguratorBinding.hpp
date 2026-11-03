// Host-only generated binding seam. Authors: Ruslan Kovtun, codexAi. MIT.
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <functional>

namespace telemetry::ux {

enum class StaticTypeKind {
	Void,
	Scalar,
	Enum,
	Struct,
	Array
};

struct StaticMember {
	QString name;
	quint32 typeId = 0;
};

struct StaticEnumEntry {
	QString name, code; // Exact decimal wire code, including 64-bit enums.
};

struct StaticType {
	quint32 id = 0;
	StaticTypeKind kind = StaticTypeKind::Void;
	int scalarCode = 0;
	quint32 underlyingTypeId = 0, elementTypeId = 0;
	quint64 count = 0;
	QVector<StaticMember> members;
	QVector<StaticEnumEntry> entries;
};

struct StaticEndpoint {
	int category = 0;
	quint32 id = 0;
	QString catalog, name;
	quint32 valueType = 0, requestType = 0, responseType = 0;
	bool readable = false, writable = false;
	QVariant (*cached)(QObject*) = nullptr;
	bool (*cachedValid)(QObject*) = nullptr;
	QByteArray (*cachedPayload)(QObject*) = nullptr;
	QVariant (*defaultRequest)() = nullptr;
	bool (*validate)(const QVariant&, QString&) = nullptr;
	QString (*read)(QObject*) = nullptr;
	QString (*writePreserving)(QObject*, const QVariant&, const QByteArray&) = nullptr;
	QString (*invoke)(QObject*, const QVariant&) = nullptr;
};

struct StaticConfiguratorBinding {
	// The host owns Api, transport and its one DeviceClient. Call on its thread.
	QPointer<QObject> api;
	QVector<StaticType> types;
	QVector<StaticEndpoint> endpoints;
	bool (*ready)(QObject*) = nullptr;
	quint32 (*session)(QObject*) = nullptr;
	// Connections use context lifetime. Callbacks project existing Api caches;
	// they never submit requests, own a FIFO or create another confirmed cache.
	void (*connect)(QObject*, QObject* context, std::function<void()> changed,
	                std::function<void(QString, QVariantMap)> finished) = nullptr;
};

} // namespace telemetry::ux
