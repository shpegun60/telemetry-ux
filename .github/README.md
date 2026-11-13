# Compatibility qualification

`workflows/ci.yml` runs the current UX consumers against the same immutable SDK
commit recorded in `release-pair.json`. The four SDK defaults must match that
canonical pair; the identity check rejects drift between them. These are current
consumer checks, not a claim that new UX APIs work with an older SDK. There is
one implementation of each consumer suite.

An explicit full SDK/UX pair supplied through `workflow_call` overrides the
normal SDK default for that run. The separately pinned `setup-apt` action is a
CI helper, not the SDK library installed by the consumers.

`workflows/release-pair.yml` calls those same six jobs with the SDK commit in
`release-pair.json` and the exact UX PR head (or pushed main commit). This covers
Inspector, generated/static QML, browser providers/WebSocket, Python codegen on
Windows/Linux, and the independently installed pure Qt view. The aggregate
**Exact SDK + UX integration** fails if any job fails, is cancelled or is skipped.

The `exact-release-pair` artifact records the selected inputs, not a success
claim. The aggregate check supplies the result; each consumer retains its own
actual command logs, installed package identities and qualification receipts.

Update `sdkRevision` and all four `ci.yml` SDK defaults together after reviewing
and testing a new joint delivery. A moving SDK `main` is never silently
substituted for a recorded commit. A successful old
compatibility profile alone does not qualify the selected release pair. Add the
aggregate check to the required checks after its first successful qualification.

The separate Windows portable producer and clean consumer in
`workflows/windows-portable.yml` are currently disabled while the Qt 6.12 Windows
kit is unavailable through the CI installer. The aggregate permits that one
explicit skip; its success does not qualify a Windows ZIP. When enabled, that
consumer installs no development dependencies and runs actual QML read/edit/TCP
journeys from the relocated ZIP. See
[the distribution contract](../packaging/windows/README.md).
