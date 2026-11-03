// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CoProducer.h"
#include "PatternExtent.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace vibera::coproducer
{
namespace
{
bool contains(const std::vector<Id> &ids, Id id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
const MixerInsert *insert(const Project &p, int index)
{
    for (const auto &m : p.mixer)
        if (m.index == index)
            return &m;
    return nullptr;
}
bool audible(const Project &p, const Channel &c)
{
    const auto *master = insert(p, 0), *bus = insert(p, c.mixer);
    return !c.muted && c.gain > 0 && master && !master->muted && master->gain > 0 && bus && !bus->muted &&
           bus->gain > 0;
}
juce::String digest(const juce::MemoryOutputStream &stream)
{
    return juce::SHA256(stream.getData(), stream.getDataSize()).toHexString();
}
PluginRuntimeState runtimeState(const Options &options, Id id)
{
    for (const auto &state : options.pluginRuntime)
        if (state.plugin == id)
            return state;
    return {id, {}, 0, false};
}
juce::String destinationDigest(const Project &p, const Channel &c, const Options &options)
{
    juce::MemoryOutputStream stream;
    stream.writeInt64(c.id);
    stream.writeString(c.source); // Private input is hashed, never sent to providers.
    stream.writeInt(c.mixer);
    stream.writeFloat(c.gain);
    stream.writeFloat(c.pan);
    stream.writeBool(c.muted);
    stream.writeInt(c.sampler.root);
    stream.writeInt(c.sampler.cut);
    stream.writeInt(c.sampler.cutBy);
    stream.writeInt(c.sampler.maxPoly);
    stream.writeDouble(c.sampler.cents);
    stream.writeDouble(c.sampler.start);
    stream.writeDouble(c.sampler.end);
    stream.writeBool(c.sampler.reverse);
    stream.writeBool(c.sampler.normalize);
    stream.writeBool(c.sampler.loop);
    stream.writeBool(c.sampler.pingPong);
    stream.writeDouble(c.sampler.loopStart);
    stream.writeDouble(c.sampler.loopEnd);
    stream.writeBool(c.sampler.envelope);
    stream.writeDouble(c.sampler.attack);
    stream.writeDouble(c.sampler.decay);
    stream.writeDouble(c.sampler.sustain);
    stream.writeDouble(c.sampler.release);
    stream.writeDouble(c.sampler.sourceTempo);
    stream.writeBool(c.sampler.fitTempo);
    if (c.sampler.fadeIn != 0 || c.sampler.fadeOut != 0 || c.sampler.loopCrossfade != 0 || c.sampler.stretch ||
        c.sampler.hqInterpolation)
    { // Preserve earlier context fingerprints.
        stream.writeDouble(c.sampler.fadeIn);
        stream.writeDouble(c.sampler.fadeOut);
        stream.writeDouble(c.sampler.loopCrossfade);
        stream.writeBool(c.sampler.stretch);
        stream.writeBool(c.sampler.hqInterpolation);
    }
    auto runtime = [&](Id id)
    {
        const auto state = runtimeState(options, id);
        stream.writeString(state.instance);
        stream.writeInt64(state.parameterRevision);
        stream.writeBool(state.available);
    };
    if (c.instrument)
    {
        stream.writeInt64(c.instrument->id);
        stream.writeString(c.instrument->descriptor);
        stream.writeString(c.instrument->state);
        stream.writeBool(c.instrument->bypass);
        runtime(c.instrument->id);
    }
    for (const auto &m : p.mixer)
        if (m.index == 0 || m.index == c.mixer)
        {
            stream.writeInt(m.index);
            stream.writeFloat(m.gain);
            stream.writeFloat(m.pan);
            stream.writeBool(m.muted);
            for (const auto &effect : m.effects)
            {
                stream.writeInt64(effect.id);
                stream.writeString(effect.descriptor);
                stream.writeString(effect.state);
                stream.writeBool(effect.bypass);
                runtime(effect.id);
            }
        }
    return digest(stream);
}
juce::String fingerprint(const Snapshot &s)
{
    juce::MemoryOutputStream stream;
    stream.writeString(s.projectIdentity);
    stream.writeBool(s.selection.patternMode);
    stream.writeInt64(s.selection.pattern);
    stream.writeInt64(s.selection.destination);
    stream.writeDouble(s.selection.region.start);
    stream.writeDouble(s.selection.region.length);
    for (const auto &ids : {s.selection.notes, s.selection.placements})
    {
        stream.writeInt64(static_cast<juce::int64>(ids.size()));
        for (auto id : ids)
            stream.writeInt64(id);
    }
    stream.writeBool(s.contextAutomatic);
    stream.writeDouble(s.tempo);
    stream.writeInt(s.numerator);
    stream.writeInt(s.denominator);
    stream.writeInt64(static_cast<juce::int64>(s.tracks.size()));
    for (const auto &track : s.tracks)
    {
        stream.writeInt64(track.channel);
        stream.writeString(track.destinationDigest);
        stream.writeBool(track.included);
    }
    stream.writeInt64(static_cast<juce::int64>(s.events.size()));
    for (const auto &event : s.events)
    {
        stream.writeInt64(event.note);
        stream.writeInt64(event.channel);
        stream.writeInt64(event.pattern);
        stream.writeInt64(event.placement);
        stream.writeDouble(event.start);
        stream.writeDouble(event.length);
        stream.writeDouble(event.sourceBeat);
        stream.writeDouble(event.sourceLength);
        stream.writeInt(event.pitch);
        stream.writeFloat(event.velocity);
        stream.writeBool(event.audible);
        stream.writeBool(event.locked);
    }
    stream.writeInt64(static_cast<juce::int64>(s.placements.size()));
    for (const auto &placement : s.placements)
    {
        stream.writeInt64(placement.placement);
        stream.writeInt64(placement.pattern);
        stream.writeInt(placement.lane);
        stream.writeDouble(placement.start);
        stream.writeDouble(placement.length);
        stream.writeDouble(placement.offset);
        stream.writeDouble(placement.sourceLength);
    }
    stream.writeInt64(static_cast<juce::int64>(s.chords.size()));
    for (const auto &chord : s.chords)
    {
        stream.writeDouble(chord.start);
        stream.writeDouble(chord.length);
        stream.writeString(chord.label);
        stream.writeInt(int(chord.provenance));
        stream.writeInt(int(chord.pitchClasses.size()));
        for (auto pitch : chord.pitchClasses)
            stream.writeInt(pitch);
    }
    stream.writeString(s.destinationProfile.family);
    stream.writeInt(s.destinationProfile.low);
    stream.writeInt(s.destinationProfile.high);
    stream.writeInt(s.destinationProfile.polyphony);
    stream.writeBool(s.destinationProfile.fractionalPitch);
    stream.writeInt(int(s.destinationProfile.provenance));
    stream.writeInt(int(s.modelMappings.size()));
    for (const auto &mapping : s.modelMappings)
    {
        stream.writeInt64(mapping.channel);
        stream.writeInt(mapping.program);
        stream.writeInt(int(mapping.provenance));
        stream.writeInt(int(mapping.pitches.size()));
        for (auto pitch : mapping.pitches)
            stream.writeInt(pitch);
    }
    stream.writeBool(s.complete);
    return digest(stream);
}
} // namespace

bool channelAudible(const Project &p, Id id)
{
    const auto *channel = p.channel(id);
    return channel && audible(p, *channel);
}
juce::Result collect(const Project &p, const Options &options, Snapshot &output)
{
    const auto &selection = options.selection;
    const auto region = selection.region;
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    if (options.projectIdentity.isEmpty() || !p.channel(selection.destination) ||
        (selection.patternMode && !p.pattern(selection.pattern)))
        return fail("CoProducer requires project identity, destination and a valid selected Pattern.");
    if (!std::isfinite(region.start) || !std::isfinite(region.length) || region.start < 0 ||
        region.length <= 0 || region.length > 4096 || !std::isfinite(options.preceding) ||
        !std::isfinite(options.following) || options.preceding < 0 || options.following < 0 ||
        options.preceding > 4096 || options.following > 4096 || region.start + region.length > 1000000 ||
        options.maxEvents == 0 || options.maxEvents > 65536 || options.maxExaminedNotes == 0 ||
        options.maxExaminedNotes > 1000000 || options.chords.size() > 4096 || options.tracks.size() > 128 ||
        selection.notes.size() > 65536 || options.lockedNotes.size() > 65536 ||
        selection.placements.size() > 100000)
        return fail("Invalid or excessive CoProducer context range/budget.");
    for (auto id : options.tracks)
        if (!p.channel(id))
            return fail("A context Channel no longer exists.");
    if (!std::isfinite(p.tempo) || p.tempo < 20 || p.tempo > 400 || p.numerator < 1 || p.numerator > 32 ||
        (p.denominator != 2 && p.denominator != 4 && p.denominator != 8 && p.denominator != 16))
        return fail("Invalid project tempo/meter.");
    std::set<Id> wantedNotes(selection.notes.begin(), selection.notes.end());
    wantedNotes.insert(options.lockedNotes.begin(), options.lockedNotes.end());
    size_t verifiedNotes = 0;
    if (!wantedNotes.empty())
        for (const auto &pattern : p.patterns)
        {
            for (const auto &note : pattern.notes)
            {
                if (wantedNotes.empty())
                    break;
                if (verifiedNotes++ == options.maxExaminedNotes)
                    return fail(
                        "Selected/locked note verification exceeded the scan budget; narrow context.");
                wantedNotes.erase(note.id);
            }
            if (wantedNotes.empty())
                break;
        }
    if (!wantedNotes.empty())
        return fail("A selected or locked note no longer exists.");
    for (auto id : selection.placements)
        if (std::none_of(p.placements.begin(), p.placements.end(),
                         [id](const auto &placement) { return placement.id == id; }))
            return fail("A selected placement no longer exists.");
    const auto profile = options.destinationProfile;
    if (profile.low < 0 || profile.high > 127 || profile.low > profile.high || profile.polyphony < 0 ||
        profile.polyphony > 128 || profile.family.length() > 128)
        return fail("Invalid destination capability profile.");
    if (options.modelMappings.size() > 256)
        return fail("Excessive model instrument mappings.");
    auto mappings = options.modelMappings;
    std::sort(mappings.begin(), mappings.end(),
              [](const auto &a, const auto &b) { return a.channel < b.channel; });
    Id previousChannel = 0;
    for (auto &mapping : mappings)
    {
        if (!p.channel(mapping.channel) || mapping.channel == previousChannel || mapping.program < 0 ||
            mapping.program > 128 || mapping.provenance != Provenance::userSpecified ||
            mapping.pitches.empty() || mapping.pitches.size() > 128)
            return fail("Invalid or nonexplicit model instrument mapping.");
        std::sort(mapping.pitches.begin(), mapping.pitches.end());
        if (mapping.pitches.front() < 0 || mapping.pitches.back() > 127 ||
            std::adjacent_find(mapping.pitches.begin(), mapping.pitches.end()) != mapping.pitches.end())
            return fail("Invalid or duplicate mapped instrument pitches.");
        previousChannel = mapping.channel;
    }
    for (const auto &chord : options.chords)
        if (!std::isfinite(chord.start) || !std::isfinite(chord.length) || chord.start < 0 ||
            chord.length <= 0 || chord.label.length() > 256 || chord.pitchClasses.size() > 12 ||
            std::any_of(chord.pitchClasses.begin(), chord.pitchClasses.end(),
                        [](int pitch) { return pitch < 0 || pitch > 11; }))
            return fail("Invalid chord annotation.");
    Snapshot s;
    s.projectIdentity = options.projectIdentity;
    s.revision = options.revision;
    s.selection = selection;
    s.contextAutomatic = options.automaticTracks;
    for (auto *ids : {&s.selection.notes, &s.selection.placements})
    {
        std::sort(ids->begin(), ids->end());
        ids->erase(std::unique(ids->begin(), ids->end()), ids->end());
    }
    s.tempo = p.tempo;
    const auto meter = playbackMeter(p, selection.patternMode, selection.pattern);
    s.numerator = meter.first;
    s.denominator = meter.second;
    s.destinationProfile = profile;
    s.modelMappings = std::move(mappings);
    const double first = std::max(0.0, region.start - options.preceding);
    const double last = region.start + region.length + options.following;
    std::set<Id> included;
    for (const auto &channel : p.channels)
    {
        const bool heard = audible(p, channel);
        const bool selected = options.automaticTracks ? heard : contains(options.tracks, channel.id);
        if (selected || channel.id == selection.destination)
        {
            s.tracks.push_back({channel.id, channel.mixer, channel.name,
                                channel.instrument ? channel.instrument->name : sourceName(channel.source),
                                heard, channel.gain, channel.pan, destinationDigest(p, channel, options),
                                selected});
            auto includeRuntime = [&](Id id)
            {
                if (std::none_of(s.pluginRuntime.begin(), s.pluginRuntime.end(),
                                 [&](const auto &item) { return item.plugin == id; }))
                    s.pluginRuntime.push_back(runtimeState(options, id));
            };
            if (channel.instrument)
                includeRuntime(channel.instrument->id);
            for (const auto &insert : p.mixer)
                if (insert.index == 0 || insert.index == channel.mixer)
                    for (const auto &effect : insert.effects)
                        includeRuntime(effect.id);
            if (selected)
                included.insert(channel.id);
        }
    }
    bool scanExceeded = false;
    auto emit = [&](const Pattern &pattern, double start, double length, double offset, Id placement)
    {
        for (const auto &note : pattern.notes)
        {
            if (s.examinedNotes == options.maxExaminedNotes)
            {
                scanExceeded = true;
                return;
            }
            ++s.examinedNotes;
            const double local = note.beat - offset;
            // Match the actual sequence(): source offsets omit earlier note-ons, not chase them.
            if (!included.count(note.channel) || local < 0 || local >= length ||
                local >= pattern.length - offset || note.velocity <= 0)
                continue;
            const double absolute = start + local;
            const double duration = std::min(note.length, length - local);
            if (absolute >= last || absolute + duration <= first)
                continue;
            if (s.events.size() == options.maxEvents)
            {
                ++s.omittedEvents;
                continue;
            }
            s.events.push_back({note.id, note.channel, pattern.id, placement, absolute - region.start,
                                duration, note.beat, note.length, note.pitch, note.velocity,
                                audible(p, *p.channel(note.channel)),
                                absolute < region.start + region.length && absolute + duration > region.start,
                                contains(options.lockedNotes, note.id)});
        }
    };
    if (selection.patternMode)
    {
        const auto &pattern = *p.pattern(selection.pattern);
        s.placements.push_back({0, pattern.id, 0, 0, pattern.length, 0, pattern.length});
        emit(pattern, 0, pattern.length, 0, 0);
    }
    else
        for (const auto &placement : p.placements)
        {
            if (scanExceeded)
                break;
            if (placement.beat >= last || placement.beat + placement.length <= first)
                continue;
            if (const auto *pattern = p.pattern(placement.pattern))
            {
                s.placements.push_back({placement.id, pattern->id, placement.lane, placement.beat,
                                        placement.length, placement.offset, pattern->length});
                emit(*pattern, placement.beat, placement.length, placement.offset, placement.id);
            }
        }
    for (const auto &chord : options.chords)
        if (chord.start < last && chord.start + chord.length > first)
            s.chords.push_back(chord);
    std::sort(s.chords.begin(), s.chords.end(), [](const auto &a, const auto &b)
              { return std::tie(a.start, a.label) < std::tie(b.start, b.label); });
    std::sort(s.tracks.begin(), s.tracks.end(),
              [](const auto &a, const auto &b) { return a.channel < b.channel; });
    std::sort(s.placements.begin(), s.placements.end(), [](const auto &a, const auto &b)
              { return std::tie(a.start, a.placement) < std::tie(b.start, b.placement); });
    std::sort(s.events.begin(), s.events.end(),
              [](const auto &a, const auto &b)
              {
                  return std::tie(a.start, a.channel, a.placement, a.note) <
                         std::tie(b.start, b.channel, b.placement, b.note);
              });
    s.complete = !scanExceeded && s.omittedEvents == 0;
    for (auto id : options.lockedNotes)
        if (std::none_of(s.events.begin(), s.events.end(),
                         [id](const auto &event) { return event.note == id; }))
        {
            s.complete = false;
            s.limitations.add(
                "A locked note is outside included context; include its Channel/region before generation.");
        }
    if (scanExceeded)
        s.limitations.add("Context scan budget exceeded; additional relevant event count is unknown.");
    if (s.omittedEvents != 0)
        s.limitations.add(juce::String(int(s.omittedEvents)) +
                          " relevant events omitted by the event budget.");
    s.limitations.add("Project currently has one global tempo/meter; local maps, solo, clip enablement, "
                      "controllers, pedal, tuning and markers are not represented.");
    s.limitations.add("Audio samples expose timing/instrument metadata only; audio harmony and keyswitch "
                      "mappings are unknown.");
    s.limitations.add("Audibility reflects Channel/insert/Master mute and zero gain; plugin silence and "
                      "routing effects are not inferred.");
    if (s.chords.empty())
        s.limitations.add(
            "Harmony is unknown. Supply chord annotations; no harmony was inferred from names.");
    if (!s.pluginRuntime.empty())
        s.limitations.add("Live plugin freshness uses worker-reported parameter revisions; unreported "
                          "preset/internal changes remain unknown.");
    s.fingerprint = fingerprint(s);
    output = std::move(s);
    return juce::Result::ok();
}

juce::Result checkFresh(const Project &p, const Options &options, const Snapshot &previous)
{
    Snapshot current;
    const auto result = collect(p, options, current);
    if (result.failed())
        return result;
    if (!previous.complete || !current.complete)
        return juce::Result::fail("Incomplete context must be narrowed before generation or acceptance.");
    if (current.fingerprint != previous.fingerprint)
        return juce::Result::fail(
            "Relevant musical context or destination changed; regenerate or explicitly revalidate.");
    return juce::Result::ok();
}

juce::String Snapshot::summary() const
{
    int targetEvents = 0;
    for (const auto &event : events)
        targetEvents += event.inTarget ? 1 : 0;
    juce::String text = "CoProducer context\n";
    text += "Time unit: quarter notes. Region " + juce::String(selection.region.start, 3) + " to " +
            juce::String(selection.region.start + selection.region.length, 3) + "\n";
    text += juce::String(tempo, 2) + " BPM, " + juce::String(numerator) + "/" + juce::String(denominator) +
            " meter, " + juce::String(quarterNotesPerBar(), 3) + " quarter notes per bar\n";
    text += juce::String(int(std::count_if(tracks.begin(), tracks.end(),
                                           [](const auto &track) { return track.included; }))) +
            " context Channels, " + juce::String(targetEvents) + " target notes, " +
            juce::String(int(events.size()) - targetEvents) + " surrounding notes\n";
    text += "Instrument profile: " + destinationProfile.family +
            (destinationProfile.provenance == Provenance::unknown ? " (unknown capabilities)\n" : "\n");
    text += "Harmony: " +
            (chords.empty() ? juce::String("unknown")
                            : juce::String(int(chords.size())) + " supplied annotations") +
            "\n";
    for (const auto &track : tracks)
        text += track.name + " / " + track.instrument +
                (track.audible ? " [audible]" : " [muted or zero gain]") +
                (track.included ? " [context]\n" : " [destination only; notes excluded]\n");
    text += complete ? "Context budget: complete\n" : "Context budget: INCOMPLETE\n";
    text += "\nLimitations:\n" + limitations.joinIntoString("\n");
    return text;
}
juce::Result validate(const Request &request, const Candidate &candidate)
{
    if (!request.snapshot.complete || request.id.isEmpty() || request.candidateCount < 1 ||
        request.candidateCount > 3 || !std::isfinite(request.variationAmount) ||
        request.variationAmount < 0 || request.variationAmount > 1 || candidate.requestId != request.id ||
        candidate.fingerprint != request.snapshot.fingerprint || candidate.provider.isEmpty() ||
        candidate.modelVersion.isEmpty() || candidate.events.empty() || candidate.events.size() > 65536)
        return juce::Result::fail("Invalid CoProducer request, provenance or event count.");
    const auto &profile = request.snapshot.destinationProfile;
    const auto mapping = std::find_if(request.snapshot.modelMappings.begin(),
                                      request.snapshot.modelMappings.end(), [&](const auto &item)
                                      { return item.channel == request.snapshot.selection.destination; });
    std::vector<std::pair<double, int>> boundaries;
    for (const auto &event : candidate.events)
    {
        if (!std::isfinite(event.start) || !std::isfinite(event.length) || !std::isfinite(event.cents) ||
            !std::isfinite(event.velocity) || event.start < 0 || event.length <= 0 ||
            event.start + event.length > request.snapshot.selection.region.length ||
            event.pitch < profile.low || event.pitch > profile.high || event.velocity <= 0 ||
            event.velocity > 1)
            return juce::Result::fail("Generated event violates target boundaries or destination range.");
        if (mapping != request.snapshot.modelMappings.end() &&
            std::find(mapping->pitches.begin(), mapping->pitches.end(), event.pitch) ==
                mapping->pitches.end())
            return juce::Result::fail("Generated event is outside the explicit destination pitch/drum map.");
        if (event.cents != 0)
            return juce::Result::fail(
                "Fractional pitch output is not mapped by this destination adapter yet.");
        boundaries.emplace_back(event.start, 1);
        boundaries.emplace_back(event.start + event.length, -1);
    }
    if (profile.polyphony > 0)
    {
        std::sort(boundaries.begin(), boundaries.end()); // End events precede starts at equal time.
        int voices = 0;
        for (const auto &[time, change] : boundaries)
        {
            juce::ignoreUnused(time);
            voices += change;
            if (voices > profile.polyphony)
                return juce::Result::fail("Generated part exceeds the declared polyphony.");
        }
    }
    return juce::Result::ok();
}

juce::Result accept(Controller &controller, const Options &options, const Request &request,
                    const Candidate &candidate, int lane, ApplyOperation operation)
{
    auto result = validate(request, candidate);
    if (result.failed())
        return result;
    result = checkFresh(controller.project, options, request.snapshot);
    if (result.failed())
        return result;
    if (controller.projectIdentity != request.snapshot.projectIdentity || controller.hasGesture())
        return juce::Result::fail(
            "Project changed or an edit gesture is active; revalidate before accepting.");
    CommandArgs args;
    args.channel = request.snapshot.selection.destination;
    args.beat = request.snapshot.selection.region.start;
    args.length = request.snapshot.selection.region.length;
    args.enabled = !request.snapshot.selection.patternMode;
    args.lane = lane;
    args.text = candidate.testFixture ? "CoProducer test fixture" : "CoProducer " + request.role;
    if (operation == ApplyOperation::replaceRegion)
    {
        args.pattern = request.snapshot.selection.pattern;
        args.ids = options.lockedNotes;
        if (args.enabled)
        {
            if (request.snapshot.selection.placements.size() != 1)
                return juce::Result::fail(
                    "Select exactly one Song clip to replace its destination region as a unique copy.");
            args.target = request.snapshot.selection.placements.front();
            const Placement *placement = nullptr;
            for (const auto &item : controller.project.placements)
                if (item.id == args.target)
                    placement = &item;
            if (!placement || args.beat < placement->beat ||
                args.beat + args.length > placement->beat + placement->length)
                return juce::Result::fail("Replacement region must fit inside the selected Song clip.");
            args.pattern = placement->pattern;
            args.beat = placement->offset + args.beat - placement->beat;
        }
    }
    juce::var provenance(new juce::DynamicObject());
    auto &settings = *provenance.getDynamicObject();
    settings.setProperty("schema", 1);
    settings.setProperty("requestId", request.id);
    settings.setProperty("operation", operation == ApplyOperation::newPart ? "new part"
                                      : args.enabled
                                          ? "replace unique Song clip destination region"
                                          : "replace Pattern destination region (all linked placements)");
    settings.setProperty("sourceRegionStart", operation == ApplyOperation::replaceRegion ? args.beat : 0.0);
    settings.setProperty("replacementPlacement", juce::String(args.target));
    settings.setProperty("contextFingerprint", request.snapshot.fingerprint);
    settings.setProperty("provider", candidate.provider);
    settings.setProperty("modelVersion", candidate.modelVersion);
    settings.setProperty("fixture", candidate.testFixture);
    settings.setProperty("seed", juce::int64(candidate.hasSeed ? candidate.seed : request.seed));
    settings.setProperty("role", request.role);
    settings.setProperty("style", request.style);
    settings.setProperty("variation", request.variation);
    settings.setProperty("family", request.snapshot.destinationProfile.family);
    settings.setProperty("timeBasis", "quarter-notes");
    settings.setProperty("targetStart", request.snapshot.selection.region.start);
    settings.setProperty("targetLength", request.snapshot.selection.region.length);
    settings.setProperty("tempo", request.snapshot.tempo);
    settings.setProperty("numerator", request.snapshot.numerator);
    settings.setProperty("denominator", request.snapshot.denominator);
    settings.setProperty("patternMode", request.snapshot.selection.patternMode);
    settings.setProperty("destination", juce::String(request.snapshot.selection.destination));
    settings.setProperty("contextPolicy",
                         request.snapshot.contextAutomatic ? "automatic audibility" : "explicit Channels");
    juce::Array<juce::var> contextIds;
    for (const auto &track : request.snapshot.tracks)
        if (track.included)
            contextIds.add(juce::String(track.channel));
    settings.setProperty("contextChannels", contextIds);
    juce::Array<juce::var> modelMappings;
    for (const auto &mapping : request.snapshot.modelMappings)
    {
        juce::var item(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("channel", juce::String(mapping.channel));
        item.getDynamicObject()->setProperty("program", mapping.program);
        item.getDynamicObject()->setProperty("source", "user-specified");
        juce::Array<juce::var> pitches;
        for (auto pitch : mapping.pitches)
            pitches.add(pitch);
        item.getDynamicObject()->setProperty("pitches", pitches);
        modelMappings.add(item);
    }
    settings.setProperty("modelInstrumentMappings", modelMappings);
    juce::Array<juce::var> runtimeSettings;
    for (const auto &state : request.snapshot.pluginRuntime)
    {
        juce::var item(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("plugin", juce::String(state.plugin));
        item.getDynamicObject()->setProperty("reportedParameterRevision",
                                             juce::int64(state.parameterRevision));
        item.getDynamicObject()->setProperty("available", state.available);
        runtimeSettings.add(item);
    }
    settings.setProperty("livePluginValidation", runtimeSettings);
    settings.setProperty("livePluginValidationScope", "Historical worker-reported revisions, not restorable "
                                                      "preset state; unreported changes remain unknown.");
    juce::Array<juce::var> chordSettings;
    for (const auto &chord : request.snapshot.chords)
    {
        juce::var item(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("start", chord.start);
        item.getDynamicObject()->setProperty("length", chord.length);
        item.getDynamicObject()->setProperty("label", chord.label);
        item.getDynamicObject()->setProperty("provenance", int(chord.provenance));
        juce::Array<juce::var> pitches;
        for (int pitch : chord.pitchClasses)
            pitches.add(pitch);
        item.getDynamicObject()->setProperty("pitchClasses", pitches);
        chordSettings.add(item);
    }
    settings.setProperty("chords", chordSettings);
    settings.setProperty("scope",
                         operation == ApplyOperation::replaceRegion
                             ? "historical generated destination region only; retained source notes and "
                               "later edits are not model output"
                             : "historical generation settings; later note edits are not model output");
    settings.setProperty("variationMix",
                         request.variation == "idea variation" ? request.variationAmount : 0.0);
    settings.setProperty("reproduction", "Requires the original context and pinned backend; later edits or "
                                         "another backend can change output.");
    args.generation = juce::JSON::toString(provenance);
    for (const auto &event : candidate.events)
        args.recordedNotes.push_back(
            {0, args.channel, event.start, event.length, event.pitch, event.velocity, false});
    return controller.execute(
        operation == ApplyOperation::newPart ? "coproducer.accept" : "coproducer.replace", args);
}
} // namespace vibera::coproducer
