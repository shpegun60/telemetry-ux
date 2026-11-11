// One file in the viewer: a table when it is CSV, text when it is text, hex
// always, with a switch between the ones it has. Read-only. MIT.
//
// The host gives `content`, a FileContent - usually a row of OpenFiles, whose
// role of that name fills it in a view's delegate - and answers the requests:
// reloadRequested() to read the device's file again, openExternallyRequested
// and showInFolderRequested with the local file, closeRequested.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: viewer
    objectName: "fileViewer"
    padding: 0

    required property var content
    // The host can read the device's file again (connected, the file listed).
    property bool reloadable: false
    // "table", "text" or "hex"; empty for the format's own first choice.
    property string mode: ""
    readonly property string shownMode: !content ? "" : offers(mode) ? mode
        : content.tabular ? "table" : content.textual ? "text" : "hex"
    readonly property bool ready: !!content && content.loaded
    readonly property string problem: content ? content.error : ""

    signal reloadRequested()
    signal openExternallyRequested(string localFile)
    signal showInFolderRequested(string localFile)
    signal closeRequested()

    function offers(name) {
        if (!content)
            return false
        return name === "hex" ? content.loaded
             : name === "text" ? content.textual
             : name === "table" ? content.tabular : false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            Repeater {
                model: [
                    { name: "table", text: qsTr("Table") },
                    { name: "text", text: qsTr("Text") },
                    { name: "hex", text: qsTr("Hex") }
                ]
                delegate: Button {
                    required property var modelData
                    objectName: "fileViewerMode_" + modelData.name
                    visible: viewer.offers(modelData.name)
                    text: modelData.text
                    flat: viewer.shownMode !== modelData.name
                    highlighted: viewer.shownMode === modelData.name
                    Accessible.checkable: true
                    Accessible.checked: viewer.shownMode === modelData.name
                    onClicked: viewer.mode = modelData.name
                }
            }
            Item { Layout.fillWidth: true }
            LinkButton {
                objectName: "fileViewerReload"
                visible: viewer.reloadable
                tint: Theme.accent
                text: qsTr("Reload")
                onClicked: viewer.reloadRequested()
            }
            LinkButton {
                objectName: "fileViewerExternal"
                visible: viewer.ready
                tint: Theme.accent
                text: qsTr("Open in another program")
                onClicked: viewer.openExternallyRequested(viewer.content.localFile)
            }
            LinkButton {
                objectName: "fileViewerShow"
                visible: viewer.ready
                tint: Theme.accent
                text: qsTr("Show in folder")
                onClicked: viewer.showInFolderRequested(viewer.content.localFile)
            }
            LinkButton {
                objectName: "fileViewerClose"
                tint: Theme.accent
                text: qsTr("Close")
                onClicked: viewer.closeRequested()
            }
        }

        Label {
            objectName: "fileViewerError"
            visible: viewer.problem.length > 0
            Layout.fillWidth: true
            Layout.margins: 8
            wrapMode: Text.Wrap
            color: Theme.error
            text: viewer.problem
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: viewer.ready
            currentIndex: viewer.shownMode === "table" ? 0 : viewer.shownMode === "text" ? 1 : 2

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
                    model: viewer.shownMode === "table" ? content.table : null
                    delegate: Rectangle {
                        required property string display
                        required property int row
                        required property int column
                        implicitWidth: Math.min(360, Math.max(72, cell.implicitWidth + 16))
                        implicitHeight: Type.rowHeight
                        color: row % 2 ? Theme.surfaceAlternate : Theme.surface
                        Label {
                            id: cell
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: content.table.numeric(parent.column) ? Text.AlignRight : Text.AlignLeft
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                            text: parent.display
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
                    objectName: "fileViewerText"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    font.family: Type.fixedFamily
                    color: Theme.textPrimary
                    text: viewer.shownMode === "text" ? content.text : ""
                }
            }

            // Hex: offset, 16 bytes, their ASCII.
            ListView {
                id: hexView
                objectName: "fileViewerHex"
                clip: true
                model: viewer.shownMode === "hex" ? content.hex : null
                delegate: Row {
                    required property string offset
                    required property string bytes
                    required property string ascii
                    leftPadding: 8
                    spacing: 16
                    Label { font.family: Type.fixedFamily; color: Theme.textSecondary; text: parent.offset }
                    Label { font.family: Type.fixedFamily; color: Theme.textPrimary; text: parent.bytes }
                    Label { font.family: Type.fixedFamily; color: Theme.textSecondary; text: parent.ascii }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }

        // Where it came from, how big, when it was read, and what it is.
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
                    .arg(Qt.formatTime(viewer.content.readAt, "hh:mm:ss"))
                    .arg(viewer.content.format)
                    .arg(viewer.content.devicePath.length > 0 ? viewer.content.devicePath : viewer.content.localFile)
        }
    }
}
