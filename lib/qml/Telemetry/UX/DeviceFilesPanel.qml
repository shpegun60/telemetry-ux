// The device's files as a tree of their own paths, with downloads and their
// progress. Reusable presentation: the host owns the models, the dialogs and
// every write to disk. MIT.
//
// The host gives `files` (DeviceFilesModel) and `transfers`
// (DeviceFileTransfers). Download asks the host for a destination through
// downloadRequested(selection) - a save dialog for one file, a folder for
// several - and the host calls transfers.download(...). Open asks the host to
// show a file through openRequested(file); Show in folder through
// showInFolderRequested(localPath).
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

    signal downloadRequested(var selection)
    signal openRequested(var file)
    signal showInFolderRequested(string localPath)

    // DeviceFileTransfers.State, in its order.
    readonly property int stateWaiting: 0
    readonly property int stateReading: 1
    readonly property int stateSaved: 2
    readonly property int stateCancelled: 3
    readonly property int stateFailed: 4

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
    TextMetrics { id: countMetrics; font.pointSize: Type.caption; text: qsTr("%n file(s)", "", 4096) }

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Label {
                objectName: "deviceFilesSummary"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: !panel.files.available ? ""
                    : panel.files.loading ? (panel.files.fileCount > 0 ? qsTr("Refreshing…")
                                                                       : qsTr("Reading the file list…"))
                    : qsTr("%n file(s)", "", panel.files.fileCount)
            }
            Button {
                objectName: "deviceFilesRefresh"
                text: qsTr("Refresh")
                enabled: panel.files.available && !panel.files.loading
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
            Layout.margins: 16
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            text: qsTr("Connect to see the device's files.")
        }

        RowLayout {
            objectName: "deviceFilesError"
            visible: panel.files.available && panel.files.error.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

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
            Layout.leftMargin: 8
            Layout.rightMargin: 8
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
            Layout.margins: 16
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            text: qsTr("This device has no files.")
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            objectName: "deviceFilesHeading"
            visible: fileTree.visible
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8

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
            Keys.onReturnPressed: panel.activateCurrent()
            Keys.onEnterPressed: panel.activateCurrent()
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
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            wrapMode: Text.Wrap
            font.pointSize: Type.caption
            color: Theme.textMuted
            text: qsTr("Only system files so far. Logs and settings appear here when the firmware registers them.")
        }

        RowLayout {
            objectName: "deviceFilesActions"
            visible: panel.files.available && panel.files.fileCount > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Label {
                objectName: "deviceFilesMarked"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: panel.files.markedCount > 0
                    ? qsTr("%n marked · %1", "", panel.files.markedCount)
                          .arg(panel.files.formatSize(panel.files.markedBytes))
                    : ""
            }
            Button {
                objectName: "deviceFilesOpen"
                text: qsTr("Open")
                enabled: panel.currentIsFile
                onClicked: panel.openCurrent()
            }
            Button {
                objectName: "deviceFilesDownload"
                // Marked files first; with none marked, the current file.
                text: panel.files.markedCount > 1
                    ? qsTr("Download %n files…", "", panel.files.markedCount)
                    : panel.files.markedCount === 1 ? qsTr("Download 1 file…")
                    : panel.currentIsFile ? qsTr("Download %1…").arg(panel.currentName)
                    : qsTr("Download…")
                enabled: panel.files.markedCount > 0 || panel.currentIsFile
                onClicked: panel.download()
            }
        }

        ListView {
            id: transferList
            objectName: "deviceFilesTransfers"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 4 * (Type.rowHeight + 8) + 32)
            visible: count > 0
            clip: true
            model: panel.transfers
            spacing: 4

            header: RowLayout {
                width: transferList.width
                spacing: 8

                Label {
                    objectName: "deviceFilesTransfersHeading"
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    font.pointSize: Type.caption
                    font.weight: Type.headingWeight
                    color: Theme.textSecondary
                    text: qsTr("Transfers")
                }
                LinkButton {
                    objectName: "deviceFilesCancelAll"
                    visible: panel.transfers.activeCount >= 2
                    tint: Theme.accent
                    text: qsTr("Cancel all")
                    onClicked: panel.transfers.cancelAll()
                }
                LinkButton {
                    objectName: "deviceFilesClearFinished"
                    Layout.rightMargin: 8
                    visible: panel.finishedTransfers > 0
                    tint: Theme.accent
                    text: qsTr("Clear finished")
                    onClicked: panel.transfers.clearFinished()
                }
            }

            delegate: ColumnLayout {
                id: transferRow
                objectName: "deviceFileTransfer"
                required property int index
                required property string name
                required property string localPath
                required property int state
                required property string figures
                required property string result
                required property real progress
                required property bool active

                readonly property bool failed: state === panel.stateFailed
                readonly property bool saved: state === panel.stateSaved

                width: transferList.width
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 8

                    Label {
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: transferList.width / 3
                        color: Theme.textPrimary
                        text: transferRow.name
                    }
                    Label {
                        objectName: "deviceFileTransferFigures"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pointSize: Type.caption
                        color: transferRow.active ? Theme.textSecondary
                             : transferRow.failed ? Theme.error
                             : transferRow.saved ? Theme.textSecondary : Theme.textMuted
                        text: transferRow.active ? transferRow.figures
                            : transferRow.failed ? qsTr("! %1").arg(transferRow.result)
                            : transferRow.result
                    }
                    LinkButton {
                        objectName: "deviceFileTransferCancel"
                        visible: transferRow.active
                        tint: Theme.accent
                        text: qsTr("Cancel")
                        Accessible.name: qsTr("Cancel %1").arg(transferRow.name)
                        onClicked: panel.transfers.cancel(transferRow.index)
                    }
                    LinkButton {
                        objectName: "deviceFileTransferRetry"
                        visible: transferRow.failed
                        tint: Theme.accent
                        text: qsTr("Retry")
                        Accessible.name: qsTr("Retry %1").arg(transferRow.name)
                        onClicked: panel.transfers.retry(transferRow.index)
                    }
                    LinkButton {
                        objectName: "deviceFileTransferShow"
                        visible: transferRow.saved
                        tint: Theme.accent
                        text: qsTr("Show in folder")
                        Accessible.name: qsTr("Show %1 in its folder").arg(transferRow.name)
                        onClicked: panel.showInFolderRequested(transferRow.localPath)
                    }
                }
                // A waiting row has no bar: only the file being read has one.
                ProgressLine {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    progress: transferRow.progress
                    running: transferRow.state === panel.stateReading
                    reducedMotion: panel.reducedMotion
                    description: qsTr("Downloading %1").arg(transferRow.name)
                    valueText: transferRow.figures
                }
            }
        }
    }
}
