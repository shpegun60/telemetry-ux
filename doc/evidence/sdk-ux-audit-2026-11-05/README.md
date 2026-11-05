# SDK and UX source audit ? 5 November 2026

The audit covers maintained production sources, examples, executable tests,
build scripts and package configuration. The complete source ledger contains
982 files / 190,981 physical lines across both repositories, including the new
runner regression. This repository's exact subset is in [coverage.json](coverage.json).
SHA-256 values normalize CRLF to LF so another checkout can verify the inputs.

979 files were read in full. Two compiler-produced Client runtime files were
checked byte-for-byte against TypeScript output. One pinned Boost.PFR generated
file (9,078 lines) was checked structurally across all 202 adapters and 20,100
field references, with manual review of helpers and boundary cases. These are
separate review categories in the ledger; generated equivalence is not a claim
of manual inspection of every repeated generated line. Seven retained historical
evidence scripts were excluded and are listed explicitly. Historical receipts,
measurement data and binary assets are not new execution evidence.

## Confirmed repairs

| Area | Reproduction and resulting behavior |
| --- | --- |
| Encoded header | Overlapping Header/output could overwrite native members before serialization. Snapshot validated members before the first output write; refusal leaves output untouched. |
| Native scalar registry | Distinct LP64/LLP64 integral spellings could create non-reserved scalar TypeIds rejected by RuntimeSchema. Resolve supported scalar spellings to the existing wire code, including enum/array dependencies. |
| Generator publication | A retained Windows directory differing only in case could accept generation but reject its next check. Preflight the complete final file/directory layout before staging. |
| Windows runtime parity | Autocrlf changed checked-in compiler output while TypeScript emitted LF. Pin the two exact-output runtime files to LF; keep the strict byte gate. |
| Async and serial test reports | Exit zero without the required counted report could be accepted. Require a single positive report, expected profile and maintained floor; exercise actual failing subprocess controls. |
| CI dependency setup | Host-only Clang jobs downloaded unused ARM tools and an unused 2 GB C++ archive could exhaust setup budgets. Select dependencies per matrix, use bounded official-mirror downloads, keep all tests and verify actual ARM objects/link maps without the archive. |
| Reused test receipts | Early failure could retain a previous success in ergonomics, docs or codegen. Invalidate before fallible discovery/hashing; retain legitimate supplemental evidence before invalidation. Eight regression cases exercise fixed failures and old-behavior controls. |
| Dynamic QML watch values | A queued or handed-off watch reply could republish old values after an accepted mutation. Retire the existing cycle/revision after acceptance and retain pending ownership until completion. Enqueue refusal preserves the old cycle. |
| Static QML response | A previous Service response survived Command/Write. Hide it while a mutation is pending, preserve it for refused enqueue, and retire it after accepted completion or uncertainty. |

The SDK fixes are in [SDK PR #36](https://github.com/shpegun60/telemetry-sdk/pull/36);
the presentation fixes are in [UX PR #24](https://github.com/shpegun60/telemetry-ux/pull/24).
No second FIFO, protocol version, hidden allocation, new transport or retry policy
was introduced. Existing exactly-once limitations remain unchanged.

## Qualification and limits

Targeted local checks included GCC/Clang ASan/UBSan Encoded overlap regressions,
LP64/LLP64 scalar fixtures and generated C++/JS round trips, Cortex-M7 compile/link,
async client and actual serial PTY suites, strict emitted/published JS/TS parity,
and actual Qt/QML mutation/refusal/stale-callback tests. QML checks included 863
watch assertions, 450 static contract assertions and 590 generated/native
assertions with no warnings. The real Windows JS/TS client suite completed 2,954
checks. Detailed local receipts remain under the external audit artifact root;
the final GitHub workflows are the authority for their exact tested commit.

The original SDK formatting failure was reproduced with pinned clang-format
22.1.8 and repaired only in three new C++ tests. A separate NumericOracle compile
timeout was investigated: the same-head PR job passed; local Clang compilation
used 115.07 seconds and 4,761,892 KB maximum RSS, then passed 1,572,092 checks.
No timeout was increased. This remains a compiler-resource sensitivity, not a
claim that an arbitrary future runner cannot time out.

This is a software audit, not a proof of absence of defects. No board was flashed,
reset or measured. Existing hardware evidence keeps its original source identity.
The SDK and UX baselines were respectively e07e82c48844564a537ad4ac38843cd319859d94
and 4596102ac64d8f6d2ae363b05cadbfbe1fcc5502. Final source identities and CI are
recorded by the PR and merge workflows, rather than a self-referential commit
identifier inside this document.
