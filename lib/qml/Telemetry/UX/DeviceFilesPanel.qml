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
// Menu key, Shift+F10) rather than buttons, and they act on the selection,
// which the model keeps. Click, Ctrl+click and Shift+click select; the arrows,
// Home, End, Page Up and Page Down move it (Shift extends, Ctrl moves alone);
// Space adds or removes the current row; Enter opens the selection, or folds
// a folder; Ctrl+S downloads, Alt+Shift+C copies the path, Alt+Enter shows the
// properties, Ctrl+A selects every readable file, Esc and Ctrl+Shift+A select
// none, Ctrl+R refreshes. The host calls registerQmlTypes() first (Clipboard).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Pane {
    id: panel
    objectName: "deviceFilesPane"
    padding: 0

    required property var files
    required property var transfers
    property bool reducedMotion: false
    // A host Explorer can provide its own compact section header and actions.
    property bool headerShown: true
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

    property bool modelResetting: false
    readonly property var currentIndex: modelResetting ? null : fileTree.selectionModel.currentIndex
    readonly property bool currentIsFile: !modelResetting && !!currentIndex && currentIndex.valid && files.isFile(currentIndex)
    readonly property string currentName: currentIsFile && currentIndex && currentIndex.valid ? files.data(currentIndex, Qt.DisplayRole) : ""
    readonly property int finishedTransfers: transfers.rowCount() - transfers.activeCount

    // One width per column, shared by the heading and every row.
    readonly property real sizeWidth: Math.ceil(sizeMetrics.advanceWidth) + 4
    // What a file is in words, from 360 px; its access as three letters at
    // every width.
    readonly property real kindWidth: Math.ceil(Math.max(kindMetrics.advanceWidth,
                                                         countMetrics.advanceWidth)) + 4
    readonly property bool kindShown: fileTree.width >= 360
    readonly property real markWidth: Math.ceil(markMetrics.advanceWidth) + 2
    TextMetrics { id: sizeMetrics; font.pointSize: Type.caption; text: "888.8 KiB" }
    TextMetrics { id: kindMetrics; font.pointSize: Type.caption; text: qsTr("Capability") }
    TextMetrics { id: countMetrics; font.pointSize: Type.caption; text: qsTr("%n files", "", 4096) }
    TextMetrics { id: markMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "rws" }

    function chosen() {
        if (modelResetting)
            return []
        return currentIndex && currentIndex.valid ? files.selectedFiles(currentIndex) : files.selectedFiles()
    }
    function download() {
        const selection = chosen()
        if (selection.length > 0)
            panel.downloadRequested(selection)
    }
    function openIndex(index) {
        if (modelResetting || !index || !index.valid) return
        const file = files.fileAt(index)
        if (file.path !== undefined && (!file.sizeKnown || file.readable))
            panel.openRequested(file)
    }
    function openCurrent() {
        openIndex(currentIndex)
    }
    // At most this many files open from one Enter; with more selected,
    // Enter opens the current file alone.
    readonly property int openAtOnce: 16
    // Enter: on a selected file, every selected file the device lets us read;
    // on any other file, that file; a folder folds or unfolds.
    function activateCurrent() {
        if (modelResetting || !currentIndex || !currentIndex.valid) return
        if (currentIsFile) {
            const selected = files.isSelected(currentIndex) ? files.selectedFiles(currentIndex) : []
            if (selected.length > 1 && selected.length <= openAtOnce)
                selected.forEach(function(file) { panel.openRequested(file) })
            else
                openCurrent()
        } else if (fileTree.currentRow >= 0) {
            fileTree.toggleExpanded(fileTree.currentRow)
        }
    }
    function rowOf(index) {
        return modelResetting || !index || !index.valid ? -1 : fileTree.rowAtIndex(index)
    }
    // The selection, as a file explorer's: the model keeps it, files only,
    // by path across Refresh; a folder row stands for its files. The anchor,
    // where a Shift range starts, is kept as a path: rows move when folders
    // fold and when the list is read again.
    property string anchorPath: ""
    function setAnchor(index) {
        anchorPath = index && index.valid ? files.pathAt(index) : ""
    }
    // The anchor's row now; where it is hidden or gone, `fallback`.
    function anchorRow(fallback) {
        const row = anchorPath.length > 0 ? rowOf(files.indexOfPath(anchorPath)) : -1
        return row >= 0 ? row : fallback
    }
    // The rows shown from one row to another: a file as it is, a folded
    // folder for its readable files; an unfolded folder adds nothing, its
    // rows below speak for it. With `add` the rest stays selected.
    function selectRows(from, to, add) {
        const first = Math.max(0, Math.min(from, to)), last = Math.min(fileTree.rows - 1, Math.max(from, to))
        const indexes = []
        for (let row = first; row <= last; ++row) {
            const index = fileTree.index(row, 0)
            if (files.isFile(index) || !fileTree.isExpanded(row))
                indexes.push(index)
        }
        files.selectIndexes(indexes, add)
    }
    function rowClicked(index, row, modifiers) {
        const before = fileTree.currentRow
        fileTree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        if (modifiers & Qt.ShiftModifier) {
            // No anchor, or one now hidden: the range starts where the
            // current row was, and that is the anchor from now on.
            let from = anchorRow(-1)
            if (from < 0) {
                from = before >= 0 ? before : row
                setAnchor(fileTree.index(from, 0))
            }
            selectRows(from, row, modifiers & Qt.ControlModifier)
            return
        }
        if (modifiers & Qt.ControlModifier)
            files.toggleSelected(index)
        else
            files.selectOnly(index)
        setAnchor(index)
    }
    // A key moves the current row to `row` and the selection with it; Shift
    // extends from the anchor; Ctrl moves alone.
    function moveTo(row, modifiers) {
        if (fileTree.rows === 0)
            return
        row = Math.max(0, Math.min(fileTree.rows - 1, row))
        const index = fileTree.index(row, 0)
        const anchored = anchorRow(-1)
        const from = anchored >= 0 ? anchored : fileTree.currentRow >= 0 ? fileTree.currentRow : row
        fileTree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        fileTree.positionViewAtRow(row, TableView.Contain)
        if (modifiers & Qt.ShiftModifier) {
            // No anchor, or one now hidden: the range grows from where it
            // started, never slides with the current row.
            if (anchored < 0)
                setAnchor(fileTree.index(from, 0))
            selectRows(from, row, false)
        } else if (!(modifiers & Qt.ControlModifier)) {
            files.selectOnly(index)
            setAnchor(index)
        }
    }
    function moveCurrent(step, modifiers) {
        moveTo((fileTree.currentRow < 0 ? (step > 0 ? -1 : fileTree.rows) : fileTree.currentRow) + step, modifiers)
    }
    // Page Up and Page Down: the rows that fit, less one kept in view.
    readonly property int pageRows: Math.max(1, Math.floor(fileTree.height / Type.rowHeight) - 1)
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
    // Copy path on a file or folder; on a selection, every selected path, one a line.
    function copyPaths(kind, file) {
        if (modelResetting) return
        if (kind === "selection") {
            Clipboard.setText(files.selectedPaths().join("\n"))
        } else if (kind === "file") {
            if (file && typeof file.path === "string")
                Clipboard.setText(file.path)
        } else if (kind === "folder" && currentIndex && currentIndex.valid) {
            const path = files.pathAt(currentIndex)
            if (path.length > 0)
                Clipboard.setText(path)
        }
    }
    function showProperties(index) {
        if (modelResetting || !index || !index.valid) return
        const file = files.fileAt(index)
        if (file.path === undefined)
            return
        propertiesPopup.file = file
        const row = rowOf(index)
        // The popup stands beside its row: a row not wholly in view - scrolled
        // away or cut by an edge - is brought to the middle first.
        if (row >= 0) {
            const shown = fileTree.itemAtCell(Qt.point(0, row))
            const top = shown ? shown.y - fileTree.contentY : -1
            if (!shown || top < 0 || top + shown.height > fileTree.height) {
                fileTree.positionViewAtRow(row, TableView.AlignVCenter)
                fileTree.forceLayout()
            }
        }
        const item = row >= 0 ? fileTree.itemAtCell(Qt.point(0, row)) : null
        const below = item ? item.mapToItem(panel, 24, item.height) : Qt.point(24, 48)
        const above = item ? item.mapToItem(panel, 24, 0) : below
        // Bound to the popup's own size, which settles only once it has laid
        // out the new file: under its row, or above it - its bottom 4 px
        // over the row's top - where there is no room below.
        const found = !!item
        propertiesPopup.x = Qt.binding(function() {
            return Math.min(below.x, Math.max(0, panel.width - propertiesPopup.width - 8))
        })
        propertiesPopup.y = Qt.binding(function() {
            const height = propertiesPopup.height
            return !found || below.y + height <= panel.height ? below.y : Math.max(0, above.y - height - 4)
        })
        propertiesPopup.open()
    }
    // The keyboard goes back to the tree after a menu or the properties.
    function returnFocus() {
        Qt.callLater(function() { if (fileTree && fileTree.visible) fileTree.forceActiveFocus() })
    }
    // The menu for the row at index, or for empty space with an invalid index;
    // at x, y in `from`'s coordinates. Right-clicking makes that row current,
    // as in a file explorer; a selected file among two or more selected acts
    // on the selection.
    function openMenu(index, from, x, y) {
        if (modelResetting) return
        let kind = "empty"
        if (index && index.valid) {
            fileTree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
            // As in a file explorer: a right click on a file outside the
            // selection selects that file. A folder has its own menu.
            if (files.isFile(index) && !files.isSelected(index)) {
                files.selectOnly(index)
                setAnchor(index)
            }
            kind = !files.isFile(index) ? "folder"
                 : files.isSelected(index) && files.selectedCount > 1 ? "selection" : "file"
        }
        fileMenu.kind = kind
        fileMenu.index = index
        fileMenu.file = kind === "file" || kind === "selection" ? files.fileAt(index) : ({})
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

    // Release plain indexes while their nodes still exist. Popup close handlers
    // may publish/focus synchronously, so no old menu target survives close().
    function clearModelTargets() {
        if (fileMenu) {
            fileMenu.index = undefined
            fileMenu.kind = "empty"
            fileMenu.file = ({})
        }
        if (propertiesPopup) propertiesPopup.file = ({})
        if (fileTree && fileTree.selectionModel) fileTree.selectionModel.clear()
        if (fileMenu) fileMenu.close()
        if (propertiesPopup) propertiesPopup.close()
    }
    onFilesChanged: {
        modelResetting = true
        clearModelTargets()
        anchorPath = ""
        modelResetting = false
    }
    Connections {
        target: panel.files
        function onModelAboutToBeReset() {
            panel.modelResetting = true
            panel.clearModelTargets()
        }
        function onModelReset() {
            panel.clearModelTargets()
            panel.modelResetting = false
            Qt.callLater(function() { if (fileTree) fileTree.expandRecursively() })
        }
        function onStateChanged() {
            if (!panel.files.available) {
                panel.clearModelTargets()
                panel.anchorPath = ""
            }
        }
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

        // One row: what is listed or selected, then Download and Refresh as
        // pictograms, as an IDE's explorer carries its actions in its header.
        RowLayout {
            objectName: "deviceFilesHeader"
            visible: panel.headerShown
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
                    : panel.files.selectedCount === 1
                    ? qsTr("1 file selected · %1").arg(panel.files.formatSize(panel.files.selectedBytes))
                    : panel.files.selectedCount > 1
                    ? qsTr("%n files selected · %1", "", panel.files.selectedCount).arg(panel.files.formatSize(panel.files.selectedBytes))
                    : panel.files.fileCount === 1 ? qsTr("1 file") : qsTr("%n files", "", panel.files.fileCount)
            }
            IconToolButton {
                objectName: "deviceFilesDownload"
                iconName: "download"
                visible: panel.files.available && panel.files.fileCount > 0
                // The selected files the device lets us read; with none
                // selected, the current file. What is left out says so.
                readonly property int readable: panel.files.selectedReadable
                readonly property int skipped: panel.files.selectedCount - readable
                text: panel.files.selectedCount > 0 && readable === 0
                    ? qsTr("Download… (the selected files can't be read)")
                    : skipped > 0
                    ? qsTr("Download %n files… (%1 can't be read)", "", readable).arg(skipped)
                    : readable > 1 ? qsTr("Download %n files…", "", readable)
                    : readable === 1 ? qsTr("Download 1 file…")
                    : panel.currentIsFile ? qsTr("Download %1…").arg(panel.currentName)
                    : qsTr("Download…")
                shortcutText: qsTr("Ctrl+S")
                enabled: readable > 0 || (panel.files.selectedCount === 0 && panel.currentIsFile)
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

        // The columns' titles, at the rows' widths, on an opaque strip with a
        // hairline: a row scrolled half away under it never reads as drawn
        // through it.
        Rectangle {
            visible: fileTree.visible
            Layout.fillWidth: true
            Layout.preferredHeight: heading.implicitHeight + 1
            z: 1
            color: Theme.surfaceWindow
            RowLayout {
                id: heading
                objectName: "deviceFilesHeading"
                visible: fileTree.visible
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Type.padding
                anchors.rightMargin: Type.padding
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
                    visible: panel.kindShown
                    Layout.preferredWidth: panel.kindWidth
                    font.pointSize: Type.caption
                    color: Theme.textSecondary
                    text: qsTr("Kind")
                }
                // The access letters' column, named by its letters.
                Label {
                    objectName: "deviceFilesAccessHeading"
                    Layout.preferredWidth: panel.markWidth
                    Layout.leftMargin: Type.padding
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    color: Theme.textMuted
                    text: "rws"
                    HoverHandler { id: accessHeadingHover }
                    ToolTip.visible: accessHeadingHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Access: r can be read, w can be written, s keeps what is written across a restart.")
                    Accessible.name: qsTr("Access")
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.outlineVariant
            }
        }

        TreeView {
            id: fileTree
            // A zoom changes the row height: the rows already laid out follow it.
            Connections {
                target: Type
                function onRowHeightChanged() { Qt.callLater(function() { if (fileTree) fileTree.forceLayout() }) }
            }
            objectName: "deviceFilesTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.files.available && panel.files.fileCount > 0
            clip: true
            model: panel.files
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: fileTree.model }
            // The selection is the model's (the panel's gestures): the view's
            // own would take Ctrl+click for itself. It keeps only the current row.
            selectionBehavior: TableView.SelectionDisabled
            // Scrolled at once, as an IDE's list is: a row brought into view
            // is where it will stay, so Properties can stand beside it.
            animate: false
            columnWidthProvider: function() { return Math.max(1, fileTree.width) }
            onWidthChanged: Qt.callLater(function() { if (fileTree) fileTree.forceLayout() })

            // Space and Ctrl+Space: the current row in or out of the selection.
            Keys.onSpacePressed: function(event) {
                const index = fileTree.selectionModel.currentIndex
                if (index.valid) {
                    panel.files.toggleSelected(index)
                    panel.setAnchor(index)
                    event.accepted = true
                }
            }
            Keys.onEscapePressed: function(event) {
                event.accepted = panel.files.selectedCount > 0
                if (event.accepted)
                    panel.files.clearSelection()
            }
            Keys.onUpPressed: function(event) { panel.moveCurrent(-1, event.modifiers) }
            Keys.onDownPressed: function(event) { panel.moveCurrent(1, event.modifiers) }
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
                    panel.copyPaths(panel.currentIsFile && panel.currentIndex && panel.currentIndex.valid
                                    && panel.files.isSelected(panel.currentIndex) && panel.files.selectedCount > 1 ? "selection"
                                    : panel.currentIsFile ? "file" : "folder",
                                    panel.currentIsFile && panel.currentIndex && panel.currentIndex.valid
                                        ? panel.files.fileAt(panel.currentIndex) : ({}))
                } else if (control && shift && event.key === Qt.Key_A) {
                    panel.files.clearSelection()
                } else if (control && event.key === Qt.Key_A) {
                    panel.files.selectAll()
                } else if (event.key === Qt.Key_Home) {
                    panel.moveTo(0, event.modifiers)
                } else if (event.key === Qt.Key_End) {
                    panel.moveTo(fileTree.rows - 1, event.modifiers)
                } else if (event.key === Qt.Key_PageUp) {
                    panel.moveTo(fileTree.currentRow - panel.pageRows, event.modifiers)
                } else if (event.key === Qt.Key_PageDown) {
                    panel.moveTo(fileTree.currentRow + panel.pageRows, event.modifiers)
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
                kindWidth: panel.kindWidth
                kindShown: panel.kindShown
                markWidth: panel.markWidth
                onRowClicked: function(index, row, modifiers) { panel.rowClicked(index, row, modifiers) }
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

        // Made only while shown here: a host that docks Transfers apart
        // (transfersShown: false) keeps no second list to update.
        Loader {
            objectName: "deviceFilesTransfersPanel"
            active: panel.transfersShown
            visible: active && !!item && item.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: item ? item.implicitHeight : 0
            sourceComponent: DeviceTransfersPanel {
                transfers: panel.transfers
                reducedMotion: panel.reducedMotion
                onShowInFolderRequested: function(localPath) { panel.showInFolderRequested(localPath) }
            }
        }
    }
}
