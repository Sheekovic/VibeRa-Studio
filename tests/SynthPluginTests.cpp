// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthPlugin.h"
#include "synth/SynthUserPresets.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace synth_plugin_test
{
thread_local bool callback = false;
thread_local unsigned allocations = 0, destructions = 0;
struct CallbackScope
{
    CallbackScope()
    {
        callback = true;
    }
    ~CallbackScope()
    {
        callback = false;
    }
};
} // namespace synth_plugin_test
void *operator new(std::size_t n)
{
    if (synth_plugin_test::callback)
        ++synth_plugin_test::allocations;
    if (auto *p = std::malloc(std::max<std::size_t>(n, 1)))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void *p) noexcept
{
    if (p && synth_plugin_test::callback)
        ++synth_plugin_test::destructions;
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}

using namespace vibera::synth;
namespace
{
unsigned checks = 0;
juce::StringArray componentCaseIds;
void require(bool value, const juce::String &message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message.toStdString());
}
void componentCase(const juce::String &id)
{
    require(!componentCaseIds.contains(id), "Duplicate component acceptance case: " + id);
    componentCaseIds.add(id);
}
juce::var componentCases()
{
    juce::Array<juce::var> result;
    for (const auto &id : componentCaseIds)
        result.add(id);
    return result;
}
struct OwnedUserDirectory
{
    juce::String previous = juce::SystemStats::getEnvironmentVariable("VIBERA_SYNTH_USER_DIRECTORY", "");
    explicit OwnedUserDirectory(const juce::File &directory)
    {
#if JUCE_WINDOWS
        require(_wputenv_s(L"VIBERA_SYNTH_USER_DIRECTORY", directory.getFullPathName().toWideCharPointer()) ==
                    0,
                "Cannot set owned synth test directory");
#else
        require(::setenv("VIBERA_SYNTH_USER_DIRECTORY", directory.getFullPathName().toRawUTF8(), 1) == 0,
                "Cannot set owned synth test directory");
#endif
        require(SynthPlugin::userDirectory() == directory,
                "Test executable did not honor validated owned user directory");
    }
    ~OwnedUserDirectory()
    {
#if JUCE_WINDOWS
        _wputenv_s(L"VIBERA_SYNTH_USER_DIRECTORY", previous.toWideCharPointer());
#else
        if (previous.isEmpty())
            ::unsetenv("VIBERA_SYNTH_USER_DIRECTORY");
        else
            ::setenv("VIBERA_SYNTH_USER_DIRECTORY", previous.toRawUTF8(), 1);
#endif
    }
};
std::uint64_t byteIdentity(const juce::File &file)
{
    auto stream = file.createInputStream();
    require(bool(stream), "Cannot read factory input: " + file.getFullPathName());
    std::array<unsigned char, 8192> buffer{};
    std::uint64_t hash = 14695981039346656037ull;
    while (!stream->isExhausted())
    {
        const auto n = stream->read(buffer.data(), int(buffer.size()));
        require(n > 0, "Factory identity read stalled");
        for (int i = 0; i < n; ++i)
        {
            hash ^= buffer[std::size_t(i)];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}
struct OwnedFactoryManifest
{
    juce::File file = SynthPlugin::factoryDirectory().getChildFile("library.json");
    juce::MemoryBlock original;
    juce::String originalSha256;
    bool altered = false;
    OwnedFactoryManifest()
    {
        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto configuration = executable.getParentDirectory();
        const auto target = configuration.getParentDirectory();
        const auto buildRoot = target.getParentDirectory();
        require(executable.getFileName() == "VibeRaSynthPluginTests.exe" &&
                    target.getFileName() == "VibeRaSynthPluginTests_artefacts" &&
                    buildRoot.getFileName() == "windows" &&
                    buildRoot.getParentDirectory().getFileName() == "build" &&
                    file == configuration.getChildFile("Content/Synthesizer/library.json") &&
                    file.isAChildOf(target),
                "Manifest mutation requires the exact test executable's own build/windows target folder");
        require(!executable.isSymbolicLink() && !buildRoot.isSymbolicLink() &&
                    !buildRoot.getParentDirectory().isSymbolicLink(),
                "Owned test executable/build root must not be a linked path");
        for (auto path = file; path != buildRoot; path = path.getParentDirectory())
            require(!path.isSymbolicLink(), "Owned manifest path must not traverse a link");
        require(file.getSize() > 0 && file.getSize() <= 2 * 1024 * 1024 && file.loadFileAsData(original),
                "Cannot retain bounded original test-copy manifest bytes");
        originalSha256 = juce::SHA256(original).toHexString();
        require(juce::SHA256(file).toHexString() == originalSha256,
                "Original test-copy manifest changed while being retained");
    }
    bool restore()
    {
        if (!file.replaceWithData(original.getData(), original.getSize()) ||
            juce::SHA256(file).toHexString() != originalSha256)
            return false;
        altered = false;
        return true;
    }
    ~OwnedFactoryManifest()
    {
        if (altered && !restore())
            std::cerr << "ERROR: cannot restore owned test-copy manifest " << file.getFullPathName()
                      << "; expected SHA256=" << originalSha256 << '\n';
    }
};
juce::var factoryManifestFailures()
{
    OwnedFactoryManifest owned;
    const auto presets = factoryPresets();
    const auto sampled = std::find_if(presets.begin(), presets.end(),
                                      [](const FactoryPreset &p) { return p.source == SourceKind::sample; });
    require(sampled != presets.end(), "No recorded factory sound for missing-content acceptance");
    juce::StringArray errors;
    for (const bool missing : {true, false})
    {
        owned.altered = true; // The guard also restores after any failed write/assertion/constructor.
        if (missing)
            require(owned.file.deleteFile() && !owned.file.exists(), "Cannot hide owned test-copy manifest");
        else
        {
            constexpr char corrupt[] = "{ invalid owned test manifest";
            require(owned.file.replaceWithData(corrupt, sizeof(corrupt) - 1),
                    "Cannot write owned corrupt-manifest fixture");
        }
        {
            SynthPlugin failed;
            const auto error = failed.contentError();
            require(error.containsIgnoreCase("reinstall") && error.contains("Content/Synthesizer") &&
                        error.contains("library.json") && ((failed.telemetry() >> 32) & 1u) == 0,
                    juce::String(missing ? "Missing" : "Corrupt") +
                        " actual factory manifest did not produce an actionable content error: " + error);
            const auto selected = failed.currentPresetId();
            require(failed.selectFactoryPreset(sampled->id).failed() && failed.currentPresetId() == selected,
                    "Missing recorded content allowed a sampled preset or mutated the rejected selection");
            errors.add(error);
        }
        require(owned.restore() && juce::SHA256(owned.file).toHexString() == owned.originalSha256,
                "Owned factory manifest bytes/hash were not restored after failure acceptance");
    }
    auto *result = new juce::DynamicObject;
    result->setProperty("cases", "missing, malformed JSON");
    result->setProperty("errors", errors.joinIntoString("\n---\n"));
    result->setProperty("restoredSha256", owned.originalSha256);
    result->setProperty("scope", "Exact VibeRaSynthPluginTests_artefacts content copy only");
    return result;
}
Values snapshot(SynthPlugin &plugin)
{
    Values v{};
    for (std::size_t i = 0; i < parameterCount; ++i)
        v[i] = plugin.getHostedParameter(int(i))->getValue();
    return v;
}
void same(const Values &a, const Values &b, const juce::String &message)
{
    for (std::size_t i = 0; i < parameterCount; ++i)
        require(std::abs(a[i] - b[i]) <= 2e-6f,
                message + " / " + parameterSpec(static_cast<ParameterId>(i)).key);
}
float normalized(ParameterId id, float native)
{
    const auto &s = parameterSpec(id);
    juce::NormalisableRange<float> r(s.minimum, s.maximum, s.interval, s.skew);
    return r.convertTo0to1(r.snapToLegalValue(native));
}
void set(SynthPlugin &p, ParameterId id, float native)
{
    auto *parameter = p.parameterForId(SynthPlugin::nativeId(id));
    require(parameter != nullptr, "Missing hosted synth scalar");
    parameter->setValue(normalized(id, native));
}
juce::MemoryBlock encode(const juce::XmlElement &xml)
{
    juce::MemoryBlock b;
    juce::AudioProcessor::copyXmlToBinary(xml, b);
    return b;
}
void configurePlain(SynthPlugin &p)
{
    const auto presets = factoryPresets();
    const auto found = std::find_if(presets.begin(), presets.end(),
                                    [](const FactoryPreset &f) { return f.source == SourceKind::synthesis; });
    require(found != presets.end() && p.selectFactoryPreset(found->id).wasOk(),
            "No available electronic factory patch");
    for (auto id : {ParameterId::eqEnabled, ParameterId::compressorEnabled, ParameterId::chorusEnabled,
                    ParameterId::delayEnabled, ParameterId::reverbEnabled})
        set(p, id, 0);
    set(p, ParameterId::sourceType, 0);
    set(p, ParameterId::oscillatorType, 0);
    set(p, ParameterId::oscillatorBlend, 0);
    set(p, ParameterId::filterType, 3);
    set(p, ParameterId::motion, 0);
    set(p, ParameterId::vibratoDepthCents, 0);
    set(p, ParameterId::attackMs, 1);
    set(p, ParameterId::decayMs, 1);
    set(p, ParameterId::sustain, 1);
    set(p, ParameterId::releaseMs, 5);
    set(p, ParameterId::pitchBend, 0);
    set(p, ParameterId::masterDb, 0);
}
void parametersAndState(SynthPlugin &plugin)
{
    require(plugin.getParameters().size() == int(parameterCount) && plugin.acceptsMidi() &&
                !plugin.producesMidi() && !plugin.getVST3Client(),
            "Native synthesizer parameter/MIDI/format contract is wrong");
    require(isNativeDescription(plugin.getPluginDescription()),
            "Native synthesizer descriptor identity mismatch");
    std::set<juce::String> ids;
    for (const auto &spec : parameterSpecs())
    {
        auto *p = plugin.parameterForId(SynthPlugin::nativeId(spec.id));
        SynthPlugin::ParameterInfo info;
        float value = -1;
        require(p && plugin.parameterInfo(SynthPlugin::nativeId(spec.id), info) &&
                    plugin.parameterValue(info.id, value) &&
                    p == plugin.getHostedParameter(int(parameterIndex(spec.id))) && p->isAutomatable(),
                "Hosted synth metadata mismatch");
        require(ids.insert(plugin.getHostedParameter(int(parameterIndex(spec.id)))->getParameterID()).second,
                "Duplicate stable synthesizer parameter ID");
        const auto before = p->getValue();
        p->setValue(std::numeric_limits<float>::quiet_NaN());
        require(p->getValue() == before, "Non-finite scalar changed synth state");
    }
    configurePlain(plugin);
    set(plugin, ParameterId::tone, .31f);
    set(plugin, ParameterId::masterDb, -9);
    const auto expected = snapshot(plugin);
    juce::MemoryBlock saved;
    plugin.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize()));
    require(xml && plugin.acceptsStateEnvelope(*xml), "Own synth state envelope refused");
    set(plugin, ParameterId::tone, .9f);
    require(plugin.restoreState(saved.getData(), int(saved.getSize())), "Own synth state failed restore");
    same(snapshot(plugin), expected, "Exact native synth state roundtrip");
    const auto refuse = [&](std::unique_ptr<juce::XmlElement> bad, const juce::String &reason)
    {
        const auto b = encode(*bad);
        require(!plugin.restoreState(b.getData(), int(b.getSize())), "Accepted " + reason);
        same(snapshot(plugin), expected, "Partial mutation on " + reason);
    };
    auto bad = std::make_unique<juce::XmlElement>(*xml);
    bad->setAttribute("schemaVersion", "1.5");
    refuse(std::move(bad), "fractional schema");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->setAttribute("extra", "x");
    refuse(std::move(bad), "unknown root attribute");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->getFirstChildElement()->setAttribute("id", "65536junk");
    refuse(std::move(bad), "numeric ID junk");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->getFirstChildElement()->setAttribute("value", "0.4junk");
    refuse(std::move(bad), "numeric value junk");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->getFirstChildElement()->setAttribute("value", "NaN");
    refuse(std::move(bad), "non-finite value");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->getChildElement(int(parameterIndex(ParameterId::polyphony)))->setAttribute("value", .123456);
    refuse(std::move(bad), "off-grid discrete polyphony");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->removeChildElement(bad->getFirstChildElement(), true);
    refuse(std::move(bad), "missing scalar");
    bad = std::make_unique<juce::XmlElement>(*xml);
    bad->addChildElement(new juce::XmlElement(*bad->getFirstChildElement()));
    refuse(std::move(bad), "duplicate scalar");
    bad = std::make_unique<juce::XmlElement>(*xml);
    auto *s1 = bad->createNewChildElement("VibeRaParameterValues");
    s1->setAttribute("version", 1);
    auto *s2 = bad->createNewChildElement("VibeRaParameterValues");
    s2->setAttribute("version", 1);
    refuse(std::move(bad), "duplicate worker sidecars");
    auto appended = std::make_unique<juce::XmlElement>(*xml);
    auto *sidecar = appended->createNewChildElement("VibeRaParameterValues");
    sidecar->setAttribute("version", 1);
    auto *entry = sidecar->createNewChildElement("PARAM");
    entry->setAttribute("id", juce::String(juce::int64(SynthPlugin::nativeId(ParameterId::tone))));
    entry->setAttribute("value", .42);
    const auto bytes = encode(*appended);
    require(plugin.restoreState(bytes.getData(), int(bytes.getSize())) &&
                std::abs(plugin.parameterForId(SynthPlugin::nativeId(ParameterId::tone))->getValue() - .42f) <
                    1e-6,
            "Validated worker sidecar was not restored");
    require(plugin.restoreState(saved.getData(), int(saved.getSize())),
            "Valid retry after malformed state failed");
}
void timedEventsAndPanic(SynthPlugin &plugin)
{
    configurePlain(plugin);
    plugin.prepareToPlay(48000, 256);
    juce::MemoryBlock initial;
    plugin.getStateInformation(initial);
    const auto master = SynthPlugin::nativeId(ParameterId::masterDb);
    const std::array<SynthPlugin::ParameterEvent, 2> events{{{master, 64, 0}, {master, 128, 1}}};
    juce::AudioBuffer<float> actual(2, 256), expected(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.f), 0);
    const auto current = plugin.parameterForId(master)->getValue();
    bool admitted = false, flushed = false;
    {
        synth_plugin_test::CallbackScope guard;
        admitted = plugin.setParameterEventsForNextBlock(events, 256);
        flushed = plugin.flushParameterChangesForState();
    }
    require(admitted && flushed && plugin.parameterForId(master)->getValue() == current,
            "Native state flush consumed future timed scalar targets");
    {
        synth_plugin_test::CallbackScope guard;
        plugin.processBlock(actual, midi);
    }
    require(plugin.restoreState(initial.getData(), int(initial.getSize())),
            "Timed reference state restore failed");
    plugin.prepareToPlay(48000, 256);
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.f), 0);
    int begin = 0;
    for (const auto &e : events)
    {
        juce::AudioBuffer<float> section(expected.getArrayOfWritePointers(), 2, begin,
                                         e.sampleOffset - begin);
        plugin.processBlock(section, midi);
        plugin.parameterForId(e.id)->setValue(e.value);
        begin = e.sampleOffset;
    }
    juce::AudioBuffer<float> last(expected.getArrayOfWritePointers(), 2, begin, 256 - begin);
    plugin.processBlock(last, midi);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 256; ++i)
            require(std::isfinite(actual.getSample(c, i)) &&
                        std::abs(actual.getSample(c, i) - expected.getSample(c, i)) < 1e-6,
                    "Sample-offset adapter events differ from segmented scalar/audio reference");
    auto *selector = plugin.parameterForId(SynthPlugin::nativeId(ParameterId::factoryPreset));
    const auto selected = selector->getValue();
    selector->setValue(selected);
    plugin.processBlock(actual, midi);
    require(plugin.activeVoiceCount() == 1, "Unchanged preset selector reset a sustained note");
    require(plugin.setParameterEventsForNextBlock(events, 256), "Mismatched-frame fixture admission failed");
    juce::AudioBuffer<float> shortBlock(2, 128);
    plugin.processBlock(shortBlock, midi);
    require(shortBlock.getMagnitude(0, 0, 128) == 0 && plugin.activeVoiceCount() == 0,
            "Mismatched event block retained sound or silently applied partial events");
    plugin.reset();
    {
        synth_plugin_test::CallbackScope guard;
        plugin.processBlock(actual, midi);
    }
    require(actual.getMagnitude(0, 0, 256) == 0 && plugin.activeVoiceCount() == 0,
            "Deterministic synth reset retained notes/effect tails");
}
juce::Array<juce::var> factoryPcm(SynthPlugin &plugin)
{
    const auto manifest = juce::JSON::parse(SynthPlugin::factoryDirectory().getChildFile("library.json"));
    const auto *instruments = manifest["instruments"].getArray();
    require(instruments && instruments->size() >= 18, "Recorded factory instrument map is incomplete");
    juce::Array<juce::var> rows;
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    plugin.prepareToPlay(48000, 256);
    for (const auto &preset : factoryPresets())
    {
        require(plugin.selectFactoryPreset(preset.id).wasOk(),
                "Factory patch unavailable: " + juce::String(preset.id));
        require(plugin.currentPresetId() == preset.id, "Factory selector disagrees with patch identity");
        int note = 60;
        if (preset.source != SourceKind::synthesis)
        {
            bool found = false;
            for (const auto &instrument : *instruments)
                if (instrument["id"].toString() == preset.sampleAsset)
                {
                    const auto *regions = instrument["regions"].getArray();
                    require(regions && !regions->isEmpty(), "Recorded patch has no actual sample regions");
                    note = int(regions->getReference(0)["rootNote"]);
                    found = true;
                    break;
                }
            require(found, "Factory acoustic patch silently substituted an oscillator");
        }
        double energy = 0;
        float maximum = 0;
        int frames = 0;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, .85f), 23);
        for (int block = 0; block < 188; ++block)
        {
            {
                synth_plugin_test::CallbackScope guard;
                plugin.processBlock(buffer, midi);
            }
            if (block == 0)
                for (int i = 0; i < 23; ++i)
                    require(buffer.getSample(0, i) == 0 && buffer.getSample(1, i) == 0,
                            "Instrument played before actual MIDI offset");
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                {
                    const auto value = buffer.getSample(c, i);
                    require(std::isfinite(value), "Factory patch produced non-finite PCM");
                    energy += double(value) * value;
                    maximum = std::max(maximum, std::abs(value));
                }
            frames += 256;
        }
        require(energy > 1e-8 && maximum > 1e-6,
                "Factory patch produced no meaningful actual PCM: " + juce::String(preset.id));
        auto *row = new juce::DynamicObject;
        row->setProperty("preset", preset.id);
        row->setProperty("source", sourceKindName(preset.source));
        row->setProperty("asset", preset.sampleAsset);
        row->setProperty("frames", frames);
        row->setProperty("peak", maximum);
        row->setProperty("meanSquare", energy / (2 * frames));
        rows.add(row);
    }
    require(synth_plugin_test::allocations == 0 && synth_plugin_test::destructions == 0,
            "Native synth callback allocated/destroyed C++ heap objects");
    return rows;
}
juce::Array<juce::var> callbackPerformance(SynthPlugin &plugin)
{
    // Warm the actual adapter and five native effects; measure whole256-frame
    // callbacks, never infer a device deadline from the engine's inner slices.
    juce::Array<juce::var> results;
    constexpr int frames = 256, blocks = 600;
    constexpr double deadline = 1000000.0 * frames / 48000;
    juce::AudioBuffer<float> audio(2, frames);
    juce::MidiBuffer midi;
    for (const auto *id : {"pop-velvet-upright", "pop-crystal"})
    {
        auto selected = plugin.selectFactoryPreset(id);
        require(selected.wasOk(), "Timing patch unavailable");
        set(plugin, ParameterId::polyphony, 64);
        set(plugin, ParameterId::sustain, 1);
        set(plugin, ParameterId::attackMs, 1);
        set(plugin, ParameterId::eqEnabled, 1);
        set(plugin, ParameterId::compressorEnabled, 1);
        set(plugin, ParameterId::chorusEnabled, 1);
        set(plugin, ParameterId::delayEnabled, 1);
        set(plugin, ParameterId::reverbEnabled, 1);
        plugin.prepareToPlay(48000, frames);
        for (int note = 36; note < 100; ++note)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, .6f), 0);
        plugin.processBlock(audio, midi);
        const auto voices = plugin.activeVoiceCount();
        require(voices == 64, "Timing fixture must exercise64 real voices");
        for (int i = 0; i < 32; ++i)
            plugin.processBlock(audio, midi);
        std::array<double, blocks> micros{};
        for (auto &elapsed : micros)
        {
            const auto start = std::chrono::steady_clock::now();
            {
                synth_plugin_test::CallbackScope guard;
                plugin.processBlock(audio, midi);
            }
            elapsed =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        }
        std::sort(micros.begin(), micros.end());
        auto *row = new juce::DynamicObject;
        row->setProperty("preset", plugin.currentPresetId());
        row->setProperty("voicesAtStart", voices);
        row->setProperty("frames", frames);
        row->setProperty("sampleRate", 48000);
        row->setProperty("blocks", blocks);
        row->setProperty("wholeCallbackDeadlineMicroseconds", deadline);
        row->setProperty("medianMicroseconds", micros[blocks / 2]);
        row->setProperty("p99Microseconds", micros[blocks * 99 / 100]);
        row->setProperty("maximumMicroseconds", micros.back());
        row->setProperty("deadlineMisses", int(std::count_if(micros.begin(), micros.end(),
                                                             [](double v) { return v > deadline; })));
        results.add(row);
        require(micros[blocks * 99 / 100] < deadline,
                "Representative64-voice callback p99 exceeds outer256-frame budget");
        plugin.reset();
    }
    return results;
}
void masterAttenuatesExistingTails(SynthPlugin &plugin)
{
    configurePlain(plugin);
    set(plugin, ParameterId::delayEnabled, 1);
    set(plugin, ParameterId::delayTimeMs, 10);
    set(plugin, ParameterId::delayFeedback, .8f);
    set(plugin, ParameterId::delayMix, 1);
    set(plugin, ParameterId::space, 0);
    juce::MemoryBlock base;
    plugin.getStateInformation(base);
    const auto renderTail = [&](float master)
    {
        require(plugin.restoreState(base.getData(), int(base.getSize())),
                "Cannot restore actual delay-tail oracle");
        plugin.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 69), 128);
        for (int block = 0; block < 8; ++block)
            plugin.processBlock(audio, midi);
        require(plugin.activeVoiceCount() == 0, "Tail oracle still contains active source voices");
        set(plugin, ParameterId::masterDb, master);
        for (int block = 0; block < 8; ++block)
            plugin.processBlock(audio, midi);
        double energy = 0;
        for (int block = 0; block < 8; ++block)
        {
            {
                synth_plugin_test::CallbackScope guard;
                plugin.processBlock(audio, midi);
            }
            for (int i = 0; i < 256; ++i)
                energy += double(audio.getSample(0, i)) * audio.getSample(0, i);
        }
        return energy;
    };
    const auto loud = renderTail(0), quiet = renderTail(-60);
    require(loud > 1e-8 && quiet < loud * .000025,
            "Master does not attenuate already-running effect tails after source voices stop");
}
juce::Component *component(juce::Component &root, const juce::String &id)
{
    if (root.getComponentID() == id)
        return &root;
    for (int i = 0; i < root.getNumChildComponents(); ++i)
        if (auto *found = component(*root.getChildComponent(i), id))
            return found;
    return nullptr;
}
template <typename Predicate> void awaitEditor(Predicate predicate, const juce::String &message)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500;
    while (!predicate() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep(5);
    }
    require(predicate(), message);
}
void clickComponent(juce::Component &editor, const juce::String &id)
{
    auto *button = dynamic_cast<juce::Button *>(component(editor, id));
    require(button && button->isEnabled() && bool(button->onClick),
            "Enabled native button callback missing: " + id);
    // Invoke the real editor callback; this does not inject OS pointer input.
    button->onClick();
}
juce::MouseEvent componentPress(juce::Component &target, juce::Point<float> point)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            juce::MouseInputSource::defaultPressure,
            0,
            0,
            0,
            0,
            &target,
            &target,
            now,
            point,
            now,
            1,
            false};
}
struct ScalarControlObserver final : juce::AudioProcessorParameter::Listener
{
    SynthPlugin &plugin;
    int expected;
    unsigned values = 0, begins = 0, ends = 0;
    bool onlyExpected = true;
    ScalarControlObserver(SynthPlugin &p, ParameterId id) : plugin(p), expected(int(parameterIndex(id)))
    {
        for (auto *parameter : plugin.getParameters())
            parameter->addListener(this);
    }
    ~ScalarControlObserver() override
    {
        for (auto *parameter : plugin.getParameters())
            parameter->removeListener(this);
    }
    void parameterValueChanged(int index, float) override
    {
        ++values;
        onlyExpected = onlyExpected && index == expected;
    }
    void parameterGestureChanged(int index, bool starting) override
    {
        onlyExpected = onlyExpected && index == expected;
        if (starting)
            ++begins;
        else
            ++ends;
    }
};
void editorScalarGesturesAndEntry(SynthPlugin &plugin, juce::AudioProcessorEditor &editor)
{
    auto *tone = dynamic_cast<juce::Slider *>(component(editor, "synth.parameter.tone"));
    auto *attack = dynamic_cast<juce::Slider *>(component(editor, "synth.parameter.attack-ms"));
    auto *workspace = dynamic_cast<juce::Viewport *>(component(editor, "synth.workspaceScroll"));
    require(editor.getPeer() && tone && attack && workspace && workspace->getViewedComponent() &&
                tone->onDragStart && tone->onDragEnd && attack->isTextBoxEditable(),
            "Peer-backed native slider/gesture/text-entry controls missing");
    const auto entry = snapshot(plugin);
    const auto tonePrior = tone->getValue();
    const auto reveal = [&](juce::Component &control)
    {
        const auto area = workspace->getViewedComponent()->getLocalArea(&control, control.getLocalBounds());
        workspace->setViewPosition(std::max(0, area.getCentreX() - workspace->getViewWidth() / 2),
                                   std::max(0, area.getCentreY() - workspace->getViewHeight() / 2));
        require(control.isShowing() && workspace->getViewArea().intersects(area),
                "Actual scalar widget is unreachable in its native workspace");
    };
    reveal(*tone);
    {
        ScalarControlObserver observer(plugin, ParameterId::tone);
        auto changed = entry;
        {
            // The same Slider notification scope used by JUCE mouse gestures,
            // typed commits and default reset; invokes the real editor bindings.
            juce::Slider::ScopedDragNotification drag(*tone);
            tone->setValue(.42, juce::sendNotificationSync);
            tone->setValue(.61, juce::sendNotificationSync);
            changed[parameterIndex(ParameterId::tone)] = normalized(ParameterId::tone, .61f);
            same(snapshot(plugin), changed, "Ordinary slider drag changed neighbours or missed its value");
            require(observer.values == 2 && observer.begins == 1 && observer.ends == 0 &&
                        observer.onlyExpected,
                    "Ordinary slider drag did not retain one hosted gesture across multiple values");
        }
        same(snapshot(plugin), changed, "Ordinary drag-end unexpectedly reset a non-pitch value");
        require(observer.begins == 1 && observer.ends == 1 && observer.onlyExpected,
                "Ordinary slider drag-end did not close exactly one hosted gesture");
        tone->setValue(tonePrior, juce::sendNotificationSync);
        same(snapshot(plugin), entry, "Cannot restore exact patch after ordinary native slider drag");
        require(observer.values == 3 && observer.begins == 2 && observer.ends == 2 && observer.onlyExpected,
                "Ordinary drag baseline restore left a hosted gesture open");
    }
    componentCase("ui.parameter.non-pitch-drag-gesture-boundaries");
    {
        ScalarControlObserver observer(plugin, ParameterId::tone);
        auto changed = entry;
        {
            juce::Slider::ScopedDragNotification drag(*tone);
            tone->setValue(.47, juce::sendNotificationSync);
            changed[parameterIndex(ParameterId::tone)] = normalized(ParameterId::tone, .47f);
            editor.giveAwayKeyboardFocus();
            editor.focusLost(juce::Component::focusChangedDirectly);
            same(snapshot(plugin), changed,
                 "Focus cleanup discarded an ordinary slider value or edited neighbours");
            require(observer.values == 1 && observer.begins == 1 && observer.ends == 1 &&
                        observer.onlyExpected,
                    "Focus loss failed to close an open ordinary hosted slider gesture");
        }
        require(observer.begins == 1 && observer.ends == 1,
                "Late drag-end duplicated the already completed focus-loss gesture");
        tone->setValue(tonePrior, juce::sendNotificationSync);
        same(snapshot(plugin), entry, "Cannot restore exact patch after ordinary slider focus cleanup");
        require(observer.values == 2 && observer.begins == 2 && observer.ends == 2 && observer.onlyExpected,
                "Ordinary slider did not accept a fresh balanced edit after focus cleanup");
    }
    componentCase("ui.parameter.non-pitch-focus-loss-closes-gesture");
    reveal(*attack);
    juce::Label *textBox = nullptr;
    for (int child = 0; child < attack->getNumChildComponents(); ++child)
        if (auto *label = dynamic_cast<juce::Label *>(attack->getChildComponent(child));
            label && label->isEditable())
        {
            require(textBox == nullptr, "Attack slider has ambiguous editable text boxes");
            textBox = label;
        }
    require(textBox != nullptr, "Actual native Attack Slider TextBox Label missing");
    const auto commitText = [&](const juce::String &text)
    {
        attack->showTextBox();
        auto *field = textBox->getCurrentTextEditor();
        require(field && field->isShowing(), "Native Slider TextBox did not create its actual TextEditor");
        field->setText(text, false);
        // Public Slider commit path -> Label editor commit -> JUCE text parser
        // -> ScopedDragNotification -> the existing hosted native scalar binding.
        attack->hideTextBox(false);
        require(textBox->getCurrentTextEditor() == nullptr,
                "Native Slider TextBox commit left its editor open");
    };
    const auto &attackSpec = parameterSpec(ParameterId::attackMs);
    const auto attackPrior = attack->getValue();
    const auto typed = std::abs(attackPrior - 125.) > .01 ? 125.f : 250.f;
    {
        ScalarControlObserver observer(plugin, ParameterId::attackMs);
        commitText(juce::String(typed, 1) + " ms");
        auto expected = entry;
        expected[parameterIndex(ParameterId::attackMs)] = normalized(ParameterId::attackMs, typed);
        same(snapshot(plugin), expected,
             "Typed native milliseconds changed neighbours or used normalized units");
        require(std::abs(attack->getValue() - typed) < .001 && observer.values == 1 && observer.begins == 1 &&
                    observer.ends == 1 && observer.onlyExpected,
                "Native TextBox commit did not publish one balanced actual Attack edit");
        componentCase("ui.parameter.typed-entry");
        commitText(juce::String(attackSpec.maximum * 5, 1) + " ms");
        expected[parameterIndex(ParameterId::attackMs)] =
            normalized(ParameterId::attackMs, attackSpec.maximum);
        same(snapshot(plugin), expected,
             "Typed upper-range value did not clamp through the real Slider range");
        require(attack->getValue() == attackSpec.maximum,
                "Native TextBox upper clamp/readback differs from its range");
        commitText(juce::String(attackSpec.minimum - 1000, 1) + " ms");
        expected[parameterIndex(ParameterId::attackMs)] =
            normalized(ParameterId::attackMs, attackSpec.minimum);
        same(snapshot(plugin), expected,
             "Typed lower-range value did not clamp through the real Slider range");
        require(attack->getValue() == attackSpec.minimum && observer.values == 3 && observer.begins == 3 &&
                    observer.ends == 3 && observer.onlyExpected,
                "Native TextBox range commits lacked exact clamped readback/balanced gestures");
        componentCase("ui.parameter.typed-entry-range-clamp");
        const bool restoreChanges = std::abs(attackPrior - attackSpec.minimum) > .00001;
        commitText(juce::String(attackPrior, 9) + " ms");
        same(snapshot(plugin), entry, "Cannot restore exact native patch through its real TextBox editor");
        const unsigned expectedEdits = restoreChanges ? 4 : 3;
        require(observer.values == expectedEdits && observer.begins == expectedEdits &&
                    observer.ends == expectedEdits && observer.onlyExpected,
                "TextBox baseline restore added an unrelated or unbalanced hosted edit");
    }
    {
        ScalarControlObserver observer(plugin, ParameterId::attackMs);
        attack->showTextBox();
        auto *field = textBox->getCurrentTextEditor();
        require(field != nullptr, "Native Slider text-cancel fixture did not enter text editing");
        field->setText("7777 ms", false);
        attack->hideTextBox(true);
        same(snapshot(plugin), entry, "Discarding uncommitted native text changed the actual patch");
        require(textBox->getCurrentTextEditor() == nullptr && observer.values == 0 && observer.begins == 0 &&
                    observer.ends == 0,
                "Discarded Slider TextBox edit emitted a phantom hosted history gesture");
    }
    componentCase("ui.parameter.typed-entry-cancel");
    reveal(*tone);
    tone->setValue(.23, juce::sendNotificationSync);
    {
        ScalarControlObserver observer(plugin, ParameterId::tone);
        tone->mouseDoubleClick(componentPress(*tone, tone->getLocalBounds().getCentre().toFloat()));
        auto expected = entry;
        const auto initial = parameterSpec(ParameterId::tone).initial;
        expected[parameterIndex(ParameterId::tone)] = normalized(ParameterId::tone, initial);
        same(snapshot(plugin), expected,
             "Actual Slider double-click did not reset its configured default only");
        require(std::abs(tone->getValue() - initial) < 1e-6 && observer.values == 1 && observer.begins == 1 &&
                    observer.ends == 1 && observer.onlyExpected,
                "Real Slider double-click handler omitted its default readback or balanced hosted gesture");
        tone->setValue(tonePrior, juce::sendNotificationSync);
        same(snapshot(plugin), entry, "Cannot restore exact patch after real double-click default reset");
        require(observer.values == 2 && observer.begins == 2 && observer.ends == 2 && observer.onlyExpected,
                "Default reset baseline restore left unrelated or unbalanced hosted notifications");
    }
    componentCase("ui.parameter.double-click-default");
    workspace->setViewPosition(0, 0);
}
void allEditorControls(SynthPlugin &plugin, juce::AudioProcessorEditor &editor)
{
    juce::MemoryBlock entryState;
    plugin.getStateInformation(entryState);
    const auto presets = factoryPresets();
    const auto sampled = std::find_if(presets.begin(), presets.end(),
                                      [](const auto &preset) { return preset.source == SourceKind::sample; });
    require(sampled != presets.end() && plugin.selectFactoryPreset(sampled->id).wasOk(),
            "All-control UI fixture needs a genuine recorded bank for legal Source changes");
    auto *source = dynamic_cast<juce::ComboBox *>(component(editor, "synth.parameter.source-type"));
    auto *workspace = dynamic_cast<juce::Viewport *>(component(editor, "synth.workspaceScroll"));
    require(source && workspace && workspace->getViewedComponent(),
            "Native source/workspace controls missing");
    awaitEditor([&] { return source->getSelectedId() == 2 && source->isItemEnabled(3); },
                "Recorded-bank readback did not enable the actual sample/hybrid Source choices");
    const auto baseline = snapshot(plugin);
    const auto effectPage = [](ParameterId id) -> const char *
    {
        switch (id)
        {
        case ParameterId::eqEnabled:
        case ParameterId::eqLowDb:
        case ParameterId::eqMidDb:
        case ParameterId::eqHighDb:
            return "EQ";
        case ParameterId::compressorEnabled:
        case ParameterId::compressorThresholdDb:
        case ParameterId::compressorRatio:
            return "Compressor";
        case ParameterId::chorusEnabled:
        case ParameterId::chorusMix:
            return "Chorus";
        case ParameterId::delayEnabled:
        case ParameterId::delayTimeMs:
        case ParameterId::delayFeedback:
        case ParameterId::delayMix:
            return "Delay";
        case ParameterId::reverbEnabled:
        case ParameterId::reverbSize:
        case ParameterId::reverbDecaySeconds:
        case ParameterId::reverbMix:
            return "Reverb";
        default:
            return nullptr;
        }
    };
    unsigned sliders = 0, combos = 0, toggles = 0;
    for (const auto &spec : parameterSpecs())
    {
        if (spec.id == ParameterId::factoryPreset)
            continue; // Factory identity is covered by the real preset-list/button branches below.
        const juce::String id = "synth.parameter." + juce::String(spec.key);
        auto *control = component(editor, id);
        require(control && control->isEnabled(), "Native parameter control missing/disabled: " + id);
        if (const auto *page = effectPage(spec.id))
            clickComponent(editor, "synth.effects." + juce::String(page));
        if (workspace->getViewedComponent()->isParentOf(control))
        {
            const auto area =
                workspace->getViewedComponent()->getLocalArea(control, control->getLocalBounds());
            workspace->setViewPosition(std::max(0, area.getCentreX() - workspace->getViewWidth() / 2),
                                       std::max(0, area.getCentreY() - workspace->getViewHeight() / 2));
            require(control->isVisible() && workspace->getViewArea().intersects(area),
                    "Native parameter cannot be reached through its actual workspace: " + id);
        }
        const juce::NormalisableRange<float> range(spec.minimum, spec.maximum, spec.interval, spec.skew);
        const auto prior = range.snapToLegalValue(range.convertFrom0to1(baseline[parameterIndex(spec.id)]));
        auto target = range.snapToLegalValue(spec.minimum + (spec.maximum - spec.minimum) * .73f);
        if (std::abs(range.convertTo0to1(target) - baseline[parameterIndex(spec.id)]) < .01f)
            target = baseline[parameterIndex(spec.id)] <= .5f ? spec.maximum : spec.minimum;
        auto *slider = dynamic_cast<juce::Slider *>(control);
        auto *combo = dynamic_cast<juce::ComboBox *>(control);
        auto *toggle = dynamic_cast<juce::ToggleButton *>(control);
        require(int(slider != nullptr) + int(combo != nullptr) + int(toggle != nullptr) == 1,
                "Unexpected native parameter widget: " + id);
        if (slider)
        {
            ++sliders;
            require(slider->getName() == spec.name && slider->getTooltip().contains(spec.name),
                    "Native slider lacks its semantic name/tooltip: " + id);
        }
        else if (combo)
        {
            ++combos;
            require(combo->getName() == spec.name && combo->getTooltip().contains(spec.name) &&
                        combo->isItemEnabled(int(target) + 1),
                    "Native combo choice/name/tooltip unavailable: " + id);
        }
        else
        {
            ++toggles;
            target = prior < .5f ? 1.f : 0.f;
            require(toggle->getTooltip().isNotEmpty(), "Native effect enable tooltip missing: " + id);
        }
        const auto change = [&](float value)
        {
            if (slider)
                slider->setValue(value, juce::sendNotificationSync);
            else if (combo)
                combo->setSelectedId(int(std::lround(value)) + 1, juce::sendNotificationSync);
            else
                toggle->setToggleState(value >= .5f, juce::sendNotificationSync);
        };
        ScalarControlObserver observer(plugin, spec.id);
        change(target);
        auto expected = baseline;
        expected[parameterIndex(spec.id)] = normalized(spec.id, target);
        same(snapshot(plugin), expected, "Native widget changed a neighbour or missed its scalar: " + id);
        float nativeReadback = -1;
        require(plugin.parameterValue(SynthPlugin::nativeId(spec.id), nativeReadback) &&
                    std::abs(nativeReadback - expected[parameterIndex(spec.id)]) < 2e-6f &&
                    observer.values == 1 && observer.begins == 1 && observer.ends == 1 &&
                    observer.onlyExpected,
                "Native widget lacks exact scalar readback/balanced host notifications: " + id);
        change(prior);
        same(snapshot(plugin), baseline, "Native widget failed to restore its exact baseline: " + id);
        require(observer.values == 2 && observer.begins == 2 && observer.ends == 2 && observer.onlyExpected,
                "Native widget restore changed unrelated gestures or left one open: " + id);
        componentCase("ui.parameter." + juce::String(spec.key));
    }
    require(sliders == 52 && combos == 6 && toggles == 5 && sliders + combos + toggles == 63,
            "All 63 actual native widgets must be exercised, excluding only factory identity");
    require(plugin.restoreState(entryState.getData(), int(entryState.getSize())),
            "Cannot restore complete entry patch after 63 actual UI control edits");
    awaitEditor([&] { return source->getSelectedId() == 1; },
                "Restored synthesis entry patch did not reach native Source/UI readback");
    workspace->setViewPosition(0, 0);
}
void editorBrowserAndNavigation(SynthPlugin &plugin, juce::AudioProcessorEditor &editor)
{
    const auto presets = factoryPresets();
    auto *search = dynamic_cast<juce::TextEditor *>(component(editor, "synth.browser.search"));
    auto *family = dynamic_cast<juce::ComboBox *>(component(editor, "synth.browser.family"));
    auto *list = dynamic_cast<juce::ListBox *>(component(editor, "synth.browser.presets"));
    auto *workspace = dynamic_cast<juce::Viewport *>(component(editor, "synth.workspaceScroll"));
    auto *keyboard = dynamic_cast<juce::MidiKeyboardComponent *>(component(editor, "synth.keyboard"));
    auto *empty = component(editor, "synth.browser.empty");
    require(search && family && list && list->getModel() && workspace && keyboard && empty,
            "Native browser/navigation surface is incomplete");
    const auto editSearch = [&](const juce::String &text)
    {
        require(bool(search->onTextChange), "Native search callback missing");
        search->setText(text, false);
        search->onTextChange(); // Real callback, dispatched directly without a modal message loop.
    };
    const auto selectFamily = [&](const juce::String &name)
    {
        int id = name == "All" ? 1 : 0;
        for (int row = 0; row < family->getNumItems(); ++row)
            if (family->getItemText(row) == name)
                id = family->getItemId(row);
        require(id > 0, "Actual instrument family choice missing: " + name);
        family->setSelectedId(id, juce::sendNotificationSync);
    };
    const auto beforeFilters = snapshot(plugin);
    clickComponent(editor, "synth.genre.Jazz");
    selectFamily("Bass");
    editSearch("SYNTHESIS");
    std::vector<const FactoryPreset *> expected;
    for (const auto &preset : presets)
        if (juce::StringArray::fromTokens(preset.genre, "|", "").contains("Jazz") &&
            juce::String(preset.family) == "Bass" && preset.source == SourceKind::synthesis)
            expected.push_back(&preset);
    const auto composed = SynthPlugin::readBrowserState();
    require(!expected.empty() && list->getModel()->getNumRows() == int(expected.size()) &&
                composed.genre == "Jazz" && composed.family == "Bass" && composed.search == "SYNTHESIS" &&
                !composed.favouritesOnly && !empty->isVisible(),
            "Genre + instrument + source search callbacks did not compose/persist");
    same(snapshot(plugin), beforeFilters, "Browser filtering changed the current sound");
    componentCase("ui.browser.genre-family-search");
    editSearch("owned-no-factory-preset-can-match-720bd8");
    auto *previous = dynamic_cast<juce::Button *>(component(editor, "synth.preset.previous"));
    auto *next = dynamic_cast<juce::Button *>(component(editor, "synth.preset.next"));
    require(previous && next && list->getModel()->getNumRows() == 0 && empty->isVisible() &&
                !previous->isEnabled() && !next->isEnabled(),
            "Empty composed search lacks its honest empty state/disabled navigation");
    same(snapshot(plugin), beforeFilters, "Empty search mutated the current sound");
    componentCase("ui.browser.empty-state");
    clickComponent(editor, "synth.genre.All Genres");
    selectFamily("All");
    editSearch("");
    require(list->getModel()->getNumRows() == int(presets.size()) && !empty->isVisible() &&
                previous->isEnabled() && next->isEnabled(),
            "Clearing actual composed filters did not restore the complete factory list");
    componentCase("ui.browser.clear-filters");
    const auto beforeStar = snapshot(plugin);
    const auto current = plugin.currentPresetId();
    const auto wasFavourite = SynthPlugin::readBrowserState().favouriteIds.contains(current);
    clickComponent(editor, "synth.preset.favourite");
    require(SynthPlugin::readBrowserState().favouriteIds.contains(current) != wasFavourite,
            "Current-preset star callback did not toggle its own stable ID");
    same(snapshot(plugin), beforeStar, "Current-preset star loaded or edited a patch");
    clickComponent(editor, "synth.preset.favourite");
    require(SynthPlugin::readBrowserState().favouriteIds.contains(current) == wasFavourite,
            "Current-preset star callback did not restore its prior membership");
    componentCase("ui.browser.current-star-only");
    int currentIndex = -1;
    for (std::size_t row = 0; row < presets.size(); ++row)
        if (current == presets[row].id)
            currentIndex = int(row);
    require(currentIndex >= 0, "Native current patch is absent from its factory inventory");
    const int row = (currentIndex + 1) % int(presets.size());
    const auto rowId = juce::String(presets[std::size_t(row)].id);
    const auto rowWasFavourite = SynthPlugin::readBrowserState().favouriteIds.contains(rowId);
    list->getModel()->listBoxItemClicked(row, componentPress(*list, {12, 20}));
    require(SynthPlugin::readBrowserState().favouriteIds.contains(rowId) != rowWasFavourite &&
                plugin.currentPresetId() == current,
            "Preset-row star did not toggle its own ID independently from the sounding patch");
    same(snapshot(plugin), beforeStar, "Preset-row star edited/loaded native scalars");
    list->getModel()->listBoxItemClicked(row, componentPress(*list, {12, 20}));
    require(SynthPlugin::readBrowserState().favouriteIds.contains(rowId) == rowWasFavourite,
            "Preset-row star did not restore prior membership");
    componentCase("ui.browser.row-star-only");
    clickComponent(editor, "synth.genre.Favourites");
    require(SynthPlugin::readBrowserState().favouritesOnly &&
                list->getModel()->getNumRows() == SynthPlugin::readBrowserState().favouriteIds.size(),
            "Actual Favourites button did not filter the stable saved membership");
    same(snapshot(plugin), beforeStar, "Favourite filtering changed the current patch");
    componentCase("ui.browser.favourites-filter");
    clickComponent(editor, "synth.genre.All Genres");
    list->selectRow(row);
    require(SynthPlugin::readBrowserState().selectedPresetId == rowId && plugin.currentPresetId() == current,
            "Preset row selection should persist its highlight before loading");
    same(snapshot(plugin), beforeStar, "Row selection loaded the highlighted preset");
    componentCase("ui.browser.row-selection-only");
    list->getModel()->listBoxItemClicked(row, componentPress(*list, {70, 20}));
    require(plugin.currentPresetId() == rowId, "Actual preset-row body click did not load its stable patch");
    Values expectedPatch{};
    for (const auto &spec : parameterSpecs())
        expectedPatch[parameterIndex(spec.id)] =
            normalized(spec.id, presets[std::size_t(row)].values[parameterIndex(spec.id)]);
    same(snapshot(plugin), expectedPatch, "Preset-row click failed to apply every actual native scalar");
    componentCase("ui.browser.row-click-load");
    clickComponent(editor, "synth.preset.next");
    require(plugin.currentPresetId() == presets[std::size_t((row + 1) % int(presets.size()))].id,
            "Actual Next button did not follow factory order");
    clickComponent(editor, "synth.preset.previous");
    require(plugin.currentPresetId() == rowId, "Actual Previous button did not restore the preceding sound");
    list->getModel()->listBoxItemClicked(int(presets.size()) - 1, componentPress(*list, {70, 20}));
    clickComponent(editor, "synth.preset.next");
    require(plugin.currentPresetId() == presets.front().id,
            "Actual Next button did not wrap to the first sound");
    clickComponent(editor, "synth.preset.previous");
    require(plugin.currentPresetId() == presets.back().id,
            "Actual Previous button did not wrap to the final sound");
    componentCase("ui.browser.previous-next-wrap");
    const auto beforeNavigation = snapshot(plugin);
    const std::array<const char *, 5> pages{"EQ", "Compressor", "Chorus", "Delay", "Reverb"};
    const std::array<ParameterId, 5> representatives{ParameterId::eqLowDb, ParameterId::compressorRatio,
                                                     ParameterId::chorusMix, ParameterId::delayMix,
                                                     ParameterId::reverbMix};
    for (std::size_t page = 0; page < pages.size(); ++page)
    {
        clickComponent(editor, "synth.effects." + juce::String(pages[page]));
        require(SynthPlugin::readBrowserState().effectPage == int(page),
                "FX page preference did not persist");
        for (std::size_t other = 0; other < pages.size(); ++other)
            require(component(editor,
                              "synth.parameter." + juce::String(parameterSpec(representatives[other]).key))
                            ->isVisible() == (page == other),
                    "Actual FX page exposes a different processor's controls");
        same(snapshot(plugin), beforeNavigation, "FX tab navigation changed native DSP settings");
        componentCase("ui.fx.page." + juce::String(pages[page]));
    }
    const auto octave = SynthPlugin::readBrowserState().octave;
    require(octave == 0, "Owned initial keyboard octave must be zero");
    clickComponent(editor, "synth.keyboard.octaveUp");
    require(SynthPlugin::readBrowserState().octave == 1 && keyboard->getLowestVisibleKey() == 48,
            "Actual octave-up button did not share persisted keyboard range");
    for (int i = 0; i < 2; ++i)
        clickComponent(editor, "synth.keyboard.octaveDown");
    require(SynthPlugin::readBrowserState().octave == -1 && keyboard->getLowestVisibleKey() == 24 &&
                !component(editor, "synth.keyboard.octaveDown")->isEnabled(),
            "Lower octave bound is not enforced");
    for (int i = 0; i < 5; ++i)
        clickComponent(editor, "synth.keyboard.octaveUp");
    require(SynthPlugin::readBrowserState().octave == 4 && keyboard->getLowestVisibleKey() == 84 &&
                !component(editor, "synth.keyboard.octaveUp")->isEnabled(),
            "Upper octave bound is not enforced");
    for (int i = 0; i < 4; ++i)
        clickComponent(editor, "synth.keyboard.octaveDown");
    same(snapshot(plugin), beforeNavigation, "Octave navigation transposed/mutated instrument scalars");
    componentCase("ui.keyboard.octave-preferences-and-bounds");
    editor.setSize(760, 540);
    require(editor.getLocalBounds().contains(workspace->getBounds()) && keyboard->getWidth() > 250 &&
                editor.getLocalBounds().contains(editor.getLocalArea(keyboard, keyboard->getLocalBounds())) &&
                workspace->getViewedComponent()->getWidth() > workspace->getViewWidth() &&
                workspace->getViewedComponent()->getHeight() > workspace->getViewHeight(),
            "Minimum editor geometry loses the keyboard or scrollable native workspace");
    workspace->setViewPosition(workspace->getViewedComponent()->getWidth(),
                               workspace->getViewedComponent()->getHeight());
    const auto scrolled = SynthPlugin::readBrowserState();
    require(scrolled.scrollX == workspace->getViewPositionX() &&
                scrolled.scrollY == workspace->getViewPositionY() && scrolled.scrollX > 0 &&
                scrolled.scrollY > 0,
            "Minimum workspace scrolling did not persist its actual clamped position");
    workspace->setViewPosition(0, 0);
    require(search->getWantsKeyboardFocus() && family->getWantsKeyboardFocus() &&
                list->getWantsKeyboardFocus() && previous->getWantsKeyboardFocus() &&
                list->getTooltip().contains("Enter"),
            "Native browser focusability/instruction tooltip contract is missing");
    search->grabKeyboardFocus();
    require(search->hasKeyboardFocus(true), "Native Search did not accept component keyboard focus");
    family->grabKeyboardFocus();
    require(family->hasKeyboardFocus(true) && !search->hasKeyboardFocus(true),
            "Component focus did not transfer from Search to the instrument family");
    list->grabKeyboardFocus();
    require(list->hasKeyboardFocus(true), "Native preset list did not accept component keyboard focus");
    componentCase("ui.layout.minimum-geometry-scroll-focus-tooltips");
    auto externallyChanged = SynthPlugin::readBrowserState();
    externallyChanged.effectPage = 3;
    externallyChanged.octave = 1;
    externallyChanged.scrollX = 50;
    externallyChanged.scrollY = 130;
    externallyChanged.presetScrollY = 110;
    externallyChanged.selectedPresetId = presets[3].id;
    require(SynthPlugin::writeBrowserState(externallyChanged).wasOk(),
            "Cannot publish owned external/shared editor navigation preferences");
    awaitEditor(
        [&]
        {
            return component(editor, "synth.parameter.delay-mix")->isVisible() &&
                   !component(editor, "synth.parameter.eq-low-db")->isVisible() &&
                   keyboard->getLowestVisibleKey() == 48 && workspace->getViewPositionX() == 50 &&
                   workspace->getViewPositionY() == 130 && list->getSelectedRow() == 3 &&
                   list->getViewport()->getViewPositionY() == 110;
        },
        "Live native editor did not adopt shared FX/octave/workspace/list/highlight preferences");
    same(snapshot(plugin), beforeNavigation, "Shared navigation adoption changed native instrument scalars");
    componentCase("ui.navigation.shared-preference-readback");
    clickComponent(editor, "synth.effects.EQ");
    clickComponent(editor, "synth.keyboard.octaveDown");
    workspace->setViewPosition(0, 0);
    editor.setSize(1380, 920);
    same(snapshot(plugin), beforeNavigation,
         "Small geometry/focus/scroll operations changed instrument scalars");
}
void sampledPatchPublication(SynthPlugin &plugin)
{
    const auto presets = factoryPresets();
    const auto sampled = std::find_if(presets.begin(), presets.end(),
                                      [](const FactoryPreset &p) { return p.source == SourceKind::sample; });
    require(sampled != presets.end() && sampled->sampleAsset[0] != '\0',
            "No complete sampled factory patch for native publication acceptance");
    const auto sampledIndex = int(sampled - presets.begin());
    configurePlain(plugin);
    const auto synthesis = snapshot(plugin);
    const auto synthesisIndex = plugin.getCurrentProgram();
    require(presets[std::size_t(synthesisIndex)].sampleAsset[0] == '\0',
            "Invalid-source fixture must use a synthesis-only bank");
    juce::MemoryBlock synthesisState;
    plugin.getStateInformation(synthesisState);
    for (const int source : {1, 2})
    {
        const std::array<std::pair<std::uint32_t, float>, 1> scalar{
            {{SynthPlugin::nativeId(ParameterId::sourceType),
              normalized(ParameterId::sourceType, float(source))}}};
        require(!plugin.validateParameterPatch(scalar),
                "Native batch validator accepted recorded source with a synthesis-only bank");
        same(snapshot(plugin), synthesis, "Rejected batch validation mutated scalar state");
        set(plugin, ParameterId::sourceType, float(source));
        same(snapshot(plugin), synthesis, "Rejected scalar source changed a synthesis-only patch");
        auto invalid = synthesis;
        invalid[parameterIndex(ParameterId::sourceType)] = normalized(ParameterId::sourceType, float(source));
        const auto invalidState = makeSynthNativeState(invalid);
        require(!plugin.restoreState(invalidState.getData(), int(invalidState.getSize())),
                "Native state accepted recorded source with a synthesis-only bank");
        same(snapshot(plugin), synthesis, "Rejected source state partially changed the current patch");
        auto overlaid =
            juce::AudioProcessor::getXmlFromBinary(synthesisState.getData(), int(synthesisState.getSize()));
        auto *sidecar = overlaid->createNewChildElement("VibeRaParameterValues");
        sidecar->setAttribute("version", 1);
        auto *entry = sidecar->createNewChildElement("PARAM");
        entry->setAttribute("id", juce::String(juce::int64(SynthPlugin::nativeId(ParameterId::sourceType))));
        entry->setAttribute("value", double(normalized(ParameterId::sourceType, float(source))));
        const auto invalidSidecar = encode(*overlaid);
        require(!plugin.restoreState(invalidSidecar.getData(), int(invalidSidecar.getSize())),
                "Worker sidecar bypassed the final source/bank compatibility validation");
        same(snapshot(plugin), synthesis, "Rejected source sidecar partially changed the current patch");
    }
    Values sampledValues{};
    std::array<std::pair<std::uint32_t, float>, parameterCount> complete{};
    for (std::size_t index = 0; index < parameterCount; ++index)
    {
        const auto id = static_cast<ParameterId>(index);
        sampledValues[index] =
            normalized(id, id == ParameterId::factoryPreset ? float(sampledIndex) : sampled->values[index]);
        complete[index] = {SynthPlugin::nativeId(id), sampledValues[index]};
    }
    require(plugin.validateParameterPatch(complete), "Complete legal sampled patch was refused");
    for (const int source : {1, 2})
    {
        auto accepted = sampledValues;
        accepted[parameterIndex(ParameterId::sourceType)] =
            normalized(ParameterId::sourceType, float(source));
        const auto bytes = makeSynthNativeState(accepted);
        require(plugin.restoreState(bytes.getData(), int(bytes.getSize())) &&
                    plugin.currentPresetId() == sampled->id,
                "Complete native sampled/hybrid patch retained the previous bank");
        same(snapshot(plugin), accepted, "Complete native sampled patch did not restore atomically");
    }
    const auto sampledState = makeSynthNativeState(sampledValues);
    require(plugin.restoreState(sampledState.getData(), int(sampledState.getSize())),
            "Sample-only reference state restore failed");
    const std::array<std::pair<std::uint32_t, float>, 1> wrongBank{
        {{SynthPlugin::nativeId(ParameterId::factoryPreset),
          normalized(ParameterId::factoryPreset, float(synthesisIndex))}}};
    require(!plugin.validateParameterPatch(wrongBank),
            "Selector-only patch detached an active recorded source from its bank");
    set(plugin, ParameterId::factoryPreset, float(synthesisIndex));
    same(snapshot(plugin), sampledValues, "Rejected selector silently retained an incompatible patch");

    const auto manifest = juce::JSON::parse(SynthPlugin::factoryDirectory().getChildFile("library.json"));
    int note = -1;
    if (const auto *instruments = manifest["instruments"].getArray())
        for (const auto &instrument : *instruments)
            if (instrument["id"].toString() == sampled->sampleAsset)
                if (const auto *regions = instrument["regions"].getArray(); regions && !regions->isEmpty())
                    note = int(regions->getReference(0)["rootNote"]);
    require(note >= 0 && note <= 127, "Recorded publication fixture has no valid actual PCM root note");
    const auto render = [&]
    {
        juce::AudioBuffer<float> result(2, 4096), block(2, 256);
        juce::MidiBuffer midi;
        plugin.prepareToPlay(48000, 256);
        midi.addEvent(juce::MidiMessage::noteOn(1, note, .85f), 17);
        for (int offset = 0; offset < result.getNumSamples(); offset += 256)
        {
            {
                synth_plugin_test::CallbackScope guard;
                plugin.processBlock(block, midi);
            }
            for (int channel = 0; channel < 2; ++channel)
                result.copyFrom(channel, offset, block, channel, 0, 256);
        }
        require(((plugin.telemetry() >> 8) & 0xffffu) == std::uint64_t(sampledIndex) &&
                    ((plugin.telemetry() >> 24) & 0xffu) == 1 && ((plugin.telemetry() >> 33) & 1u) == 1 &&
                    result.getMagnitude(0, 0, result.getNumSamples()) > 1e-6,
                "Actual recorded callback retained the wrong bank/source or produced no PCM");
        return result;
    };
    const auto reference = render();
    configurePlain(plugin);
    plugin.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> prime(2, 256);
    juce::MidiBuffer empty;
    plugin.processBlock(prime, empty);
    require(SynthPlugin::writeBrowserState({}).wasOk(),
            "Cannot clear owned filters for UI preset publication");
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditor());
    auto *list = dynamic_cast<juce::ListBox *>(component(*editor, "synth.browser.presets"));
    require(list && list->getModel() && list->getModel()->getNumRows() == int(presets.size()),
            "Native preset-list UI did not expose the complete unfiltered bank");
    struct PublicationObserver final : juce::AudioProcessorParameter::Listener
    {
        SynthPlugin &plugin;
        juce::AudioBuffer<float> audio{2, 256};
        juce::MidiBuffer midi;
        int previousBank;
        unsigned changes = 0, begins = 0, ends = 0;
        bool sourceObserved = false, bankObserved = false, compatible = true;
        PublicationObserver(SynthPlugin &p, int bank) : plugin(p), previousBank(bank)
        {
            for (auto *parameter : plugin.getParameters())
                parameter->addListener(this);
        }
        ~PublicationObserver() override
        {
            for (auto *parameter : plugin.getParameters())
                parameter->removeListener(this);
        }
        void parameterValueChanged(int index, float) override
        {
            ++changes;
            sourceObserved = sourceObserved || index == int(parameterIndex(ParameterId::sourceType));
            bankObserved = bankObserved || index == int(parameterIndex(ParameterId::factoryPreset));
            {
                synth_plugin_test::CallbackScope guard;
                plugin.processBlock(audio, midi);
            }
            const auto telemetry = plugin.telemetry();
            compatible = compatible && ((telemetry >> 8) & 0xffffu) == std::uint64_t(previousBank) &&
                         ((telemetry >> 24) & 0xffu) == 0;
        }
        void parameterGestureChanged(int, bool starting) override
        {
            if (starting)
                ++begins;
            else
                ++ends;
        }
    };
    {
        PublicationObserver observer(plugin, synthesisIndex);
        list->selectRow(sampledIndex);
        require(list->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),
                "Actual preset-list Return key did not invoke native UI patch loading");
        require(observer.changes > 0 && observer.sourceObserved && observer.bankObserved &&
                    observer.begins == parameterCount && observer.ends == parameterCount &&
                    observer.compatible,
                "UI publication exposed an intermediate source/bank combination or omitted full gestures");
    }
    require(plugin.currentPresetId() == sampled->id, "UI complete patch silently retained the previous bank");
    same(snapshot(plugin), sampledValues, "UI complete patch differs from accepted native sampled state");
    componentCase("ui.browser.return-load-atomic-recorded-patch");
    const auto actual = render();
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < actual.getNumSamples(); ++frame)
            require(std::abs(actual.getSample(channel, frame) - reference.getSample(channel, frame)) < 1e-6,
                    "UI-published recorded bank PCM differs from exact native-state reference");
    require(synth_plugin_test::allocations == 0 && synth_plugin_test::destructions == 0,
            "Native patch publication allocated/destroyed callback heap objects");
}
void editorAndUserPresets(SynthPlugin &plugin, const juce::File &output)
{
    configurePlain(plugin);
    set(plugin, ParameterId::tone, .27f);
    const auto before = snapshot(plugin);
    const auto user = SynthPlugin::userDirectory().getChildFile("Owned acceptance.vrsynth");
    require(plugin.saveUserPreset(user).wasOk() && user.existsAsFile(),
            "Cannot save owned user synth preset");
    SynthUserPreset document;
    require(readSynthUserPreset(user, document).wasOk() && document.metadata.name == "Owned acceptance" &&
                document.metadata.basePreset == plugin.currentPresetId() && document.metadata.id.isNotEmpty(),
            "User preset lacks its own stable identity/name/base metadata");
    same(document.normalizedValues, before, "User wrapper contains exact native scalar targets");
    const auto stableId = document.metadata.id;
    set(plugin, ParameterId::tone, .51f);
    require(plugin.saveUserPreset(user).wasOk() && readSynthUserPreset(user, document).wasOk() &&
                document.metadata.id == stableId,
            "Overwriting a user preset replaced its stable identity");
    set(plugin, ParameterId::tone, .27f);
    require(plugin.saveUserPreset(user).wasOk(), "Cannot restore original owned user patch after overwrite");
    const auto second = SynthPlugin::userDirectory().getChildFile("Second owned patch.vrsynth");
    SynthUserPreset secondDocument;
    require(plugin.saveUserPreset(second).wasOk() && readSynthUserPreset(second, secondDocument).wasOk() &&
                secondDocument.metadata.id != stableId &&
                secondDocument.metadata.name == "Second owned patch",
            "Save As did not create an independently named user preset identity");
    std::vector<SynthUserPresetMetadata> userRows;
    juce::StringArray userErrors;
    require(listSynthUserPresets(SynthPlugin::userDirectory(), userRows, userErrors).wasOk() &&
                userRows.size() == 2 && userErrors.isEmpty(),
            "User preset listing did not return both independently validated patches");
    require(userRows.front().toVar()["factory"].isBool() && !bool(userRows.front().toVar()["factory"]) &&
                userRows.front().toVar()["id"].toString().isNotEmpty(),
            "MCP user listing metadata does not distinguish independent user presets");
    juce::MemoryBlock wrapperBytes;
    require(user.loadFileAsData(wrapperBytes), "Cannot inspect actual saved user wrapper");
    auto wrapper =
        juce::AudioProcessor::getXmlFromBinary(wrapperBytes.getData(), int(wrapperBytes.getSize()));
    require(wrapper && wrapper->hasTagName("VIBERA_SYNTH_PRESET") && wrapper->getChildByName("SOURCE") &&
                wrapper->getChildByName("MACROS") && wrapper->getChildByName("VIBERA_SYNTHESIZER"),
            "Saved user preset omitted its source/macro/native state wrapper");
    wrapper->getChildByName("VIBERA_SYNTHESIZER")->getFirstChildElement()->setAttribute("value", "0.5junk");
    const auto invalidBytes = encode(*wrapper);
    const auto invalidUser = SynthPlugin::userDirectory().getChildFile("malformed-fixture.vrsynth");
    require(invalidUser.replaceWithData(invalidBytes.getData(), invalidBytes.getSize()),
            "Cannot write owned invalid user preset fixture");
    require(readSynthUserPreset(invalidUser, document).failed() && document.metadata.id == stableId,
            "User wrapper accepted malformed native data or partially replaced metadata");
    require(listSynthUserPresets(SynthPlugin::userDirectory(), userRows, userErrors).wasOk() &&
                userRows.size() == 2 && userErrors.size() == 1,
            "User preset listing hid a malformed file instead of reporting its refusal");
    require(plugin.saveUserPreset(SynthPlugin::factoryDirectory().getChildFile("forbidden-user.vrsynth"))
                .failed(),
            "Factory directory accepted a user preset write");
    set(plugin, ParameterId::tone, .9f);
    require(plugin.loadUserPreset(user).wasOk(), "Owned user synth preset failed reopen");
    same(snapshot(plugin), before, "User preset restored exact scalar patch");
    SynthBrowserState browser;
    browser.genre = "Jazz";
    browser.search = "owned-acceptance";
    browser.favouriteIds.add(factoryPresets().front().id);
    require(SynthPlugin::writeBrowserState(browser).wasOk(), "Cannot save owned browser preferences");
    const auto reopened = SynthPlugin::readBrowserState();
    require(reopened.genre == browser.genre && reopened.search == browser.search &&
                reopened.favouriteIds == browser.favouriteIds,
            "Browser favourites/filter did not persist in owned user directory");
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditor());
    // JUCE keyboard hit testing requires a real peer. This is still direct
    // component-handler evidence, not synthetic Windows/physical input.
    require(editor->isOpaque(),
            "Fully painted native synth editor must declare its opaque peer before attachment");
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setTopLeftPosition(40, 40);
    editor->setVisible(true);
    allEditorControls(plugin, *editor);
    editorBrowserAndNavigation(plugin, *editor);
    auto *slider = dynamic_cast<juce::Slider *>(component(*editor, "synth.parameter.tone"));
    require(slider != nullptr, "Original Tone control missing");
    slider->setValue(.81, juce::sendNotificationSync);
    require(std::abs(plugin.parameterForId(SynthPlugin::nativeId(ParameterId::tone))->getValue() - .81f) <
                1e-5,
            "Native Tone control does not share hosted parameter behavior");
    set(plugin, ParameterId::tone, .33f);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500;
    while (std::abs(slider->getValue() - .33) > .001 && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep(10);
    }
    require(std::abs(slider->getValue() - .33) < .001,
            "Host parameter readback did not reach original synth UI");
    componentCase("ui.parameter.host-to-control-readback");
    editorScalarGesturesAndEntry(plugin, *editor);
    auto *keyboard = dynamic_cast<juce::MidiKeyboardComponent *>(component(*editor, "synth.keyboard"));
    require(keyboard != nullptr, "Original native keyboard missing");
    require(editor->isOnDesktop() && editor->getParentComponent() == nullptr && editor->getPeer(),
            "Keyboard component fixture must own its top-level native peer");
#if JUCE_WINDOWS
    {
        const auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle());
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        require(process == GetCurrentProcessId() && GetWindow(window, GW_OWNER) == nullptr,
                "Keyboard component fixture cannot alter a foreign or owned native window");
    }
#endif
    const auto previousTopmost = editor->isAlwaysOnTop();
    const juce::ScopeGuard restoreFixtureVisibility{[&] { editor->setAlwaysOnTop(previousTopmost); }};
    configurePlain(plugin);
    plugin.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> keyboardBlock(2, 512);
    juce::MidiBuffer noNotes;
    plugin.reset();
    plugin.processBlock(keyboardBlock, noNotes);
    const auto key = keyboard->getRectangleForKey(60);
    const juce::Point<float> point(key.getCentreX(), key.getBottom() - 4);
    require(keyboard->getLocalBounds().toFloat().contains(point),
            "Keyboard test point outside actual geometry: " + keyboard->getLocalBounds().toString() +
                " key=" + key.toString());
    const auto localPoint = editor->getLocalPoint(keyboard, point);
    auto *hit = editor->getComponentAt(localPoint);
    require(hit == keyboard,
            "Keyboard pointer target obscured by " +
                (hit ? hit->getComponentID() + " / " + hit->getName() : juce::String("none")));
    const auto keyboardPeerEvidence = [&]
    {
        auto *peer = editor->getPeer();
        const auto peerPoint = editor->getLocalPoint(keyboard, point);
        juce::String result = "local=" + point.toString() + " editorPoint=" + peerPoint.toString() +
                              " screenPoint=" + keyboard->localPointToGlobal(point).toString() +
                              " editorBounds=" + editor->getBounds().toString() +
                              " opaque=" + juce::String(int(editor->isOpaque())) +
                              " showing=" + juce::String(int(editor->isShowing())) +
                              " contains=" + juce::String(int(keyboard->reallyContains(point, false)));
        if (peer != nullptr)
        {
            const auto scale = peer->getPlatformScaleFactor();
            result += " peerBounds=" + peer->getBounds().toString() + " peerScale=" + juce::String(scale) +
                      " masterScale=" + juce::String(juce::Desktop::getInstance().getGlobalScaleFactor());
#if JUCE_WINDOWS
            const auto expected = static_cast<HWND>(peer->getNativeHandle());
            const auto extendedStyle = GetWindowLongPtrW(expected, GWL_EXSTYLE);
            result += " nativeExStyle=" + juce::String::toHexString(juce::int64(extendedStyle)) +
                      " nativeLayered=" + juce::String(int((extendedStyle & WS_EX_LAYERED) != 0)) +
                      " nativeTopmost=" + juce::String(int((extendedStyle & WS_EX_TOPMOST) != 0)) +
                      " nativeVisible=" + juce::String(int(IsWindowVisible(expected) != 0)) +
                      " nativeEnabled=" + juce::String(int(IsWindowEnabled(expected) != 0)) +
                      " nativeDpi=" + juce::String(GetDpiForWindow(expected)) + " threadDpiAwareness=" +
                      juce::String(int(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()))) +
                      " windowDpiAwareness=" +
                      juce::String(
                          int(GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(expected))));
            RECT nativeBounds{};
            if (GetWindowRect(expected, &nativeBounds))
                result += " nativeRect=" + juce::String(nativeBounds.left) + "," +
                          juce::String(nativeBounds.top) + "," + juce::String(nativeBounds.right) + "," +
                          juce::String(nativeBounds.bottom);
            POINT origin{};
            if (ClientToScreen(expected, &origin))
            {
                // This fixture has no host transform or Desktop master scale.
                const POINT probe{origin.x + juce::roundToInt(peerPoint.x * scale),
                                  origin.y + juce::roundToInt(peerPoint.y * scale)};
                const auto observed = WindowFromPoint(probe);
                const auto observedRoot = GetAncestor(observed, GA_ROOT);
                const auto foregroundRoot = GetAncestor(GetForegroundWindow(), GA_ROOT);
                const auto describeRoot = [](HWND window)
                {
                    DWORD process = 0;
                    GetWindowThreadProcessId(window, &process);
                    wchar_t className[256]{};
                    GetClassNameW(window, className, 256);
                    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
                    return "pid=" + juce::String(juce::int64(process)) + " class=" + juce::String(className) +
                           " exStyle=" + juce::String::toHexString(juce::int64(style)) +
                           " visible=" + juce::String(int(IsWindowVisible(window) != 0)) +
                           " enabled=" + juce::String(int(IsWindowEnabled(window) != 0));
                };
                bool observedAbove = false;
                int positions = 0;
                for (auto window = GetWindow(expected, GW_HWNDPREV); window != nullptr && positions < 64;
                     window = GetWindow(window, GW_HWNDPREV), ++positions)
                    if (window == observedRoot)
                    {
                        observedAbove = true;
                        break;
                    }
                result += " observedRoot={" + describeRoot(observedRoot) + "} foregroundRoot={" +
                          describeRoot(foregroundRoot) +
                          "} observedRootAbove=" + juce::String(int(observedAbove)) +
                          " observedRootIsForeground=" + juce::String(int(observedRoot == foregroundRoot)) +
                          " ownIsForeground=" + juce::String(int(expected == foregroundRoot)) +
                          " zOrderProbeCount=" + juce::String(positions);
                DWORD process = 0;
                GetWindowThreadProcessId(observed, &process);
                wchar_t className[256]{};
                GetClassNameW(observed, className, 256);
                result += " nativeProbe=" + juce::String(probe.x) + "," + juce::String(probe.y) +
                          " nativeHitOwnPeer=" + juce::String(int(observed == expected)) +
                          " nativeHitPid=" + juce::String(juce::int64(process)) +
                          " nativeHitClass=" + juce::String(className);
            }
#endif
        }
        return result;
    };
    const auto nativeKeyboardReady = [&]
    {
#if JUCE_WINDOWS
        const auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle());
        if (!IsWindowVisible(window) || !IsWindowEnabled(window) || IsIconic(window) ||
            (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)
            return false;
#endif
        return editor->isAlwaysOnTop() && keyboard->isShowing() && keyboard->reallyContains(point, false);
    };
    const auto showKeyboardFixture = [&]
    {
        const auto before = keyboardPeerEvidence();
        // This console fixture does not request foreground/input ownership.
        // Keep only its verified native peer visible for component callbacks;
        // HWND_TOPMOST uses SWP_NOACTIVATE and is restored on every scope exit.
        editor->setAlwaysOnTop(true);
        editor->toFront(false);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500;
        while (!nativeKeyboardReady() && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep(5);
        }
        const auto after = keyboardPeerEvidence();
        std::cout << "Keyboard peer precondition before: " << before << "\nafter: " << after << '\n';
        require(nativeKeyboardReady(), "Owned keyboard fixture native containment unavailable; before: " +
                                           before + " after: " + after);
    };
    showKeyboardFixture();
    const auto noteInfo = keyboard->getNoteAndVelocityAtPosition(point);
    require(noteInfo.note == 60 && noteInfo.velocity > 0,
            "Keyboard geometry did not resolve actual note60: " + juce::String(noteInfo.note) +
                " velocity=" + juce::String(noteInfo.velocity));
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent press(juce::Desktop::getInstance().getMainMouseSource(), point,
                                 juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                 juce::MouseInputSource::defaultPressure, 0, 0, 0, 0, keyboard, keyboard, now,
                                 point, now, 1, false);
    keyboard->mouseDown(press);
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 1,
            "Component keyboard press did not reach the actual GUI note queue; voices=" +
                juce::String(plugin.activeVoiceCount()) + " bounds=" + keyboard->getLocalBounds().toString() +
                " key=" + key.toString());
    // The pinned Windows peer routes WM_CAPTURECHANGED through synthetic mouseUp.
    keyboard->mouseUp(press.withNewPosition(juce::Point<float>(-10, -10)));
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 0, "Capture-loss mouseUp left a GUI voice held");
    componentCase("ui.keyboard.capture-loss-release");
    keyboard->mouseDown(press);
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 1, "Same key could not be pressed after capture-loss mouseUp");
    editor->setVisible(false);
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 0, "Hiding the editor left a GUI voice held");
    componentCase("ui.keyboard.hide-release");
    editor->setVisible(true);
    showKeyboardFixture();
    keyboard->mouseDown(press);
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 1, "Editor cleanup retained private same-key mouse bookkeeping");
    keyboard->mouseUp(press);
    plugin.processBlock(keyboardBlock, noNotes);
    require(plugin.activeVoiceCount() == 0, "Repeated keyboard lifecycle did not release its GUI voice");
    componentCase("ui.keyboard.same-key-after-cleanup");
    auto *pitch = dynamic_cast<juce::Slider *>(component(*editor, "synth.parameter.pitch-bend"));
    require(pitch && pitch->onDragStart && pitch->onDragEnd,
            "Native spring-return pitch gesture callbacks missing");
    const auto beforePitch = snapshot(plugin);
    {
        ScalarControlObserver observer(plugin, ParameterId::pitchBend);
        pitch->onDragStart();
        pitch->setValue(.65, juce::sendNotificationSync);
        require(std::abs(plugin.parameterForId(SynthPlugin::nativeId(ParameterId::pitchBend))->getValue() -
                         normalized(ParameterId::pitchBend, .65f)) < 2e-6f &&
                    observer.begins == 1 && observer.ends == 0,
                "Pitch drag did not preserve one open hosted parameter gesture");
        pitch->onDragEnd();
        same(snapshot(plugin), beforePitch,
             "Pitch drag release did not spring to centre without neighbour edits");
        require(pitch->getValue() == 0 && observer.begins == 1 && observer.ends == 1 && observer.onlyExpected,
                "Pitch release omitted its centre readback or balanced host gesture");
    }
    componentCase("ui.pitch.drag-release-centres");
    {
        ScalarControlObserver observer(plugin, ParameterId::pitchBend);
        pitch->onDragStart();
        pitch->setValue(-.75, juce::sendNotificationSync);
        keyboard->mouseDown(press);
        plugin.processBlock(keyboardBlock, noNotes);
        require(plugin.activeVoiceCount() == 1, "Focus-loss fixture did not actually hold a GUI voice");
        editor->giveAwayKeyboardFocus();
        editor->focusLost(juce::Component::focusChangedDirectly);
        plugin.processBlock(keyboardBlock, noNotes);
        same(snapshot(plugin), beforePitch, "Editor focus loss failed to centre pitch or edited neighbours");
        require(pitch->getValue() == 0 && observer.begins == 1 && observer.ends == 1 &&
                    observer.onlyExpected && plugin.activeVoiceCount() == 0,
                "Editor focus-loss handler left a pitch gesture or GUI voice held");
        keyboard->mouseDown(press);
        plugin.processBlock(keyboardBlock, noNotes);
        require(plugin.activeVoiceCount() == 1, "Focus-loss cleanup prevented a new press on the same key");
        keyboard->mouseUp(press);
        plugin.processBlock(keyboardBlock, noNotes);
        require(plugin.activeVoiceCount() == 0, "Same-key retry after focus loss left a stuck GUI voice");
    }
    componentCase("ui.pitch.focus-loss-centres-and-releases-gui-notes");
    for (const bool guiFirst : {false, true})
    {
        plugin.reset();
        plugin.processBlock(keyboardBlock, noNotes);
        if (guiFirst)
        {
            keyboard->mouseDown(press);
            plugin.processBlock(keyboardBlock, noNotes);
        }
        noNotes.addEvent(juce::MidiMessage::noteOn(1, 60, .8f), 0);
        plugin.processBlock(keyboardBlock, noNotes);
        if (!guiFirst)
        {
            keyboard->mouseDown(press);
            plugin.processBlock(keyboardBlock, noNotes);
        }
        require(plugin.activeVoiceCount() == 1, "Shared GUI/Host note was duplicated");
        if (guiFirst)
            noNotes.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        else
            keyboard->mouseUp(press);
        plugin.processBlock(keyboardBlock, noNotes);
        require(plugin.activeVoiceCount() == 1, "One GUI/Host owner release cancelled another held note");
        if (guiFirst)
            keyboard->mouseUp(press);
        else
            noNotes.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        plugin.processBlock(keyboardBlock, noNotes);
        require(plugin.activeVoiceCount() == 0, "Final GUI/Host owner release left a stuck note");
        componentCase(guiFirst ? "ui.keyboard.gui-first-host-owner-preserved"
                               : "ui.keyboard.host-first-gui-owner-preserved");
    }
    {
        // External (host-routed hardware) MIDI keys are published for the editor
        // keyboard highlight; editor-keyboard notes are not mirrored twice.
        juce::MidiBuffer hostNotes;
        hostNotes.addEvent(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 0);
        plugin.processBlock(keyboardBlock, hostNotes);
        require(plugin.hostNoteHeld(64) && !plugin.hostNoteHeld(60),
                "External MIDI key was not published for the editor keyboard");
        hostNotes.clear();
        hostNotes.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        plugin.processBlock(keyboardBlock, hostNotes);
        require(!plugin.hostNoteHeld(64), "External MIDI key release was not published");
        hostNotes.clear();
        hostNotes.addEvent(juce::MidiMessage::noteOn(1, 67, juce::uint8(90)), 0);
        hostNotes.addEvent(juce::MidiMessage::allNotesOff(1), 1);
        plugin.processBlock(keyboardBlock, hostNotes);
        require(!plugin.hostNoteHeld(67), "All-notes-off left an external key highlighted");
        // Output make-up is audible, and the safety limiter holds the -1 dBFS ceiling
        // for a dense full-velocity chord at maximum Master.
        set(plugin, ParameterId::masterDb, 6);
        hostNotes.clear();
        for (int note = 48; note < 72; note += 2)
            hostNotes.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(127)), 0);
        float loudest = 0;
        for (int block = 0; block < 16; ++block)
        {
            plugin.processBlock(keyboardBlock, hostNotes);
            hostNotes.clear();
            loudest = std::max(loudest, keyboardBlock.getMagnitude(0, keyboardBlock.getNumSamples()));
        }
        require(loudest > .5f && loudest <= .8913f,
                "Output make-up/limiter ceiling is wrong: peak " + juce::String(loudest));
        hostNotes.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        plugin.processBlock(keyboardBlock, hostNotes);
        set(plugin, ParameterId::masterDb, 0);
    }
    for (const auto scale : {1.f, 1.5f})
    {
        editor->setSize(scale == 1.f ? 1600 : 1280, scale == 1.f ? 900 : 720);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
        const auto name = scale == 1.f ? "synth-editor-100.png" : "synth-editor-150.png";
        auto stream = output.getChildFile(name).createOutputStream();
        require(stream && stream->setPosition(0), "Cannot create owned synth UI capture");
        stream->truncate();
        require(juce::PNGImageFormat().writeImageToStream(image, *stream), "Cannot write synth UI capture");
        componentCase(scale == 1.f ? "ui.render.component-100-percent" : "ui.render.component-150-percent");
    }
    // Lower sections (filter, envelopes, voice, effects) for visual review.
    if (auto *workspace = dynamic_cast<juce::Viewport *>(component(*editor, "synth.workspaceScroll")))
    {
        editor->setSize(1600, 900);
        workspace->setViewPosition(0, workspace->getViewedComponent()->getHeight());
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.f);
        auto stream = output.getChildFile("synth-editor-lower.png").createOutputStream();
        require(stream && stream->setPosition(0), "Cannot create owned lower synth UI capture");
        stream->truncate();
        require(juce::PNGImageFormat().writeImageToStream(image, *stream),
                "Cannot write lower synth UI capture");
        workspace->setViewPosition(0, 0);
    }
}
} // namespace
int main(int argc, char **argv)
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    const auto began = std::chrono::steady_clock::now();
    juce::File output;
    try
    {
        require(argc == 2, "Owned build/windows acceptance output required");
        output = juce::File(juce::String::fromUTF8(argv[1]));
        require(juce::File::isAbsolutePath(output.getFullPathName()) && output.createDirectory().wasOk(),
                "Invalid acceptance output");
        OwnedUserDirectory owned(
            output.getChildFile("user-data").getChildFile("attempt-" + juce::Uuid().toString()));
        const auto directory = SynthPlugin::factoryDirectory();
        require(directory.getChildFile("library.json").existsAsFile(),
                "Factory Content/Synthesizer must accompany exact native test executable");
        const auto files = directory.findChildFiles(juce::File::findFiles, true);
        std::vector<std::pair<juce::File, std::uint64_t>> identities;
        for (const auto &file : files)
            identities.emplace_back(file, byteIdentity(file));
        const auto manifestFailures = factoryManifestFailures();
        SynthPlugin plugin;
        require(plugin.contentError().isEmpty(),
                "Factory recorded content failed real preload: " + plugin.contentError());
        parametersAndState(plugin);
        timedEventsAndPanic(plugin);
        sampledPatchPublication(plugin);
        const auto pcm = factoryPcm(plugin);
        const auto performance = callbackPerformance(plugin);
        require(output.getChildFile("performance.json").replaceWithText(juce::JSON::toString(performance)),
                "Cannot retain callback measurements");
        masterAttenuatesExistingTails(plugin);
        editorAndUserPresets(plugin, output);
        const auto finalFiles = directory.findChildFiles(juce::File::findFiles, true);
        require(finalFiles.size() == files.size(), "Factory directory file inventory changed");
        for (const auto &identity : identities)
            require(byteIdentity(identity.first) == identity.second,
                    "Factory file changed: " + identity.first.getFullPathName());
        auto *receipt = new juce::DynamicObject;
        receipt->setProperty("passed", true);
        receipt->setProperty("checks", int(checks));
        receipt->setProperty("componentCaseIds", componentCases());
        receipt->setProperty("componentScope",
                             "Actual native component callbacks, peer-backed keyboard hit "
                             "testing, parameter notifications/readback, focus handlers and "
                             "component renders; no injected OS input or physical interaction");
        receipt->setProperty("componentPending",
                             juce::var(juce::Array<juce::var>{"ui.userPreset.native-save-chooser",
                                                              "ui.userPreset.native-load-chooser",
                                                              "ui.userPreset.native-chooser-cancellation"}));
        receipt->setProperty("componentLimitations",
                             "Save/load native chooser interaction is unqualified here; "
                             "owned files/codecs and semantic MCP cover writes separately. "
                             "Native OS input, human listening and hardware remain separate "
                             "from component focus/callback/render evidence.");
        receipt->setProperty("factoryPcm", pcm);
        receipt->setProperty("performance", performance);
        receipt->setProperty("factoryManifestFailures", manifestFailures);
        receipt->setProperty("factoryFilesUnchanged", int(identities.size()));
        receipt->setProperty("callbackCppAllocations", int(synth_plugin_test::allocations));
        receipt->setProperty("callbackCppDestructions", int(synth_plugin_test::destructions));
        receipt->setProperty("seconds",
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
        receipt->setProperty("evidence", "Actual native adapter, pinned recorded factory samples, oscillator "
                                         "PCM, scalar/state/events/user-preset and component-render checks; "
                                         "no OS input, hearing or physical hardware claim");
        require(output.getChildFile("verification.json")
                    .replaceWithText(juce::JSON::toString(juce::var(receipt))),
                "Cannot save synth acceptance receipt");
        std::cout << "PASS native synthesizer; " << checks << " checks; factory patches=" << pcm.size()
                  << "; callback C++ heap=" << synth_plugin_test::allocations << "/"
                  << synth_plugin_test::destructions << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        if (output.isDirectory())
        {
            auto *receipt = new juce::DynamicObject;
            receipt->setProperty("passed", false);
            receipt->setProperty("checks", int(checks));
            receipt->setProperty("error", error.what());
            receipt->setProperty("componentCaseIds", componentCases());
            receipt->setProperty(
                "seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
            if (!output.getChildFile("verification.json")
                     .replaceWithText(juce::JSON::toString(juce::var(receipt))))
                std::cerr << "Cannot retain failed synthesizer verification receipt\n";
        }
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
