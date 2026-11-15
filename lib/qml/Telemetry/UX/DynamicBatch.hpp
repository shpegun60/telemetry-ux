// Ordered, bounded presentation over one borrowed Session execution source. MIT.
#pragma once
#include "DynamicModels.hpp"
#include "UndoHistory.hpp"
#include <QtCore/QAbstractListModel>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace telemetry::ux {
struct BatchRow {
	client::Operation operation = client::Operation::FieldRead;
	quint32 id = 0, session = 0, requestType = 0, responseType = 0;
	unsigned responseBytes = 0;
	QString name, status = QStringLiteral("Draft");
	QByteArray request, response;
	QVariantMap result;
	bool hasValue = false, historical = false;
	// Who the row is to the history (UR-1): given when the Batch takes it,
	// kept through Execute, the same when Undo puts the row back.
	quint64 uid = 0;
	bool operator==(const BatchRow&) const = default;
};

class BatchListModel final : public QAbstractListModel {
	Q_OBJECT
public:
	enum Role {
		NameRole = Qt::UserRole + 1,
		OperationRole,
		EndpointIdRole,
		StatusRole,
		HasValueRole,
		HistoricalRole,
		ResultRole
	};
	Q_ENUM(Role)
	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	bool changing() const noexcept;
	const std::vector<BatchRow>& rows() const noexcept;
	void replace(std::vector<BatchRow> rows);
	void setValueGuard(std::function<bool()> guard);

private:
	std::vector<BatchRow> rows_;
	bool changing_ = false;
	std::function<bool()> valueGuard_;
};

struct BatchCompletion {
	struct Row {
		quint32 id = 0;
		client::Operation operation = client::Operation::FieldRead;
		QVariantMap result;
		QByteArray payload;
		bool hasValue = false;
	};

	client::Status status = client::Status::Ok;
	QString valueStatus = QStringLiteral("NoValue");
	bool succeeded = false;
	int count = 0;
	std::array<Row, 32> rows{};
};

// The completion slot contains owned bytes/facts only. The native callback
// writes it without calling a QObject or emitting a model notification.
struct BatchSource {
	struct Limits {
		unsigned items = 0, requestBytes = 0, replyBytes = 0;
	};

	struct Request {
		client::Operation operation;
		quint32 id;
		client::Input payload;
	};

	using Completion = std::shared_ptr<std::optional<BatchCompletion>>;
	std::shared_ptr<void> storage;
	std::function<bool()> current, busy;
	std::function<quint32()> session;
	std::function<runtime::RuntimeSchema()> schema;
	std::function<std::optional<Limits>()> capability;
	std::function<client::Submission(std::span<const Request>, Completion)> submit;
};

class DynamicBatch final {
public:
	DynamicBatch();
	~DynamicBatch();
	void setNotify(std::function<void()> notify, std::function<bool()> blocked,
	               std::function<bool()> allowed, std::function<void()> finished);
	void setSource(BatchSource source);
	void retire();
	void invalidateValues();
	void poll();
	bool supported() const;
	// The current device's schema, as the source has it; invalid otherwise.
	runtime::RuntimeSchema schema() const;
	bool pending() const;
	bool active() const;
	bool canAdd(const BatchRow& row) const;
	bool add(BatchRow row);
	// Why rows could not be added, the first thing to fix first (BA-1):
	// Previous when the Batch still holds items of an earlier connection.
	// canAdd(row) is refusal of that row alone being None.
	enum class Refusal { None, Busy, Session, Previous, Permission, Items, Bytes };
	[[nodiscard]] Refusal refusal(std::span<const BatchRow> rows) const;
	// All of rows after the others, in order, as one change; none if any is refused.
	bool add(std::vector<BatchRow> rows);
	// The most items this device takes in one Batch (at most 32); 0 when it has no Batch.
	[[nodiscard]] int itemLimit() const;
	bool remove(int row);
	bool move(int row, int delta);
	bool clear();
	// Where the Batch's changes go as steps (UR-1): add, remove, move and
	// Clear, as "batch.items" by uid with each row's place and the row itself
	// ("Add device.Voltage to the Batch", "Move device.Voltage to item 2 in
	// the Batch", "Clear the Batch"). Moves of one item within two seconds
	// merge. Execute is never a step. Null records nothing. A step puts its
	// rows back whatever the connection: rows of an earlier one come back as
	// historical rows, as a reconnect leaves them.
	void setHistory(UndoHistory* history);
	[[nodiscard]] UndoHistory* history() const;
	// The serial of the step the last change recorded; 0 when it recorded none.
	[[nodiscard]] int lastSerial() const;
	bool select(int row);
	bool canExecute() const;
	bool execute();
	int count() const;
	int selection() const;
	QString status() const;
	QString responseEndpoint() const;
	BatchListModel* model() const;
	ValueModel* responses() const;

private:
	struct Impl;
	std::shared_ptr<Impl> d;
};
} // namespace telemetry::ux

Q_DECLARE_METATYPE(telemetry::ux::BatchRow)
