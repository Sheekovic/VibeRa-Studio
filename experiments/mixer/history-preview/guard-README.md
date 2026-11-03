# Exact-byte JUCE history preview guard bundle

Deployment payload is frozen under `bundle/`; no live project/vendor files have
been changed. Controller/test preview behavior remains the previous draft.
This bundle replaces only the previous LF-normalized JUCE deployment proposal.

## Original archive proof

The existing `vendor/archives/juce.zip` was read, not downloaded or modified.
Its SHA256 is `f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a`,
matching dependencies.lock.json for revision
`37c894f83d379179b2070d437ccd0f1cd9af9576`.
Both original UndoManager files were extracted in memory and compared byte for
byte with the original draft bases. All original newlines are CRLF. The patched
files preserve CRLF throughout and their LF-normalized content equals the
previous final API implementation exactly. `raw-provenance.json` records file
identities, archive paths, byte/newline counts and the original/after/payload
hashes. `raw-source/` contains source-only fixture pre/postimages.

## Frozen deployable files

Copy the following four files from `bundle/` into the same relative project
locations after the root integration owner's running source suite finishes:

- `scripts/apply-history-preview-patch.ps1`
  SHA256 `40bb59a7caf88d84187fd67262ea36af0d21fd2a89e48a217d8a55f67ac3c4d2`
- `patches/juce-history-preview/manifest.json`
  SHA256 `d582b2bccc00f8f15c53c75137e0e116927eb152c632b14c9d30f5f68e83383b`
- `patches/juce-history-preview/juce_UndoManager.h.patch`
  SHA256 `86c04bb6c0e39c0c47add934e7f4cd2e9baf14bb7379a6875e054067497bbec6`
- `patches/juce-history-preview/juce_UndoManager.cpp.patch`
  SHA256 `13ed78763260e258843b5764be5cd9ac74aca482103e9472ef1005109b5fb371`

Raw postimages:

- UndoManager.h: `44e61c66b9ffed5eddb19232e6323ca0748b474b463d6efb9baf9570708e3a3e`
- UndoManager.cpp: `f5608a00535ee9897e6b69bf2bb4e84b7fb62cbe6ca1bffdd8a8de46843e8ed6`

The raw mixed-newline patches use LF diff headers and CRLF source context/additions.
Add this exception after the existing general patch rule in `.gitattributes`:

```gitattributes
patches/juce-history-preview/*.patch -text
```

Otherwise Git normalization can change payload bytes and defeat reproducibility.
The script already uses CRLF, consistent with the repository's PowerShell rule.
Add the script invocation to the existing dependency-patch chain and register
the same exact input/payload identities in applicable dependency/source guards.
Do not deploy the two JUCE sections from the old combined LF-only draft patch;
use this raw bundle for vendor files and the unchanged earlier Commands/test
draft for application integration.

## Guard behavior

The script follows the existing pinned editor/MIDI mirror-first pattern. It pins
JUCE revision/archive/source directory and the exact reviewed two-target
original/after/payload contract; it cross-checks that contract against the
manifest. It refuses unknown target sets, paths, bytes, payloads, changed pins
and reparse ancestors. It checks both targets before touching either source.
Already-applied inputs are reversed only in an owned temporary mirror and
checked against exact originals. Both patches are then applied in that mirror
and both exact raw postimages must match. This happens even for VerifyOnly and
fully already-applied inputs. Known mixed original/patched state can be verified
without mutation, or safely completed after the full mirror preflight. An
unknown member in any mixed state refuses before any source write.

Before final application it rechecks all initial target identities, runs the
combined pending-patch check, applies only pending targets and verifies final
raw hashes. Temporary mirror cleanup verifies its absolute boundary and rejects
reparses. No plugin process, build, native desktop, package or application is
started. The script is a strict preflight/application guard, not an operating
system transaction: concurrent hostile filesystem races or I/O failure during
the final multi-file write do not have a claimed universal rollback guarantee.
Use the repository's serialized integration/build owner as usual.

## Verification actually performed

Disposable source-mirror attempt `m/92d5bb1e`: **19/19 PASS**, total guard wall
time **15.218125600s**. `receipts.json` retains command, exit, stdout/stderr,
exact before/after target identities, unrelated-file preservation and scratch
cleanup for every invocation. `summary.json` pins the tested script, manifest
and runner. Cases cover original VerifyOnly/apply; applied idempotence/VerifyOnly;
known mixed verification/completion; unknown first/second/mixed targets;
tampered payload, postimage and coordinated payload/manifest changes; path
escape, duplicate targets, wrong revision/archive pin, missing input, normalized
vendor bytes and normalized payload when all inputs are already applied.

After the root owner released timing-sensitive qualification, owned-junction
attempt `r/6c70836e` passed **3/3** in **1.838358400s** of guard wall time. It
exercised a reparse ancestor at the vendor target parent, patch directory and
preflight parent. Each refused with a reparse error before source writes,
retained both exact target hashes and the unrelated file, removed only its
verified owned fixture junction, and restored the original directory. Exact
commands/output/before-after hashes and cleanup exits are in that attempt's
`receipts.json`; all three cleanup exits are0. No live path was redirected.

No guard job remains running. The preceding Python runner-writer quoting error occurred
before file creation or guard execution; the corrected writer generated the
19-case runner. It was a preparation-script error, not a product/test failure.

No C++ compile, Controller acceptance executable, native input, plugin hosting,
real-time callback or listening qualification is claimed by these guard checks.
