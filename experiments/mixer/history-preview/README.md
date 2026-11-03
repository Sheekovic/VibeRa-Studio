# Read-only history destination draft

Status: isolated implementation draft, not integrated, compiled or executed.
The root integration owner owns live source, pinned-patch registration, builds
and actual runtime qualification. No application/package/version changes.

## Concrete question and source finding

A private Mixer structural graph must be prepared for the actual destination
of Undo/Redo before changing the current model or history cursor. Performing
Undo and then Redo to inspect the destination is unsafe: the pinned JUCE
`UndoManager::undo/redo` clears its history when an action returns false, and
can already have executed other actions in that transaction. A prepared graph
failure must occur before either operation is executed.

Pinned JUCE `getActionsInCurrentTransaction` is not sufficient. It returns no
actions after `beginNewTransaction`, even while a real Undo transaction exists,
and provides no Redo transaction. `getUndoDescription` supplies a label, not
the stored Project snapshots. `Controller::StateAction` already stores the
authoritative complete before/after Projects, including fresh captured native
states, source IDs and allocator state. Gestures/document macros finish as one
StateAction, preserving their existing transaction policy.

## Minimal changes

- JUCE: two const append-to-array accessors for the actual `getCurrentSet` and
  `getNextSet` action lists. Earliest action first; no action invocation, cursor
  changes, timer, notification, lock or shadow history. Caller owns the control
  thread; returned pointers remain manager-owned and are not stable across
  mutations. Existing current/open-transaction APIs are unchanged.
- StateAction: const before/after snapshot and owner identity accessors.
- Controller: `previewHistoryDestination(direction, candidate)` recognizes only
  this Controller's StateActions. Undo uses the earliest before snapshot because
  execution is reversed; Redo uses the latest after snapshot. Foreign/mixed
  transactions, active gestures, recursive history execution, empty directions,
  invalid directions and invalid stored destinations are explicitly refused.
  A refused preview leaves the previous output candidate unchanged.
- `HistoryDestination` owns private Project copies and identity metadata only.
  Public access is read-only. No live Undo action, plugin host, callback, graph,
  separate stack or independently maintained cursor is owned here.
- `validateHistoryDestination` re-reads actual history and rejects changed
  Controller/session/revision/source state, transaction action identity/name or
  destination. It performs no Undo/Redo, mutation, cancellation or notification.
  Repeat it on the sole control thread immediately before eventual commit.

Error strings carry stable internal categories: `history.invalidDirection`,
`history.busy`, `history.unavailable`, `history.unsupportedTransaction`,
`history.invalidDestination`, `history.staleDestination`. UI/MCP integration of
the eventual structural transaction must map these to its typed errors; this
draft does not add a new public command or claim that integration is complete.

## Integration ownership and qualification

`history-destination.patch` changes exactly five files: Commands.h/.cpp, pinned
JUCE UndoManager.h/.cpp, and the original new
`tests/HistoryDestinationAcceptance.h`. `manifest.json` records raw and LF
SHA256 for the four original bases and five final files, plus exact patch hash.
Persist the two JUCE changes through the repository's existing pinned dependency
patch mechanism/guard; do not rely on ignored vendor edits alone.

Root runner integration, after `require` is declared:

```cpp
#include "HistoryDestinationAcceptance.h"
```

Add a separate owned-output branch (or existing focused runner group) calling:

```cpp
historyDestinationTests(output);
```

The header uses the production Commands/Customization APIs and original local
UndoableActions. It does not scan, launch or impersonate a real plugin. The
opaque-state test deliberately uses an unavailable, model-only VST3 descriptor.
It verifies actual JUCE pair ordering/cursor boundaries, the false-Undo history
clearing contract, non-mutating admission/readback, exact Undo/Redo, active
gesture/foreign action refusals, stale metadata/direct model/session guards,
source allocator/identity preservation, document macro grouping, and captured
opaque native baseline retention. Existing independent tests remain unchanged.

Root should run focused model/history tests first, followed by the required
broader source regression because Commands and pinned JUCE are shared. No
timeout, assertion, callback budget, musical oracle or release gate is changed.

## Remaining transaction requirements

This is the history metadata/preparation seam only. It is not an atomic
model/graph/history transaction, not a publication primitive, and not proof of
plugin-bearing structural Undo/Redo. Existing `undoEdit/redoEdit`, StateAction
perform/undo, dirty/revision notifications, gesture cancellation and allocator
policy are untouched. Existing snapshot assignment/notify can throw; this
draft does not claim allocation-failure-safe history commit or listener rollback.

The root transaction still needs fresh native-state capture rules, privately
constructed/prepared graph ownership, preparation-failure tests, stable target
and instance guards, control-thread exclusion between final validation and
commit, correct publication/model/history ordering, resource retirement, actual
live Pattern/Song/held-note acceptance and shared UI/MCP semantics. A busy
gesture is refused here; any caller that wishes to finish/cancel one must use
the existing explicit behavior before requesting a new preview.

## Source-only checks actually performed

`draft-verification.json`: four live raw bases matched at draft generation;
read-only `git apply --check --whitespace=error` passed; applying the exact patch
only under this scratch directory reproduced all five final files with
LF-normalized exact content. `reproduction/` is source only, not another build
or runnable distribution. No C++ compilation, acceptance executable, native
input, audio callback, timing, hosting or listening result is claimed.
