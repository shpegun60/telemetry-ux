// The transfers of a device's files: each download, open and write with its
// progress, how it ended and what can be done next - Cancel, Retry, Write
// anyway, Show in folder. Device files shows it under its list; a host may
// give it a dock of its own instead (fill: true). MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: transfersPane
    objectName: "deviceTransfersPane"
    padding: 0

    required property var transfers
    property bool reducedMotion: false
    // In a dock of its own: the whole height, and a line when there is nothing.
    property bool fill: false
    readonly property int count: transferList.count

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
    readonly property int finishedTransfers: transfers.rowCount() - transfers.activeCount
    // In a dock the dock's title names it: the bar holds only the actions,
    // shown while one applies; under the file list it carries its own title.
    readonly property bool actionsShown: !fill || transfers.activeCount >= 2 || finishedTransfers > 0

    // Under the file list, at most four rows; in a dock, whatever it is given.
    // The bar is a row high in the layout whatever it holds: counting its
    // content's height (a caption, with nothing but the title) cut the row
    // under it at the foot, where its progress runs.
    implicitHeight: fill ? 120 : (actionsShown ? Type.rowHeight : 0)
                                 + Math.min(transferList.contentHeight, 4 * Type.rowHeight)
    background: Item {}

    Label {
        objectName: "deviceTransfersEmpty"
        anchors.centerIn: parent
        visible: transfersPane.fill && transferList.count === 0
        font.pointSize: Type.caption
        color: Theme.textSecondary
        text: qsTr("No transfers yet. Downloads and writes show here.")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The actions stay put above the rows, however far they scroll.
        RowLayout {
            id: actionRow
            objectName: "deviceFilesTransfersBar"
            visible: transfersPane.actionsShown
            Layout.fillWidth: true
            Layout.preferredHeight: Type.rowHeight
            spacing: Type.padding

            Item { Layout.fillWidth: true; visible: transfersPane.fill }
            Label {
                objectName: "deviceFilesTransfersHeading"
                visible: !transfersPane.fill
                Layout.fillWidth: true
                Layout.leftMargin: Type.padding
                font.pointSize: Type.caption
                font.weight: Type.headingWeight
                color: Theme.textSecondary
                text: qsTr("Transfers")
            }
            LinkButton {
                objectName: "deviceFilesCancelAll"
                visible: transfersPane.transfers.activeCount >= 2
                text: qsTr("Cancel all")
                onClicked: transfersPane.transfers.cancelAll()
            }
            LinkButton {
                objectName: "deviceFilesClearFinished"
                Layout.rightMargin: Type.padding
                visible: transfersPane.finishedTransfers > 0
                text: qsTr("Clear finished")
                onClicked: transfersPane.transfers.clearFinished()
            }
        }

        ListView {
            id: transferList
            objectName: "deviceFilesTransfers"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Always shown: hidden while empty, this list stayed hidden when rows
            // came (the client's Transfers dock on Qt 6.12; not reproduced in a
            // smaller case). The empty line above says when there is nothing.
            clip: true
            model: transfersPane.transfers

            delegate: Item {
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
                // Read to be shown in the Viewer, not saved for the user.
                required property bool opening
                // A copy written to the device, and how it ended.
                required property bool upload
                required property int outcome

                readonly property bool failed: state === transfersPane.stateFailed
                readonly property bool saved: state === transfersPane.stateSaved
                // Written, but not as it was meant: read back different, or not checked.
                readonly property bool warned: state === transfersPane.stateWritten
                                               && outcome !== 1 && outcome !== 0
                // After a conflict or a part write the copy can be written anyway;
                // after a plain failure it can be tried again.
                readonly property bool writeAnyway: upload && !active
                    && (outcome === transfersPane.outcomeChanged || outcome === transfersPane.outcomeNotKnown
                        || outcome === transfersPane.outcomePartial)
                readonly property bool tryAgain: upload ? !active && (outcome === transfersPane.outcomeFailed
                                                                      || outcome === transfersPane.outcomeNotChecked)
                                                        : failed

                width: transferList.width
                // A row high, as every list's: the progress runs along its foot.
                height: Type.rowHeight
                HoverHandler { id: rowHover }
                Rectangle {
                    anchors.fill: parent
                    color: rowHover.hovered ? Theme.stateHover : "transparent"
                }
                // A failure, or a write stopped part way.
                readonly property bool alarming: !active && (failed || (upload && outcome === transfersPane.outcomePartial))

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Type.padding
                    anchors.rightMargin: Type.padding
                    spacing: Type.padding

                    // Which way it goes: up to the device, or down from it.
                    ToolIcon {
                        objectName: "deviceFileTransferDirection"
                        Layout.preferredWidth: Type.iconSize
                        Layout.preferredHeight: Type.iconSize
                        name: transferRow.upload ? "upload" : "download"
                        color: Theme.textSecondary
                        Accessible.ignored: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: transferList.width / 3
                        color: Theme.textPrimary
                        text: transferRow.name
                        Accessible.name: transferRow.upload ? qsTr("%1, written to the device").arg(transferRow.name)
                                                            : transferRow.name
                    }
                    Item { Layout.fillWidth: true }
                    // A failure's glyph in the error colour; its words stay readable.
                    ToolIcon {
                        objectName: "deviceFileTransferAlarm"
                        visible: transferRow.alarming
                        Layout.preferredWidth: Type.iconSize
                        Layout.preferredHeight: Type.iconSize
                        name: "error"
                        color: Theme.error
                        Accessible.ignored: true
                    }
                    Label {
                        objectName: "deviceFileTransferFigures"
                        Layout.maximumWidth: transferList.width / 2
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        font.pointSize: Type.caption
                        color: transferRow.active ? Theme.textSecondary
                             : transferRow.alarming ? Theme.textPrimary
                             : transferRow.warned ? Theme.warning
                             : transferRow.saved || transferRow.state === transfersPane.stateWritten ? Theme.textSecondary
                             : Theme.textMuted
                        text: transferRow.active && transferRow.opening
                                  ? qsTr("Opening · %1").arg(transferRow.figures)
                            : transferRow.active ? transferRow.figures
                            : transferRow.result
                        // Cut by a narrow panel: the whole of it on hover.
                        HoverHandler { id: figuresHover }
                        ToolTip.visible: figuresHover.hovered && truncated
                        ToolTip.text: text
                    }
                    LinkButton {
                        objectName: "deviceFileTransferCancel"
                        visible: transferRow.active
                        text: qsTr("Cancel")
                        Accessible.name: qsTr("Cancel %1").arg(transferRow.name)
                        onClicked: transfersPane.transfers.cancel(transferRow.index)
                    }
                    LinkButton {
                        objectName: "deviceFileTransferRetry"
                        visible: transferRow.tryAgain
                        text: transferRow.upload ? qsTr("Try again") : qsTr("Retry")
                        Accessible.name: qsTr("Retry %1").arg(transferRow.name)
                        // Written but not read back: its record is gone, so again anyway.
                        onClicked: transfersPane.transfers.retry(transferRow.index,
                                                         transferRow.upload && transferRow.outcome === transfersPane.outcomeNotChecked)
                    }
                    LinkButton {
                        objectName: "deviceFileTransferWriteAnyway"
                        visible: transferRow.writeAnyway
                        text: transferRow.outcome === transfersPane.outcomePartial ? qsTr("Write again") : qsTr("Write anyway")
                        Accessible.name: transferRow.outcome === transfersPane.outcomePartial
                                         ? qsTr("Write %1 to the device again").arg(transferRow.name)
                                         : qsTr("Write %1 to the device anyway").arg(transferRow.name)
                        onClicked: transfersPane.transfers.retry(transferRow.index, true)
                    }
                    // A pictogram on the row under the pointer, its place kept
                    // on every saved row so nothing moves.
                    IconToolButton {
                        objectName: "deviceFileTransferShow"
                        // A copy read for the Viewer lives in the temporary folder.
                        visible: transferRow.saved && !transferRow.opening && !transferRow.upload
                        opacity: rowHover.hovered || visualFocus ? 1 : 0
                        implicitHeight: Type.rowHeight
                        implicitWidth: Type.rowHeight
                        iconName: "folder"
                        text: qsTr("Show %1 in its folder").arg(transferRow.name)
                        onClicked: transfersPane.showInFolderRequested(transferRow.localPath)
                    }
                }
                // A waiting row has no bar: only the file being read has one.
                ProgressLine {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: Type.padding
                    anchors.rightMargin: Type.padding
                    thickness: 2
                    progress: transferRow.progress
                    running: transferRow.state === transfersPane.stateReading || transferRow.state === transfersPane.stateWriting
                    reducedMotion: transfersPane.reducedMotion
                    description: transferRow.upload ? qsTr("Writing %1").arg(transferRow.name)
                                                    : qsTr("Downloading %1").arg(transferRow.name)
                    valueText: transferRow.figures
                }
            }
        }
    }
}
