// Session-independent Qt scheduling over a borrowed execution source. MIT.
#pragma once
#include "WatchlistModel.hpp"
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
	};

	client::Status status = client::Status::Ok;
	int count = 0;
	std::array<Row, 32> rows{};
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
	// Accepted submissions retain their callback and backing storage until drain.
	// The completion slot receives owned copies only; it cannot publish Qt signals.
	std::function<client::Submission(std::span<const std::uint32_t>, Completion)> readMany;
	std::function<client::Submission(quint32, Completion)> readField;
};

class DynamicWatchlist final {
public:
	explicit DynamicWatchlist(QObject* context);
	~DynamicWatchlist();
	void setNotify(std::function<void()> notify, std::function<bool()> blocked);
	void setSource(WatchlistSource source);
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
	int count() const;
	QString status() const;
	bool individual() const;
	void setIndividual(bool value);
	int interval() const;
	void setInterval(int milliseconds);
	bool begin(bool polling);
	void stop();

private:
	struct Impl;
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux
