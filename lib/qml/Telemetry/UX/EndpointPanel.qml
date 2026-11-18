// Field/Command/Service Details (family spec C, Details layout spec): one
// stack from the top at natural height - the path with the panel's one
// action, line 2 in words, one status line, then each section's table under
// its own heading row - in one scroll. Every table shows its whole structure,
// with "–" for values not read or called yet. The host owns the provider,
// buffers and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQml.Models

Pane {
    id: panel
    palette.window: Theme.surfaceWindow
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surface
    palette.alternateBase: Theme.surfaceAlternate
    palette.text: Theme.textPrimary
    palette.button: Theme.buttonFill
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.stateSelected
    palette.highlightedText: Theme.selectedForeground
    palette.placeholderText: Theme.textMuted
    // Kept for hosts that set them; the panel has no card and always scrolls
    // as one column, so neither changes anything any more.
    property bool framed: true
    property bool scrolls: false
    // No horizontal padding: stripes, hover and selection fills and the
    // header rules span the pane; text stands on L and R (Type.padding).
    padding: 0
    // The top edge's progress line while an action runs. A host that shows
    // its own (the Batch, whose run is one) turns this one off.
    property bool progressShown: true
    // No fading and no flashes: the status line still says what happened.
    property bool reducedMotion: false
    // What Details sends is never undone (UR-1). A host with an UndoRouter
    // gives these words to the area that holds the panel -
    // `UndoScope.refusal: details.undoRefusal` - so Ctrl+Z there outside a
    // field takes back nothing and says why; the Edit menu still names its step.
    readonly property string undoRefusal: provider.commandSelected || provider.serviceSelected
                                          ? qsTr("Undo does not take back what is sent to the device.")
                                          : qsTr("Undo does not take back a write. To go back, apply the old value.")
    readonly property bool hasSelection: provider.selection.length > 0
    required property var provider
    property int categoryFilter: 0
    property Component headerComponent: null
    property Component footerComponent: null
    // The header's title: the path. A host that shows something else (the
    // Batch's pane) names it otherwise.
    property string title: provider.selection
    // Words before line 2, as the Batch's pane says "Item 3 · ".
    property string infoPrefix: ""
    // Replaces the header's own actions (Read, Cancel, Apply, Execute, Call).
    property Component actionsComponent: null
    // Kept for hosts that set it: a response before its call shows its
    // structure with "–", never a sentence.
    property string emptyResponseText: ""
    // What the panel says with nothing selected, where the title would be.
    property string emptyText: qsTr("Select a Field, Command or Service in the Device tree.")
    readonly property int selectedCategory: provider.commandSelected ? 2 : provider.serviceSelected ? 3 : 1
    readonly property bool selectionMatches: categoryFilter === 0 || (provider.selection.length > 0 && selectedCategory === categoryFilter)
    readonly property bool disconnected: typeof provider.connected === "boolean" && !provider.connected
    readonly property var selectionInfo: typeof provider.selectionInfo === "object" && provider.selectionInfo ? provider.selectionInfo : ({})
    property int valueModelRevision: 0
    property int responseModelRevision: 0
    // Moved one turn after rows come or go; the tables' root indexes follow only the resets.
    property int valueRowsRevision: 0
    property int responseRowsRevision: 0
    // When each table's model was last reset, for the cells that flash once
    // when their value changed (the action's result seen).
    property double valuesResetAt: 0
    property double responsesResetAt: 0
    // Changes are off: what the selection would need them for, in words.
    readonly property bool changesOff: typeof provider.allowMutations === "boolean" && !provider.allowMutations
    property string refusalText: !showSelection || !hasSelection || disconnected || !changesOff ? ""
        : provider.commandSelected ? qsTr("Changes are off. Allow changes to execute this Command.")
        : provider.serviceSelected ? qsTr("Changes are off. Allow changes to call this Service.")
        : selectionInfo.writable === true ? qsTr("Changes are off. Allow changes to edit this Field.") : ""
    // The rows a table shows under its root: a section's under its heading,
    // a Field's from its root row.
    function contentRows(model, withHeading) {
        if (!model || typeof model.rowCount !== "function")
            return 0
        const root = contentRoot(model, withHeading)
        return root && root.valid ? model.rowCount(root) : model.rowCount()
    }
    // A Command or Service that takes nothing: its heading and one quiet
    // line, no table. The request's size says so; a provider without it
    // says so by having no rows.
    readonly property bool noArguments: {
        void valueModelRevision
        void valueRowsRevision
        if (!showSelection || !hasSelection || !(provider.commandSelected || provider.serviceSelected))
            return false
        // Its size is known once its type is (a provider without the
        // schema says neither).
        if (typeof selectionInfo.wireBytes === "number" && typeof selectionInfo.typeText === "string"
            && selectionInfo.typeText.length > 0)
            return selectionInfo.wireBytes === 0
        return !provider.busy && contentRows(provider.values, true) === 0
    }
    // A Service whose response holds nothing.
    readonly property bool noResponseValues: showResponse && typeof selectionInfo.responseWireBytes === "number"
                                             && typeof selectionInfo.responseTypeText === "string"
                                             && selectionInfo.responseTypeText.length > 0
                                             && selectionInfo.responseWireBytes === 0
    readonly property bool showMetadata: {
        void valueModelRevision
        void valueRowsRevision
        if (!showSelection || disconnected || !hasSelection || selectedCategory !== 1 || provider.editing ||
            typeof provider.definitions !== "object" || !provider.definitions ||
            typeof provider.definitionRootIndex === "undefined")
            return false
        return provider.values && typeof provider.values.rowCount === "function" && provider.values.rowCount() === 0
    }
    // The complete Configurator keeps its empty workspace and host extension
    // slots while offline. Category-only panels show retained outcomes alone.
    readonly property bool showSelection: selectionMatches && (categoryFilter === 0 || !disconnected)
    // A Service's response, below its request: offline too, its values
    // stale or "–" as the provider keeps them.
    readonly property bool showResponse: (categoryFilter === 0 || categoryFilter === 3) && provider.serviceSelected === true
                                         && showSelection && hasSelection
    // The response the provider holds belongs to the Service on show.
    readonly property bool responseMatches: typeof provider.serviceResponseEndpoint !== "string"
                                            || provider.serviceResponseEndpoint.length === 0
                                            || provider.serviceResponseEndpoint === provider.selection
    property string contextHelp: provider.serviceSelected
        ? qsTr("Request values start at zero, not at device defaults. Call sends one request. The response is kept until the next accepted change or session.")
        : provider.commandSelected
        ? qsTr("Arguments start at zero, not at device defaults. Execute sends the Command once. Accepted does not confirm that the device completed it.")
        : provider.editing
        ? qsTr("You are editing a draft. Apply writes it once, then reads the device again. Floating-point values show the exact encoded value below the input.")
        : qsTr("The type and structure come from the Descriptor. Read reads the Field; expanding its definition does not send requests.")

    // Line 2: the selection in words, "Field · f32 · 4 bytes · read only".
    readonly property string selectionLine: {
        if (typeof selectionInfo.detail === "string" && selectionInfo.detail.length > 0)
            return infoPrefix + selectionInfo.detail
        const parts = [selectedCategory === 2 ? qsTr("Command") : selectedCategory === 3 ? qsTr("Service") : qsTr("Field")]
        if (typeof selectionInfo.typeText === "string" && selectionInfo.typeText.length > 0)
            parts.push(selectionInfo.typeText)
        if (typeof selectionInfo.wireBytes === "number" && selectionInfo.wireBytes > 0)
            parts.push(selectionInfo.wireBytes === 1 ? qsTr("1 byte") : qsTr("%L1 bytes").arg(selectionInfo.wireBytes))
        if (selectedCategory === 1 && typeof selectionInfo.readable === "boolean" && typeof selectionInfo.writable === "boolean")
            parts.push(selectionInfo.readable && selectionInfo.writable ? qsTr("read and write")
                : selectionInfo.readable ? qsTr("read only") : selectionInfo.writable ? qsTr("write only") : qsTr("no access"))
        return infoPrefix + parts.join(" · ")
    }
    // The status line: {text, failed, tip}, the state said once. A provider
    // that says it (detailsStatus) is taken at its word; otherwise it is made
    // here.
    readonly property var statusInfo: {
        void valueModelRevision
        void valueRowsRevision
        if (typeof provider.detailsStatus === "object" && provider.detailsStatus
            && typeof provider.detailsStatus.text === "string")
            return provider.detailsStatus
        if (!hasSelection)
            return ({ text: "", failed: false, tip: "" })
        if (provider.busy)
            return ({ text: provider.commandSelected ? qsTr("Executing…") : provider.serviceSelected ? qsTr("Calling…")
                            : provider.editing ? qsTr("Writing…") : qsTr("Reading…"), failed: false, tip: "" })
        if (selectedCategory === 1 && provider.editing)
            return ({ text: qsTr("Edited, not applied"), failed: false, tip: "" })
        const outcome = selectedCategory === 2 ? provider.commandOutcome : selectedCategory === 3 ? provider.serviceOutcome
            : provider.writeOutcome
        const attention = selectedCategory === 2 ? provider.commandOutcomeNeedsAttention
            : selectedCategory === 3 ? provider.serviceOutcomeNeedsAttention : provider.writeOutcomeNeedsAttention
        if (typeof outcome === "string" && outcome.length > 0)
            return ({ text: outcome, failed: attention === true, tip: "" })
        const read = !!provider.values && typeof provider.values.rowCount === "function" && provider.values.rowCount() > 0
            && provider.values.shape !== true
        return ({ text: selectedCategory === 2 ? qsTr("Not executed yet") : selectedCategory === 3 ? qsTr("Not called yet")
                        : read ? qsTr("Read") : qsTr("Not read yet"), failed: false, tip: "" })
    }
    // What a category panel keeps from before the connection dropped.
    readonly property string retainedText: {
        if (!disconnected)
            return ""
        const parts = []
        // A category panel keeps its kind's; the whole panel the selection's,
        // or every kind's with nothing selected.
        const kind = categoryFilter !== 0 ? categoryFilter : hasSelection ? selectedCategory : 0
        const add = function(category, text) {
            if ((kind === 0 || kind === category) && typeof text === "string" && text.length > 0)
                parts.push(text)
        }
        add(1, provider.writeOutcome)
        add(2, provider.commandOutcome)
        add(3, provider.serviceOutcome)
        return parts.length > 0 ? qsTr("Before the connection dropped: %1").arg(parts.join(" · ")) : ""
    }
    background: Rectangle { color: Theme.surfaceWindow }

    // Columns (family spec A3): Name fills; Value is as wide as the widest
    // value shown, growing only; Type as wide as the widest type, 40 to 128 px;
    // Type hides before Name gets narrower than 96 px. Nothing scrolls sideways.
    // The widths are the panel's, so every table's Value and Type stand at
    // one x.
    property real valueSeen: 0
    property real numberSeen: 0
    property real typeSeen: 0
    function noteValue(width, numeric) {
        if (width > valueSeen) valueSeen = width
        if (numeric && width > numberSeen) numberSeen = width
    }
    function noteType(width) { if (width > typeSeen) typeSeen = width }
    function resetColumns() { valueSeen = 0; numberSeen = 0; typeSeen = 0; ++columnEpoch }
    Connections { target: Type; function onBodyChanged() { panel.resetColumns() } }
    function valueWidth(width) {
        const most = Math.max(Math.min(Type.bodyPixels * 16.5, width * 0.4), numberSeen)
        return Math.min(Math.max(Type.bodyPixels * 4.2, valueSeen), most)
    }
    function typeWidth() { return Math.min(Type.bodyPixels * 9.6, Math.max(Type.bodyPixels * 3, typeSeen)) }
    function typeShown(width) {
        return width - Type.padding * 2 - valueWidth(width) - typeWidth() - Type.columnGap * 2 - Type.indent
            >= Type.bodyPixels * 7.2
    }
    function typeX(width) { return width - Type.padding - typeWidth() }
    function valueX(width) { return (typeShown(width) ? typeX(width) - Type.columnGap : width - Type.padding) - valueWidth(width) }
    function nameEnd(width) { return valueX(width) - Type.columnGap }

    // The panel's one primary action: Apply, Execute or Call.
    component AccentButton: Button {
        font.weight: Type.headingWeight
        palette.button: Theme.accent
        palette.buttonText: Theme.accentForeground
        palette.brightText: Theme.accentForeground
        palette.disabled.button: Theme.surfaceDisabled
        palette.disabled.buttonText: Theme.textDisabled
    }
    // An action that says what it is doing while it runs: "Reading…" in
    // place of "Read", as wide as the wider of its two words so nothing
    // beside it moves.
    component ActionMetrics: TextMetrics {
        required property Button button
        font: button.font
    }
    // A failure's 12 px glyph: a ring with "!". Pure QML, so a host needs none
    // of the module's C++ types for it.
    component ErrorGlyph: Shape {
        id: glyph
        readonly property real s: width / 12
        implicitWidth: Math.round(Type.bodyPixels * 0.9)
        implicitHeight: implicitWidth
        preferredRendererType: Shape.CurveRenderer
        Accessible.ignored: true
        ShapePath {
            strokeColor: Theme.error
            strokeWidth: Math.max(1, glyph.s * 1.2)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: glyph.s * 6
            startY: glyph.s * 0.9
            PathArc { x: glyph.s * 6; y: glyph.s * 11.1; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathArc { x: glyph.s * 6; y: glyph.s * 0.9; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathMove { x: glyph.s * 6; y: glyph.s * 3.4 }
            PathLine { x: glyph.s * 6; y: glyph.s * 6.9 }
            PathMove { x: glyph.s * 6; y: glyph.s * 8.9 }
            PathLine { x: glyph.s * 6; y: glyph.s * 9.0 }
        }
    }
    // A footnote under its table: one caption line at the rows' names,
    // elided, the whole of it in the tooltip.
    component Footnote: Label {
        id: note
        Layout.fillWidth: true
        Layout.topMargin: Type.tight
        Layout.leftMargin: Type.padding + Type.indent
        Layout.rightMargin: Type.padding
        textFormat: Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
        font.pointSize: Type.caption
        color: Theme.textMuted
        Accessible.name: text
        HoverHandler { id: noteHover }
        PlainToolTip { delay: 700; visible: noteHover.hovered && note.truncated; text: note.text }
    }
    // A section with no values: one quiet line where its first row would be.
    component QuietLine: Label {
        Layout.fillWidth: true
        Layout.preferredHeight: Type.rowHeight
        Layout.leftMargin: Type.padding + Type.indent
        Layout.rightMargin: Type.padding
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.pointSize: Type.caption
        color: Theme.textMuted
    }

    // Ctrl+Enter: Apply a draft, Execute a Command, Call a Service.
    function primaryAction() {
        if (!showSelection || !hasSelection || actionsComponent !== null)
            return false
        if (provider.commandSelected)
            return provider.canExecute ? (provider.executeCommand(), true) : false
        if (provider.serviceSelected)
            return provider.canCallService ? (provider.callService(), true) : false
        return provider.editing && provider.canApply ? (provider.apply(), true) : false
    }
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier))
            event.accepted = panel.primaryAction()
    }
    // The pane's one scroll moves just enough to show a table's row whole:
    // the tables take their whole height and never scroll themselves.
    function ensureVisible(view, row) {
        if (!view || row < 0 || row >= view.rows)
            return
        // A row outside the table's window has no delegate: its place is
        // estimated from the rows' even height.
        const item = view.itemAtCell(Qt.point(0, row))
        const rowY = item ? item.y : row * (view.rows > 0 ? view.contentHeight / view.rows : Type.rowHeight)
        const top = view.windowTop + rowY
        const bottom = top + (item ? item.height : Type.rowHeight)
        if (top < bodyScroll.contentY)
            bodyScroll.contentY = Math.max(0, top)
        else if (bottom > bodyScroll.contentY + bodyScroll.height)
            bodyScroll.contentY = Math.min(Math.max(0, bodyScroll.contentHeight - bodyScroll.height),
                                           bottom - bodyScroll.height)
        view.forceLayout()
    }
    // The cell after or before row that takes text, opened; false at the end.
    function openNext(view, row, forward) {
        for (let r = row + (forward ? 1 : -1); r >= 0 && r < view.rows; r += forward ? 1 : -1) {
            ensureVisible(view, r)
            const item = view.itemAtCell(Qt.point(0, r))
            if (item && item.valueItem && item.valueItem.canOpen)
                return item.valueItem.begin()
        }
        view.forceActiveFocus()
        return false
    }
    // F2 or Enter opens the current row's cell.
    function openCurrent(view, event) {
        const editKey = event.key === Qt.Key_F2 || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            && !(event.modifiers & Qt.ControlModifier))
        if (!editKey || view.currentRow < 0)
            return
        const item = view.itemAtCell(Qt.point(0, view.currentRow))
        if (item && item.valueItem && item.valueItem.canOpen)
            event.accepted = item.valueItem.begin()
    }
    // A Field picked while changes are allowed is read once, so its cells can
    // be edited at once: one read per selection, none while another is pending
    // and none for a Field already read. Anything the provider starts before
    // the turn ends (a Read pressed, a host's own request) takes its place.
    readonly property string selectedPath: hasSelection ? provider.selection : ""
    // The definition table is made again for every selection. A filter change
    // under a table that has rows open leaves the TreeView's flattened rows
    // behind (the members listed twice, or the rows of the Field before); a
    // table made after the change starts from the model as it is.
    property bool metadataFresh: true
    Timer { id: metadataRebuild; interval: 0; onTriggered: panel.metadataFresh = true }
    property bool readArmed: false
    onSelectedPathChanged: {
        metadataFresh = false
        metadataRebuild.restart()
        // Another endpoint: this panel's own folds are forgotten.
        if (foldMemory === null)
            ownMemory.clear()
        resetColumns()
        rootFetch.restart()
        readArmed = selectedPath.length > 0
        readTimer.restart()
        bodyScroll.contentY = 0
    }
    readonly property bool providerBusy: provider.busy === true
    // An action ran and ended: the status line's time flashes once, so a
    // Read at the same values is visibly new. A failure does not flash: its
    // glyph and words say it.
    property bool wasBusy: false
    property int statusFlashes: 0
    readonly property bool statusFlashing: statusFlash.running
    function flashStatus() {
        if (reducedMotion || statusInfo.failed === true || statusTime.text.length === 0 || !statusTime.fits)
            return
        ++statusFlashes
        statusFlash.restart()
    }
    onProviderBusyChanged: {
        if (providerBusy) {
            readArmed = false
            wasBusy = true
        } else if (wasBusy) {
            wasBusy = false
            flashStatus()
        }
    }
    Timer { id: readTimer; interval: 0; onTriggered: panel.readSelection() }
    function readSelection() {
        if (!readArmed)
            return
        readArmed = false
        if (!showSelection || !hasSelection || selectedCategory !== 1 || disconnected
            || provider.allowMutations !== true || selectionInfo.readable === false
            || provider.busy || provider.editing || !provider.canRefresh)
            return
        // A structure-only table (shape) is not read yet.
        const values = provider.values
        if (values && typeof values.rowCount === "function" && values.rowCount() > 0 && values.shape !== true)
            return
        provider.refresh()
    }
    // One root per table: a section heading (Arguments, Request, Response) or
    // the Field's own root row, never both. Under a heading the table shows the
    // struct's members; a Field keeps its root row (spec A).
    readonly property bool headed: {
        // The model says what it holds at the moment it is reset; the
        // provider's selection flags may follow a moment later.
        void valueModelRevision
        const values = provider.values
        return values && typeof values.sectioned === "function" ? values.sectioned()
            : provider.commandSelected === true || provider.serviceSelected === true
    }
    function contentRoot(model, withHeading) {
        if (!model || typeof model.index !== "function")
            return undefined
        if (withHeading === false)
            return model.index(-1, -1)
        return typeof model.contentRoot === "function" ? model.contentRoot() : model.index(-1, -1)
    }
    // The table has taken the model's current root index (a reset moves it).
    // The root row of a Field stays in the table's expanded set; the root
    // index of a section is that very row, and with it open its rows show twice.
    function closeRootRow(view, withHeading) {
        if (withHeading && view && view.rows > 0 && view.isExpanded(0)) {
            view.noteBusy = true
            view.collapse(0)
            view.noteBusy = false
        }
    }
    function viewCaughtUp(view) {
        if (view.section === "value" || view.section === "request" ? view.headed !== panel.headed : false)
            return false
        const wanted = contentRoot(view.model, view.headed)
        return !wanted || !wanted.valid ? !view.rootIndex.valid
               : view.rootIndex.valid && view.rootIndex.row === wanted.row && view.rootIndex.model === wanted.model
    }
    function fetchRoot(model, withHeading) {
        const root = contentRoot(model, withHeading)
        if (root && root.valid && model.canFetchMore(root)) {
            model.fetchMore(root)
        }
    }
    // The tables are set to their defaults and what the user had opened when
    // the model, the selection or the panel itself arrives.
    readonly property var valuesModel: provider.values
    onValuesModelChanged: rootFetch.restart()
    Component.onCompleted: rootFetch.restart()
    Timer {
        id: rootFetch
        interval: 0
        onTriggered: {
            // Rows fetched under a root the table has not taken yet would be
            // shown twice once it does.
            if (typeof valueTree === "undefined" || !valueTree || panel.viewCaughtUp(valueTree))
                panel.fetchRoot(panel.provider.values, panel.headed)
            if (typeof responseTree === "undefined" || !responseTree || panel.viewCaughtUp(responseTree))
                panel.fetchRoot(panel.provider.responses, true)
            if (typeof valueTree !== "undefined" && valueTree && !panel.showMetadata)
                panel.restoreFolds(valueTree)
            if (typeof responseTree !== "undefined" && responseTree && responseSection.hasRows)
                panel.restoreFolds(responseTree)
        }
    }

    // A section's heading row is its table's header: the section's name in
    // the Name column's place on L, "Value" over the values and "Type", and
    // the rule under the row. A Field's table has no section name: its
    // column reads "Name". A section with no values shows its name alone.
    component TableHeading: Item {
        id: heading
        required property var view
        property string title: ""
        property bool bare: false
        implicitHeight: Type.rowHeight
        readonly property real tableWidth: view ? view.width : width
        Accessible.role: title.length > 0 ? Accessible.Heading : Accessible.ColumnHeader
        Accessible.name: title.length > 0 ? title : qsTr("Name, Value, Type")
        Label {
            objectName: "tableHeadingName"
            x: heading.title.length > 0 ? Type.padding : Type.padding + Type.indent
            width: Math.max(0, (heading.bare ? heading.width - Type.padding : panel.nameEnd(heading.tableWidth)) - x)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: heading.title.length > 0 ? heading.title : qsTr("Name")
            color: heading.title.length > 0 ? Theme.textPrimary : Theme.textSecondary
            font.pointSize: Type.caption
            font.weight: heading.title.length > 0 ? Type.headingWeight : Type.bodyWeight
            Accessible.ignored: true
        }
        Label {
            objectName: "tableHeadingValue"
            visible: !heading.bare
            x: panel.valueX(heading.tableWidth)
            width: panel.valueWidth(heading.tableWidth)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Value"); color: Theme.textSecondary; font.pointSize: Type.caption
            horizontalAlignment: Text.AlignRight
            Accessible.ignored: true
        }
        Label {
            objectName: "tableHeadingType"
            x: panel.typeX(heading.tableWidth)
            width: panel.typeWidth()
            visible: !heading.bare && panel.typeShown(heading.tableWidth)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Type"); color: Theme.textSecondary; font.pointSize: Type.caption
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "tableHeadingRule"
            visible: !heading.bare
            anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.outlineVariant
        }
    }
    // ---- Fold memory (spec F): what the user opened, by path ----
    // A host's own (the Batch's, scoped by item) or this panel's, scoped by
    // the selected endpoint and forgotten when another is selected.
    property var foldMemory: null
    property string foldScope: ""
    readonly property var memory: foldMemory !== null ? foldMemory : ownMemory
    readonly property string memoryScope: foldScope.length > 0 ? foldScope : "endpoint:" + provider.selection
    FoldMemory { id: ownMemory }
    // The root row of a Field's table opens by default; one constant.
    readonly property bool rootOpenByDefault: true
    // Bumped when the columns are measured again (zoom, another selection).
    property int columnEpoch: 0

    // The tail of a path "device.Config" -> "Config".
    function lastSegment(path, fallback) {
        const cut = path.lastIndexOf(".")
        const tail = cut >= 0 ? path.substring(cut + 1) : path
        return tail.length > 0 ? tail : fallback
    }
    function setCurrentRow(view, row) {
        if (row < 0 || row >= view.rows)
            return
        view.selectionModel.setCurrentIndex(view.index(row, 0), ItemSelectionModel.ClearAndSelect)
        if (!view.noteBusy)
            ensureVisible(view, row)
    }
    function rowHasChildren(view, row) {
        const model = view.model
        return !!model && model.hasChildren(view.index(row, 0))
    }
    function pathOfRow(view, row) {
        const model = view.model
        return model && typeof model.pathOf === "function" ? model.pathOf(view.index(row, 0)) : ""
    }
    function isRangePath(path) { return path.indexOf("…") >= 0 }
    // The branch's own parent row: the nearest row above at a shallower depth.
    function parentRow(view, row) {
        const depth = view.depth(row)
        for (let r = row - 1; r >= 0; --r)
            if (view.depth(r) < depth)
                return r
        return -1
    }
    // Opens everything under a row that is loaded or cheap to load: never the
    // elements of a range, never more than 2,000 rows (spec E).
    function expandUnder(view, row) {
        if (row < 0 || row >= view.rows)
            return
        const model = view.model
        const base = view.depth(row)
        view.expand(row)
        view.forceLayout()
        // The rows a branch opens are in the tree at once; the table counts
        // them when it is laid out, which happens only when the walk reaches
        // the end of the rows it knows - not once per branch.
        for (let r = row + 1; view.rows < 2000; ++r) {
            if (r >= view.rows) {
                view.forceLayout()
                if (r >= view.rows)
                    break
            }
            if (view.depth(r) <= base)
                break
            if (!panel.rowHasChildren(view, r) || view.isExpanded(r))
                continue
            if (isRangePath(pathOfRow(view, r)) && model.rowCount(view.index(r, 0)) === 0)
                continue
            view.expand(r)
        }
        view.forceLayout()
    }
    function collapseUnder(view, row) {
        if (row < 0 || row >= view.rows)
            return
        const path = pathOfRow(view, row)
        view.collapseRecursively(row)
        memory.forgetUnder(memoryScope, view.section, path)
        memory.note(memoryScope, view.section, path, false)
    }
    // Back to the defaults: the root open, everything else folded.
    function collapseAll(view) {
        view.noteBusy = true
        for (let r = view.rows - 1; r >= 0; --r)
            if (view.isExpanded(r))
                view.collapse(r)
        view.noteBusy = false
        memory.forgetTable(memoryScope, view.section)
        applyDefaults(view)
    }
    // The rows open before the user chooses: a Field's root, a section's single branch.
    function applyDefaults(view) {
        if (view.rows === 0)
            return
        const opens = !view.headed ? rootOpenByDefault && panel.rowHasChildren(view, 0)
                                    : view.model.rowCount(view.rootIndex) === 1 && panel.rowHasChildren(view, 0)
        if (!opens || view.isExpanded(0))
            return
        const path = pathOfRow(view, 0)
        const known = memory.entries(memoryScope, view.section).filter(function(item) { return item.path === path })
        if (known.length > 0 && known[0].open === false)
            return
        view.noteBusy = true
        view.expand(0)
        view.noteBusy = false
    }
    // The definition table: only the Field's own row (row 0) is open.
    function openDefinitionRoot(view) {
        if (!view || !view.model)
            return
        view.noteBusy = true
        for (let r = view.rows - 1; r >= 1; --r)
            if (view.isExpanded(r))
                view.collapse(r)
        if (view.rows > 0 && rowHasChildren(view, 0) && !view.isExpanded(0))
            view.expand(0)
        view.noteBusy = false
        view.scheduleActive()
        view.forceLayout()
    }
    // After a model reset: defaults, then what the user had opened, shallowest
    // first; a path that no longer exists is dropped; last the current row -
    // only one the user made current (a click, a key): a restore never makes
    // one of its own, so a table shows no selection the user did not choose.
    function restoreFolds(view) {
        const model = view.model
        if (!view || !model)
            return
        // A table counts its rows when it is laid out, once per frame: lay
        // it out now, or the root would show closed for the frames until a
        // retry (the first Read of a struct Field showed its root alone).
        if (viewCaughtUp(view) && view.rows < model.rowCount(view.rootIndex))
            view.forceLayout()
        if (!viewCaughtUp(view) || view.rows < model.rowCount(view.rootIndex)) {
            // A table not shown yet, or not caught up with its model, has no
            // rows to open; try again when it has.
            if (view.restoreTries < 8) {
                ++view.restoreTries
                view.restoreRetry.restart()
            }
            return
        }
        view.restoreTries = 0
        view.noteBusy = true
        applyDefaults(view)
        if (typeof model.indexOfPath === "function") {
            for (const item of memory.entries(memoryScope, view.section)) {
                const index = model.indexOfPath(item.path)
                if (!index || !index.valid) {
                    memory.forgetUnder(memoryScope, view.section, item.path)
                    continue
                }
                const row = view.rowAtIndex(index)
                if (row < 0)
                    continue
                if (item.open && !view.isExpanded(row) && panel.rowHasChildren(view, row))
                    view.expand(row)
                else if (!item.open && view.isExpanded(row))
                    view.collapse(row)
            }
            const index = memory.hasCurrent(memoryScope, view.section)
                ? model.indexOfPath(memory.currentPath(memoryScope, view.section)) : undefined
            if (index && index.valid) {
                const row = view.rowAtIndex(index)
                if (row >= 0)
                    setCurrentRow(view, row)
            }
        }
        view.noteBusy = false
        view.scheduleActive()
    }
    // Keys for a tree table (spec E); true when the key was taken.
    function treeKey(view, event) {
        const row = view.currentIndexRow()
        if (row < 0)
            return false
        const branch = panel.rowHasChildren(view, row)
        const open = view.isExpanded(row)
        const plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
        if (event.key === Qt.Key_Right && plain) {
            if (branch && !open)
                view.expand(row)
            else if (branch && open)
                setCurrentRow(view, row + 1)
            return true
        }
        if (event.key === Qt.Key_Left && plain) {
            if (branch && open)
                view.collapse(row)
            else
                setCurrentRow(view, parentRow(view, row))
            return true
        }
        if (event.key === Qt.Key_Asterisk || event.text === "*") {
            expandUnder(view, row)
            return true
        }
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && plain && branch) {
            view.toggleExpanded(row)
            return true
        }
        if (event.key === Qt.Key_F2 && branch)
            return true
        if ((event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) || event.key === Qt.Key_Menu) {
            openMenu(view, row)
            return true
        }
        return false
    }
    function openMenu(view, row) {
        rowMenu.view = view
        rowMenu.row = row
        rowMenu.popup()
    }
    Menu {
        id: rowMenu
        objectName: "detailsRowMenu"
        property var view: null
        property int row: -1
        MenuItem {
            objectName: "treeMenuExpandAll"
            text: qsTr("Expand all under\t*")
            onTriggered: panel.expandUnder(rowMenu.view, rowMenu.row)
        }
        MenuItem {
            objectName: "treeMenuCollapseUnder"
            text: qsTr("Collapse all under")
            onTriggered: panel.collapseUnder(rowMenu.view, rowMenu.row)
        }
        MenuItem {
            objectName: "treeMenuCollapseAll"
            text: qsTr("Collapse all")
            onTriggered: panel.collapseAll(rowMenu.view)
        }
    }

    // A tree table: the house tree with one indent step for the whole table
    // (spec C), the current branch's guide, and the fold memory's hooks. It
    // takes its whole height and never scrolls: the pane does.
    component DetailTree: TreeView {
        id: tree
        // value, request or response: the table's key in the fold memory.
        property string section: "value"
        // The table sits under a section heading, which is its root.
        property bool headed: false
        // Writes by the restore are not the user's.
        property bool noteBusy: false
        property int seenDeepest: 0
        property int restoreTries: 0
        readonly property Timer restoreRetry: Timer { interval: 40; onTriggered: panel.restoreFolds(tree) }
        onVisibleChanged: if (visible) rootFetch.restart()
        readonly property int modelDeepest: model && typeof model.deepest === "number" ? model.deepest - (headed ? 1 : 0) : -1
        readonly property int deepest: Math.max(0, modelDeepest >= 0 ? modelDeepest : seenDeepest)
        // A window over its rows (Details layout §2's fallback, chosen by
        // check 15: a table of 2,000 rows at its whole height took seconds
        // to draw): as tall as the part of its slot the pane shows, moved
        // and scrolled with the pane, so only the rows in view have
        // delegates. The slot (its parent) is as tall as all the rows; the
        // pane's scroll is the only one. windowTop: the slot's top in the
        // pane's content.
        property real windowTop: 0
        readonly property real windowStart: Math.max(0, Math.min(bodyScroll.contentY - windowTop, contentHeight - height))
        width: parent ? parent.width : 0
        y: windowStart
        height: Math.max(0, Math.min(Math.max(contentHeight, Type.rowHeight), bodyScroll.height))
        onWindowStartChanged: contentY = windowStart
        onContentYChanged: if (Math.abs(contentY - windowStart) > 0.5) contentY = windowStart
        animate: false
        interactive: false
        clip: false
        alternatingRows: false
        contentWidth: width
        columnWidthProvider: function() { return tree.width }
        // One step for every row of the table: 16 px while the deepest level
        // keeps 80 px for its name, then evenly less, to 8 px.
        property real step: Type.indent
        property int activeLevel: -1
        property int activeFrom: 0
        property int activeTo: -1
        function updateStep() {
            const room = panel.nameEnd(width) - (Type.padding + Type.indent) - 6 * Type.bodyPixels
            step = deepest === 0 ? Type.indent
                : Math.max(Math.floor(Type.indent / 2), Math.min(Type.indent, Math.floor(room / deepest)))
        }
        function noteDepth(depth) { if (depth > seenDeepest) seenDeepest = depth }
        function currentIndexRow() {
            const index = selectionModel ? selectionModel.currentIndex : undefined
            return index && index.valid ? rowAtIndex(index) : -1
        }
        // Timers, not Qt.callLater: the table may be gone by the next turn.
        Timer { id: activeTimer; interval: 0; onTriggered: tree.updateActive() }
        Timer { id: stepTimer; interval: 0; onTriggered: tree.updateStep() }
        function scheduleActive() { activeTimer.restart() }
        function updateActive() {
            const row = currentIndexRow()
            if (row < 0 || row >= rows) {
                activeLevel = -1
                return
            }
            let branch = row
            if (!(panel.rowHasChildren(tree, row) && isExpanded(row))) {
                if (depth(row) === 0) {
                    activeLevel = -1
                    return
                }
                branch = panel.parentRow(tree, row)
                if (branch < 0) {
                    activeLevel = -1
                    return
                }
            }
            const level = depth(branch)
            let last = branch
            for (let r = branch + 1; r < rows && depth(r) > level; ++r)
                last = r
            activeFrom = branch + 1
            activeTo = last
            activeLevel = level
        }
        onWidthChanged: {
            updateStep()
            Qt.callLater(function() { if (tree) tree.forceLayout() })
        }
        onDeepestChanged: updateStep()
        Component.onCompleted: updateStep()
        Connections { target: Type; function onBodyChanged() { stepTimer.restart() } }
        Connections { target: Type; function onRowHeightChanged() { Qt.callLater(function() { if (tree) tree.forceLayout() }) } }
        // The columns settle as values are measured: valueSeen and typeSeen only grow.
        Connections {
            target: panel
            function onColumnEpochChanged() { stepTimer.restart() }
            function onValueSeenChanged() { stepTimer.restart() }
            function onTypeSeenChanged() { stepTimer.restart() }
        }
        Connections { target: tree.selectionModel; function onCurrentChanged() { tree.scheduleActive()
            if (!tree.noteBusy && tree.section.length > 0) {
                const row = tree.currentIndexRow()
                if (row >= 0)
                    panel.memory.noteCurrent(panel.memoryScope, tree.section, panel.pathOfRow(tree, row))
            }
            if (!tree.noteBusy)
                panel.ensureVisible(tree, tree.currentIndexRow()) } }
        // Tab into a table with no current row makes its first row current,
        // so the focus shows at once (WCAG 2.4.7); a click makes its own row.
        onActiveFocusChanged: if (activeFocus && rows > 0 && currentIndexRow() < 0) firstRow.restart()
        Timer {
            id: firstRow
            interval: 0
            onTriggered: if (tree.activeFocus && tree.rows > 0 && tree.currentIndexRow() < 0) panel.setCurrentRow(tree, 0)
        }
        onRowsChanged: scheduleActive()
        // Nothing stays open across a reset: the table's expanded set would
        // outlive the rows it was made for and show a section's rows twice.
        // What the user had open is kept by path and opened again afterwards.
        Connections {
            target: tree.model
            ignoreUnknownSignals: true
            function onModelAboutToBeReset() {
                tree.noteBusy = true
                for (let r = tree.rows - 1; r >= 0; --r)
                    if (tree.isExpanded(r))
                        tree.collapse(r)
                tree.noteBusy = false
            }
        }

        onExpanded: function(row, depth) {
            scheduleActive()
            if (noteBusy)
                return
            // The row and every branch now open under it.
            const base = tree.depth(row)
            panel.memory.note(panel.memoryScope, section, panel.pathOfRow(tree, row), true)
            for (let r = row + 1; r < rows && tree.depth(r) > base; ++r)
                if (isExpanded(r))
                    panel.memory.note(panel.memoryScope, section, panel.pathOfRow(tree, r), true)
        }
        onCollapsed: function(row, recursively) {
            scheduleActive()
            if (noteBusy)
                return
            const path = panel.pathOfRow(tree, row)
            if (recursively)
                panel.memory.forgetUnder(panel.memoryScope, section, path)
            panel.memory.note(panel.memoryScope, section, path, false)
        }
    }

    // One row of the family (spec A): Name, Value and Type at the same x at
    // every depth, striped, 22 px tall unless a cell's caption follows; the
    // house tree's chevron and indent guides left of the name.
    component DetailRow: TreeViewDelegate {
        id: cellRow
        property string cellName: ""
        property string cellType: ""
        property string cellValue: ""
        property string cellHint: ""
        // The row's path under the table's first level, and the words before it.
        property string cellPath: ""
        property string pathPrefix: ""
        // The Field's root row, in the heading weight.
        property bool container: false
        // A value not yet sent: the accent line at the row's left.
        property bool unsent: false
        property int unsentCount: 0
        property string unsentWords: ""
        // What a folded branch shows, for the accessible description.
        readonly property string summaryShown: valueItem && typeof valueItem.summaryShown === "string" ? valueItem.summaryShown : ""
        property Component valueComponent
        // The row tooltip's name, so a host's check can find it per tree.
        property string toolTipName: ""
        readonly property var valueItem: valueCell.item
        readonly property var tree: treeView
        readonly property real step: treeView && typeof treeView.step === "number" ? treeView.step : Type.indent
        // Where the name starts, and where the chevron goes: past the hard
        // cap a name keeps 4 characters.
        readonly property real nameIndent: Math.min(depth * step + Type.indent,
            Math.max(Type.indent, panel.nameEnd(width) - Type.bodyPixels * 4 - Type.padding))
        readonly property real chevronX: Type.padding + nameIndent - Type.indent
        readonly property real nameX: Type.padding + nameIndent
        // The name's text end, and the room a folded summary or an editor may take left of the value.
        readonly property real nameTextEnd: nameX + Math.min(nameLabel.implicitWidth, Math.max(0, panel.nameEnd(width) - nameX))
        readonly property real valueRoom: Math.max(0, panel.valueX(width) - Type.tight
            - (nameX + Math.min(nameLabel.implicitWidth, Type.bodyPixels * 3)))
        readonly property real restRoom: Math.max(0, panel.valueX(width) - (nameTextEnd + Type.columnGap))
        readonly property real captionRoom: Math.max(0, panel.valueX(width) - nameX)
        readonly property real captionRightRoom: Math.max(0, width - Type.padding - panel.valueX(width) - panel.valueWidth(width))
        indentation: Type.indent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        indicator: TreeChevron { row: cellRow; x: cellRow.chevronX }
        implicitWidth: treeView ? treeView.width : 0
        implicitHeight: Math.max(Type.rowHeight, valueCell.implicitHeight)
        text: cellName
        highlighted: current
        Accessible.role: Accessible.TreeItem
        Accessible.name: cellName
        Accessible.expandable: hasChildren
        Accessible.expanded: expanded
        readonly property string accessibleText: [cellType, summaryShown.length > 0 ? summaryShown : cellValue,
            depth >= 1 && cellPath.indexOf(".") > 0 ? qsTr("in %1").arg(cellPath.substring(0, cellPath.lastIndexOf("."))) : "",
            unsentWords, cellHint].filter(function(part) { return part.length > 0 }).join("\n")
        Accessible.description: accessibleText
        readonly property real measuredValue: valueCell.item && typeof valueCell.item.textWidth === "number" ? valueCell.item.textWidth : 0
        function noteMeasured() {
            if (valueCell.item)
                panel.noteValue(measuredValue, valueCell.item.numeric === true)
            if (treeView && typeof treeView.noteDepth === "function")
                treeView.noteDepth(depth)
        }
        onMeasuredValueChanged: noteMeasured()
        Component.onCompleted: noteMeasured()
        TableView.onReused: noteMeasured()
        readonly property int epoch: panel.columnEpoch
        onEpochChanged: noteMeasured()
        // The row may be gone by the next turn of the event loop.
        onImplicitHeightChanged: Qt.callLater(function() { if (cellRow && cellRow.treeView) cellRow.treeView.forceLayout() })
        // A cell's editor opened below the fold: the pane brings it in.
        Connections {
            target: cellRow.valueItem
            ignoreUnknownSignals: true
            function onOpenChanged() {
                if (cellRow.valueItem && cellRow.valueItem.open === true)
                    panel.ensureVisible(cellRow.treeView, cellRow.row)
            }
        }
        // The full path, after 700 ms, on a nested row or an elided name.
        readonly property string pathText: {
            if (cellPath.length === 0)
                return pathPrefix
            const glue = cellPath.charAt(0) === "[" || pathPrefix.endsWith(": ") ? "" : "."
            return pathPrefix + glue + cellPath
        }
        HoverHandler { id: nameHover }
        PlainToolTip {
            objectName: cellRow.toolTipName
            delay: 700
            visible: nameHover.hovered && (cellRow.depth >= 1 || nameLabel.truncated) && cellRow.pathText.length > 0
            text: cellRow.pathText + (cellRow.cellHint.length > 0 ? "\n" + cellRow.cellHint : "")
        }
        background: Rectangle {
            objectName: "detailRowFill"
            color: cellRow.highlighted
                ? (cellRow.treeView && cellRow.treeView.activeFocus ? Theme.stateSelected : Theme.stateSelectedInactive)
                : cellRow.hovered ? Theme.stateHover
                : cellRow.row % 2 === 1 ? Theme.surfaceAlternate : "transparent"
            // Indent guides: one 1 px line under each ancestor's chevron.
            Repeater {
                model: cellRow.depth
                Rectangle {
                    required property int index
                    objectName: "indentGuide"
                    readonly property real lineX: Math.round(Type.padding + index * cellRow.step + Type.indent / 2)
                    // Past the cap a row's chevron stands left of its deeper guides.
                    visible: lineX <= cellRow.chevronX + 0.5
                    x: lineX
                    width: 1
                    height: cellRow.height
                    readonly property bool active: cellRow.treeView && index === cellRow.treeView.activeLevel
                        && cellRow.row >= cellRow.treeView.activeFrom && cellRow.row <= cellRow.treeView.activeTo
                    color: active ? Theme.outline : Theme.outlineVariant
                    Accessible.ignored: true
                }
            }
        }
        // A folded branch carries the mark when any value under it is unsent.
        Rectangle {
            objectName: "unsentMark"
            width: 2; height: parent.height
            color: Theme.accent
            visible: cellRow.unsent && (!cellRow.hasChildren || !cellRow.expanded)
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "detailFocusRing"
            anchors.fill: parent; color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: cellRow.current && cellRow.treeView && cellRow.treeView.activeFocus
            Accessible.ignored: true
        }
        // A right-click opens the row's menu on the row.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                panel.setCurrentRow(cellRow.treeView, cellRow.row)
                panel.openMenu(cellRow.treeView, cellRow.row)
            }
        }
        contentItem: Item {
            Label {
                id: nameLabel
                objectName: "detailCellName"
                x: cellRow.nameX
                width: Math.max(0, panel.nameEnd(cellRow.width) - x)
                height: Type.rowHeight
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText; text: cellRow.cellName; elide: Text.ElideMiddle
                font.weight: cellRow.container ? Type.headingWeight : Type.bodyWeight
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textPrimary
            }
            Loader {
                id: valueCell
                x: panel.valueX(cellRow.width)
                width: panel.valueWidth(cellRow.width)
                sourceComponent: cellRow.valueComponent
            }
            Label {
                id: typeCellLabel
                objectName: "detailCellType"
                x: panel.typeX(cellRow.width)
                width: panel.typeWidth()
                height: Type.rowHeight
                visible: panel.typeShown(cellRow.width)
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText; text: cellRow.cellType; elide: Text.ElideRight
                font.family: Type.fixedFamily; font.pointSize: Type.caption
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textSecondary
                onImplicitWidthChanged: panel.noteType(implicitWidth)
                Component.onCompleted: panel.noteType(implicitWidth)
                HoverHandler { id: typeHover }
                PlainToolTip { visible: typeHover.hovered && typeCellLabel.truncated; text: cellRow.cellType }
            }
        }
    }
    // The space the pane's scroll bar takes while it shows: R moves left by
    // it. Set a turn later, so the bar's coming and the content's width
    // never feed each other in one pass.
    property real barRoom: 0
    Timer {
        id: barTimer
        interval: 0
        // A style may keep the bar visible and only fade it: what counts is
        // whether the content is taller than the view.
        onTriggered: panel.barRoom = bodyBar.visible && bodyBar.policy !== ScrollBar.AlwaysOff
                                     && bodyBar.size < 1.0 ? bodyBar.width : 0
    }
    Flickable {
        id: bodyScroll
        objectName: "endpointBodyScroll"
        anchors.fill: parent
        clip: true
        interactive: contentHeight > height + 0.5
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: bodyColumn.height
        ScrollBar.vertical: ScrollBar {
            id: bodyBar
            objectName: "endpointBodyScrollBar"
            policy: ScrollBar.AsNeeded
            onVisibleChanged: barTimer.restart()
            onSizeChanged: barTimer.restart()
            onWidthChanged: barTimer.restart()
        }
        // The blocks from the top at their own heights, the top and bottom
        // padding included; the room under the last block stays empty.
        Item {
            id: bodyColumn
            objectName: "endpointBodyColumn"
            width: bodyScroll.width - panel.barRoom
            height: blocks.implicitHeight + 2 * Type.groupSpacing
            ColumnLayout {
                id: blocks
                y: Type.groupSpacing
                width: parent.width
                spacing: 0
                // The host's own header and footer follow the selection, not the
                // connection: they are its chrome, shown while the panel shows its
                // kind, connected or not.
                Loader {
                    id: hostHeader
                    Layout.fillWidth: true
                    active: panel.selectionMatches && panel.headerComponent !== null
                    visible: active
                    sourceComponent: panel.headerComponent
                }
                // Nothing selected: one sentence where the title would be.
                Label {
                    objectName: "emptyEndpointLabel"
                    visible: (panel.showSelection && !panel.hasSelection)
                             || (!panel.showSelection && panel.retainedText.length === 0)
                    Layout.fillWidth: true
                    Layout.topMargin: hostHeader.visible ? Type.groupSpacing : 0
                    Layout.leftMargin: Type.padding
                    Layout.rightMargin: Type.padding
                    Layout.minimumHeight: Type.controlHeight
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: panel.showSelection ? panel.emptyText
                        : panel.categoryFilter === 1 ? qsTr("Select a Field")
                        : panel.categoryFilter === 2 ? qsTr("Select a Command") : qsTr("Select a Service")
                    color: Theme.textMuted
                }
                ColumnLayout {
                    objectName: "endpointHeader"
                    visible: panel.showSelection && panel.hasSelection
                    Layout.fillWidth: true
                    Layout.topMargin: hostHeader.visible ? Type.groupSpacing : 0
                    Layout.leftMargin: Type.padding
                    Layout.rightMargin: Type.padding
                    spacing: 0
                    // The title row: the path on L, the actions ending on R. They
                    // share the row down to a title of six characters; only below
                    // that do the actions take their own row, at the right.
                    GridLayout {
                        id: selectionHeader
                        objectName: "endpointTitleRow"
                        Layout.fillWidth: true
                        readonly property real actionsWidth: panel.actionsComponent !== null ? hostActions.implicitWidth
                                                                                             : selectionActions.implicitWidth
                        readonly property bool wraps: width - actionsWidth - Type.groupSpacing < Type.bodyPixels * 6
                        columns: wraps ? 1 : 2
                        columnSpacing: Type.groupSpacing; rowSpacing: Type.tight
                        Label {
                            id: titleLabel
                            objectName: "endpointSelectionTitle"
                            textFormat: Text.PlainText; text: panel.title
                            font.weight: Type.headingWeight; color: Theme.textPrimary
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            Layout.preferredHeight: Type.controlHeight
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideMiddle
                            Accessible.role: Accessible.Heading
                            HoverHandler { id: titleHover }
                            PlainToolTip { delay: 700; visible: titleHover.hovered && titleLabel.truncated; text: panel.title }
                        }
                        Loader {
                            id: hostActions
                            objectName: "endpointHostActions"
                            active: panel.actionsComponent !== null
                            visible: active
                            sourceComponent: panel.actionsComponent
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            Layout.preferredHeight: Type.controlHeight
                        }
                        RowLayout {
                            id: selectionActions
                            objectName: "endpointSelectionActions"
                            visible: panel.actionsComponent === null
                            spacing: Type.groupSpacing
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            // The cells are the editor: a draft exists from the first
                            // change, and only then are there Cancel and Apply.
                            Button {
                                id: refreshButton
                                objectName: "refreshButton"
                                readonly property bool working: panel.providerBusy && panel.selectedCategory === 1
                                text: working ? qsTr("Reading…") : qsTr("Read")
                                visible: !panel.provider.commandSelected && !panel.provider.serviceSelected && !panel.provider.editing
                                enabled: !working && panel.provider.canRefresh === true; onClicked: panel.provider.refresh()
                                Layout.preferredHeight: Type.controlHeight
                                Layout.minimumWidth: Math.ceil(readWords.advanceWidth) + leftPadding + rightPadding
                                ActionMetrics { id: readWords; button: refreshButton; text: qsTr("Reading…") }
                                ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Read this Field from the device")
                            }
                            Button {
                                objectName: "cancelEditButton"; text: qsTr("Cancel")
                                visible: panel.provider.editing === true && !panel.provider.commandSelected && !panel.provider.serviceSelected
                                enabled: !panel.provider.busy; onClicked: panel.provider.cancelEdit()
                                Layout.preferredHeight: Type.controlHeight
                                ToolTip.visible: hovered; ToolTip.delay: 700
                                ToolTip.text: qsTr("Put back the values read; nothing is written")
                            }
                            AccentButton {
                                id: applyButton
                                objectName: "applyButton"
                                readonly property bool working: panel.providerBusy && panel.selectedCategory === 1
                                text: working ? qsTr("Applying…") : qsTr("Apply")
                                visible: panel.provider.editing === true && !panel.provider.commandSelected && !panel.provider.serviceSelected
                                enabled: !working && panel.provider.canApply === true; onClicked: panel.provider.apply()
                                Layout.preferredHeight: Type.controlHeight
                                Layout.minimumWidth: Math.ceil(applyWords.advanceWidth) + leftPadding + rightPadding
                                ActionMetrics { id: applyWords; button: applyButton; text: qsTr("Applying…") }
                                ToolTip.visible: hovered; ToolTip.delay: 700
                                ToolTip.text: qsTr("Write once, then read the device again (Ctrl+Enter). Undo does not take a write back.")
                                Accessible.description: ToolTip.text
                            }
                            AccentButton {
                                id: executeButton
                                objectName: "executeButton"
                                readonly property bool working: panel.providerBusy && panel.selectedCategory === 2
                                text: working ? qsTr("Executing…") : qsTr("Execute")
                                visible: panel.provider.commandSelected === true
                                enabled: !working && panel.provider.canExecute === true
                                onClicked: panel.provider.executeCommand()
                                Layout.preferredHeight: Type.controlHeight
                                Layout.minimumWidth: Math.ceil(executeWords.advanceWidth) + leftPadding + rightPadding
                                ActionMetrics { id: executeWords; button: executeButton; text: qsTr("Executing…") }
                                ToolTip.visible: hovered; ToolTip.delay: 700
                                ToolTip.text: qsTr("Send the Command once (Ctrl+Enter). Undo does not take it back.")
                                Accessible.description: ToolTip.text
                            }
                            AccentButton {
                                id: callButton
                                objectName: "callServiceButton"
                                readonly property bool working: panel.providerBusy && panel.selectedCategory === 3
                                text: working ? qsTr("Calling…") : qsTr("Call")
                                visible: panel.provider.serviceSelected === true
                                enabled: !working && panel.provider.canCallService === true
                                onClicked: panel.provider.callService()
                                Layout.preferredHeight: Type.controlHeight
                                Layout.minimumWidth: Math.ceil(callWords.advanceWidth) + leftPadding + rightPadding
                                ActionMetrics { id: callWords; button: callButton; text: qsTr("Calling…") }
                                ToolTip.visible: hovered; ToolTip.delay: 700
                                ToolTip.text: qsTr("Send one request (Ctrl+Enter). Undo does not take it back.")
                                Accessible.description: ToolTip.text
                            }
                        }
                    }
                    // Line 2: what the endpoint is, one line, the whole of it in
                    // the tooltip and the accessible name.
                    Label {
                        id: selectionInfoLabel
                        objectName: "endpointSelectionInfo"
                        Layout.fillWidth: true
                        Layout.topMargin: Type.tight
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        font.pointSize: Type.caption
                        color: Theme.textMuted
                        text: panel.selectionLine
                        Accessible.name: panel.selectionLine
                        Accessible.description: panel.contextHelp
                        HoverHandler { id: infoHover }
                        PlainToolTip {
                            objectName: "endpointSelectionInfoToolTip"
                            delay: 700
                            visible: infoHover.hovered && selectionInfoLabel.truncated
                            text: panel.selectionLine
                        }
                    }
                }
                // The status line: what happened last, or what is happening, once;
                // a failure has its glyph and the reason in the primary text colour.
                RowLayout {
                    objectName: "endpointStatus"
                    visible: (panel.showSelection && panel.hasSelection && panel.statusInfo.text.length > 0)
                        || panel.retainedText.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Type.tight
                    Layout.leftMargin: Type.padding
                    Layout.rightMargin: Type.padding
                    spacing: Type.spacing
                    ErrorGlyph {
                        objectName: "endpointStatusGlyph"
                        visible: panel.retainedText.length === 0 && panel.statusInfo.failed === true
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: Math.max(0, (statusLine.lineHeightPixels - height) / 2)
                    }
                    Label {
                        id: statusLine
                        objectName: "endpointStatusLine"
                        Layout.fillWidth: true
                        readonly property real lineHeightPixels: statusMetrics.height
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pointSize: Type.caption
                        text: panel.retainedText.length > 0 ? panel.retainedText : panel.statusInfo.text
                        color: panel.retainedText.length === 0 && panel.statusInfo.failed === true ? Theme.textPrimary : Theme.textSecondary
                        Accessible.name: text
                        Accessible.description: typeof panel.statusInfo.tip === "string" ? panel.statusInfo.tip : ""
                        FontMetrics { id: statusMetrics; font: statusLine.font }
                        HoverHandler { id: statusHover }
                        PlainToolTip {
                            delay: 700
                            readonly property string tip: typeof panel.statusInfo.tip === "string" ? panel.statusInfo.tip : ""
                            visible: statusHover.hovered && (tip.length > 0 || statusLine.truncated)
                            text: statusLine.truncated ? statusLine.text + (tip.length > 0 ? "\n" + tip : "") : tip
                        }
                        // The time, flashed once in the accent when an action ends:
                        // the same glyphs over the line's own, fading out.
                        Label {
                            id: statusTime
                            objectName: "endpointStatusTime"
                            readonly property var found: /\b\d{1,2}:\d{2}:\d{2}\b/.exec(statusLine.text)
                            readonly property bool fits: statusLine.lineCount === 1 && !statusLine.truncated
                            text: found ? found[0] : ""
                            x: found ? Math.round(statusPrefix.advanceWidth) : 0
                            y: 0
                            height: statusLine.lineHeightPixels
                            textFormat: Text.PlainText
                            font: statusLine.font
                            color: Theme.accent
                            opacity: 0
                            visible: text.length > 0 && fits
                            Accessible.ignored: true
                            TextMetrics {
                                id: statusPrefix
                                font: statusLine.font
                                text: statusTime.found ? statusLine.text.substring(0, statusTime.found.index) : ""
                            }
                        }
                        OpacityAnimator {
                            id: statusFlash
                            target: statusTime
                            from: 1
                            to: 0
                            duration: 600
                            easing.type: Easing.InQuad
                        }
                    }
                }
                // Why an action is off, with the way to turn it on: a routine state,
                // in the neutral container, read out as an alert.
                Rectangle {
                    id: refusalStrip
                    objectName: "endpointRefusal"
                    visible: panel.refusalText.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Type.groupSpacing
                    Layout.leftMargin: Type.padding
                    Layout.rightMargin: Type.padding
                    implicitHeight: Math.max(Type.controlHeight, refusalRow.implicitHeight + 2 * Type.tight)
                    radius: Type.radius
                    color: Theme.neutralContainer
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: panel.refusalText
                    RowLayout {
                        id: refusalRow
                        anchors.fill: parent
                        anchors.leftMargin: Type.groupSpacing
                        anchors.rightMargin: Type.tight
                        spacing: Type.padding
                        // No pictogram: a host that embeds this panel need not
                        // register the library's C++ types (ToolIcon) for it.
                        Label {
                            objectName: "endpointRefusalText"
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Theme.textPrimary
                            text: panel.refusalText
                        }
                        Button {
                            objectName: "endpointAllowChanges"
                            text: qsTr("Allow changes")
                            flat: true
                            enabled: !panel.provider.busy
                            // A provider may refuse, or keep the switch to itself.
                            onClicked: {
                                try {
                                    panel.provider.allowMutations = true
                                } catch (error) {
                                    console.warn("Changes could not be allowed: " + error)
                                }
                            }
                        }
                    }
                }
                // Section 1: a Field's value, a Command's arguments or a
                // Service's request - its heading row, its rows (or the quiet
                // line), its footnote.
                ColumnLayout {
                    id: valueSection
                    objectName: "valueSection"
                    visible: panel.showSelection && panel.hasSelection
                    Layout.fillWidth: true
                    Layout.topMargin: Type.sectionSpacing
                    spacing: 0
                    readonly property string sectionName: panel.selectedCategory === 1 ? ""
                        : panel.selectedCategory === 3 ? qsTr("Request") : qsTr("Arguments")
                    TableHeading {
                        id: valueHeading
                        objectName: "valueTableHeader"
                        Layout.fillWidth: true
                        title: valueSection.sectionName
                        bare: panel.noArguments
                        view: panel.showMetadata && metadataLoader.item ? metadataLoader.item.table : valueTree
                    }
                    QuietLine {
                        objectName: "noArgumentsLabel"
                        visible: panel.noArguments
                        text: panel.provider.serviceSelected ? qsTr("No request values.") : qsTr("No arguments.")
                    }
                    Loader {
                        id: metadataLoader
                        objectName: "metadataTableSlot"
                        active: panel.showMetadata && panel.metadataFresh; visible: active
                        Layout.fillWidth: true
                        Layout.preferredHeight: item ? item.table.contentHeight : 0
                        sourceComponent: Component {
                          Item {
                            readonly property alias table: metadataTree
                            DetailTree {
                                id: metadataTree
                                objectName: "metadataTree"
                                windowTop: blocks.y + valueSection.y + metadataLoader.y
                                section: ""
                                headed: true
                                Keys.onPressed: function(event) { event.accepted = panel.treeKey(metadataTree, event) }
                                // One root per table (spec A): the Field's own row, open,
                                // its members under it and folded below that. The table's
                                // root is the Field's parent, so the Field is row 0. Set on
                                // the next turn: showing the rows loads them, which must not
                                // happen inside a host's selection.
                                readonly property var definitionRoot: panel.provider.definitionRootIndex
                                function showDefinition() {
                                    rootIndex = definitionRoot
                                    fieldRoot.restart()
                                }
                                onDefinitionRootChanged: showDefinition()
                                // Two Fields of one group share their root.
                                Connections { target: panel; function onSelectedPathChanged() { metadataTree.showDefinition() } }
                                // A filter change keeps the table's expanded set while the rows
                                // under it are made again, and the members show twice: the
                                // Field's row is opened again after every change.
                                Connections {
                                    target: metadataTree.model
                                    ignoreUnknownSignals: true
                                    function onRowsInserted() { fieldRoot.restart() }
                                    function onRowsRemoved() { fieldRoot.restart() }
                                    function onLayoutChanged() { fieldRoot.restart() }
                                    function onModelReset() { fieldRoot.restart() }
                                }
                                Component.onCompleted: fieldRoot.restart()
                                Timer {
                                    id: fieldRoot
                                    interval: 0
                                    onTriggered: panel.openDefinitionRoot(metadataTree)
                                }
                                model: panel.provider.definitions; rootIndex: definitionRoot
                                activeFocusOnTab: true
                                selectionModel: ItemSelectionModel { model: metadataTree.model }
                                delegate: DetailRow {
                                    id: metadataDelegate
                                    objectName: "endpointRow"
                                    required property string name
                                    required property string detail
                                    required property bool isEndpoint
                                    readonly property bool twoLines: false
                                    readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                                    cellName: name; cellType: typeText; cellValue: qsTr("Not read")
                                    cellHint: detail + "\n" + panel.contextHelp
                                    container: depth === 0
                                    // Not read: "–" where each value will be, at its
                                    // alignment; an open branch shows nothing there.
                                    valueComponent: Label {
                                        objectName: "metadataValueLabel"
                                        height: Type.rowHeight
                                        verticalAlignment: Text.AlignVCenter
                                        text: metadataDelegate.hasChildren && metadataDelegate.expanded ? "" : "–"
                                        font.family: Type.fixedFamily
                                        color: metadataDelegate.highlighted ? Theme.selectedForeground : Theme.textMuted
                                        horizontalAlignment: metadataDelegate.hasChildren
                                            || /^(?:[usi](?:8|16|32|64)|f(?:32|64)|bool)$/.test(metadataDelegate.typeText)
                                            ? Text.AlignRight : Text.AlignLeft
                                        Accessible.description: qsTr("Not read")
                                    }
                                }
                            }
                          }
                        }
                    }
                    Item {
                      id: valueSlot
                      objectName: "valueTableSlot"
                      visible: !panel.showMetadata && !panel.noArguments
                      Layout.fillWidth: true
                      Layout.preferredHeight: valueTree.contentHeight
                      DetailTree {
                        id: valueTree
                        objectName: "valueTree"
                        windowTop: blocks.y + valueSection.y + valueSlot.y
                        section: panel.headed ? "request" : "value"
                        headed: panel.headed
                        model: panel.provider.values
                        rootIndex: {
                            void panel.valueModelRevision
                            void panel.valueRowsRevision
                            return panel.contentRoot(panel.provider.values, panel.headed)
                        }
                        activeFocusOnTab: true
                        editTriggers: TableView.NoEditTriggers
                        Keys.onPressed: function(event) {
                            if (panel.treeKey(valueTree, event))
                                event.accepted = true
                            else
                                panel.openCurrent(valueTree, event)
                        }
                        selectionModel: ItemSelectionModel { model: valueTree.model }
                        delegate: DetailRow {
                            id: valueDelegate
                            objectName: "valueRow"
                            toolTipName: "valueToolTip"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            required property bool editable
                            required property string editorText
                            readonly property var summaryParts: model.summaryParts !== undefined ? model.summaryParts : []
                            readonly property string summaryKind: typeof model.summaryKind === "string" ? model.summaryKind : ""
                            readonly property int itemCount: typeof model.itemCount === "number" ? model.itemCount : -1
                            readonly property string rowPath: typeof model.path === "string" ? model.path : ""
                            readonly property int unsentRows: typeof model.unsentCount === "number" ? model.unsentCount : (model.unsent === true ? 1 : 0)
                            readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                            readonly property string canonicalText: typeof model.canonicalText === "string" ? model.canonicalText : ""
                            readonly property string editorKind: typeof model.editorKind === "string" ? model.editorKind : "text"
                            readonly property var editorChoices: model.editorChoices !== undefined ? model.editorChoices : []
                            readonly property bool choicesTruncated: model.editorChoicesTruncated === true
                            readonly property string takes: typeof model.takes === "string" ? model.takes : ""
                            readonly property bool stale: model.stale === true
                            readonly property string staleNote: typeof model.staleNote === "string" ? model.staleNote : ""
                            readonly property bool updated: model.updated === true
                            readonly property bool rootRow: isAggregate && !panel.headed && depth === 0
                            cellName: rootRow ? panel.lastSegment(panel.provider.selection, name) : name
                            cellType: typeText; cellHint: detail; cellValue: value
                            cellPath: rowPath
                            pathPrefix: panel.headed ? panel.provider.selection + (panel.provider.serviceSelected ? qsTr(" request: ") : qsTr(" arguments: "))
                                                      : panel.provider.selection
                            container: rootRow
                            unsent: model.unsent === true
                            unsentCount: unsentRows
                            unsentWords: unsentRows <= 0 ? ""
                                : panel.headed ? (unsentRows === 1 ? qsTr("1 value not yet sent") : qsTr("%L1 values not yet sent").arg(unsentRows))
                                : (unsentRows === 1 ? qsTr("1 value edited, not applied") : qsTr("%L1 values edited, not applied").arg(unsentRows))
                            TableView.onReused: if (valueItem && typeof valueItem.reset === "function") {
                                valueItem.reset()
                                valueItem.maybeFlash()
                            }
                            valueComponent: ValueCell {
                                objectName: "valueCell"
                                editorRoom: valueDelegate.valueRoom
                                name: valueDelegate.name
                                value: valueDelegate.value
                                typeText: valueDelegate.typeText
                                detail: valueDelegate.detail
                                isAggregate: valueDelegate.isAggregate
                                childCount: valueDelegate.childCount
                                loadedCount: valueDelegate.loadedCount
                                expanded: valueDelegate.expanded
                                summaryParts: valueDelegate.summaryParts
                                summaryKind: valueDelegate.summaryKind
                                itemCount: valueDelegate.itemCount
                                restRoom: valueDelegate.restRoom
                                captionRoom: valueDelegate.captionRoom
                                captionRightRoom: valueDelegate.captionRightRoom
                                editable: valueDelegate.editable
                                editorText: valueDelegate.editorText
                                editorKind: valueDelegate.editorKind
                                editorChoices: valueDelegate.editorChoices
                                choicesTruncated: valueDelegate.choicesTruncated
                                canonicalText: valueDelegate.canonicalText
                                takes: valueDelegate.takes
                                stale: valueDelegate.stale
                                staleNote: valueDelegate.staleNote
                                highlighted: valueDelegate.highlighted
                                updated: valueDelegate.updated
                                updatedAt: panel.valuesResetAt
                                reducedMotion: panel.reducedMotion
                                // Changes off: the value shows, nothing takes input,
                                // and the strip above says why.
                                locked: panel.changesOff || panel.provider.busy
                                setText: function(text) {
                                    return panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                }
                                loadMore: function() {
                                    panel.provider.values.fetchMore(valueTree.modelIndex(Qt.point(0, valueDelegate.row)))
                                }
                                onTabbed: function(forward) { panel.openNext(valueTree, valueDelegate.row, forward) }
                                onFinished: function(byKey) {
                                    // A provider whose values are steps (the Batch's item)
                                    // starts the next change of this value as a new one.
                                    if (typeof panel.provider.closeValue === "function")
                                        panel.provider.closeValue()
                                    if (byKey)
                                        valueTree.forceActiveFocus()
                                }
                            }
                        }
                      }
                    }
                    // Under the table, never between heading and rows: where a
                    // request's values start, where a write's start, or which
                    // read the values on show are from.
                    Footnote {
                        id: valueNote
                        objectName: "valueSectionCaption"
                        readonly property bool stale: !!panel.provider.values && panel.provider.values.stale === true
                                                      && typeof panel.provider.values.staleNote === "string"
                                                      && panel.provider.values.staleNote.length > 0
                        visible: !panel.noArguments && text.length > 0
                        text: stale ? panel.provider.values.staleNote
                            : panel.selectedCategory !== 1 ? qsTr("Start at zero, not at the device's defaults.")
                            : typeof panel.provider.valueNote === "string" ? panel.provider.valueNote : ""
                    }
                }
                // Section 2: a Service's response, under its request at every
                // width; its structure before a call, "–" in every value.
                ColumnLayout {
                    id: responseSection
                    objectName: "responseSection"
                    visible: panel.showResponse
                    Layout.fillWidth: true
                    Layout.topMargin: Type.sectionSpacing
                    spacing: 0
                    readonly property bool hasRows: {
                        void panel.responseModelRevision
                        void panel.responseRowsRevision
                        return panel.responseMatches && !!panel.provider.responses && typeof panel.provider.responses.rowCount === "function"
                            && panel.provider.responses.rowCount() > 0
                    }
                    // A response from before the request changed, or from before
                    // a call that failed: muted, with a note under it.
                    readonly property bool stale: !!panel.provider.responses && panel.provider.responses.stale === true
                    TableHeading {
                        id: responseTableHeader
                        objectName: "responseTableHeader"
                        Layout.fillWidth: true
                        title: qsTr("Response")
                        bare: panel.noResponseValues
                        view: responseTree
                        Accessible.description: typeof panel.provider.serviceResponseEndpoint === "string"
                                                ? panel.provider.serviceResponseEndpoint : ""
                    }
                    QuietLine {
                        objectName: "noResponseLabel"
                        visible: panel.noResponseValues
                        text: qsTr("No response values.")
                    }
                    Item {
                      id: responseSlot
                      objectName: "responseTableSlot"
                      visible: responseSection.hasRows && !panel.noResponseValues
                      Layout.fillWidth: true
                      Layout.preferredHeight: responseTree.contentHeight
                      DetailTree {
                        id: responseTree
                        objectName: "responseTree"
                        windowTop: blocks.y + responseSection.y + responseSlot.y
                        section: "response"
                        headed: true
                        model: panel.provider.responses
                        rootIndex: {
                            void panel.responseModelRevision
                            return panel.contentRoot(panel.provider.responses, true)
                        }
                        activeFocusOnTab: true
                        Keys.onPressed: function(event) { event.accepted = panel.treeKey(responseTree, event) }
                        selectionModel: ItemSelectionModel { model: responseTree.model }
                        delegate: DetailRow {
                            id: responseDelegate
                            objectName: "responseRow"
                            toolTipName: "responseToolTip"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            readonly property var summaryParts: model.summaryParts !== undefined ? model.summaryParts : []
                            readonly property string summaryKind: typeof model.summaryKind === "string" ? model.summaryKind : ""
                            readonly property int itemCount: typeof model.itemCount === "number" ? model.itemCount : -1
                            readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                            readonly property bool stale: model.stale === true
                            readonly property string staleNote: typeof model.staleNote === "string" ? model.staleNote : ""
                            readonly property bool updated: model.updated === true
                            cellName: name; cellType: typeText; cellHint: stale && staleNote.length > 0 ? staleNote : detail
                            cellValue: value
                            cellPath: typeof model.path === "string" ? model.path : ""
                            pathPrefix: panel.provider.selection + qsTr(" response: ")
                            container: false
                            TableView.onReused: if (valueItem && typeof valueItem.maybeFlash === "function") valueItem.maybeFlash()
                            valueComponent: ValueCell {
                                objectName: "responseCell"
                                name: responseDelegate.name
                                value: responseDelegate.value
                                typeText: responseDelegate.typeText
                                detail: responseDelegate.detail
                                isAggregate: responseDelegate.isAggregate
                                childCount: responseDelegate.childCount
                                loadedCount: responseDelegate.loadedCount
                                expanded: responseDelegate.expanded
                                summaryParts: responseDelegate.summaryParts
                                summaryKind: responseDelegate.summaryKind
                                itemCount: responseDelegate.itemCount
                                restRoom: responseDelegate.restRoom
                                captionRoom: responseDelegate.captionRoom
                                captionRightRoom: responseDelegate.captionRightRoom
                                stale: responseDelegate.stale
                                staleNote: responseDelegate.staleNote
                                highlighted: responseDelegate.highlighted
                                updated: responseDelegate.updated
                                updatedAt: panel.responsesResetAt
                                reducedMotion: panel.reducedMotion
                                loadMore: function() {
                                    panel.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, responseDelegate.row)))
                                }
                            }
                        }
                      }
                    }
                    Footnote {
                        objectName: "responseStaleNote"
                        visible: responseSection.hasRows && responseSection.stale && text.length > 0
                        text: !!panel.provider.responses && typeof panel.provider.responses.staleNote === "string"
                              ? panel.provider.responses.staleNote : ""
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.topMargin: active ? Type.sectionSpacing : 0
                    active: panel.selectionMatches && panel.footerComponent !== null
                    visible: active
                    sourceComponent: panel.footerComponent
                }
            }
        }
    }
    // Work under way: one line over the top padding, never a spinner, and no
    // height of its own.
    ProgressLine {
        objectName: "endpointProgress"
        x: 0
        y: 0
        z: 2
        width: panel.width
        visible: panel.progressShown
        running: panel.progressShown && panel.providerBusy
        reducedMotion: panel.reducedMotion
        description: panel.statusInfo.text
    }
    Timer { id: valueRevision; interval: 0; onTriggered: ++panel.valueRowsRevision }
    Timer { id: responseRevision; interval: 0; onTriggered: ++panel.responseRowsRevision }
    Connections {
        target: panel.provider.values
        function onModelReset() {
            ++panel.valueModelRevision
            panel.valuesResetAt = Date.now()
            rootFetch.restart()
            Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
        }
        // After the change settles: a view that fetches rows while it takes its
        // root index would otherwise re-evaluate that very binding.
        function onRowsInserted() { valueRevision.restart() }
        function onRowsRemoved() { valueRevision.restart() }
    }
    Connections {
        target: panel.provider.responses
        function onModelReset() {
            ++panel.responseModelRevision
            panel.responsesResetAt = Date.now()
            rootFetch.restart()
            Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
        }
        function onRowsInserted() { responseRevision.restart() }
        function onRowsRemoved() { responseRevision.restart() }
    }
    Connections {
        target: panel.provider
        function onReadFinished(published) {
            ++panel.valueModelRevision
            if (published) {
                rootFetch.restart()
                Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
            }
        }
    }
}
