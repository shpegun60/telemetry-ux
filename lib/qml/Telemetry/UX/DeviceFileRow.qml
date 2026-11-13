// One row of the Device files tree: an icon for what the file is, the
// device's own name, the size, what the file is in words (from 360 px) and
// three letters for what may be done with it - read, write, and whether the
// device keeps what is written across a restart - in fixed columns. Only the files
// the program itself uses have a coloured icon. A selected file is
// highlighted - fully while the tree has the keyboard, quietly while it does
// not. A folder is never highlighted: it stands for its files and shows how
// many of them are selected. A file the device does not let us read is
// dimmed, and its letters say why. The current row is framed while the tree
// has the keyboard. A click is the panel's to read with its keys (Ctrl,
// Shift). MIT.
//
// The Basic style is imported here only, where the row customises its
// controls; the panel keeps the style the host chose.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: fileRow
    // One grid: a level's indent is an icon and its gap, so a child's
    // chevron stands under its parent's icon and its icon under the name.
    indentation: Type.iconSize + Type.spacing
    spacing: Type.spacing
    // The panel has no padding: the margin lines the chevrons up with the
    // Device tree's.
    leftMargin: Type.padding
    indicator: TreeChevron { row: fileRow }
    objectName: "deviceFileRow"

    required property string name
    required property bool isFolder
    required property bool downloadable
    required property string sizeText
    required property string spokenSize
    required property bool isSelected
    required property int selectedInside
    required property int readableInside
    required property int fileCount
    required property string kind
    required property string kindText
    required property bool programFile
    required property string accessMark
    required property string accessWords
    required property string statError

    // The panel's column widths, so every row and the header line up.
    property real sizeWidth: 72
    property real kindWidth: 72
    property real markWidth: 16
    property bool kindShown: true

    // The device's index of this row, whatever is current or selected.
    readonly property var sourceIndex: treeView.modelIndex(Qt.point(0, row))
    readonly property bool focused: treeView.activeFocus
    readonly property bool strong: highlighted && focused
    readonly property bool unreadable: !isFolder && !downloadable
    // A dimmed row stays legible on a selection: textDisabled would not be.
    readonly property color textColor: unreadable ? (highlighted ? Theme.textSecondary : Theme.textDisabled)
                                     : strong ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: unreadable ? (highlighted ? Theme.textSecondary : Theme.textDisabled)
                                       : highlighted ? (focused ? Theme.selectedForeground : Theme.textPrimary)
                                       : Theme.textSecondary
    readonly property color iconColor: unreadable ? (strong ? Theme.textSecondary : Theme.textDisabled)
                                     : programFile ? Theme.fileProgram
                                     : strong ? Theme.selectedForeground : Theme.textSecondary
    readonly property string unreadableText: qsTr("Can't be read from the device, so it can't be downloaded")
    readonly property int unreadableInside: fileCount - readableInside
    // "3 of 6 files selected", and why a folder's click left some out.
    readonly property string insideText: (unreadableInside > 0
            ? qsTr("%1 of %2 files selected (%3 can't be read)").arg(selectedInside).arg(fileCount).arg(unreadableInside)
            : qsTr("%1 of %2 files selected").arg(selectedInside).arg(fileCount))
    // What the icon says, in words.
    readonly property string kindTip: {
        switch (kind) {
        case "folder": return fileCount === 1 ? qsTr("Folder, 1 file") : qsTr("Folder, %L1 files").arg(fileCount)
        case "descriptor": return qsTr("Descriptor. The program reads it to learn the device's Fields, Commands and Services.")
        case "values": return qsTr("Values. Every Field's value; Live reads it again.")
        case "identity": return qsTr("Identity. The device's name and ID; Rename writes it.")
        case "session": return qsTr("Session. The protocol's state; the device usually doesn't let it be read.")
        case "capability": return qsTr("Capability. Tells the program how it can talk to the device.")
        case "json": return qsTr("JSON. Opens as a tree.")
        case "table": return qsTr("CSV. Opens as a table.")
        case "text": return qsTr("Text. Opens as text.")
        case "codec": return qsTr("%1. Opens with %1.").arg(kindText)
        default: return qsTr("Binary. Opens in Hex.")
        }
    }
    readonly property string iconName: {
        switch (kind) {
        case "folder": return "folder"
        case "descriptor": return "fileDescriptor"
        case "values": return "fileValues"
        case "identity": return "fileIdentity"
        case "session": return "fileSession"
        case "capability": return "fileCapability"
        case "json": return "fileJson"
        case "table": return "fileTable"
        case "text": return "fileText"
        case "codec": return "fileCodec"
        default: return "file"
        }
    }
    // The three letters in words: for the tooltip and a screen reader.
    readonly property string accessTip: {
        switch (accessMark) {
        case "rws": return qsTr("Read and write. You can open, download and write it; the device keeps what is written across a restart.")
        case "rw-": return qsTr("Read and write. You can open, download and write it; what is written is kept until the device restarts.")
        case "-ws": return qsTr("Write only. The device keeps what is written across a restart, but doesn't let it be read, so it can't be opened or downloaded.")
        case "-w-": return qsTr("Write only. What is written is kept until the device restarts; the device doesn't let it be read, so it can't be opened or downloaded.")
        case "r--": return qsTr("Read only. You can open and download it; the device doesn't accept writes.")
        case "---": return qsTr("No access. The device lets it be neither read nor written.")
        case "?": return qsTr("Access not known: %1.").arg(statError)
        default: return qsTr("Access not known yet.")
        }
    }

    // A left click, with the keys held: the panel selects from it.
    signal rowClicked(var index, int row, int modifiers)
    signal openRequested(var index)
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: Type.padding
    text: name
    highlighted: !isFolder && isSelected
    palette.highlight: focused ? Theme.stateSelected : Theme.stateSelectedInactive

    // The device's name goes in last, under the highest number, so nothing in
    // it is ever read as a placeholder.
    Accessible.name: isFolder ? qsTr("%2, folder, %1").arg(spokenSize).arg(name)
                     : programFile ? qsTr("%4, %1, used by the program, %2, %3").arg(kindText).arg(spokenSize).arg(accessWords).arg(name)
                     : qsTr("%4, %1, %2, %3").arg(kindText).arg(spokenSize).arg(accessWords).arg(name)
    Accessible.description: unreadable ? unreadableText : isFolder && selectedInside > 0 ? insideText : ""
    Accessible.selectable: !isFolder
    Accessible.selected: highlighted

    contentItem: RowLayout {
        spacing: Type.padding

        // What the file is - coloured only for the program's own files - and
        // the device's own name, never replaced; the grid's gap between them,
        // tighter than the columns'.
        RowLayout {
            Layout.fillWidth: true
            spacing: Type.spacing
            ToolIcon {
                objectName: "deviceFileIcon"
                Layout.preferredWidth: Type.iconSize
                Layout.preferredHeight: Type.iconSize
                name: fileRow.iconName
                color: fileRow.iconColor
                HoverHandler { id: iconHover }
                ToolTip.visible: iconHover.hovered
                ToolTip.delay: 300
                ToolTip.text: fileRow.kindTip
                Accessible.ignored: true
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                text: fileRow.name
                color: fileRow.textColor
            }
        }
        // A folder holding selected files: how many, in a pill in the
        // selection's own fill - quiet with it while the tree lacks the
        // keyboard; a capsule, never narrower than tall, from two digits.
        Label {
            id: inside
            objectName: "deviceFileSelectedInside"
            visible: fileRow.isFolder && fileRow.selectedInside > 0
            Layout.minimumWidth: implicitHeight
            leftPadding: 4
            rightPadding: 4
            horizontalAlignment: Text.AlignHCenter
            font.pointSize: Type.caption
            color: fileRow.focused ? Theme.selectedForeground : Theme.textPrimary
            text: qsTr("%L1").arg(fileRow.selectedInside)
            background: Rectangle {
                radius: height / 2
                color: fileRow.focused ? Theme.stateSelected : Theme.stateSelectedInactive
            }
            HoverHandler { id: insideHover }
            ToolTip.visible: insideHover.hovered
            ToolTip.delay: 300
            ToolTip.text: fileRow.insideText
            Accessible.ignored: true
        }
        Label {
            objectName: "deviceFileSize"
            Layout.preferredWidth: fileRow.sizeWidth
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            font.pointSize: Type.caption
            color: fileRow.detailColor
            text: fileRow.sizeText
        }
        // What the icon says, in a word; a folder's file count.
        Label {
            objectName: "deviceFileKind"
            visible: fileRow.kindShown
            Layout.preferredWidth: fileRow.kindWidth
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: fileRow.detailColor
            text: fileRow.kindText
        }
        // Read, write and kept as ls writes them, one letter a slot: "r--"
        // says "and not writable" by its shape. Letters weigh more than
        // dashes; on a file that cannot be read all keep their weight - they
        // are why the row is dimmed. Three fixed labels, so a row reused
        // while scrolling makes none. A gutter sets the letters apart from
        // the size; a folder keeps the column empty, so its size lines up.
        Row {
            objectName: "deviceFileAccess"
            Layout.preferredWidth: fileRow.markWidth
            Layout.leftMargin: Type.padding
            DeviceFileAccessLetter { row: fileRow; letter: fileRow.accessMark.charAt(0) }
            DeviceFileAccessLetter { row: fileRow; letter: fileRow.accessMark.charAt(1) }
            DeviceFileAccessLetter { row: fileRow; letter: fileRow.accessMark.charAt(2) }
            HoverHandler { id: accessHover }
            ToolTip.visible: accessHover.hovered && !fileRow.isFolder
            ToolTip.delay: 300
            ToolTip.text: fileRow.accessTip
        }
    }

    // Inset a pixel, so a selected neighbour never swallows its edges.
    Rectangle {
        objectName: "deviceFileFocusFrame"
        anchors.fill: parent
        anchors.margins: 1
        color: "transparent"
        radius: 3
        border.width: 2
        border.color: Theme.focusRing
        visible: fileRow.focused && fileRow.current
    }

    onDoubleClicked: if (!isFolder) fileRow.openRequested(sourceIndex)
    // The handler's own point carries the keys held; the tapped argument
    // does not.
    TapHandler {
        id: leftTap
        acceptedButtons: Qt.LeftButton
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, leftTap.point.modifiers)
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) {
            fileRow.contextMenuRequested(fileRow.sourceIndex, point.position.x, point.position.y)
        }
    }
}
