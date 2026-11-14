// Session-independent Qt scheduling over a borrowed execution source. MIT.
#pragma once
#include "WatchlistModel.hpp"
#include "WatchFrame.hpp"
#include <functional>
#include <optional>
#include <span>
#include <array>

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
	};

	client::Status status = client::Status::Ok;
	int count = 0;
	std::array<Row, 32> rows{};
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
	// Optional admitted metadata, copied once on attachment. No read is sent.
	std::function<runtime::RuntimeSchema()> schema;
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
	void setWritable(std::function<bool(quint32)> writable);
	void retire(bool clearRows = true);
	void poll(); // Only after native callback/progress has returned.
	void tick(); // Local scheduler tick, also usable with a controlled test clock.
	QAbstractItemModel* model() const;
	bool supported() const;
	bool active() const;
	bool pending() const;
	bool canAdd(quint32 id) const;
	bool add(quint32 id, QString name);
	QVariantMap addMany(const QVector<QPair<quint32, QString>>& entries);
	void remove(int row);
	void clear();
	// Which of ids the Watchlist can hold: a Field of 1 to 8 bytes of the
	// current device. nullopt while it cannot tell (no source, not current,
	// or in the middle of a change), so a caller does not keep an empty answer.
	std::optional<QSet<quint32>> holdable(const QSet<quint32>& ids) const;
	// Whether holdable can answer now; cheap.
	bool canTellHoldable() const;
	// The Fields held, and a counter that moves whenever they change.
	QSet<quint32> held() const;
	quint64 membership() const;
	// Takes every row of ids out, and puts such rows back; refused (empty,
	// false) while polling or while the Watchlist cannot change.
	QVector<WatchlistModel::Removed> removeMany(const QSet<quint32>& ids);
	bool restore(const QVector<WatchlistModel::Removed>& rows);
	int count() const;
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
