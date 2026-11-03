# Indexed Browser search

Independent native JUCE implementation; no FL code, assets or recovered logic.
Research: Qwen draft preserved at 051fba9; reviewed integration at420e28a. The
current branch integrates both intentions with safe resource ownership. The
canonical distribution now includes this foundation after final qualification and in-place packaging.

## Interaction and reference evidence

| FL task | Expected interaction | VibeRa equivalent | Intentional difference / remaining scope | Evidence |
| --- | --- | --- | --- | --- |
| Search libraries | Ctrl+F searches configured folders/subfolders | Ctrl+F enables All folders and focuses the query | Default local filtering remains available; no automatic personal/reference folder scan | Official Browser manual D; VibeRa handler and core tests |
| Navigate matches | F2 previous, F3 next | Browser-context F2/F3; Shift+F3 alternative | Global Pattern rename remains outside Browser | Official manual D; scoped profile tests; installed FL runtime not observed |
| Cancel search | Escape | Clear query, reject obsolete results and return to tree | Completed index should remain reusable; fixed: separate cancel/root generations retain the completed index | Official manual D; cancellation tests |
| Audition/load | Select to preview; middle-click Send to selected; drag compatible content | Existing audition, middle-click, double-click new sample Channel and file-path drag handlers | Dedicated flat result list with parent folder label/full-path tooltip | Existing production handlers and GUI tests |
| Refresh content | Reread updated folders | Refresh button / configurable browser.reindex | Browser-context Ctrl+R fresh default; customized/unbound profiles and Export outside Browser preserved; no automatic filesystem watcher | Official manual D; cache/refresh tests |

Primary reference re-opened2026-10-14:
https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm
The installed reference build is24.2.1.4526. Current online documentation may
describe later behavior. Static labels establish feature leads, not live behavior
or reference cache algorithms. See REFERENCE_EVIDENCE_REVIEW.md.

## Query and ownership

Case-insensitive filename substring, `type:wav;flac`, and
`folder:"E:\My Samples"` clauses combine. The foundation is extended by the original bounded wildcard/Boolean compiler
and Starred/saved-query tabs in0.0.33; see [BROWSER_COLLECTIONS.md](BROWSER_COLLECTIONS.md)
for precise semantics, migration, evidence and differences. Version0.0.34 adds
[tags/ANY/ALL and saved filter scopes](BROWSER_TAGS.md); broader databases/layouts remain open.

One background service owns enumeration, index, query matching and cache I/O.
Only configured roots are scanned; unsupported files and links are skipped.
There is no per-folder 512/1024-entry truncation; an explicit 500000-entry/folder
resource limit refuses incomplete publication. Windows scope boundaries are
normalized. Cache version 1 has a 24-hour freshness bound, explicit roots, a 64 MiB
limit, validated entries and atomic replacement. Refresh forces a rescan. These
are independent choices, not claims about FL internals. Slow network/junction
and long-session qualification remain open.

Queries coalesce for 150 ms. Root and query generations reject obsolete work.
UI polls with a nonblocking try-lock and displays virtualized results; large
index/result reclamation stays on the owned worker. It never touches UI/DSP.
Shutdown is cooperative and does not join filesystem I/O on the UI thread.
Legacy JUCE directory-list ownership remains separate. No personal cache is
used by device-disabled tests; fixtures use unique owned folders.

Shortcuts resolve editor context first and global fallback second. Validation
now follows that same rule; collisions within one context are rejected. New
defaults respect customized global keys and deliberately unbound known actions.
Existing document macros retain their semantic actions and Undo grouping.

## Historical foundation verification and remaining scope

Main canonical compilation only. Content hashes invalidate copied source inputs
whose timestamps may be older than object files; successful compilation records
the exact input state. The earlier stale registry is retained at
`build/browser-profile-{diagnostic,details}*` and `build/browser-musical-details/`.

Initial corrected14/14 suite passed in 127.06 s (`build/browser-full-ctest.log`).
Both playback modes:768 callbacks/four loops, prepared audition, unchanged
document/graph/device/cache and held MIDI capture, zero measured silence/overruns;
maxima0.2487/0.2239ms. These are virtual callbacks/JUCE handlers, not listening or
OS musical input. The first crowded component image was inspected and then
corrected by placing results before the preview and compacting search controls.

Negative `build/browser-cancel-red.log` proves Escape unnecessarily rebuilt a
completed index. Cancellation now has a separate generation, preserving completed
root data while refusing canceled scan/query publication. The corrected focused
suite passes 1583 assertions (`build/browser-cancel-fixed.log`). Final canonical source 14/14 in 126.83 s and all 12 packaged process checks passed.
Real Analog Lab V Browser Pattern/Song runs retain768 callbacks/mode, held captures
and worker/lifetimes with zero measured overruns/silence. See current STATUS and
VERIFICATION for exact scopes; no full Browser parity, physical input or listening claim.

Historical0.0.33 qualification superseded the foundation tuple above:15/15 source,12/12 packaged processes,12757 Browser assertions and actual installed-instrument live checks. Current identity and limits: STATUS/VERIFICATION and BROWSER_COLLECTIONS.md.

Current0.0.34:15/15 source,12/12 packaged,17412 Browser assertions, real installed-instrument live metadata/tag writes and saved filters. Exact tuple: STATUS/VERIFICATION and BROWSER_TAGS.md. Full Browser scope remains open.
