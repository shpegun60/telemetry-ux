# Device-tree previews

Implemented for Session-backed Dynamic Qt providers on 14 November 2026. The
presentation below records the design and its bounds. Maintained checks and
their coverage are described in [the test guide](../tests/endpoint_previews/README.md).
Static and third-party providers retain their existing text-editor fallback.

## UX design

The Device tree remains a compact browser. Previewing a value does not select an
endpoint, change an editor draft, change the Watchlist or enable a mutation.
Only explicit endpoint activation follows the host's existing Details route.

**One quiet toolbar.** Keep Find in its current place and put a compact
**Preview values** toggle beside it. It starts on for a provider that supports
previews and is absent for providers without that capability. Turning it off
releases this tree's demand; metadata stays visible. The control's description
says that it reads visible Fields slowly while this tree is shown. It is not a
Watch polling control.

**Rows.** Use the existing `Theme` and `Type` roles, tree chevron, watch checkbox
and selection/focus treatment. No additional cards, status badges or per-row
buttons. Names and values are plain text. Values use `Type.fixedFamily`, arrive
as exact strings and are never converted through a QML number.

- Every width uses one compact row with **Name, Value, Type, Access**
  columns, matching the Values viewer's table hierarchy. Only Name contains
  depth indentation, the chevron and Watch checkbox. Deep indentation is
  bounded within Name so it cannot cover Value or consume all label space.
- Header separators resize columns with the mouse; a focused header supports
  Left/Right to resize and Home to reset. Double-clicking a separator also
  restores that column. Name absorbs spare space until explicitly resized.
- Narrow panels scroll the same logical table horizontally instead of
  wrapping every Field into multiple lines. Headers and rows scroll together.
  A long name, type or value is elided, with its full plain text available in
  the cell tooltip, accessible description and existing Details route.
- Type names use the same schema formatter as Values/Descriptor. Field access
  uses the two-letter marks `r-`, `rw`, `-w` or `--`, derived from descriptor
  permissions. The full words remain in the tooltip and accessible description;
  Commands and Services retain their operation names. Unit comes from presentation
  metadata, and is empty when unknown. None is inferred from the value's text.
- Commands and Services show their schema type/signature and category. They
  never receive a preview demand or an execute/call action from this view.

**States.** Never-read values show an em dash with **Not read**. Waiting and
in-flight work say **Queued…** and **Reading…**, without a spinner on every row.
An available cached value stays legible while this deliberately slow preview
waits for its next refresh. Cache age and TTL are not displayed, and TTL alone
does not dim a valid value. Refusal/error text explains an unavailable value;
zero, an empty success cell or a green dot must not stand in for missing data.
Read/error states and refusal reasons remain in the Value tooltip and accessible
description rather than a permanent status column. Detailed IDs, byte
counts, enum codes, limits and help belong in Details or the row description,
not in a permanent column on every row.

A Field beyond the preview byte/expansion budget still visibly says **Open
Details**, even though it is not eligible for a preview request. That reason
takes priority over **Expand to preview** on a collapsed aggregate. A bounded
cache refusal says **Preview limit**. Neither state invents a value or silently
turns the preview back on.

**Structures and arrays.** A collapsed aggregate shows its type/member count
without requesting its payload. Expanding it requests its owning Field once;
all member and element rows share that owned snapshot. Nested rows are read-only
value rows, not new endpoints or Watch candidates. Long arrays use the existing
Values-style bounded ranges, such as `[0 … 255]`, then materialize elements only
when a range is opened. Neither child creation nor model-role access sends I/O.

**Visibility and search.** Only the normal tree's actually visible rows contribute
demand, including visible children whose owning Field has scrolled out of view.
Collapsed branches, offscreen rows, a hidden dock, an inactive tab and destruction
release future demand. Accepted work may finish through the existing lifecycle.
Find searches metadata only and releases this view's preview demand. Typing,
moving through results and reading model roles send no device request. Search
does not restart because only a preview value or age changed.

**Input.** Preserve the existing keyboard tree navigation, Enter-to-Details,
Space-to-Watch, context menu and draft-discard guard. A preview is never an
implicit editor activation. The preview toggle is reachable by Tab, exposes its
checked state and has a visible focus treatment. State/type/value descriptions
are available without hovering.

**Acceptance.** Inspect actual native captures at 320, 600 and 900 px and enlarged
font zoom, with exact U64/S64, unknown enum, long names, unavailable/stale values,
nested structures and a large array. Check that groups retain compact height,
no text overlaps, focus and watch-box geometry stay legible, and the Details
draft is unchanged by preview expansion/scrolling. Request-count tests must
separately prove that hidden/search views read nothing, aggregates share one
Field result, and stale/session changes do not publish a value from another
device. This is the design checklist; the test guide distinguishes the exercised
host scenarios from further hardware and visual qualification.

## Typed Details editors

The shared Field/Command/Service editor uses the host's normal styled ComboBox
for Boolean and enum values. A Boolean choice reads `false (0)` or `true (1)`.
Enum choices show the name and exact numeric code; only that code string reaches
the setter. Unknown enum codes remain exact and visibly unknown. **Enter code…**
switches to the existing numeric text input; the return-to-list action explicitly
discards that entered code. A limited choice list says that another code can be
entered. No enum is treated as closed merely because its name is absent.

Definitions, request drafts and owned responses use **Name, Value, Type**
columns with the same quiet hierarchy as Values. Narrow Details scrolls a
minimum-width table horizontally so exact U64/S64 and floating editors retain
usable space; editor rows reserve the native control's height. Access, byte
size and the draft/read state appear once above the table. Request and Response
stack in narrow panels; the uncalled response remains compact.
The backend's `canonicalText` provides **Encoded: … (f32/f64)** beneath a float
editor. This is the prepared typed representation, not a claim of successful
transmission or storage. The user's original text, including incomplete input,
stays separate and is not rounded by JavaScript. Invalid input is stated
explicitly and must not become an implicit successful value.

For a Field not read yet, Details shows descriptor type/access/byte information
and a local definition tree. Its empty value is **Not read**, not a payload
borrowed from preview cache. Expanding definitions sends nothing. The first
successful explicit read replaces this definition view with the owned value.

Connected Details shows outcomes for the selected operation category and a
Service response only for its matching endpoint. Field Apply/Cancel controls do
not appear on a Service or Command request. Disconnected panels retain relevant
historical mutation outcomes, including OutcomeUnknown, with the existing
Previous connection caption; they expose no mutation actions or response body.

Typed Watch editing and bounded Boolean/enum graph presentation are described
in [Categorical signals](CategoricalSignals.md). Numeric graph coordinates
never imply completed commands or replace exact raw history.
