# Typed values and compact tree-tables

The Qt panels use the same type scale and palette as the Values file viewer.
Device navigation has separate **Name, Value, Type, Access** columns;
hierarchy changes only Name's indentation. Header separators resize with the
mouse or Left/Right keys; Home or a double-click restores a column. Narrow docks
scroll horizontally instead of turning every Field into a paragraph.
Field access uses two fixed letters like Device files: `r-`, `rw`, `-w` or `--`.
The full permission words remain in the tooltip and accessible description.
This tree deliberately refreshes slowly. It shows no age or routine TTL-stale
label; read failures remain available in the value tooltip. Session/schema
invalidation still removes values through the existing guarded cache.

Details uses **Name, Value, Type** for definitions, request drafts and owned
responses. A definition is available before reading and shows an em dash for
its missing value. Selecting or expanding that definition sends no request.
Access, byte size and the current draft/read state appear once above the table.
The `?` button provides operation guidance by mouse or keyboard. An uncalled
Service has a compact empty response; an owned response has its own usable,
scrollable section. Small panels stack Request and Response.

## Editors

The native Dynamic provider exposes optional `editorKind`, `editorText`,
`editorChoices`, `editorChoicesTruncated`, `typeText` and `categorical` Watch
roles. Boolean choices show `false (0)` and `true (1)`. Enum choices show a name
and its exact signed or unsigned code; unknown codes remain editable through
**Enter code**. At most 256 named choices are copied. A limited dictionary is
marked as limited, not treated as complete.

Watch editing starts from exact owned bits, never from a plotted double or a
formatted label. Choosing a value changes a local draft. **Write** or Enter
with the popup closed submits it explicitly; Escape or leaving the editor
cancels it. The active Watch editor can use a second row so a long numeric code
has usable space. Ordinary Watch and Device-tree rows remain compact.

Details keeps the text typed by the user separate from the prepared value.
For example, F32 input `0.1` can show **Encoded: 0.100000001 (f32)** below the
input. That caption appears when the prepared float differs from the edited
text, or when validation needs attention. It does not report device acceptance.
U64/S64 and enum codes remain exact strings throughout editing.

Providers without these optional roles retain the text editor. This does not
claim typed-editor parity for every older Static provider.

## Outcome emphasis

The Dynamic provider also exposes three optional notifying Boolean properties:

| Property | Attention is needed when |
| --- | --- |
| `writeOutcomeNeedsAttention` | The write failed, its result is uncertain, or the confirming readback was not published |
| `commandOutcomeNeedsAttention` | Execution was not confirmed, including `Accepted`, refusal and `OutcomeUnknown` |
| `serviceOutcomeNeedsAttention` | The call failed or its expected response could not be published; successful Void is not an error |

These flags come from typed operation/model results, not from parsing the
English outcome text. Details uses a neutral colour for a confirmed result and
the warning colour when attention is needed. A false flag alone does not prove
completion: an accepted operation starts with a pending fact and a false flag.
Disconnect preserves each terminal flag together with its historical outcome.
An uncertain result does not establish whether the device changed anything and
does not trigger an automatic retry.

Older providers may omit these properties. Their outcomes retain the previous
warning-colour fallback; implementing the new properties is optional.

## Live trends

Boolean and safely plottable enum curves use step/hold geometry: the last
received code is held until the next observation, with a break at a failed or
missing observation. Transitions between device reads remain unknown. Numeric
curves keep their existing interpolation. Bool readouts include the code;
enum readouts retain the received label/code rather than naming a transformed
numeric value as another enum state.

Live history retains at most 512 observations per series. Step rendering can
add corners, at most 1023 display vertices; those corners are not observations
and cause no device reads. Durable SQLite history is an application concern,
implemented separately by `telemetry-qt-client`.

All panels share the existing host Session and FIFO. These presentation changes
add no transport, polling loop, automatic mutation retry or wire operation.
