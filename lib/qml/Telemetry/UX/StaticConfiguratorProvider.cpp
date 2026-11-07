// Shared Qt presentation for generated APIs; no client or wire codec. MIT.
#include "StaticConfiguratorProvider.hpp"
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace telemetry::ux {
namespace {
using Path = QVector<QVariant>;

QString pathKey(const Path& path)
{
	QString key;
	for (const auto& part : path) {
		if (part.metaType().id() == QMetaType::QString)
			key += QStringLiteral("m%1:%2;").arg(part.toString().size()).arg(part.toString());
		else
			key += QStringLiteral("a%1;").arg(part.toInt());
	}
	return key;
}

QVariant atPath(QVariant value, const Path& path)
{
	for (const auto& part : path) {
		if (part.metaType().id() == QMetaType::QString)
			value = value.toMap().value(part.toString());
		else {
			const auto array = value.toList();
			const auto i = part.toInt();
			if (i < 0 || i >= array.size())
				return {};
			value = array[i];
		}
	}
	return value;
}

bool replacePath(QVariant& value, const Path& path, qsizetype depth, const QVariant& replacement)
{
	if (depth == path.size()) {
		value = replacement;
		return true;
	}
	const auto& part = path[depth];
	if (part.metaType().id() == QMetaType::QString) {
		auto map = value.toMap();
		auto it = map.find(part.toString());
		if (it == map.end() || !replacePath(it.value(), path, depth + 1, replacement))
			return false;
		value = map;
	} else {
		auto array = value.toList();
		const auto i = part.toInt();
		if (i < 0 || i >= array.size() || !replacePath(array[i], path, depth + 1, replacement))
			return false;
		value = array;
	}
	return true;
}

const StaticType* typeAt(const StaticConfiguratorBinding& binding, quint32 id)
{
	if (id >= static_cast<quint32>(binding.types.size()) || binding.types[id].id != id)
		return nullptr;
	return &binding.types[id];
}

bool aggregate(const StaticType& type)
{
	return type.kind == StaticTypeKind::Struct || type.kind == StaticTypeKind::Array;
}

QString detail(const StaticType& type)
{
	if (type.kind == StaticTypeKind::Struct)
		return QStringLiteral("struct");
	if (type.kind == StaticTypeKind::Array)
		return QStringLiteral("array[%1]").arg(type.count);
	if (type.kind == StaticTypeKind::Enum)
		return QStringLiteral("enum");
	static const char* const labels[] = {"Void", "Bool", "U8",  "S8",  "U16", "S16",
	                                     "U32",  "S32",  "U64", "S64", "F32", "F64"};
	return type.scalarCode >= 0 && type.scalarCode < 12
	           ? QString::fromLatin1(labels[type.scalarCode])
	           : QString();
}

QString scalarText(const StaticType& type, const QVariant& value, bool editor)
{
	if (type.kind == StaticTypeKind::Scalar && type.scalarCode == 1)
		return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
	if (type.kind == StaticTypeKind::Scalar && (type.scalarCode == 10 || type.scalarCode == 11)) {
		const auto number = value.toDouble();
		if (number == 0 && std::signbit(number))
			return QStringLiteral("-0");
		return QString::number(number, 'g', type.scalarCode == 10 ? 9 : 17);
	}
	const auto code = value.toString();
	if (!editor && type.kind == StaticTypeKind::Enum) {
		for (const auto& entry : type.entries)
			if (entry.code == code)
				return entry.name + QStringLiteral(" (%1)").arg(code);
	}
	return code;
}

bool parseText(const StaticConfiguratorBinding& binding, const StaticType& type,
               const QString& text, QVariant& value)
{
	const auto* scalar =
	    type.kind == StaticTypeKind::Enum ? typeAt(binding, type.underlyingTypeId) : &type;
	if (!scalar || scalar->kind != StaticTypeKind::Scalar || text.isEmpty() ||
	    text != text.trimmed())
		return false;
	if (scalar->scalarCode == 1) {
		if (text == QStringLiteral("true") || text == QStringLiteral("1"))
			value = true;
		else if (text == QStringLiteral("false") || text == QStringLiteral("0"))
			value = false;
		else
			return false;
		return true;
	}
	if (scalar->scalarCode == 8 || scalar->scalarCode == 9) {
		value = text;
		return true;
	}
	bool ok = false;
	if (scalar->scalarCode == 10 || scalar->scalarCode == 11)
		value = text.toDouble(&ok);
	else
		value = text.toLongLong(&ok, 10);
	return ok;
}

struct RootValue {
	quint32 type = 0;
	QVariant value;
	bool valid = false, editable = false;
};

class ValueTree final : public QAbstractItemModel {
public:
	enum Roles {
		Name = Qt::UserRole + 1,
		Detail,
		Value,
		IsAggregate,
		ChildCount,
		LoadedCount,
		Editable,
		EditorText
	};

	struct Node {
		quintptr id = 0, parent = 0;
		QString name;
		quint32 type = 0;
		Path path;
		QVector<quintptr> children;
	};

	ValueTree(const StaticConfiguratorBinding& binding, std::function<RootValue()> root,
	          std::function<QString(const Path&, const QString&)> text,
	          std::function<bool(bool)> mutation, QObject* parent)
	    : QAbstractItemModel(parent), binding_(binding), root_(std::move(root)),
	      text_(std::move(text)), mutation_(std::move(mutation))
	{}

	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"},
		        {Detail, "detail"},
		        {Value, "value"},
		        {IsAggregate, "isAggregate"},
		        {ChildCount, "childCount"},
		        {LoadedCount, "loadedCount"},
		        {Editable, "editable"},
		        {EditorText, "editorText"}};
	}

	const Node* node(const QModelIndex& index) const
	{
		if (!index.isValid() || index.model() != this || index.column() != 0)
			return nullptr;
		const auto it = nodes_.constFind(index.internalId());
		return it == nodes_.cend() ? nullptr : &it.value();
	}

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
	{
		if (row < 0 || column != 0 || (parent.isValid() && !node(parent)))
			return {};
		const auto* p = parent.isValid() ? node(parent) : rootNode();
		if (!p || row >= p->children.size())
			return {};
		return createIndex(row, column, p->children[row]);
	}

	QModelIndex parent(const QModelIndex& index) const override
	{
		const auto* n = node(index);
		if (!n || n->parent == 0)
			return {};
		const auto it = nodes_.constFind(n->parent);
		if (it == nodes_.cend())
			return {};
		const auto parentIt = nodes_.constFind(it->parent);
		if (parentIt == nodes_.cend())
			return {};
		return createIndex(static_cast<int>(parentIt->children.indexOf(it->id)), 0, it->id);
	}

	int rowCount(const QModelIndex& parent = {}) const override
	{
		const auto* n = parent.isValid() ? node(parent) : rootNode();
		return n ? static_cast<int>(n->children.size()) : 0;
	}

	int columnCount(const QModelIndex& = {}) const override
	{
		return 1;
	}

	QVariant data(const QModelIndex& index, int role) const override
	{
		const auto* n = node(index);
		const auto root = root_();
		if (!n)
			return {};
		if (!root.valid) {
			if (role == IsAggregate || role == Editable)
				return false;
			if (role == ChildCount || role == LoadedCount)
				return 0;
			if (role == Qt::DisplayRole || role == Name || role == Detail || role == Value ||
			    role == EditorText)
				return QString();
			return {};
		}
		const auto* type = typeAt(binding_, n->type);
		if (!type)
			return {};
		switch (role) {
			case Qt::DisplayRole:
			case Name:
				return n->name;
			case Detail:
				return detail(*type);
			case Value:
				return aggregate(*type) ? QString()
				                        : scalarText(*type, atPath(root.value, n->path), false);
			case IsAggregate:
				return aggregate(*type);
			case ChildCount:
				return QVariant::fromValue<qulonglong>(count(*n));
			case LoadedCount:
				return n->children.size();
			case Editable:
				return root.editable && !aggregate(*type) && type->kind != StaticTypeKind::Void;
			case EditorText: {
				const auto fallback = aggregate(*type)
				                          ? QString()
				                          : scalarText(*type, atPath(root.value, n->path), true);
				return root.editable ? text_(n->path, fallback) : fallback;
			}
			default:
				return {};
		}
	}

	bool hasChildren(const QModelIndex& parent = {}) const override
	{
		const auto* n = parent.isValid() ? node(parent) : rootNode();
		return n && (!n->children.isEmpty() || count(*n) > 0);
	}

	bool canFetchMore(const QModelIndex& parent) const override
	{
		const auto* n = parent.isValid() ? node(parent) : rootNode();
		return n && root_().valid && static_cast<quint64>(n->children.size()) < count(*n);
	}

	void fetchMore(const QModelIndex& parent) override
	{
		const auto* n = parent.isValid() ? node(parent) : rootNode();
		if (!n || !canFetchMore(parent))
			return;
		const Node copy = *n;
		const auto first = copy.children.size();
		const auto last = std::min<quint64>(count(copy), static_cast<quint64>(first) + 128);
		QVector<Node> pending;
		for (quint64 i = static_cast<quint64>(first); i < last; ++i)
			pending.push_back(child(copy, i));
		if (!mutation_(true))
			return;
		QPointer<ValueTree> alive(this);
		beginInsertRows(parent, static_cast<int>(first), static_cast<int>(last) - 1);
		if (!alive)
			return;
		for (auto& item : pending) {
			nodes_[copy.id].children.push_back(item.id);
			nodes_.insert(item.id, std::move(item));
		}
		endInsertRows();
		if (alive)
			mutation_(false);
	}

	void reset()
	{
		QPointer<ValueTree> alive(this);
		beginResetModel();
		if (!alive)
			return;
		nodes_.clear();
		const auto root = root_();
		if (root.valid) {
			const auto* type = typeAt(binding_, root.type);
			if (type) {
				Node n;
				n.type = root.type;
				nodes_.insert(0, n);
				if (aggregate(*type)) {
					const auto end = std::min<quint64>(count(n), 128);
					for (quint64 i = 0; i < end; ++i) {
						auto item = child(n, i);
						nodes_[0].children.push_back(item.id);
						nodes_.insert(item.id, std::move(item));
					}
				} else if (type->kind != StaticTypeKind::Void) {
					Node item;
					item.id = nextId();
					item.type = root.type;
					item.name = QStringLiteral("value");
					nodes_[0].children.push_back(item.id);
					nodes_.insert(item.id, std::move(item));
				}
			}
		}
		endResetModel();
	}

	void textChanged(const QModelIndex& index)
	{
		Q_EMIT dataChanged(index, index, {EditorText});
	}

private:
	const Node* rootNode() const
	{
		const auto it = nodes_.constFind(0);
		return it == nodes_.cend() ? nullptr : &it.value();
	}

	quint64 count(const Node& n) const
	{
		const auto* type = typeAt(binding_, n.type);
		if (!type)
			return 0;
		if (type->kind == StaticTypeKind::Array)
			return type->count;
		if (type->kind == StaticTypeKind::Struct)
			return static_cast<quint64>(type->members.size());
		return 0;
	}

	quintptr nextId()
	{
		if (next_ == std::numeric_limits<quintptr>::max())
			qFatal("Static Configurator index space exhausted");
		return ++next_;
	}

	Node child(const Node& parent, quint64 index)
	{
		Node n;
		n.id = nextId();
		n.parent = parent.id;
		n.path = parent.path;
		const auto* type = typeAt(binding_, parent.type);
		if (type->kind == StaticTypeKind::Array) {
			n.name = QStringLiteral("[%1]").arg(index);
			n.type = type->elementTypeId;
			n.path.push_back(static_cast<int>(index));
		} else {
			const auto& member = type->members[static_cast<qsizetype>(index)];
			n.name = member.name;
			n.type = member.typeId;
			n.path.push_back(member.name);
		}
		return n;
	}

	const StaticConfiguratorBinding& binding_;
	std::function<RootValue()> root_;
	std::function<QString(const Path&, const QString&)> text_;
	std::function<bool(bool)> mutation_;
	QHash<quintptr, Node> nodes_;
	quintptr next_ = 0;
};

// Only membership and freshness live here. Values remain in the generated Api.
class WatchList final : public QAbstractListModel {
public:
	struct Row {
		int endpoint = -1;
		quint32 epoch = 0;
		QString status = QStringLiteral("NotRead");
		bool published = false;
	};

	enum Roles {
		Name = Qt::UserRole + 1,
		Status,
		Text
	};

	WatchList(const StaticConfiguratorBinding& binding, QObject* parent)
	    : QAbstractListModel(parent), binding_(binding)
	{}

	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"}, {Status, "status"}, {Text, "text"}};
	}

	int rowCount(const QModelIndex& parent = {}) const override
	{
		return parent.isValid() ? 0 : static_cast<int>(rows.size());
	}

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
	{
		return parent.isValid() || column != 0 || row < 0 || row >= rows.size()
		           ? QModelIndex{}
		           : createIndex(row, column, revision_);
	}

	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid() || index.model() != this || index.column() != 0 ||
		    index.internalId() != revision_ || index.row() < 0 || index.row() >= rows.size())
			return {};
		const auto& row = rows[index.row()];
		if (row.endpoint < 0 || row.endpoint >= binding_.endpoints.size())
			return {};
		const auto& e = binding_.endpoints[row.endpoint];
		if (role == Qt::DisplayRole || role == Name)
			return e.catalog + QStringLiteral(" / ") + e.name;
		const bool current = binding_.api && binding_.ready && binding_.ready(binding_.api) &&
		                     binding_.session && row.epoch == binding_.session(binding_.api);
		if (role == Status)
			return current ? row.status : QStringLiteral("Stale");
		if (role == Text) {
			if (!current || !row.published || !e.cachedValid || !e.cachedValid(binding_.api) ||
			    !e.cached)
				return QString();
			const auto* type = typeAt(binding_, e.valueType);
			return type ? scalarText(*type, e.cached(binding_.api), false) : QString();
		}
		return {};
	}

	void replace(QVector<Row> replacement)
	{
		if (revision_ == std::numeric_limits<quintptr>::max())
			qFatal("Static Watchlist index space exhausted");
		QPointer<WatchList> alive(this);
		beginResetModel();
		if (!alive)
			return;
		++revision_;
		rows = std::move(replacement);
		endResetModel();
	}

	void invalidate()
	{
		for (auto& row : rows) {
			row.epoch = 0;
			row.status = QStringLiteral("Stale");
			row.published = false;
		}
	}

	void notify()
	{
		if (!rows.isEmpty())
			Q_EMIT dataChanged(index(0, 0), index(static_cast<int>(rows.size()) - 1, 0),
			                   {Status, Text});
	}

	QVector<Row> rows;

private:
	const StaticConfiguratorBinding& binding_;
	quintptr revision_ = 1;
};

class EndpointTree final : public QAbstractItemModel {
public:
	enum Roles {
		Name = Qt::UserRole + 1,
		Detail,
		IsEndpoint
	};

	explicit EndpointTree(const StaticConfiguratorBinding& binding, QObject* parent)
	    : QAbstractItemModel(parent), binding_(binding)
	{
		// A category/catalog parent followed by its endpoint rows. IDs are immutable
		// within this static binding and never contain pointers into API caches.
		for (int i = 0; i < binding_.endpoints.size(); ++i) {
			const auto& e = binding_.endpoints[i];
			const auto group = QString::number(e.category) + QLatin1Char(':') + e.catalog;
			int row = keys_.indexOf(group);
			if (row < 0) {
				row = keys_.size();
				keys_.push_back(group);
				groups_.push_back({});
			}
			groups_[row].push_back(i);
		}
	}

	QHash<int, QByteArray> roleNames() const override
	{
		return {{Name, "name"}, {Detail, "detail"}, {IsEndpoint, "isEndpoint"}};
	}

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
	{
		if (row < 0 || column != 0)
			return {};
		if (!parent.isValid())
			return row < groups_.size() ? createIndex(row, 0, quintptr(row + 1)) : QModelIndex();
		if (parent.model() != this || parent.internalId() > static_cast<quintptr>(groups_.size()) ||
		    parent.internalId() == 0)
			return {};
		const auto group = static_cast<int>(parent.internalId() - 1);
		if (row >= groups_[group].size())
			return {};
		return createIndex(row, 0, static_cast<quintptr>(groups_.size() + 1 + groups_[group][row]));
	}

	QModelIndex parent(const QModelIndex& index) const override
	{
		const int endpoint = endpointIndex(index);
		if (endpoint < 0)
			return {};
		for (int group = 0; group < groups_.size(); ++group)
			if (groups_[group].contains(endpoint))
				return createIndex(group, 0, quintptr(group + 1));
		return {};
	}

	int rowCount(const QModelIndex& parent = {}) const override
	{
		if (!parent.isValid())
			return groups_.size();
		if (parent.model() != this || parent.column() != 0 || parent.internalId() == 0 ||
		    parent.internalId() > static_cast<quintptr>(groups_.size()))
			return 0;
		return groups_[static_cast<int>(parent.internalId() - 1)].size();
	}

	int columnCount(const QModelIndex& = {}) const override
	{
		return 1;
	}

	int endpointIndex(const QModelIndex& index) const
	{
		if (!index.isValid() || index.model() != this || index.column() != 0)
			return -1;
		const auto id = index.internalId();
		if (id <= static_cast<quintptr>(groups_.size()) ||
		    id > static_cast<quintptr>(groups_.size() + binding_.endpoints.size()))
			return -1;
		return static_cast<int>(id) - groups_.size() - 1;
	}

	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid() || index.model() != this)
			return {};
		const auto endpoint = endpointIndex(index);
		if (endpoint >= 0) {
			const auto& e = binding_.endpoints[endpoint];
			if (role == Qt::DisplayRole || role == Name)
				return e.name;
			if (role == Detail)
				return QStringLiteral("ID %1").arg(e.id);
			if (role == IsEndpoint)
				return true;
		} else if (index.internalId() > 0 &&
		           index.internalId() <= static_cast<quintptr>(groups_.size())) {
			const auto& e =
			    binding_.endpoints[groups_[static_cast<int>(index.internalId() - 1)].front()];
			if (role == Qt::DisplayRole || role == Name)
				return (e.category == 1   ? QStringLiteral("Fields / ")
				        : e.category == 2 ? QStringLiteral("Commands / ")
				                          : QStringLiteral("Services / ")) +
				       e.catalog;
			if (role == Detail)
				return QString();
			if (role == IsEndpoint)
				return false;
		}
		return {};
	}

private:
	const StaticConfiguratorBinding& binding_;
	QStringList keys_;
	QVector<QVector<int>> groups_;
};
} // namespace

struct StaticConfiguratorProvider::Impl {
	enum class Operation {
		None,
		Read,
		Write,
		Readback,
		Command,
		Service,
		WatchRead
	};
	StaticConfiguratorProvider* q;
	StaticConfiguratorBinding binding;
	EndpointTree endpointModel;
	ValueTree valueModel, responseModel;
	WatchList watchModel;
	QTimer watchTimer;
	bool watchRunning = false, watchCycle = false, watchContinuation = false;
	int watchCursor = 0, pendingWatchRow = -1;
	QString watchStatus = QStringLiteral("Individual FieldRead; not started");
	int selected = -1;
	bool allowed = false, pending = false, publishing = false, draftActive = false, retired = false,
	     blocked = false, supported = false, needsReset = false, resetScheduled = false,
	     priorRetired = false;
	quint32 epoch = 0, pendingEpoch = 0, writeEpoch = 0, commandEpoch = 0, serviceEpoch = 0;
	Operation operation = Operation::None;
	QString token, status = QStringLiteral("Not connected"), write, command, service,
	               responseEndpoint;
	QVariant draft, response;
	QByteArray seed;
	quint32 responseType = 0;
	QHash<QString, QString> texts;
	QSet<QString> errors;

	Impl(StaticConfiguratorProvider* owner, StaticConfiguratorBinding value)
	    : q(owner), binding(std::move(value)), endpointModel(binding, owner),
	      valueModel(
	          binding,
	          [this] {
		          return currentValue();
	          },
	          [this](const Path& path, const QString& fallback) {
		          return texts.value(pathKey(path), fallback);
	          },
	          [this](bool entering) {
		          return modelMutation(entering);
	          },
	          owner),
	      responseModel(
	          binding,
	          [this] {
		          return RootValue{responseType, response,
		                           ready() && response.isValid() && operation != Operation::Write &&
		                               operation != Operation::Command &&
		                               operation != Operation::Service,
		                           false};
	          },
	          [](const Path&, const QString& s) {
		          return s;
	          },
	          [this](bool entering) {
		          return modelMutation(entering);
	          },
	          owner),
	      watchModel(binding, owner)
	{
		watchTimer.setParent(owner);
		watchTimer.setObjectName(QStringLiteral("staticConfiguratorWatchTimer"));
		watchTimer.setInterval(1000);
		watchTimer.setTimerType(Qt::PreciseTimer);
		QObject::connect(&watchTimer, &QTimer::timeout, owner, [this] {
			if (!watchRunning || watchCycle || !idle() || draftActive)
				return; // Slow/foreground work consumes no additional queue slots.
			watchCycle = true;
			watchCursor = 0;
			nextWatch();
		});
		if (binding.api && binding.connect)
			binding.connect(
			    binding.api, owner,
			    [this] {
				    changed();
			    },
			    [this](QString t, QVariantMap r) {
				    finished(t, r);
			    });
		epoch = session();
		changed();
	}

	const StaticEndpoint* endpoint() const
	{
		return selected >= 0 && selected < binding.endpoints.size() ? &binding.endpoints[selected]
		                                                            : nullptr;
	}

	bool ready() const
	{
		return binding.api && binding.ready && binding.ready(binding.api);
	}

	quint32 session() const
	{
		return binding.api && binding.session ? binding.session(binding.api) : 0;
	}

	bool owner() const
	{
		return QThread::currentThread() == q->thread();
	}

	bool idle() const
	{
		return owner() && !pending && !publishing && ready();
	}

	bool editable() const
	{
		return idle() && !watchCycle && allowed && !blocked;
	}

	bool watchActive() const
	{
		return watchRunning || watchCycle || (pending && operation == Operation::WatchRead);
	}

	bool watchEligible(int index) const
	{
		if (index < 0 || index >= binding.endpoints.size())
			return false;
		const auto& e = binding.endpoints[index];
		const auto* type = typeAt(binding, e.valueType);
		if (!type || e.category != 1 || !e.readable || !e.read || !e.cached || !e.cachedValid)
			return false;
		if (type->kind == StaticTypeKind::Enum)
			type = typeAt(binding, type->underlyingTypeId);
		return type && type->kind == StaticTypeKind::Scalar && type->scalarCode >= 1 &&
		       type->scalarCode <= 11;
	}

	void replaceWatch(QVector<WatchList::Row> rows)
	{
		publishing = true;
		QPointer<StaticConfiguratorProvider> alive(q);
		watchModel.replace(std::move(rows));
		if (!alive)
			return;
		publishing = false;
		publish(false);
	}

	void continueWatch()
	{
		if (!watchCycle || watchContinuation)
			return;
		watchContinuation = true;
		QMetaObject::invokeMethod(
		    q,
		    [this] {
			    watchContinuation = false;
			    if (watchCycle && !pending && !publishing)
				    nextWatch();
		    },
		    Qt::QueuedConnection);
	}

	void nextWatch()
	{
		if (!watchCycle || !idle() || draftActive)
			return;
		if (watchCursor >= watchModel.rows.size()) {
			watchCycle = false;
			watchStatus = watchRunning ? QStringLiteral("Individual polling")
			                           : QStringLiteral("Individual cycle completed");
			publish(false);
			return;
		}
		const auto rowIndex = watchCursor++;
		const auto endpointIndex = watchModel.rows[rowIndex].endpoint;
		if (!watchEligible(endpointIndex)) {
			watchModel.rows[rowIndex].status = QStringLiteral("Unsupported");
			watchModel.rows[rowIndex].published = false;
			continueWatch();
			return;
		}
		pending = true;
		operation = Operation::WatchRead;
		pendingWatchRow = rowIndex;
		pendingEpoch = session();
		token.clear();
		watchStatus = QStringLiteral("Individual FieldRead %1/%2")
		                  .arg(rowIndex + 1)
		                  .arg(watchModel.rows.size());
		auto& row = watchModel.rows[rowIndex];
		row.epoch = pendingEpoch;
		row.status = QStringLiteral("Pending");
		row.published = false;
		QPointer<StaticConfiguratorProvider> alive(q);
		const auto issued = binding.endpoints[endpointIndex].read(binding.api);
		if (!alive)
			return;
		if (issued.isEmpty()) {
			pending = false;
			operation = Operation::None;
			pendingWatchRow = -1;
			if (rowIndex < watchModel.rows.size())
				watchModel.rows[rowIndex].status = QStringLiteral("SubmissionRefused");
			continueWatch();
		} else
			token = issued;
		publish(false);
	}

	void beginWatch(bool polling)
	{
		if (!owner() || publishing || watchActive() || !idle() || draftActive ||
		    watchModel.rows.isEmpty())
			return;
		watchRunning = polling;
		watchCycle = true;
		watchCursor = 0;
		if (polling)
			watchTimer.start();
		nextWatch();
	}

	void clearDraft()
	{
		draftActive = false;
		draft.clear();
		seed.clear();
		texts.clear();
		errors.clear();
	}

	RootValue currentValue() const
	{
		const auto* e = endpoint();
		if (!e || !ready() || !supported)
			return {};
		if (draftActive)
			return {e->category == 1 ? e->valueType : e->requestType, draft, true,
			        ready() && !pending && allowed && !blocked};
		if (e->category != 1 || retired || !e->cachedValid || !e->cachedValid(binding.api) ||
		    !e->cached)
			return {};
		return {e->valueType, e->cached(binding.api), true, false};
	}

	bool typeBudget(quint32 id, quint64& nodes, int depth = 0) const
	{
		if (depth > 64 || nodes == 0)
			return false;
		--nodes;
		const auto* type = typeAt(binding, id);
		if (!type)
			return false;
		if (type->kind == StaticTypeKind::Struct) {
			for (const auto& member : type->members)
				if (!typeBudget(member.typeId, nodes, depth + 1))
					return false;
		} else if (type->kind == StaticTypeKind::Array) {
			if (type->count > nodes || type->count > 65536)
				return false;
			for (quint64 i = 0; i < type->count; ++i)
				if (!typeBudget(type->elementTypeId, nodes, depth + 1))
					return false;
		}
		return true;
	}

	void publish(bool reset = true)
	{
		if (publishing) {
			needsReset = needsReset || reset;
			return;
		}
		publishing = true;
		QPointer<StaticConfiguratorProvider> alive(q);
		if (reset) {
			valueModel.reset();
			if (!alive)
				return;
			responseModel.reset();
			if (!alive)
				return;
		}
		watchModel.notify();
		if (!alive)
			return;
		publishing = false;
		if (needsReset) {
			scheduleReset();
			return;
		}
		Q_EMIT q->stateChanged();
	}

	void scheduleReset()
	{
		if (resetScheduled)
			return;
		resetScheduled = true;
		// Coalesce presentation notification only. The context cancels this call
		// on destruction; no operation, client processing or polling is queued.
		QMetaObject::invokeMethod(
		    q,
		    [this] {
			    resetScheduled = false;
			    if (!needsReset)
				    return;
			    needsReset = false;
			    publish();
		    },
		    Qt::QueuedConnection);
	}

	bool modelMutation(bool entering)
	{
		if (entering) {
			if (publishing)
				return false;
			publishing = true;
			return true;
		}
		publishing = false;
		if (needsReset)
			scheduleReset();
		return true;
	}

	void changed()
	{
		const auto now = session();
		if (now != epoch || !ready()) {
			epoch = now;
			allowed = false;
			watchRunning = watchCycle = false;
			watchTimer.stop();
			watchModel.invalidate();
			watchStatus = QStringLiteral("Stale; start an explicit new cycle after admission");
			clearDraft();
			retired = true;
			response.clear();
			responseType = 0;
			responseEndpoint.clear();
			blocked = false;
			status = ready() ? QStringLiteral("Connected") : QStringLiteral("Not connected");
		}
		publish();
	}

	QString outcome(const QVariantMap& result, Operation op) const
	{
		QString text;
		if (op == Operation::Command && result.value(QStringLiteral("accepted")).toBool()) {
			text = QStringLiteral("Accepted; application completion is not confirmed");
		} else if (result.value(QStringLiteral("success")).toBool()) {
			if (op == Operation::Command)
				text = QStringLiteral("Executed");
			else if (op == Operation::Write)
				text = QStringLiteral("Applied; confirmed by device");
			else if (op == Operation::Service) {
				const auto* e = endpoint();
				text = e && e->responseType != 0 ? QStringLiteral("Completed; value available")
				                                 : QStringLiteral("Completed; no response value");
			} else
				text = QStringLiteral("Completed");
		} else {
			text = result.value(QStringLiteral("errorKind")).toString();
			const auto message = result.value(QStringLiteral("message")).toString();
			bool named = false;
			if (text == QStringLiteral("Endpoint")) {
				const QStringList names =
				    op == Operation::Write
				        ? QStringList{QStringLiteral("Applied"),  QStringLiteral("NotFound"),
				                      QStringLiteral("ReadOnly"), QStringLiteral("InvalidValue"),
				                      QStringLiteral("Busy"),     QStringLiteral("Unavailable")}
				    : op == Operation::Command
				        ? QStringList{QStringLiteral("Executed"),
				                      QStringLiteral("Accepted"),
				                      QStringLiteral("NotFound"),
				                      QStringLiteral("Unavailable"),
				                      QStringLiteral("ArgumentCountMismatch"),
				                      QStringLiteral("InvalidValue"),
				                      QStringLiteral("Busy"),
				                      QStringLiteral("Failed")}
				    : op == Operation::Service
				        ? QStringList{QStringLiteral("Ok"), QStringLiteral("InvalidArgument"),
				                      QStringLiteral("Unavailable"), QStringLiteral("Busy"),
				                      QStringLiteral("Failed")}
				        : QStringList{};
				const auto statusCode = result.value(QStringLiteral("endpointStatus")).toInt();
				if (statusCode >= 0 && statusCode < names.size()) {
					text = names[statusCode];
					named = true;
				}
			}
			if (!message.isEmpty() &&
			    !(named && message.startsWith(QStringLiteral("Endpoint status "))))
				text += QStringLiteral(" · ") + message;
			if (text.isEmpty())
				text = QStringLiteral("Operation refused");
		}
		return text;
	}

	QString historical(const QString& text, quint32 origin) const
	{
		return !text.isEmpty() && (!ready() || origin != session())
		           ? text + QStringLiteral(" · previous session")
		           : text;
	}

	void start(Operation op)
	{
		const auto* e = endpoint();
		if (!e || !idle() || watchCycle)
			return;
		// Snapshot arguments before calling generated conversion code, which can
		// run application QJS getters. No borrowed cache/draft survives reentry.
		const auto args = draft;
		const QByteArray original = seed;
		const auto selectedBefore = selected;
		priorRetired = retired;
		pending = true;
		operation = op;
		pendingEpoch = session();
		token.clear();
		status = QStringLiteral("Pending");
		if (op == Operation::Write || op == Operation::Command || op == Operation::Service) {
			watchRunning = false;
			watchTimer.stop();
			watchModel.invalidate();
			watchStatus = QStringLiteral("Stopped for foreground mutation; read again explicitly");
		}
		QPointer<StaticConfiguratorProvider> alive(q);
		QString issued;
		if ((op == Operation::Read || op == Operation::Readback) && e->read)
			issued = e->read(binding.api);
		else if (op == Operation::Write && e->writePreserving)
			issued = e->writePreserving(binding.api, args, original);
		else if ((op == Operation::Command || op == Operation::Service) && e->invoke)
			issued = e->invoke(binding.api, args);
		if (!alive)
			return;
		if (issued.isEmpty()) {
			pending = false;
			operation = Operation::None;
			status = QStringLiteral("Submission refused");
			publish(false);
			return;
		}
		token = issued;
		if (selectedBefore == selected &&
		    (op == Operation::Write || op == Operation::Read || op == Operation::Readback))
			retired = true;
		publish();
	}

	void finished(const QString& completed, const QVariantMap& result)
	{
		if (!pending || token.isEmpty() || completed != token)
			return;
		const auto op = operation;
		const bool success = result.value(QStringLiteral("success")).toBool();
		const bool current = ready() && pendingEpoch == session();
		const auto error = result.value(QStringLiteral("errorKind")).toString();
		if (op == Operation::WatchRead) {
			const auto rowIndex = pendingWatchRow;
			pending = false;
			operation = Operation::None;
			pendingWatchRow = -1;
			token.clear();
			if (rowIndex >= 0 && rowIndex < watchModel.rows.size()) {
				auto& row = watchModel.rows[rowIndex];
				row.published = success && current;
				row.epoch = current ? pendingEpoch : 0;
				row.status = !current  ? QStringLiteral("Stale")
				             : success ? QStringLiteral("Ok")
				                       : error;
				if (row.status.isEmpty())
					row.status = QStringLiteral("ReadFailed");
				const auto& e = binding.endpoints[row.endpoint];
				if (row.published && (!e.cachedValid || !e.cachedValid(binding.api))) {
					row.published = false;
					row.status = QStringLiteral("NoValue");
				}
				if (current && !success && error == QStringLiteral("Dispatch")) {
					const auto dispatch = result.value(QStringLiteral("dispatchStatus")).toInt();
					if (dispatch == 4)
						row.status = QStringLiteral("NotFound");
					if (dispatch == 9)
						row.status = QStringLiteral("Unavailable");
				}
			}
			if (!current || error == QStringLiteral("RecoveryRequired") ||
			    error == QStringLiteral("Schema") || error == QStringLiteral("OutcomeUnknown")) {
				watchRunning = watchCycle = false;
				watchTimer.stop();
				watchStatus =
				    current ? error + QStringLiteral("; polling stopped")
				            : QStringLiteral("Stale; start an explicit new cycle after admission");
			} else if (!watchCycle && !watchRunning)
				watchStatus = QStringLiteral("Stopped");
			QPointer<StaticConfiguratorProvider> alive(q);
			publish(false);
			if (alive)
				continueWatch();
			return;
		}
		const bool refusal =
		    error == QStringLiteral("QueueFull") || error == QStringLiteral("Busy");
		const auto text = outcome(result, op);
		pending = false;
		operation = Operation::None;
		token.clear();
		status = text;
		if (current && error == QStringLiteral("OutcomeUnknown"))
			blocked = true;
		// A refused enqueue preserves the previous owned response. An accepted
		// mutation retires it even when execution failed or its result is unknown.
		if ((op == Operation::Write || op == Operation::Command || op == Operation::Service) &&
		    (!refusal || !current)) {
			response.clear();
			responseType = 0;
			responseEndpoint.clear();
		}
		if (op == Operation::Write) {
			write = text;
			writeEpoch = pendingEpoch;
			if (!refusal || !current)
				clearDraft();
			if (refusal && current)
				retired = priorRetired;
		} else if (op == Operation::Command) {
			command = text;
			commandEpoch = pendingEpoch;
		} else if (op == Operation::Service) {
			service = text;
			serviceEpoch = pendingEpoch;
			if (!refusal || !current) {
				const auto* e = endpoint();
				if (success && current && e && e->responseType != 0) {
					response = result.value(QStringLiteral("value"));
					responseType = e->responseType;
					responseEndpoint = e->catalog + QStringLiteral(" / ") + e->name;
				}
			}
		} else if (op == Operation::Read || op == Operation::Readback) {
			if (success && current)
				retired = false;
			else if (refusal && current)
				retired = priorRetired;
		}
		QPointer<StaticConfiguratorProvider> alive(q);
		if (op == Operation::Write && success && current) {
			start(Operation::Readback);
			return;
		}
		publish();
		if (!alive)
			return;
		if (op == Operation::Read || op == Operation::Readback) {
			Q_EMIT q->readFinished(success && current);
		}
		if (alive)
			continueWatch();
	}
};

StaticConfiguratorProvider::StaticConfiguratorProvider(StaticConfiguratorBinding binding,
                                                       QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this, std::move(binding)))
{}

StaticConfiguratorProvider::~StaticConfiguratorProvider() = default;

QAbstractItemModel* StaticConfiguratorProvider::endpoints() const
{
	return &d->endpointModel;
}

QAbstractItemModel* StaticConfiguratorProvider::values() const
{
	return &d->valueModel;
}

QAbstractItemModel* StaticConfiguratorProvider::responses() const
{
	return &d->responseModel;
}

QAbstractItemModel* StaticConfiguratorProvider::watchlist() const
{
	return &d->watchModel;
}

bool StaticConfiguratorProvider::watchActive() const
{
	return d->watchActive();
}

bool StaticConfiguratorProvider::watchPending() const
{
	return d->pending && d->operation == Impl::Operation::WatchRead;
}

bool StaticConfiguratorProvider::canAddWatch() const
{
	if (!d->idle() || d->watchActive() || !d->watchEligible(d->selected) ||
	    d->watchModel.rows.size() >= std::numeric_limits<int>::max())
		return false;
	for (const auto& row : d->watchModel.rows)
		if (row.endpoint == d->selected)
			return false;
	return true;
}

int StaticConfiguratorProvider::watchCount() const
{
	return static_cast<int>(d->watchModel.rows.size());
}

QString StaticConfiguratorProvider::watchStatus() const
{
	return d->watchStatus;
}

int StaticConfiguratorProvider::watchIntervalMs() const
{
	return d->watchTimer.interval();
}

void StaticConfiguratorProvider::setWatchIndividual(bool individual)
{
	if (!d->owner() || d->publishing || d->watchActive() || individual)
		return;
	d->watchStatus =
	    QStringLiteral("ReadMany is unavailable; choose Individual FieldRead explicitly");
	d->publish(false);
}

void StaticConfiguratorProvider::setWatchIntervalMs(int interval)
{
	if (!d->owner() || d->publishing || d->watchActive() || interval < 1 || interval > 60000 ||
	    interval == d->watchTimer.interval())
		return;
	d->watchTimer.setInterval(interval);
	d->publish(false);
}

bool StaticConfiguratorProvider::addWatch()
{
	if (!canAddWatch())
		return false;
	auto rows = d->watchModel.rows;
	rows.push_back({d->selected, d->session(), QStringLiteral("NotRead"), false});
	d->replaceWatch(std::move(rows));
	return true;
}

void StaticConfiguratorProvider::removeWatch(int row)
{
	if (!d->owner() || d->publishing || d->watchActive() || row < 0 ||
	    row >= d->watchModel.rows.size())
		return;
	auto rows = d->watchModel.rows;
	rows.removeAt(row);
	d->replaceWatch(std::move(rows));
}

void StaticConfiguratorProvider::clearWatch()
{
	if (!d->owner() || d->publishing || d->watchActive() || d->watchModel.rows.isEmpty())
		return;
	d->replaceWatch({});
}

void StaticConfiguratorProvider::refreshWatch()
{
	d->beginWatch(false);
}

void StaticConfiguratorProvider::startWatch()
{
	d->beginWatch(true);
}

void StaticConfiguratorProvider::stopWatch()
{
	if (!d->owner() || d->publishing)
		return;
	d->watchRunning = d->watchCycle = false;
	d->watchTimer.stop();
	d->watchStatus = watchPending()
	                     ? QStringLiteral("Stopping; draining the current Individual read")
	                     : QStringLiteral("Stopped");
	d->publish(false);
}

QString StaticConfiguratorProvider::selection() const
{
	const auto* e = d->endpoint();
	return e ? e->catalog + QStringLiteral(" / ") + e->name : QString();
}

QString StaticConfiguratorProvider::serviceResponseEndpoint() const
{
	return d->responseEndpoint;
}

bool StaticConfiguratorProvider::connected() const
{
	return d->ready();
}

bool StaticConfiguratorProvider::busy() const
{
	return d->pending;
}

bool StaticConfiguratorProvider::editing() const
{
	const auto* e = d->endpoint();
	return e && e->category == 1 && d->draftActive;
}

bool StaticConfiguratorProvider::allowMutations() const
{
	return d->allowed;
}

void StaticConfiguratorProvider::setAllowMutations(bool allowed)
{
	if (!d->owner() || d->publishing || d->allowed == allowed ||
	    (allowed && (!d->ready() || d->epoch != d->session())))
		return;
	d->allowed = allowed;
	d->publish();
}

bool StaticConfiguratorProvider::canEdit() const
{
	const auto* e = d->endpoint();
	return d->editable() && e && e->category == 1 && e->writable && !d->draftActive &&
	       d->currentValue().valid;
}

bool StaticConfiguratorProvider::canApply() const
{
	const auto* e = d->endpoint();
	return d->editable() && e && e->category == 1 && e->writable && d->draftActive &&
	       d->errors.isEmpty();
}

bool StaticConfiguratorProvider::canRefresh() const
{
	const auto* e = d->endpoint();
	return d->idle() && !d->watchCycle && d->supported && e && e->category == 1 && e->readable;
}

bool StaticConfiguratorProvider::commandSelected() const
{
	const auto* e = d->endpoint();
	return e && e->category == 2;
}

bool StaticConfiguratorProvider::canExecute() const
{
	return d->editable() && commandSelected() && d->draftActive && d->errors.isEmpty();
}

bool StaticConfiguratorProvider::serviceSelected() const
{
	const auto* e = d->endpoint();
	return e && e->category == 3;
}

bool StaticConfiguratorProvider::canCallService() const
{
	return d->editable() && serviceSelected() && d->draftActive && d->errors.isEmpty();
}

QString StaticConfiguratorProvider::writeOutcome() const
{
	return d->historical(d->write, d->writeEpoch);
}

QString StaticConfiguratorProvider::commandOutcome() const
{
	return d->historical(d->command, d->commandEpoch);
}

QString StaticConfiguratorProvider::serviceOutcome() const
{
	return d->historical(d->service, d->serviceEpoch);
}

QString StaticConfiguratorProvider::statusText() const
{
	return d->status;
}

void StaticConfiguratorProvider::selectEndpoint(const QModelIndex& index)
{
	if (!d->owner() || d->publishing || d->pending || d->watchCycle)
		return;
	const auto selected = d->endpointModel.endpointIndex(index);
	if (selected < 0 || selected == d->selected)
		return;
	d->selected = selected;
	d->clearDraft();
	d->retired = false;
	d->supported = false;
	const auto* e = d->endpoint();
	quint64 requestNodes = 65536, responseNodes = 65536;
	d->supported = d->typeBudget(e->category == 1 ? e->valueType : e->requestType, requestNodes) &&
	               (e->category != 3 || d->typeBudget(e->responseType, responseNodes));
	if (!d->supported) {
		d->status = QStringLiteral("Endpoint exceeds presentation limits");
		d->publish();
		return;
	}
	if (e->category != 1 && (e->requestType == 0 || e->defaultRequest)) {
		quint64 nodes = 65536;
		if (!d->typeBudget(e->requestType, nodes))
			d->status = QStringLiteral("Request exceeds presentation limits");
		else {
			d->draft = e->requestType == 0 ? QVariant() : e->defaultRequest();
			d->draftActive = true;
			d->status = QStringLiteral("Initial request values; device defaults may differ");
		}
	}
	d->publish();
}

void StaticConfiguratorProvider::refresh()
{
	if (canRefresh())
		d->start(Impl::Operation::Read);
}

void StaticConfiguratorProvider::beginEdit()
{
	if (!canEdit())
		return;
	const auto* e = d->endpoint();
	quint64 nodes = 65536;
	if (!d->typeBudget(e->valueType, nodes) || !e->cachedPayload) {
		d->status = QStringLiteral("Value exceeds presentation limits");
		d->publish(false);
		return;
	}
	d->seed = e->cachedPayload(d->binding.api);
	d->draft = e->cached(d->binding.api);
	d->draftActive = true;
	d->texts.clear();
	d->errors.clear();
	d->publish();
}

void StaticConfiguratorProvider::cancelEdit()
{
	if (!d->idle() || d->publishing || !editing())
		return;
	d->clearDraft();
	d->publish();
}

void StaticConfiguratorProvider::apply()
{
	if (canApply())
		d->start(Impl::Operation::Write);
}

void StaticConfiguratorProvider::executeCommand()
{
	if (canExecute())
		d->start(Impl::Operation::Command);
}

void StaticConfiguratorProvider::callService()
{
	if (canCallService())
		d->start(Impl::Operation::Service);
}

bool StaticConfiguratorProvider::setScalarText(const QModelIndex& index, const QString& text)
{
	if (!d->editable() || !d->draftActive)
		return false;
	const auto* node = d->valueModel.node(index);
	if (!node)
		return false;
	const auto* type = typeAt(d->binding, node->type);
	const auto* endpoint = d->endpoint();
	if (!type || aggregate(*type) || !endpoint || !endpoint->validate)
		return false;
	const Path path = node->path;
	const auto key = pathKey(path);
	QVariant value, candidate = d->draft;
	QString error;
	const bool valid = parseText(d->binding, *type, text, value) &&
	                   replacePath(candidate, path, 0, value) &&
	                   endpoint->validate(candidate, error);
	d->texts.insert(key, text);
	if (valid) {
		d->draft = std::move(candidate);
		d->errors.remove(key);
	} else
		d->errors.insert(key);
	QPointer<StaticConfiguratorProvider> alive(this);
	d->publishing = true;
	d->valueModel.textChanged(index);
	if (!alive)
		return valid;
	d->publishing = false;
	d->publish(false);
	return valid;
}
} // namespace telemetry::ux
