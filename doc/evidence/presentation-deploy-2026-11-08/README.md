# Acceptance snapshot: presentation metadata and own-device deploy

This is the public evidence for the completed UX stages at
`00275e1a60c98c3638afe538b15a83f064561095`, paired with SDK
`972e30e28ba203e04959cbc9c0af0358bfdbc60d`. It describes that historical cut,
not the current HEAD of either repository. Publishing these documents does not
change the qualified production code.

The portable [FINAL-ACCEPTANCE.json](FINAL-ACCEPTANCE.json) records exact workflow
IDs, revisions, evidence hashes and coverage limits. It is derived from the
local acceptance record captured at `2026-11-08T00:53:07.119711+00:00`; its
provenance includes the original record's digest. Personal local paths and
undistributed local receipts are not presented as publicly reproducible inputs.
The directory's `.gitattributes` preserves JSON bytes and their recorded hashes
across Windows and Linux checkouts; keep these evidence files unformatted.

## Accepted code and CI

| Scope | Exact revision | Successful checks |
| --- | --- | --- |
| SDK | `972e30e28ba203e04959cbc9c0af0358bfdbc60d` | [19/19](https://github.com/shpegun60/telemetry-sdk/actions/runs/42421647855) |
| UX | `00275e1a60c98c3638afe538b15a83f064561095` | [6/6](https://github.com/shpegun60/telemetry-ux/actions/runs/42430622131) |
| Exact SDK + UX pair | same two revisions | [10/10](https://github.com/shpegun60/telemetry-ux/actions/runs/42430622189) |

[PR #34](https://github.com/shpegun60/telemetry-ux/pull/34) adds schema-bound
labels, units, plain-text help and advisory numeric hints to Web and Qt providers.
[PR #35](https://github.com/shpegun60/telemetry-ux/pull/35) adds the own-device
export workflow. The merge tree is identical to accepted PR #35 head
`14e374569795b2b8c473fde80a9ccd866f848e4d`.

The local-main/clean-tree statements in the original delivery were observations
made by the local agent at the recorded UTC time. GitHub establishes remote
revisions and CI results; it cannot independently establish that Windows state.

## Download and reproduce the archive checks

Download `installed-web-configurator` from the
[specific PR #35 artifact](https://github.com/shpegun60/telemetry-ux/actions/runs/42429738594/artifacts/13596882298)
or its [successful workflow](https://github.com/shpegun60/telemetry-ux/actions/runs/42429738594).
Keep the ZIP unmodified. GitHub may require sign-in, and its artifact retention
applies; the API's captured expiry is in
[ARTIFACT-METADATA.json](github/ARTIFACT-METADATA.json). These documents do not
promise that GitHub will retain the ZIP indefinitely.

The expected archive SHA-256 is:

```text
3c05cb10bc829b888fcda10838987c5f7a868569069232284d2e08c7954efffe
```

From this checkout, run the standard-library-only, read-only
[verifier](../../../tools/verify_delivery_evidence.py):

```sh
python -B tools/verify_delivery_evidence.py --archive /downloads/installed-web-configurator.zip
```

It reads the ZIP without extraction, native execution or network access. It
compares the digest with the captured GitHub metadata, verifies all 508 ZIP CRCs,
the 14 named successful summary receipts, the original 84 top-level command-log
hashes and the four C++ table dumps. The latter contain 90 asset payloads across
the GCC and Clang variants; each payload is checked against its recorded path,
length, hash and unchanged relocation inventory.

The original [VERIFIED-ARTIFACT.json](github/VERIFIED-ARTIFACT.json) is retained
unchanged. Its 90 source-hash comparisons, two generated-title substitutions and
six fixture comparisons are historical agent observations, not checks repeated
by this archive-only verifier. They must not be confused with the 90 asset
payloads checked by the four table dumps.

## Exact log coverage

[LOG-COVERAGE.json](github/LOG-COVERAGE.json) names every referenced log and
its expected hash. The original count of 84 covers top-level command logs.
Recursively, the receipts reference 120 unique logs: 108 are in the retained
ZIP and can be checked; 12 created-project browser negative-control logs were
not uploaded. The verifier reports the declared missing inputs explicitly.

The absent members are `serve-changed.log`, `serve-missing.log`,
`budget-without-export.log`, `zero-byte-budget.log`, `zero-file-budget.log` and
`excess-file-budget.log` under each `ux-created-web-{gcc,clang-asan}/browser/`.
The corresponding receipt/check outcomes and successful CI jobs remain
available, but those 12 original log contents cannot be reconstructed from this
ZIP. They are not replaced with logs from a different local run. The workflow
now retains these nested browser logs for future runs; that does not alter this
historical archive.

## What the delivered features do

Presentation `minimum`, `maximum` and `step` are advisory hints. They do not
block Apply, clamp values, scale units or replace validation on the device.
See the [qualified presentation contract](https://github.com/shpegun60/telemetry-ux/blob/00275e1a60c98c3638afe538b15a83f064561095/doc/PresentationMetadata.md).

After creating a `web-device` project and supplying compatible installed SDK/UX
packages and development tools, these commands build the native host example,
export browser assets and test them locally:

```sh
python -B prepare.py --export-web cpp
python -B serve.py --exported
```

This is not a deployable STM32 firmware build. `--export-web files` emits the
runtime filesystem bundle; `cpp` also emits `telemetry_web_assets.hpp`.
See the [qualified deploy guide](https://github.com/shpegun60/telemetry-ux/blob/00275e1a60c98c3638afe538b15a83f064561095/doc/OwnDeviceDeploy.md)
and [Qt Creator example](https://github.com/shpegun60/telemetry-ux/tree/00275e1a60c98c3638afe538b15a83f064561095/examples/qt_connection).

The [ARM object receipt](ARM-OBJECT-ACCEPTANCE.json) records a local Cortex-M7
C++17 compilation. The original object and preparation directory are not
distributed here, so this is a local measurement record, not a reproduced build
from the archive verifier. Neither a generated table nor an object compile
establishes firmware linking, Flash fit, RAM/stack cost or operation on a board.

Firmware HTTP/WebSocket integration, server authorization and physical STM32
qualification remain outside this cut. No board was programmed for these stages.
The SDK wire protocol, portable runtime and existing Session/client/FIFO remain
unchanged.
