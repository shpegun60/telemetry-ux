// Owned terminal Watch completions. Qt-only, independent of SQL and native views. MIT.
#ifndef TELEMETRY_UX_WATCH_FRAME_HPP
#define TELEMETRY_UX_WATCH_FRAME_HPP
#pragma once
#include <QtCore/QDateTime>
#include <QtCore/QMetaType>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>

namespace telemetry::ux {
enum class WatchScalar : quint8 { Unknown = 0, Boolean = 1, U8, S8, U16, S16, U32, S32, U64, S64, F32, F64 };
enum class WatchOperation : quint8 { FieldRead = 1, ReadMany };
enum class WatchCompletionPhase : quint8 { Current = 1, Failed, Drained };
struct WatchValueType {
    quint32 typeId = 0;
    WatchScalar scalar = WatchScalar::Unknown;
    quint8 width = 0;
    bool enumeration = false;
    bool valid() const noexcept {
        unsigned expected = 0;
        switch (scalar) {
        case WatchScalar::Boolean: case WatchScalar::U8: case WatchScalar::S8: expected = 1; break;
        case WatchScalar::U16: case WatchScalar::S16: expected = 2; break;
        case WatchScalar::U32: case WatchScalar::S32: case WatchScalar::F32: expected = 4; break;
        case WatchScalar::U64: case WatchScalar::S64: case WatchScalar::F64: expected = 8; break;
        case WatchScalar::Unknown: break;
        }
        return expected && width == expected &&
            !(enumeration && (scalar == WatchScalar::Boolean ||
                scalar == WatchScalar::F32 || scalar == WatchScalar::F64));
    }
};
struct WatchValueBits {
    WatchValueType type;
    quint8 size = 0; // zero is no value, independently of the declared width
    std::array<std::byte, 8> bytes{}; // canonical little-endian, never converted through double
    bool assign(const std::byte* input, std::size_t count) noexcept {
        size = 0;
        if (!type.valid() || !input || count != type.width) return false;
        std::copy_n(input, count, bytes.begin());
        size = quint8(count);
        return true;
    }
};
struct WatchTimestamp {
    qint64 monotonicNs = 0, utcMs = 0;
    static WatchTimestamp now() noexcept {
        // The steady-clock epoch is shared by this kit's host and library. UTC
        // is receipt time, not device measurement time, and never orders samples.
        return {std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count(),
                QDateTime::currentMSecsSinceEpoch()};
    }
};
struct WatchNativeStatus {
    qint32 client = 0, dispatch = 0, endpoint = 0, codec = 0, value = -1;
};
struct WatchSourceSnapshot {
    quint32 nativeSession = 0;
    quint64 fingerprint = 0, schemaGeneration = 0;
};
struct WatchFrameRow {
    quint32 id = 0;
    qint32 itemStatus = -1, valueStatus = -1; // -1: no per-item result was returned
    WatchValueBits value;
};
struct WatchFrame {
    qint64 sequence = 0, cycle = 0, portion = 0, sourceBinding = 0;
    qint64 startedNs = 0;
    WatchTimestamp completed;
    WatchSourceSnapshot source;
    WatchNativeStatus native;
    qint32 presentationStatus = 0;
    WatchOperation operation = WatchOperation::ReadMany;
    WatchCompletionPhase phase = WatchCompletionPhase::Failed;
    bool urgent = false, correlated = false, exact = false;
    int count = 0; // accepted request count, even when the failure returned zero rows
    std::array<WatchFrameRow, 32> rows{}; // immutable requested order, owned bits
};
} // namespace telemetry::ux
Q_DECLARE_METATYPE(telemetry::ux::WatchFrame)
#endif
