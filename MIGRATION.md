# Generator source provenance

The QML emitters were moved from `shpegun60/telemetry`, revision
`4d7afbb48476b5e73c179105f89d6f639bd7bf9c`, directory
`tools/telemetry_codegen/generators`, into `python/telemetry_ux_codegen`.
The original copyright and MIT license are retained.

| Original file | Original Git blob |
| --- | --- |
| `qml.py` | `56ebb57bab3641502d70fe69dc1d5e2bf3a52631` |
| `qml_client.py` | `fc97a7b961cbc8a57ad881a7d7fde4027af6f255` |
| `qml_js.py` | `be353918f9bef717038f4e4c473e79b96ba13dca` |
| `qml_macros.py` | `0e40ba0df649542401781aba47cca67c41aa6e56` |
| `qml_rpc.py` | `0bc06f441c8830d1e585230d51a6b41736c124de` |
| `qml_values.py` | `5b89505bf40dbe7111c2c9d8fdd74cd957776971` |

The initial move changes private SDK helper imports into imports from the
versioned public `emitter_api`. It does not rewrite generated Qt classes,
conversion rules, pending bookkeeping, RPC integration or wire formats.
There is one QML emitter implementation. Parsing, SDK generation and guarded
publication remain owned by the SDK.

The SDK and UX are separate repositories and packages. Matching exact
development versions and emitter API/IR versions are checked at generation.
Installed consumers must not depend on a sibling checkout or a source fallback.

## Inspector application

The single Inspector application and its QML/TCP suites move from SDK revision
`3a66a10f7742ac6ddaaef0f181a09bfc71b7d82e` to `apps/inspector` and the two
matching `tests` directories here. The old SDK implementation is removed by its
coordinated migration PR; no forwarding application or second controller is
retained. The executable name, QML URI and operation/lifecycle behavior remain
unchanged. Only the former disabled-UART diagnostic changes to name the installed
transport component.

The normal application consumes an installed SDK. QML/TCP tests record the
actual installed headers/libraries and SDK metadata separately from their UX
sources. SDK-side serial GUI tests remain an explicit cross-repository gate
because their PTY peer uses the actual NUCLEO firmware Protocol and private
COBS profile. Those firmware fixtures are not copied into UX or required by a
normal application build. Historical hardware receipts retain their original
paths, revisions and bytes; this move makes no new physical-board claim.

## Reusable QML view

`lib/qml/Telemetry/UX/Configurator.qml` extracts the Inspector's existing endpoint,
value, editor, Service response and Watchlist presentation. The application keeps
its connection controls and supplies the same controller explicitly. Both hosts
use the same view; no second client, queue, snapshot store or schema engine is
introduced. Existing object names and user action routing are preserved.

The static `TelemetryUX::configurator` package installs the QML view, public Qt
dependencies and native resource objects. Its independent package gate hides the
producer source/build tree, relocates the prefix and loads the view from a plain
Qt host with a user-owned provider. This verifies view packaging, not generated
typed-client integration or new physical hardware behavior.

The Widgets playground and other example UI consumers have separate migration
gates. This view extraction does not claim those later migrations are complete.

## Browser presentation and starter

The browser provider and renderer are UX-owned ES modules. Static mode imports
the generated JavaScript SDK and structural metadata; dynamic mode uses the
SDK's admitted DynamicClient and validated runtime schema. Both supply one
presentation contract to the same renderer, retaining the host's existing
client, FIFO and transport ownership. No Descriptor parser, payload codec or
retry engine is copied into UX.

The optional `generate_web_starter` extension uses the same frozen SDK schema
and guarded CLI publication as the QML emitters. It generates three thin entry
files, not another form implementation. Installed-package browser tests exercise
both actual clients against a persistent native Model and Encoded dispatcher;
they do not establish a physical-device, Ethernet or firmware HTTP port.

## Generated QML example and qualification

The generated-QObject example and its QML qualification suite originate in SDK
revision `6e15220b1271139b3f1996a16d385c660f856f45`, directories
`examples/codegen_qml` and `tests/codegen_qml`. Their new application-owned
`examples/codegen_qml/fixture` retains the demo DTOs and endpoint definitions from
the SDK's codegen and structured-client fixtures, with the original MIT notices.
The SDK's headless fixtures and codec tests remain SDK-owned.

The example and tests link installed SDK targets rather than compiling SDK
implementation files. Generation uses matching installed core and UX packages;
the test-only Descriptor builders are fixtures, not another runtime parser.
Encoded and RPC modes keep their existing generated client adapter and one FIFO.
This ownership move changes build and test inputs, not transport, generated API
semantics or wire formats. Removal of the former SDK presentation copies follows
qualification of this separate UX consumer.
