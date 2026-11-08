# M5 tuning and maqam module

Ledger: [TUNING.json](TUNING.json).

## Complete checklist

- [x] **TM-A per-channel 12-tone tuning and maqam presets:** Cents per pitch class, maqam presets on any tonic, one exact Undo, limits and persistence.
- [x] **TM-B scala files:** 12-note Scala tables and any-size Scala scales mapped to the keyboard; invalid files refused.
- [x] **TM-C keyboard-mapped scales:** channel.scale degree cents with a root key, Undo and schema gate.
- [x] **TM-D tuning to plugin instruments:** MPE member-channel bends and MTS-ESP source; explicit refusal where unsupported.
- [x] **TM-E maqam note snapping:** notes.maqam quarter-tone snapping that keeps tuned notes.
- [x] **TM-F rendered pitch:** Offline renders of a tuned Channel measure the expected frequency (Rast E -50 cents, A unchanged, Undo).
- [x] **TM-G mcp and skill guidance:** channel.tuning/scale/mpe/mtsEsp and notes.maqam are canonical commands with skill guidance (score-tools.md).
- [ ] **TM-H exact module release:** Complete suite, module gate and packaged verification before replacing the distribution.
