// One file in the viewer: its format's own view when a codec read it, a table
// when it is CSV, text when it is text, hex always, with a switch between the
// ones it has. Read-only. MIT.
//
// The host gives `content`, a FileContent - usually a row of OpenFiles, whose
// role of that name fills it in a view's delegate - and answers the requests:
// reloadRequested() to read the device's file again, cancelRequested() while
// it is being read, openExternallyRequested, showInFolderRequested and
// saveAsRequested with the local file, closeRequested. The file's actions are
// in its `⋯` menu (and the host's tab menu), not in a row of buttons.
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
    signal saveAsRequested(string localFile)
    signal closeRequested()

    // The device's path, or the local file's for one from disk.
    readonly property string shownPath: !content ? ""
        : content.devicePath.length > 0 ? content.devicePath : content.localFile
    function openMenu() { moreMenu.popup(moreButton, 0, moreButton.height) }

    // The keyboard into the file: the table, the hex rows or the text, or
    // Cancel while it is being read.
    function focusContent() {
        if (reading)
            cancelLink.forceActiveFocus()
        else if (shownMode === "table")
            table.forceActiveFocus()
        else if (shownMode === "hex")
            hexPane.focusContent()
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
            // The file's actions, as the tab's menu has them.
            ToolButton {
                id: moreButton
                objectName: "fileViewerMore"
                flat: true
                implicitWidth: Math.max(32, implicitContentWidth + 16)
                font.pointSize: Type.body * 1.25
                text: qsTr("⋯")
                Accessible.name: qsTr("More actions")
                Accessible.description: viewer.fileName
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("More actions")
                onClicked: viewer.openMenu()
            }
        }

        Menu {
            id: moreMenu
            objectName: "fileViewerMenu"
            MenuItem {
                objectName: "fileViewerReload"
                text: qsTr("&Reload%1\tCtrl+R").arg(viewer.reloadable ? "" : qsTr(" (not connected)"))
                enabled: viewer.reloadable && !viewer.reading
                onTriggered: viewer.reloadRequested()
            }
            MenuItem {
                objectName: "fileViewerSaveAs"
                text: qsTr("&Save as…\tCtrl+Shift+S")
                enabled: viewer.ready
                onTriggered: viewer.saveAsRequested(viewer.content.localFile)
            }
            MenuSeparator {}
            MenuItem {
                objectName: "fileViewerCopyPath"
                text: qsTr("Copy &path\tAlt+Shift+C")
                enabled: viewer.shownPath.length > 0
                onTriggered: Clipboard.setText(viewer.shownPath)
            }
            MenuItem {
                objectName: "fileViewerShow"
                text: qsTr("Show in &folder\tAlt+Shift+R")
                enabled: viewer.ready
                onTriggered: viewer.showInFolderRequested(viewer.content.localFile)
            }
            MenuItem {
                objectName: "fileViewerExternal"
                text: qsTr("Open in another pro&gram")
                enabled: viewer.ready
                onTriggered: viewer.openExternallyRequested(viewer.content.localFile)
            }
            MenuSeparator {}
            MenuItem {
                objectName: "fileViewerClose"
                text: qsTr("&Close\tCtrl+W")
                onTriggered: viewer.closeRequested()
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

            // Hex: the painted grid, its bars, the status line and the inspector.
            HexPane {
                id: hexPane
                content: viewer.content
                // A codec's parts of the file, tinted and named in the hex view;
                // bound to the model's own property, which says when they change.
                regions: viewer.content && viewer.content.decoded && viewer.content.decoded.regions
                         ? viewer.content.decoded.regions : []
            }

            // The codec's own view of its model, given as "decoded", and the
            // file as "content" (a view that reads it again uses its `live`).
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
                    setSource(viewer.content.codecView, { decoded: viewer.content.decoded, content: viewer.content })
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
