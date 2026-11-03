# Installed structured payload-tool qualification

This suite builds the UX-owned native application fixture against an installed
SDK 0.1.0 prefix (`core`, `resource_adapter`), installs an explicit SDK npm tarball
outside the checkout, and drives the actual HTML/app.js in Chromium. It does not
compile SDK source files or import SDK Python tests/private parsers.

```sh
python3 -B tests/structured_client/run.py \
  --build-dir /tmp/ux-payload-gcc \
  --sdk-prefix /path/to/installed-sdk-gcc \
  --sdk-package /path/to/telemetry-sdk-0.1.0.tgz \
  --browser-python /path/to/playwright-venv/bin/python \
  --node /path/to/node --cxx g++ --cmake cmake --ninja ninja
```

The browser Python needs `playwright==1.63.0` and its matching Chromium. Set
`TELEMETRY_BROWSER_EXECUTABLE` only when deliberately selecting another installed
Chromium. `--npm-js` can select the matching Node npm CLI explicitly. The build
directory must be fresh and outside the checkout and installed SDK.

For Clang ASan/UBSan, use `--cxx clang++-18 --sanitize` and a separate installed
SDK prefix whose native libraries were built with the same sanitizers. The
runner enables leak detection and stops on sanitizer findings. All tests are
offline and access no board, port or debug target.

The preserved browser checks cover the exact native request download and Service
response (U64/S64 extremes, signed zero and arrays), all application refusal
statuses, Void, unavailable Service, timeout without replay, ValuesFile identity,
and deferred File reads across selection/descriptor replacement. The reflected
literal-name fixture replaces the one known `multiplier` byte sequence and
recomputes only the public v3 FNV fingerprint; the installed browser parser still
validates the descriptor. No descriptor parser is copied into this suite.

Additional checks verify that only the allowlisted application/runtime files are
served, the browser receives the exact installed runtime asset, a normal Node
consumer imports that npm runtime, and an explicit CMake SDK prefix wins over a
real competing `Telemetry_ROOT`. `summary.json` records native executable and
input hashes, installed SDK identity, installed npm contents, tool identities,
actual counts and before/after equality. Browser screenshots are retained.

The headless SDK's exhaustive payload-parser tests remain in Telemetry SDK. This
suite qualifies the UX application and installed-package boundary, not a second
copy of those tests. Its HTML/app/native fixture and Browser.py originated in
SDK `6e15220b1271139b3f1996a16d385c660f856f45`; ownership and dependency paths
changed here while the interaction and native payload oracles were preserved.
