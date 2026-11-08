# Parallel chains (FX container)

Parallel processing is built from Mixer inserts, the familiar pattern-first way:
`mixer.insert.create` with `target` = an existing non-Master insert adds `value` (1..32)
branches in one Undo step. Each branch is named `<source> Parallel N` (unless `text`
names it), receives a unity send from the source, and outputs to the same destinations
at the same levels as the source. The source's own path stays the dry signal, so effects
added to a branch run in parallel with it (parallel compression, wet reverb chains,
distortion blends). Balance with the branch faders/sends; mute a branch to bypass it.

- MCP: `vibera_command {"command":"mixer.insert.create","args":{"target":1,"value":2}}`;
  it is an insert lifecycle operation: poll `vibera_mixerchain {"action":"job"}` until
  completed. Read routes with `vibera_inspect mixer`.
- UI: right-click a Mixer insert's effect area > Add parallel chains (FX container) > 1..4.
- Refusals: missing source, Mixer solo active, insert limit. Master cannot be a source
  (target 0 means ordinary inserts routed to Master).

Verified: an empty branch on a tone doubles the exported level (ratio 2.000) and muting
it restores the dry level (scripts/verify-parallel-chains-mcp.py). Model and render
evidence, not listening.
