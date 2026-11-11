/*
 * @file HexView.hpp
 * @brief A file's bytes as a hex grid: painted rows, a byte cursor and a selection, the inspector.
 * @author Ruslan Kovtun (shpegun60)
 * SPDX-License-Identifier: MIT
 *
 * The Viewer's Hex mode. One painted item draws only the rows in view straight
 * from the bytes - a 64 MiB file is four million rows, which delegates cannot
 * carry - and keeps the cursor, the selection and what the status line and the
 * inspector say. The look follows the designer's H-1 (doc/DEVICE_FILES.md in
 * the client): an offset gutter, 8, 16 or 32 bytes a row by width with a gap
 * after every 8, a text column, zeros dimmed, the cursor framed in both panes.
 *
 * QML gives it the bytes (a FileContent), the fixed font, the row height and
 * the Theme's colours, and drives its scrolling through topRow. Keys and the
 * mouse are its own. Read-only: editing is the H-2 slice.
 */
#ifndef TELEMETRY_UX_HEX_VIEW_HPP
#define TELEMETRY_UX_HEX_VIEW_HPP
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QPointer>
#include <QtCore/QVariantList>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>
#include <atomic>
#include <memory>
#include <vector>

namespace telemetry::ux {
class FileContent;

// Not final: QML derives its own element from a creatable type.
class HexView : public QQuickPaintedItem {
	Q_OBJECT
	QML_ELEMENT
	// A FileContent whose bytes are shown; they are read again when it changes.
	Q_PROPERTY(QObject* content READ content WRITE setContent NOTIFY contentChanged)
	Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY layoutChanged)
	Q_PROPERTY(qreal rowHeight READ rowHeight WRITE setRowHeight NOTIFY layoutChanged)
	// 32, 16 or 8: the most that fit the width; the text column goes first
	// when even 8 do not fit beside it.
	Q_PROPERTY(int bytesPerRow READ bytesPerRow NOTIFY layoutChanged)
	Q_PROPERTY(bool textColumn READ textColumn NOTIFY layoutChanged)
	Q_PROPERTY(bool decimalOffsets READ decimalOffsets WRITE setDecimalOffsets NOTIFY layoutChanged)
	Q_PROPERTY(qreal headerHeight READ headerHeight NOTIFY layoutChanged)
	Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY layoutChanged)
	Q_PROPERTY(qint64 size READ size NOTIFY contentChanged)
	Q_PROPERTY(qint64 rowCount READ rowCount NOTIFY layoutChanged)
	Q_PROPERTY(int visibleRows READ visibleRows NOTIFY layoutChanged)
	// The first row in view; QML's scroll bar moves it.
	Q_PROPERTY(qint64 topRow READ topRow WRITE setTopRow NOTIFY scrolled)
	Q_PROPERTY(qint64 cursor READ cursor NOTIFY cursorChanged)
	Q_PROPERTY(qint64 selectionStart READ selectionStart NOTIFY cursorChanged)
	Q_PROPERTY(qint64 selectionLength READ selectionLength NOTIFY cursorChanged)
	// The cursor is in the text column rather than the hex one.
	Q_PROPERTY(bool textPane READ textPane NOTIFY cursorChanged)
	// "Offset 0x0001F4 (500) · Selected 16 bytes (0x10), 0x0001F4–0x000203 · In: header › magic"
	Q_PROPERTY(QString status READ status NOTIFY cursorChanged)
	// The pointer is over the Offset heading, which a click switches.
	Q_PROPERTY(bool offsetHeadingHovered READ offsetHeadingHovered NOTIFY hoverChanged)
	// What a screen reader says at the cursor.
	Q_PROPERTY(QString accessibleText READ accessibleText NOTIFY cursorChanged)
	// Something the last action could not do, in words; empty otherwise.
	Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
	// The inspector: the bytes at the cursor read as each type.
	Q_PROPERTY(bool bigEndian READ bigEndian WRITE setBigEndian NOTIFY cursorChanged)
	Q_PROPERTY(QVariantList inspector READ inspector NOTIFY cursorChanged)
	// Find: how many places hold the last pattern (counted up to maxMatches),
	// which one the cursor is on (1-based, 0 for none), whether the count is
	// still running, and the pattern's length.
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
	Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)
	Q_PROPERTY(bool counting READ counting NOTIFY matchesChanged)
	Q_PROPERTY(bool matchesCapped READ matchesCapped NOTIFY matchesChanged)
	// A codec's parts of the file: {offset, length, name, path, kind, depth}
	// each; depth 0 for a top-level part (the default).
	Q_PROPERTY(QVariantList regions READ regions WRITE setRegions NOTIFY regionsChanged)
	// The Theme's colours, given by QML.
	Q_PROPERTY(QColor textColor MEMBER textColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor secondaryColor MEMBER secondaryColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor mutedColor MEMBER mutedColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor selectionColor MEMBER selectionColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor selectedTextColor MEMBER selectedTextColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor focusColor MEMBER focusColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor regionColor MEMBER regionColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor outlineColor MEMBER outlineColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor matchColor MEMBER matchColor_ NOTIFY coloursChanged)
	Q_PROPERTY(QColor matchOutlineColor MEMBER matchOutlineColor_ NOTIFY coloursChanged)
public:
	// Copying more than this is refused: Save as… is the way for more.
	static constexpr qint64 maxCopyBytes = 1024 * 1024;
	// Matches counted and drawn at most; the count then reads "10 000+".
	static constexpr int maxMatches = 10000;
	// Larger files are counted on a worker; smaller ones at once.
	static constexpr qint64 countInlineBytes = 8 * 1024 * 1024;

	explicit HexView(QQuickItem* parent = nullptr);

	QObject* content() const;
	void setContent(QObject* content);
	// The bytes directly, for a host without a FileContent (an implicitly shared copy).
	void setBytes(const QByteArray& bytes);
	QFont font() const { return font_; }
	void setFont(const QFont& font);
	qreal rowHeight() const noexcept { return rowHeight_; }
	void setRowHeight(qreal height);
	int bytesPerRow() const noexcept { return bytesPerRow_; }
	bool textColumn() const noexcept { return textColumn_; }
	bool decimalOffsets() const noexcept { return decimalOffsets_; }
	void setDecimalOffsets(bool decimal);
	qreal headerHeight() const noexcept { return rowHeight_; }
	qreal contentWidth() const;
	qint64 size() const noexcept { return bytes_.size(); }
	qint64 rowCount() const noexcept;
	int visibleRows() const noexcept;
	qint64 topRow() const noexcept { return topRow_; }
	void setTopRow(qint64 row);
	qint64 cursor() const noexcept { return cursor_; }
	qint64 selectionStart() const noexcept;
	qint64 selectionLength() const noexcept;
	bool textPane() const noexcept { return textPane_; }
	bool offsetHeadingHovered() const noexcept { return headingHovered_; }
	QString status() const;
	QString accessibleText() const;
	QString notice() const { return notice_; }
	bool bigEndian() const noexcept { return bigEndian_; }
	void setBigEndian(bool big);
	QVariantList inspector() const;
	QVariantList regions() const { return regionList_; }
	void setRegions(const QVariantList& regions);

	// Moves the cursor (and, with extend, the selection's end) and keeps it in view.
	Q_INVOKABLE void moveTo(qint64 offset, bool extend = false);
	// Selects a range and brings it to the middle of the view: a jump, not a step.
	Q_INVOKABLE void selectRange(qint64 start, qint64 length);
	Q_INVOKABLE void selectAll();
	Q_INVOKABLE void collapseSelection();
	// Go to (Ctrl+G): "0x1F4", "1F4h", "500", "+16", "-0x10", "end", "end-16".
	// Empty when the cursor moved there; otherwise why not, in words.
	Q_INVOKABLE QString goTo(const QString& expression);
	// Select a block (Ctrl+E): a start and a length, or a start and an end
	// (inclusive), each in the forms goTo takes. Empty when selected.
	Q_INVOKABLE QString selectBlock(const QString& start, const QString& lengthOrEnd, bool isEnd);
	// The selection (or the byte at the cursor) as "hex" (DE AD BE EF),
	// "hexCompact" (DEADBEEF), "text", "c" (a C array), "base64", "offset"
	// (0x0001F4) or "offsetLength"; empty for an unknown format.
	Q_INVOKABLE QString textAs(const QString& format) const;
	// Puts textAs(format) on the clipboard; false, with a notice, above maxCopyBytes.
	Q_INVOKABLE bool copy(const QString& format);
	// Find (Ctrl+F): kind is "text" (UTF-8), "hex" (DE AD BE EF, deadbeef or
	// 0xDE,0xAD), "integer" or "float"; width the number's bytes (1, 2, 4, 8;
	// 4 or 8 for a float), in the inspector's byte order. Selects the next
	// match after the cursor (or the previous one) and counts them all. Empty
	// when found; "Wrapped to the start" or "Wrapped to the end" when it went
	// round; otherwise why not, in words.
	Q_INVOKABLE QString find(const QString& query, const QString& kind, int width = 4, bool forward = true);
	// Forgets the pattern and its highlights.
	Q_INVOKABLE void clearFind();
	int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
	int matchIndex() const noexcept;
	bool counting() const noexcept { return counting_; }
	bool matchesCapped() const noexcept { return capped_; }
	// The bytes a query stands for, or empty with why in error.
	QByteArray pattern(const QString& query, const QString& kind, int width, QString* error) const;
	// The value a goTo expression names, or -1 with why in error.
	qint64 evaluate(const QString& expression, QString* error) const;

Q_SIGNALS:
	void contentChanged();
	void layoutChanged();
	void scrolled();
	void cursorChanged();
	void noticeChanged();
	void regionsChanged();
	void matchesChanged();
	void hoverChanged();
	void coloursChanged();
	// A right click, or the Menu key or Shift+F10: where to open the context menu.
	void contextMenuRequested(qreal x, qreal y);

protected:
	void paint(QPainter* painter) override;
	void geometryChange(const QRectF& next, const QRectF& previous) override;
	void keyPressEvent(QKeyEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void hoverMoveEvent(QHoverEvent* event) override;
	void hoverLeaveEvent(QHoverEvent* event) override;

private:
	struct Region {
		qint64 offset = 0, length = 0;
		QString name, path, kind;
		int depth = 0;
	};
	struct Columns {
		qreal offset = 0, hex = 0, text = 0, end = 0, cell = 0;
	};

	void reload();
	void relayout();
	Columns columns(int bytesPerRow, bool withText) const;
	int offsetDigits() const;
	// Hex digits for an offset in this file, whatever the gutter shows: at least 4.
	int hexDigits() const;
	QString offsetText(qint64 offset) const;
	qreal hexX(const Columns& at, int column) const;
	// The byte under a point and whether it is in the text column; -1 when none.
	qint64 byteAt(QPointF point, bool* inText) const;
	// Keeps a byte in view: with centre, a row outside the middle third is
	// brought to the middle (a jump); otherwise the least scroll (a step).
	void ensureVisible(qint64 offset, bool centre = false);
	void setNotice(const QString& notice);
	const Region* regionAt(qint64 offset) const;
	void changed();
	void countMatches();

	QPointer<FileContent> content_;
	QByteArray pattern_;
	std::vector<qint64> matches_;
	bool counting_ = false, capped_ = false;
	// Bumped by every new pattern or new bytes: a count from before stops at
	// its next check and its result is dropped. Shared with the worker.
	std::shared_ptr<std::atomic<quint64>> findGeneration_ = std::make_shared<std::atomic<quint64>>(0);
	QByteArray bytes_;
	QFont font_;
	qreal rowHeight_ = 32;
	int bytesPerRow_ = 16;
	bool textColumn_ = true, decimalOffsets_ = false, textPane_ = false, bigEndian_ = false;
	bool headingHovered_ = false;
	qint64 topRow_ = 0, cursor_ = 0, anchor_ = 0;
	QString notice_;
	QVariantList regionList_;
	std::vector<Region> regions_;
	QColor textColor_{0xe8, 0xed, 0xf5}, secondaryColor_{0x9f, 0xb0, 0xc3}, mutedColor_{0x8c, 0x9f, 0xb3};
	QColor selectionColor_{0x2f, 0x7d, 0x91}, selectedTextColor_{Qt::white}, focusColor_{0x6f, 0xd4, 0xbf};
	QColor regionColor_{0x1c, 0x2a, 0x3a}, outlineColor_{0x64, 0x7f, 0x9e};
	QColor matchColor_{0x1d, 0x3a, 0x52}, matchOutlineColor_{0xa8, 0xd4, 0xf5};
};
} // namespace telemetry::ux
#endif
