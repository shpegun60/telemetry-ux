// One file in the viewer: its format's own view when a codec read it, a table
// when it is CSV, text when it is text, hex always, with a switch between the
// ones it has. Read-only. MIT.
//
// The host gives `content`, a FileContent - usually a row of OpenFiles, whose
// role of that name fills it in a view's delegate - and answers the requests:
// reloadRequested() to read the device's file again, cancelRequested() while
// it is being read, openExternallyRequested and showInFolderRequested with the
// local file, closeRequested.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Pane {
    id: viewer
    objectName: "fileViewer"
    padding: 0

    required property var content
    // The host can read the device's file again (connected, the file listed).
    property bool reloadable: false
    // "decoded", "table", "text" or "hex"; empty for the format's own first choice.
    property string mode: ""
    readonly property bool ready: !!content && content.loaded
    readonly property bool reading: !!content && content.loading
    readonly property string problem: content ? content.error : ""
    // Nothing is shown until the file has been read.
    readonly property string shownMode: !ready ? "" : offers(mode) ? mode
        : content.decoded ? "decoded" : content.tabular ? "table" : content.textual ? "text" : "hex"
    readonly property int modeCount: (offers("decoded") ? 1 : 0) + (offers("table") ? 1 : 0)
        + (offers("text") ? 1 : 0) + (offers("hex") ? 1 : 0)
    // The device's name, or the local file's for one from disk.
    readonly property string fileName: {
        if (!content)
            return ""
        const path = content.devicePath.length > 0 ? content.devicePath : content.localFile
        return path.substring(path.lastIndexOf("/") + 1)
    }

    signal reloadRequested()
    signal cancelRequested()
    signal openExternallyRequested(string localFile)
    signal showInFolderRequested(string localFile)
    signal closeRequested()

    // The keyboard into the file: the table, the hex rows or the text, or
    // Cancel while it is being read.
    function focusContent() {
        if (reading)
            cancelLink.forceActiveFocus()
        else if (shownMode === "table")
            table.forceActiveFocus()
        else if (shownMode === "hex")
            hexView.forceActiveFocus()
        else if (shownMode === "text")
            textArea.forceActiveFocus()
        else if (shownMode === "decoded" && decodedView.item)
            decodedView.item.forceActiveFocus()
    }

    function offers(name) {
        if (!viewer.content || !viewer.content.loaded)
            return false
        return name === "hex" ? true
             : name === "decoded" ? !!viewer.content.decoded
             : name === "text" ? viewer.content.textual
             : name === "table" ? viewer.content.tabular : false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            // The ways this file can be shown; hidden when there is only one.
            ButtonGroup { id: modes }
            Repeater {
                model: [
                    // The codec's name, translated where it was made.
                    { name: "decoded", text: viewer.content ? viewer.content.codecName : "" },
                    { name: "table", text: qsTr("Table") },
                    { name: "text", text: qsTr("Text") },
                    { name: "hex", text: qsTr("Hex") }
                ]
                delegate: Button {
                    required property var modelData
                    objectName: "fileViewerMode_" + modelData.name
                    visible: viewer.modeCount > 1 && viewer.offers(modelData.name)
                    text: modelData.text
                    checkable: true
                    checked: viewer.shownMode === modelData.name
                    flat: !checked
                    ButtonGroup.group: modes
                    onClicked: viewer.mode = modelData.name
                }
            }
            Item { Layout.fillWidth: true }
            LinkButton {
                objectName: "fileViewerReload"
                visible: viewer.reloadable && !viewer.reading
                tint: Theme.textSecondary
                text: qsTr("Reload")
                onClicked: viewer.reloadRequested()
            }
            LinkButton {
                objectName: "fileViewerExternal"
                visible: viewer.ready
                tint: Theme.textSecondary
                text: qsTr("Open in another program")
                onClicked: viewer.openExternallyRequested(viewer.content.localFile)
            }
            LinkButton {
                objectName: "fileViewerShow"
                visible: viewer.ready
                tint: Theme.textSecondary
                text: qsTr("Show in folder")
                onClicked: viewer.showInFolderRequested(viewer.content.localFile)
            }
            LinkButton {
                objectName: "fileViewerClose"
                tint: Theme.textSecondary
                text: qsTr("Close")
                onClicked: viewer.closeRequested()
            }
        }

        // While the device's file is read: what, how far, and a way out.
        Item {
            objectName: "fileViewerReading"
            visible: viewer.reading
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height / 3 - height / 2
                width: Math.min(320, parent.width - 32)
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: Theme.textSecondary
                    // The device's name goes in last, under the highest number.
                    text: viewer.reading ? qsTr("Reading %2… %1").arg(viewer.content.figures).arg(viewer.fileName) : ""
                }
                ProgressLine {
                    Layout.fillWidth: true
                    thickness: 6
                    progress: viewer.reading ? viewer.content.progress : -1
                    running: viewer.reading
                    description: qsTr("Reading %1").arg(viewer.fileName)
                    valueText: viewer.reading ? viewer.content.figures : ""
                }
                LinkButton {
                    id: cancelLink
                    objectName: "fileViewerCancel"
                    Layout.alignment: Qt.AlignHCenter
                    tint: Theme.textSecondary
                    text: qsTr("Cancel")
                    onClicked: viewer.cancelRequested()
                }
            }
        }

        // The read failed, or the copy could not be read: why, and a way back.
        RowLayout {
            objectName: "fileViewerError"
            visible: !viewer.reading && viewer.problem.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.error
                text: qsTr("! %1").arg(viewer.problem)
            }
            LinkButton {
                objectName: "fileViewerReadAgain"
                visible: viewer.reloadable
                tint: Theme.errorLink
                text: qsTr("Read again")
                onClicked: viewer.reloadRequested()
            }
        }

        // A codec took the file for its own but could not read it: the file is
        // shown as text or hex, and this says why.
        Label {
            objectName: "fileViewerCodecProblem"
            visible: viewer.ready && !!viewer.content.codecError && viewer.content.codecError.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: Theme.warning
            text: visible ? qsTr("! %1").arg(viewer.content.codecError) : ""
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: viewer.ready
            currentIndex: viewer.shownMode === "table" ? 0 : viewer.shownMode === "text" ? 1
                        : viewer.shownMode === "decoded" ? 3 : 2

            // Table: the CSV's first row names the columns; numbers are right-aligned.
            ColumnLayout {
                spacing: 0
                HorizontalHeaderView {
                    objectName: "fileViewerTableHeader"
                    Layout.fillWidth: true
                    syncView: table
                    clip: true
                }
                TableView {
                    id: table
                    objectName: "fileViewerTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    columnSpacing: 1
                    rowSpacing: 1
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    selectionModel: ItemSelectionModel { model: table.model }
                    model: viewer.shownMode === "table" ? viewer.content.table : null
                    delegate: Rectangle {
                        id: tableCell
                        required property string display
                        required property int row
                        required property int column
                        required property bool current
                        implicitWidth: Math.min(360, Math.max(72, cell.implicitWidth + 16))
                        implicitHeight: Type.rowHeight
                        color: row % 2 ? Theme.surfaceAlternate : Theme.surface
                        border.width: current && table.activeFocus ? 2 : 0
                        border.color: Theme.focusRing
                        Accessible.role: Accessible.Cell
                        Accessible.name: qsTr("%1: %2")
                            .arg(viewer.content.table.headerData(tableCell.column, Qt.Horizontal))
                            .arg(tableCell.display)
                        Label {
                            id: cell
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: viewer.content.table.numeric(tableCell.column) ? Text.AlignRight
                                                                                                : Text.AlignLeft
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                            text: tableCell.display
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {}
                }
            }

            // Text, read-only, in the fixed font.
            ScrollView {
                objectName: "fileViewerTextScroll"
                clip: true
                TextArea {
                    id: textArea
                    objectName: "fileViewerText"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    font.family: Type.fixedFamily
                    color: Theme.textPrimary
                    text: viewer.shownMode === "text" ? viewer.content.text : ""
                }
            }

            // Hex: offset, 16 bytes, their ASCII; the current row framed.
            ListView {
                id: hexView
                objectName: "fileViewerHex"
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                model: viewer.shownMode === "hex" ? viewer.content.hex : null
                highlight: Rectangle {
                    color: "transparent"
                    radius: 3
                    border.width: hexView.activeFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                delegate: Row {
                    id: hexRow
                    required property string offset
                    required property string bytes
                    required property string ascii
                    leftPadding: 8
                    spacing: 16
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("%1: %2").arg(hexRow.offset).arg(hexRow.bytes)
                    Label { font.family: Type.fixedFamily; color: Theme.textSecondary; text: hexRow.offset }
                    Label { font.family: Type.fixedFamily; color: Theme.textPrimary; text: hexRow.bytes }
                    Label { font.family: Type.fixedFamily; color: Theme.textSecondary; text: hexRow.ascii }
                }
                ScrollBar.vertical: ScrollBar {}
            }

            // The codec's own view of its model, given as "decoded".
            Loader {
                id: decodedView
                objectName: "fileViewerDecoded"
                active: viewer.shownMode === "decoded"
                // A new model is a new view: the old one may still hold the
                // model that is about to go.
                onActiveChanged: load()
                Connections {
                    target: viewer.content
                    function onChanged() { decodedView.load() }
                }
                function load() {
                    if (!active || !viewer.content || !viewer.content.decoded) {
                        setSource("")
                        return
                    }
                    if (item && item.decoded === viewer.content.decoded)
                        return
                    setSource(viewer.content.codecView, { decoded: viewer.content.decoded })
                }
            }
        }

        // How big, when it was read, what it is, and where it came from.
        Label {
            objectName: "fileViewerFooter"
            visible: viewer.ready
            Layout.fillWidth: true
            Layout.margins: 8
            elide: Text.ElideMiddle
            font.pointSize: Type.caption
            color: Theme.textSecondary
            // The path is the device's text: it goes in last, under the highest number.
            text: !viewer.ready ? ""
                : qsTr("%1 · read %2 · %3 · %4")
                    .arg(viewer.content.sizeText)
                    .arg(viewer.content.readAt.toLocaleTimeString(Qt.locale(), Locale.LongFormat))
                    .arg(viewer.content.format)
                    .arg(viewer.content.devicePath.length > 0 ? viewer.content.devicePath : viewer.content.localFile)
        }
    }
}
