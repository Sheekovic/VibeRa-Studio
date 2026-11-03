# Metronome, count-in and live recording preview (0.0.8)

The toolbar Click button and Ctrl+M enable the metronome. Count-in/Ctrl+P toggles the last nonzero recording lead-in; right-click selects Off, one bar or two bars. Settings > Transport controls original Click/Wood/Beep sounds, level and project time signature. Existing customized shortcut profiles remain intact; new commands can be bound in Settings > Shortcuts.

Arm Rec, enable count-in, then Play. The audio timeline supplies the lead-in, not a UI timer. MIDI is monitored during the lead-in but events before the recording boundary are not stored. Orange pending notes/velocity and a provisional Song placement update during recording. Stop keeps one undoable take; Esc cancels. Pending notes are previews, not editable canonical notes. Document editing or seeking still finishes the take first. Controller/pedal/bend recording, score logging, typing-keyboard input and MIDI-file interchange remain pending.

Tempo is quarter-note BPM; the metronome pulse follows the denominator and accents the numerator. Project meter is persisted and undoable; new patterns use one bar. Existing patterns keep their musical duration. Guide choices are local preferences. Original prepared PCM clicks route through Master effects/fader and are omitted from offline WAV. Custom sounds and Mixer destination selection remain pending.

## Implementation and experiments

Prepared immutable PCM and fixed DSP state avoid callback allocation, file I/O, blocking locks and object destruction in the new guide path. Atomic control transfers count-in state; per-instrument gates suppress scheduled notes until the requested cursor while retaining live MIDI monitoring. Tracktion roll-in and posted context position preserve the initial count-in instead of allowing performPlay's queued seek to overwrite it. This does not establish a full upstream real-time audit.

Initial loop-toggle/negative-position and roll-in-only experiments started music early. Posting the initial context position fixed those cases. Tracktion can begin one sample past the requested start; bounded one-sample tolerance and timestamp deduplication prevent losing the first click without duplicating pulses. An intermediate expanded built-in Song-at-positive-cursor run produced no music; later focused and full regressions pass, but long-session/hardware qualification remains pending and the intermittent result is not explained conclusively.

An installed Analog Lab fixture initially reused an instance with a release tail, producing PCM before the requested new-note onset. The failure log is retained. Fresh instance identities isolate new-note onset without cutting product voices or resetting instrument state to hide the tail.

## Verification scope

Tests cover exact pulse frames at 44.1/48 kHz, 7/8 accents/count boundaries, guide preferences, meter commands/undo/redo, Pattern/Song at zero/positive cursor, unchanged graph/device lifecycle counters, early MIDI exclusion, live previews and Esc cancellation. Built-in first music starts at frame 96001 for a 96000-frame lead-in. Installed Analog Lab first new-note PCM is frame 96670, including declared IPC/plugin latency and attack; this is not sample-perfect plugin attack or hardware latency evidence. Native captures and GUI component renders are separate from physical input and listening. No assistant listening or physical recording test was performed.

Documented behavior: [FL transport/toolbars](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/toolbar_panels.htm), [FL project settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/songsettings_settings.htm), accessed 2026-10-12. Installed FL interactions were not directly observed; the current manual may differ from installed 24.2.1.4526. VibeRa code and click PCM are original.
