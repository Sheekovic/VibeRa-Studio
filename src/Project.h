// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerEq.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <cstdint>
#include <optional>
#include "PluginHost.h"
#include "SamplerSettings.h"
#include "Automation.h"
#include "ChannelAppearance.h"
#include "SamplerControllerLinks.h"
#include "TempoMap.h"

namespace vibera
{
using Id = std::int64_t;
struct Note
{
    Id id = 0, channel = 0;
    double beat = 0, length = .25;
    int pitch = 60;
    float velocity = .8f;
    bool step = false;
    bool muted = false; // Score state only; retain source properties and live-input ownership.
    double pan = 0, fineCents = 0; // Local sampler expression: +/-1 balance, +/-100 cents.
    double shiftPercent = 0;       // Score timing:0..100, applied once after raw-occurrence Swing.
    int repeatMode = 0;            // Published Repeat rate index0..14; raw source duration remains unchanged.
    double releaseVelocity = 0;    // Standard normalized MIDI note-off data; not sampler ADSR.
    double modX = .5, modY = .5;   // Local sampler cutoff/resonance offsets; .5 is neutral.
    bool slide = false, portamento = false; // Independent local-sampler source note modes.
    int colourGroup = 0;                    // Per-note voice group0..15 (UI1..16), not a DAW Channel/group.
    Id group = 0; // Note group (Shift+G): clicking one note selects the whole group; 0 = none.
};
// Piano roll event editor target (FL "Target control"): the note property the lane under the
// notes shows and paints. Lane values are normalised 0..1; Pan and Fine pitch are centred.
inline const juce::StringArray &noteLaneTargets()
{
    static const juce::StringArray names{"velocity", "release", "pan", "modX", "modY", "fine"};
    return names;
}
inline double noteLaneValue(const Note &n, int target)
{
    switch (target)
    {
    case 1: return n.releaseVelocity;
    case 2: return (n.pan + 1) / 2;
    case 3: return n.modX;
    case 4: return n.modY;
    case 5: return (n.fineCents + 100) / 200;
    default: return n.velocity;
    }
}
inline double noteLaneDefault(int target) { return target == 0 ? .78 : target == 1 ? 0. : .5; }
struct Channel
{
    Id id = 0;
    juce::String name, source; // builtin:kick/snare/hat/bass, or external WAV path
    int mixer = 0;
    float gain = .7f, pan = 0;
    bool muted = false;
    std::optional<PluginSpec> instrument;
    bool audioClip = false;
    double audioDuration = 0; // Full source duration in seconds; only for Audio Clip Channels.
    // Cents added to every note of each pitch class (C..B), +/-100 (version 47). The local
    // sampler plays it with each note's own fine tuning; plugins only through `mpe` below.
    std::array<double, 12> tuning{};
    // Keyboard-mapped scale of any size (version 53): degree cents (last = period) from
    // scaleRoot; non-empty overrides the 12-tone table above. See tuning::scaleDeviation.
    std::vector<double> scale;
    int scaleRoot = 60;
    // Plugin instruments (version 56): play the tuning above through MPE, each note on its
    // own member channel (2..16) with a pitch bend of the note's cents (+/-48 semitone range).
    bool mpe = false;
    // Broadcast this Channel's tuning to MTS-ESP client plugins (version 56; one Channel).
    bool mtsEsp = false;
    SamplerSettings sampler;
    Id group = 0;        // Display group only; zero means Unsorted, never a routing boundary.
    double swingMix = 1; // Multiplier of global Swing; source notes remain unchanged.
    bool truncateSwing = false;
    juce::uint32 colour = 0;    // Opaque display colour; zero follows the theme palette.
    juce::String icon = "none"; // Stable original icon ID, independent of source/plugin identity.
    bool muteLocked = false;    // Protect the current score mute from automatic solo/bulk changes.
    float pitchBend = 0;        // Plugin instrument Channel PITCH: MIDI pitch bend -1..1 sent with its notes.
    int pitchRange = 2;         // RANGE shown with PITCH, semitones at full bend (1..48).
    std::map<int, juce::String> keyLabels; // Piano roll key names (drum maps), pitch -> at most 32 chars.
};
struct ChannelGroup
{
    Id id = 0;
    juce::String name;
};
enum class ChannelLoopMode
{
    fixed,
    step,
    beat,
    bar
};
struct ChannelLoop
{
    Id channel = 0;
    ChannelLoopMode mode = ChannelLoopMode::fixed;
    double length = 1; // Fixed quarter-note beats; automatic modes store the neutral value1.
};
// A recorded MIDI controller event in a Pattern (version 46): type 0 = CC `number`
// (value 0..1), 1 = pitch bend (value -1..1), 2 = channel pressure (value 0..1).
// Played to plugin instruments on `midiChannel`; the local sampler ignores them.
// type 0 CC (number, value 0..1), 1 pitch bend (value -1..1), 2 channel pressure (value 0..1),
// 3 program change (number = program 0..127).
struct ControllerEvent
{
    Id id = 0, channel = 0;
    double beat = 0, value = 0;
    int type = 0, number = 0, midiChannel = 0;
};
struct Pattern
{
    Id id = 0;
    juce::String name;
    double length = 4;
    std::vector<Note> notes;
    std::vector<ControllerEvent> controllers; // Recorded CC/pitch bend/pressure, by beat.
    juce::String generation; // Optional historical CoProducer settings; inert bounded metadata.
    juce::uint32 colour = 0; // Optional opaque display colour; zero follows the theme palette.
    juce::String group;      // Picker filter metadata; never a routing or playback boundary.
    std::vector<ChannelLoop> channelLoops; // Per Pattern+Channel; absent means no repetition.
    bool autoLength = true;                // Canonical score edits recompute the last occupied musical bar.
    int numerator = 0, denominator = 0;    // Both zero inherit the document meter.
    double launchQuantum = -1;             // Performance launch step in beats: -1 one bar, 0 immediately.
    // Performance Follow Action (version 57): after `followAfter` plays the launched pad switches
    // to another Pattern. Actions: "" none, again, next, previous, first, last, any, other, stop.
    juce::String followAction;
    int followAfter = 1;
    // Pattern automation (v56): windows of project automation sources at Pattern beats (lane and
    // id unused). Plays with the Pattern in Song and Pattern mode (PatternAutomation.h).
    std::vector<AutomationPlacement> automation;
};
// -1 (one bar of the Song meter), 0 (immediately) or 1/16 beat .. 64 beats.
inline bool validLaunchQuantum(double beats) noexcept
{
    return beats == -1 || beats == 0 || (std::isfinite(beats) && beats >= .0625 && beats <= 64);
}
struct Placement
{
    Id id = 0, pattern = 0;
    int lane = 0;
    double beat = 0, length = 4, offset = 0;
    bool muted = false;
};
struct ClipGroup
{
    Id id = 0;
    std::vector<Id> members; // Arrangement placements of every Clip type, never source/routing groups.
};
struct MixerRoute
{
    int target = 0;
    float gain = 1;
    bool operator==(const MixerRoute &) const = default;
};
struct MixerInsert
{
    int index = 0;
    juce::String name;
    float gain = .8f, pan = 0;
    bool muted = false;
    std::vector<PluginSpec> effects;
    std::vector<MixerRoute> routes;
    juce::uint32 colour = 0; // Zero follows the theme; otherwise opaque ARGB.
    bool effectsBypassed = false; // Effective slot bypass override, individual flags/state retained.
    float stereoSeparation = 0; // -1 subtract opposite channel;0 original;+1 mono.
    bool phaseInverted = false, stereoSwapped = false;
    MixerEqSettings eq; // Integrated 3-band EQ after the effect slots (flat by default).
    int dock = 0;       // Display docking: -1 left, 0 middle, +1 right (layout only, never routing).
    float delayMs = 0;  // Track delay: a time offset 0..500 ms after the EQ (alignment, not an echo).
    MixerInsert(int i=0, juce::String n={}, float g=.8f, float p=0, bool m=false,
                std::vector<PluginSpec> fx={})
        : index(i), name(std::move(n)), gain(g), pan(p), muted(m), effects(std::move(fx))
    { if (i != 0) routes.push_back({0, 1}); }
};
struct MixerGroup
{
    juce::String id, name; // Stable UUID; never recycled by Undo/Redo or a display row.
    juce::uint32 colour = 0;
    std::vector<int> members; // Non-Master stable IDs; display/selection only, never routing.
};
struct AudioPlacement
{
    Id id = 0, channel = 0;
    int lane = 0;
    double beat = 0, offset = 0, duration = 0; // Source window in seconds; no automatic stretching.
    bool muted = false;
    double fadeIn = 0, fadeOut = 0; // Seconds at the clip edges; fadeIn + fadeOut <= duration.
    // Warped to the Song tempo (version 56): the original Channel and source window played
    // at warpBpm. The app re-renders the warp when the tempo map or clip position changes.
    double warpBpm = 0, warpOffset = 0, warpDuration = 0;
    Id warpChannel = 0;
};
// A Song tempo change (project version 45). The base `tempo` applies from beat 0; each
// change applies from its beat, or ramps linearly into it from the previous tempo.
struct TempoChange
{
    Id id = 0;
    double beat = 0, bpm = 120;
    bool ramp = false;
};
// A Song time-signature change (version 54) at a bar line of the meter before it.
struct MeterChange
{
    Id id = 0;
    double beat = 0;
    int numerator = 4, denominator = 4;
};
// Where a beat falls in the Song's bars: 0-based bar index, beats into the bar, bar length.
struct BarPosition
{
    int bar = 0;
    double beatInBar = 0, barLength = 4;
    int numerator = 4, denominator = 4;
};
// Chord Track event (version 57): a chord symbol (e.g. "Am7", "F#maj7", "C/E") from `beat` until
// the next event. Song-wide, like markers; notes can follow it (notes.followChords).
struct ChordEvent
{
    Id id = 0;
    double beat = 0;
    juce::String symbol; // At most 32 characters; must parse with score::chordPitchClasses.
};
struct Marker
{
    Id id = 0;
    double beat = 0;    // Song position in quarter-note beats.
    juce::String name;  // At most 128 characters.
};
// Playlist track settings by lane (version 50); lanes without an entry use the defaults.
struct PlaylistTrack
{
    int lane = 0;            // 0..127
    juce::String name;       // Empty shows "Track n"; at most 128 characters.
    juce::uint32 colour = 0; // Opaque ARGB; zero follows the theme.
    bool muted = false;      // Silences every clip on the track without touching clip mute.
    bool groupedAbove = false; // Joins the group of the track above (version 51).
    bool collapsed = false;    // On a group's first track: hide the grouped tracks (view only; they play).
    bool locked = false;       // Clips on this track cannot be added, moved, edited or deleted (v56).
    double height = 1;         // Row height relative to the Playlist lane height, .5..3 (v56).
    Id channel = 0;            // Instrument track: drawing makes Patterns for this Channel (v56).
    bool audio = false;        // Audio track: audio recordings land here first (v56).
};
// A stored (inactive) Playlist arrangement (version 48); see Arrangements.h.
struct Arrangement
{
    Id id = 0;
    juce::String name;
    std::vector<Placement> placements;
    std::vector<AudioPlacement> audioPlacements;
    std::vector<AutomationPlacement> automationPlacements;
    std::vector<ClipGroup> clipGroups;
    std::vector<Marker> markers;
};
struct Project
{
    static constexpr int currentVersion = 57;
    static constexpr int maxChannels = 999; // Channel Rack capacity (FL-sized projects).
    int version = currentVersion;
    Id nextId = 1;
    juce::String name = "Untitled";
    double tempo = 120;
    double swing = 0; // Normalized 0..1, all score Patterns/Channels.
    int numerator = 4, denominator = 4;
    std::vector<Channel> channels;
    std::vector<ChannelGroup> channelGroups;
    std::vector<Pattern> patterns;
    std::vector<Placement> placements;
    std::vector<ClipGroup> clipGroups;
    std::vector<Marker> markers; // Playlist markers (version 44); display order is by beat.
    std::vector<ChordEvent> chords; // Chord Track (version 57), sorted by beat.
    std::vector<TempoChange> tempoChanges; // Song tempo map (version 45), strictly increasing beats.
    std::vector<PlaylistTrack> tracks;     // Sparse, sorted by lane (version 50).
    std::vector<MeterChange> meterChanges; // Song time signatures after the first (v54), by beat.
    BarPosition barPosition(double beat) const
    {
        BarPosition at;
        int numerator = this->numerator, denominator = this->denominator, bars = 0;
        double from = 0;
        for (const auto &change : meterChanges)
        {
            if (change.beat > beat + 1.e-9)
                break;
            const double length = numerator * 4. / denominator;
            bars += int(std::round((change.beat - from) / length));
            from = change.beat;
            numerator = change.numerator;
            denominator = change.denominator;
        }
        at.barLength = numerator * 4. / denominator;
        const double into = beat - from;
        const int whole = int(std::floor(into / at.barLength + 1.e-9));
        at.bar = bars + whole;
        at.beatInBar = into - whole * at.barLength;
        at.numerator = numerator;
        at.denominator = denominator;
        return at;
    }
    // The first bar line at or after `beat`.
    double nextBarLine(double beat) const
    {
        const auto at = barPosition(beat);
        return at.beatInBar < 1.e-9 ? beat : beat - at.beatInBar + at.barLength;
    }
    // Performance scenes 1..8: the Patterns each scene launches together (version 52).
    std::array<std::vector<Id>, 8> performanceScenes;
    // Performance pads 1..16 (version 55): an assigned Pattern, or 0 for the n-th Pattern.
    std::array<Id, 16> padPatterns{};
    Id padPattern(int index) const
    {
        if (index < 0 || index >= 16)
            return 0;
        if (padPatterns[size_t(index)] && pattern(padPatterns[size_t(index)]))
            return padPatterns[size_t(index)];
        return index < int(patterns.size()) ? patterns[size_t(index)].id : 0;
    }
    const PlaylistTrack *track(int lane) const
    {
        for (const auto &t : tracks)
            if (t.lane == lane)
                return &t;
        return nullptr;
    }
    // Playlist lanes shown top to bottom: tracks grouped under a collapsed head are hidden.
    std::vector<int> visiblePlaylistLanes() const
    {
        std::vector<int> lanes;
        int head = 0;
        for (int lane = 0; lane < 128; ++lane)
        {
            const auto *t = track(lane);
            if (lane == 0 || !t || !t->groupedAbove)
                head = lane;
            else if (const auto *h = track(head); h && h->collapsed)
                continue;
            lanes.push_back(lane);
        }
        return lanes;
    }
    bool laneMuted(int lane) const
    {
        const auto *t = track(lane);
        return t && t->muted;
    }
    // The current arrangement is the placement fields above; others are stored here.
    Id arrangementId = 0;          // 0 until a second arrangement exists.
    juce::String arrangementName;  // Empty shows as "Arrangement 1".
    std::vector<Arrangement> arrangements;
    std::vector<AudioPlacement> audioPlacements;
    std::vector<MixerInsert> mixer;
    std::vector<MixerGroup> mixerGroups;
    AutomationDocument automation;
    std::vector<SamplerControllerLink> samplerControllerLinks; // Project-owned fixed sampler CC destinations.
    struct SoloState
    {
        Id channel = 0;
        std::vector<Id> mutedBefore;
        bool groupScoped = false;
        Id group = 0;             // Captured filter-group identity; zero is Unsorted when groupScoped.
        std::vector<Id> affected; // Unlocked members owned by this scope; never visible-row indices.
    };
    std::optional<SoloState> solo;
    struct MixerSoloState
    {
        int insert = 0;
        bool connected = false;
        std::vector<int> mutedBefore;
        juce::String group; // Empty retains legacy single-insert solo; otherwise one UUID group owns it.
    };
    std::optional<MixerSoloState> mixerSolo;
    Id allocate()
    {
        return nextId++;
    }
    Pattern *pattern(Id);
    const Pattern *pattern(Id) const;
    Channel *channel(Id);
    const Channel *channel(Id) const;
    // The original source of tempo-fitted Audio Clips that no clip plays any more: kept for
    // re-fitting, but not shown as a separate sample (the fitted Channel stands in for it).
    bool warpOriginalOnly(Id id) const
    {
        bool original = false;
        for (const auto &clip : audioPlacements)
        {
            if (clip.channel == id)
                return false;
            original = original || (clip.warpBpm > 0 && clip.warpChannel == id);
        }
        return original;
    }
    ChannelGroup *channelGroup(Id);
    const ChannelGroup *channelGroup(Id) const;
    PluginSpec *plugin(Id);
    const PluginSpec *plugin(Id) const;
    AutomationSource *automationSource(Id);
    const AutomationSource *automationSource(Id) const;
    std::vector<AutomationInitialValue> automationDefaults() const;
    double endBeat() const;
    // Song timing through the tempo map. Pattern mode plays at the base tempo.
    TempoMap songTempo() const;
    double songSeconds(double beat) const;
    double songBeat(double seconds) const;
    // Beat where an Audio Clip of `seconds` starting at `beat` ends in the Song.
    double audioEndBeat(double beat, double seconds) const;
    juce::ValueTree state() const;
    static juce::Result parse(const juce::ValueTree &, Project &);
    juce::Result validate() const;
    static Project empty();
    static Project demo();
};
// Pure model selection. Metadata is not native bus-negotiation authority.
PluginMainIo pluginMainIoFor(const PluginSpec &) noexcept;
juce::Result setPluginMainIo(Project &, Id, const PluginMainIo &);
// Positive classification for fresh-captured I/O-only Undo/Redo, both plugin roles.
bool purePluginIoHistory(const Project &, const Project &);
struct ScheduledNote
{
    Id channel;
    double beat, length;
    int pitch;
    float velocity;
    Id note = 0, placement = 0;
    bool step = false;
    double pan = 0, fineCents = 0;
    std::uint32_t repeatOrdinal = 0; // Prepared occurrence identity; never a persisted Note/Clip ID.
    double releaseVelocity = 0;
    double modX = .5, modY = .5;
    bool slide = false, portamento = false;
    int colourGroup = 0;
};
std::vector<ScheduledNote> sequence(const Project &, bool patternMode, Id selectedPattern);
juce::Result saveProject(const Project &, const juce::File &);
// Atomic write keeping absolute media paths (no media collection): autosaves and backups.
juce::Result saveProjectSnapshot(const Project &, const juce::File &);
juce::Result loadProject(const juce::File &, Project &);
juce::String sourceName(const juce::String &);
} // namespace vibera
