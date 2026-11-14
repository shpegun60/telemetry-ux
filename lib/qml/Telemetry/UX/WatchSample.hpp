// Typed facts beside a Watchlist row's display text. Qt-only. MIT.
#ifndef TELEMETRY_UX_WATCH_SAMPLE_HPP
#define TELEMETRY_UX_WATCH_SAMPLE_HPP
#pragma once

#include <QtCore/QString>
#include <QtCore/QVariant>
#include <cmath>

namespace telemetry::ux {
// A Watchlist reading as a number, taken from the typed value and never parsed
// back from its display text. exact is false when a double cannot carry the
// value without loss (an integer beyond 2^53 - 1) or it is not a finite number
// (NaN, infinity) or not numeric at all; such a value must not be plotted.
struct WatchSample {
	double value = 0;
	bool exact = false;

	static WatchSample ofSigned(long long v) noexcept
	{
		constexpr long long limit = 9007199254740991LL; // 2^53 - 1
		return {static_cast<double>(v), v >= -limit && v <= limit};
	}

	static WatchSample ofUnsigned(unsigned long long v) noexcept
	{
		constexpr unsigned long long limit = 9007199254740991ULL;
		return {static_cast<double>(v), v <= limit};
	}

	static WatchSample ofFloating(double v) noexcept
	{
		return {v, std::isfinite(v)};
	}

	// The model role value: a double when exact, otherwise no value.
	QVariant role() const
	{
		return exact ? QVariant(value) : QVariant();
	}
};

// The most rows one Watchlist holds, in both providers: whole Fields and values
// inside struct or array Fields. One polling cycle then needs at most 32
// ReadMany portions plus one FieldRead per struct or array Field watched;
// adding past it reports "capacity".
inline constexpr int watchlistRowLimit = 1024;

// Stable codes beside a row's display status. Providers word their statuses
// differently ("NotRead", "Not read", refusal texts); the codes do not differ.
// Missing: a path the device attached lacks, or names nothing to watch on it
// (WL-4c); never read, so never recorded.
enum class WatchStatus : int { NotRead = 0, Pending = 1, Ok = 2, Stale = 3, Failed = 4, Missing = 5 };

inline WatchStatus watchStatusCode(const QString& status) noexcept
{
	if (status == QLatin1String("Ok"))
		return WatchStatus::Ok;
	if (status == QLatin1String("Pending"))
		return WatchStatus::Pending;
	if (status == QLatin1String("NotRead") || status == QLatin1String("Not read") ||
	    status.isEmpty())
		return WatchStatus::NotRead;
	if (status == QLatin1String("Stale"))
		return WatchStatus::Stale;
	if (status == QLatin1String("Missing") || status == QLatin1String("Unwatchable"))
		return WatchStatus::Missing;
	return WatchStatus::Failed;
}
} // namespace telemetry::ux
#endif
