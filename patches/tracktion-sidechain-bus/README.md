# Optional graph-sidechain receiver bus

Original local adaptation of pinned Tracktion revision
4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86, under its existing GPL-3.0-or-later
license. The dependency pin and archive identity remain unchanged.

`Plugin::getSidechainBusIDOverride() const` returns `std::optional<int>`.
Its default is empty. The existing graph-sidechain receiver uses an explicitly
supplied bus ID, otherwise the original source-track-derived bus ID. This is a
graph-preparation query, not an audio callback operation. No new synchronization,
allocation or per-sample processing is introduced.

The source track ID must still be valid and the plugin must not be missing.
Existing direct/auxiliary channel maps, output trimming, automatic source sends
and dependency handling are unchanged. Callers own transient
bus namespace allocation, a matching prepared send at the correct tap, and valid
channel wiring. The hook alone does not establish sidechain functionality.

The third patch defers `SummingNode` property caching until `isPrepared`, matching
the existing `PluginNode` policy. During graph transformation a sidechain
`ReturnNode` may initially report no source, then discover its matching send.
Caching the earlier maximum can freeze an incomplete latency/channel description.
`Node::initialise` calls `prepareToPlay` then `getNodeProperties`, so the final
prepared properties still cache before processing. No compensation threshold,
sample oracle or callback deadline is changed. This fixes the identified stale
cache mechanism; actual unequal-path audio correction still requires rendering.

The manifest records exact original and patched SHA256 identities for only
`tracktion_Plugin.h`, `tracktion_EditNodeBuilder.cpp` and
`tracktion_SummingNode.h`. The original source was
checked against the existing archive with its dependency-lock SHA256. Apply with
`scripts/apply-sidechain-bus-patch.ps1`; `-VerifyOnly` validates all three files and
patch paths without modification. Unexpected vendor bytes fail before any
target is changed. Reapplication to the exact patched bytes is idempotent.

Temporary-copy patch/reverse checks are engineering evidence only. Integration
still requires the parent build, actual graph rendering, independent detector/main
PCM oracles and the complete Mixer acceptance requirements. No FL code/assets
were involved, and this patch is not a separately packaged feature.
