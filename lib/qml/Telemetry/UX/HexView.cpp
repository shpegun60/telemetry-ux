/*
 * @file HexView.cpp
 * @brief The hex grid: layout, painting, cursor and selection, go to, copy, the inspector.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 */
#include "HexView.hpp"
#include <utility>
#include "FileContent.hpp"
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QThreadPool>
#include <QtCore/QStringDecoder>
#include <QtCore/QTimeZone>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QFontMetricsF>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace telemetry::ux {
namespace {
constexpr qreal padding = 8;
// Characters between the offset and the bytes, and between the bytes and the text.
constexpr int gutterGap = 2;

bool printable(char c)
{
	const auto byte = static_cast<unsigned char>(c);
	return byte >= 0x20 && byte < 0x7f;
}

QString hex(qint64 value, int digits)
{
	return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}
} // namespace

HexView::HexView(QQuickItem* parent) : QQuickPaintedItem(parent)
{
	setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
	setAcceptHoverEvents(true);
	setFlag(ItemAcceptsInputMethod, false);
	setActiveFocusOnTab(true);
	setOpaquePainting(false);
	font_.setStyleHint(QFont::Monospace);
	font_.setFamily(QStringLiteral("Consolas"));
	connect(this, &HexView::coloursChanged, this, [this] {
		update();
	});
	connect(this, &QQuickItem::activeFocusChanged, this, [this] {
		update();
	});
}

QObject* HexView::content() const
{
	return content_;
}

void HexView::setContent(QObject* content)
{
	auto* file = qobject_cast<FileContent*>(content);
	if (file == content_)
		return;
	if (content_)
		disconnect(content_, nullptr, this, nullptr);
	content_ = file;
	if (content_) {
		connect(content_, &FileContent::changed, this, &HexView::reload);
		// An edit changes the content's bytes in place: this copy lets go
		// first, so the edit copies nothing, and takes them again after.
		connect(content_, &FileContent::aboutToEdit, this, [this] { bytes_ = QByteArray(); });
		connect(content_, &FileContent::bytesEdited, this, [this] {
			bytes_ = content_->bytes();
			if (!pattern_.isEmpty())
				countMatches();
			changed();
		});
		connect(content_, &FileContent::editChanged, this, [this] {
			nibble_ = 0;
			changed();
		});
	}
	reload();
	Q_EMIT contentChanged();
}

bool HexView::editing() const
{
	return content_ && content_->editing();
}

bool HexView::typeDigit(int value)
{
	if (!editing() || bytes_.isEmpty())
		return false;
	const auto at = cursor_;
	const auto byte = static_cast<unsigned char>(std::as_const(bytes_)[at]);
	const bool second = nibble_ == 1;
	const auto written = second ? (byte & 0xF0) | value : (value << 4) | (byte & 0x0F);
	anchor_ = cursor_;
	content_->overwrite(at, QByteArray(1, static_cast<char>(written)), true);
	if (second) {
		moveTo(std::min<qint64>(at + 1, bytes_.size() - 1));
		nibble_ = 0;
	} else {
		nibble_ = 1;
		changed();
	}
	return true;
}

bool HexView::typeLetter(char letter)
{
	if (!editing() || bytes_.isEmpty())
		return false;
	const auto at = cursor_;
	anchor_ = cursor_;
	content_->overwrite(at, QByteArray(1, letter), true);
	moveTo(std::min<qint64>(at + 1, bytes_.size() - 1));
	return true;
}

void HexView::setBytes(const QByteArray& bytes)
{
	if (content_) {
		disconnect(content_, nullptr, this, nullptr);
		content_ = nullptr;
	}
	bytes_ = bytes;
	clearFind();
	cursor_ = anchor_ = topRow_ = 0;
	relayout();
	Q_EMIT contentChanged();
	changed();
}

void HexView::reload()
{
	const auto bytes = content_ ? content_->bytes() : QByteArray{};
	if (bytes.constData() == bytes_.constData() && bytes.size() == bytes_.size())
		return;
	bytes_ = bytes;
	clearFind();
	const auto last = std::max<qint64>(0, bytes_.size() - 1);
	cursor_ = std::min(cursor_, last);
	anchor_ = std::min(anchor_, last);
	relayout();
	setTopRow(topRow_);
	Q_EMIT contentChanged();
	changed();
}

void HexView::setFont(const QFont& font)
{
	if (font == font_)
		return;
	font_ = font;
	relayout();
}

void HexView::setRowHeight(qreal height)
{
	if (height <= 0 || qFuzzyCompare(height, rowHeight_))
		return;
	rowHeight_ = height;
	relayout();
}

void HexView::setDecimalOffsets(bool decimal)
{
	if (decimal == decimalOffsets_)
		return;
	decimalOffsets_ = decimal;
	relayout();
	changed();
}

int HexView::offsetDigits() const
{
	const auto last = std::max<qint64>(0, bytes_.size() - 1);
	int digits = 1;
	for (auto value = last; value >= (decimalOffsets_ ? 10 : 16); value /= (decimalOffsets_ ? 10 : 16))
		++digits;
	return std::max(decimalOffsets_ ? 6 : 4, digits);
}

int HexView::hexDigits() const
{
	int digits = 1;
	for (auto value = std::max<qint64>(0, bytes_.size() - 1); value >= 16; value /= 16)
		++digits;
	return std::max(4, digits);
}

QString HexView::offsetText(qint64 offset) const
{
	return decimalOffsets_ ? QStringLiteral("%1").arg(offset, offsetDigits(), 10, QLatin1Char('0'))
	                       : hex(offset, offsetDigits());
}

HexView::Columns HexView::columns(int bytesPerRow, bool withText) const
{
	const QFontMetricsF metrics(font_);
	Columns at;
	at.cell = metrics.horizontalAdvance(QLatin1Char('0'));
	at.offset = padding;
	// The gutter holds the offsets and its heading, whichever is wider.
	const auto gutter = std::max(offsetDigits(), static_cast<int>(tr("Offset").size()));
	at.hex = at.offset + (gutter + gutterGap) * at.cell;
	// Two digits and a space a byte, one more space after every 8.
	const int hexChars = bytesPerRow * 3 - 1 + (bytesPerRow / 8 - 1);
	at.text = at.hex + (hexChars + gutterGap) * at.cell;
	at.end = (withText ? at.text + bytesPerRow * at.cell : at.hex + hexChars * at.cell) + padding;
	return at;
}

qreal HexView::hexX(const Columns& at, int column) const
{
	return at.hex + (column * 3 + column / 8) * at.cell;
}

qreal HexView::contentWidth() const
{
	return columns(bytesPerRow_, textColumn_).end;
}

void HexView::relayout()
{
	int perRow = 8;
	bool withText = width() >= columns(8, true).end || width() <= 0;
	for (const int candidate : {32, 16, 8})
		if (columns(candidate, true).end <= width()) {
			perRow = candidate;
			withText = true;
			break;
		}
	if (perRow != bytesPerRow_) {
		// Keep the cursor's row in view across the change.
		const auto cursorRow = cursor_ / perRow;
		bytesPerRow_ = perRow;
		topRow_ = std::max<qint64>(0, cursorRow - visibleRows() / 2);
	}
	textColumn_ = withText;
	setImplicitWidth(columns(bytesPerRow_, textColumn_).end);
	setTopRow(topRow_);
	Q_EMIT layoutChanged();
	update();
}

void HexView::geometryChange(const QRectF& next, const QRectF& previous)
{
	QQuickPaintedItem::geometryChange(next, previous);
	if (next.size() != previous.size()) {
		// A bar opening above, or the dock resized: the cursor stays in view.
		const bool cursorShown = cursor_ / bytesPerRow_ >= topRow_ &&
		                         cursor_ / bytesPerRow_ < topRow_ + std::max(1, static_cast<int>((previous.height() - headerHeight()) / rowHeight_));
		relayout();
		if (cursorShown)
			ensureVisible(cursor_);
	}
}

qint64 HexView::rowCount() const noexcept
{
	return (bytes_.size() + bytesPerRow_ - 1) / bytesPerRow_;
}

int HexView::visibleRows() const noexcept
{
	return std::max(1, static_cast<int>((height() - headerHeight()) / rowHeight_));
}

void HexView::setTopRow(qint64 row)
{
	const auto last = std::max<qint64>(0, rowCount() - visibleRows());
	row = std::clamp<qint64>(row, 0, last);
	if (row == topRow_)
		return;
	topRow_ = row;
	Q_EMIT scrolled();
	update();
}

qint64 HexView::selectionStart() const noexcept
{
	return std::min(cursor_, anchor_);
}

qint64 HexView::selectionLength() const noexcept
{
	return cursor_ == anchor_ ? 0 : std::abs(cursor_ - anchor_) + 1;
}

void HexView::changed()
{
	Q_EMIT cursorChanged();
	update();
}

void HexView::setNotice(const QString& notice)
{
	if (notice == notice_)
		return;
	notice_ = notice;
	Q_EMIT noticeChanged();
}

void HexView::ensureVisible(qint64 offset, bool centre)
{
	const auto row = offset / bytesPerRow_;
	if (centre) {
		const auto third = std::max(1, visibleRows() / 3);
		if (row < topRow_ + third || row >= topRow_ + visibleRows() - third)
			setTopRow(row - visibleRows() / 2);
		return;
	}
	if (row < topRow_)
		setTopRow(row);
	else if (row >= topRow_ + visibleRows())
		setTopRow(row - visibleRows() + 1);
}

void HexView::hoverMoveEvent(QHoverEvent* event)
{
	const auto over = event->position().y() < headerHeight() &&
	                  event->position().x() < columns(bytesPerRow_, textColumn_).hex;
	if (over != headingHovered_) {
		headingHovered_ = over;
		setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
		Q_EMIT hoverChanged();
		update();
	}
}

void HexView::hoverLeaveEvent(QHoverEvent* event)
{
	Q_UNUSED(event);
	if (headingHovered_) {
		headingHovered_ = false;
		unsetCursor();
		Q_EMIT hoverChanged();
		update();
	}
}

void HexView::moveTo(qint64 offset, bool extend)
{
	if (bytes_.isEmpty())
		return;
	offset = std::clamp<qint64>(offset, 0, bytes_.size() - 1);
	cursor_ = offset;
	nibble_ = 0;
	if (!extend)
		anchor_ = offset;
	ensureVisible(offset);
	setNotice({});
	changed();
}

void HexView::selectRange(qint64 start, qint64 length)
{
	if (bytes_.isEmpty() || length <= 0)
		return;
	start = std::clamp<qint64>(start, 0, bytes_.size() - 1);
	const auto end = std::clamp<qint64>(start + length - 1, start, bytes_.size() - 1);
	anchor_ = start;
	cursor_ = end;
	ensureVisible(start, true);
	setNotice({});
	changed();
}

void HexView::selectAll()
{
	selectRange(0, bytes_.size());
}

void HexView::collapseSelection()
{
	anchor_ = cursor_;
	changed();
}

void HexView::setBigEndian(bool big)
{
	if (big == bigEndian_)
		return;
	bigEndian_ = big;
	changed();
}

qint64 HexView::evaluate(const QString& expression, QString* error) const
{
	const auto fail = [&](const QString& why) {
		if (error)
			*error = why;
		return qint64{-1};
	};
	auto text = expression.trimmed().toLower();
	text.remove(QLatin1Char(' '));
	if (text.isEmpty())
		return fail(tr("Enter an offset, such as 0x1F4, 500 or end-16."));
	qint64 base = 0, sign = 1;
	bool relative = false;
	if (text.startsWith(QStringLiteral("end"))) {
		base = bytes_.size();
		text = text.mid(3);
		relative = true;
		if (text.isEmpty())
			return std::max<qint64>(0, bytes_.size() - 1);
	} else if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-'))) {
		base = cursor_;
		relative = true;
	}
	if (relative) {
		if (text.startsWith(QLatin1Char('-')))
			sign = -1;
		else if (!text.startsWith(QLatin1Char('+')))
			return fail(tr("Write end-16 or end+0x10 after end."));
		text = text.mid(1);
	}
	bool ok = false;
	qint64 value = 0;
	if (text.startsWith(QStringLiteral("0x")))
		value = text.mid(2).toLongLong(&ok, 16);
	else if (text.endsWith(QLatin1Char('h')))
		value = text.chopped(1).toLongLong(&ok, 16);
	else if (text.contains(QRegularExpression(QStringLiteral("^[0-9]+$"))))
		value = text.toLongLong(&ok, 10);
	else if (text.contains(QRegularExpression(QStringLiteral("^[0-9a-f]+$"))))
		value = text.toLongLong(&ok, 16);
	if (!ok || value < 0)
		return fail(tr("Not an offset: %1").arg(expression.trimmed()));
	return base + sign * value;
}

QString HexView::goTo(const QString& expression)
{
	QString error;
	const auto offset = evaluate(expression, &error);
	if (!error.isEmpty())
		return error;
	if (bytes_.isEmpty())
		return tr("This file is empty.");
	if (offset < 0 || offset >= bytes_.size())
		return tr("Not in this file: 0 to 0x%1.").arg(hex(bytes_.size() - 1, 1));
	moveTo(offset);
	// A jump lands in the middle of the view.
	ensureVisible(offset, true);
	return {};
}

QString HexView::selectBlock(const QString& start, const QString& lengthOrEnd, bool isEnd)
{
	QString error;
	const auto from = evaluate(start, &error);
	if (!error.isEmpty())
		return error;
	const auto other = evaluate(lengthOrEnd, &error);
	if (!error.isEmpty())
		return error;
	if (bytes_.isEmpty())
		return tr("This file is empty.");
	const auto last = bytes_.size() - 1;
	const auto end = isEnd ? other : from + other - 1;
	if (from < 0 || from > last || end < from || end > last)
		return tr("Not in this file: 0 to 0x%1.").arg(hex(last, 1));
	selectRange(from, end - from + 1);
	return {};
}

QByteArray HexView::pattern(const QString& query, const QString& kind, int width, QString* error) const
{
	const auto fail = [&](const QString& why) {
		if (error)
			*error = why;
		return QByteArray{};
	};
	if (query.isEmpty())
		return fail(tr("Enter what to find."));
	if (kind == QStringLiteral("text"))
		return query.toUtf8();
	if (kind == QStringLiteral("hex")) {
		auto digits = query;
		digits.remove(QRegularExpression(QStringLiteral("0[xX]")));
		digits.remove(QRegularExpression(QStringLiteral("[\\s,;:-]")));
		if (digits.isEmpty() || digits.size() % 2 != 0 ||
		    !digits.contains(QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$"))))
			return fail(tr("Hex is pairs of digits 0-9, A-F, such as DE AD BE EF."));
		return QByteArray::fromHex(digits.toLatin1());
	}
	const auto ordered = [&](quint64 value, int bytes) {
		QByteArray out(bytes, '\0');
		for (int i = 0; i < bytes; ++i) {
			const auto byte = static_cast<char>((value >> (8 * i)) & 0xffu);
			out[bigEndian_ ? bytes - 1 - i : i] = byte;
		}
		return out;
	};
	if (kind == QStringLiteral("integer")) {
		if (width != 1 && width != 2 && width != 4 && width != 8)
			return fail(tr("An integer is 1, 2, 4 or 8 bytes."));
		bool ok = false;
		const auto text = query.trimmed();
		qint64 value = text.toLongLong(&ok, 0);
		quint64 bits = static_cast<quint64>(value);
		if (!ok) {
			bits = text.toULongLong(&ok, 0);
			if (!ok)
				return fail(tr("Not an integer: %1").arg(text));
		} else if (width < 8) {
			const auto limit = qint64{1} << (8 * width);
			if (value >= limit || value < -(limit / 2))
				return fail(tr("%1 does not fit in %n bytes.", nullptr, width).arg(text));
		}
		return ordered(bits, width);
	}
	if (kind == QStringLiteral("float")) {
		bool ok = false;
		const auto value = query.trimmed().toDouble(&ok);
		if (!ok)
			return fail(tr("Not a number: %1").arg(query.trimmed()));
		if (width == 4) {
			const auto single = static_cast<float>(value);
			quint32 bits = 0;
			std::memcpy(&bits, &single, sizeof bits);
			return ordered(bits, 4);
		}
		if (width == 8) {
			quint64 bits = 0;
			std::memcpy(&bits, &value, sizeof bits);
			return ordered(bits, 8);
		}
		return fail(tr("A float is 4 or 8 bytes."));
	}
	return fail(tr("Unknown kind of search: %1").arg(kind));
}

QString HexView::find(const QString& query, const QString& kind, int width, bool forward)
{
	QString error;
	const auto bytes = pattern(query, kind, width, &error);
	if (!error.isEmpty())
		return error;
	if (bytes_.isEmpty())
		return tr("This file is empty.");
	if (bytes != pattern_) {
		pattern_ = bytes;
		countMatches();
	}
	// Next from just after the cursor, previous from just before it; round once.
	const QByteArrayView haystack(bytes_);
	qint64 found = -1;
	QString wrapped;
	if (forward) {
		const auto from = selectionLength() > 0 || cursor_ > 0 ? cursor_ + 1 : 0;
		found = haystack.indexOf(pattern_, std::min<qint64>(from, bytes_.size()));
		if (found < 0 && from > 0) {
			found = haystack.indexOf(pattern_, 0);
			wrapped = tr("Wrapped to the start");
		}
	} else {
		const auto from = std::max<qint64>(-1, selectionStart() - 1);
		found = from >= 0 ? haystack.lastIndexOf(pattern_, from) : -1;
		if (found < 0) {
			found = haystack.lastIndexOf(pattern_);
			wrapped = tr("Wrapped to the end");
		}
	}
	if (found < 0) {
		update();
		return tr("Not found");
	}
	// The match is the selection, the cursor on its first byte.
	anchor_ = found + pattern_.size() - 1;
	cursor_ = found;
	ensureVisible(found, true);
	setNotice({});
	Q_EMIT matchesChanged();
	changed();
	return wrapped;
}

void HexView::clearFind()
{
	++*findGeneration_;
	const bool had = !pattern_.isEmpty() || !matches_.empty() || counting_;
	pattern_.clear();
	matches_.clear();
	counting_ = capped_ = false;
	if (had) {
		Q_EMIT matchesChanged();
		update();
	}
}

void HexView::countMatches()
{
	const auto generation = ++*findGeneration_;
	matches_.clear();
	capped_ = false;
	// Every place, capped: the matches list feeds the highlights and the count.
	// A count overtaken by a newer pattern stops at its next 4 MiB.
	const auto count = [](const QByteArray& haystack, const QByteArray& needle, bool* capped,
	                      const std::atomic<quint64>* current, quint64 mine) {
		std::vector<qint64> found;
		const QByteArrayView view(haystack);
		constexpr qint64 checkEvery = 4 * 1024 * 1024;
		qint64 nextCheck = checkEvery;
		for (auto at = view.indexOf(needle, 0); at >= 0; at = view.indexOf(needle, at + 1)) {
			if (found.size() == static_cast<std::size_t>(maxMatches)) {
				*capped = true;
				break;
			}
			found.push_back(at);
			if (at >= nextCheck) {
				if (current->load(std::memory_order_relaxed) != mine)
					return std::vector<qint64>{};
				nextCheck = at + checkEvery;
			}
		}
		return found;
	};
	if (bytes_.size() <= countInlineBytes) {
		counting_ = false;
		matches_ = count(bytes_, pattern_, &capped_, findGeneration_.get(), generation);
		Q_EMIT matchesChanged();
		update();
		return;
	}
	// A large file is counted on a worker, over shared copies: later edits or
	// new bytes detach from them, and a newer pattern drops this count.
	counting_ = true;
	Q_EMIT matchesChanged();
	QPointer<HexView> self(this);
	const auto haystack = bytes_;
	const auto needle = pattern_;
	const auto current = findGeneration_;
	QThreadPool::globalInstance()->start([self, haystack, needle, generation, count, current] {
		bool capped = false;
		auto found = count(haystack, needle, &capped, current.get(), generation);
		if (current->load(std::memory_order_relaxed) != generation)
			return;
		QMetaObject::invokeMethod(
		    qApp, [self, generation, found = std::move(found), capped]() mutable {
			    if (!self || self->findGeneration_->load() != generation)
				    return;
			    self->matches_ = std::move(found);
			    self->capped_ = capped;
			    self->counting_ = false;
			    Q_EMIT self->matchesChanged();
			    self->update();
		    },
		    Qt::QueuedConnection);
	});
}

int HexView::matchIndex() const noexcept
{
	if (pattern_.isEmpty() || matches_.empty())
		return 0;
	const auto at = std::lower_bound(matches_.begin(), matches_.end(), selectionStart());
	return at != matches_.end() && *at == selectionStart() ? static_cast<int>(at - matches_.begin()) + 1 : 0;
}

QString HexView::textAs(const QString& format) const
{
	if (bytes_.isEmpty())
		return {};
	const auto start = selectionLength() > 0 ? selectionStart() : cursor_;
	const auto length = std::max<qint64>(1, selectionLength());
	const auto bytes = bytes_.mid(start, length);
	if (format == QStringLiteral("hex"))
		return QString::fromLatin1(bytes.toHex(' ').toUpper());
	if (format == QStringLiteral("hexCompact"))
		return QString::fromLatin1(bytes.toHex().toUpper());
	if (format == QStringLiteral("text"))
		return QString::fromUtf8(bytes);
	if (format == QStringLiteral("base64"))
		return QString::fromLatin1(bytes.toBase64());
	if (format == QStringLiteral("offset"))
		return QStringLiteral("0x") + hex(start, hexDigits());
	if (format == QStringLiteral("offsetLength"))
		return QStringLiteral("0x%1, %2").arg(hex(start, hexDigits())).arg(length);
	if (format == QStringLiteral("c")) {
		QString text = QStringLiteral("const uint8_t data[%1] = {").arg(length);
		for (qint64 i = 0; i < length; ++i) {
			if (i % 12 == 0)
				text += QStringLiteral("\n    ");
			text += QStringLiteral("0x%1").arg(hex(static_cast<unsigned char>(bytes[i]), 2));
			if (i + 1 < length)
				text += i % 12 == 11 ? QStringLiteral(",") : QStringLiteral(", ");
		}
		return text + QStringLiteral("\n};");
	}
	return {};
}

bool HexView::copy(const QString& format)
{
	if (std::max<qint64>(1, selectionLength()) > maxCopyBytes) {
		setNotice(tr("Copy is limited to 1 MiB; use Save as… for more."));
		return false;
	}
	const auto text = textAs(format);
	if (text.isEmpty())
		return false;
	if (auto* clipboard = QGuiApplication::clipboard())
		clipboard->setText(text);
	setNotice({});
	return true;
}

const HexView::Region* HexView::regionAt(qint64 offset) const
{
	const Region* found = nullptr;
	for (const auto& region : regions_)
		if (offset >= region.offset && offset < region.offset + region.length &&
		    (!found || region.depth >= found->depth))
			found = &region;
	return found;
}

void HexView::setRegions(const QVariantList& regions)
{
	regionList_ = regions;
	regions_.clear();
	for (const auto& entry : regions) {
		const auto map = entry.toMap();
		Region region;
		region.offset = map.value(QStringLiteral("offset")).toLongLong();
		region.length = map.value(QStringLiteral("length")).toLongLong();
		region.name = map.value(QStringLiteral("name")).toString();
		region.path = map.value(QStringLiteral("path")).toString();
		region.kind = map.value(QStringLiteral("kind")).toString();
		region.depth = map.value(QStringLiteral("depth")).toInt();
		if (region.length > 0)
			regions_.push_back(region);
	}
	std::stable_sort(regions_.begin(), regions_.end(), [](const Region& a, const Region& b) {
		return a.offset < b.offset;
	});
	Q_EMIT regionsChanged();
	changed();
}

QString HexView::status() const
{
	if (bytes_.isEmpty())
		return tr("This file is empty.");
	QString text = tr("0x%1 (%2)").arg(hex(cursor_, hexDigits())).arg(cursor_);
	if (const auto length = selectionLength(); length > 0)
		text += tr(" · %n bytes, 0x%1–0x%2", nullptr, static_cast<int>(std::min<qint64>(length, INT_MAX)))
		            .arg(hex(selectionStart(), hexDigits()))
		            .arg(hex(selectionStart() + length - 1, hexDigits()));
	if (const auto* region = regionAt(cursor_))
		text += tr(" · In: %1 · %n bytes at 0x%2", nullptr, static_cast<int>(region->length))
		            .arg(region->path.isEmpty() ? region->name : region->path)
		            .arg(hex(region->offset, hexDigits()));
	if (content_ && content_->isEdited(cursor_))
		text += tr(" · Edited, not saved");
	return text;
}

QString HexView::accessibleText() const
{
	if (bytes_.isEmpty())
		return tr("Hex view, empty file");
	const auto byte = static_cast<unsigned char>(std::as_const(bytes_)[cursor_]);
	auto text = printable(static_cast<char>(byte))
	                ? tr("Offset %1 hex, %2. Byte %3 hex, %4, character %5.")
	                      .arg(hex(cursor_, 1))
	                      .arg(cursor_)
	                      .arg(hex(byte, 2))
	                      .arg(byte)
	                      .arg(QChar(byte))
	                : tr("Offset %1 hex, %2. Byte %3 hex, %4.").arg(hex(cursor_, 1)).arg(cursor_).arg(hex(byte, 2)).arg(byte);
	if (const auto length = selectionLength(); length > 0)
		text += QLatin1Char(' ') + tr("%n bytes selected, %1 to %2.", nullptr, static_cast<int>(std::min<qint64>(length, INT_MAX)))
		            .arg(hex(selectionStart(), 1))
		            .arg(hex(selectionStart() + length - 1, 1));
	if (const auto* region = regionAt(cursor_))
		text += QLatin1Char(' ') + tr("In %1.").arg(region->path.isEmpty() ? region->name : region->path);
	return text;
}

QVariantList HexView::inspector() const
{
	const auto remaining = bytes_.size() - cursor_;
	const auto past = tr("past the end of the file");
	const auto row = [](const QString& type, const QString& value, const QString& description = {}) {
		return QVariantMap{{QStringLiteral("type"), type},
		                   {QStringLiteral("value"), value},
		                   {QStringLiteral("description"), description}};
	};
	// The bytes at the cursor as an unsigned integer of `width` bytes, in the
	// chosen order; exact for 64 bits, which JavaScript's numbers are not.
	const auto unsignedAt = [&](int width) {
		quint64 value = 0;
		for (int i = 0; i < width; ++i) {
			const auto byte = static_cast<quint64>(static_cast<unsigned char>(std::as_const(bytes_)[cursor_ + i]));
			value |= bigEndian_ ? byte << (8 * (width - 1 - i)) : byte << (8 * i);
		}
		return value;
	};
	QVariantList rows;
	for (const int width : {1, 2, 4, 8}) {
		const auto bits = QString::number(width * 8);
		if (remaining < width) {
			rows << row(QStringLiteral("int") + bits, QStringLiteral("—"), past)
			     << row(QStringLiteral("uint") + bits, QStringLiteral("—"), past);
			continue;
		}
		const auto value = unsignedAt(width);
		const auto shift = 64 - width * 8;
		const auto signedValue = shift == 0 ? static_cast<qint64>(value)
		                                    : static_cast<qint64>(value << shift) >> shift;
		rows << row(QStringLiteral("int") + bits, QString::number(signedValue))
		     << row(QStringLiteral("uint") + bits, QString::number(value));
	}
	if (remaining >= 4) {
		const auto bits = static_cast<quint32>(unsignedAt(4));
		float value = 0;
		std::memcpy(&value, &bits, sizeof value);
		rows << row(QStringLiteral("float32"), QString::number(static_cast<double>(value), 'g', 9));
	} else
		rows << row(QStringLiteral("float32"), QStringLiteral("—"), past);
	if (remaining >= 8) {
		const auto bits = unsignedAt(8);
		double value = 0;
		std::memcpy(&value, &bits, sizeof value);
		rows << row(QStringLiteral("float64"), QString::number(value, 'g', 17));
	} else
		rows << row(QStringLiteral("float64"), QStringLiteral("—"), past);
	if (remaining >= 1) {
		const auto byte = static_cast<unsigned char>(std::as_const(bytes_)[cursor_]);
		const auto bits = QStringLiteral("%1").arg(byte, 8, 2, QLatin1Char('0'));
		rows << row(QStringLiteral("bits"), bits.left(4) + QLatin1Char(' ') + bits.mid(4));
		// One UTF-8 character starting here, when it is one.
		auto decoder = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
		const auto lead = byte < 0x80 ? 1 : byte >= 0xf0 ? 4 : byte >= 0xe0 ? 3 : byte >= 0xc0 ? 2 : 0;
		QString character;
		if (lead > 0 && remaining >= lead) {
			character = decoder(bytes_.mid(cursor_, lead));
			if (decoder.hasError() || character.isEmpty() || character.size() > 2 ||
			    (character.size() == 1 && character.at(0).unicode() < 0x20))
				character.clear();
		}
		rows << row(QStringLiteral("UTF-8"),
		            character.isEmpty() ? QStringLiteral("—")
		                                : QStringLiteral("%1  (U+%2)").arg(character).arg(hex(character.toUcs4().value(0), 4)));
	}
	if (remaining >= 4) {
		const auto seconds = static_cast<qint64>(static_cast<quint32>(unsignedAt(4)));
		rows << row(QStringLiteral("time_t32"),
		            QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc()).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
		                QStringLiteral(" UTC"));
	} else
		rows << row(QStringLiteral("time_t32"), QStringLiteral("—"), past);
	return rows;
}

qint64 HexView::byteAt(QPointF point, bool* inText) const
{
	if (bytes_.isEmpty() || point.y() < headerHeight())
		return -1;
	const auto at = columns(bytesPerRow_, textColumn_);
	const auto row = topRow_ + static_cast<qint64>((point.y() - headerHeight()) / rowHeight_);
	int column = -1;
	bool text = false;
	if (textColumn_ && point.x() >= at.text - at.cell / 2) {
		column = static_cast<int>((point.x() - at.text) / at.cell);
		text = true;
	} else if (point.x() >= at.hex - at.cell) {
		// A byte's cell runs from its first digit to the space after it.
		for (int i = bytesPerRow_ - 1; i >= 0; --i)
			if (point.x() >= hexX(at, i) - at.cell / 2) {
				column = i;
				break;
			}
	}
	if (column < 0)
		return -1;
	column = std::clamp(column, 0, bytesPerRow_ - 1);
	if (inText)
		*inText = text;
	return std::min<qint64>(row * bytesPerRow_ + column, bytes_.size() - 1);
}

void HexView::mousePressEvent(QMouseEvent* event)
{
	forceActiveFocus(Qt::MouseFocusReason);
	// The Offset heading switches between hex and decimal offsets.
	if (event->position().y() < headerHeight() && event->position().x() < columns(bytesPerRow_, textColumn_).hex) {
		setDecimalOffsets(!decimalOffsets_);
		event->accept();
		return;
	}
	bool text = false;
	const auto offset = byteAt(event->position(), &text);
	if (event->button() == Qt::RightButton) {
		// As in a file explorer: a click outside the selection moves to that byte first.
		const bool inside = selectionLength() > 0 && offset >= selectionStart() &&
		                    offset < selectionStart() + selectionLength();
		if (offset >= 0 && !inside) {
			textPane_ = text;
			moveTo(offset);
		}
		Q_EMIT contextMenuRequested(event->position().x(), event->position().y());
		event->accept();
		return;
	}
	if (offset < 0) {
		event->accept();
		return;
	}
	textPane_ = text;
	moveTo(offset, event->modifiers() & Qt::ShiftModifier);
	event->accept();
}

void HexView::mouseMoveEvent(QMouseEvent* event)
{
	if (!(event->buttons() & Qt::LeftButton)) {
		event->ignore();
		return;
	}
	bool text = false;
	auto point = event->position();
	// Dragging past the edges scrolls by a row.
	if (point.y() < headerHeight())
		setTopRow(topRow_ - 1);
	else if (point.y() > height())
		setTopRow(topRow_ + 1);
	point.setY(std::clamp(point.y(), headerHeight(), height() - 1));
	const auto offset = byteAt(point, &text);
	if (offset >= 0)
		moveTo(offset, true);
	event->accept();
}

void HexView::wheelEvent(QWheelEvent* event)
{
	const auto steps = event->angleDelta().y() / 40;
	setTopRow(topRow_ - (steps == 0 ? (event->angleDelta().y() > 0 ? 1 : event->angleDelta().y() < 0 ? -1 : 0) : steps));
	event->accept();
}

void HexView::keyPressEvent(QKeyEvent* event)
{
	if (bytes_.isEmpty()) {
		QQuickPaintedItem::keyPressEvent(event);
		return;
	}
	const bool extend = event->modifiers() & Qt::ShiftModifier;
	const bool control = event->modifiers() & Qt::ControlModifier;
	// Editing: a hex digit in the hex column, a printable letter in the text
	// column, written over the byte at the cursor; Ctrl+Z and Ctrl+Y undo and redo.
	if (editing()) {
		const auto typed = event->text();
		if (!control && !(event->modifiers() & Qt::AltModifier) && typed.size() == 1) {
			const auto c = typed.at(0).unicode();
			if (!textPane_) {
				const int value = c >= '0' && c <= '9' ? c - '0'
				                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
				                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
				                                         : -1;
				if (value >= 0 && typeDigit(value)) {
					event->accept();
					return;
				}
			} else if (c >= 0x20 && c < 0x7f && typeLetter(static_cast<char>(c))) {
				event->accept();
				return;
			}
		}
		if (control && (event->key() == Qt::Key_Z || event->key() == Qt::Key_Y)) {
			if (event->key() == Qt::Key_Y || extend)
				content_->redo();
			else
				content_->undo();
			event->accept();
			return;
		}
	}
	const qint64 page = static_cast<qint64>(visibleRows()) * bytesPerRow_;
	const auto rowStart = cursor_ - cursor_ % bytesPerRow_;
	switch (event->key()) {
		case Qt::Key_Left: moveTo(cursor_ - 1, extend); break;
		case Qt::Key_Right: moveTo(cursor_ + 1, extend); break;
		case Qt::Key_Up: moveTo(cursor_ - bytesPerRow_, extend); break;
		case Qt::Key_Down: moveTo(cursor_ + bytesPerRow_, extend); break;
		case Qt::Key_PageUp:
			setTopRow(topRow_ - visibleRows());
			moveTo(cursor_ - page, extend);
			break;
		case Qt::Key_PageDown:
			setTopRow(topRow_ + visibleRows());
			moveTo(cursor_ + page, extend);
			break;
		case Qt::Key_Home: moveTo(control ? 0 : rowStart, extend); break;
		case Qt::Key_End: moveTo(control ? bytes_.size() - 1 : rowStart + bytesPerRow_ - 1, extend); break;
		case Qt::Key_Escape:
			if (selectionLength() == 0) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			collapseSelection();
			break;
		case Qt::Key_A:
			if (!control) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			selectAll();
			break;
		case Qt::Key_C:
			if (!control) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			// Ctrl+C copies in the pane's own form; Ctrl+Shift+C is Copy as, the host's menu.
			if (event->modifiers() & Qt::ShiftModifier) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			copy(textPane_ ? QStringLiteral("text") : QStringLiteral("hex"));
			break;
		case Qt::Key_Menu: {
			const auto at = columns(bytesPerRow_, textColumn_);
			const auto row = cursor_ / bytesPerRow_ - topRow_;
			const auto column = static_cast<int>(cursor_ % bytesPerRow_);
			Q_EMIT contextMenuRequested(textPane_ ? at.text + column * at.cell : hexX(at, column),
			                            headerHeight() + (row + 1) * rowHeight_);
			break;
		}
		case Qt::Key_F10:
			if (!(event->modifiers() & Qt::ShiftModifier)) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			Q_EMIT contextMenuRequested(hexX(columns(bytesPerRow_, textColumn_), static_cast<int>(cursor_ % bytesPerRow_)),
			                            headerHeight() + (cursor_ / bytesPerRow_ - topRow_ + 1) * rowHeight_);
			break;
		case Qt::Key_Tab:
			// Hex, then text, then out: the editor is two tab stops, never a trap.
			if (textPane_ || !textColumn_) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			textPane_ = true;
			changed();
			break;
		case Qt::Key_Backtab:
			if (!textPane_) {
				QQuickPaintedItem::keyPressEvent(event);
				return;
			}
			textPane_ = false;
			changed();
			break;
		default:
			QQuickPaintedItem::keyPressEvent(event);
			return;
	}
	event->accept();
}

void HexView::paint(QPainter* painter)
{
	const auto at = columns(bytesPerRow_, textColumn_);
	const QFontMetricsF metrics(font_);
	painter->setFont(font_);
	const auto baseline = [&](qreal top) {
		return top + (rowHeight_ - metrics.height()) / 2 + metrics.ascent();
	};

	// The headings: Offset, each column's low digit, Text.
	painter->setPen(secondaryColor_);
	painter->drawText(QPointF(at.offset, baseline(0)), tr("Offset"));
	if (headingHovered_) {
		const auto width = metrics.horizontalAdvance(tr("Offset"));
		painter->drawLine(QPointF(at.offset, baseline(0) + 2), QPointF(at.offset + width, baseline(0) + 2));
	}
	for (int i = 0; i < bytesPerRow_; ++i)
		painter->drawText(QPointF(hexX(at, i) + at.cell / 2, baseline(0)), QString(QChar(QLatin1Char("0123456789ABCDEF"[i % 16]))));
	if (textColumn_)
		painter->drawText(QPointF(at.text, baseline(0)), tr("Text"));

	if (bytes_.isEmpty())
		return;
	const auto selStart = selectionStart();
	const auto selEnd = selectionLength() > 0 ? selStart + selectionLength() : selStart;
	const auto* cursorRegion = regionAt(cursor_);
	const auto first = topRow_ * bytesPerRow_;
	const auto last = std::min<qint64>(bytes_.size(), (topRow_ + visibleRows() + 1) * bytesPerRow_);

	// The codec's regions: siblings alternate between no tint and the tint, so a
	// part's edges show; its name says what it is, never its colour.
	int sibling = 0;
	for (const auto& region : regions_) {
		if (region.depth != 0)
			continue;
		const bool tinted = sibling++ % 2 == 1;
		if (!tinted || region.offset + region.length <= first || region.offset >= last)
			continue;
		for (auto row = std::max(region.offset, first) / bytesPerRow_;
		     row * bytesPerRow_ < std::min(region.offset + region.length, last); ++row) {
			const auto top = headerHeight() + (row - topRow_) * rowHeight_;
			const auto a = static_cast<int>(std::max(region.offset, row * bytesPerRow_) - row * bytesPerRow_);
			const auto b = static_cast<int>(std::min(region.offset + region.length, (row + 1) * bytesPerRow_) - row * bytesPerRow_);
			painter->fillRect(QRectF(hexX(at, a) - at.cell / 2, top, hexX(at, b - 1) + 2.5 * at.cell - hexX(at, a), rowHeight_),
			                  regionColor_);
			if (textColumn_)
				painter->fillRect(QRectF(at.text + a * at.cell, top, (b - a) * at.cell, rowHeight_), regionColor_);
		}
	}

	// Find's matches in view: a fill and a thin outline, the outline being the
	// cue that does not rely on colour.
	if (!matches_.empty() && !pattern_.isEmpty()) {
		const auto length = static_cast<qint64>(pattern_.size());
		for (auto it = std::lower_bound(matches_.begin(), matches_.end(), std::max<qint64>(0, first - length + 1));
		     it != matches_.end() && *it < last; ++it) {
			for (auto row = std::max(*it, first) / bytesPerRow_;
			     row * bytesPerRow_ < std::min(*it + length, last); ++row) {
				const auto top = headerHeight() + (row - topRow_) * rowHeight_;
				const auto a = static_cast<int>(std::max(*it, row * bytesPerRow_) - row * bytesPerRow_);
				const auto b = static_cast<int>(std::min(*it + length, (row + 1) * bytesPerRow_) - row * bytesPerRow_);
				const QRectF cells(hexX(at, a) - at.cell / 2, top + 1, hexX(at, b - 1) + 2.5 * at.cell - hexX(at, a), rowHeight_ - 2);
				painter->fillRect(cells, matchColor_);
				painter->setPen(QPen(matchOutlineColor_, 1));
				painter->setBrush(Qt::NoBrush);
				painter->drawRect(cells);
				if (textColumn_) {
					const QRectF letters(at.text + a * at.cell, top + 1, (b - a) * at.cell, rowHeight_ - 2);
					painter->fillRect(letters, matchColor_);
					painter->drawRect(letters);
				}
			}
		}
	}

	for (auto row = topRow_; row <= topRow_ + visibleRows() && row < rowCount(); ++row) {
		const auto top = headerHeight() + (row - topRow_) * rowHeight_;
		const auto start = row * bytesPerRow_;
		const auto count = static_cast<int>(std::min<qint64>(bytesPerRow_, bytes_.size() - start));

		// The selection, the same fill in both panes.
		const auto a = std::max(selStart, start), b = std::min(selEnd, start + count);
		if (selectionLength() > 0 && a < b) {
			const auto from = static_cast<int>(a - start), to = static_cast<int>(b - start);
			painter->fillRect(QRectF(hexX(at, from) - at.cell / 2, top, hexX(at, to - 1) + 2.5 * at.cell - hexX(at, from), rowHeight_),
			                  selectionColor_);
			if (textColumn_)
				painter->fillRect(QRectF(at.text + from * at.cell, top, (to - from) * at.cell, rowHeight_), selectionColor_);
		}

		painter->setPen(secondaryColor_);
		painter->drawText(QPointF(at.offset, baseline(top)), offsetText(start));

		// Each byte's two digits fall into one of three strings by how they are
		// drawn - plain, dimmed (zero), selected - kept aligned with spaces.
		const int hexChars = bytesPerRow_ * 3 + bytesPerRow_ / 8;
		QString plain(hexChars, QLatin1Char(' ')), dim = plain, chosen = plain, edited = plain;
		QString plainText(bytesPerRow_, QLatin1Char(' ')), dimText = plainText, chosenText = plainText,
		                                                   editedText = plainText;
		bool rowEdited = false;
		for (int i = 0; i < count; ++i) {
			const auto offset = start + i;
			const auto byte = static_cast<unsigned char>(std::as_const(bytes_)[offset]);
			const bool selected = selectionLength() > 0 && offset >= selStart && offset < selEnd;
			const bool changedByte = content_ && content_->isEdited(offset);
			if (changedByte) {
				rowEdited = true;
				painter->setPen(QPen(selected ? selectedTextColor_ : editedColor_, 1));
				const auto line = baseline(top) + 3;
				painter->drawLine(QPointF(hexX(at, i), line), QPointF(hexX(at, i) + 2 * at.cell, line));
				if (textColumn_)
					painter->drawLine(QPointF(at.text + i * at.cell, line), QPointF(at.text + (i + 1) * at.cell, line));
			}
			auto& target = selected ? chosen : changedByte ? edited : byte == 0 ? dim : plain;
			const auto pos = i * 3 + i / 8;
			const auto digits = hex(byte, 2);
			target[pos] = digits[0];
			target[pos + 1] = digits[1];
			const bool shown = printable(static_cast<char>(byte));
			auto& textTarget = selected ? chosenText : changedByte ? editedText : shown ? plainText : dimText;
			textTarget[i] = shown ? QChar(byte) : QChar(0x00b7);
		}
		const QPointF hexAt(at.hex, baseline(top));
		painter->setPen(textColor_);
		painter->drawText(hexAt, plain);
		painter->setPen(mutedColor_);
		painter->drawText(hexAt, dim);
		painter->setPen(selectedTextColor_);
		painter->drawText(hexAt, chosen);
		painter->setPen(editedColor_);
		painter->drawText(hexAt, edited);
		if (textColumn_) {
			const QPointF textAt(at.text, baseline(top));
			painter->setPen(textColor_);
			painter->drawText(textAt, plainText);
			painter->setPen(mutedColor_);
			painter->drawText(textAt, dimText);
			painter->setPen(selectedTextColor_);
			painter->drawText(textAt, chosenText);
			painter->setPen(editedColor_);
			painter->drawText(textAt, editedText);
		}
		// A row with edits not saved: a dot before its offset.
		if (rowEdited) {
			painter->setPen(Qt::NoPen);
			painter->setBrush(editedColor_);
			painter->drawEllipse(QPointF(std::max<qreal>(4, at.offset / 2), top + rowHeight_ / 2), 3, 3);
			painter->setBrush(Qt::NoBrush);
		}
	}

	// The region at the cursor: a thin outline around its whole extent.
	if (cursorRegion) {
		painter->setPen(QPen(outlineColor_, 1));
		painter->setBrush(Qt::NoBrush);
		for (auto row = std::max(cursorRegion->offset, first) / bytesPerRow_;
		     row * bytesPerRow_ < std::min(cursorRegion->offset + cursorRegion->length, last); ++row) {
			const auto top = headerHeight() + (row - topRow_) * rowHeight_;
			const auto a = static_cast<int>(std::max(cursorRegion->offset, row * bytesPerRow_) - row * bytesPerRow_);
			const auto b = static_cast<int>(
			    std::min(cursorRegion->offset + cursorRegion->length, (row + 1) * bytesPerRow_) - row * bytesPerRow_);
			painter->drawRect(QRectF(hexX(at, a) - at.cell / 2, top + 0.5, hexX(at, b - 1) + 2.5 * at.cell - hexX(at, a) - 1,
			                         rowHeight_ - 1));
		}
	}

	// The cursor, in both panes: the active one framed in the focus colour
	// (white on the selection, where the focus colour would not show), the
	// other one thinly.
	if (cursor_ >= first && cursor_ < last) {
		const auto row = cursor_ / bytesPerRow_;
		const auto column = static_cast<int>(cursor_ % bytesPerRow_);
		const auto top = headerHeight() + (row - topRow_) * rowHeight_;
		const bool onSelection = selectionLength() > 0 && cursor_ >= selStart && cursor_ < selEnd;
		const QRectF hexCell(hexX(at, column) - at.cell / 4, top + 2, 2.5 * at.cell, rowHeight_ - 4);
		const QRectF textCell(at.text, top + 2, at.cell, rowHeight_ - 4);
		const auto frame = [&](const QRectF& cell, bool active) {
			const bool focused = active && hasActiveFocus();
			painter->setPen(QPen(focused ? (onSelection ? selectedTextColor_ : focusColor_) : secondaryColor_, focused ? 2 : 1));
			painter->setBrush(Qt::NoBrush);
			painter->drawRect(cell);
		};
		frame(hexCell, !textPane_);
		if (textColumn_)
			frame(textCell.translated(column * at.cell, 0), textPane_);
		if (editing() && !textPane_ && hasActiveFocus()) {
			const auto x = hexX(at, column) + nibble_ * at.cell;
			painter->fillRect(QRectF(x, top + rowHeight_ - 5, at.cell, 2), onSelection ? selectedTextColor_ : focusColor_);
		}
	}
}
} // namespace telemetry::ux
