# Installed Widgets playground qualification

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This host-only gate builds the actual Widgets example against an installed
Telemetry SDK 0.1.0 prefix. It never builds SDK source files or accesses a device.
The example preserves the original native Model, window and Resource facade.

```sh
python3 -B tests/widgets_playground/run.py \
  --sdk-prefix /path/to/installed-sdk --qt-prefix /path/to/qt \
  --build-dir /tmp/widgets-installed-check --cxx g++
```

Use `--sanitize --cxx clang++-18` on Linux for ASan/UBSan in the example and test
translation units. Prebuilt SDK libraries retain their own compiler/instrumentation;
the receipt records their exact bytes and SDKMetadata.json revision. Qt offscreen
profiles disable LeakSanitizer and make no leak-coverage claim.

The gate runs `telemetry_playground --smoke-test` with the offscreen platform:
exact integer display, nested values, native/encoded writes and commands,
invalid-argument rejection and application Resource composition remain checked.
A separate `DeviceCheck.cpp` consumer includes only the facade's generic
Resource header and validates listing, Descriptor/Values streams and read-only
file behavior. Its include directories must not expose the private native Model
or reflection SDK components.

All build output, command logs, binary hashes, UX source hashes and installed SDK
before/after hashes stay in a fresh external directory. The summary distinguishes
UX source identity from the installed SDK's version, source revision and raw
payload hashes. No repository or toolchain is claimed hermetic.
