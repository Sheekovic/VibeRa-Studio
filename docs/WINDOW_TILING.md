# Optional automatic window tiling

Development 0.0.24 builds on the existing docked, movable, rounded and detached editors. Use **Tools > Arrange windows**. Default classic docking and existing editor shortcuts remain familiar.

- **Balanced auto tiling** arranges visible Channel Rack, Piano Roll, Playlist and Mixer without overlaps. The Browser retains its own resizable left column. Tiles reflow as the workspace changes size or editors are hidden/shown.
- **Focus active editor** gives the active editor a prominent region and arranges other visible editors beside/below it. Explicit editor opening/window shortcuts change the primary editor; ordinary note clicks do not move panels under a gesture. Compact spaces fall back to a balanced arrangement.
- **Free window movement** preserves the current arrangement and allows independent dragging/resizing. Dragging a tiled editor also leaves automatic mode. Maximize/restore retains automatic mode.
- **Tile detached editors on their monitors** arranges existing visible detached core editors within their current monitor work areas, including negative-origin desktops. It does not detach extra editors, move user applications, or move panels to another monitor. If a monitor cannot fit native minimum sizes, its existing windows are preserved.
- **Restore classic docked layout** remains available. The stable workspace commands can be assigned in Settings > Shortcuts; no new default key bindings are imposed.

Layout changes run on the JUCE message thread and do not invoke musical commands, finish MIDI takes, rebuild audio, or write project history. Workspace layout format 2 saves mode and primary editor separately from project schema 7. Valid format 1 layouts migrate to manual mode; malformed mode/geometry rejects atomically. Older applications may reject format 2 workspace layouts.

Scope limits: instrument/plugin/settings auxiliary windows are not automatically tiled. Detached arrangement is a one-shot command, not a continuously enforced desktop manager. No physical multi-monitor/DPI, human familiarity or listening qualification is implied by geometry and component tests. The existing desktop recovery behavior remains separate.

Verification and exact build/package state are recorded in STATUS.md and VERIFICATION.md. Tests exercise usable geometry, no overlap, negative origins, visibility, maximize/restore, manual movement, layout migration/invalid input, actual detached peers and Pattern/Song virtual callback lifecycle preservation. Fresh compact, 1080p and scaled 4K component renders must be inspected after implementation changes.
