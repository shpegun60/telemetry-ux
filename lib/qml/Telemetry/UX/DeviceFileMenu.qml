// The Device files tree's context menu, as in a file explorer: on a file,
// Open first (in bold, the double-click), Open with, Download, Copy path and
// name, Mark, Properties; on a folder, on a marked set, and on empty space its
// own items. A disabled item keeps its text and says why in brackets. MIT.
//
// The panel sets `kind` ("file", "folder", "marked", "empty") and `file` (the
// model's fileAt(index), or the folder's path) before popup().
import QtQuick
import QtQuick.Controls
import Telemetry.UX

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
        enabled: menu.connected && menu.readable
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
        enabled: menu.connected && menu.readable
        onTriggered: menu.panel.downloadRequested([menu.file])
    }

    // On a folder.
    MenuItem {
        objectName: "deviceFileMenuExpand"
        visible: menu.kind === "folder"
        height: visible ? implicitHeight : 0
        text: menu.panel.isExpanded(menu.index) ? qsTr("&Collapse\t←") : qsTr("&Expand\t→")
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
        objectName: "deviceFileMenuMarkFolder"
        visible: menu.kind === "folder"
        height: visible ? implicitHeight : 0
        text: qsTr("&Mark all files in folder\tSpace")
        onTriggered: menu.files.setMarked(menu.index, true)
    }

    // On a marked set.
    MenuItem {
        objectName: "deviceFileMenuDownloadMarked"
        visible: menu.kind === "marked"
        height: visible ? implicitHeight : 0
        text: qsTr("&Download %n marked files…\tCtrl+S", "", menu.files.markedCount)
        enabled: menu.connected
        onTriggered: menu.panel.download()
    }
    MenuItem {
        objectName: "deviceFileMenuClearMarks"
        visible: menu.kind === "marked" || menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("&Clear marks\tCtrl+Shift+A")
        enabled: menu.files.markedCount > 0
        onTriggered: menu.files.markAll(false)
    }

    // On empty space below the rows.
    MenuItem {
        objectName: "deviceFileMenuRefresh"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("&Refresh\tCtrl+R")
        enabled: menu.connected && !menu.files.loading
        onTriggered: menu.files.refresh()
    }
    MenuItem {
        objectName: "deviceFileMenuDownloadAll"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        readonly property var everything: menu.kind === "empty" ? menu.panel.allReadable() : []
        text: qsTr("Download &all…") + (everything.length === 0 ? qsTr(" (no readable files)") : "")
        enabled: menu.connected && everything.length > 0
        onTriggered: menu.panel.downloadRequested(everything)
    }
    MenuItem {
        objectName: "deviceFileMenuMarkAll"
        visible: menu.kind === "empty"
        height: visible ? implicitHeight : 0
        text: qsTr("&Mark all\tCtrl+A")
        onTriggered: menu.files.markAll(true)
    }

    // Copy, on a file, a folder or a marked set.
    MenuSeparator { visible: menu.kind !== "empty"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuCopyPath"
        visible: menu.kind !== "empty"
        height: visible ? implicitHeight : 0
        text: menu.kind === "marked" ? qsTr("Copy &paths\tAlt+Shift+C") : qsTr("Copy &path\tAlt+Shift+C")
        onTriggered: menu.panel.copyPaths(menu.kind, menu.file)
    }
    MenuItem {
        objectName: "deviceFileMenuCopyName"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("Copy &name")
        onTriggered: Clipboard.setText(menu.file.name)
    }

    // Mark and Properties, on a file.
    MenuSeparator { visible: menu.kind === "file"; height: visible ? implicitHeight : 0 }
    MenuItem {
        objectName: "deviceFileMenuMark"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("&Mark for download%1\tSpace").arg(menu.readable ? "" : qsTr(" (not readable)"))
        checkable: true
        checked: menu.kind === "file" && menu.index !== undefined && menu.files.isMarked(menu.index)
        enabled: menu.readable
        onTriggered: menu.files.setMarked(menu.index, checked)
    }
    MenuItem {
        objectName: "deviceFileMenuProperties"
        visible: menu.kind === "file"
        height: visible ? implicitHeight : 0
        text: qsTr("P&roperties\tAlt+Enter")
        onTriggered: menu.panel.showProperties(menu.index)
    }

    onClosed: menu.panel.returnFocus()
}
