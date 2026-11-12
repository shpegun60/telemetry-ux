// The device's files as a tree of their own paths, with downloads and their
// progress. Reusable presentation: the host owns the models, the dialogs and
// every write to disk. MIT.
//
// The host gives `files` (DeviceFilesModel) and `transfers`
// (DeviceFileTransfers). Download asks the host for a destination through
// downloadRequested(selection) - a save dialog for one file, a folder for
// several - and the host calls transfers.download(...). Open asks the host to
// show a file through openRequested(file) - a double-click, Enter, or the
// menu's first item - and Open with through openWithRequested(file, view),
// view being "table", "text", "hex" or "external"; Show in folder through
// showInFolderRequested(localPath).
//
// As in a file explorer, the actions are in a context menu (right click, the
// Menu key, Shift+F10) rather than buttons. Keys on the tree: Enter opens,
// Space marks, Ctrl+S downloads, Alt+Shift+C copies the path, Alt+Enter shows
// the properties, Ctrl+A marks all, Ctrl+Shift+A clears the marks, Ctrl+R
// refreshes. The host calls registerQmlTypes() first (Clipboard).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Telemetry.UX

Pane {
    id: panel
    objectName: "deviceFilesPane"
    padding: 0

    required property var files
    required property var transfers
    property bool reducedMotion: false
    // Transfers under the list; a host that gives them a dock of their own
    // (DeviceTransfersPanel) sets this false.
    property bool transfersShown: true

    signal downloadRequested(var selection)
    signal openRequested(var file)
    signal openWithRequested(var file, string view)
    signal showInFolderRequested(string localPath)

    // DeviceFileTransfers.State, in its order.
    readonly property int stateWaiting: 0
    readonly property int stateReading: 1
    readonly property int stateSaved: 2
    readonly property int stateCancelled: 3
    readonly property int stateFailed: 4
    readonly property int stateWriting: 5
    readonly property int stateWritten: 6
    // DeviceFileTransfers.Outcome, as numbers.
    readonly property int outcomeNotChecked: 3
    readonly property int outcomeChanged: 4
    readonly property int outcomeNotKnown: 5
    readonly property int outcomePartial: 8
    readonly property int outcomeFailed: 9

    readonly property var currentIndex: fileTree.selectionModel.currentIndex
    readonly property bool currentIsFile: files.isFile(currentIndex)
    readonly property string currentName: currentIsFile ? files.data(currentIndex, Qt.DisplayRole) : ""
    readonly property int finishedTransfers: transfers.rowCount() - transfers.activeCount

    // One width per column, shared by the heading and every row.
    readonly property real sizeWidth: Math.ceil(sizeMetrics.advanceWidth) + 4
    readonly property real accessWidth: Math.ceil(Math.max(accessMetrics.advanceWidth,
                                                           countMetrics.advanceWidth)) + 4
    readonly property bool accessShown: fileTree.width >= 360
    TextMetrics { id: sizeMetrics; font.pointSize: Type.caption; text: "888.8 KiB" }
    TextMetrics { id: accessMetrics; font.pointSize: Type.caption; text: qsTr("read, write") }
    TextMetrics { id: countMetrics; font.pointSize: Type.caption; text: qsTr("%n files", "", 4096) }

    function chosen() {
        return files.selection(currentIndex)
    }
    function download() {
        const selection = chosen()
        if (selection.length > 0)
            panel.downloadRequested(selection)
    }
    function openIndex(index) {
        const file = files.fileAt(index)
        if (file.path !== undefined)
            panel.openRequested(file)
    }
    function openCurrent() {
        openIndex(currentIndex)
    }
    // Enter: a file opens, a folder folds or unfolds.
    function activateCurrent() {
        if (currentIsFile)
            openCurrent()
        else if (fileTree.currentRow >= 0)
            fileTree.toggleExpanded(fileTree.currentRow)
    }
    function rowOf(index) {
        return fileTree.rowAtIndex(index)
    }
    function isExpanded(index) {
        const row = index ? rowOf(index) : -1
        return row >= 0 && fileTree.isExpanded(row)
    }
    function toggleFolder(index) {
        const row = rowOf(index)
        if (row >= 0)
            fileTree.toggleExpanded(row)
    }
    function allReadable() {
        return files.filesUnder()
    }
    // Copy path on a file or folder; on a marked set, every marked path, one a line.
    function copyPaths(kind, file) {
        if (kind === "marked") {
            const marked = files.selection(currentIndex)
            Clipboard.setText(marked.map(function(entry) { return entry.path }).join("\n"))
        } else {
            Clipboard.setText(kind === "file" ? file.path : files.pathAt(fileMenu.index))
        }
    }
    function showProperties(index) {
        const file = files.fileAt(index)
        if (file.path === undefined)
            return
        propertiesPopup.file = file
        const row = rowOf(index)
        const item = row >= 0 ? fileTree.itemAtCell(Qt.point(0, row)) : null
        const at = item ? item.mapToItem(panel, 24, item.height) : Qt.point(24, 48)
        propertiesPopup.x = Math.min(at.x, Math.max(0, panel.width - propertiesPopup.width - 8))
        propertiesPopup.y = at.y
        propertiesPopup.open()
    }
    // The keyboard goes back to the tree after a menu or the properties.
    function returnFocus() {
        Qt.callLater(function() { if (fileTree.visible) fileTree.forceActiveFocus() })
    }
    // The menu for the row at index, or for empty space with an invalid index;
    // at x, y in `from`'s coordinates. Right-clicking makes that row current,
    // as in a file explorer; a marked row among two or more marked acts on the set.
    function openMenu(index, from, x, y) {
        let kind = "empty"
        if (index && index.valid) {
            fileTree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
            kind = !files.isFile(index) ? "folder"
                 : files.isMarked(index) && files.markedCount > 1 ? "marked" : "file"
        }
        fileMenu.kind = kind
        fileMenu.index = index
        fileMenu.file = kind === "file" || kind === "marked" ? files.fileAt(index) : ({})
        fileMenu.popup(from, x, y)
    }
    // The Menu key and Shift+F10: the menu under the current row.
    function openMenuForCurrent() {
        const row = fileTree.currentRow
        const item = row >= 0 ? fileTree.itemAtCell(Qt.point(0, row)) : null
        if (item)
            openMenu(currentIndex, item, 16, item.height)
        else
            openMenu(null, fileTree, 16, 16)
    }

    DeviceFileMenu {
        id: fileMenu
        panel: panel
    }
    DeviceFileProperties {
        id: propertiesPopup
        files: panel.files
        onClosedBack: panel.returnFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // One row: what is listed or marked, then Download and Refresh as
        // pictograms, as an IDE's explorer carries its actions in its header.
        RowLayout {
            objectName: "deviceFilesHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.tight
            Layout.topMargin: Type.tight
            Layout.bottomMargin: Type.tight
            spacing: Type.tight

            Label {
                objectName: "deviceFilesSummary"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: !panel.files.available ? ""
                    : panel.files.loading ? (panel.files.fileCount > 0 ? qsTr("Refreshing…")
                                                                       : qsTr("Reading the file list…"))
                    : panel.files.markedCount > 0
                    ? qsTr("%n marked · %1", "", panel.files.markedCount).arg(panel.files.formatSize(panel.files.markedBytes))
                    : panel.files.fileCount === 1 ? qsTr("1 file") : qsTr("%n files", "", panel.files.fileCount)
            }
            IconToolButton {
                objectName: "deviceFilesDownload"
                iconName: "download"
                visible: panel.files.available && panel.files.fileCount > 0
                // Marked files first; with none marked, the current file.
                text: panel.files.markedCount > 1
                    ? qsTr("Download %n files…", "", panel.files.markedCount)
                    : panel.files.markedCount === 1 ? qsTr("Download 1 file…")
                    : panel.currentIsFile ? qsTr("Download %1…").arg(panel.currentName)
                    : qsTr("Download…")
                shortcutText: qsTr("Ctrl+S")
                enabled: panel.files.markedCount > 0 || panel.currentIsFile
                onClicked: panel.download()
            }
            IconToolButton {
                objectName: "deviceFilesRefresh"
                iconName: "refresh"
                visible: panel.files.available
                text: qsTr("Refresh the file list")
                shortcutText: qsTr("Ctrl+R")
                enabled: !panel.files.loading
                onClicked: panel.files.refresh()
            }
        }

        ProgressLine {
            objectName: "deviceFilesListProgress"
            Layout.fillWidth: true
            progress: -1
            running: panel.files.loading
            reducedMotion: panel.reducedMotion
            description: qsTr("Reading the file list")
        }

        Label {
            objectName: "deviceFilesNotConnected"
            visible: !panel.files.available
            Layout.fillWidth: true
            Layout.margins: Type.groupSpacing
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            text: qsTr("Connect to see the device's files.")
        }

        RowLayout {
            objectName: "deviceFilesError"
            visible: panel.files.available && panel.files.error.length > 0
            Layout.fillWidth: true
            Layout.margins: Type.padding
            spacing: Type.padding

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.error
                text: qsTr("Could not read the file list: %1.").arg(panel.files.error)
            }
            Button {
                text: qsTr("Retry")
                onClicked: panel.files.refresh()
            }
        }

        Label {
            objectName: "deviceFilesTruncated"
            visible: panel.files.available && !panel.files.loading && panel.files.truncated
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            wrapMode: Text.Wrap
            font.pointSize: Type.caption
            color: Theme.warning
            text: qsTr("The device lists more files than the panel holds; the first %1 are shown.")
                .arg(panel.files.maxFiles)
        }

        Label {
            objectName: "deviceFilesEmpty"
            visible: panel.files.available && !panel.files.loading
                && panel.files.error.length === 0 && panel.files.fileCount === 0
            Layout.fillWidth: true
            Layout.margins: Type.groupSpacing
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            text: qsTr("This device has no files.")
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            objectName: "deviceFilesHeading"
            visible: fileTree.visible
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding

            Label {
                Layout.fillWidth: true
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: qsTr("Name")
            }
            Label {
                Layout.preferredWidth: panel.sizeWidth
                horizontalAlignment: Text.AlignRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: qsTr("Size")
            }
            Label {
                visible: panel.accessShown
                Layout.preferredWidth: panel.accessWidth
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: qsTr("Access")
            }
        }

        TreeView {
            id: fileTree
            objectName: "deviceFilesTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.files.available && panel.files.fileCount > 0
            clip: true
            model: panel.files
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: fileTree.model }
            columnWidthProvider: function() { return Math.max(1, fileTree.width) }
            onWidthChanged: Qt.callLater(function() { if (fileTree) fileTree.forceLayout() })

            Keys.onSpacePressed: function(event) {
                const index = fileTree.selectionModel.currentIndex
                if (index.valid) {
                    // The model leaves a file it cannot read unmarked.
                    panel.files.setMarked(index, !panel.files.isMarked(index))
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) {
                if (event.modifiers & Qt.AltModifier)
                    panel.showProperties(panel.currentIndex)
                else
                    panel.activateCurrent()
            }
            Keys.onEnterPressed: panel.activateCurrent()
            // Ctrl+S is Download here: claimed before a window's own Ctrl+S
            // (a host's Save) can take it.
            Keys.onShortcutOverride: function(event) {
                event.accepted = (event.modifiers & Qt.ControlModifier) && !(event.modifiers & Qt.ShiftModifier)
                                 && event.key === Qt.Key_S
            }
            Keys.onPressed: function(event) {
                const control = event.modifiers & Qt.ControlModifier
                const shift = event.modifiers & Qt.ShiftModifier
                const alt = event.modifiers & Qt.AltModifier
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && shift)) {
                    panel.openMenuForCurrent()
                } else if (control && event.key === Qt.Key_S) {
                    panel.download()
                } else if (alt && shift && event.key === Qt.Key_C) {
                    panel.copyPaths(panel.files.isMarked(panel.currentIndex) && panel.files.markedCount > 1 ? "marked"
                                    : panel.currentIsFile ? "file" : "folder", panel.files.fileAt(panel.currentIndex))
                } else if (control && shift && event.key === Qt.Key_A) {
                    panel.files.markAll(false)
                } else if (control && event.key === Qt.Key_A) {
                    panel.files.markAll(true)
                } else if (control && event.key === Qt.Key_R) {
                    if (panel.files.available && !panel.files.loading)
                        panel.files.refresh()
                } else {
                    return
                }
                event.accepted = true
            }
            // Right unfolds and Left folds a folder on every Qt the panel runs on.
            Keys.onRightPressed: function(event) {
                const row = fileTree.currentRow
                event.accepted = row >= 0 && !panel.currentIsFile && !fileTree.isExpanded(row)
                if (event.accepted)
                    fileTree.expand(row)
            }
            Keys.onLeftPressed: function(event) {
                const row = fileTree.currentRow
                event.accepted = row >= 0 && !panel.currentIsFile && fileTree.isExpanded(row)
                if (event.accepted)
                    fileTree.collapse(row)
            }

            delegate: DeviceFileRow {
                sizeWidth: panel.sizeWidth
                accessWidth: panel.accessWidth
                accessShown: panel.accessShown
                onMarkRequested: function(index, value) { panel.files.setMarked(index, value) }
                onOpenRequested: function(index) { panel.openIndex(index) }
                onContextMenuRequested: function(index, x, y) { panel.openMenu(index, this, x, y) }
            }
            // A right click below the rows: the menu for the whole list.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: function(point) {
                    if (fileTree.cellAtPosition(point.position.x, point.position.y, true).y < 0)
                        panel.openMenu(null, fileTree, point.position.x, point.position.y)
                }
            }
            ScrollBar.vertical: ScrollBar {}
            Component.onCompleted: Qt.callLater(function() {
                if (fileTree)
                    fileTree.expandRecursively()
            })
            Connections {
                target: panel.files
                function onModelReset() {
                    Qt.callLater(function() { if (fileTree) fileTree.expandRecursively() })
                }
            }
        }

        Label {
            objectName: "deviceFilesSystemOnly"
            visible: panel.files.available && !panel.files.loading && panel.files.systemOnly
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            wrapMode: Text.Wrap
            font.pointSize: Type.caption
            color: Theme.textMuted
            text: qsTr("Only system files so far. Logs and settings appear here when the firmware registers them.")
        }

        DeviceTransfersPanel {
            objectName: "deviceFilesTransfersPanel"
            visible: panel.transfersShown && count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            transfers: panel.transfers
            reducedMotion: panel.reducedMotion
            onShowInFolderRequested: function(localPath) { panel.showInFolderRequested(localPath) }
        }
    }
}
