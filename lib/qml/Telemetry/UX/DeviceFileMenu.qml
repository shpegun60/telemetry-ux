// The Device files tree's context menu, as in a file explorer: on a file,
// Open first (in bold, the double-click), Open with, Download, Copy path and
// name, Add to or Remove from the selection, Properties; on a folder, on a
// selection, and on empty space its own items. A disabled item keeps its text
// and says why in brackets. An item hidden for this kind is disabled too: the
// arrow keys do not skip a hidden item, and Enter would run it. MIT.
//
// The panel sets `kind` ("file", "folder", "selection", "empty") and `file`
// (the model's fileAt(index), or the folder's path) before popup().
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    objectName: "deviceFileMenu"

    required property var panel
    property string kind: "file"
    property var file: ({})
    property var index
    readonly property var files: panel.files
    readonly property bool connected: files.available
    readonly property bool readable: kind === "file" && file.readable !== false
    readonly property string why: !connected ? qsTr(" (not connected)") : !readable ? qsTr(" (not readable)") : ""
    readonly property bool csv: kind === "file" && String(file.path || "").toLowerCase().endsWith(".csv")

    // On a file.
    MenuItem {
        objectName: "deviceFileMenuOpen"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("&Open%1\tEnter").arg(menu.why)
        font.bold: true
        enabled: menu.kind === "file" && menu.connected && menu.readable
        onTriggered: menu.panel.openRequested(menu.file)
    }
    Menu {
        id: openWith
        objectName: "deviceFileMenuOpenWith"
        title: qsTr("Open &with")
        enabled: menu.kind === "file" && menu.connected && menu.readable
        MenuItem {
            objectName: "deviceFileMenuOpenTable"
            visible: menu.csv
            enabled: menu.csv
            height: visible ? implicitHeight : 0
            text: qsTr("&Table")
            onTriggered: menu.panel.openWithRequested(menu.file, "table")
        }
        MenuItem {
            objectName: "deviceFileMenuOpenText"
            text: qsTr("Te&xt")
            onTriggered: menu.panel.openWithRequested(menu.file, "text")
        }
        MenuItem {
            objectName: "deviceFileMenuOpenHex"
            text: qsTr("&Hex")
            onTriggered: menu.panel.openWithRequested(menu.file, "hex")
        }
        MenuSeparator {}
        MenuItem {
            objectName: "deviceFileMenuOpenExternal"
            text: qsTr("&Another program")
            onTriggered: menu.panel.openWithRequested(menu.file, "external")
        }
    }
    MenuSeparator { visible: menu.kind === "file"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuDownload"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("&Download…%1\tCtrl+S").arg(menu.why)
        enabled: menu.kind === "file" && menu.connected && menu.readable
        onTriggered: menu.panel.downloadRequested([menu.file])
    }

    // On a folder.
    MenuItem {
        objectName: "deviceFileMenuExpand"
        visible: menu.kind === "folder"
        height: visible ? implicitHeight : 0
        text: menu.panel.isExpanded(menu.index) ? qsTr("&Collapse\t←") : qsTr("&Expand\t→")
        enabled: menu.kind === "folder"
        onTriggered: menu.panel.toggleFolder(menu.index)
    }
    MenuSeparator { visible: menu.kind === "folder"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuDownloadFolder"
        visible: menu.kind === "folder"
        height: visible ? implicitHeight : 0
        readonly property var inside: menu.kind === "folder" ? menu.files.filesUnder(menu.index) : []
        text: qsTr("&Download folder…%1\tCtrl+S").arg(inside.length === 0 ? qsTr(" (no readable files)") : "")
        enabled: menu.connected && inside.length > 0
        onTriggered: menu.panel.downloadRequested(inside)
    }
    MenuItem {
        objectName: "deviceFileMenuSelectFolder"
        visible: menu.kind === "folder"
        height: visible ? implicitHeight : 0
        // The device's name, its & a letter rather than an access key.
        text: qsTr("&Select the files in %1\tSpace").arg(
                  menu.kind === "folder" ? String(menu.files.data(menu.index, Qt.DisplayRole)).replace(/&/g, "&&") : "")
        enabled: menu.kind === "folder"
        onTriggered: menu.files.select(menu.index, true)
    }

    // On a selection of two or more.
    MenuItem {
        objectName: "deviceFileMenuDownloadSelected"
        visible: menu.kind === "selection"
        height: visible ? implicitHeight : 0
        readonly property int readable: menu.files.selectedReadable
        readonly property int skipped: menu.files.selectedCount - readable
        text: (skipped > 0 ? qsTr("&Download %n selected files… (%1 can't be read)\tCtrl+S", "", readable).arg(skipped)
                           : qsTr("&Download %n selected files…\tCtrl+S", "", readable))
        enabled: menu.kind === "selection" && menu.connected && readable > 0
        onTriggered: menu.panel.download()
    }
    MenuItem {
        objectName: "deviceFileMenuSelectNone"
        visible: menu.kind === "selection" || menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("Select &none\tEsc")
        enabled: (menu.kind === "selection" || menu.kind === "empty") && menu.files.selectedCount > 0
        onTriggered: menu.files.clearSelection()
    }

    // On empty space below the rows.
    MenuItem {
        objectName: "deviceFileMenuRefresh"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("&Refresh\tCtrl+R")
        enabled: menu.kind === "empty" && menu.connected && !menu.files.loading
        onTriggered: menu.files.refresh()
    }
    MenuItem {
        objectName: "deviceFileMenuDownloadAll"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        readonly property var everything: menu.kind === "empty" ? menu.panel.allReadable() : []
        text: qsTr("Download &all…") + (everything.length === 0 ? qsTr(" (no readable files)") : "")
        enabled: menu.kind === "empty" && menu.connected && everything.length > 0
        onTriggered: menu.panel.downloadRequested(everything)
    }
    MenuItem {
        objectName: "deviceFileMenuSelectAll"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("Select &all files\tCtrl+A")
        enabled: menu.kind === "empty"
        onTriggered: menu.files.selectAll()
    }

    // Copy, on a file, a folder or a selection.
    MenuSeparator { visible: menu.kind !== "empty"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuCopyPath"
        visible: menu.kind !== "empty"
        height: visible ? implicitHeight : 0
        text: menu.kind === "selection" ? qsTr("Copy &paths\tAlt+Shift+C") : qsTr("Copy &path\tAlt+Shift+C")
        enabled: menu.kind !== "empty"
        onTriggered: menu.panel.copyPaths(menu.kind, menu.file)
    }
    MenuItem {
        objectName: "deviceFileMenuCopyName"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("Copy &name")
        enabled: menu.kind === "file"
        onTriggered: Clipboard.setText(menu.file.name)
    }

    // In or out of the selection, and Properties, on a file.
    MenuSeparator { visible: menu.kind === "file"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuSelect"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        readonly property bool selected: menu.kind === "file" && menu.index !== undefined
                                         && menu.files.selectedCount >= 0 && menu.files.isSelected(menu.index)
        text: selected ? qsTr("&Remove from the selection\tCtrl+Space") : qsTr("&Add to the selection\tCtrl+Space")
        enabled: menu.kind === "file"
        onTriggered: menu.files.toggleSelected(menu.index)
    }
    MenuItem {
        objectName: "deviceFileMenuProperties"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("P&roperties\tAlt+Enter")
        enabled: menu.kind === "file"
        onTriggered: menu.panel.showProperties(menu.index)
    }

    onClosed: menu.panel.returnFocus()
}
