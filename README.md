# Telemetry UX

This checkout contains the optional Telemetry UI generator extension. It
depends on the exact matching SDK code-generator version and provides no
second parser, transport, FIFO or command-line entrypoint.

The `telemetry-ux-codegen` development package adds `--targets qml` to the
SDK-owned `telemetry-codegen` command. Its Python runtime needs no Qt dependency;
building the generated QML module still requires the documented matching Qt kit.
Headless C++/JavaScript/TypeScript generation needs only the SDK core package.

Install local matching wheels into a fresh environment:

```sh
python3 -m venv /external/codegen-env
/external/codegen-env/bin/python -m pip install --no-index \
  --find-links /external/packages telemetry-codegen-core==0.1.0 \
  telemetry-ux-codegen==0.1.0
/external/codegen-env/bin/telemetry-codegen generate descriptor.bin \
  --targets qml --output /external/generated
```

The module-free `telemetry-codegen==0.1.0` compatibility package installs both
packages. For an old monolithic installation, use a fresh environment or
explicitly uninstall the old `telemetry-codegen` package before installing the
new packages. An in-place upgrade across overlapping module owners is not
supported. The existing `python -m tools.telemetry_codegen` invocation remains
owned by the core package.

Keep user UI code outside generated output. Regeneration and `--check` preserve
the generator's owned-output contract. This migration has not yet moved the
Inspector applications or introduced a unified Configurator. Those changes
have separate installation and usability gates. [Source provenance](MIGRATION.md)
records the SDK revision from which the generators were moved.
No registry publication or new hardware qualification is implied.
