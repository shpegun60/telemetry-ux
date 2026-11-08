# Schema-bound presentation metadata qualification

`cases.json` is the shared Web/Qt parser corpus. It covers exact numeric text,
UTF-16 length limits, typed paths, category/role rules, duplicate selectors,
unknown properties and malformed decimal hints. `Parse.mjs` exercises the browser
parser; the native fixture consumes the same JSON cases.

Run the installed native consumer with a matching SDK prefix that contains
`core`, `encoded_server`, `encoded_backend`, `workbench` and `qt_connection`:

```sh
python -B tests/presentation_metadata/run.py \
  --build-dir /external/presentation-check \
  --sdk-prefix /external/sdk-prefix --qt-prefix /usr \
  --cxx g++ --cmake cmake --ninja ninja
```

The runner copies current UX sources into its fresh external directory, installs
them, moves the prefix and hides the producer source/build directories. It then
copies the native fixture and builds an independent consumer using only installed
SDK/UX packages. The existing native host fixture is reused from
`tests/dynamic_qml_configurator/Fixture.hpp`; no second dispatcher or client is
implemented for metadata testing.

The fixture checks parser agreement, matching and mismatching fingerprints,
unchanged wire values and permissions, advisory hints, independent providers,
replacement/clearing and native QML with no tolerated warnings. The runner hashes
the source inputs, copied consumer files, installed SDK and installed UX before
and after qualification. It records command logs and a `summary.json` receipt.
No hardware is connected or modified.

For an instrumented SDK prefix, add `--sanitize --cxx clang++-18`. The UX producer
and consumer receive ASan/UBSan flags; the runner verifies instrumentation in
seven installed archives. A narrowly named Qt software-texture leak exception is
shared with existing QML qualification. A separate injected 123-byte application
leak must still fail with exit 86, proving that application leaks remain visible.

On Windows, use the `g++.exe` belonging to the selected Qt MinGW kit, that kit's
`--qt-prefix` and explicit CMake/Ninja executables when they are not on `PATH`.
Each run requires a new or empty output directory; previous receipts are never
removed or overwritten by another qualification run.
