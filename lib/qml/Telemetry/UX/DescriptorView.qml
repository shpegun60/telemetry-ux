// The descriptor codec's view: which descriptor this is (its fingerprint, with
// Copy), how much it describes, and every endpoint as a tree - category, group,
// endpoint, a struct's members - with its type and access. Find narrows the
// tree to the matches and their parents. Copy takes a node's path, its value
// line, or its whole subtree as indented text, JSON or TSV. MIT.
//
// FileViewer loads it with `decoded`, a DescriptorContent.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

FocusScope {
    id: view
    objectName: "descriptorView"

    required property var decoded

    readonly property real typeWidth: Math.min(220, Math.max(120, width * 0.3))
    readonly property real accessWidth: 120
    readonly property bool accessShown: width >= 480
    readonly property var currentIndex: tree.selectionModel.currentIndex

    function focusContent() { tree.forceActiveFocus() }
    // Copies the current node (or the whole tree) in a format: "text", "json", "tsv".
    function copyAs(format, whole) {
        const text = view.decoded.textOf(whole ? null : view.currentIndex, format)
        if (text.length > 0)
            Clipboard.setText(text)
    }
    function openMenu(index, from, x, y) {
        if (index && index.valid)
            tree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        nodeMenu.popup(from, x, y)
    }

    // A node's menu: its path and line, its subtree in three forms.
    Menu {
        id: nodeMenu
        objectName: "descriptorMenu"
        MenuItem {
            objectName: "descriptorCopyPath"
            text: qsTr("Copy &path\tAlt+Shift+C")
            enabled: !!view.currentIndex && view.currentIndex.valid
            onTriggered: Clipboard.setText(view.decoded.pathOf(view.currentIndex))
        }
        MenuItem {
            objectName: "descriptorCopyRow"
            text: qsTr("Copy &row")
            enabled: !!view.currentIndex && view.currentIndex.valid
            onTriggered: Clipboard.setText(view.decoded.rowOf(view.currentIndex))
        }
        Menu {
            title: qsTr("Copy &as")
            MenuItem { text: qsTr("&Text"); onTriggered: view.copyAs("text", false) }
            MenuItem { text: qsTr("&JSON"); onTriggered: view.copyAs("json", false) }
            MenuItem { text: qsTr("T&SV"); onTriggered: view.copyAs("tsv", false) }
        }
        MenuSeparator {}
        Menu {
            title: view.findText.length > 0 ? qsTr("Copy shown a&s") : qsTr("Copy a&ll as")
            MenuItem { text: qsTr("&Text"); onTriggered: view.copyAs("text", true) }
            MenuItem { text: qsTr("&JSON"); onTriggered: view.copyAs("json", true) }
            MenuItem { text: qsTr("T&SV"); onTriggered: view.copyAs("tsv", true) }
        }
        onClosed: tree.forceActiveFocus()
    }
    property alias findText: findField.text

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // Which descriptor: its fingerprint, in groups of four, with Copy.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Label {
                text: qsTr("Descriptor")
                color: Theme.textPrimary
                font.weight: Type.headingWeight
            }
            Label {
                text: qsTr("fingerprint")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
            Label {
                objectName: "descriptorFingerprint"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.family: Type.fixedFamily
                color: Theme.textSecondary
                text: view.decoded.fingerprintGroups
                HoverHandler { id: fingerprintHover }
                ToolTip.visible: fingerprintHover.hovered
                ToolTip.text: qsTr("A 64-bit number, stored little-endian at 0x0010")
            }
            LinkButton {
                objectName: "descriptorCopyFingerprint"
                tint: Theme.textSecondary
                text: qsTr("Copy")
                Accessible.name: qsTr("Copy the fingerprint")
                onClicked: Clipboard.setText(view.decoded.fingerprint)
            }
        }
        Label {
            objectName: "descriptorSummary"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: Theme.textSecondary
            text: view.decoded.summary
        }

        // Find: the matches and their parents, the rest folded away.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            TextField {
                id: findField
                objectName: "descriptorFind"
                Layout.fillWidth: true
                Layout.maximumWidth: 320
                placeholderText: qsTr("Find by name or path (Ctrl+F)")
                Accessible.name: qsTr("Find by name or path")
                onTextChanged: {
                    matches.text = text.length === 0 ? "" : qsTr("%n matches", "", view.decoded.setFilter(text))
                    if (text.length === 0)
                        view.decoded.setFilter("")
                    Qt.callLater(function() { tree.expandRecursively() })
                }
                Keys.onEscapePressed: { text = ""; tree.forceActiveFocus() }
            }
            Label {
                id: matches
                objectName: "descriptorMatches"
                color: Theme.textSecondary
                font.pointSize: Type.caption
            }
            Item { Layout.fillWidth: true }
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Label { Layout.fillWidth: true; text: qsTr("Name"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Label { Layout.preferredWidth: view.typeWidth; text: qsTr("Type"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Label {
                visible: view.accessShown
                Layout.preferredWidth: view.accessWidth
                text: qsTr("Access")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
        }

        TreeView {
            id: tree
            objectName: "descriptorTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.decoded.endpoints
            activeFocusOnTab: true
            focus: true
            selectionModel: ItemSelectionModel { model: tree.model }
            columnWidthProvider: function() { return Math.max(1, tree.width) }
            onWidthChanged: Qt.callLater(function() { if (tree) tree.forceLayout() })
            Keys.onPressed: function(event) {
                const control = event.modifiers & Qt.ControlModifier
                const shift = event.modifiers & Qt.ShiftModifier
                const alt = event.modifiers & Qt.AltModifier
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && shift)) {
                    const item = tree.itemAtCell(Qt.point(0, tree.currentRow))
                    view.openMenu(view.currentIndex, item ? item : tree, 16, item ? item.height : 16)
                } else if (control && shift && event.key === Qt.Key_C) {
                    view.copyAs("text", false)
                } else if (alt && shift && event.key === Qt.Key_C) {
                    Clipboard.setText(view.decoded.pathOf(view.currentIndex))
                } else if (control && event.key === Qt.Key_C) {
                    // A leaf: its line; a container: its subtree as text.
                    view.copyAs("text", false)
                } else if (control && event.key === Qt.Key_F) {
                    findField.forceActiveFocus()
                } else {
                    return
                }
                event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                event.accepted = tree.currentRow >= 0 && !tree.isExpanded(tree.currentRow)
                if (event.accepted)
                    tree.expand(tree.currentRow)
            }
            Keys.onLeftPressed: function(event) {
                event.accepted = tree.currentRow >= 0 && tree.isExpanded(tree.currentRow)
                if (event.accepted)
                    tree.collapse(tree.currentRow)
            }
            delegate: DescriptorRow {
                typeWidth: view.typeWidth
                accessWidth: view.accessWidth
                accessShown: view.accessShown
                onContextMenuRequested: function(index, x, y) { view.openMenu(index, this, x, y) }
            }
            ScrollBar.vertical: ScrollBar {}
            // Categories and groups open, members folded: what a descriptor
            // has, without every struct spilled out.
            Component.onCompleted: Qt.callLater(function() {
                if (!tree)
                    return
                for (let row = tree.rows - 1; row >= 0; --row)
                    if (tree.depth(row) === 0)
                        tree.expand(row)
            })
        }
    }
}
