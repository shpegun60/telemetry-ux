// The reads of one cycle in portions within a device's budgets. MIT.
#pragma once
#include "WatchKey.hpp"
#include <QtCore/QHash>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// Pure planning, shared by the Watchlist and, next, the Device tree's previews
// (PV-2): it sees only the keys of what is watched and what the source can
// do, sends nothing, and says why it stops by a code the caller puts in words.
namespace telemetry::ux::read_portions {
// ReadMany's or Batch's budgets as the device advertises them.
struct Budget {
	unsigned items = 0, requestBytes = 0, replyBytes = 0;
};

// The host's Batch request buffer (ConnectionStorage::batchScratch): a Batch
// body never grows past it, whatever the device takes.
inline constexpr unsigned batchRequestScratch = 4096;

// Why a plan ends in a Fail portion.
enum class Failure : quint8 {
	None,
	NoMembers,      // values of a struct or array, and no member read
	NoFieldRead,    // Individual mode, and no FieldRead
	NoReadMany,     // whole Fields, and no ReadMany (or no budget for it)
	NotScalar,      // a whole Field that is stale or not a readable scalar
	ExceedsReadMany // a scalar no ReadMany portion can carry
};

// One read of a cycle. Portions keep the order of their first row: whole
// Fields go together in ReadMany portions (or one FieldRead each in Individual
// mode), and all the values watched in one struct or array Field are one
// member read, wherever their rows are. Where the device has Batch, several
// such Fields are read in one Batch of FieldReads, placed where the first of
// them would have been read. A Fail portion stops the cycle where the read it
// replaces would have gone.
struct Portion {
	enum class Kind : quint8 { Many, Field, Members, Fail, Batch } kind = Kind::Many;
	// A Field of a Batch with the values watched in it, ascending.
	struct Member {
		quint32 field = 0;
		std::vector<std::uint32_t> leaves;
	};
	quint32 field = 0;
	std::vector<std::uint32_t> ids;    // Many: the Fields, at most 32
	std::vector<std::uint32_t> leaves; // Members: ascending ordinals
	std::vector<Member> fields;        // Batch: two or more Fields, at most 32
	Failure failure = Failure::None;   // Fail only

	int rows() const
	{
		if (kind == Kind::Batch) {
			int n = 0;
			for (const auto& member : fields)
				n += int(member.leaves.size());
			return n;
		}
		return kind == Kind::Many      ? int(ids.size())
		       : kind == Kind::Members ? int(leaves.size())
		       : kind == Kind::Field   ? 1
		                               : 0;
	}
};

// What the source can do. budget is asked at most once, and only when a whole
// Field needs ReadMany; batchBudget at most once, and only when two struct or
// array Fields could share a Batch. valueBytes is a struct or array Field's
// size on the wire; a Field it does not know is read on its own.
struct Source {
	bool individual = false;
	bool readMany = false, readField = false, readMembers = false, readBatch = false;
	std::function<std::optional<Budget>()> budget, batchBudget;
	std::function<std::optional<unsigned>(quint32)> fieldBytes, valueBytes;
};

struct Plan {
	std::vector<Portion> portions;
	QHash<quint32, int> of; // the portion that reads a Field
	bool members = false;   // some row is a value inside a Field
};

// The keys in row order; unresolved keys are skipped.
[[nodiscard]] Plan plan(std::span<const WatchKey> keys, const Source& source);
} // namespace telemetry::ux::read_portions
