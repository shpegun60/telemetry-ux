# Labels, units and editor hints

Supply one optional presentation document to the existing Web or Qt Configurator.
It adds readable labels, units and help without changing the device Descriptor,
generated DTOs, wire values, permissions or connection ownership. The same JSON
works in both frontends.

Ranges and steps are **advisory hints only** in version 1. They do not block Apply,
clamp a value, scale a unit or replace the SDK's type and canonical-wire checks.
Put required validation in the device endpoint. The interface identifies range
and step text as advisory rather than promising enforcement.
No default value is inferred or written.

## A small document

```json
{
  "format": "telemetry-presentation",
  "version": 1,
  "fingerprint": "0x0123456789abcdef",
  "entries": [
    {
      "category": 1,
      "id": 42,
      "role": "value",
      "path": ["threshold"],
      "label": "Trip threshold",
      "unit": "V",
      "help": "The device checks the permitted operating range.",
      "minimum": "0",
      "maximum": "250.5",
      "step": "0.1"
    }
  ]
}
```

The identity, Field ID and member above are examples. Use your own schema's
fingerprint and numeric endpoint ID, available from the generated structural
metadata or the connected Session. Do not copy an example fingerprint into a
different device's document. The fingerprint is an exact 64-bit schema identity,
written as `0x` followed by 16 lowercase hexadecimal digits.

`path: []` describes the root value. A struct member uses its original string
name; an array element uses a numeric index, for example `["phases", 0, "voltage"]`.
Strings containing a dot remain one member name. The string `"0"` and index `0`
are different path components. Labels never change these paths or endpoint IDs.

| Category | Role | Applies to |
| --- | --- | --- |
| `1` Field | `value` | Current value and local Field draft |
| `2` Command | `request` | Command arguments |
| `3` Service | `request` | Service arguments |
| `3` Service | `response` | Service response |

Two endpoint categories may contain the same numeric ID. A document therefore
always includes category, ID, role and path; names alone are not selectors.
There are no wildcard paths, type-wide rules, translations, unit conversions,
layout controls or enum-code remapping in this version.

## Web

Load the document once and pass the same object to each panel. The host chooses
where to read it; the library does not fetch a file or contact the device for hints.

```js
import {mountConfigurator} from 'telemetry-ux/web';

const response = await fetch('./presentation.json');
if (!response.ok) throw new Error('Cannot load presentation.json');
const presentation = await response.json();
const configuration = mountConfigurator(document.querySelector('#configuration'), {
    session, presentation
});
const measurements = mountConfigurator(document.querySelector('#measurements'), {
    session, presentation
});
```

Both panels keep their own selection and draft while borrowing the same Session
and FIFO. Loading or mounting metadata submits no read, write, Command or Service.
The existing call without `presentation` keeps the original UI behaviour.

## Qt

Read the same JSON into a `QByteArray` and set it once on the existing binding,
before or after creating panels:

```cpp
#include <Telemetry/UX/QtConnectionBinding.hpp>
#include <QtCore/QFile>

telemetry::ux::QtConnectionBinding presentation{&connection};
QFile hints{QStringLiteral("presentation.json")};
if (!hints.open(QIODevice::ReadOnly) || !presentation.setPresentation(hints.readAll()))
    return 1; // Report the file error or presentation.presentationError() in your host.

auto* configuration = presentation.createProvider();
auto* measurements = presentation.createProvider();
```

The binding shares the parsed document with its existing and future providers.
It does not create another connection, client or timer. Advanced applications
can call the same `setPresentation(const QByteArray&)` method directly on a
`DynamicConfiguratorProvider` or `StaticConfiguratorProvider`.

`setPresentation()` returns `false` for a malformed replacement, leaves the old
document intact and sets `presentationError()`. An empty `QByteArray` clears the
optional document. Call it on the provider's Qt owner thread.

## Identity, display and failure rules

A syntactically valid document can be loaded before connecting. Hints apply only
while the displayed schema has the matching fingerprint. A mismatch hides hints;
it does not reject an otherwise valid connection or trigger a fallback, reconnect
or request. Replacing the Descriptor or retiring the session cannot apply old
hints to a new identity. Unknown endpoint IDs and paths have no effect.

Labels replace display names only; original identifiers remain available in
details. Units appear beside displayed values, never inside editable numeric text
or encoded payloads. Help, units, labels and advisory numbers are plain text.
They do not load markup, images or scripts. A label cannot make an endpoint
writable or enable a hidden operation.

`minimum`, `maximum` and `step` are decimal **strings**, including for U64/S64.
For example, `"18446744073709551615"` is not converted through a JavaScript
Number or QML Number. Input and output values keep the existing exact-text path.
Version 1 checks hint syntax only: it does not compare the stated minimum with
the maximum, test a hint against the wire type, or validate a step grid.

The parser accepts only the properties described here. Unknown properties,
duplicate selectors and incorrectly typed or oversized properties reject the
entire document before it is used. Duplicate selectors mean identical category,
ID, role and typed path, even
when their labels differ. An unmatched selector is harmless; it is not a request
to add a missing endpoint or create a value.

| Item | Version 1 limit |
| --- | --- |
| `entries` | At most 4096 |
| `path` | At most 64 components |
| Endpoint ID and numeric path component | Integer `0` through `4294967295` |
| `label` | At most 256 UTF-16 code units |
| `unit` | At most 64 UTF-16 code units |
| `help` | At most 4096 UTF-16 code units |
| Each numeric hint | At most 128 UTF-16 code units |

Each entry supplies at least one hint property. Label, unit and help may be empty
strings. Numeric strings use
`^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?$`: no whitespace, exponent, leading plus,
leading zeroes, NaN or infinity. `step` must additionally be positive and nonzero.
No optional property is represented by `null`; omit it instead.

The document is host presentation data, not device validation policy. Keep it
alongside your application configuration and review it with the matching Model.
The [shared parser cases](../tests/presentation_metadata/cases.json) exercise the
same versioned syntax in Web and Qt.
