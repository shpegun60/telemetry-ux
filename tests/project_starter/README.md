# Complete project creation

The dependency-free controls qualify deterministic creation, initial manifests,
fresh-output refusal, name/path validation, case aliases, real Windows junctions
or Linux symlinks, package identity and unchanged source inputs:

```sh
python -B tests/project_starter/run.py --output /external/starter-controls
```

For the installed QML consumer add `--sdk-prefix`, `--ux-prefix`, `--qt-prefix`
and the matching `--cmake`, `--ninja`, `--cxx` tools. The test generates a complete
application, hides its producer template directory, builds through its generated
CMake preset and executes both actual QML panels. It requires two successful
reads through the one native client and zero QML warnings.
Use `--sanitize` with ASan/UBSan-instrumented installed archives. The copied
QML example uses offscreen Qt and disables leak checking for its global platform
caches; the maintained provider suite owns the separate lifetime/leak controls.

The controls also create the `qt-connection` target twice, verify its manifests
and exact three-source-file allowlist, and refuse an existing destination,
invalid name, partial prefixes and npm-only options. Add `--qt-connection` with
the same three prefixes to build its actual generated executable after hiding
the producer templates. Both installed packages must include `qt_connection`;
the generated configure/build uses only installed headers, modules and libraries.
Compiler commands are checked for accidental producer or SDK/UX source paths.
This creator suite records that connected-device build as `runtime: false`:
actual loopback TCP, two-panel QML and lifecycle are qualified separately by
the [owned Qt connection suite](../qt_connection/README.md). It does not claim
that copying a project establishes a connection to a physical device.

For the complete Web consumer also add `--sdk-package`, `--ux-package`, `--node`,
optional `--npm-cli`, and `--browser-python` with the existing pinned Playwright,
Chromium and websockets dependencies. The generated root prepare wrapper builds
the native dispatcher and installs packages. The maintained real browser journey
then checks discovery, exact U64, normalized Write/readback, Command, Service,
lost acknowledgement, explicit reconnect and no replay against generated paths.
The same `--sanitize` option instruments the native Web peer and keeps its leak
checking enabled; there is no Qt platform in that native peer.

No producer templates, application files or user directories are removed by the
test. Only its own fresh external producer fixture is renamed to prove generated
applications do not depend on a source checkout. Omitting integration options
runs only the dependency-free controls; the receipt states actual coverage.
All summaries and logs stay in the explicit external output directory.
