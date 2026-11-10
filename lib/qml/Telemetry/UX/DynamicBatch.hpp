// Ordered, bounded presentation over one borrowed Session execution source. MIT.
#pragma once
#include "DynamicModels.hpp"
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
	bool pending() const;
	bool active() const;
	bool canAdd(const BatchRow& row) const;
	bool add(BatchRow row);
	bool remove(int row);
	bool move(int row, int delta);
	bool clear();
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
