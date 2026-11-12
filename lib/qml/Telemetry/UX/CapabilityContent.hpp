/*
 * @file CapabilityContent.hpp
 * @brief A device's capability record (read-many.bin, batch.bin) read for the viewer: what it allows, in words.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The capability codec of the Device files viewer. A device that answers
 * several reads in one request (TRM1, /telemetry/read-many.bin) or takes a
 * mixed ordered batch (TBM1, /telemetry/batch.bin) says so in a record of 24
 * bytes. The SDK's own parsers (ReadManyWire, BatchWire) read it - the ones a
 * connection uses to admit the device - and this shows each field with what
 * it means, the descriptor it binds to (the connected device's or another),
 * and the record's regions for the hex view. Nothing here talks to a device.
 */
#ifndef TELEMETRY_UX_CAPABILITY_CONTENT_HPP
#define TELEMETRY_UX_CAPABILITY_CONTENT_HPP
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <span>

namespace telemetry::ux {
class CapabilityContent final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool valid READ valid CONSTANT)
	Q_PROPERTY(QString error READ error CONSTANT)
	// "Read many" or "Batch".
	Q_PROPERTY(QString kind READ kind CONSTANT)
	// What the device does with it, in one sentence.
	Q_PROPERTY(QString purpose READ purpose CONSTANT)
	// Every field: {name, value, meaning, warn}, in the record's order.
	Q_PROPERTY(QVariantList fields READ fields CONSTANT)
	// The descriptor it binds to, in groups of four, and whether that is the
	// connected device's: "connected", "other", or "" when none is connected.
	Q_PROPERTY(QString fingerprintGroups READ fingerprintGroups CONSTANT)
	Q_PROPERTY(QString binding READ binding CONSTANT)
	// The record's fields, for the hex view.
	Q_PROPERTY(QVariantList regions READ regions CONSTANT)
public:
	// Reads bytes as a TRM1 or TBM1 record; connected is the connected
	// device's descriptor fingerprint, when hasConnected.
	CapabilityContent(const QByteArray& bytes, bool hasConnected, quint64 connected, QObject* parent = nullptr);

	bool valid() const noexcept { return valid_; }
	QString error() const { return error_; }
	QString kind() const { return kind_; }
	QString purpose() const { return purpose_; }
	QVariantList fields() const { return fields_; }
	QString fingerprintGroups() const { return fingerprintGroups_; }
	QString binding() const { return binding_; }
	QVariantList regions() const { return regions_; }
	// Every field as "name<TAB>value" lines, for Copy.
	Q_INVOKABLE QString text() const;

	// Whether a device path names a capability file: the protocol's own.
	static bool names(const QString& devicePath);
	// Whether the bytes begin as one: TRM1 or TBM1.
	static bool signature(const QByteArray& head);
	// The record's fields, from the bytes alone.
	static QVariantList headerRegions(std::span<const std::byte> bytes);

private:
	bool valid_ = false;
	QString error_, kind_, purpose_, fingerprintGroups_, binding_;
	QVariantList fields_, regions_;
};
} // namespace telemetry::ux
#endif
