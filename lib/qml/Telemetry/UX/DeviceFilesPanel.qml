// The device's files as a tree of their own paths, with downloads and their
// progress. Reusable presentation: the host owns the models, the dialogs and
// every write to disk. MIT.
//
// The host gives `files` (DeviceFilesModel) and `transfers`
// (DeviceFileTransfers). Download asks the host for a destination through
// downloadRequested(selection) - a save dialog for one file, a folder for
// several - and the host calls transfers.download(...). Open asks the host to
// show a file through openRequested(file).
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

    readonly property var currentIndex: fileTree.selectionModel.currentIndex
    readonly property bool currentIsFile: files.isFile(currentIndex)
    readonly property string currentName: currentIsFile ? files.data(currentIndex, Qt.DisplayRole) : ""

    function chosen() {
        return files.selection(currentIndex)
    }
    function download() {
        const selection = chosen()
        if (selection.length > 0)
            panel.downloadRequested(selection)
    }
    function openCurrent() {
        const selection = files.selection(currentIndex)
        if (currentIsFile && selection.length > 0)
            panel.openRequested(selection[0])
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
                    : panel.files.loading ? qsTr("Reading the file list…")
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
            objectName: "deviceFilesEmpty"
            visible: panel.files.available && !panel.files.loading
                && panel.files.error.length === 0 && panel.files.fileCount === 0
            Layout.fillWidth: true
            Layout.margins: 16
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            text: qsTr("This device has no files.")
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
                    panel.files.setMarked(index, !panel.files.isMarked(index))
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: panel.openCurrent()
            Keys.onEnterPressed: panel.openCurrent()

            delegate: TreeViewDelegate {
                id: fileRow
                objectName: "deviceFileRow"
                required property string name
                required property string path
                required property bool isFolder
                required property string sizeText
                required property string accessText
                required property int marked
                readonly property var sourceIndex: fileTree.modelIndex(Qt.point(0, row))

                implicitWidth: fileTree.width
                implicitHeight: Type.rowHeight
                text: name

                // The device's name goes in last, under the highest number, so
                // nothing in it is ever read as a placeholder.
                Accessible.name: isFolder
                    ? qsTr("%2, folder, %1").arg(sizeText).arg(name)
                    : qsTr("%3, %1, %2").arg(sizeText).arg(accessText).arg(name)

                contentItem: RowLayout {
                    spacing: 8

                    CheckBox {
                        objectName: "deviceFileMark"
                        focusPolicy: Qt.NoFocus
                        tristate: fileRow.isFolder
                        checkState: fileRow.marked
                        Accessible.name: qsTr("Mark %1 for download").arg(fileRow.name)
                        onClicked: panel.files.setMarked(fileRow.sourceIndex,
                                                         fileRow.marked !== Qt.Checked)
                    }
                    // The device's own name, never replaced.
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        text: fileRow.name
                        color: Theme.textPrimary
                    }
                    Label {
                        objectName: "deviceFileSize"
                        horizontalAlignment: Text.AlignRight
                        font.pointSize: Type.caption
                        color: fileRow.isFolder ? Theme.textMuted : Theme.textSecondary
                        text: fileRow.sizeText
                    }
                    Label {
                        objectName: "deviceFileAccess"
                        visible: !fileRow.isFolder && fileTree.width >= 360
                        Layout.preferredWidth: 72
                        font.pointSize: Type.caption
                        color: Theme.textSecondary
                        text: fileRow.accessText
                    }
                }
                onDoubleClicked: if (!isFolder) panel.openCurrent()
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
            Layout.preferredHeight: Math.min(contentHeight, 4 * (Type.rowHeight + 8))
            visible: count > 0
            clip: true
            model: panel.transfers
            spacing: 4

            header: Label {
                width: transferList.width
                leftPadding: 8
                font.pointSize: Type.caption
                color: Theme.textMuted
                text: qsTr("Transfers")
            }

            delegate: ColumnLayout {
                id: transferRow
                objectName: "deviceFileTransfer"
                required property int index
                required property string name
                required property string figures
                required property string result
                required property real progress
                required property bool active

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
                        color: transferRow.active ? Theme.textSecondary : Theme.textMuted
                        text: transferRow.active ? transferRow.figures : transferRow.result
                    }
                    Button {
                        objectName: "deviceFileTransferCancel"
                        visible: transferRow.active
                        flat: true
                        text: qsTr("Cancel")
                        Accessible.name: qsTr("Cancel %1").arg(transferRow.name)
                        onClicked: panel.transfers.cancel(transferRow.index)
                    }
                }
                ProgressLine {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    progress: transferRow.progress
                    running: transferRow.active
                    reducedMotion: panel.reducedMotion
                    description: qsTr("Downloading %1").arg(transferRow.name)
                    valueText: transferRow.figures
                }
            }
        }
    }
}
