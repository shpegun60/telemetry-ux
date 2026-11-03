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
GUI applications and reusable Configurator components have a separate migration
gate; this initial source move does not claim that they are already extracted.
