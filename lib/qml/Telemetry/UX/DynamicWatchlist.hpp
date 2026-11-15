// Session-independent Qt scheduling over a borrowed execution source. MIT.
#pragma once
#include "WatchlistModel.hpp"
#include "WatchFrame.hpp"
#include "WatchLeaves.hpp"
#include <functional>
#include <optional>
#include <span>
#include <array>
#include <vector>

namespace telemetry::ux {
// Host-only dependency boundary: native-only Dynamic providers do not acquire
// Qt Network or a second connection. The Qt Session adapter supplies this seam.
struct WatchlistCompletion {
	struct Row {
		quint32 id = 0;
		QString status, text;
		WatchSample sample; // WatchlistModel::sampleOf() of the same value as text
		qint32 itemStatus = -1, valueStatus = -1;
		WatchValueBits value;
		quint32 leaf = 0; // 0 for a whole Field, else 1 + the value's leaf ordinal
	};

	client::Status status = client::Status::Ok;
	int count = 0;
	std::array<Row, 32> rows{};
	// readMembers only: one row per leaf asked, in its order; count is its size.
	// readBatch: every Field's rows, the Fields in the order asked.
	std::vector<Row> members;
	// readBatch only: each Field's rows in members, and that item's outcome.
	struct Part {
		quint32 field = 0;
		int first = 0, count = 0;
		client::Status status = client::Status::Ok;
		WatchNativeStatus native;
	};
	std::vector<Part> parts;
	WatchNativeStatus native;
	WatchSourceSnapshot source;
	WatchTimestamp completed;
};

struct WatchlistSource {
	struct Limits {
		unsigned items = 0, requestBytes = 0, replyBytes = 0;
	};

	using Completion = std::shared_ptr<std::optional<WatchlistCompletion>>;
	std::shared_ptr<void> storage;
	std::function<bool()> current, busy;
	std::function<std::optional<Limits>()> capability;
	std::function<std::optional<unsigned>(quint32)> fieldBytes;
	// Optional: every Field of the current device fieldBytes accepts, found in
	// one pass. Without it each Field is asked through fieldBytes.
	std::function<QSet<quint32>()> holdable;
	// Accepted submissions retain their callback and backing storage until drain.
	// The completion slot receives owned copies only; it cannot publish Qt signals.
	std::function<client::Submission(std::span<const std::uint32_t>, Completion)> readMany;
	std::function<client::Submission(quint32, Completion)> readField;
	// Optional: one FieldRead of a struct or array Field, answered as the
	// values at these leaf ordinals (ascending, 1 to watchlistRowLimit) in
	// completion.members. Without it the Watchlist holds whole Fields only.
	std::function<client::Submission(quint32 field, std::span<const std::uint32_t> leaves, Completion)>
	    readMembers;
	// Optional, with readMembers: several struct or array Fields in one Batch
	// of FieldReads, answered as readMembers answers one, Field after Field,
	// with completion.parts. batchCapability gives the device's Batch budgets,
	// valueBytes a struct or array Field's size on the wire. Without them each
	// such Field is a read of its own.
	struct BatchField {
		quint32 field = 0;
		std::span<const std::uint32_t> leaves;
	};
	std::function<std::optional<Limits>()> batchCapability;
	std::function<std::optional<unsigned>(quint32)> valueBytes;
	std::function<client::Submission(std::span<const BatchField>, Completion)> readBatch;
	// Optional admitted metadata, copied once on attachment. No read is sent.
	std::function<runtime::RuntimeSchema()> schema;
};

// How the Watchlist keeps pace with its interval (WL-6): from the cycles it
// completed since Start, or from the last Read once. Times in milliseconds.
// A cycle runs from its first request to its last completion; the period from
// one cycle's start to the next's. A tick due while a cycle still runs is
// skipped, so the period polling achieves is a whole number of intervals.
struct WatchPace {
	int cycles = 0;       // completed since Start, or 1 after a Read once
	bool polling = false; // false: the figures are the last Read once's
	// The status is a cycle's progress or completion, not a wait, a failure or
	// a stop: what the pace may stand in for.
	bool routine = false;
	int values = 0; // rows the last cycle read
	// The last cycle's exchanges by kind; again: written Fields read again.
	quint32 readMany = 0, batch = 0, fieldRead = 0, again = 0;
	double lastCycleMs = 0, medianCycleMs = 0, medianPeriodMs = 0;
	// The period polling achieves, a whole number of intervals: the last cycle
	// rounded up to one, or the median period rounded to one, whichever is
	// longer - so a slow cycle shows at once, and the median of five periods
	// lets go only when most of them fit again. Continuous: the median period
	// itself, or the last cycle before two have started.
	double achievedPeriodMs = 0;
	int intervalMs = 0;
	quint64 skippedTicks = 0;
	// Polling reads less often than its interval asks.
	bool behind = false;
};

// What the Watchlist can hold of a Field: the Field itself (a scalar), or its
// values (a struct or an array, as many as leaves).
struct WatchHolding {
	bool aggregate = false;
	quint32 leaves = 1;
};

class DynamicWatchlist final {
public:
	explicit DynamicWatchlist(QObject* context);
	~DynamicWatchlist();
	void setNotify(std::function<void()> notify, std::function<bool()> blocked);
	// Direct owner-thread terminal publication after callback/progress. The value
	// owns its bytes; a recorder receiver performs bounded enqueue only. Stop is
	// allowed there; recursive poll/start is refused until publication returns.
	void setCompletionObserver(std::function<void(const WatchFrame&)> observer);
	void setSource(WatchlistSource source);
	void setWritable(std::function<bool(WatchKey)> writable);
	void setWriteRefusal(std::function<QString(WatchKey)> refusal);
	void retire(bool clearRows = true);
	void poll(); // Only after native callback/progress has returned.
	void tick(); // Local scheduler tick, also usable with a controlled test clock.
	WatchPace pace() const;
	// The monotonic clock the pace is measured with, in nanoseconds; for tests.
	void setClock(std::function<qint64()> monotonicNs);
	QAbstractItemModel* model() const;
	bool supported() const;
	bool active() const;
	bool pending() const;
	bool canAdd(quint32 id) const;
	bool add(quint32 id, QString name);
	QVariantMap addMany(const QVector<QPair<quint32, QString>>& entries);
	// Whole Fields and values inside struct or array Fields in one change,
	// answered as addMany. A value needs readMembers and the source's schema,
	// a Field of at most workbenchSnapshotBytes and a key naming a leaf; its
	// member text and type are taken from the key when left empty.
	QVariantMap addEntries(const QVector<WatchEntry>& entries);
	void remove(int row);
	void clear();
	// Which of ids the Watchlist can hold: a Field of 1 to 8 bytes of the
	// current device, or the values of a struct or array Field (with
	// readMembers, at most workbenchSnapshotBytes, at least one value).
	// nullopt while it cannot tell (no source, not current, or in the middle
	// of a change), so a caller does not keep an empty answer.
	std::optional<QHash<quint32, WatchHolding>> holdable(const QSet<quint32>& ids) const;
	// The rows for the values [first, first + count) of a struct or array
	// Field not held yet, named fieldPath + their suffix ("device.Config" +
	// ".Gain"); nullopt when the Field or the range is not this device's.
	std::optional<QVector<WatchEntry>> entries(quint32 field, quint32 first, quint32 count,
	                                           const QString& fieldPath) const;
	// Whether this connection reads struct members (readMembers).
	bool readsMembers() const;
	// Where a value is in its Field (the steps a write walks); nullopt for a
	// whole Field or a key not of this device.
	std::optional<WatchLeaves::Leaf> leaf(WatchKey key) const;
	// A saved row back to an entry: the Field by its group and name (exactly
	// one), a value inside it by its member text. Missing: no such Field or
	// value; Ambiguous: two Fields of that name; Unwatchable: the Watchlist
	// cannot hold it on this device.
	enum class Resolution { Ok, Missing, Ambiguous, Unwatchable };
	std::pair<Resolution, WatchEntry> resolve(const QString& group, const QString& name,
	                                          const QString& member) const;
	// A typed path as the rows it adds (WL-4b): "device.Config.Gain",
	// "data.Samples[3]", or a struct or array ("device.Config") for all its
	// values. entries are the values not watched yet, in the Field's order;
	// already counts those watched, firstWatched is one of them. missing:
	// this device has no such Field; error says why nothing is added.
	struct PathResolution {
		QVector<WatchEntry> entries;
		quint32 already = 0;
		WatchKey firstWatched = 0;
		bool missing = false;
		QString error;
		quint32 adds = 0;  // the values entries holds, or would hold
	};
	// build false: counts only (entries left empty, adds said in `adds`),
	// cheap enough for every keystroke.
	PathResolution resolvePath(const QString& text, bool build = true) const;
	// Rows for paths this device lacks (WL-4c): unresolved keys, kept in place,
	// found again on every connection. missingEntry makes one for path;
	// rebindRows resolves every such row against the device now attached.
	WatchEntry missingEntry(const QString& path) const;
	// The row kept for path without a device (none, or one that lacks it), or -1.
	[[nodiscard]] int unboundRow(const QString& path) const;
	void rebindRows();
	// What may come next in a typed path, for its completion: groups, a
	// group's Fields, a struct's members, an array's elements, then paths
	// whose last step contains the word typed; at most limit, in the
	// descriptor's order. watched: every value it holds is in the Watchlist.
	struct PathCandidate {
		QString path;
		QString typeText;
		quint32 values = 0;
		bool container = false;
		bool watched = false;
		// What follows a container's path: "." for a struct, "[" for an array.
		QString next;
	};
	QVector<PathCandidate> pathCandidates(const QString& text, int limit = 50) const;
	// A row as it is saved: {group, name} or {group, name, member}; empty
	// when the row or its Field is not known.
	QVariantMap binding(int row) const;
	// Whether holdable can answer now; cheap.
	bool canTellHoldable() const;
	// The keys held, and a counter that moves whenever they change.
	QSet<WatchKey> held() const;
	quint64 membership() const;
	// Takes every row of keys out, and puts such rows back; refused (empty,
	// false) while the Watchlist cannot change. Rows may be added and removed
	// while it polls: a change enters the list at once, and the reads follow
	// it from the next portion on (the read in flight finishes as asked).
	QVector<WatchlistModel::Removed> removeMany(const QSet<WatchKey>& keys);
	bool restore(const QVector<WatchlistModel::Removed>& rows);
	int count() const;
	// Some row is on the device attached: a read would read something.
	bool readable() const;
	QString status() const;
	bool individual() const;
	void setIndividual(bool value);
	int interval() const;
	void setInterval(int milliseconds);
	bool begin(bool polling);
	void stop();
	// After a write, Command, Service or Batch while polling. With the Field
	// written, it is read again before the cycle goes on when this cycle has
	// already read it (or its read was in flight); without one, the next cycle
	// starts as soon as this one ends. Between cycles the next starts now.
	void wake(std::optional<quint32> written = std::nullopt);
	// Another panel changed data on the device. A polling Watchlist keeps
	// polling and reads everything again right after this cycle; otherwise its
	// values are marked stale.
	void dataChanged();

private:
	struct Impl;
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
