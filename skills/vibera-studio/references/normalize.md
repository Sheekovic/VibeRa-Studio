# Sampler Normalize (0.0.58)

Discover the connected application's typed contracts first. Use stable Channel
IDs and the normal projectIdentity/revision guards. Normalize applies to sample
and original-instrument Channels; VST3 instruments and streamed Audio Clip
Channels are unsupported and fail without changing the document.

`vibera_command` with `command="channel.sampler.normalize"` and
`args={"channel":id,"enabled":true}` enables it; false disables. The command
defaults to true when enabled is omitted; a new project's setting defaults off.
Only channel/enabled arguments are meaningful. GUI checkbox, shortcut/controller
toggle, macro and MCP setter share the same application validation and history.
Repeating the setter is idempotent; a real change has one Undo/Redo step.
The existing macro editor uses Amount0=off/1=on for Normalize; invalid values
are refused. Core multi-command and explicit-off macro behavior is tested.
General MCP macro authoring/profile persistence remains an inherited coverage
gap; do not claim that managing saved macros is exposed by this setting.

This is whole-source stereo peak normalization to unity before velocity, Channel
and Mixer gain, envelopes and voice sums. It is not a master limiter or loudness
target. One shared gain preserves stereo balance and polarity; silence remains
silent. Original files and PCM, pitch, trim, loop points and note data are retained.
Trimming to a quieter section does not renormalize that section. Exact installed
FL trim-order behavior remains unqualified. Already sounding sample voices
smoothly approach the new gain over about5ms; new notes use it immediately.

Read `channels[].sampler.normalize`, `normalizationAvailable`, `sourcePeak` and
`normalizationGain`. Peak is the whole loaded source's absolute maximum across
both channels. Gain is its reciprocal, or1 for silence, even if Normalize is off.
Unavailable preparation reports null peak/gain. Wait for pendingAudioUpdate to
clear before treating the current prepared source as projected audio. Document
command completion is not proof of heard output or physical hardware behavior.

The Channel settings checkbox is beside Reverse and Volume envelope. Its
waveform reflects the requested normalized level. The separately labelled
original-source preview stays raw; use keyboard/score/MIDI to audition settings.
MIDI takes should remain active through this nonstructural setting. Do not cancel
or finish a user's take to retry an unrelated operation.

Example: inspect Channels; target the intended sample; save/read its current
setting; enable Normalize; wait for projection; audition; inspect the same Channel;
Undo/Redo for comparison; save the project and export a finite Song WAV. Poll
save/open/export job IDs through real completed/failed/cancelled outcomes. Verify
file existence/duration/PCM as appropriate. Recover a timeout by reading state/job
result before repeating mutations. Source files must remain unchanged.

Schema15 stores the flag. Real older files migrate with Normalize off; modern
flags disguised as old schemas are rejected. Do not relabel project versions.
Current qualification and remaining listening/native/hardware evidence belong
to the repository STATUS/VERIFICATION, not this recipe alone.
