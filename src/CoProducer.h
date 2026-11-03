// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"

namespace vibera::coproducer
{
enum class Provenance
{
    extracted,
    userSpecified,
    inferred,
    unknown
};
struct Region
{
    double start = 0, length = 4; // Quarter-note units, never denominator beats.
};
struct Chord
{
    double start = 0, length = 4; // Absolute musical timeline.
    juce::String label;
    std::vector<int> pitchClasses;
    Provenance provenance = Provenance::userSpecified;
};
struct InstrumentProfile
{
    juce::String family = "Unknown";
    int low = 0, high = 127, polyphony = 0; // Zero means unknown, not monophonic.
    bool fractionalPitch = false;
    Provenance provenance = Provenance::unknown;
};
struct ModelInstrumentMapping
{
    Id channel = 0;
    int program = -1;         // Explicit zero-based GM; 128 is percussion, -1 is unknown.
    std::vector<int> pitches; // Explicit range/drum notes; no plugin-name inference.
    Provenance provenance = Provenance::unknown;
};
struct Selection
{
    bool patternMode = true;
    Id pattern = 0, destination = 0;
    Region region;
    std::vector<Id> notes, placements;
};
struct Options
{
    juce::String projectIdentity;
    std::uint64_t revision = 0;
    Selection selection;
    double preceding = 4, following = 4;
    bool automaticTracks = true;
    std::vector<Id> tracks; // Explicit override includes muted tracks deliberately.
    std::vector<Id> lockedNotes;
    std::vector<Chord> chords;
    std::vector<PluginRuntimeState> pluginRuntime; // Local runtime validation; never sent to models.
    InstrumentProfile destinationProfile;
    std::vector<ModelInstrumentMapping> modelMappings;
    size_t maxEvents = 8192, maxExaminedNotes = 200000;
};
struct ContextEvent
{
    Id note = 0, channel = 0, pattern = 0, placement = 0;
    double start = 0, length = 0; // Relative to target start; may be negative.
    double sourceBeat = 0, sourceLength = 0;
    int pitch = 60;
    float velocity = 0;
    bool audible = false, inTarget = false, locked = false;
};
struct ContextTrack
{
    Id channel = 0;
    int mixer = 0;
    juce::String name, instrument; // Display hints; no file paths or plugin state.
    bool audible = false;
    float gain = 0, pan = 0;
    juce::String destinationDigest;
    bool included = true; // False means destination metadata only, not context notes.
};
struct ContextPlacement
{
    Id placement = 0, pattern = 0;
    int lane = 0;
    double start = 0, length = 0, offset = 0, sourceLength = 0;
};
struct Snapshot
{
    static constexpr int schemaVersion = 1;
    juce::String projectIdentity, fingerprint;
    std::uint64_t revision = 0;
    Selection selection;
    bool contextAutomatic = true;
    double tempo = 120;
    int numerator = 4, denominator = 4;
    std::vector<ContextTrack> tracks;
    std::vector<ContextPlacement> placements;
    std::vector<ContextEvent> events;
    std::vector<Chord> chords;
    std::vector<PluginRuntimeState> pluginRuntime; // Only relevant instruments/routed effects.
    InstrumentProfile destinationProfile;
    std::vector<ModelInstrumentMapping> modelMappings;
    size_t omittedEvents = 0, examinedNotes = 0;
    bool complete = true;
    juce::StringArray limitations;
    double quarterNotesPerBar() const
    {
        return numerator * 4.0 / denominator;
    }
    juce::String summary() const;
};

// Call on an owning/control thread with a consistent Project value, never from DSP.
// The returned snapshot owns every value; a worker never sees mutable engine objects.
bool channelAudible(const Project &, Id); // Same logical audibility policy used by collection.
juce::Result collect(const Project &, const Options &, Snapshot &);
juce::Result checkFresh(const Project &, const Options &, const Snapshot &);
struct GeneratedEvent
{
    double start = 0, length = .25, cents = 0;
    int pitch = 60;
    float velocity = .8f;
};
struct Request
{
    juce::String id = juce::Uuid().toString();
    Snapshot snapshot;
    juce::String role, style, variation = "new idea";
    std::uint64_t seed = 0;
    int candidateCount = 1;
    double variationAmount = .5; // Latent noise mix, not a note-identity guarantee.
};
struct Candidate
{
    juce::String requestId, fingerprint, provider, modelVersion;
    std::vector<GeneratedEvent> events;
    bool testFixture = false;
    std::uint64_t seed = 0;
    bool hasSeed = false;
};
juce::Result validate(const Request &, const Candidate &);
enum class ApplyOperation
{
    newPart,
    replaceRegion
};
// Replacement is explicit: Pattern source/all linked placements, or one unique Song clip.
juce::Result accept(Controller &, const Options &, const Request &, const Candidate &, int lane = 0,
                    ApplyOperation = ApplyOperation::newPart);
} // namespace vibera::coproducer
