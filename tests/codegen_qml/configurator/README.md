# Generated static Configurator binding

This installed-package gate checks the optional `--ux-starter qml` path. The
ordinary seven-file QML output is sealed against the pre-feature `1112f9c4`
emitter for two explicit UX-owned schemas. Default generation adds no UX C++
library dependency. The opt-in binding requires `TelemetryUX::static_configurator`.
The host's top-level CMake also finds that component before adding the generated
QML directory, so Qt's parent-level executable finalizer sees its imported targets:

```cmake
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS static_configurator)
add_subdirectory("${GENERATED_SDK}/qml" generated)
```

```sh
python3 -B tests/codegen_qml/configurator/run.py \
  --sdk-prefix /path/to/installed-sdk \
  --ux-prefix /path/to/installed-ux \
  --codegen-python /path/to/matching-codegen-venv/bin/python \
  --qt-prefix /usr --cxx g++ --build-dir /tmp/static-qml-generated
```

Both codegen distributions must be installed, matching version 0.1.0, outside
the checkout. The test invokes their CLI from the external build directory.
`--sanitize --cxx clang++-18` requires an instrumented SDK and UX prefix.

The application-owned native Model runs through the existing generated
`DeviceClientAdapter` and one actual Encoded backend/FIFO. Checks cover atomic
QVariant/canonical-payload cache publication, exact F32/F64 positive/negative
quiet/signaling NaN payloads and signed zeros when another leaf changes, explicit
finite edits, canonical seed rejection, a real QJS getter mutating the borrowed
seed's external `fromRawData` backing memory, retired-session cache suppression
and preserved checked operation evidence.
A second compiled schema exercises reserved Qt macros, colliding new method/cache
families and host-side type-name collisions. This gate tests the generated seam;
the adjacent `tests/static_qml_configurator` gate tests the shared provider/view.

The original payload is the known edit base, not a server compare-and-swap token.
An explicitly supplied different but canonical seed remains a permitted input.
Callbacks, engine processing and transport ownership stay with the application;
this suite performs no physical-device or UART actions.
