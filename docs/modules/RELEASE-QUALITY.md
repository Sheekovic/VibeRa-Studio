# M6 release quality module

Ledger: [RELEASE-QUALITY.json](RELEASE-QUALITY.json). Every software row is
release-required; human/clean-machine checks are listed separately in the ledger.

## Complete checklist

- [x] **RQ-A installer and portable package:** Per-user installer and portable folder built from the qualified distribution: install, launch, upgrade over an older version, uninstall entry and clean removal without touching user projects/settings.
- [ ] **RQ-B long-session stability:** Two-hour unattended playback/edit stress on the real device: no crash, leak growth, worker loss, stuck notes or callback overruns beyond the declared device limits; memory/handle/worker counts recorded over time.
- [x] **RQ-C crash and forced-termination recovery:** Forced app and worker termination during edit, playback, recording and save: autosave/recovery restores the last committed state, takes are not lost and outcomes are truthful.
- [x] **RQ-D project migration:** Projects from earlier schema versions open, migrate and save without losing audible settings or links; refusals are explicit for unsupported versions.
- [x] **RQ-E accessibility:** Keyboard-only navigation of the main workspace and dialogs, focus order and visible focus, accessible names/roles for controls, readable contrast and target sizes.
- [x] **RQ-F scalable ui and windows:** 100-200 percent scale and 1080p-4K layouts without clipping/overlap; detached windows open in front, restore on the right monitor and survive monitor disconnect.
- [x] **RQ-G diagnostics:** User-visible diagnostics export (version, device, plugin workers, recent errors) without personal paths or credentials; actionable error messages.
- [x] **RQ-H source, notices and checksums:** Package ships licenses/notices for every bundled dependency and asset, exact source commit and checksums; reproducible build inputs recorded.
- [ ] **RQ-I exact module release:** Complete source suite, module gate, staged/final installed verification and distribution audit on exact identities before replacing the sole distribution.
