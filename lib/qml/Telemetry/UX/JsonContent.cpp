/*
 * @file JsonContent.cpp
 * @brief A JSON file read for the viewer: a tree of its values, made as it is opened.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "JsonContent.hpp"
#include "FileContent.hpp"
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <algorithm>
#include <utility>

namespace telemetry::ux {
using Node = JsonTreeModel::Node;

// ---- The model ------------------------------------------------------------

JsonTreeModel::JsonTreeModel(QObject* parent) : QAbstractItemModel(parent), root_(std::make_unique<Node>()) {}

JsonTreeModel::~JsonTreeModel() = default;

void JsonTreeModel::setRoot(std::unique_ptr<Node> root)
{
	beginResetModel();
	root_ = std::move(root);
	endResetModel();
}

Node* JsonTreeModel::node(const QModelIndex& index) const noexcept
{
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

const Node* JsonTreeModel::nodeAt(const QModelIndex& index) const noexcept
{
	return index.isValid() && index.model() == this ? node(index) : nullptr;
}

QModelIndex JsonTreeModel::indexOf(const Node& at) const
{
	return &at == root_.get() ? QModelIndex{} : createIndex(at.row, 0, const_cast<Node*>(&at));
}

void JsonTreeModel::append(Node& under, std::vector<std::unique_ptr<Node>> rows)
{
	if (rows.empty())
		return;
	const auto first = static_cast<int>(under.children.size());
	beginInsertRows(indexOf(under), first, first + static_cast<int>(rows.size()) - 1);
	for (auto& row : rows) {
		row->parent = &under;
		row->row = static_cast<int>(under.children.size());
		under.children.push_back(std::move(row));
	}
	endInsertRows();
}

bool JsonTreeModel::hasChildren(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && (!owner->children.empty() || owner->total > 0);
}

bool JsonTreeModel::canFetchMore(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner && owner->total > 0 && owner->children.empty();
}

void JsonTreeModel::fetchMore(const QModelIndex& parentIndex)
{
	if (auto* owner = node(parentIndex); owner && canFetchMore(parentIndex) && populate)
		populate(*owner);
}

QModelIndex JsonTreeModel::index(int row, int column, const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	if (!owner || column != 0 || row < 0 || row >= static_cast<int>(owner->children.size()))
		return {};
	return createIndex(row, 0, owner->children[static_cast<std::size_t>(row)].get());
}

QModelIndex JsonTreeModel::parent(const QModelIndex& child) const
{
	const auto* at = child.isValid() ? node(child) : nullptr;
	if (!at || !at->parent || at->parent == root_.get())
		return {};
	return createIndex(at->parent->row, 0, at->parent);
}

int JsonTreeModel::rowCount(const QModelIndex& parentIndex) const
{
	const auto* owner = node(parentIndex);
	return owner ? static_cast<int>(owner->children.size()) : 0;
}

int JsonTreeModel::columnCount(const QModelIndex&) const
{
	return 1;
}

QVariant JsonTreeModel::data(const QModelIndex& index, int role) const
{
	const auto* at = index.isValid() ? node(index) : nullptr;
	if (!at)
		return {};
	switch (role) {
		case Qt::DisplayRole:
		case NameRole:
			return at->name;
		case KindRole:
			return at->kind;
		case TypeRole:
			return at->type;
		case ValueRole:
			return at->value;
		case UnavailableRole:
		case ChangedRole:
			return false;
		case CountRole:
		case ChangedFieldsRole:
			return 0;
		case PathRole:
			// A range's rows are its container's: it has no path of its own.
			return at->kind == QStringLiteral("range") ? QString{} : at->path;
		case NumericRole:
			// Right-aligned like numbers, so a column that mixes them does not zigzag.
			return at->kind == QStringLiteral("number") || at->kind == QStringLiteral("bool") ||
			       at->kind == QStringLiteral("null");
		case FindRole:
			return at->kind == QStringLiteral("range") ? QString{} : at->find;
		default:
			return {};
	}
}

QHash<int, QByteArray> JsonTreeModel::roleNames() const
{
	return {{NameRole, "name"},   {KindRole, "kind"},         {TypeRole, "typeText"},
	        {ValueRole, "value"}, {UnavailableRole, "unavailable"}, {ChangedRole, "changed"},
	        {CountRole, "count"}, {PathRole, "path"},           {NumericRole, "numeric"},
	        {ChangedFieldsRole, "changedFields"}};
}

// ---- Reading the text -----------------------------------------------------

namespace {
// The text was checked by Qt's parser first: these only walk it.
bool space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

qsizetype skipSpace(const QByteArray& t, qsizetype p)
{
	while (p < t.size() && space(t[p]))
		++p;
	return p;
}

// From the opening quote to just past the closing one.
qsizetype skipString(const QByteArray& t, qsizetype p)
{
	for (++p; p < t.size(); ++p) {
		if (t[p] == '\\')
			++p;
		else if (t[p] == '"')
			return p + 1;
	}
	return t.size();
}

qsizetype skipValue(const QByteArray& t, qsizetype p)
{
	if (p >= t.size())
		return p;
	const char c = t[p];
	if (c == '"')
		return skipString(t, p);
	if (c == '{' || c == '[') {
		int depth = 0;
		while (p < t.size()) {
			const char d = t[p];
			if (d == '"') {
				p = skipString(t, p);
				continue;
			}
			if (d == '{' || d == '[')
				++depth;
			else if ((d == '}' || d == ']') && --depth == 0)
				return p + 1;
			++p;
		}
		return t.size();
	}
	while (p < t.size() && !space(t[p]) && t[p] != ',' && t[p] != '}' && t[p] != ']')
		++p;
	return p;
}

// A string's text: [start, end) holds it with its quotes.
QString decodeString(const QByteArray& t, qsizetype start, qsizetype end)
{
	QString out;
	const qsizetype last = std::max(start + 1, end - 1);
	qsizetype run = start + 1;
	for (qsizetype p = run; p < last; ++p) {
		if (t[p] != '\\')
			continue;
		out += QString::fromUtf8(t.constData() + run, p - run);
		const char e = p + 1 < last ? t[p + 1] : '\\';
		++p;
		switch (e) {
			case 'n':
				out += QLatin1Char('\n');
				break;
			case 't':
				out += QLatin1Char('\t');
				break;
			case 'r':
				out += QLatin1Char('\r');
				break;
			case 'b':
				out += QLatin1Char('\b');
				break;
			case 'f':
				out += QLatin1Char('\f');
				break;
			case 'u': {
				// A UTF-16 unit; a surrogate pair is two of them in a row.
				bool ok = false;
				const auto unit = QByteArray(t.constData() + p + 1, std::min<qsizetype>(4, last - p - 1)).toUShort(&ok, 16);
				if (ok)
					out += QChar(unit);
				p += 4;
				break;
			}
			default:
				out += QLatin1Char(e);
		}
		run = p + 1;
	}
	if (run < last)
		out += QString::fromUtf8(t.constData() + run, last - run);
	return out;
}

QString kindAt(const QByteArray& t, qsizetype p)
{
	if (p >= t.size())
		return QStringLiteral("null");
	switch (t[p]) {
		case '{':
			return QStringLiteral("object");
		case '[':
			return QStringLiteral("array");
		case '"':
			return QStringLiteral("string");
		case 't':
		case 'f':
			return QStringLiteral("bool");
		case 'n':
			return QStringLiteral("null");
		default:
			return QStringLiteral("number");
	}
}

struct Member {
	QString name;
	// Where the member begins (its key, in an object) and its value's bytes.
	qsizetype from = 0, start = 0, end = 0;
};

// The member after p - just past the opening bracket or the last member -
// or false at the container's end. A key is decoded only when asked.
bool nextMember(const QByteArray& t, bool object, qsizetype& p, Member& out, bool decodeKey = true)
{
	p = skipSpace(t, p);
	if (p < t.size() && t[p] == ',')
		p = skipSpace(t, p + 1);
	if (p >= t.size() || t[p] == '}' || t[p] == ']')
		return false;
	out.from = p;
	if (object) {
		const auto keyEnd = skipString(t, p);
		if (decodeKey)
			out.name = decodeString(t, p, keyEnd);
		p = skipSpace(t, keyEnd);
		if (p < t.size() && t[p] == ':')
			p = skipSpace(t, p + 1);
	}
	out.start = p;
	out.end = skipValue(t, p);
	p = out.end;
	return true;
}

std::uint32_t memberCount(const QByteArray& t, qsizetype start, bool object)
{
	std::uint32_t count = 0;
	qsizetype p = start + 1;
	Member member;
	while (nextMember(t, object, p, member, false))
		++count;
	return count;
}

// A value on its own row, as the file writes it; a long one cut.
QString shown(const QByteArray& t, qsizetype start, qsizetype end)
{
	constexpr qsizetype most = 1000;
	const auto length = end - start;
	return length <= most ? QString::fromUtf8(t.constData() + start, length)
	                      : QString::fromUtf8(t.constData() + start, most) + QStringLiteral("…");
}

// A scalar's text, a string without its quotes or escapes.
QString scalarText(const QByteArray& t, qsizetype start, qsizetype end)
{
	return start < t.size() && t[start] == '"' ? decodeString(t, start, end)
	                                           : QString::fromUtf8(t.constData() + start, end - start);
}

// An object or an array on its own row: its first members, then "…".
QString preview(const QByteArray& t, qsizetype start, bool object)
{
	QStringList parts;
	qsizetype p = start + 1;
	Member member;
	const int most = object ? 4 : 8;
	while (nextMember(t, object, p, member)) {
		if (parts.size() == most) {
			parts << QStringLiteral("…");
			break;
		}
		const auto kind = kindAt(t, member.start);
		const auto value = kind == QStringLiteral("object")  ? QStringLiteral("{…}")
		                   : kind == QStringLiteral("array") ? QStringLiteral("[…]")
		                                                     : shown(t, member.start, std::min(member.end, member.start + 40));
		parts << (object ? member.name + QStringLiteral(": ") + value : value);
	}
	return object ? QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}')
	              : QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
}

QString childPath(const QString& path, bool object, const QString& name)
{
	if (!object)
		return path + name;
	return path.isEmpty() ? name : path + QLatin1Char('.') + name;
}

// Where Qt's parser stopped, as a person counts.
QString placeOf(const QByteArray& t, qsizetype offset)
{
	int line = 1, column = 1;
	for (qsizetype i = 0; i < std::min(offset, t.size()); ++i) {
		if (t[i] == '\n') {
			++line;
			column = 1;
		} else {
			++column;
		}
	}
	return JsonContent::tr("line %1, column %2").arg(line).arg(column);
}
} // namespace

// ---- The content ----------------------------------------------------------

JsonContent::JsonContent(const QByteArray& bytes, QObject* parent)
    : QObject(parent), text_(bytes), tree_(this), shown_(JsonTreeModel::FindRole, JsonTreeModel::PathRole, this)
{
	shown_.setSourceModel(&tree_);
	tree_.populate = [this](Node& node) { populate(node); };
	// A UTF-8 byte order mark is no part of the JSON.
	const qsizetype begin = text_.startsWith("\xEF\xBB\xBF") ? 3 : 0;
	QJsonParseError parsed{};
	(void)QJsonDocument::fromJson(begin ? text_.mid(begin) : text_, &parsed);
	if (parsed.error != QJsonParseError::NoError) {
		error_ = tr("%1 at %2").arg(parsed.errorString(), placeOf(text_, begin + parsed.offset));
		return;
	}
	const auto start = skipSpace(text_, begin);
	const auto kind = kindAt(text_, start);
	if (kind != QStringLiteral("object") && kind != QStringLiteral("array")) {
		error_ = tr("not a JSON object or array");
		return;
	}
	auto root = make(QString{}, QString{}, start, skipValue(text_, start));
	summary_ = root->type;
	// The top level's members for the hex view - the first 4096.
	const bool object = kind == QStringLiteral("object");
	qsizetype p = start + 1;
	Member member;
	for (std::uint32_t i = 0; i < 4096 && nextMember(text_, object, p, member); ++i) {
		const auto name = object ? member.name : QStringLiteral("[%1]").arg(i);
		regions_.push_back(QVariantMap{{QStringLiteral("offset"), static_cast<qint64>(member.from)},
		                               {QStringLiteral("length"), static_cast<qint64>(member.end - member.from)},
		                               {QStringLiteral("name"), name},
		                               {QStringLiteral("path"), name},
		                               {QStringLiteral("kind"), QStringLiteral("token")},
		                               {QStringLiteral("depth"), 0}});
	}
	tree_.setRoot(std::move(root));
	// The top level shown at once: its first step.
	populate(tree_.root());
	valid_ = true;
}

JsonContent::~JsonContent() = default;

std::unique_ptr<Node> JsonContent::make(const QString& name, const QString& path, qsizetype start, qsizetype end) const
{
	auto node = std::make_unique<Node>();
	node->name = name;
	node->path = path;
	node->start = start;
	node->end = end;
	node->kind = kindAt(text_, start);
	if (node->kind == QStringLiteral("object") || node->kind == QStringLiteral("array")) {
		const bool object = node->kind == QStringLiteral("object");
		node->total = memberCount(text_, start, object);
		node->value = preview(text_, start, object);
		node->type = object ? (node->total == 1 ? tr("object, 1 key") : tr("object, %L1 keys").arg(node->total))
		                    : (node->total == 1 ? tr("array, 1 item") : tr("array, %L1 items").arg(node->total));
		node->find = name;
	} else {
		node->value = shown(text_, start, end);
		// JSON's own names for its types.
		node->type = node->kind == QStringLiteral("bool") ? QStringLiteral("boolean") : node->kind;
		// Find looks at the name and the value - a string's text, not its escapes.
		node->find = name + QChar(0x1f) + scalarText(text_, start, end);
	}
	return node;
}

void JsonContent::populate(Node& node)
{
	if (node.total == 0 || !node.children.empty())
		return;
	// A range: the members it spans, of the object or array it belongs to.
	Node* container = &node;
	while (container->kind == QStringLiteral("range") && container->parent)
		container = container->parent;
	makeMembers(node, *container, node.kind == QStringLiteral("range") ? node.first : 0u, node.total);
}

void JsonContent::markBlocks(Node& container) const
{
	if (!container.marks.empty())
		return;
	// One pass: where every 256th member begins, and for an object the first
	// and last key of each block - a range's name.
	const bool object = container.kind == QStringLiteral("object");
	qsizetype p = container.start + 1;
	Member member;
	for (std::uint32_t i = 0; i < container.total; ++i) {
		const auto inBlock = i % rangeSize;
		if (inBlock == 0)
			container.marks.push_back(p);
		const bool boundary = object && (inBlock == 0 || inBlock == rangeSize - 1 || i + 1 == container.total);
		if (!nextMember(text_, object, p, member, boundary))
			break;
		if (boundary && inBlock == 0)
			container.blockKeys << member.name << member.name;
		else if (boundary)
			container.blockKeys.last() = member.name;
	}
}

void JsonContent::makeMembers(Node& under, Node& container, std::uint32_t first, std::uint32_t count)
{
	const bool object = container.kind == QStringLiteral("object");
	std::vector<std::unique_ptr<Node>> rows;
	// More than one range's worth: ranges, as Eclipse shows a long array.
	if (count > rangeSize) {
		markBlocks(container);
		std::uint64_t span = rangeSize;
		while ((count + span - 1) / span > rangeSize)
			span *= rangeSize;
		for (std::uint64_t start = first; start < std::uint64_t{first} + count; start += span) {
			const auto size = static_cast<std::uint32_t>(std::min<std::uint64_t>(span, std::uint64_t{first} + count - start));
			auto range = std::make_unique<Node>();
			// Indexes, written as the elements' own names are: no grouping.
			range->name = tr("[%1 … %2]").arg(start).arg(start + size - 1);
			range->kind = QStringLiteral("range");
			range->type = object ? (size == 1 ? tr("1 key") : tr("%L1 keys").arg(size))
			                     : (size == 1 ? tr("1 item") : tr("%L1 items").arg(size));
			range->path = container.path;
			range->first = static_cast<std::uint32_t>(start);
			range->total = size;
			// An object's range says which keys it holds.
			const auto firstBlock = static_cast<qsizetype>(start / rangeSize);
			const auto lastBlock = static_cast<qsizetype>((start + size - 1) / rangeSize);
			if (object && 2 * lastBlock + 1 < container.blockKeys.size())
				range->value = container.blockKeys.at(2 * firstBlock) + QStringLiteral(" … ") +
				               container.blockKeys.at(2 * lastBlock + 1);
			rows.push_back(std::move(range));
		}
		tree_.append(under, std::move(rows));
		return;
	}
	// The members themselves, read from where their block begins.
	if (first > 0)
		markBlocks(container);
	const auto block = static_cast<std::size_t>(first / rangeSize);
	qsizetype p = first == 0 ? container.start + 1 : container.marks.at(block);
	Member member;
	for (std::uint32_t skipped = static_cast<std::uint32_t>(block) * rangeSize; skipped < first; ++skipped)
		nextMember(text_, object, p, member, false);
	for (auto i = first; i < first + count && nextMember(text_, object, p, member); ++i) {
		const auto name = object ? member.name : QStringLiteral("[%1]").arg(i);
		auto row = make(name, childPath(container.path, object, name), member.start, member.end);
		rows.push_back(std::move(row));
	}
	tree_.append(under, std::move(rows));
}

Node* JsonContent::childAt(Node& parent, std::uint32_t slot, std::size_t& made)
{
	Node* at = &parent;
	for (;;) {
		const auto before = at->children.size();
		populate(*at);
		made += at->children.size() - before;
		if (at->children.empty())
			return nullptr;
		const auto base = at->kind == QStringLiteral("range") ? at->first : 0u;
		if (slot < base)
			return nullptr;
		const auto offset = slot - base;
		// Ranges first: every one but the last holds the same number of members.
		if (at->children.front()->kind == QStringLiteral("range")) {
			const auto index = offset / at->children.front()->total;
			if (index >= at->children.size())
				return nullptr;
			at = at->children[index].get();
			continue;
		}
		return offset < at->children.size() ? at->children[offset].get() : nullptr;
	}
}

void JsonContent::openMatches(const QString& filter)
{
	auto* root = &tree_.root();
	const auto hit = [&](const QString& text, const QString& path) {
		return text.contains(filter, Qt::CaseInsensitive) || path.contains(filter, Qt::CaseInsensitive);
	};
	// The way to every match not inside another: member indexes from the top.
	std::vector<std::vector<std::uint32_t>> routes;
	std::size_t visits = 0;
	std::vector<std::uint32_t> steps;
	std::function<void(qsizetype, bool, const QString&)> walk = [&](qsizetype start, bool object, const QString& path) {
		qsizetype p = start + 1;
		Member member;
		for (std::uint32_t i = 0; nextMember(text_, object, p, member); ++i) {
			if (visits >= maxFindVisits || routes.size() >= maxFindMatches) {
				findLimited_ = true;
				return;
			}
			++visits;
			const auto name = object ? member.name : QStringLiteral("[%1]").arg(i);
			const auto path2 = childPath(path, object, name);
			const auto kind = kindAt(text_, member.start);
			const bool container = kind == QStringLiteral("object") || kind == QStringLiteral("array");
			const auto text = container ? name : name + QChar(0x1f) + scalarText(text_, member.start, member.end);
			steps.push_back(i);
			if (hit(text, path2))
				routes.push_back(steps);
			else if (container)
				walk(member.start, kind == QStringLiteral("object"), path2);
			steps.pop_back();
			if (findLimited_)
				return;
		}
	};
	walk(root->start, root->kind == QStringLiteral("object"), QString{});
	// Each route made, row by row - through the ranges, which make only the
	// rows on the way. The rows made stay, so they are bounded too.
	std::size_t made = 0;
	for (const auto& route : routes) {
		if (made >= maxFindRows) {
			findLimited_ = true;
			break;
		}
		Node* at = root;
		for (const auto slot : route)
			if (!(at = childAt(*at, slot, made)))
				break;
	}
}

int JsonContent::setFilter(const QString& text)
{
	if (!valid_)
		return 0;
	const auto trimmed = text.trimmed();
	// The same find again (Enter after the typing paused): nothing to redo.
	if (trimmed == shown_.text())
		return shown_.setText(trimmed);
	findLimited_ = false;
	if (!trimmed.isEmpty()) {
		// Rows are made with no find shown: a proxy that filters looks through
		// every made row below a parent for each row added there.
		shown_.setText({});
		openMatches(trimmed);
	}
	return shown_.setText(trimmed);
}

bool JsonContent::matches(const QModelIndex& index) const
{
	return shown_.matches(index);
}

QString JsonContent::findLimitText() const
{
	return tr("Find looks through %L1 values, opens %L2 matches and makes %L3 rows at most. Narrow the search to "
	          "find the rest.")
	    .arg(maxFindVisits)
	    .arg(maxFindMatches)
	    .arg(maxFindRows);
}

const Node* JsonContent::nodeOf(const QModelIndex& index) const
{
	if (!index.isValid() || index.model() != &shown_)
		return nullptr;
	return tree_.nodeAt(shown_.mapToSource(index));
}

QString JsonContent::pathOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	return at && at->kind != QStringLiteral("range") ? at->path : QString{};
}

QString JsonContent::valueOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at || at->total > 0 || at->kind == QStringLiteral("object") || at->kind == QStringLiteral("array") ||
	    at->kind == QStringLiteral("range"))
		return {};
	return scalarText(text_, at->start, at->end);
}

QString JsonContent::rowOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	return at->name + QLatin1Char('\t') + at->value + QLatin1Char('\t') + at->type;
}

QString JsonContent::jsonOf(const QModelIndex& index) const
{
	const auto* at = nodeOf(index);
	if (!at)
		return {};
	if (at->kind != QStringLiteral("range"))
		return QString::fromUtf8(text_.constData() + at->start, at->end - at->start);
	// A range: its members as the file writes them, in the brackets of its
	// object or array, so the copy is JSON too.
	const Node* container = at;
	while (container->kind == QStringLiteral("range") && container->parent)
		container = container->parent;
	const bool object = container->kind == QStringLiteral("object");
	if (container->marks.empty())
		return {};
	const auto block = static_cast<std::size_t>(at->first / rangeSize);
	qsizetype p = container->marks.at(block);
	Member member;
	for (std::uint32_t skipped = static_cast<std::uint32_t>(block) * rangeSize; skipped < at->first; ++skipped)
		nextMember(text_, object, p, member, false);
	qsizetype from = -1, end = -1;
	for (std::uint32_t i = 0; i < at->total && nextMember(text_, object, p, member, false); ++i) {
		if (from < 0)
			from = member.from;
		end = member.end;
	}
	if (from < 0)
		return {};
	return QString(object ? QLatin1Char('{') : QLatin1Char('[')) + QString::fromUtf8(text_.constData() + from, end - from) +
	       (object ? QLatin1Char('}') : QLatin1Char(']'));
}

QString JsonContent::jsonOfAll() const
{
	// Without a byte order mark: it is no part of the JSON.
	const qsizetype begin = text_.startsWith("\xEF\xBB\xBF") ? 3 : 0;
	return valid_ ? QString::fromUtf8(text_.constData() + begin, text_.size() - begin) : QString{};
}

bool JsonContent::names(const QString& path)
{
	return path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
}

bool JsonContent::looksLikeJson(const QByteArray& head)
{
	const qsizetype begin = head.startsWith("\xEF\xBB\xBF") ? 3 : 0;
	const auto p = skipSpace(head, begin);
	return p < head.size() && (head[p] == '{' || head[p] == '[');
}

void installJsonCodec(OpenFiles& files)
{
	files.addCodec(FileCodec{
	    QStringLiteral("JSON"),
	    [](const FileIdentity& identity) {
		    if (JsonContent::names(identity.devicePath) || JsonContent::names(identity.localFile))
			    return FileMatch::Path;
		    // A name without an extension: its first bytes decide.
		    const auto& name = identity.devicePath.isEmpty() ? identity.localFile : identity.devicePath;
		    if (QFileInfo(name).suffix().isEmpty() && JsonContent::looksLikeJson(identity.head))
			    return FileMatch::Guess;
		    return FileMatch::No;
	    },
	    [](const QByteArray& bytes, const FileCodecContext& context, QString& error) -> std::unique_ptr<QObject> {
		    auto content = std::make_unique<JsonContent>(bytes);
		    if (!content->valid()) {
			    // Only a file named JSON is wrong for not being JSON; a guess from
			    // its first bytes ("[INFO] start") leaves it to Text unremarked.
			    if (JsonContent::names(context.devicePath) || JsonContent::names(context.localFile))
				    error = content->error();
			    return nullptr;
		    }
		    return content;
	    },
	    QUrl(QStringLiteral("qrc:/qt-project.org/imports/Telemetry/UX/JsonView.qml"))});
}
} // namespace telemetry::ux
