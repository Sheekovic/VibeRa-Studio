# Native parameter schema authority

Unreleased0.0.40 correction. Delivery qualification belongs in STATUS/VERIFICATION.

An admitted native automation route retains authority through redundant metadata
notifications. The worker compares complete ordered metadata on its control thread:
native IDs, flags, availability, step counts, units, defaults, bounded names/labels,
JUCE categories, count/error/diagnostic. Current values are excluded: a changed
value does not change the identity/capabilities of a native parameter. No hash or
partial-ID heuristic substitutes for that comparison.

A real schema change still advances the generation; malformed schemas remain
conservative failures. A catalogue request that discovers a difference defers
publication until the existing control/DSP pause reconciles it, rather than
publishing new metadata under an old generation. Refresh/publication allocations,
queries and resource destruction remain on the worker control thread. DSP packet
layout/processing, device buffer, transport and project schema are unchanged.

Every metadata refresh invalidates gesture history through the existing gesture
epoch, even when route generation stays constant. Cached Last tweaked and partial
gestures cannot survive a possible context discontinuity. Fresh complete gestures
recover normally. This is separate from native manual-value Undo; current native
values and cached gesture begins are still not canonical before-state authority.

## Evidence and limits

The earlier installed MVibratoMB startup generation1->2 rejected144 Song events.
Opt-in worker traces captured three actual transitions:310 metadata entries each,
all compared fields identical. Original SDK redundant-title test fails on the old
worker after12 assertions (before1/after2). With the correction it retains1,
delivers actual native events using the prior route, advances to2 for a real
writability change, refuses stale controls/events and readmits fresh authority.
The full original parameter suite passes181611 assertions, including live
Pattern/Song redundant notifications, actual native-editor epoch invalidation,
malformed metadata and genuine schema/gesture invalidation.

The corrected installed-provider run passes4197 assertions, Pattern/Song448
callbacks and one loop each, native Song events16768/rejected0, retained graph/
device/worker, edit/Undo/Redo, state/save/reopen and WAV. Three worker refresh1->2
traces retain generation1 and advance gesture epoch1->2 with identical310-entry
metadata. This uses the installed effect, not just a mock. No global/physical input,
listening, general plugin compatibility or completed family claim.

Receipts: build/schema-transition-analysis.json; schema-transition-trace/;
schema-transition-fixed-trace/; schema-notification-red/green.log;
schema-notification-parameters.log; schema-transition-fixed-provider/.
The diagnostic VIBERA_SCHEMA_TRACE_DIRECTORY opt-in accepts an absolute directory,
records per-refresh metadata without current values/opaque state, and writes on
the worker control thread only after its DSP pause is released. It is off by
default; diagnostics can influence control scheduling. Raw provider data stays
local and is not an application asset.

Real dynamic schema changes still require broader automatic route readmission
qualification. Native Undo, full automation/controller links, PDC and provider/
hardware coverage remain open. The independent5081ms/16.27ms callback failures
remain unresolved. Five delivered0.0.39 preview checks (1799 assertions each) and
one full musical sequence (27381) pass, which does not erase those failures or
prove they were introduced by0.0.40. Full source/package gates must still pass.
