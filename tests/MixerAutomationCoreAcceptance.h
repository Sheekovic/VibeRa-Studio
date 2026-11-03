// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "AutomationGain.h"
#include "AutomationState.h"
#include "Customization.h"
#include "MixerAutomationTarget.h"
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <iostream>

// Include in the existing VibeRaTests TU, after require/AudioEngine/te aliases.
// Original source-only tests. Direct adapter processing is not whole-graph,
// worker, physical hardware, listening, concurrency or semantic MCP evidence.
namespace vibera::test::mixer_automation_core
{
inline constexpr AutomationKey pan{AutomationParameter::mixerPan, 1, 0};
inline constexpr AutomationKey separation{AutomationParameter::mixerStereoSeparation, 1, 0};
inline void model(const juce::File &folder)
{
    static_assert(
        unsigned(AutomationParameter::mixerGain) == 0 && unsigned(AutomationParameter::mixerPan) == 1 &&
        unsigned(AutomationParameter::channelGain) == 2 && unsigned(AutomationParameter::channelPan) == 3 &&
        unsigned(AutomationParameter::pluginNormalized) == 4 &&
        unsigned(AutomationParameter::mixerStereoSeparation) == 5);
    Controller c;
    const auto baseline = c.project.state();
    CommandArgs create;
    create.mixer = 1;
    create.automationControl = MixerAutomationControl::pan;
    create.beat = 2;
    create.length = 4;
    create.lane = 2;
    require(c.execute("automation.create", create).wasOk(), "Typed pan source creation");
    const auto &source = c.project.automation.sources.front();
    const auto sourceId = source.id;
    require(source.links.front().key == pan && source.links.front().minimum == -1 &&
                source.links.front().maximum == 1 && source.points.front().value == .5f &&
                source.points.back().value == .5f && c.project.automation.initialValues.front().value == 0,
            "Bipolar centre maps to normalized0.5 and captures manual initialization");
    auto created = c.project.state();
    require(c.undoEditChecked().wasOk() && baseline.isEquivalentTo(c.project.state()),
            "Create is one exact Undo");
    require(c.redoEditChecked().wasOk() && created.isEquivalentTo(c.project.state()), "Create exact Redo");
    CommandArgs link;
    link.target = sourceId;
    link.mixer = 1;
    link.automationControl = MixerAutomationControl::stereoSeparation;
    require(c.execute("automation.link", link).wasOk(),
            "Same normalized source links to separate stereo target");
    require(c.project.automation.sources.front().links.size() == 2 &&
                c.project.automation.initialValues.size() == 2,
            "Distinct kind/owner keys never alias");
    link.beat = 1;
    link.length = -1;
    require(c.execute("automation.range", link).wasOk(), "Inverted bipolar separation range");
    const auto intact = c.project.state();
    const auto revision = c.revision;
    link.length = 1.0001;
    require(c.execute("automation.range", link).failed() && c.revision == revision &&
                intact.isEquivalentTo(c.project.state()),
            "Out-of-domain typed range fails without mutation/history");
    link.automationControl = static_cast<MixerAutomationControl>(7);
    require(c.execute("automation.link", link).failed() && intact.isEquivalentTo(c.project.state()),
            "Unassigned target control7 remains explicitly unsupported");
    link.automationControl = MixerAutomationControl::pan;
    require(c.execute("project.tempo", link).failed() && intact.isEquivalentTo(c.project.state()),
            "Typed target metadata cannot be ignored by unrelated command");
    const auto file = folder.getChildFile("pan-separation-22.vibra");
    require(!file.exists() && saveProject(c.project, file).wasOk(), "Owned new schema22 project save");
    Project restored;
    require(loadProject(file, restored).wasOk() && intact.isEquivalentTo(restored.state()),
            "Disk round trip keeps key kind, owner, parameter, ranges, points and init values");
    auto malformed = intact.createCopy();
    malformed.getChildWithName("AUTOMATION")
        .getChildWithName("SOURCES")
        .getChild(0)
        .getChildWithName("LINKS")
        .getChild(0)
        .setProperty("kind", 7, nullptr);
    require(Project::parse(malformed, restored).failed() && intact.isEquivalentTo(restored.state()),
            "Unassigned future key cannot destroy existing destination Project");
    for (int version : {20, 21})
    {
        auto old = Project::empty().state();
        old.setProperty("version", version, nullptr);
        stripPatternExtentForLegacy(old);
        Project migrated;
        require(Project::parse(old, migrated).wasOk() && migrated.version == Project::currentVersion &&
                    migrated.automation.sources.empty() && migrated.automation.initialValues.empty(),
                "Genuine default old schema migrates without synthesizing automation");
        old = intact.createCopy();
        old.setProperty("version", version, nullptr);
        require(Project::parse(old, restored).failed() && intact.isEquivalentTo(restored.state()),
                "New pan/separation keys masquerading as old schema are refused atomically");
    }
    auto unsupported = c.project;
    unsupported.automation.initialValues.push_back(
        {{AutomationParameter::channelPan, unsupported.channels.front().id, 0}, 0});
    require(unsupported.validate().failed(), "Channel targets remain outside this Mixer batch");
    // Initialization deliberately survives source/placement deletion; removing
    // it as well restores current manual fallback, not a stale held endpoint.
    CommandArgs remove;
    remove.target = sourceId;
    require(c.execute("automation.source.delete", remove).wasOk(), "Delete source and placements");
    AutomationProgram program;
    std::string error;
    require(AutomationProgram::compile(c.project.automation, true, program, error,
                                       c.project.automationDefaults()) &&
                program.find(pan) && program.valueAt(pan, 100) == 0.f,
            "Initial-only Song lane survives clip deletion");
    remove.mixer = 1;
    remove.automationControl = MixerAutomationControl::pan;
    require(c.execute("automation.init.remove", remove).wasOk(), "Explicitly remove typed initialization");
    require(AutomationProgram::compile(c.project.automation, true, program, error,
                                       c.project.automationDefaults()) &&
                !program.find(pan),
            "No source/init leaves pan manual rather than held automation");
    require(AutomationProgram::compile(c.project.automation, false, program, error,
                                       c.project.automationDefaults()) &&
                program.targetCount() == 0,
            "Existing Pattern mode remains manual-only");
    Controller macroController;
    Customization custom;
    CommandArgs init;
    init.mixer = 1;
    init.value = -.5;
    init.automationControl = MixerAutomationControl::pan;
    require(
        custom.addMacro({"macro.original-pan", "Original typed pan init", {{"automation.init", init, true}}})
            .wasOk(),
        "Typed automation macro registered through actual Customization API");
    const auto profile = custom.state();
    require(int(profile["version"]) == 4,
            "Typed macro uses profile4 so older readers cannot run gain by mistake");
    Customization decoded;
    require(decoded.importState(profile).wasOk() &&
                decoded.macros.front().steps.front().parameters.automationControl ==
                    MixerAutomationControl::pan,
            "Typed target survives actual macro serialization");
    MacroContext context;
    context.insert = 3;
    auto macroBefore = macroController.project.state();
    require(decoded.runMacro("macro.original-pan", macroController, context).wasOk() &&
                macroController.project.automation.initialValues.front().key ==
                    AutomationKey{AutomationParameter::mixerPan, 3, 0} &&
                macroController.project.automation.initialValues.front().value == -.5f,
            "Current-context macro targets selected stable insert and explicit pan kind");
    require(macroController.undoEditChecked().wasOk() &&
                macroBefore.isEquivalentTo(macroController.project.state()),
            "Whole typed macro has one exact Undo");
    auto oldProfile = profile.clone();
    oldProfile.getDynamicObject()->setProperty("version", 3);
    const auto decodedBefore = juce::JSON::toString(decoded.state());
    require(decoded.importState(oldProfile).failed() &&
                juce::JSON::toString(decoded.state()) == decodedBefore,
            "New control under old macro profile fails without overwriting settings");
    Controller gestured;
    const auto gestureBefore = gestured.project.state();
    init.mixer = 1;
    require(gestured.beginGesture("original-pan-owner"), "Acquire actual canonical automation gesture");
    for (const auto value : {-.5, .25, .75})
    {
        init.value = value;
        require(gestured.execute("automation.init", init, "original-pan-owner").wasOk(),
                "Owned typed automation updates");
    }
    require(gestured.endGestureChecked("Original pan initialization", "original-pan-owner").wasOk(),
            "Actual checked typed gesture commits once");
    const auto gestureAfter = gestured.project.state();
    require(gestured.undoEditChecked().wasOk() && gestureBefore.isEquivalentTo(gestured.project.state()),
            "One Undo removes every intermediate initialization update");
    require(gestured.redoEditChecked().wasOk() && gestureAfter.isEquivalentTo(gestured.project.state()),
            "One Redo restores final initialization only");
    require(gestured.beginGesture("original-cancel-owner"), "Acquire cancellable target gesture");
    init.value = -.25;
    require(gestured.execute("automation.init", init, "original-cancel-owner").wasOk() &&
                gestured.cancelGestureChecked("original-cancel-owner").wasOk() &&
                gestureAfter.isEquivalentTo(gestured.project.state()),
            "Exact checked cancel restores initialized state");
}

inline void stereoPrimitive()
{
    // Pure literal separation+orientation oracle. No production matrix used.
    for (const double rate : {44100., 48000.})
    {
        dsp::PreparedStereo processor;
        require(processor.prepare(rate, {0, false, false}), "Original stereo primitive preparation");
        require(processor.setTarget({.5f, true, true}), "Manual fallback and discrete flags target");
        const int ramp = int(rate * .005);
        for (int n = 0; n < ramp + 21; ++n)
        {
            if (n == 7)
            {
                float left = 1, right = -.5f;
                require(!processor.processAutomated(left, right, 2) && left == 1 && right == -.5f,
                        "Mid-ramp invalid value is refused before PCM or either clock changes");
            }
            const float s = -1.f + float(n % 65) / 32.f;
            const double own = s > 0 ? 1 - .5 * s : 1, cross = s > 0 ? .5 * s : s;
            const double l = own - .5 * cross, r = cross - .5 * own;
            const double t = std::min(1., double(n + 1) / ramp);
            const double expectedL = (1 - t) * l - t * r, expectedR = -t * l + (1 - t) * r;
            float left = 1, right = -.5f;
            require(processor.processAutomated(left, right, s) && std::abs(left - expectedL) < 2e-5 &&
                        std::abs(right - expectedR) < 2e-5,
                    "All samples: immediate automated separation composed with exact5ms swap/polarity ramp");
        }
        float left = 1, right = -.5f;
        processor.process(left, right);
        // Manual .5 gives .625,-.125; fully inverted/swapped gives .125,-.625.
        require(left == .125f && right == -.625f, "Manual matrix clock advanced underneath automation");
        left = 1;
        right = -.5f;
        require(!processor.processAutomated(left, right, std::numeric_limits<float>::quiet_NaN()) &&
                    left == 1 && right == -.5f,
                "Invalid automated value leaves PCM and ramp untouched");
    }
    dsp::PreparedStereo interrupted;
    require(interrupted.prepare(48000, {0, false, false}) && interrupted.setTarget({0, true, true}),
            "Interrupted orientation transition starts from literal identity");
    double own = 1, cross = 0, startOwn = 1, startCross = 0, goalOwn = 0, goalCross = -1;
    int elapsed = 0;
    for (int n = 0; n < 360; ++n)
    {
        if (n == 73)
        {
            require(interrupted.setTarget({0, true, false}),
                    "Retarget orientation during an active5ms transition");
            startOwn = own;
            startCross = cross;
            goalOwn = -1;
            goalCross = 0;
            elapsed = 0;
        }
        const double t = std::min(1., double(++elapsed) / 240.);
        own = startOwn + (goalOwn - startOwn) * t;
        cross = startCross + (goalCross - startCross) * t;
        const float s = float(n % 33) / 16.f - 1;
        const double a = s > 0 ? 1 - .5 * s : 1, b = s > 0 ? .5 * s : s;
        const double sourceL = a - .5 * b, sourceR = b - .5 * a;
        float l = 1, r = -.5f;
        require(interrupted.processAutomated(l, r, s) &&
                    std::abs(l - (own * sourceL + cross * sourceR)) < 2e-5 &&
                    std::abs(r - (cross * sourceL + own * sourceR)) < 2e-5,
                "Every separation/flag sample retains current orientation when interrupted, without restart "
                "or snap");
    }
}

struct AdapterFixture
{
    AudioEngine audio{false};
    Project project = Project::empty();
    std::unique_ptr<te::Edit> edit;
    te::Plugin::Ptr plugin;
    AutomationGain *adapter = nullptr;
    std::shared_ptr<LiveTimeline> timeline = std::make_shared<LiveTimeline>();
    std::uint64_t generation = 0;
    explicit AdapterFixture(AutomationKey key, bool pattern, double rate = 48000)
    {
        project.tempo = 120;
        auto &insert = project.mixer[1];
        insert.gain = 1;
        insert.pan = 0;
        insert.stereoSeparation = 0;
        const double length = 256. / 24000.;
        AutomationSource source;
        source.id = project.allocate();
        source.length = length;
        source.points = {{project.allocate(), 0, 0}, {project.allocate(), length, 1}};
        source.links = {{key, -1, 1}};
        project.automation.sources.push_back(source);
        project.automation.initialValues.push_back({key, 0});
        project.automation.placements.push_back({project.allocate(), source.id, 0, length, 0, 0, false});
        require(project.validate().wasOk(), "Original literal automation Project valid");
        juce::String error;
        edit = audio.buildEdit(project, pattern, project.patterns.front().id, te::Edit::forRendering, error);
        require(bool(edit), "Actual native Edit prepared for original adapter component");
        publish(pattern);
        plugin = edit->getPluginCache().createNewPlugin(AutomationGain::xmlTypeName, {});
        adapter = dynamic_cast<AutomationGain *>(plugin.get());
        require(adapter != nullptr, "Actual registered AutomationGain adapter");
        adapter->configure(timeline, insert, te::PanLawLinear);
        plugin->baseClassInitialise({{}, rate, 256});
    }
    void publish(bool pattern)
    {
        auto candidate = compileLiveDocument(project, pattern, project.patterns.front().id, ++generation);
        require(candidate && timeline->publish(candidate),
                "Publish actual compiled immutable automation document");
    }
    ~AdapterFixture()
    {
        if (plugin && !plugin->baseClassNeedsInitialising())
            plugin->baseClassDeinitialise();
        plugin = nullptr; // Release cache-external handle before Edit/AudioEngine destruction.
    }
};
__declspec(noinline) inline void adapterSamples(AutomationKey key, bool pattern, bool playing, bool rendering)
{
    auto fixture = std::make_unique<AdapterFixture>(key, pattern);
    juce::AudioBuffer<float> pcm(2, 271);
    constexpr int prefix = 7;
    int cursor = 0;
    for (int frames : {64, 1, 127, 64, 17})
    {
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < pcm.getNumSamples(); ++n)
                pcm.setSample(c, n, n >= prefix && n < prefix + frames ? (c == 0 ? 1.f : -.5f) : .125f);
        const te::PluginRenderContext context(&pcm, prefix, frames, nullptr, 0,
                                              {te::TimePosition::fromSeconds(cursor / 48000.),
                                               te::TimePosition::fromSeconds((cursor + frames) / 48000.)},
                                              playing, false, rendering, false);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            fixture->adapter->applyToBuffer(context);
        }
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Original adapter component has zero observed C++ allocation/destruction");
        for (int n = 0; n < pcm.getNumSamples(); ++n)
        {
            const bool inside = n >= prefix && n < prefix + frames;
            double left = .125, right = .125;
            if (inside)
            {
                left = 1;
                right = -.5;
                if (!pattern && (playing || rendering))
                {
                    const double value = -1 + std::min(256, cursor + n - prefix) / 128.;
                    if (key.kind == AutomationParameter::mixerPan)
                    {
                        left = 1 - value;
                        right = -.5 * (1 + value);
                    }
                    else if (value <= 0)
                    {
                        left = 1 - .5 * value;
                        right = -.5 + value;
                    }
                    else
                    {
                        left = 1 - .75 * value;
                        right = -.5 + .75 * value;
                    }
                }
            }
            const bool exact = std::abs(pcm.getSample(0, n) - left) <= (inside ? 2e-6 : 0) &&
                               std::abs(pcm.getSample(1, n) - right) <= (inside ? 2e-6 : 0);
            if (!exact)
                std::cerr << "automation adapter kind=" << unsigned(key.kind)
                          << " frame=" << cursor + n - prefix << " pattern=" << pattern
                          << " playing=" << playing << " rendering=" << rendering
                          << " actual=" << pcm.getSample(0, n) << ',' << pcm.getSample(1, n)
                          << " expected=" << left << ',' << right << std::endl;
            require(
                exact,
                "Every adapter sample and outside-prefix sentinel follows independent absolute-clock oracle");
        }
        cursor += frames;
    }
}
__declspec(noinline) inline void fallback(AutomationKey key)
{
    auto fixture = std::make_unique<AdapterFixture>(key, false);
    if (key == pan)
        fixture->project.mixer[1].pan = 1;
    else
        fixture->project.mixer[1].stereoSeparation = .5f;
    fixture->publish(false);
    juce::AudioBuffer<float> buffer(2, 12);
    for (int c = 0; c < 2; ++c)
        std::fill_n(buffer.getWritePointer(c), 12, c == 0 ? 1.f : -.5f);
    const te::PluginRenderContext active(
        &buffer, 0, 12, nullptr, 0,
        {te::TimePosition::fromSeconds(0), te::TimePosition::fromSeconds(12. / 48000.)}, true, false, false,
        false);
    fixture->adapter->applyToBuffer(active);
    // At this point the manual ramp has advanced12 frames beneath automation.
    buffer.setSample(0, 0, 1);
    buffer.setSample(1, 0, -.5f);
    const te::PluginRenderContext stopped(
        &buffer, 0, 1, nullptr, 0,
        {te::TimePosition::fromSeconds(12. / 48000.), te::TimePosition::fromSeconds(13. / 48000.)}, false,
        false, false, false);
    fixture->adapter->applyToBuffer(stopped);
    const double t = key == pan ? 13. / 720. : 13. / 240.;
    const double l = key == pan ? 1 - t : 1 - .375 * t;
    const double r = key == pan ? -.5 * (1 + t) : -.5 + .375 * t;
    require(std::abs(buffer.getSample(0, 0) - l) <= 5e-5 && std::abs(buffer.getSample(1, 0) - r) <= 5e-5,
            "Stop reveals already-advanced manual ramp, not frozen or reset control state");
}
__declspec(noinline) inline void offline(const juce::File &folder)
{
    // This independent whole-file export oracle requires the actual existing
    // linear pan law; it never changes process/global/user settings to force it.
    require(te::getDefaultPanLaw() == te::PanLawLinear,
            "Original offline fixture requires existing linear pan law");
    const auto sample = folder.getChildFile("original-signed-bed.wav");
    require(!sample.exists(), "Owned original WAV never overwrites an existing sample");
    {
        juce::AudioBuffer<float> data(2, 48000 * 4);
        std::fill_n(data.getWritePointer(0), data.getNumSamples(), .125f);
        std::fill_n(data.getWritePointer(1), data.getNumSamples(), -.0625f);
        auto stream = sample.createOutputStream();
        require(bool(stream), "Original WAV stream");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), 48000., 2, 24, {}, 0));
        if (writer)
            (void)stream.release(); // Factory failure/exception retains CONTROL stream ownership.
        require(writer && writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples()),
                "Write exactly representable PCM24 input");
    }
    auto project = Project::empty();
    project.channels.resize(1);
    auto &channel = project.channels.front();
    channel.source = sample.getFullPathName();
    channel.mixer = 1;
    channel.gain = 1;
    channel.pan = 0;
    channel.sampler.root = 60;
    channel.sampler.envelope = false;
    channel.sampler.normalize = false;
    channel.sampler.loop = false;
    project.tempo = 120;
    project.patterns.front().length = 4;
    project.patterns.front().notes.push_back({project.allocate(), channel.id, 0, 4, 60, 1, false});
    project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
    for (auto &insert : project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
    }
    const auto baselineFile = folder.getChildFile("identity.wav");
    auto renderer = std::make_unique<AudioEngine>(false);
    require(!baselineFile.exists() && renderer->renderSync(project, baselineFile).wasOk(),
            "Actual unchanged baseline offline WAV export");
    double rate = 0;
    const auto baseline = readWav(baselineFile, rate);
    require(rate == 48000 && baseline.getNumChannels() == 2 && baseline.getNumSamples() > 256 &&
                baseline.getMagnitude(0, baseline.getNumSamples()) > .01f,
            "Genuine nonzero signed stereo baseline, not a silent relational oracle");
    for (const auto key : {pan, separation})
    {
        auto candidate = project;
        AutomationSource source;
        source.id = candidate.allocate();
        source.length = 4;
        source.points = {{candidate.allocate(), 0, 0}, {candidate.allocate(), 4, 1}};
        source.links = {{key, -1, 1}};
        candidate.automation.sources.push_back(source);
        candidate.automation.initialValues.push_back({key, 0});
        candidate.automation.placements.push_back({candidate.allocate(), source.id, 0, 4, 0, 1, false});
        const auto file = folder.getChildFile(key == pan ? "pan.wav" : "separation.wav");
        require(!file.exists() && renderer->renderSync(candidate, file).wasOk(),
                "Actual typed automation whole-file WAV export");
        double sampleRate = 0;
        const auto actual = readWav(file, sampleRate);
        require(sampleRate == rate && actual.getNumChannels() == 2 &&
                    actual.getNumSamples() == baseline.getNumSamples(),
                "Complete offline extent and channel/rate identity");
        for (int frame = 0; frame < actual.getNumSamples(); ++frame)
        {
            const double value = -1 + std::min(1., double(frame) / 96000.) * 2;
            const double l = baseline.getSample(0, frame), r = baseline.getSample(1, frame);
            double expectedL, expectedR;
            if (key == pan)
            {
                expectedL = l * (1 - value);
                expectedR = r * (1 + value);
            }
            else
            {
                const double own = value > 0 ? 1 - .5 * value : 1, cross = value > 0 ? .5 * value : value;
                expectedL = own * l + cross * r;
                expectedR = cross * l + own * r;
            }
            // Four PCM24 counts account for two independent file quantizations
            // plus bounded float coefficient/multiply roundoff. No phase fit,
            // discarded attack/release samples or observed-output calibration.
            require(std::isfinite(actual.getSample(0, frame)) && std::isfinite(actual.getSample(1, frame)) &&
                        std::abs(actual.getSample(0, frame) - expectedL) <= 4. / 8388608. &&
                        std::abs(actual.getSample(1, frame) - expectedR) <= 4. / 8388608.,
                    "Every exported sample follows literal musical-clock automation matrix");
        }
    }
}
inline void tests(const juce::File &freshFolder)
{
    require(!freshFolder.exists() && freshFolder.createDirectory().wasOk(),
            "Fresh owned automation core output");
    model(freshFolder);
    stereoPrimitive();
    for (const auto key : {pan, separation})
    {
        for (bool pattern : {true, false})
            for (int mode : {0, 1, 2})
                adapterSamples(key, pattern, mode == 1, mode == 2);
        fallback(key);
    }
    offline(freshFolder);
}
} // namespace vibera::test::mixer_automation_core
