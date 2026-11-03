# Native workspace redesign: 0.0.2

Current scrollbar refinement follows the user's explicit preference:
[SLIM_SCROLLBARS.md](SLIM_SCROLLBARS.md) records slimmer reserved gutters and
hidden Rack vertical navigation with plain-wheel Channel scrolling. The
historical redraw evidence below is not current-build qualification.

The visual target is the user-supplied `Reference/VibeRa Studio_ Midnight Motion.png`. All six PNGs, the ICO and the SVG's embedded raster were actually opened. `Logo.png` is RGBA with damaged contours and stray pixels. The standalone icons have the same edge damage; the supplied SVG embeds a raster rather than vector outlines. The reference files were preserved. The two-ribbon V was faithfully redrawn as smooth vector geometry, rendered with antialiasing and transparent background, and embedded in the app. The application title uses native Segoe UI; no proprietary artwork or fonts were extracted.

## Result and intentional differences

- Charcoal surfaces, restrained teal accents, colored source clips/steps/inserts, Segoe UI, consistent compact headers and controls.
- Browser uses a real resizable divider and retains original sound audition, dragging and sample import. Its project summary reflects the current document. No fake folder library or nonfunctional search field.
- Playlist occupies the full primary width. Piano Roll and Channel Rack occupy the second row. Mixer occupies the bottom, supports hiding and maximising, and reduces to a header below 800 logical pixels of window height. Panel dividers redistribute space on resize; hidden editors release their area. Each header supports hide/maximise/restore. Familiar F-key commands remain configurable.
- Piano Roll adapts the number of visible pitches to its height, fits the pattern horizontally, shows actual MIDI black/white key groupings, correct FL octave labels and velocities. Ctrl+wheel zoom, Shift+wheel pitch scroll and wheel time scroll are additional navigation. Pattern/channel changes and resizing reveal the actual notes.
- Playlist rulers follow actual project meter, lanes remain independent of channels/inserts, and thumbnails show actual pattern notes in the placement's source window. Linked patterns and Make Unique are unchanged. No invented audio waveforms, section markers or chords.
- Channel Rack retains sample replacement, two-button step editing, melodic previews and routing. Steps page when needed rather than shrinking below 14 logical pixels. The displayed step range is explicit; paging is clamped after pattern/size changes.
- Mixer retains actual level control and channel assignment. Unsupported meters, effects, sends, mute/solo controls and CoProducer generation controls were omitted. `docs/feature/CoProducer.md` was absent; the actual specification at `docs/CoProducer.md` was read and left untouched. No AI backend is implemented or qualified.

No project-schema, audio-engine, sampler or command/macro implementation changes were needed. The same commands, grouped undo, persistence and Tracktion renderer back the redesigned editors. Initial native keyboard focus was corrected after OS-input testing exposed a failure missed by direct-handler tests. Screenshot output now truncates reused PNG streams so evidence shows the current application.

## Evidence categories

1. Automated musical acceptance: 78 assertions; existing sequencing, rendering, routing, project round trips, failure preservation, undo/redo, profiles and macros.
2. Automated GUI acceptance: actual editor mouse/shortcut handlers and document operations; hide/resize/maximise/restore without changing musical state. Bounds and toolbar checks at 1920x1040 @100%, 1280x690 @150%, 3840x2080 @100%, 2560x1386 @150%, 1920x1040 @200%. These are logical layout plus raster-scale simulations, not Windows display-setting changes. PNGs include populated projects and Settings tabs. Files: build/windows/gui_acceptance.
3. Native desktop: running executable with a real saved Midnight Session project on the actual 3840x2160 primary monitor at 144 DPI (150%). Captured client area 3840x2054, plus a resized 1920x1080 outer window with 1897x1024 client. OS synthetic F6/F9 and Browser divider drag passed and were visually inspected. Captures are screenshots from the Windows screen, not mockups or component snapshots. Files: build/windows/desktop_acceptance. The subsequent final-binary repeat, including additional header-click maximise/restore, was blocked by Windows foreground ownership while Firefox was active. The harness sent no input/capture when ownership was denied. Existing desktop images predate final nonvisual note-hit/page guards; current-binary component renders and both regression suites pass. See desktop-observation.md and the partial desktop-verification.txt; final executable provenance does not claim a completed native repeat.
4. Listening / hardware / human input: not performed. Device names in the status bar do not establish heard audio, measured latency or hardware qualification. No physical 1080p monitor or Windows scale-change/mixed-DPI test was performed.

All generated populated viewport and successful native desktop screenshots were opened and inspected. Corrected visible defects include missing bass pitches after resize, stale Settings screenshots and initial native shortcut focus. Remaining limitations: native maximise/restore input and the final desktop repeat are pending; layout/zoom are session-local; undocking and screen-reader qualification are pending; very large projects and graphics/frame-time stress are not qualified; full audio real-time audit and uninterrupted graph edits remain pending. Review does not establish user acceptance of the visual style or complete FL workflow parity.

## Reproduce

Build/test using AGENTS.md commands. CTest regenerates fresh screenshots, `midnight-session.vibra`, the editable eight-bar workflow project and WAV, and profile evidence. Run the opt-in visible desktop harness with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-desktop.ps1
```

It opens and gracefully closes only its own application instance, refuses input/capture without foreground ownership, captures only VibeRa's client area, restores the cursor, and records evidence. It must not be silently used as a general background test while someone else is interacting with the desktop. Optional developer branding regeneration: `python scripts/generate-branding.py` (requires Pillow). Checked-in SVG/PNG/ICO assets make ordinary builds independent of Python.

## Preview 0.0.15 navigation and appearance
See [WORKSPACE_NAVIGATION.md](WORKSPACE_NAVIGATION.md) for the implemented 18-strip Mixer, Playlist toolbar and H/V scrollbars, overflow viewports and saved colour palette. Evidence and pending native musical/hardware/FL parity gates are kept separate.

## Movable and rounded workspace (0.0.16)
Header dragging creates internal floating panels; Dock and Tools > Restore docked workspace layout restore positions. Other docked geometry stays stable. Rounded alpha clipping includes children and hit testing. Native OS corner preference is optional on supported Windows versions. Frame geometry, layering and UI compositing remain separate from project/engine state; no persistence across app restarts yet. See PREVIEW_0_0_16.md for exact GUI/native/evidence boundaries.
