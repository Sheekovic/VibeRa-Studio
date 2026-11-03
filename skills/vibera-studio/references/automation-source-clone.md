# Independent Automation Clip variations

Discover `automation.source.clone` using `vibera_commands` with
`includeSchemas=true`. It is a development-source operation; a released executable
supports it only if its actual discovery lists it. This does not establish full
FL Studio parity or arbitrary native plugin control.

In the Playlist Picker, select **Auto**, right-click one source, then choose
**Clone** or **Clone with links**. The new source is selected for normal drag or
click placement. Both choices make a separately editable curve with fresh source
and point IDs and copied length, colour, shapes, values and tensions. Neither adds
a Playlist placement, changes the original curve, duplicates target instruments,
or changes initialized values. A name suffix is added when within the saved-name
limit. Multiple selected sources disable these single-source actions.

Semantic equivalent:

```json
{"command":"automation.source.clone","args":{"target":123,"text":"preserve"}}
```

`target` is an existing stable **source** ID, never a placement, point, Channel,
Mixer display row or native parameter ID. `text` is `clear` (default when omitted)
or `preserve`. Clear removes copied target links; preserve keeps the exact target
owner IDs, parameter IDs and mapping ranges, including inverted ranges. It does
not copy external MIDI assignments or vendor-internal relationships.

The operation commits synchronously with one Undo. Check `completionState`,
`completionScope=canonicalAutomationSource`, `createdSource`, `linkPolicy` and
`revision`. Completion confirms the document edit; `dspSettlingAcknowledged=false`
does not claim heard audio. Stale revision, unavailable source, invalid policy,
capacity, active edit or conflicting file/native preparation must fail without a
partial source or history entry. There is no asynchronous cancellation token for
this synchronous edit. Cancel a context menu to leave the project unchanged.

Use paginated `vibera_inspect` for `automationSources`, `automationPoints` and
`automationLinks` (the latter two require `source=createdSource`), plus
`automationInitialValues`. Follow `nextOffset` until null; bounded state summaries
are insufficient for large sources. Points use quarter-note beats and normalized
0..1 values; links use their target's units. See [Mixer automation](mixer-automation.md).

To place the new curve semantically, use `vibera_picker` with `action=type`,
`kind=automation`, then `action=select, ids=[createdSource]`, and
`action=place, beat=<quarter-note beat>, lane=<zero-based lane>`. Selection is
workspace state, not document history. Or call `automation.place` with explicit
source `target`, `beat`, `length`, `lane` and source offset in `value`.

Keep these operations distinct:

| Operation | Curve identity | Target links | Placement |
| --- | --- | --- | --- |
| Source Clone | New | Cleared | None |
| Source Clone with links | New | Preserved | None |
| `automation.duplicate` | Same | Shared through source | Another linked occurrence |
| `automation.unique` | New | Cleared | Retargets the chosen occurrence |
| `channel.clone` | Independent instrument settings | No Automation links copied | No score copied |

Edit a cloned point with `automation.point.move`, then verify both source curves.
Use `edit.undo` / `edit.redo` and reread IDs; Redo restores the same clone IDs.
The global UI/MCP history action finishes a MIDI take first; this operation does
not change that recording policy. Canonical history tests retaining an active
take must not be described as global shortcut behavior.
An explicit clone macro uses a stable source target and the same policy; it does
not infer a target from a Mixer/Channel selection. Refused later batch steps roll
back the entire batch. Such preview/rollback advances the revision even on failure;
reread it before the next guarded request. Standalone invalid commands do not
publish a preview or change the revision. Save/open/export use `vibera_project`; poll every returned
job until actual completed/failed, never treat acceptance as completion.

Cloning an unplaced source must leave playback unchanged. Once placed, preserved
links drive the **same controls** as the original. Existing overlap priority still
applies (latest active start, then greatest placement ID); this is not a blend.
Channel gain/pan, arbitrary formulas/LFOs and unsurfaced plugin widgets are not
supported targets simply because related enum values or native controls exist.
