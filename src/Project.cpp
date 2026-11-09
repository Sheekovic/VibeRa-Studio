// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthRegistry.h"
#include "NativeInstruments.h"
#include "Project.h"
#include "ScoreTools.h"
#include "Performance.h"
#include "Arrangements.h"
#include "NoteTiming.h"
#include "NoteRepeat.h"
#include "PatternChannelLoops.h"
#include "Swing.h"
#include "ClipGroups.h"
#include "MixerRouting.h"
#include "MixerControls.h"
#include "MixerGroups.h"
#include "AutomationState.h"
#include "SamplerAutomationParameters.h"
#include "PluginStateEncoding.h"
#include "effects/NativeEffectsRegistry.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <tuple>
#include <stdexcept>
#include <charconv>
#include <limits>
#include "NumberText.h"

namespace vibera
{
template <class T> static T *byId(std::vector<T> &xs, Id id)
{
    auto it = std::find_if(xs.begin(), xs.end(), [id](const auto &x) { return x.id == id; });
    return it == xs.end() ? nullptr : &*it;
}
Pattern *Project::pattern(Id id)
{
    return byId(patterns, id);
}
const Pattern *Project::pattern(Id id) const
{
    return byId(const_cast<Project *>(this)->patterns, id);
}
Channel *Project::channel(Id id)
{
    return byId(channels, id);
}
const Channel *Project::channel(Id id) const
{
    return byId(const_cast<Project *>(this)->channels, id);
}
ChannelGroup *Project::channelGroup(Id id)
{
    return byId(channelGroups, id);
}
const ChannelGroup *Project::channelGroup(Id id) const
{
    return byId(const_cast<Project *>(this)->channelGroups, id);
}
PluginSpec *Project::plugin(Id id)
{
    for (auto &channel : channels)
        if (channel.instrument && channel.instrument->id == id)
            return &*channel.instrument;
    for (auto &insert : mixer)
        for (auto &effect : insert.effects)
            if (effect.id == id)
                return &effect;
    return nullptr;
}
const PluginSpec *Project::plugin(Id id) const
{
    return const_cast<Project *>(this)->plugin(id);
}
AutomationSource *Project::automationSource(Id id)
{
    return byId(automation.sources, id);
}
const AutomationSource *Project::automationSource(Id id) const
{
    return byId(const_cast<AutomationDocument &>(automation).sources, id);
}
std::vector<AutomationInitialValue> Project::automationDefaults() const
{
    std::vector<AutomationInitialValue> values;
    for (const auto &insert : mixer)
    {
        values.push_back({{AutomationParameter::mixerGain, insert.index, 0}, insert.gain});
        values.push_back({{AutomationParameter::mixerPan, insert.index, 0}, insert.pan});
        values.push_back({{AutomationParameter::mixerStereoSeparation, insert.index, 0}, insert.stereoSeparation});
        for (const auto &route : insert.routes)
            values.push_back(
                {{AutomationParameter::mixerSend, insert.index, std::uint32_t(route.target)}, route.gain});
    }
    const auto add = [&](const PluginSpec &plugin)
    {
        for (const auto &binding : plugin.automationBindings)
            values.push_back(
                {{AutomationParameter::pluginNormalized, plugin.id, binding.id}, binding.manual});
    };
    for (const auto &channel : channels)
        if (channel.instrument)
            add(*channel.instrument);
    for (const auto &insert : mixer)
        for (const auto &effect : insert.effects)
            add(effect);
    // Only referenced sampler keys need defaults. Avoid88 rows per unused Channel.
    // Keep recoverable curves/defaults when the Channel temporarily has another role;
    // native/AudioClip eligibility belongs to the prepared sampler consumer.
    std::set<AutomationKey> samplerKeys;
    for (const auto &source : automation.sources)
        for (const auto &link : source.links)
            if (link.key.kind == AutomationParameter::samplerModulation)
                samplerKeys.insert(link.key);
    for (const auto &value : automation.initialValues)
        if (value.key.kind == AutomationParameter::samplerModulation)
            samplerKeys.insert(value.key);
    for (const auto &modulator : automation.modulators)
    {
        const auto key = modulator.link.key;
        if (key.kind == AutomationParameter::samplerModulation)
            samplerKeys.insert(key);
        else if (key.kind == AutomationParameter::channelGain || key.kind == AutomationParameter::channelPan)
            if (const auto *owner = channel(key.owner))
                values.push_back({key, key.kind == AutomationParameter::channelGain ? owner->gain : owner->pan});
    }
    for (const auto &key : samplerKeys)
        if (const auto *owner = channel(key.owner))
            if (const auto parameter = lookupSamplerAutomationParameter(key.parameter))
                values.push_back({key, float(samplerModulationValue(
                                           owner->sampler.modulation[size_t(parameter->registryTarget)],
                                           *parameter->registryParameter))});
    return values;
}
TempoMap Project::songTempo() const
{
    std::vector<TempoPoint> points;
    points.reserve(tempoChanges.size());
    for (const auto &change : tempoChanges)
        points.push_back({change.beat, change.bpm, change.ramp});
    return TempoMap(tempo, points);
}
double Project::songSeconds(double beat) const
{
    return tempoChanges.empty() ? beat * 60. / tempo : songTempo().secondsAt(beat);
}
double Project::songBeat(double seconds) const
{
    return tempoChanges.empty() ? seconds * tempo / 60. : songTempo().beatAt(seconds);
}
double Project::audioEndBeat(double beat, double seconds) const
{
    if (tempoChanges.empty())
        return beat + seconds * tempo / 60.;
    const auto map = songTempo();
    return map.beatAt(map.secondsAt(beat) + seconds);
}
double Project::endBeat() const
{
    double end = 0;
    for (auto &p : placements)
        end = std::max(end, p.beat + p.length);
    const auto map = songTempo();
    for (const auto &p : audioPlacements)
        end = std::max(end, map.beatAt(map.secondsAt(p.beat) + p.duration));
    for (const auto &p : automation.placements)
        end = std::max(end, p.beat + p.length);
    return end;
}
juce::String sourceName(const juce::String &source)
{
    return source.startsWith("builtin:") ? source.fromFirstOccurrenceOf(":", false, false)
                                         : juce::File(source).getFileNameWithoutExtension();
}
Project Project::empty()
{
    Project p;
    for (int i = 0; i < 18; ++i)
        p.mixer.push_back(
            {i, i == 0 ? "Master" : "Insert " + juce::String(i), i == 0 ? .8f : 1.f, 0.f, false});
    for (auto name : {"kick", "snare", "hat", "bass"})
    {
        auto id = p.allocate();
        p.channels.push_back(
            {id, juce::String(name).toUpperCase(), "builtin:" + juce::String(name), 0, .7f, 0, false});
    }
    auto id = p.allocate();
    p.patterns.push_back({id, "Pattern 1", 4, {}});
    return p;
}
Project Project::demo()
{
    auto p = empty();
    p.name = "First Light";
    auto &pat = p.patterns.front();
    pat.length = 8;
    for (int s = 0; s < 32; ++s)
    {
        if (s % 4 == 0)
            pat.notes.push_back({p.allocate(), p.channels[0].id, s * .25, .25, 60, .85f, true});
        if (s % 8 == 4)
            pat.notes.push_back({p.allocate(), p.channels[1].id, s * .25, .25, 60, .75f, true});
        if (s % 2 == 0)
            pat.notes.push_back({p.allocate(), p.channels[2].id, s * .25, .25, 60, .4f, true});
    }
    for (int i = 0; i < 8; ++i)
        pat.notes.push_back(
            {p.allocate(), p.channels[3].id, double(i), .75, i % 4 < 2 ? 48 : 55, .65f, false});
    for (int i = 0; i < 4; ++i)
        p.channels[size_t(i)].mixer = i + 1;
    for (int i = 0; i < 4; ++i)
        p.placements.push_back({p.allocate(), pat.id, 0, i * 8., 8, 0});
    return p;
}
PluginMainIo pluginMainIoFor(const PluginSpec &plugin) noexcept
{
    return {plugin.mainInputBus, plugin.mainOutputBus, plugin.mainInputChannels, plugin.mainInputMonoSource};
}
juce::Result setPluginMainIo(Project &project, Id target, const PluginMainIo &io)
{
    auto *plugin = project.plugin(target);
    if (!plugin)
        return juce::Result::fail("pluginIo.unavailableTarget: Existing instrument or effect ID required");
    if (!io.valid() || (plugin->detector && plugin->detector->inputBus == io.inputBus))
        return juce::Result::fail("pluginIo.invalidSelection: Valid main buses and explicit mono mapping, "
                                  "distinct from aux, required");
    plugin->mainInputBus = io.inputBus;
    plugin->mainOutputBus = io.outputBus;
    plugin->mainInputChannels = io.inputChannels;
    plugin->mainInputMonoSource = io.monoSource;
    return juce::Result::ok();
}
bool purePluginIoHistory(const Project &before, const Project &after)
{
    if (before.validate().failed() || after.validate().failed())
        return false;
    auto normalized = after;
    bool changed = false;
    const auto normalize = [&](const PluginSpec &old)
    {
        auto *next = normalized.plugin(old.id);
        if (!next || next->descriptor != old.descriptor)
            return false;
        changed |= pluginMainIoFor(*next) != pluginMainIoFor(old);
        if (setPluginMainIo(normalized, old.id, pluginMainIoFor(old)).failed())
            return false;
        next->state = old.state;
        return true;
    };
    for (const auto &channel : before.channels)
        if (channel.instrument && !normalize(*channel.instrument))
            return false;
    for (const auto &row : before.mixer)
        for (const auto &plugin : row.effects)
            if (!normalize(plugin))
                return false;
    return changed && normalized.state().isEquivalentTo(before.state());
}
static double exactNumber(const juce::var &);
static juce::ValueTree pluginState(const PluginSpec &p)
{
    juce::ValueTree t("PLUGIN");
    t.setProperty("id", juce::int64(p.id), nullptr);
    t.setProperty("name", p.name, nullptr);
    t.setProperty("descriptor", p.descriptor, nullptr);
    t.setProperty("state", p.state, nullptr);
    t.setProperty("instrument", p.instrument, nullptr);
    t.setProperty("bypass", p.bypass, nullptr);
    if (p.wet != 1)
        t.setProperty("wet", double(p.wet), nullptr);
    t.appendChild(pluginBindingsState(p.automationBindings), nullptr);
    if (!p.extraOutputMixers.empty())
    {
        juce::StringArray targets;
        for (int target : p.extraOutputMixers)
            targets.add(juce::String(target));
        t.setProperty("extraOutputMixers", targets.joinIntoString(","), nullptr);
    }
    if (pluginMainIoFor(p) != PluginMainIo{})
    {
        t.setProperty("mainInputBus", p.mainInputBus, nullptr);
        t.setProperty("mainOutputBus", p.mainOutputBus, nullptr);
        t.setProperty("mainInputChannels", p.mainInputChannels, nullptr);
        t.setProperty("mainInputMonoSource", p.mainInputMonoSource, nullptr);
    }
    if (p.detector)
    {
        juce::ValueTree detector("DETECTOR");
        detector.setProperty("sourceMixer", p.detector->sourceMixer, nullptr);
        detector.setProperty("inputBus", p.detector->inputBus, nullptr);
        detector.setProperty("channels", p.detector->channels, nullptr);
        t.appendChild(detector, nullptr);
    }
    return t;
}
static juce::Result parsePlugin(const juce::ValueTree &t, PluginSpec &output, int version)
{
    PluginSpec plugin{juce::int64(t["id"]),  t["name"].toString(),  t["descriptor"].toString(),
                      t["state"].toString(), bool(t["instrument"]), bool(t["bypass"])};
    if (t.hasProperty("wet"))
    {
        const auto wet = exactNumber(t["wet"]);
        if (version < 20 || !std::isfinite(wet) || wet < 0 || wet > 1 || (plugin.instrument && wet != 1))
            return juce::Result::fail("Invalid host effect mix or newer mix data under an old schema");
        plugin.wet = float(wet);
    }
    const auto bindings = t.getChildWithName("AUTOMATION_BINDINGS");
    int bindingTrees = 0, detectorTrees = 0;
    for (const auto &child : t)
    {
        if (child.hasType("AUTOMATION_BINDINGS"))
            ++bindingTrees;
        else if (child.hasType("DETECTOR"))
            ++detectorTrees;
        else
            return juce::Result::fail("Unsupported plugin child data");
    }
    if (bindingTrees > 1 || detectorTrees > 1 || t.hasProperty("detector") ||
        (version < 24 && detectorTrees) || (version >= 9 && !bindings.isValid()))
        return juce::Result::fail("Missing, duplicated or unsupported plugin binding data");
    if (bindings.isValid())
    {
        if (auto result = parsePluginBindings(bindings, plugin.automationBindings); result.failed())
            return result;
        if (version < 9 && !plugin.automationBindings.empty())
            return juce::Result::fail("Plugin automation bindings require project version 9");
    }
    if (detectorTrees)
    {
        const auto detector = t.getChildWithName("DETECTOR");
        const double source = exactNumber(detector["sourceMixer"]);
        const double bus = exactNumber(detector["inputBus"]);
        const double channels = exactNumber(detector["channels"]);
        if (plugin.instrument || detector.getNumChildren() || detector.getNumProperties() != 3 ||
            !detector.hasProperty("sourceMixer") || !detector.hasProperty("inputBus") ||
            !detector.hasProperty("channels") || !std::isfinite(source) || source < 1 ||
            source > std::numeric_limits<int>::max() || std::floor(source) != source || !std::isfinite(bus) ||
            bus < 1 || bus > 31 || std::floor(bus) != bus || !std::isfinite(channels) ||
            (channels != 1 && channels != 2))
            return juce::Result::fail("sidechain.invalidEncoding: Exact effect detector assignment required");
        plugin.detector = PluginDetectorAssignment{int(source), int(bus), int(channels)};
    }
    if (t.hasProperty("extraOutputMixers"))
    {
        const auto tokens = juce::StringArray::fromTokens(t["extraOutputMixers"].toString(), ",", "");
        if (tokens.isEmpty() || tokens.size() > PluginBusRequest::maximumExtraOutputBuses)
            return juce::Result::fail("pluginIo.invalidEncoding: 1..7 extra output Mixer targets required");
        for (const auto &token : tokens)
        {
            if (!token.containsOnly("0123456789") || token.isEmpty() || token.length() > 9)
                return juce::Result::fail("pluginIo.invalidEncoding: Integral extra output Mixer targets required");
            plugin.extraOutputMixers.push_back(token.getIntValue());
        }
    }
    int ioFields = 0;
    for (const auto *key : {"mainInputBus", "mainOutputBus", "mainInputChannels", "mainInputMonoSource"})
        ioFields += int(t.hasProperty(key));
    if (ioFields)
    {
        if (version < 26 || ioFields != 4)
            return juce::Result::fail("pluginIo.invalidEncoding: Complete main I/O requires schema26");
        int values[4]{};
        int index = 0;
        for (const auto *key : {"mainInputBus", "mainOutputBus", "mainInputChannels", "mainInputMonoSource"})
        {
            const auto value = exactNumber(t[key]);
            if (!std::isfinite(value) || value < 0 || value > 31 || value != std::floor(value))
                return juce::Result::fail("pluginIo.invalidEncoding: Integral bounded I/O fields required");
            values[index++] = int(value);
        }
        plugin.mainInputBus = values[0];
        plugin.mainOutputBus = values[1];
        plugin.mainInputChannels = values[2];
        plugin.mainInputMonoSource = values[3];
        if (!pluginBusRequestFor(plugin).valid())
            return juce::Result::fail("pluginIo.invalidEncoding: Invalid main or overlapping aux selection");
    }
    output = std::move(plugin);
    return juce::Result::ok();
}
juce::ValueTree Project::state() const
{
    juce::ValueTree root("VIBERA");
    auto set = [](juce::ValueTree &t, const char *k, auto v) { t.setProperty(k, v, nullptr); };
    set(root, "version", version);
    set(root, "nextId", juce::int64(nextId));
    set(root, "name", name);
    set(root, "tempo", tempo);
    set(root, "swing", swing);
    set(root, "numerator", numerator);
    set(root, "denominator", denominator);
    juce::ValueTree soloTree("CHANNEL_SOLO");
    set(soloTree, "channel", juce::int64(solo ? solo->channel : 0));
    if (solo)
    {
        if (solo->groupScoped)
        {
            set(soloTree, "groupScoped", true);
            set(soloTree, "group", juce::int64(solo->group));
        }
        for (auto id : solo->mutedBefore)
        {
            juce::ValueTree item("MUTED");
            set(item, "channel", juce::int64(id));
            soloTree.appendChild(item, nullptr);
        }
        for (auto id : solo->affected)
        {
            juce::ValueTree item("AFFECTED");
            set(item, "channel", juce::int64(id));
            soloTree.appendChild(item, nullptr);
        }
    }
    root.appendChild(soloTree, nullptr);
    juce::ValueTree clipGroupTree("CLIP_GROUPS");
    for (const auto &group : clipGroups)
    {
        juce::ValueTree entry("GROUP");
        set(entry, "id", juce::int64(group.id));
        for (auto id : group.members)
        {
            juce::ValueTree item("MEMBER");
            set(item, "clip", juce::int64(id));
            entry.appendChild(item, nullptr);
        }
        clipGroupTree.appendChild(entry, nullptr);
    }
    root.appendChild(clipGroupTree, nullptr);
    if (!arrangements.empty() || arrangementId != 0 || arrangementName.isNotEmpty())
    {
        juce::ValueTree tree("ARRANGEMENTS");
        set(tree, "current", juce::int64(arrangementId));
        set(tree, "name", arrangementName);
        for (const auto &a : arrangements)
            tree.appendChild(arrangements::toTree(a), nullptr);
        root.appendChild(tree, nullptr);
    }
    if (std::any_of(padPatterns.begin(), padPatterns.end(), [](Id id) { return id != 0; }))
    {
        juce::StringArray ids;
        for (const auto id : padPatterns)
            ids.add(juce::String(juce::int64(id)));
        set(root, "padPatterns", ids.joinIntoString(","));
    }
    if (!meterChanges.empty())
    {
        juce::ValueTree meterTree("METER_CHANGES");
        for (const auto &change : meterChanges)
        {
            juce::ValueTree entry("METER_CHANGE");
            set(entry, "id", juce::int64(change.id));
            set(entry, "beat", change.beat);
            set(entry, "numerator", change.numerator);
            set(entry, "denominator", change.denominator);
            meterTree.appendChild(entry, nullptr);
        }
        root.appendChild(meterTree, nullptr);
    }
    if (!tempoChanges.empty())
    {
        juce::ValueTree tempoTree("TEMPO_CHANGES");
        for (const auto &change : tempoChanges)
        {
            juce::ValueTree entry("TEMPO_CHANGE");
            set(entry, "id", juce::int64(change.id));
            set(entry, "beat", change.beat);
            set(entry, "bpm", change.bpm);
            set(entry, "ramp", change.ramp);
            tempoTree.appendChild(entry, nullptr);
        }
        root.appendChild(tempoTree, nullptr);
    }
    if (std::any_of(performanceScenes.begin(), performanceScenes.end(), [](const auto &s) { return !s.empty(); }))
    {
        juce::ValueTree sceneTree("SCENES");
        for (size_t i = 0; i < performanceScenes.size(); ++i)
            for (const auto pattern : performanceScenes[i])
            {
                juce::ValueTree entry("SCENE_PATTERN");
                set(entry, "scene", int(i) + 1);
                set(entry, "pattern", juce::int64(pattern));
                sceneTree.appendChild(entry, nullptr);
            }
        root.appendChild(sceneTree, nullptr);
    }
    if (!tracks.empty())
    {
        juce::ValueTree trackTree("TRACKS");
        for (const auto &track : tracks)
        {
            juce::ValueTree entry("TRACK");
            set(entry, "lane", track.lane);
            if (track.name.isNotEmpty())
                set(entry, "name", track.name);
            if (track.colour != 0)
                set(entry, "colour", juce::String::toHexString(track.colour).paddedLeft('0', 8));
            if (track.muted)
                set(entry, "muted", true);
            if (track.groupedAbove)
                set(entry, "grouped", true);
            if (track.collapsed)
                set(entry, "collapsed", true);
            if (track.locked)
                set(entry, "locked", true);
            if (track.height != 1)
                set(entry, "height", track.height);
            if (track.channel != 0)
                set(entry, "channel", juce::int64(track.channel));
            if (track.audio)
                set(entry, "audio", true);
            trackTree.appendChild(entry, nullptr);
        }
        root.appendChild(trackTree, nullptr);
    }
    if (!markers.empty())
    {
        juce::ValueTree markerTree("MARKERS");
        for (const auto &marker : markers)
        {
            juce::ValueTree entry("MARKER");
            set(entry, "id", juce::int64(marker.id));
            set(entry, "beat", marker.beat);
            set(entry, "name", marker.name);
            if (marker.action.isNotEmpty())
                set(entry, "action", marker.action);
            markerTree.appendChild(entry, nullptr);
        }
        root.appendChild(markerTree, nullptr);
    }
    if (!chords.empty())
    {
        juce::ValueTree chordTree("CHORDS");
        for (const auto &chord : chords)
        {
            juce::ValueTree entry("CHORD");
            set(entry, "id", juce::int64(chord.id));
            set(entry, "beat", chord.beat);
            set(entry, "symbol", chord.symbol);
            chordTree.appendChild(entry, nullptr);
        }
        root.appendChild(chordTree, nullptr);
    }
    juce::ValueTree cs("CHANNELS"), ps("PATTERNS"), ls("PLACEMENTS"), ms("MIXER"), als("AUDIO_PLACEMENTS");
    for (auto &c : channels)
    {
        juce::ValueTree t("CHANNEL");
        set(t, "id", juce::int64(c.id));
        set(t, "name", c.name);
        if (c.colour != 0)
            set(t, "colour", juce::int64(c.colour));
        if (c.icon != "none")
            set(t, "icon", c.icon);
        if (c.muteLocked)
            set(t, "muteLocked", true);
        if (c.pitchBend != 0)
            set(t, "pitchBend", double(c.pitchBend));
        if (c.pitchRange != 2)
            set(t, "pitchRange", c.pitchRange);
        if (!c.keyLabels.empty())
        {
            juce::StringArray labels;
            for (const auto &[pitch, label] : c.keyLabels)
                labels.add(juce::String(pitch) + "=" + label);
            set(t, "keyLabels", labels.joinIntoString(";"));
        }
        if (c.receiveMidiChannel != 0)
            set(t, "receiveMidi", c.receiveMidiChannel);
        if (!c.articulations.empty())
        {
            juce::StringArray entries;
            for (const auto &a : c.articulations)
                entries.add((a.kind == 1   ? "cc" + juce::String(a.keyswitch) + ":" + juce::String(a.value)
                             : a.kind == 2 ? "pc" + juce::String(a.keyswitch)
                                           : juce::String(a.keyswitch)) +
                            "=" + a.name);
            set(t, "articulations", entries.joinIntoString(";"));
        }
        if (c.group != 0)
            set(t, "group", juce::int64(c.group));
        set(t, "source", c.source);
        set(t, "mixer", c.mixer);
        set(t, "gain", double(c.gain));
        set(t, "pan", double(c.pan));
        set(t, "muted", c.muted);
        set(t, "swingMix", c.swingMix);
        set(t, "truncateSwing", c.truncateSwing);
        set(t, "audioClip", c.audioClip);
        set(t, "audioDuration", c.audioDuration);
        if (c.mpe)
            set(t, "mpe", true);
        if (c.mtsEsp)
            set(t, "mtsEsp", true);
        if (std::any_of(c.tuning.begin(), c.tuning.end(), [](double cents) { return cents != 0; }))
        {
            juce::StringArray cents;
            for (auto value : c.tuning)
                cents.add(juce::String(value, 6));
            set(t, "tuning", cents.joinIntoString(","));
        }
        if (!c.scale.empty())
        {
            juce::StringArray cents;
            for (auto value : c.scale)
                cents.add(juce::String(value, 6));
            set(t, "scale", cents.joinIntoString(","));
            set(t, "scaleRoot", c.scaleRoot);
        }
        juce::ValueTree sampler("SAMPLER");
        set(sampler, "root", c.sampler.root);
        set(sampler, "cents", c.sampler.cents);
        set(sampler, "start", c.sampler.start);
        set(sampler, "end", c.sampler.end);
        set(sampler, "reverse", c.sampler.reverse);
        set(sampler, "normalize", c.sampler.normalize);
        set(sampler, "envelope", c.sampler.envelope);
        set(sampler, "attack", c.sampler.attack);
        set(sampler, "decay", c.sampler.decay);
        set(sampler, "sustain", c.sampler.sustain);
        set(sampler, "release", c.sampler.release);
        set(sampler, "cut", c.sampler.cut);
        set(sampler, "cutBy", c.sampler.cutBy);
        set(sampler, "maxPoly", c.sampler.maxPoly);
        set(sampler, "sourceTempo", c.sampler.sourceTempo);
        set(sampler, "fitTempo", c.sampler.fitTempo);
        set(sampler, "loop", c.sampler.loop);
        set(sampler, "pingPong", c.sampler.pingPong);
        set(sampler, "loopStart", c.sampler.loopStart);
        set(sampler, "loopEnd", c.sampler.loopEnd);
        set(sampler, "filterType", c.sampler.filterType);
        set(sampler, "filterCutoff", c.sampler.filterCutoff);
        set(sampler, "filterResonance", c.sampler.filterResonance);
        if (c.sampler.fadeIn != 0)
            set(sampler, "fadeIn", c.sampler.fadeIn); // Sparse project42 fields.
        if (c.sampler.fadeOut != 0)
            set(sampler, "fadeOut", c.sampler.fadeOut);
        if (c.sampler.loopCrossfade != 0)
            set(sampler, "loopCrossfade", c.sampler.loopCrossfade);
        if (c.sampler.stretch)
            set(sampler, "stretch", true);
        if (c.sampler.stretchPitch != 0)
            set(sampler, "stretchPitch", c.sampler.stretchPitch);
        if (c.sampler.timeMul != 1)
            set(sampler, "timeMul", c.sampler.timeMul);
        if (c.sampler.timeBeats != 0)
            set(sampler, "timeBeats", c.sampler.timeBeats);
        if (c.sampler.hqInterpolation)
            set(sampler, "hqInterpolation", true);
        if (c.sampler.mono || c.sampler.portamento || c.sampler.fullPortamento ||
            c.sampler.slideTime != .06 || c.sampler.gateBeats != 0)
        {
            juce::ValueTree voice("VOICE");
            set(voice, "mono", c.sampler.mono);
            set(voice, "portamento", c.sampler.portamento);
            set(voice, "fullPortamento", c.sampler.fullPortamento);
            set(voice, "slideTime", c.sampler.slideTime);
            set(voice, "gateBeats", c.sampler.gateBeats);
            sampler.appendChild(voice, nullptr);
        }
        juce::ValueTree modulation("MODULATION");
        const bool stored = std::any_of(c.sampler.modulation.begin(), c.sampler.modulation.end(),
                                        [](const auto &target) { return target != SamplerModulation{}; });
        set(modulation, "stored", stored);
        for (size_t target = 0; stored && target < c.sampler.modulation.size(); ++target)
        {
            juce::ValueTree row("TARGET");
            set(row, "id", juce::String(samplerModulationTargets[target].data()));
            for (const auto &parameter : samplerModulationParameters)
            {
                const char *name = parameter.id.data();
                const auto value = samplerModulationValue(c.sampler.modulation[target], parameter);
                if (parameter.boolean)
                    set(row, name, value != 0);
                else if (parameter.integral)
                    set(row, name, int(value));
                else
                    set(row, name, value);
            }
            modulation.appendChild(row, nullptr);
        }
        sampler.appendChild(modulation, nullptr);
        t.appendChild(sampler, nullptr);
        if (c.instrument)
            t.appendChild(pluginState(*c.instrument), nullptr);
        cs.appendChild(t, nullptr);
    }
    for (auto &p : patterns)
    {
        juce::ValueTree t("PATTERN");
        set(t, "id", juce::int64(p.id));
        set(t, "name", p.name);
        if (p.group.isNotEmpty())
            set(t, "group", p.group);
        if (p.colour != 0)
            set(t, "colour", juce::String::toHexString(p.colour).paddedLeft('0', 8));
        set(t, "length", p.length);
        set(t, "autoLength", p.autoLength);
        set(t, "numerator", p.numerator);
        set(t, "denominator", p.denominator);
        if (p.launchQuantum != -1)
            set(t, "launchQuantum", p.launchQuantum);
        if (p.followAction.isNotEmpty())
        {
            set(t, "followAction", p.followAction);
            set(t, "followAfter", p.followAfter);
        }
        if (p.generation.isNotEmpty())
            set(t, "generation", p.generation);
        for (const auto &loop : p.channelLoops)
        {
            juce::ValueTree e("CHANNEL_LOOP");
            set(e, "channel", juce::int64(loop.channel));
            set(e, "mode", channelLoopModeName(loop.mode));
            set(e, "length", loop.length);
            t.appendChild(e, nullptr);
        }
        for (auto &n : p.notes)
        {
            juce::ValueTree e("NOTE");
            set(e, "id", juce::int64(n.id));
            set(e, "channel", juce::int64(n.channel));
            set(e, "beat", n.beat);
            set(e, "length", n.length);
            set(e, "pitch", n.pitch);
            set(e, "velocity", double(n.velocity));
            set(e, "step", n.step);
            set(e, "muted", n.muted);
            if (n.pan != 0)
                set(e, "pan", n.pan);
            if (n.fineCents != 0)
                set(e, "fineCents", n.fineCents);
            if (n.shiftPercent != 0)
                set(e, "shiftPercent", n.shiftPercent);
            if (n.repeatMode != 0)
                set(e, "repeatMode", n.repeatMode);
            if (n.releaseVelocity != 0)
                set(e, "releaseVelocity", n.releaseVelocity);
            if (n.modX != .5)
                set(e, "modX", n.modX);
            if (n.slide)
                set(e, "slide", n.slide);
            if (n.portamento)
                set(e, "portamento", n.portamento);
            if (n.colourGroup != 0)
                set(e, "colourGroup", n.colourGroup);
            if (n.group != 0)
                set(e, "group", juce::int64(n.group));
            if (n.articulation != 0)
                set(e, "articulation", n.articulation);
            if (n.modY != .5)
                set(e, "modY", n.modY);
            t.appendChild(e, nullptr);
        }
        for (const auto &w : p.automation)
        {
            juce::ValueTree e("AUTOMATION");
            set(e, "source", juce::int64(w.source));
            set(e, "beat", w.beat);
            set(e, "length", w.length);
            if (w.offset != 0)
                set(e, "offset", w.offset);
            if (w.muted)
                set(e, "muted", true);
            t.appendChild(e, nullptr);
        }
        for (const auto &c : p.controllers)
        {
            juce::ValueTree e("CONTROLLER");
            set(e, "id", juce::int64(c.id));
            set(e, "channel", juce::int64(c.channel));
            set(e, "beat", c.beat);
            set(e, "type", c.type);
            set(e, "number", c.number);
            set(e, "value", c.value);
            set(e, "midiChannel", c.midiChannel);
            t.appendChild(e, nullptr);
        }
        ps.appendChild(t, nullptr);
    }
    for (auto &p : placements)
    {
        juce::ValueTree t("PLACEMENT");
        set(t, "id", juce::int64(p.id));
        set(t, "pattern", juce::int64(p.pattern));
        set(t, "lane", p.lane);
        set(t, "beat", p.beat);
        set(t, "length", p.length);
        set(t, "offset", p.offset);
        set(t, "muted", p.muted);
        ls.appendChild(t, nullptr);
    }
    for (auto &m : mixer)
    {
        juce::ValueTree t("INSERT");
        set(t, "index", m.index);
        set(t, "name", m.name);
        set(t, "gain", double(m.gain));
        set(t, "pan", double(m.pan));
        if (m.stereoSeparation != 0) set(t, "stereoSeparation", double(m.stereoSeparation));
        if (m.delayMs != 0)
            set(t, "delayMs", double(m.delayMs));
        if (m.dock != 0)
            set(t, "dock", m.dock);
        if (!(m.eq == MixerEqSettings{}))
        {
            juce::StringArray values;
            for (int band = 0; band < 3; ++band)
                values.add(juce::String(m.eq.gain[size_t(band)], 3)), values.add(juce::String(m.eq.freq[size_t(band)], 2));
            set(t, "eq", values.joinIntoString(","));
        }
        if (m.phaseInverted) set(t, "phaseInverted", true);
        if (m.stereoSwapped) set(t, "stereoSwapped", true);
        set(t, "muted", m.muted);
        if (m.effectsBypassed)
            set(t, "effectsBypassed", true);
        if (m.colour != 0)
            set(t, "colour", juce::int64(m.colour));
        for (auto &plugin : m.effects)
            t.appendChild(pluginState(plugin), nullptr);
        const bool defaultRoutes = m.index == 0 ? m.routes.empty() : m.routes == std::vector<MixerRoute>{{0,1}};
        if (!defaultRoutes)
        {
            juce::ValueTree routes("ROUTES");
            for (const auto &route : m.routes)
            {
                juce::ValueTree child("SEND");
                set(child,"target",route.target); set(child,"gain",double(route.gain));
                routes.appendChild(child,nullptr);
            }
            t.appendChild(routes,nullptr);
        }
        ms.appendChild(t, nullptr);
    }
    if (mixerSolo)
    {
        juce::ValueTree saved("MIXER_SOLO");
        set(saved, "insert", mixerSolo->insert);
        set(saved, "connected", mixerSolo->connected);
        if (mixerSolo->group.isNotEmpty()) set(saved, "group", mixerSolo->group);
        for (auto index : mixerSolo->mutedBefore)
        {
            juce::ValueTree item("MUTED");
            set(item, "insert", index);
            saved.appendChild(item, nullptr);
        }
        root.appendChild(saved, nullptr);
    }
    for (const auto &p : audioPlacements)
    {
        juce::ValueTree t("AUDIO_PLACEMENT");
        set(t, "id", juce::int64(p.id));
        set(t, "channel", juce::int64(p.channel));
        set(t, "lane", p.lane);
        set(t, "beat", p.beat);
        set(t, "offset", p.offset);
        set(t, "muted", p.muted);
        set(t, "duration", p.duration);
        if (p.fadeIn > 0)
            set(t, "fadeIn", p.fadeIn);
        if (p.fadeOut > 0)
            set(t, "fadeOut", p.fadeOut);
        if (p.warpBpm > 0)
        {
            set(t, "warpBpm", p.warpBpm);
            set(t, "warpChannel", juce::int64(p.warpChannel));
            set(t, "warpOffset", p.warpOffset);
            set(t, "warpDuration", p.warpDuration);
        }
        als.appendChild(t, nullptr);
    }
    root.appendChild(als, nullptr);
    root.appendChild(automationState(automation), nullptr);
    root.appendChild(cs, nullptr);
    juce::ValueTree groups("CHANNEL_GROUPS");
    for (const auto &group : channelGroups)
    {
        juce::ValueTree item("GROUP");
        set(item, "id", juce::int64(group.id));
        set(item, "name", group.name);
        groups.appendChild(item, nullptr);
    }
    root.appendChild(groups, nullptr);
    root.appendChild(ps, nullptr);
    root.appendChild(ls, nullptr);
    root.appendChild(ms, nullptr);
    if (!mixerGroups.empty())
    {
        juce::ValueTree groups("MIXER_GROUPS");
        for (const auto &group : mixerGroups)
        {
            juce::ValueTree row("GROUP");
            set(row, "id", group.id); set(row, "name", group.name); set(row, "colour", juce::int64(group.colour));
            for (int id : group.members)
            {
                juce::ValueTree member("MEMBER"); set(member, "insert", id); row.appendChild(member, nullptr);
            }
            groups.appendChild(row, nullptr);
        }
        root.appendChild(groups, nullptr);
    }
    if (!samplerControllerLinks.empty())
    {
        juce::ValueTree links("SAMPLER_CONTROLLER_LINKS");
        for (const auto &link : samplerControllerLinks)
        {
            juce::ValueTree row("LINK");
            set(row, "id", juce::int64(link.id));
            set(row, "channel", juce::int64(link.channel));
            set(row, "parameter", juce::int64(link.parameter));
            set(row, "port", link.port);
            set(row, "midiChannel", link.midiChannel);
            set(row, "cc", link.cc);
            set(row, "layer", link.layer);
            set(row, "minimum", link.minimum);
            set(row, "maximum", link.maximum);
            set(row, "pickup", link.pickup);
            set(row, "enabled", link.enabled);
            set(row, "label", link.label);
            links.appendChild(row, nullptr);
        }
        root.appendChild(links, nullptr);
    }
    return root;
}
static double exactNumber(const juce::var &value)
{
    if (value.isDouble() || value.isInt() || value.isInt64())
        return double(value);
    if (!value.isString())
        return std::numeric_limits<double>::quiet_NaN();
    const auto text = value.toString().trim().toStdString();
    auto begin = text.data(), end = begin + text.size();
    if (begin != end && *begin == '+')
        ++begin;
    double number = 0;
#if JUCE_MAC
    return number_text::parseClassic(std::string_view(begin, size_t(end - begin)), number)
               ? number : std::numeric_limits<double>::quiet_NaN();
#else
    const auto converted = std::from_chars(begin, end, number, std::chars_format::general);
    return converted.ec == std::errc{} && converted.ptr == end ? number
                                                               : std::numeric_limits<double>::quiet_NaN();
#endif
}
juce::Result Project::parse(const juce::ValueTree &t, Project &result)
{
    if (!t.hasType("VIBERA") || !t.hasProperty("version"))
        return juce::Result::fail("Not a VibeRa project");
    const auto fileVersion = exactNumber(t["version"]);
    if (!std::isfinite(fileVersion) || fileVersion < 1 || fileVersion > currentVersion ||
        std::floor(fileVersion) != fileVersion)
        return juce::Result::fail("Unsupported project version; file was not changed");
    if (int(t["version"]) >= 3 && !t.getChildWithName("AUDIO_PLACEMENTS").isValid())
        return juce::Result::fail("Missing audio placements");
    if (int(t["version"]) < 3 && t.getChildWithName("AUDIO_PLACEMENTS").getNumChildren() != 0)
        return juce::Result::fail("Audio placements require project version 3");
    Project p;
    int controllerLinkTrees = 0;
    for (const auto &child : t)
        controllerLinkTrees += child.hasType("SAMPLER_CONTROLLER_LINKS");
    const auto controllerLinks = t.getChildWithName("SAMPLER_CONTROLLER_LINKS");
    if (controllerLinkTrees > 1 || (fileVersion < 39 && controllerLinkTrees != 0) ||
        controllerLinks.getNumProperties() != 0 ||
        controllerLinks.getNumChildren() > int(samplerControllerLinkLimit))
        return juce::Result::fail("samplerController.invalidSchema: Invalid/newer project link collection");
    auto linkInteger = [](const juce::var &value, std::int64_t &out)
    {
        if (value.isInt() || value.isInt64())
        {
            out = juce::int64(value);
            return true;
        }
        if (!value.isString())
            return false;
        const auto text = value.toString().toStdString();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    };
    auto linkBoolean = [](const juce::var &value, bool &out)
    {
        if (value.isBool())
        {
            out = bool(value);
            return true;
        }
        if (value.isString() && (value.toString() == "0" || value.toString() == "1"))
        {
            out = value.toString() == "1";
            return true;
        }
        return false;
    };
    for (const auto &row : controllerLinks)
    {
        if (!row.hasType("LINK") || row.getNumChildren() != 0 || row.getNumProperties() != 12)
            return juce::Result::fail("samplerController.invalidSchema: Exact typed LINK fields required");
        for (const auto *field : {"id", "channel", "parameter", "port", "midiChannel", "cc", "layer",
                                  "minimum", "maximum", "pickup", "enabled", "label"})
            if (!row.hasProperty(field))
                return juce::Result::fail("samplerController.invalidSchema: Missing LINK field");
        SamplerControllerLink link;
        std::int64_t parameter = 0, port = 0, midiChannel = 0, cc = 0, layer = 0;
        if (!linkInteger(row["id"], link.id) || !linkInteger(row["channel"], link.channel) ||
            !linkInteger(row["parameter"], parameter) || parameter < 0 || parameter > 147 ||
            !linkInteger(row["port"], port) || port < 0 || port > 1 ||
            !linkInteger(row["midiChannel"], midiChannel) || midiChannel < 0 || midiChannel > 16 ||
            !linkInteger(row["cc"], cc) || cc < 0 || cc > 127 || !linkInteger(row["layer"], layer) ||
            layer < 0 || layer > 3 || !linkBoolean(row["pickup"], link.pickup) ||
            !linkBoolean(row["enabled"], link.enabled) || !row["label"].isString())
            return juce::Result::fail("samplerController.invalidSchema: Invalid LINK field type/range");
        link.parameter = std::uint32_t(parameter);
        link.port = int(port);
        link.midiChannel = int(midiChannel);
        link.cc = int(cc);
        link.layer = int(layer);
        link.minimum = exactNumber(row["minimum"]);
        link.maximum = exactNumber(row["maximum"]);
        link.label = row["label"].toString();
        p.samplerControllerLinks.push_back(std::move(link));
    }
    if (const auto tree = t.getChildWithName("ARRANGEMENTS"); tree.isValid())
    {
        if (fileVersion < 48 || tree.getNumChildren() > 256)
            return juce::Result::fail("Arrangements need project version 48 (at most 256)");
        p.arrangementId = juce::int64(tree["current"]);
        p.arrangementName = tree["name"].toString();
        for (const auto &entry : tree)
        {
            Arrangement a;
            if (auto parsed = arrangements::fromTree(entry, a); parsed.failed())
                return parsed;
            p.arrangements.push_back(std::move(a));
        }
    }
    if (t.hasProperty("padPatterns"))
    {
        const auto ids = juce::StringArray::fromTokens(t["padPatterns"].toString(), ",", "");
        if (fileVersion < 55 || ids.size() != 16)
            return juce::Result::fail("Performance pad layout needs project version 55 and 16 entries");
        for (int i = 0; i < 16; ++i)
            p.padPatterns[size_t(i)] = Id(ids[i].getLargeIntValue());
    }
    if (const auto meterTree = t.getChildWithName("METER_CHANGES"); meterTree.isValid())
    {
        if (fileVersion < 54 || meterTree.getNumChildren() > 1024)
            return juce::Result::fail("Song time-signature changes need project version 54");
        for (const auto &entry : meterTree)
        {
            const auto id = exactNumber(entry["id"]);
            if (!entry.hasType("METER_CHANGE") || !std::isfinite(id) || id < 1 || std::floor(id) != id)
                return juce::Result::fail("Invalid time-signature change");
            p.meterChanges.push_back({Id(id), exactNumber(entry["beat"]), int(entry["numerator"]), int(entry["denominator"])});
        }
    }
    if (const auto tempoTree = t.getChildWithName("TEMPO_CHANGES"); tempoTree.isValid())
    {
        int trees = 0;
        for (const auto &child : t)
            trees += child.hasType("TEMPO_CHANGES");
        if (trees > 1 || fileVersion < 45 || tempoTree.getNumChildren() > 4096)
            return juce::Result::fail("Tempo changes need project version 45 (one list, at most 4096)");
        for (const auto &entry : tempoTree)
        {
            const auto id = exactNumber(entry["id"]);
            if (!entry.hasType("TEMPO_CHANGE") || !std::isfinite(id) || id < 1 || id > 1000000000 ||
                std::floor(id) != id)
                return juce::Result::fail("Invalid tempo change");
            p.tempoChanges.push_back({Id(id), exactNumber(entry["beat"]), exactNumber(entry["bpm"]), bool(entry["ramp"])});
        }
    }
    int markerTrees = 0;
    for (const auto &child : t)
        markerTrees += child.hasType("MARKERS");
    if (const auto markerTree = t.getChildWithName("MARKERS"); markerTree.isValid())
    {
        if (markerTrees > 1 || fileVersion < 44 || markerTree.getNumChildren() > 10000)
            return juce::Result::fail("Playlist markers need project version 44 (one list, at most 10000)");
        for (const auto &entry : markerTree)
        {
            const auto id = exactNumber(entry["id"]);
            if (!entry.hasType("MARKER") || !std::isfinite(id) || id < 1 || id > 1000000000 ||
                std::floor(id) != id)
                return juce::Result::fail("Invalid Playlist marker");
            p.markers.push_back({Id(id), exactNumber(entry["beat"]), entry["name"].toString()});
            if (entry.hasProperty("action"))
            {
                const auto action = entry["action"].toString();
                if (fileVersion < 57 || action.isEmpty() || !validMarkerAction(action))
                    return juce::Result::fail("Invalid Playlist marker action");
                p.markers.back().action = action;
            }
        }
    }
    if (const auto chordTree = t.getChildWithName("CHORDS"); chordTree.isValid())
    {
        if (fileVersion < 57 || chordTree.getNumChildren() > 10000)
            return juce::Result::fail("Chord Track needs project version 57 (at most 10000 chords)");
        for (const auto &entry : chordTree)
        {
            const auto id = exactNumber(entry["id"]);
            if (!entry.hasType("CHORD") || !std::isfinite(id) || id < 1 || id > 1000000000 || std::floor(id) != id)
                return juce::Result::fail("Invalid Chord Track event");
            p.chords.push_back({Id(id), exactNumber(entry["beat"]), entry["symbol"].toString()});
        }
    }
    if (const auto sceneTree = t.getChildWithName("SCENES"); sceneTree.isValid())
    {
        if (fileVersion < 52 || sceneTree.getNumChildren() > 8 * 1024)
            return juce::Result::fail("Performance scenes need project version 52");
        for (const auto &entry : sceneTree)
        {
            const auto scene = exactNumber(entry["scene"]), pattern = exactNumber(entry["pattern"]);
            if (!entry.hasType("SCENE_PATTERN") || !std::isfinite(scene) || scene < 1 || scene > 8 ||
                std::floor(scene) != scene || !std::isfinite(pattern) || pattern < 1 || std::floor(pattern) != pattern)
                return juce::Result::fail("Invalid performance scene");
            p.performanceScenes[size_t(scene) - 1].push_back(Id(pattern));
        }
    }
    int trackTrees = 0;
    for (const auto &child : t)
        trackTrees += child.hasType("TRACKS");
    if (const auto trackTree = t.getChildWithName("TRACKS"); trackTree.isValid())
    {
        if (trackTrees > 1 || fileVersion < 50 || trackTree.getNumChildren() > 128)
            return juce::Result::fail("Playlist tracks need project version 50 (one list, at most 128)");
        for (const auto &entry : trackTree)
        {
            const auto lane = exactNumber(entry["lane"]);
            PlaylistTrack track;
            if (!entry.hasType("TRACK") || !std::isfinite(lane) || lane < 0 || lane > 127 ||
                std::floor(lane) != lane || p.track(int(lane)) || entry["name"].toString().length() > 128)
                return juce::Result::fail("Invalid Playlist track");
            track.lane = int(lane);
            track.name = entry["name"].toString();
            track.muted = bool(entry["muted"]);
            if ((entry.hasProperty("grouped") || entry.hasProperty("collapsed")) && fileVersion < 51)
                return juce::Result::fail("Playlist track groups need project version 51");
            track.groupedAbove = bool(entry["grouped"]) && track.lane > 0;
            track.collapsed = bool(entry["collapsed"]);
            if (entry.hasProperty("locked") && fileVersion < 56)
                return juce::Result::fail("Playlist track locks need project version 56");
            track.locked = bool(entry["locked"]);
            if (entry.hasProperty("height"))
            {
                const double height = double(entry["height"]);
                if (fileVersion < 56 || !std::isfinite(height) || height < .5 || height > 3)
                    return juce::Result::fail("Playlist track heights are .5..3 and need project version 56");
                track.height = height;
            }
            if (entry.hasProperty("channel"))
            {
                if (fileVersion < 56)
                    return juce::Result::fail("Instrument tracks need project version 56");
                track.channel = Id(juce::int64(entry["channel"]));
            }
            if (entry.hasProperty("audio") && fileVersion < 56)
                return juce::Result::fail("Audio tracks need project version 56");
            track.audio = bool(entry["audio"]) && track.channel == 0;
            if (entry.hasProperty("colour"))
            {
                const auto text = entry["colour"].toString();
                if (text.length() != 8 || !text.containsOnly("0123456789abcdefABCDEF") ||
                    (juce::uint32(text.getHexValue32()) >> 24) != 255)
                    return juce::Result::fail("Playlist track colour must be opaque ARGB");
                track.colour = juce::uint32(text.getHexValue32());
            }
            p.tracks.push_back(track);
        }
        std::sort(p.tracks.begin(), p.tracks.end(), [](const auto &x, const auto &y) { return x.lane < y.lane; });
    }
    int clipGroupTrees = 0;
    for (const auto &child : t)
        clipGroupTrees += child.hasType("CLIP_GROUPS");
    const auto clipGroups = t.getChildWithName("CLIP_GROUPS");
    if (clipGroupTrees > 1 || (fileVersion >= 16 && !clipGroups.isValid()) ||
        (fileVersion < 16 && clipGroups.isValid()))
        return juce::Result::fail("Missing/duplicate Clip groups or newer data under an old project version");
    if (clipGroups.getNumChildren() > 50000)
        return juce::Result::fail("Too many Clip groups");
    size_t groupMembers = 0;
    for (const auto &entry : clipGroups)
    {
        const auto number = exactNumber(entry["id"]);
        if (!entry.hasType("GROUP") || !std::isfinite(number) || number < 1 || number > 1000000000 ||
            std::floor(number) != number || entry.getNumChildren() < 2 || entry.getNumChildren() > 100000)
            return juce::Result::fail("Invalid Clip group identity or size");
        groupMembers += size_t(entry.getNumChildren());
        if (groupMembers > 100000)
            return juce::Result::fail("Too many Clip group members");
        ClipGroup group;
        group.id = Id(number);
        for (const auto &member : entry)
        {
            const auto id = exactNumber(member["clip"]);
            if (!member.hasType("MEMBER") || !std::isfinite(id) || id < 1 || id > 1000000000 ||
                std::floor(id) != id || member.getNumChildren() != 0)
                return juce::Result::fail("Invalid Clip group member");
            group.members.push_back(Id(id));
        }
        p.clipGroups.push_back(std::move(group));
    }
    int automationTrees = 0;
    for (const auto &child : t)
        if (child.hasType("AUTOMATION"))
            ++automationTrees;
    if (automationTrees > 1)
        return juce::Result::fail("Duplicated automation data");
    const auto automated = t.getChildWithName("AUTOMATION");
    if (int(fileVersion) >= 8 && !automated.isValid())
        return juce::Result::fail("Missing automation data");
    if (automated.isValid())
    {
        if (auto parsed = parseAutomationState(automated, p.automation, int(fileVersion)); parsed.failed())
            return parsed;
        if (int(fileVersion) < 8 && (!p.automation.sources.empty() || !p.automation.placements.empty() ||
                                     !p.automation.initialValues.empty() || !p.automation.modulators.empty()))
            return juce::Result::fail("Automation data requires project version 8");
    }
    p.nextId = juce::int64(t["nextId"]);
    const auto soloTree = t.getChildWithName("CHANNEL_SOLO");
    int soloTrees = 0;
    for (const auto &child : t)
        soloTrees += child.hasType("CHANNEL_SOLO");
    if (soloTrees > 1 || (fileVersion >= 12 && !soloTree.isValid()))
        return juce::Result::fail("Missing or duplicate Channel solo state");
    if (soloTree.isValid())
    {
        const auto target = exactNumber(soloTree["channel"]);
        if (!std::isfinite(target) || target < 0 || target > 1000000000 || std::floor(target) != target ||
            (target == 0 && soloTree.getNumChildren() != 0) || (fileVersion < 12 && target != 0) ||
            soloTree.getNumChildren() > (fileVersion >= 28 ? 256 : 128))
            return juce::Result::fail("Invalid or unsupported Channel solo state");
        if ((soloTree.hasProperty("groupScoped") || soloTree.hasProperty("group")) &&
            (fileVersion < 28 || target == 0))
            return juce::Result::fail("Scoped Channel solo requires an active schema28 solo");
        if (target != 0)
        {
            p.solo = SoloState{Id(target), {}};
            if (soloTree.hasProperty("groupScoped"))
            {
                const auto scoped = soloTree["groupScoped"];
                if (!scoped.isBool() && scoped.toString() != "0" && scoped.toString() != "1")
                    return juce::Result::fail("Invalid Channel solo scope flag");
                p.solo->groupScoped = bool(scoped);
            }
            if (p.solo->groupScoped != soloTree.hasProperty("group"))
                return juce::Result::fail("Channel solo group requires explicit group scope");
            if (p.solo->groupScoped)
            {
                const auto group = exactNumber(soloTree["group"]);
                if (!std::isfinite(group) || std::floor(group) != group || group < 0 || group > 1000000000)
                    return juce::Result::fail("Invalid Channel solo group identity");
                p.solo->group = Id(group);
            }
            for (const auto &item : soloTree)
            {
                const auto id = exactNumber(item["channel"]);
                const bool affected = item.hasType("AFFECTED");
                if ((!item.hasType("MUTED") && !affected) || (affected && fileVersion < 28) ||
                    !std::isfinite(id) || id < 1 || id > 1000000000 || std::floor(id) != id)
                    return juce::Result::fail("Invalid solo mute baseline");
                (affected ? p.solo->affected : p.solo->mutedBefore).push_back(Id(id));
            }
        }
    }
    const auto groups = t.getChildWithName("CHANNEL_GROUPS");
    int groupTrees = 0;
    for (const auto &child : t)
        groupTrees += child.hasType("CHANNEL_GROUPS");
    if (groupTrees > 1 || (fileVersion >= 11 && !groups.isValid()) ||
        (fileVersion < 11 && groups.getNumChildren() != 0))
        return juce::Result::fail("Missing, duplicated or unsupported Channel group data");
    for (const auto &group : groups)
    {
        const auto id = exactNumber(group["id"]);
        if (!group.hasType("GROUP") || !std::isfinite(id) || std::floor(id) != id || id < 1 ||
            id > 1000000000 || !group["name"].isString())
            return juce::Result::fail("Invalid Channel group data");
        p.channelGroups.push_back({Id(id), group["name"].toString()});
    }
    p.name = t["name"].toString();
    p.tempo = exactNumber(t["tempo"]);
    if (fileVersion >= 13)
    {
        if (!t.hasProperty("swing"))
            return juce::Result::fail("Missing project Swing");
        p.swing = exactNumber(t["swing"]);
    }
    else if (t.hasProperty("swing") && exactNumber(t["swing"]) != 0)
        return juce::Result::fail("Global Swing requires project version 13");
    p.numerator = int(t["numerator"]);
    p.denominator = int(t["denominator"]);
    if (!t.getChildWithName("CHANNELS").isValid() || !t.getChildWithName("PATTERNS").isValid() ||
        !t.getChildWithName("PLACEMENTS").isValid() || !t.getChildWithName("MIXER").isValid())
        return juce::Result::fail("Incomplete project");
    for (auto c : t.getChildWithName("CHANNELS"))
    {
        p.channels.push_back({juce::int64(c["id"]), c["name"].toString(), c["source"].toString(),
                              int(c["mixer"]), float(exactNumber(c["gain"])), float(exactNumber(c["pan"])),
                              bool(c["muted"])});
        if (fileVersion >= 13)
        {
            if (!c.hasProperty("swingMix") || !c.hasProperty("truncateSwing"))
                return juce::Result::fail("Missing Channel Swing settings");
            p.channels.back().swingMix = exactNumber(c["swingMix"]);
            if (!c["truncateSwing"].isBool() && c["truncateSwing"].toString() != "0" &&
                c["truncateSwing"].toString() != "1")
                return juce::Result::fail("Invalid Channel Swing truncation flag");
            p.channels.back().truncateSwing = bool(c["truncateSwing"]);
        }
        else if ((c.hasProperty("swingMix") && exactNumber(c["swingMix"]) != 1) ||
                 (c.hasProperty("truncateSwing") && c["truncateSwing"].toString() != "0"))
            return juce::Result::fail("Channel Swing requires project version 13");
        if (c.hasProperty("group"))
        {
            const auto group = exactNumber(c["group"]);
            if (!std::isfinite(group) || std::floor(group) != group || group < 0 || group > 1000000000 ||
                (fileVersion < 11 && group != 0))
                return juce::Result::fail("Invalid or unsupported Channel group membership");
            p.channels.back().group = Id(group);
        }
        if (c.hasProperty("colour"))
        {
            const auto colour = exactNumber(c["colour"]);
            if (!std::isfinite(colour) || std::floor(colour) != colour || colour < 0 ||
                colour > 4294967295.0 || !validChannelColour(juce::uint32(colour)) ||
                (fileVersion < 27 && colour != 0))
                return juce::Result::fail("Invalid or unsupported Channel colour");
            p.channels.back().colour = juce::uint32(colour);
        }
        if (c.hasProperty("icon"))
        {
            if (!c["icon"].isString() || !validChannelIcon(c["icon"].toString()) ||
                (fileVersion < 27 && c["icon"].toString() != "none"))
                return juce::Result::fail("Invalid or unsupported Channel icon");
            p.channels.back().icon = c["icon"].toString();
        }
        if (c.hasProperty("muteLocked"))
        {
            const auto locked = c["muteLocked"];
            if ((!locked.isBool() && locked.toString() != "0" && locked.toString() != "1") ||
                (fileVersion < 28 && bool(locked)))
                return juce::Result::fail("Invalid or unsupported Channel mute lock");
            p.channels.back().muteLocked = bool(locked);
        }
        if (c.hasProperty("pitchBend"))
        {
            const auto bend = exactNumber(c["pitchBend"]);
            if (!std::isfinite(bend) || std::abs(bend) > 1)
                return juce::Result::fail("Invalid Channel pitch bend");
            p.channels.back().pitchBend = float(bend);
        }
        if (c.hasProperty("pitchRange"))
        {
            const auto range = exactNumber(c["pitchRange"]);
            if (!std::isfinite(range) || range < 1 || range > 48 || std::floor(range) != range)
                return juce::Result::fail("Invalid Channel pitch range");
            p.channels.back().pitchRange = int(range);
        }
        if (c.hasProperty("keyLabels"))
        {
            for (const auto &entry : juce::StringArray::fromTokens(c["keyLabels"].toString(), ";", ""))
            {
                const int pitch = entry.upToFirstOccurrenceOf("=", false, false).getIntValue();
                const auto label = entry.fromFirstOccurrenceOf("=", false, false);
                if (!entry.containsChar('=') || pitch < 0 || pitch > 127 || label.isEmpty() || label.length() > 32)
                    return juce::Result::fail("Invalid Channel key label");
                p.channels.back().keyLabels[pitch] = label;
            }
        }
        if (c.hasProperty("receiveMidi"))
        {
            const double receive = exactNumber(c["receiveMidi"]);
            if (fileVersion < 57 || !std::isfinite(receive) || receive < 1 || receive > 16 ||
                std::floor(receive) != receive)
                return juce::Result::fail("Invalid Channel MIDI receive channel");
            p.channels.back().receiveMidiChannel = int(receive);
        }
        if (c.hasProperty("articulations"))
        {
            const auto entries = juce::StringArray::fromTokens(c["articulations"].toString(), ";", "");
            if (fileVersion < 57 || entries.size() > maximumArticulations)
                return juce::Result::fail("Invalid Channel expression map");
            for (const auto &entry : entries)
            {
                const auto trigger = entry.upToFirstOccurrenceOf("=", false, false).trim();
                const auto name = entry.fromFirstOccurrenceOf("=", false, false);
                auto parsed = parseArticulationTrigger(trigger);
                if (!entry.containsChar('=') || !parsed || name.isEmpty() || name.length() > 32 ||
                    (!trigger.startsWithIgnoreCase("cc") && !trigger.startsWithIgnoreCase("pc") &&
                     !trigger.containsOnly("0123456789")))
                    return juce::Result::fail("Invalid Channel expression map");
                parsed->name = name;
                p.channels.back().articulations.push_back(*parsed);
            }
        }
        auto sampler = c.getChildWithName("SAMPLER");
        int samplerChildren = 0;
        for (const auto &child : c)
            if (child.hasType("SAMPLER"))
            {
                ++samplerChildren;
                if (fileVersion < 37 && child.getNumChildren() != 0)
                    return juce::Result::fail(
                        "samplerModulation.invalidSchema: Sampler children require project37");
            }
        if (fileVersion >= 37 && samplerChildren != 1)
            return juce::Result::fail("samplerModulation.incomplete: Exactly one sampler required");
        if (int(t["version"]) >= 5)
        {
            if (!sampler.isValid())
                return juce::Result::fail("Missing sampler settings");
            for (auto name : {"root", "cents", "start", "end", "reverse", "envelope", "attack", "decay",
                              "sustain", "release"})
                if (!sampler.hasProperty(name))
                    return juce::Result::fail("Incomplete sampler settings");
            auto &settings = p.channels.back().sampler;
            auto root = exactNumber(sampler["root"]);
            if (!std::isfinite(root) || root < -1 || root > 127 || std::floor(root) != root)
                return juce::Result::fail("Invalid sampler root");
            settings.root = int(root);
            settings.cents = exactNumber(sampler["cents"]);
            settings.start = exactNumber(sampler["start"]);
            settings.end = exactNumber(sampler["end"]);
            settings.reverse = bool(sampler["reverse"]);
            if (fileVersion >= 15)
            {
                if (!sampler.hasProperty("normalize") ||
                    (!sampler["normalize"].isBool() && sampler["normalize"].toString() != "0" &&
                     sampler["normalize"].toString() != "1"))
                    return juce::Result::fail("Missing or invalid sampler Normalize flag");
                settings.normalize = bool(sampler["normalize"]);
            }
            else if (sampler.hasProperty("normalize"))
                return juce::Result::fail("Sampler Normalize requires project version15");
            int voiceTrees = 0;
            for (const auto &child : sampler)
                voiceTrees += child.hasType("VOICE");
            for (const auto *field : {"mono", "portamento", "fullPortamento", "slideTime", "gateBeats"})
                if (sampler.hasProperty(field))
                    return juce::Result::fail(
                        "samplerVoice.invalidSchema: Voice fields require their VOICE child");
            if (voiceTrees > 1 || (voiceTrees != 0 && fileVersion < 40))
                return juce::Result::fail("samplerVoice.invalidSchema: Duplicate/newer VOICE tree");
            if (voiceTrees != 0)
            {
                const auto voice = sampler.getChildWithName("VOICE");
                if (voice.getNumChildren() != 0 || voice.getNumProperties() != 5)
                    return juce::Result::fail("samplerVoice.invalidSchema: Exact five VOICE fields required");
                for (const auto *field : {"mono", "portamento", "fullPortamento", "slideTime", "gateBeats"})
                    if (!voice.hasProperty(field))
                        return juce::Result::fail("samplerVoice.invalidSchema: Missing VOICE field");
                const auto boolean = [](const juce::var &value, bool &out)
                {
                    if (value.isBool())
                    {
                        out = bool(value);
                        return true;
                    }
                    if (value.isString() && (value.toString() == "0" || value.toString() == "1"))
                    {
                        out = value.toString() == "1";
                        return true;
                    }
                    return false;
                };
                if (!boolean(voice["mono"], settings.mono) ||
                    !boolean(voice["portamento"], settings.portamento) ||
                    !boolean(voice["fullPortamento"], settings.fullPortamento))
                    return juce::Result::fail("samplerVoice.invalidValue: Exact boolean flags required");
                settings.slideTime = exactNumber(voice["slideTime"]);
                settings.gateBeats = exactNumber(voice["gateBeats"]);
                if (!std::isfinite(settings.slideTime) || settings.slideTime < 0 || settings.slideTime > 10 ||
                    !std::isfinite(settings.gateBeats) || settings.gateBeats < 0 || settings.gateBeats > 64)
                    return juce::Result::fail(
                        "samplerVoice.invalidValue: Slide seconds0..10 and Gate beats0..64 required");
            }
            if (fileVersion >= 37)
            {
                const auto modulation = sampler.getChildWithName("MODULATION");
                if (sampler.getNumChildren() != 1 + voiceTrees || !modulation.isValid())
                    return juce::Result::fail(
                        "samplerModulation.incomplete: Exactly one modulation tree required");
                const bool hasStored = modulation.hasProperty("stored");
                if (modulation.getNumProperties() != (hasStored ? 1 : 0))
                    return juce::Result::fail(
                        "samplerModulation.invalidValue: Unknown modulation attributes");
                bool stored = true; // Earlier development schema37 wrote five attribute-free rows.
                if (hasStored)
                {
                    const auto flag = modulation["stored"];
                    if (!flag.isBool() &&
                        !(flag.isString() && (flag.toString() == "0" || flag.toString() == "1")))
                        return juce::Result::fail(
                            "samplerModulation.invalidValue: Exact stored boolean required");
                    stored = flag.isBool() ? bool(flag) : flag.toString() == "1";
                }
                if (modulation.getNumChildren() != (stored ? int(samplerModulationTargets.size()) : 0))
                    return juce::Result::fail("samplerModulation.incomplete: Stored modulation requires five "
                                              "targets; defaults require none");
                std::array<bool, 5> seen{};
                for (const auto &row : modulation)
                {
                    const auto id = row["id"];
                    if (!row.hasType("TARGET") || row.getNumChildren() != 0 || !id.isString() ||
                        row.getNumProperties() != int(samplerModulationParameters.size()) + 1)
                        return juce::Result::fail(
                            "samplerModulation.invalidTarget: Exact target attributes required");
                    const auto index = samplerModulationTarget(id.toString().toStdString());
                    if (index < 0 || seen[size_t(index)])
                        return juce::Result::fail(
                            "samplerModulation.invalidTarget: Distinct known targets required");
                    seen[size_t(index)] = true;
                    auto &target = settings.modulation[size_t(index)];
                    for (const auto &parameter : samplerModulationParameters)
                    {
                        const juce::Identifier name(juce::String(parameter.id.data()));
                        if (!row.hasProperty(name))
                            return juce::Result::fail(
                                "samplerModulation.incomplete: Every target parameter required");
                        const auto field = row[name];
                        double value;
                        if (parameter.boolean)
                        {
                            if (!field.isBool() &&
                                !(field.isString() && (field.toString() == "0" || field.toString() == "1")))
                                return juce::Result::fail(
                                    "samplerModulation.invalidValue: Exact boolean required");
                            value = field.isBool() ? (bool(field) ? 1. : 0.)
                                                   : (field.toString() == "1" ? 1. : 0.);
                        }
                        else
                            value = exactNumber(field);
                        if (!setSamplerModulationValue(target, parameter, value))
                            return juce::Result::fail(
                                "samplerModulation.invalidValue: Finite ranged parameter required");
                    }
                    if (!validSamplerModulation(target, size_t(index)))
                        return juce::Result::fail(
                            "samplerModulation.panEnvelopeUnsupported: Pan supports LFO only");
                }
            }
            settings.envelope = bool(sampler["envelope"]);
            settings.attack = exactNumber(sampler["attack"]);
            settings.decay = exactNumber(sampler["decay"]);
            settings.sustain = exactNumber(sampler["sustain"]);
            settings.release = exactNumber(sampler["release"]);
            if (fileVersion >= 35)
            {
                for (const auto *field : {"filterType", "filterCutoff", "filterResonance"})
                    if (!sampler.hasProperty(field))
                        return juce::Result::fail(
                            "samplerFilter.incomplete: All three filter settings required");
                const double type = exactNumber(sampler["filterType"]);
                const double cutoff = exactNumber(sampler["filterCutoff"]);
                const double resonance = exactNumber(sampler["filterResonance"]);
                if (!std::isfinite(type) || type < 0 || type >= samplerFilterTypeCount ||
                    std::floor(type) != type || !std::isfinite(cutoff) || cutoff < 0 || cutoff > 1 ||
                    !std::isfinite(resonance) || resonance < 0 || resonance > 1)
                    return juce::Result::fail(
                        "samplerFilter.invalidValue: Type0..5 and normalized finite controls required");
                settings.filterType = int(type);
                settings.filterCutoff = cutoff;
                settings.filterResonance = resonance;
            }
            else if (sampler.hasProperty("filterType") || sampler.hasProperty("filterCutoff") ||
                     sampler.hasProperty("filterResonance"))
                return juce::Result::fail("samplerFilter.invalidSchema: Filter settings require project35");
            if (fileVersion >= 14)
            {
                for (const auto *field : {"cut", "cutBy", "maxPoly"})
                {
                    if (!sampler.hasProperty(field))
                        return juce::Result::fail("Incomplete sampler voice settings");
                    const double value = exactNumber(sampler[field]);
                    if (!std::isfinite(value) || value < 0 || value > 99 || std::floor(value) != value)
                        return juce::Result::fail("Sampler voice controls require integers0..99");
                }
                settings.cut = int(sampler["cut"]);
                settings.cutBy = int(sampler["cutBy"]);
                settings.maxPoly = int(sampler["maxPoly"]);
            }
            else if (sampler.hasProperty("cut") || sampler.hasProperty("cutBy") ||
                     sampler.hasProperty("maxPoly"))
                return juce::Result::fail("Sampler voice settings require project version14");
            if (int(t["version"]) >= 6)
            {
                if (!sampler.hasProperty("sourceTempo") || !sampler.hasProperty("fitTempo"))
                    return juce::Result::fail("Incomplete sampler tempo settings");
                settings.sourceTempo = exactNumber(sampler["sourceTempo"]);
                settings.fitTempo = bool(sampler["fitTempo"]);
            }
            else if (sampler.hasProperty("sourceTempo") || sampler.hasProperty("fitTempo"))
                return juce::Result::fail("Sampler tempo settings require project version 6");
            if (int(t["version"]) >= 7)
            {
                for (const auto *field : {"loop", "pingPong", "loopStart", "loopEnd"})
                    if (!sampler.hasProperty(field))
                        return juce::Result::fail("Incomplete sampler loop settings");
                settings.loop = bool(sampler["loop"]);
                settings.pingPong = bool(sampler["pingPong"]);
                settings.loopStart = exactNumber(sampler["loopStart"]);
                settings.loopEnd = exactNumber(sampler["loopEnd"]);
            }
            else if (sampler.hasProperty("loop") || sampler.hasProperty("pingPong") ||
                     sampler.hasProperty("loopStart") || sampler.hasProperty("loopEnd"))
                return juce::Result::fail("Sampler loop settings require project version 7");
            for (const auto *field : {"stretchPitch", "timeMul", "timeBeats"})
                if (sampler.hasProperty(field))
                {
                    if (fileVersion < 56)
                        return juce::Result::fail("samplerStretch.version: Time stretching needs project version 56");
                    const auto value = exactNumber(sampler[field]);
                    const juce::String name(field);
                    (name == "stretchPitch" ? settings.stretchPitch : name == "timeMul" ? settings.timeMul : settings.timeBeats) = value;
                }
            for (const auto *field : {"stretch", "hqInterpolation"})
                if (sampler.hasProperty(field))
                {
                    const auto flag = sampler[field];
                    if (fileVersion < 42 || !(flag.isBool() || flag.toString() == "1" || flag.toString() == "0"))
                        return juce::Result::fail("samplerQuality.invalidValue: Flag needs project42 boolean");
                    (juce::String(field) == "stretch" ? settings.stretch : settings.hqInterpolation) = bool(flag);
                }
            for (const auto *field : {"fadeIn", "fadeOut", "loopCrossfade"})
                if (sampler.hasProperty(field))
                {
                    const auto value = exactNumber(sampler[field]);
                    if (fileVersion < 42 || !std::isfinite(value) || value <= 0 || value > 10)
                        return juce::Result::fail("samplerFade.invalidValue: Fades need project42 and 0..10 s");
                    const juce::String name(field);
                    (name == "fadeIn" ? settings.fadeIn : name == "fadeOut" ? settings.fadeOut
                                                                            : settings.loopCrossfade) = value;
                }
        }
        else if (sampler.isValid())
            return juce::Result::fail("Sampler settings require project version 5");
        p.channels.back().audioClip = bool(c["audioClip"]);
        p.channels.back().audioDuration =
            c.hasProperty("audioDuration") ? exactNumber(c["audioDuration"]) : 0;
        if (c.hasProperty("mpe"))
        {
            if (int(t["version"]) < 56)
                return juce::Result::fail("MPE tuning needs project version 56");
            p.channels.back().mpe = bool(c["mpe"]);
        }
        if (c.hasProperty("mtsEsp"))
        {
            if (int(t["version"]) < 56)
                return juce::Result::fail("MTS-ESP tuning needs project version 56");
            p.channels.back().mtsEsp = bool(c["mtsEsp"]);
        }
        if (int(t["version"]) < 3 && p.channels.back().audioClip)
            return juce::Result::fail("Audio Clip Channels require project version 3");
        if (c.hasProperty("tuning"))
        {
            const auto cents = juce::StringArray::fromTokens(c["tuning"].toString(), ",", "");
            if (int(t["version"]) < 47 || cents.size() != 12)
                return juce::Result::fail("Channel tuning needs project version 47 and 12 values");
            for (int i = 0; i < 12; ++i)
                p.channels.back().tuning[size_t(i)] = cents[i].getDoubleValue();
        }
        if (c.hasProperty("scale"))
        {
            const auto cents = juce::StringArray::fromTokens(c["scale"].toString(), ",", "");
            if (int(t["version"]) < 53 || cents.isEmpty() || cents.size() > 128)
                return juce::Result::fail("Channel scales need project version 53 and 1..128 degrees");
            for (const auto &value : cents)
                p.channels.back().scale.push_back(value.getDoubleValue());
            p.channels.back().scaleRoot = int(c.getProperty("scaleRoot", 60));
        }
        int pluginChildren = 0;
        for (const auto &child : c)
            if (child.hasType("PLUGIN"))
                ++pluginChildren;
        if (pluginChildren > 1)
            return juce::Result::fail("Duplicated Channel plugin data");
        if (c.getChildWithName("PLUGIN").isValid())
        {
            PluginSpec plugin;
            if (auto parsed = parsePlugin(c.getChildWithName("PLUGIN"), plugin, int(fileVersion));
                parsed.failed())
                return parsed;
            p.channels.back().instrument = std::move(plugin);
        }
    }
    for (auto e : t.getChildWithName("PATTERNS"))
    {
        Pattern pat{juce::int64(e["id"]), e["name"].toString(), exactNumber(e["length"]), {}};
        pat.autoLength = false; // Legacy windows retain their exact audible extent.
        if (fileVersion >= 30)
        {
            const auto flag = e["autoLength"];
            const double numeratorValue = exactNumber(e["numerator"]);
            const double denominatorValue = exactNumber(e["denominator"]);
            if (!e.hasProperty("autoLength") ||
                (!flag.isBool() &&
                 (!flag.isString() || (flag.toString() != "0" && flag.toString() != "1"))) ||
                !e.hasProperty("numerator") || !e.hasProperty("denominator") ||
                !std::isfinite(numeratorValue) || numeratorValue < 0 || numeratorValue > 32 ||
                std::floor(numeratorValue) != numeratorValue || !std::isfinite(denominatorValue) ||
                denominatorValue < 0 || denominatorValue > 16 ||
                std::floor(denominatorValue) != denominatorValue)
                return juce::Result::fail("patternExtent.schema: Missing or invalid Pattern extent/meter");
            pat.autoLength = flag.isBool() ? bool(flag) : flag.toString() == "1";
            pat.numerator = int(numeratorValue);
            pat.denominator = int(denominatorValue);
        }
        else if (e.hasProperty("autoLength") || e.hasProperty("numerator") || e.hasProperty("denominator"))
            return juce::Result::fail(
                "patternExtent.schema: Pattern extent/meter requires project version30");
        if (e.hasProperty("launchQuantum"))
        {
            const double quantum = exactNumber(e["launchQuantum"]);
            if (fileVersion < 49 || !validLaunchQuantum(quantum))
                return juce::Result::fail("Invalid Pattern launch quantum");
            pat.launchQuantum = quantum;
        }
        if (e.hasProperty("followAction"))
        {
            const auto action = e["followAction"].toString();
            const double after = exactNumber(e["followAfter"]);
            if (fileVersion < 57 || !performance::validFollowAction(action) || !std::isfinite(after) || after < 1 ||
                after > 64 || std::floor(after) != after)
                return juce::Result::fail("Invalid Pattern Follow Action");
            pat.followAction = action;
            pat.followAfter = int(after);
        }
        pat.generation = e["generation"].toString();
        if (e.hasProperty("group") && fileVersion < 10)
            return juce::Result::fail("Pattern groups require project version 10");
        pat.group = e["group"].toString();
        if (e.hasProperty("colour"))
        {
            auto text = e["colour"].toString();
            if (text.length() != 8 || !text.containsOnly("0123456789abcdefABCDEF"))
                return juce::Result::fail("Invalid Pattern colour");
            pat.colour = juce::uint32(text.getHexValue32());
            if (pat.colour != 0 && (pat.colour >> 24) != 255)
                return juce::Result::fail("Pattern colour must be opaque");
        }
        for (auto n : e)
        {
            if (n.hasType("CHANNEL_LOOP"))
            {
                if (fileVersion < 29 || !n.hasProperty("channel") || !n.hasProperty("length") ||
                    !channelLoopMode(n["mode"].toString()) || n.getNumChildren() != 0 ||
                    n.getNumProperties() != 3)
                    return juce::Result::fail(
                        "channelLoop.schema: Invalid Channel loop or loop data under an old schema");
                const auto value = n["channel"];
                const auto text = value.toString().toStdString();
                Id channel = 0;
                const auto converted = std::from_chars(text.data(), text.data() + text.size(), channel);
                if ((!value.isInt() && !value.isInt64() && !value.isString()) ||
                    converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || channel < 1)
                    return juce::Result::fail("channelLoop.invalidTarget: Exact Channel ID required");
                pat.channelLoops.push_back(
                    {channel, *channelLoopMode(n["mode"].toString()), exactNumber(n["length"])});
                continue;
            }
            if (n.hasType("AUTOMATION"))
            {
                if (fileVersion < 56)
                    return juce::Result::fail("Pattern automation needs project version 56");
                AutomationPlacement window;
                window.source = juce::int64(n["source"]);
                window.beat = exactNumber(n["beat"]);
                window.length = exactNumber(n["length"]);
                window.offset = n.hasProperty("offset") ? exactNumber(n["offset"]) : 0.;
                window.muted = bool(n["muted"]);
                pat.automation.push_back(window);
                continue;
            }
            if (n.hasType("CONTROLLER"))
            {
                if (fileVersion < 46)
                    return juce::Result::fail("Recorded controllers need project version 46");
                pat.controllers.push_back({juce::int64(n["id"]), juce::int64(n["channel"]), exactNumber(n["beat"]),
                                           exactNumber(n["value"]), int(n["type"]), int(n["number"]),
                                           int(n["midiChannel"])});
                continue;
            }
            if (!n.hasType("NOTE"))
                return juce::Result::fail("Unknown Pattern child data");
            const auto flag = n["muted"];
            const bool validFlag =
                flag.isBool() || (flag.isString() && (flag.toString() == "0" || flag.toString() == "1"));
            if ((fileVersion >= 17 && (!n.hasProperty("muted") || !validFlag)) ||
                (fileVersion < 17 && n.hasProperty("muted")))
                return juce::Result::fail(
                    "Missing/invalid Note mute state or newer data under an old schema");
            const bool hasExpression = n.hasProperty("pan") || n.hasProperty("fineCents");
            if (fileVersion < 31 && hasExpression)
                return juce::Result::fail(
                    "noteExpression.invalidSchema: Note expression attributes require schema31");
            const double pan = n.hasProperty("pan") ? exactNumber(n["pan"]) : 0;
            const double fine = n.hasProperty("fineCents") ? exactNumber(n["fineCents"]) : 0;
            if (!std::isfinite(pan) || std::abs(pan) > 1 || !std::isfinite(fine) || std::abs(fine) > 100)
                return juce::Result::fail(
                    "noteExpression.invalidValue: Pan +/-1 and Fine Pitch +/-100 required");
            if (fileVersion < 32 && n.hasProperty("shiftPercent"))
                return juce::Result::fail("noteShift.invalidSchema: Shift presence requires schema32");
            const double shift = n.hasProperty("shiftPercent") ? exactNumber(n["shiftPercent"]) : 0;
            if (!std::isfinite(shift) || shift < 0 || shift > 100)
                return juce::Result::fail("noteShift.invalidValue: Finite Shift percent0..100 required");
            if (fileVersion < 33 && n.hasProperty("repeatMode"))
                return juce::Result::fail("noteRepeat.invalidSchema: Repeat presence requires schema33");
            const double repeat = n.hasProperty("repeatMode") ? exactNumber(n["repeatMode"]) : 0;
            if (!std::isfinite(repeat) || repeat < 0 || repeat > 14 || std::floor(repeat) != repeat)
                return juce::Result::fail("noteRepeat.invalidMode: Exact Repeat rate index0..14 required");
            if (fileVersion < 34 && n.hasProperty("releaseVelocity"))
                return juce::Result::fail("noteRelease.invalidSchema: Release velocity requires schema34");
            const double release = n.hasProperty("releaseVelocity") ? exactNumber(n["releaseVelocity"]) : 0;
            if (!std::isfinite(release) || release < 0 || release > 1)
                return juce::Result::fail("noteRelease.invalidValue: Finite release velocity0..1 required");
            if (fileVersion < 36 && (n.hasProperty("modX") || n.hasProperty("modY")))
                return juce::Result::fail("noteModulation.invalidSchema: Mod X/Y presence requires schema36");
            const double modX = n.hasProperty("modX") ? exactNumber(n["modX"]) : .5;
            const double modY = n.hasProperty("modY") ? exactNumber(n["modY"]) : .5;
            if (!std::isfinite(modX) || modX < 0 || modX > 1 || !std::isfinite(modY) || modY < 0 || modY > 1)
                return juce::Result::fail("noteModulation.invalidValue: Finite Mod X/Y0..1 required");
            const bool hasModes =
                n.hasProperty("slide") || n.hasProperty("portamento") || n.hasProperty("colourGroup");
            if (fileVersion < 41 && hasModes)
                return juce::Result::fail("noteMode.invalidSchema: Note modes require schema41");
            const auto modeFlag = [](const juce::var &value)
            {
                return value.isBool() ||
                       (value.isString() && (value.toString() == "0" || value.toString() == "1"));
            };
            for (const auto *field : {"slide", "portamento"})
                if (n.hasProperty(field) && !modeFlag(n[field]))
                    return juce::Result::fail("noteMode.invalidValue: Exact boolean Note mode required");
            const double group = n.hasProperty("colourGroup") ? exactNumber(n["colourGroup"]) : 0;
            if (!std::isfinite(group) || group < 0 || group > 15 || std::floor(group) != group)
                return juce::Result::fail("noteMode.invalidValue: Exact colour group0..15 required");
            const bool slide = n.hasProperty("slide") && bool(n["slide"]);
            const bool portamento = n.hasProperty("portamento") && bool(n["portamento"]);
            if (slide && repeat != 0)
                return juce::Result::fail("noteMode.incompatibleRepeat: Slide notes cannot Repeat");
            pat.notes.push_back({juce::int64(n["id"]), juce::int64(n["channel"]), exactNumber(n["beat"]),
                                 exactNumber(n["length"]), int(n["pitch"]), float(exactNumber(n["velocity"])),
                                 bool(n["step"]), bool(n["muted"]), pan, fine, shift, int(repeat), release,
                                 modX, modY, slide, portamento, int(group)});
            if (n.hasProperty("group"))
            {
                const double noteGroup = exactNumber(n["group"]);
                if (!std::isfinite(noteGroup) || noteGroup < 1 || noteGroup > 9.e15 ||
                    std::floor(noteGroup) != noteGroup)
                    return juce::Result::fail("noteGroup.invalidValue: Exact positive note group ID required");
                pat.notes.back().group = Id(juce::int64(noteGroup));
            }
            if (n.hasProperty("articulation"))
            {
                const double articulation = exactNumber(n["articulation"]);
                if (fileVersion < 57 || !std::isfinite(articulation) || articulation < 0 ||
                    articulation > maximumArticulations || std::floor(articulation) != articulation)
                    return juce::Result::fail("Invalid note articulation");
                pat.notes.back().articulation = int(articulation);
            }
        }
        if (fileVersion >= 30)
            refreshAutomaticPatternLength(p, pat);
        p.patterns.push_back(std::move(pat));
    }
    for (auto e : t.getChildWithName("PLACEMENTS"))
    {
        if ((int(t["version"]) >= 4 && !e.hasProperty("muted")) ||
            (int(t["version"]) < 4 && bool(e["muted"])))
            return juce::Result::fail("Clip mute requires project version 4");
        p.placements.push_back({juce::int64(e["id"]), juce::int64(e["pattern"]), int(e["lane"]),
                                exactNumber(e["beat"]), exactNumber(e["length"]), exactNumber(e["offset"]),
                                bool(e["muted"])});
    }
    for (auto e : t.getChildWithName("MIXER"))
    {
        const double index=exactNumber(e["index"]);
        if(!e.hasType("INSERT") || !e.hasProperty("index") || !std::isfinite(index) ||
           index<0 || index>std::numeric_limits<int>::max() || std::floor(index)!=index)
            return juce::Result::fail("Mixer insert index must be an exact nonnegative integer");
        p.mixer.push_back({int(e["index"]), e["name"].toString(), float(exactNumber(e["gain"])),
                           float(exactNumber(e["pan"])), bool(e["muted"])});
        if (e.hasProperty("dock"))
        {
            const auto dock = exactNumber(e["dock"]);
            if (dock != -1 && dock != 0 && dock != 1)
                return juce::Result::fail("Invalid Mixer docking");
            p.mixer.back().dock = int(dock);
        }
        if (e.hasProperty("delayMs"))
        {
            const auto ms = exactNumber(e["delayMs"]);
            if (!std::isfinite(ms) || ms < 0 || ms > 500)
                return juce::Result::fail("Invalid Mixer track delay");
            p.mixer.back().delayMs = float(ms);
        }
        if (e.hasProperty("eq"))
        {
            // Six numbers: gain dB and frequency Hz for the low, mid and high bands.
            auto values = juce::StringArray::fromTokens(e["eq"].toString(), ",", "");
            if (values.size() != 6)
                return juce::Result::fail("Invalid Mixer EQ encoding");
            for (int band = 0; band < 3; ++band)
            {
                const auto gainDb = values[band * 2].getFloatValue(), hz = values[band * 2 + 1].getFloatValue();
                if (!MixerEqSettings::valid(gainDb, hz))
                    return juce::Result::fail("Invalid Mixer EQ band");
                p.mixer.back().eq.gain[size_t(band)] = gainDb;
                p.mixer.back().eq.freq[size_t(band)] = hz;
            }
        }
        if (e.hasProperty("stereoSeparation"))
        {
            const auto amount = exactNumber(e["stereoSeparation"]);
            if (fileVersion < 21 || !std::isfinite(amount) || amount < -1 || amount > 1)
                return juce::Result::fail("Invalid Mixer stereo separation encoding or old schema");
            p.mixer.back().stereoSeparation = float(amount);
        }
        for (const auto *key : {"phaseInverted", "stereoSwapped"})
            if (e.hasProperty(key))
            {
                const auto flag = e[key];
                if (fileVersion < 21 || (!flag.isBool() && (!flag.isString() ||
                        (flag.toString() != "0" && flag.toString() != "1"))))
                    return juce::Result::fail("Invalid Mixer stereo switch encoding or old schema");
                (juce::String(key) == "phaseInverted" ? p.mixer.back().phaseInverted
                                                       : p.mixer.back().stereoSwapped) =
                    flag.isBool() ? bool(flag) : flag.toString() == "1";
            }
        if (e.hasProperty("effectsBypassed"))
        {
            const auto value = e["effectsBypassed"];
            if (fileVersion < 20 || (!value.isBool() && (!value.isString() || (value.toString() != "0" &&
                                                                               value.toString() != "1"))))
                return juce::Result::fail("Invalid chain bypass encoding or newer data under an old schema");
            p.mixer.back().effectsBypassed = value.isBool() ? bool(value) : value.toString() == "1";
        }
        if (e.hasProperty("colour"))
        {
            const auto colour = exactNumber(e["colour"]);
            if (fileVersion < 19 || !std::isfinite(colour) || colour != std::floor(colour) || colour < 0 ||
                colour > 4294967295. || (colour != 0 && colour < 4278190080.))
                return juce::Result::fail("Invalid Mixer colour encoding");
            p.mixer.back().colour = juce::uint32(colour);
        }
        int routeLists=0;
        for (auto child : e)
            if (child.hasType("ROUTES"))
            {
                if (++routeLists != 1 || int(fileVersion)<18)
                    return juce::Result::fail("Mixer routing requires project version18 and one route list");
                auto &routes=p.mixer.back().routes;routes.clear();
                for (auto send:child)
                {
                    const double target=exactNumber(send["target"]), level=exactNumber(send["gain"]);
                    if (!send.hasType("SEND") || !send.hasProperty("target") || !send.hasProperty("gain") ||
                        !std::isfinite(target) || target<0 || target>std::numeric_limits<int>::max() || target!=std::floor(target) || !std::isfinite(level))
                        return juce::Result::fail("Invalid Mixer send encoding");
                    routes.push_back({int(target),float(level)});
                }
            }
        for (auto child : e)
            if (child.hasType("PLUGIN"))
            {
                PluginSpec plugin;
                if (auto parsed = parsePlugin(child, plugin, int(fileVersion)); parsed.failed())
                    return parsed;
                p.mixer.back().effects.push_back(std::move(plugin));
            }
    }
    for (auto e : t.getChildWithName("AUDIO_PLACEMENTS"))
    {
        if ((int(t["version"]) >= 4 && !e.hasProperty("muted")) ||
            (int(t["version"]) < 4 && bool(e["muted"])))
            return juce::Result::fail("Clip mute requires project version 4");
        p.audioPlacements.push_back({juce::int64(e["id"]), juce::int64(e["channel"]), int(e["lane"]),
                                     exactNumber(e["beat"]), exactNumber(e["offset"]),
                                     exactNumber(e["duration"]), bool(e["muted"])});
        // Clip fades are project version 43; older files never carry them.
        if ((e.hasProperty("fadeIn") || e.hasProperty("fadeOut")) && int(t["version"]) < 43)
            return juce::Result::fail("Audio Clip fades require project version 43");
        p.audioPlacements.back().fadeIn = e.hasProperty("fadeIn") ? exactNumber(e["fadeIn"]) : 0.;
        p.audioPlacements.back().fadeOut = e.hasProperty("fadeOut") ? exactNumber(e["fadeOut"]) : 0.;
        if (e.hasProperty("warpBpm"))
        {
            if (int(t["version"]) < 56)
                return juce::Result::fail("Warped Audio Clips require project version 56");
            auto &clip = p.audioPlacements.back();
            clip.warpBpm = exactNumber(e["warpBpm"]);
            clip.warpChannel = juce::int64(e["warpChannel"]);
            clip.warpOffset = exactNumber(e["warpOffset"]);
            clip.warpDuration = exactNumber(e["warpDuration"]);
        }
    }
    int mixerGroupTrees = 0;
    for (const auto &tree : t)
        if (tree.hasType("MIXER_GROUPS"))
        {
            if (++mixerGroupTrees != 1 || fileVersion < 23 || tree.getNumProperties() != 0 || tree.getNumChildren() > 128)
                return juce::Result::fail("mixer.group.invalidEncoding: Duplicate/newer/oversized Mixer group tree");
            for (const auto &row : tree)
            {
                const auto colour = exactNumber(row["colour"]);
                if (!row.hasType("GROUP") || row.getNumProperties() != 3 || !row.hasProperty("id") ||
                    !row.hasProperty("name") || !row.hasProperty("colour") || !row["id"].isString() || !row["name"].isString() ||
                    !validMixerGroupId(row["id"].toString()) || !validMixerColour(colour) || row.getNumChildren() > 128)
                    return juce::Result::fail("mixer.group.invalidEncoding: Invalid group identity/name/colour/member limit");
                MixerGroup group{row["id"].toString(), row["name"].toString(), juce::uint32(colour), {}};
                for (const auto &member : row)
                {
                    const auto id = exactNumber(member["insert"]);
                    if (!member.hasType("MEMBER") || member.getNumProperties() != 1 ||
                        member.getNumChildren() != 0 || !std::isfinite(id) || id < 1 ||
                        id > std::numeric_limits<int>::max() || id != std::floor(id))
                        return juce::Result::fail("mixer.group.invalidEncoding: Invalid non-Master member");
                    group.members.push_back(int(id));
                }
                p.mixerGroups.push_back(std::move(group));
            }
        }
    int mixerSoloTrees = 0;
    for (const auto &saved : t)
        if (saved.hasType("MIXER_SOLO"))
        {
            const auto index = exactNumber(saved["insert"]);
            if (++mixerSoloTrees != 1 || fileVersion < 19 || !saved.hasProperty("insert") ||
                !std::isfinite(index) || index < 1 || index > std::numeric_limits<int>::max() ||
                index != std::floor(index) ||
                (saved["connected"].toString() != "0" && saved["connected"].toString() != "1") ||
                saved.getNumChildren() > 129)
                return juce::Result::fail("Invalid Mixer solo encoding");
            p.mixerSolo = MixerSoloState{int(index), saved["connected"].toString() == "1", {}};
            if (saved.hasProperty("group"))
            {
                if (fileVersion < 23 || !saved["group"].isString() || !validMixerGroupId(saved["group"].toString()))
                    return juce::Result::fail("mixer.group.invalidSolo: Invalid/newer group solo identity");
                p.mixerSolo->group = saved["group"].toString();
            }
            for (const auto &item : saved)
            {
                const auto mutedIndex = exactNumber(item["insert"]);
                if (!item.hasType("MUTED") || !item.hasProperty("insert") || !std::isfinite(mutedIndex) ||
                    mutedIndex < 0 || mutedIndex > std::numeric_limits<int>::max() ||
                    mutedIndex != std::floor(mutedIndex))
                    return juce::Result::fail("Invalid Mixer solo baseline");
                p.mixerSolo->mutedBefore.push_back(int(mutedIndex));
            }
        }
    if (fileVersion < 28 && p.solo)
        for (const auto &channel : p.channels)
            p.solo->affected.push_back(channel.id); // Genuine pre-lock global-solo migration.
    auto valid = p.validate();
    if (valid.failed())
        return valid;
    // Extend the old default rack in memory; never rewrite the user's file on load.
    if (fileVersion < 23 && p.mixer.size() == 9 &&
        std::all_of(p.mixer.begin(), p.mixer.end(),
                    [index = 0](const auto &insert) mutable { return insert.index == index++; }))
        for (int index = 9; index < 18; ++index)
            p.mixer.push_back({index, "Insert " + juce::String(index), 1.f, 0.f, false});
    reconcileMixerSolo(p); // Newly extended legacy inserts obey an active mask.
    result = std::move(p);
    return juce::Result::ok();
}
// CONTROL-only: count the prepared source plan without allocating its raw or
// repeated occurrences. Neutral legacy projects retain their original limits.
static bool hasNoteRepeat(const Project &project)
{
    for (const auto &pattern : project.patterns)
        for (const auto &note : pattern.notes)
            if (note.repeatMode != 0)
                return true;
    return false;
}
static juce::Result validateNoteRepeatBudget(const Project &project)
{
    if (!hasNoteRepeat(project))
        return juce::Result::ok();
    constexpr std::uint64_t capacity = 100000;
    // One validation invocation only; immutable mathematical results, not runtime
    // state or filesystem observations. Repeated linked windows are counted once.
    std::map<std::tuple<Id, double, double, double>, std::uint64_t> windowCounts;
    bool unrepresentableGate = false;
    const auto countWindow = [&](const Pattern &pattern, double offset, double length, double start,
                                 std::uint64_t &total) -> bool
    {
        total = 0;
        if (offset >= pattern.length || length <= 0)
            return true;
        const double windowEnd = std::min(pattern.length, offset + length);
        if (!std::isfinite(offset) || !std::isfinite(windowEnd) || offset < 0)
            return false;
        const auto key = std::tuple{pattern.id, offset, windowEnd, start};
        if (const auto cached = windowCounts.find(key); cached != windowCounts.end())
        {
            total = cached->second;
            return true;
        }
        std::map<Id, double> periods;
        for (const auto &loop : pattern.channelLoops)
        {
            const auto period = channelLoopLength(project, pattern, loop.channel);
            if (!std::isfinite(period) || period <= 0)
                return false;
            periods.emplace(loop.channel, period);
        }
        for (const auto &note : pattern.notes)
        {
            const auto loop = periods.find(note.channel);
            const auto period = loop == periods.end() ? 0. : loop->second;
            std::uint64_t first = 0, end = 1;
            if (period != 0)
            {
                if (note.beat >= period || note.beat >= pattern.length)
                    continue;
                const auto count = std::ceil((pattern.length - note.beat) / period);
                if (!std::isfinite(count) || count < 0 || count > capacity)
                    return false;
                end = std::uint64_t(count);
                // Skip raw loop origins that cannot intersect this window, even
                // with maximum Swing/Shift and a long source gate. No modulo.
                const auto upper = std::ceil((windowEnd - note.beat) / period);
                if (upper <= 0)
                    continue;
                end = std::min(end, std::uint64_t(std::min(double(capacity), upper)));
                const auto lower =
                    std::ceil((offset - note.beat - std::max(note.length, .000001) - .125 - .2475) / period);
                if (lower > 0)
                    first = std::uint64_t(std::min(double(capacity + 1), lower));
            }
            const auto *channel = project.channel(note.channel);
            for (auto iteration = first; iteration < end; ++iteration)
            {
                const double rawBeat = note.beat + double(iteration) * period;
                const double swing = channel ? swingDelay(rawBeat, project.swing, *channel) : 0;
                const double origin = rawBeat + swing + noteShiftDelay(note.shiftPercent);
                const double duration =
                    channel && channel->truncateSwing ? std::max(.000001, note.length - swing) : note.length;
                NoteRepeatWindow window;
                if (!noteRepeatVisibleWindow(note.repeatMode, origin, duration, offset, windowEnd, window) ||
                    window.count > capacity - total)
                    return false;
                if (note.repeatMode != 0 && window.count > 0)
                {
                    // The last visible gate is the shortest subdivision. Reject a
                    // positive residual that would round to off-before-its-own-on
                    // in the exact scheduled beat/seconds representation.
                    const auto ordinal = window.first + window.count - 1;
                    const double local = origin + noteRepeatOffset(note.repeatMode, ordinal) - offset;
                    const double on = start + local;
                    const double gate = std::min({noteRepeatGateDuration(note.repeatMode, duration, ordinal),
                                                  length - local, pattern.length - offset - local});
                    const double off = on + gate;
                    if (!(gate > 0 && off > on && off * 60 / project.tempo > on * 60 / project.tempo))
                    {
                        unrepresentableGate = true;
                        return false;
                    }
                }
                total += window.count;
            }
        }
        windowCounts.emplace(key, total);
        return true;
    };
    for (const auto &pattern : project.patterns)
    {
        std::uint64_t count = 0;
        if (!countWindow(pattern, 0, pattern.length, 0, count))
        {
            if (unrepresentableGate)
                return juce::Result::fail(
                    "noteRepeat.precision: Repeat gate is too short at this timeline position");
            return juce::Result::fail(
                "noteRepeat.capacity: Expanded Pattern exceeds100000 events or ordinal range");
        }
    }
    std::uint64_t songCount = 0;
    for (const auto &clip : project.placements)
        if (const auto *pattern = project.pattern(clip.pattern))
        {
            std::uint64_t count = 0;
            if (!countWindow(*pattern, clip.offset, clip.length, clip.beat, count) ||
                count > capacity - songCount)
            {
                if (unrepresentableGate)
                    return juce::Result::fail(
                        "noteRepeat.precision: Repeat gate is too short at this timeline position");
                return juce::Result::fail(
                    "noteRepeat.capacity: Expanded Song exceeds100000 events or ordinal range");
            }
            songCount += count;
        }
    return juce::Result::ok();
}
juce::Result Project::validate() const
{
    auto fail = [](const char *s) { return juce::Result::fail(s); };
    if (version != currentVersion)
        return fail("Unsupported in-memory project version");
    if (!std::isfinite(swing) || swing < 0 || swing > 1)
        return fail("Global Swing must be normalized 0-1");
    if (!std::isfinite(tempo) || tempo < 20 || tempo > 400 || numerator < 1 || numerator > 32 ||
        (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16))
        return fail("Invalid tempo or meter");
    if (channels.size() > size_t(maxChannels) || channelGroups.size() > 256 || patterns.empty() || patterns.size() > 1024 ||
        placements.size() + audioPlacements.size() + automation.placements.size() > 100000 || mixer.empty() ||
        mixer.size() > 129)
        return fail("Project limits exceeded or missing required data");
    std::set<Id> ids;
    Id maxId = 0;
    auto unique = [&](Id id)
    {
        maxId = std::max(maxId, id);
        return id > 0 && ids.insert(id).second;
    };
    if (arrangements.size() > 256 || arrangementName.length() > 128 ||
        (!arrangements.empty() && arrangementId == 0))
        return fail("Invalid arrangement list");
    for (const auto &a : arrangements)
    {
        if (!unique(a.id) || a.name.length() > 128)
            return fail("Invalid arrangement");
        for (const auto &c : a.placements)
            if (!unique(c.id) || !pattern(c.pattern) || c.lane < 0 || c.lane > 127 || !std::isfinite(c.beat) ||
                c.beat < 0 || !(c.length > 0) || c.offset < 0)
                return fail("Invalid stored arrangement clip");
        for (const auto &c : a.audioPlacements)
            if (!unique(c.id) || !channel(c.channel) || !channel(c.channel)->audioClip || c.lane < 0 || c.lane > 127 ||
                !std::isfinite(c.beat) || c.beat < 0 || !(c.duration > 0) || c.offset < 0)
                return fail("Invalid stored arrangement audio clip");
        for (const auto &c : a.automationPlacements)
            if (!unique(c.id) || !automationSource(c.source) || c.lane < 0 || !std::isfinite(c.beat) || c.beat < 0)
                return fail("Invalid stored arrangement automation clip");
        for (const auto &g : a.clipGroups)
            if (!unique(g.id))
                return fail("Invalid stored arrangement group");
        for (const auto &m : a.markers)
            if (!unique(m.id) || !std::isfinite(m.beat) || m.beat < 0 || m.name.length() > 128)
                return fail("Invalid stored arrangement marker");
    }
    if (arrangementId != 0 && !unique(arrangementId))
        return fail("Arrangement identity collides");
    for (size_t i = 0; i < tempoChanges.size(); ++i)
    {
        const auto &change = tempoChanges[i];
        if (!unique(change.id) || !std::isfinite(change.beat) || change.beat <= 0 || change.beat > 1000000 ||
            !std::isfinite(change.bpm) || change.bpm < 10 || change.bpm > 999 ||
            (i > 0 && change.beat <= tempoChanges[i - 1].beat))
            return fail("Invalid tempo change (beats must increase, BPM 10..999)");
    }
    {
        Project before; // Each time-signature change sits on a bar line of the meters before it.
        before.numerator = numerator;
        before.denominator = denominator;
        for (size_t i = 0; i < meterChanges.size(); ++i)
        {
            const auto &change = meterChanges[i];
            if (!unique(change.id) || !std::isfinite(change.beat) || change.beat <= 0 || change.beat > 1000000 ||
                change.numerator < 1 || change.numerator > 32 ||
                (change.denominator != 2 && change.denominator != 4 && change.denominator != 8 &&
                 change.denominator != 16) ||
                (i > 0 && change.beat <= meterChanges[i - 1].beat) || before.barPosition(change.beat).beatInBar > 1.e-6)
                return fail("Invalid time-signature change (rising beats on bar lines, 1..32 over 2/4/8/16)");
            before.meterChanges.push_back(change);
        }
    }
    for (const auto &marker : markers)
        if (!unique(marker.id) || !std::isfinite(marker.beat) || marker.beat < 0 || marker.beat > 1000000 ||
            marker.name.length() > 128)
            return fail("Invalid Playlist marker");
    for (size_t i = 0; i < chords.size(); ++i)
        if (!unique(chords[i].id) || !std::isfinite(chords[i].beat) || chords[i].beat < 0 || chords[i].beat > 1000000 ||
            chords[i].symbol.length() > 32 || score::chordPitchClasses(chords[i].symbol).empty() ||
            (i > 0 && chords[i].beat <= chords[i - 1].beat))
            return fail("Invalid Chord Track event (known symbol, strictly rising beats)");
    std::set<int> inserts;
    juce::StringArray groupNames;
    for (const auto &group : channelGroups)
    {
        if (!unique(group.id) || group.name.trim().isEmpty() || group.name.length() > 128 ||
            group.name != group.name.trim() || group.name.containsAnyOf("\r\n\t") ||
            group.name.equalsIgnoreCase("All") || group.name.equalsIgnoreCase("Unsorted") ||
            groupNames.contains(group.name, true))
            return fail("Invalid, reserved or duplicate Channel group name/ID");
        groupNames.add(group.name);
    }
    for (auto &m : mixer)
        if (m.index < 0 || !inserts.insert(m.index).second || !std::isfinite(m.gain) || m.gain < 0 ||
            m.gain > 2 || !std::isfinite(m.pan) || std::abs(m.pan) > 1 ||
            !std::isfinite(m.stereoSeparation) || std::abs(m.stereoSeparation) > 1 ||
            !MixerEqSettings::valid(m.eq.gain[0], m.eq.freq[0]) || !MixerEqSettings::valid(m.eq.gain[1], m.eq.freq[1]) ||
            !MixerEqSettings::valid(m.eq.gain[2], m.eq.freq[2]) || !std::isfinite(m.delayMs) || m.delayMs < 0 ||
            m.delayMs > 500 || m.dock < -1 || m.dock > 1 ||
            (m.colour != 0 && (m.colour >> 24) != 255))
            return fail("Invalid mixer state");
    if (!inserts.count(0))
        return fail("Missing Master insert");
    if (mixerGroups.size() > 128) return fail("mixer.group.limit: At most128 display groups");
    std::set<juce::String> groupIds;
    std::set<int> groupedInserts;
    juce::StringArray mixerGroupNames;
    for (const auto &group : mixerGroups)
    {
        if (!validMixerGroupId(group.id) || !groupIds.insert(group.id).second ||
            group.name.isEmpty() || group.name != group.name.trim() || group.name.length() > 64 ||
            group.name.containsAnyOf("\r\n\t") || mixerGroupNames.contains(group.name, true) ||
            (group.colour != 0 && (group.colour >> 24) != 255) || group.members.size() > 128)
            return fail("mixer.group.invalid: Invalid/duplicate group identity/name/colour");
        mixerGroupNames.add(group.name);
        for (int member : group.members)
            if (member == 0 || !inserts.count(member) || !groupedInserts.insert(member).second)
                return fail("mixer.group.invalidMembers: Missing/Master/duplicate display member");
    }
    if (mixerSolo)
    {
        if (mixerSolo->insert == 0 || !inserts.count(mixerSolo->insert))
            return fail("Unavailable Mixer solo target");
        std::set<int> baseline;
        for (auto index : mixerSolo->mutedBefore)
            if (!inserts.count(index) || !baseline.insert(index).second)
                return fail("Invalid Mixer solo baseline");
        if (mixerSolo->group.isNotEmpty())
        {
            const auto *group = mixerGroup(*this, mixerSolo->group);
            if (!group || group->members.empty() ||
                std::find(group->members.begin(), group->members.end(), mixerSolo->insert) == group->members.end())
                return fail("mixer.group.invalidSolo: Unavailable group or anchor");
        }
        auto mask = mixerSoloMask(*this, mixerSolo->insert, mixerSolo->connected, mixerSolo->group);
        for (const auto &m : mixer)
            if (m.muted == bool(mask.count(m.index)))
                return fail("Inconsistent Mixer solo mask");
    }
    for (auto &c : channels)
    {
        if (!std::isfinite(c.swingMix) || c.swingMix < 0 || c.swingMix > 1)
            return fail("Channel Swing Mix must be normalized 0-1");
        if (!validChannelColour(c.colour) || !validChannelIcon(c.icon))
            return fail("Invalid Channel appearance");
        if (!unique(c.id) || !inserts.count(c.mixer) || !std::isfinite(c.gain) || c.gain < 0 || c.gain > 2 ||
            !std::isfinite(c.pan) || std::abs(c.pan) > 1 || c.source.isEmpty() || !std::isfinite(c.pitchBend) ||
            std::abs(c.pitchBend) > 1 || c.pitchRange < 1 || c.pitchRange > 48)
            return fail("Invalid Channel state");
        if (!c.sampler.valid())
            return fail("Invalid sampler settings");
        if (c.group < 0 || (c.group != 0 && !channelGroup(c.group)))
            return fail("Channel references an unavailable display group");
        for (auto cents : c.tuning)
            if (!std::isfinite(cents) || std::abs(cents) > 100)
                return fail("Channel tuning is +/-100 cents per pitch class");
        if (c.scale.size() > 128 || c.scaleRoot < 0 || c.scaleRoot > 127)
            return fail("Channel scale has 1..128 degrees and a root key 0..127");
        for (size_t i = 0; i < c.scale.size(); ++i)
            if (!std::isfinite(c.scale[i]) || c.scale[i] <= 0 || c.scale[i] > 4800 ||
                (i > 0 && c.scale[i] <= c.scale[i - 1]))
                return fail("Channel scale degrees must rise above 0 (at most 4800 cents)");
        if (!std::isfinite(c.audioDuration) ||
            (c.audioClip && (c.instrument || c.source.startsWith("builtin:") || c.audioDuration <= 0)) ||
            (!c.audioClip && c.audioDuration != 0))
            return fail("Invalid Audio Clip Channel");
        if (c.source.startsWith("builtin:") && c.source != "builtin:kick" && c.source != "builtin:snare" &&
            c.source != "builtin:hat" && c.source != "builtin:bass")
            return fail("Unknown built-in sample");
    }
    if (solo)
    {
        const auto *target = channel(solo->channel);
        if (!target || target->muteLocked || solo->mutedBefore.size() > channels.size() ||
            solo->affected.empty() || solo->affected.size() > channels.size() ||
            (!solo->groupScoped && solo->group != 0) ||
            (solo->groupScoped &&
             (target->group != solo->group || (solo->group != 0 && !channelGroup(solo->group)))))
            return fail("Unavailable solo target or invalid baseline size");
        std::set<Id> baseline;
        for (auto id : solo->mutedBefore)
            if (!channel(id) || !baseline.insert(id).second)
                return fail("Unavailable or duplicate solo baseline Channel");
        std::set<Id> affected;
        for (auto id : solo->affected)
            if (!channel(id) || !affected.insert(id).second)
                return fail("Unavailable or duplicate affected solo Channel");
        for (const auto &c : channels)
        {
            const bool eligible = !c.muteLocked && (!solo->groupScoped || c.group == solo->group);
            if (eligible != affected.contains(c.id) || (eligible && c.muted != (c.id != solo->channel)))
                return fail("Solo state does not match Channel mute mask");
        }
    }
    for (auto &p : patterns)
    {
        if (!unique(p.id) || !std::isfinite(p.length) || p.length <= 0 || p.length > 1000000 ||
            p.notes.size() > 100000 || p.generation.length() > 65536 || p.group.length() > 128 ||
            p.group != p.group.trim() || p.group.containsChar('\n') || p.group.containsChar('\r') ||
            (p.colour != 0 && (p.colour >> 24) != 255))
            return fail("Invalid Pattern");
        if ((p.numerator == 0) != (p.denominator == 0) || p.numerator < 0 || p.numerator > 32 ||
            (p.denominator != 0 && p.denominator != 2 && p.denominator != 4 && p.denominator != 8 &&
             p.denominator != 16))
            return fail(
                "patternExtent.invalidMeter: Inherit both meter fields or use a supported time signature");
        for (auto &n : p.notes)
            if ((maxId = std::max(maxId, n.group), !unique(n.id)) || !channel(n.channel) ||
                channel(n.channel)->audioClip ||
                !std::isfinite(n.beat) || n.beat < 0 || !std::isfinite(n.length) || n.length <= 0 ||
                n.pitch < 0 || n.pitch > 127 || !std::isfinite(n.velocity) || n.velocity < 0 ||
                n.velocity > 1 || !std::isfinite(n.pan) || std::abs(n.pan) > 1 ||
                !std::isfinite(n.fineCents) || std::abs(n.fineCents) > 100 ||
                !std::isfinite(n.shiftPercent) || n.shiftPercent < 0 || n.shiftPercent > 100 ||
                !noteRepeatRate(n.repeatMode) || !std::isfinite(n.releaseVelocity) || n.releaseVelocity < 0 ||
                n.releaseVelocity > 1 || !std::isfinite(n.modX) || n.modX < 0 || n.modX > 1 ||
                !std::isfinite(n.modY) || n.modY < 0 || n.modY > 1 || n.colourGroup < 0 ||
                n.colourGroup > 15 || (n.slide && n.repeatMode != 0))
                return fail("Invalid note");
        if (p.automation.size() > 4096)
            return fail("Too much Pattern automation");
        for (const auto &w : p.automation)
        {
            const auto source = std::find_if(automation.sources.begin(), automation.sources.end(),
                                             [&](const auto &s) { return s.id == w.source; });
            if (source == automation.sources.end() || !std::isfinite(w.beat) || w.beat < 0 ||
                !std::isfinite(w.length) || w.length <= 0 || !std::isfinite(w.offset) || w.offset < 0 ||
                w.offset + w.length > source->length + 1.e-9 || w.beat + w.length > 1.e6)
                return fail("Invalid Pattern automation window");
        }
        if (p.controllers.size() > 100000)
            return fail("Too many controller events");
        for (const auto &c : p.controllers)
            if (!unique(c.id) || !channel(c.channel) || channel(c.channel)->audioClip || !std::isfinite(c.beat) ||
                c.beat < 0 || c.beat > 1000000 || c.type < 0 || c.type > 3 || c.number < 0 || c.number > 127 ||
                c.midiChannel < 0 || c.midiChannel > 15 || !std::isfinite(c.value) ||
                c.value < (c.type == 1 ? -1. : 0.) || c.value > 1)
                return fail("Invalid controller event");
        if (auto loops = validateChannelLoops(*this, p); loops.failed())
            return loops;
    }
    for (auto &p : placements)
    {
        if (!unique(p.id) || !pattern(p.pattern) || p.lane < 0 || p.lane > 127 || !std::isfinite(p.beat) ||
            p.beat < 0 || !std::isfinite(p.length) || p.length <= 0 || p.length > 4096 ||
            !std::isfinite(p.offset) || p.offset < 0 || p.beat + p.length > 1000000)
            return fail("Invalid placement");
        if (!pattern(p.pattern)->channelLoops.empty() &&
            p.id > (std::numeric_limits<Id>::max() - 4000000) / 4000001)
            return fail("channelLoop.identityCapacity: Legacy Clip ID cannot represent repeated occurrence "
                        "tags safely");
    }
    for (const auto &p : audioPlacements)
    {
        const auto *source = channel(p.channel);
        if (!unique(p.id) || !source || !source->audioClip || p.lane < 0 || p.lane > 127 ||
            !std::isfinite(p.beat) || p.beat < 0 || !std::isfinite(p.offset) || p.offset < 0 ||
            !std::isfinite(p.duration) || p.duration <= 0 ||
            p.offset + p.duration > source->audioDuration + 1.e-9 || !std::isfinite(p.fadeIn) ||
            !std::isfinite(p.fadeOut) || p.fadeIn < 0 || p.fadeOut < 0 ||
            p.fadeIn + p.fadeOut > p.duration + 1.e-9 ||
            !std::isfinite(p.warpBpm) || p.warpBpm < 0 || p.warpBpm > 522 || !std::isfinite(p.warpOffset) ||
            p.warpOffset < 0 || !std::isfinite(p.warpDuration) || p.warpDuration < 0 ||
            p.beat > 1000000)
            return fail("Invalid audio placement");
    }
    if (auto routes=validateMixerRouting(*this);routes.failed()) return routes;
    std::size_t bindingCount = 0;
    auto validPlugin = [&](const PluginSpec &plugin, bool instrument)
    {
        bindingCount += plugin.automationBindings.size();
        if (!unique(plugin.id) || plugin.instrument != instrument || plugin.name.isEmpty() ||
            plugin.descriptor.length() > 65536 || plugin.state.length() > 24 * 1024 * 1024 ||
            bindingCount > 4096 || !validPluginBindings(plugin.automationBindings) ||
            !pluginBusRequestFor(plugin).valid() || !std::isfinite(plugin.wet) || plugin.wet < 0 ||
            plugin.wet > 1 || (instrument && (plugin.wet != 1 || plugin.detector)) ||
            (!instrument && !plugin.extraOutputMixers.empty()) ||
            std::any_of(plugin.extraOutputMixers.begin(), plugin.extraOutputMixers.end(),
                        [&](int target) { return !mixerInsert(*this, target); }))
            return false;
        auto xml = juce::XmlDocument::parse(plugin.descriptor);
        juce::PluginDescription type;
        return xml && type.loadFromXml(*xml) &&
               (type.pluginFormatName == "VST3" ||
                (instrument ? isNativeInstrument(type) : effects::isNativeDescription(type))) &&
               (!plugin.detector || !type.isInstrument) && validPluginStateEncoding(plugin.state);
    };
    for (auto &c : channels)
        if (c.instrument && !validPlugin(*c.instrument, true))
            return fail("Invalid plugin instrument state");
    for (auto &m : mixer)
    {
        if (m.effects.size() > 16)
            return fail("Mixer effects limit exceeded");
        for (auto &plugin : m.effects)
            if (!validPlugin(plugin, false))
                return fail("Invalid plugin effect state");
    }
    auto validAutomationTarget = [&](AutomationKey key)
    {
        if (key.kind == AutomationParameter::samplerModulation)
            return channel(key.owner) != nullptr &&
                   lookupSamplerAutomationParameter(key.parameter).has_value();
        if (key.kind == AutomationParameter::mixerGain || key.kind == AutomationParameter::mixerPan ||
            key.kind == AutomationParameter::mixerStereoSeparation)
            return key.parameter == 0 && key.owner >= 0 && key.owner <= std::numeric_limits<int>::max() &&
                   inserts.contains(int(key.owner));
        if (key.kind == AutomationParameter::channelGain || key.kind == AutomationParameter::channelPan)
            return key.parameter == 0 && key.owner > 0 && channel(Id(key.owner)) != nullptr;
        if (key.kind == AutomationParameter::mixerSend)
        {
            if (key.owner <= 0 || key.owner > std::numeric_limits<int>::max() ||
                key.parameter > std::uint32_t(std::numeric_limits<int>::max()) ||
                !inserts.contains(int(key.parameter)))
                return false;
            return mixerRoute(*this, int(key.owner), int(key.parameter)) != nullptr;
        }
        if (key.kind == AutomationParameter::pluginNormalized)
            if (const auto *spec = plugin(key.owner))
                return std::any_of(spec->automationBindings.begin(), spec->automationBindings.end(),
                                   [&](const auto &binding) { return binding.id == key.parameter; });
        return false;
    };
    for (const auto &source : automation.sources)
    {
        if (!unique(source.id) || source.name.find('\0') != std::string::npos ||
            !juce::CharPointer_UTF8::isValidString(source.name.data(), int(source.name.size())))
            return fail("Invalid automation source identity or name");
        for (const auto &point : source.points)
            if (!unique(point.id))
                return fail("Duplicated automation point identity");
        for (const auto &link : source.links)
            if (!validAutomationTarget(link.key))
                return fail("Unavailable automation target or undeclared native parameter");
    }
    for (const auto &placement : automation.placements)
        if (!unique(placement.id))
            return fail("Duplicated automation placement identity");
    for (const auto &value : automation.initialValues)
        if (!validAutomationTarget(value.key))
            return fail("Unavailable initialized automation target");
    for (const auto &modulator : automation.modulators)
        if (!unique(modulator.id) || !validAutomationTarget(modulator.link.key) ||
            !juce::CharPointer_UTF8::isValidString(modulator.name.data(), int(modulator.name.size())) ||
            modulator.name.find('\0') != std::string::npos)
            return fail("modulator.targetUnavailable: Remove the modulator before its target");
    AutomationProgram checked;
    std::string automationError;
    if (!AutomationProgram::compile(automation, true, checked, automationError, automationDefaults()))
        return juce::Result::fail(juce::String(automationError));
    if (clipGroups.size() > 50000)
        return fail("Clip group limit exceeded");
    const auto clips = clipGroups.empty() ? std::set<Id>{} : projectClipIds(*this);
    std::set<Id> grouped;
    for (const auto &group : clipGroups)
    {
        if (!unique(group.id) || group.id > 1000000000 || group.members.size() < 2 ||
            group.members.size() > 100000)
            return fail("Invalid Clip group identity or size");
        for (auto member : group.members)
            if (member > 1000000000 || !clips.contains(member) || !grouped.insert(member).second)
                return fail("Unavailable/duplicate Clip group member");
        if (grouped.size() > 100000)
            return fail("Clip group member limit exceeded");
    }
    if (samplerControllerLinks.size() > samplerControllerLinkLimit)
        return fail("samplerController.limit: At most512 project links");
    for (size_t index = 0; index < samplerControllerLinks.size(); ++index)
    {
        const auto &link = samplerControllerLinks[index];
        if (!unique(link.id) || !channel(link.channel))
            return fail("samplerController.invalidTarget: Unique link ID and existing Channel required");
        if (auto valid = validateSamplerControllerLink(link); valid.failed())
            return valid;
        for (size_t other = 0; other < index; ++other)
            if (samplerControllerSourceOverlaps(link, samplerControllerLinks[other]))
                return fail("samplerController.sourceConflict: Enabled source addresses overlap");
    }
    if (nextId <= maxId)
        return fail("Invalid ID allocator");
    return validateNoteRepeatBudget(*this);
}
std::vector<ScheduledNote> sequence(const Project &p, bool patternMode, Id selectedPattern)
{
    if (auto valid = validateNoteRepeatBudget(p); valid.failed())
        throw std::invalid_argument(valid.getErrorMessage().toStdString());
    std::vector<ScheduledNote> result;
    std::set<Id> muted;
    std::set<std::tuple<Id, double, int>> keyswitches; // (Channel, beat, pitch) already sent.
    for (const auto &channel : p.channels)
        if (channel.muted)
            muted.insert(channel.id);
    auto emit = [&](const Pattern &pattern, double start, double length, double offset, Id placement,
                    std::uint32_t clipRepeat)
    {
        // One window of the Pattern; a longer clip calls this once per repeat (forEachClipRepeat).
        const std::uint32_t repeatBits = clipRepeat << clipRepeatShift;
        for (const auto &occurrence : rawPatternOccurrences(p, pattern))
        {
            const auto &n = occurrence.note;
            const auto *channel = p.channel(n.channel);
            const auto delay = channel ? swingDelay(n.beat, p.swing, *channel) : 0;
            const auto origin = n.beat + delay + noteShiftDelay(n.shiftPercent);
            const auto duration =
                channel && channel->truncateSwing ? std::max(.000001, n.length - delay) : n.length;
            if (n.repeatMode == 0)
            {
                const auto b = origin - offset;
                if (!n.muted && b >= 0 && b < length && b < pattern.length - offset &&
                    (n.velocity > 0 || n.slide) && !muted.contains(n.channel))
                {
                    if (n.articulation > 0 && channel && n.articulation <= int(channel->articulations.size()) &&
                        channel->articulations[size_t(n.articulation - 1)].kind == 0)
                    {
                        const int keyswitch = channel->articulations[size_t(n.articulation - 1)].keyswitch;
                        const double at = start + b;
                        if (keyswitches.insert({n.channel, at, keyswitch}).second) // Short keyswitch note first.
                            result.push_back({n.channel, at, std::min(1. / 64, duration), keyswitch, .8f, 0,
                                              loopOccurrencePlacement(placement, occurrence.iteration)});
                    }
                    result.push_back(
                        {n.channel, start + b, std::min({duration, length - b, pattern.length - offset - b}),
                         n.pitch, n.velocity, n.id, loopOccurrencePlacement(placement, occurrence.iteration),
                         n.step, n.pan, n.fineCents, repeatBits, n.releaseVelocity, n.modX, n.modY, n.slide,
                         n.portamento, n.colourGroup, n.articulation});
                }
                continue;
            }
            if (n.muted || (!n.slide && n.velocity <= 0) || muted.contains(n.channel) ||
                offset >= pattern.length)
                continue;
            NoteRepeatWindow visible;
            if (!noteRepeatVisibleWindow(n.repeatMode, origin, duration, offset,
                                         std::min(pattern.length, offset + length), visible))
                throw std::invalid_argument("noteRepeat.invalidPlan: Unrepresentable source Repeat window");
            for (std::uint32_t row = 0; row < visible.count; ++row)
            {
                const auto ordinal = visible.first + row;
                const auto b = origin + noteRepeatOffset(n.repeatMode, ordinal) - offset;
                result.push_back({n.channel, start + b,
                                  std::min({noteRepeatGateDuration(n.repeatMode, duration, ordinal),
                                            length - b, pattern.length - offset - b}),
                                  n.pitch, n.velocity, n.id,
                                  loopOccurrencePlacement(placement, occurrence.iteration), n.step, n.pan,
                                  n.fineCents, ordinal | repeatBits, n.releaseVelocity, n.modX, n.modY, n.slide,
                                  n.portamento, n.colourGroup});
            }
        }
    };
    if (patternMode)
    {
        if (auto *pat = p.pattern(selectedPattern))
            emit(*pat, 0, pat->length, 0, 0, 0);
    }
    else
        for (auto &clip : p.placements)
            if (!clip.muted && !p.laneMuted(clip.lane))
                if (auto *pat = p.pattern(clip.pattern))
                    forEachClipRepeat(clip.length, clip.offset, pat->length,
                                      [&](double position, double length, double offset, std::uint32_t repeat)
                                      { emit(*pat, clip.beat + position, length, offset, clip.id, repeat); });
    std::stable_sort(result.begin(), result.end(), [](auto &a, auto &b) { return a.beat < b.beat; });
    return result;
}
static void preserveNumericPrecision(juce::ValueTree &tree)
{
    // JUCE's general var/XML formatter can use 16 significant digits. Recorded timings need 17
    // to round-trip every binary64 value. Keep canonical in-memory properties numeric.
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        auto name = tree.getPropertyName(i);
        auto value = tree.getProperty(name);
        if (value.isDouble())
        {
#if JUCE_MAC
            tree.setProperty(name, juce::String(number_text::formatClassic(double(value))), nullptr);
#else
            char buffer[64];
            auto converted =
                std::to_chars(std::begin(buffer), std::end(buffer), double(value), std::chars_format::general,
                              std::numeric_limits<double>::max_digits10);
            jassert(converted.ec == std::errc{});
            tree.setProperty(name, juce::String::fromUTF8(buffer, int(converted.ptr - buffer)), nullptr);
#endif
        }
    }
    for (auto child : tree)
        preserveNumericPrecision(child);
}
juce::Result saveProject(const Project &project, const juce::File &file)
{
    auto valid = project.validate();
    if (valid.failed())
        return valid;
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Cannot create project directory");
    Project portable = project;
    for (auto &c : portable.channels)
        if (!c.instrument && !c.source.startsWith("builtin:"))
        {
            juce::File source(c.source);
            // A missing file must not stop the user saving: keep its last known path so it
            // can be found again (Find missing samples) and the app reports it.
            if (!juce::File::isAbsolutePath(c.source) || !source.existsAsFile())
                continue;
            auto media = file.getParentDirectory().getChildFile("media");
            if (!media.createDirectory())
                return juce::Result::fail("Cannot create media directory");
            auto destination =
                media.getChildFile(juce::MD5(source).toHexString() + source.getFileExtension());
            if (source != destination && !destination.existsAsFile() && !source.copyFileTo(destination))
                return juce::Result::fail("Cannot collect sample");
            c.source = destination.getRelativePathFrom(file.getParentDirectory());
        }
    juce::TemporaryFile temporary(file);
    auto state = portable.state();
    preserveNumericPrecision(state);
    auto xml = state.createXml();
    if (!temporary.getFile().replaceWithText(xml->toString()) ||
        temporary.getFile().getSize() > 32 * 1024 * 1024 || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Project write failed; previous file preserved");
    return juce::Result::ok();
}
juce::Result saveProjectSnapshot(const Project &project, const juce::File &file)
{
    auto valid = project.validate();
    if (valid.failed())
        return valid;
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Cannot create snapshot directory");
    juce::TemporaryFile temporary(file);
    auto state = project.state();
    preserveNumericPrecision(state);
    auto xml = state.createXml();
    if (!temporary.getFile().replaceWithText(xml->toString()) ||
        temporary.getFile().getSize() > 32 * 1024 * 1024 || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Snapshot write failed; previous file preserved");
    return juce::Result::ok();
}
juce::Result loadProject(const juce::File &file, Project &result)
{
    if (!file.existsAsFile() || file.getSize() > 32 * 1024 * 1024)
        return juce::Result::fail("Project unavailable or too large");
    auto xml = juce::XmlDocument::parse(file);
    if (!xml)
        return juce::Result::fail("Malformed project XML");
    Project next;
    auto status = Project::parse(juce::ValueTree::fromXml(*xml), next);
    if (status.failed())
        return status;
    for (auto &c : next.channels)
        if (!c.instrument && !c.source.startsWith("builtin:"))
            c.source = file.getParentDirectory().getChildFile(c.source).getFullPathName();
    result = std::move(next);
    return juce::Result::ok();
}
} // namespace vibera
