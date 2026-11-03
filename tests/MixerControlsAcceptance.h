// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/MixerControls.h"
#include "PreparedBalanceAcceptance.h"
#include "AutomationGain.h"
#include "ChannelBalance.h"
#include "MixerOverloadAcceptance.h"
static void balanceReprepareTests()
{
    AudioEngine audio(false);
    auto project = Project::empty();
    const auto pattern = project.patterns.front().id;
    juce::String error;
    auto edit = audio.buildEdit(project, true, pattern, te::Edit::forRendering, error);
    require(bool(edit), "Balance lifecycle fixture Edit");
    for (const int insert : {0, 1})
    {
        auto timeline = std::make_shared<LiveTimeline>();
        auto plugin = edit->getPluginCache().createNewPlugin(AutomationGain::xmlTypeName, {});
        auto *gain = dynamic_cast<AutomationGain *>(plugin.get());
        gain->configure(timeline, project.mixer[size_t(insert)], te::PanLawLinear);
        plugin->baseClassInitialise({{}, 48000, 256});
        auto candidate = project;
        candidate.mixer[size_t(insert)].gain = .25f;
        candidate.mixer[size_t(insert)].pan = 1;
        auto document = compileLiveDocument(candidate, true, pattern, 1);
        require(timeline->publish(document), "Publish unconsumed latest Mixer lifecycle targets");
        plugin->baseClassDeinitialise();
        for (const double rate : {44100., 44100., 48000.})
        {
            plugin->baseClassInitialise({{}, rate, 128});
            juce::AudioBuffer<float> pcm(2, 8);
            for (int frame = 0; frame < 8; ++frame)
            {
                pcm.setSample(0, frame, 1);
                pcm.setSample(1, frame, .25f);
            }
            const te::PluginRenderContext context(&pcm, 0, 8, nullptr, 0, {}, false, false, false, false);
            gain->applyToBuffer(context);
            for (int frame = 0; frame < 8; ++frame)
                require(std::abs(pcm.getSample(0, frame)) < 1.e-7f &&
                            std::abs(pcm.getSample(1, frame) - .125f) < 1.e-6f,
                        "Fresh/rate-reprepared Mixer and Master start at current gain/pan from first sample");
            require(gain->meterReading().peak[0] < 1.e-7f &&
                        std::abs(gain->meterReading().peak[1] - .125f) < 1.e-6f,
                    "Reprepared own meter follows first actual pan/gain PCM");
            plugin->baseClassDeinitialise();
        }
    }
    auto timeline = std::make_shared<LiveTimeline>();
    auto plugin = edit->getPluginCache().createNewPlugin(ChannelBalance::xmlTypeName, {});
    auto *balance = dynamic_cast<ChannelBalance *>(plugin.get());
    balance->configure(timeline, project.channels.front(), te::PanLawLinear);
    plugin->baseClassInitialise({{}, 48000, 256});
    auto candidate = project;
    candidate.channels.front().gain = .25f;
    candidate.channels.front().pan = 1;
    auto document = compileLiveDocument(candidate, true, pattern, 2);
    require(timeline->publish(document), "Publish current Channel lifecycle target");
    plugin->baseClassDeinitialise();
    for (const double rate : {44100., 44100., 48000.})
    {
        plugin->baseClassInitialise({{}, rate, 128});
        juce::AudioBuffer<float> pcm(2, 8);
        for (int frame = 0; frame < 8; ++frame)
        {
            pcm.setSample(0, frame, 1);
            pcm.setSample(1, frame, .25f);
        }
        const te::PluginRenderContext context(&pcm, 0, 8, nullptr, 0, {}, false, false, false, false);
        balance->applyToBuffer(context);
        for (int frame = 0; frame < 8; ++frame)
            require(std::abs(pcm.getSample(0, frame)) < 1.e-7f &&
                        std::abs(pcm.getSample(1, frame) - .125f) < 1.e-6f,
                    "Fresh/rate-reprepared Channel begins at current joint gain/pan");
        plugin->baseClassDeinitialise();
    }
}
static void mixerControlTests(const juce::File &folder)
{
    balanceReprepareTests();
    dsp::acceptance::preparedBalanceAcceptance();
    acceptance::overload::mixerOverloadTests();
    acceptance::overload::mixerOverloadHeapProbe([](auto &&operation)
    {
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        operation();
        callbackAllocationProbe = false;
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Actual overload publication uses no callback heap/destruction");
    });
    const auto inFlight = acceptance::overload::mixerOverloadInFlightProbe();
    folder.getChildFile("mixer-overload-concurrency.txt").replaceWithText(
        "observedWriter=" + juce::String(int(inFlight.observedWriter)) +
        " clearExcludedInFlight=" + juce::String(int(inFlight.clearExcludedInFlight)) +
        " clearFence=" + juce::String(inFlight.clearFence) +
        " completedAfterJoin=" + juce::String(inFlight.completedAfterJoin) +
        " clippedAfterJoin=" + juce::String(inFlight.clippedAfterJoin) +
        " latchedAfterJoin=" + juce::String(int(inFlight.latchedAfterJoin)) +
        "\nUnobserved interleaving is unverified, never a concurrency pass.\n");
    {
        dsp::PreparedBalance balance;
        require(balance.prepare(48000, dsp::PreparedBalance::Mode::channelGainAndPan,
                                te::PanLawLinear, 1, 0) == dsp::PreparedBalance::Result::ok,
                "Prepare bounded callback balance fixture");
        juce::AudioBuffer<float> block(4, 256);
        block.clear();
        const auto allocations = callbackAllocations.load();
        const auto destructions = callbackDestructions.load();
        bool processed = true;
        callbackAllocationProbe = true;
        for (int i = 0; i < 1000; ++i)
            processed &= balance.process(block, 0, 256, i % 2 ? .25f : 1.f,
                                          float(i % 3 - 1)) == dsp::PreparedBalance::Result::ok;
        callbackAllocationProbe = false;
        require(processed && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions,
                "Changing balance targets on 256000 frames uses no callback heap/destruction");
    }
    Controller c;
    CommandArgs a;
    a.mixer = 1;
    a.text = " DRUMS ";
    require(c.execute("mixer.rename", a).wasOk(), "Trimmed Mixer rename");
    require(mixerInsert(c.project, 1)->name == "DRUMS", "Rename readback");
    for (const auto &bad : {juce::String(""), juce::String(" \n "), juce::String::repeatedString("x", 65)})
    {
        a.text = bad;
        require(c.execute("mixer.rename", a).failed(), "Invalid Mixer name rejected");
    }
    a.value = 4278255360.;
    require(c.execute("mixer.colour", a).wasOk(), "Opaque insert colour");
    for (auto bad : {-1., 1., 4278190080.5, 4294967296.})
    {
        a.value = bad;
        require(c.execute("mixer.colour", a).failed(), "Strict ARGB");
    }
    a.mixer = 1;
    a.target = 3;
    require(c.execute("mixer.routeOnly", a).wasOk(), "Prepare downstream bus");
    a.mixer = 2;
    a.target = 3;
    require(c.execute("mixer.route", a).wasOk(), "Prepare upstream and parallel path");
    a.target = 4;
    require(c.execute("mixer.route", a).wasOk(), "Unrelated upstream branch");
    a.mixer = 7;
    a.enabled = true;
    require(c.execute("mixer.mute", a).wasOk(), "Prepare original mute");
    const auto baseline = c.project.state();
    a.mixer = 1;
    require(c.execute("mixer.solo", a).wasOk(), "Ordinary source solo");
    require(!mixerInsert(c.project, 0)->muted && !mixerInsert(c.project, 3)->muted &&
                mixerInsert(c.project, 2)->muted,
            "Retain downstream path only");
    a.mixer = 3;
    require(c.execute("mixer.soloConnected", a).wasOk(), "Connected bus solo");
    require(!mixerInsert(c.project, 1)->muted && !mixerInsert(c.project, 2)->muted &&
                mixerInsert(c.project, 4)->muted && mixerInsert(c.project, 6)->muted,
            "Directional input closure, no Master/upstream sibling bridge");
    const auto solo = c.project.state();
    Project loaded;
    const auto file = folder.getChildFile("mixer-solo-restore.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(solo),
            "Save/reopen retains original solo baseline");
    a.enabled = false;
    require(c.execute("mixer.soloConnected", a).wasOk() && c.project.state().isEquivalentTo(baseline),
            "Restore original mute state after switching target");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(solo), "Exact unsolo Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(baseline), "Exact unsolo Redo");
    a.enabled = true;
    a.mixer = 1;
    c.execute("mixer.solo", a);
    a.mixer = 8;
    c.execute("mixer.mute", a);
    require(!c.project.mixerSolo && mixerInsert(c.project, 8)->muted,
            "Manual mute ends restoration without changing other mask");
    c.undoEdit();
    a.restore = false;
    c.execute("mixer.unsolo", a);
    require(
        std::none_of(c.project.mixer.begin(), c.project.mixer.end(), [](const auto &m) { return m.muted; }),
        "Optional unmute-all exit");
    {
        Controller global;
        CommandArgs mute;
        mute.enabled = true;
        for (const int index : {0, 2, 7})
        {
            mute.mixer = index;
            require(global.execute("mixer.mute", mute).wasOk(), "Prepare independent manual mutes");
        }
        const auto beforeAll = global.project.state();
        auto expected = global.project;
        for (auto &insert : expected.mixer)
            insert.muted = false;
        require(global.execute("mixer.unmuteAll", {}).wasOk() &&
                    global.project.state().isEquivalentTo(expected.state()),
                "Global Unmute All includes Master without requiring solo");
        global.undoEdit();
        require(global.project.state().isEquivalentTo(beforeAll),
                "One global Undo restores exact manual mask");
        global.redoEdit();
        require(global.project.state().isEquivalentTo(expected.state()),
                "Global Redo restores exact enabled state");
        const auto revision = global.revision;
        const auto undo = global.undo.getUndoDescription();
        require(global.execute("mixer.unmuteAll", {}).wasOk() && global.revision == revision &&
                    global.undo.getUndoDescription() == undo,
                "Repeated Unmute All creates no spurious history");
        mute.mixer = 2;
        require(global.execute("mixer.soloConnected", mute).wasOk(), "Prepare global solo exit");
        const auto soloBefore = global.project.state();
        require(global.execute("mixer.unmuteAll", {}).wasOk() && !global.project.mixerSolo &&
                    global.project.state().isEquivalentTo(expected.state()),
                "Global Unmute All clears solo restoration ownership");
        global.undoEdit();
        require(global.project.state().isEquivalentTo(soloBefore),
                "Global Undo restores exact solo ownership");
        auto globalProfile = Customization::familiarDefaults();
        Macro globalMacro;
        globalMacro.id = "macro.unmuteAll";
        globalMacro.name = "Enable the whole mix";
        globalMacro.steps = {{"mixer.unmuteAll", {}, true}};
        MacroContext unrelatedContext;
        unrelatedContext.insert = 999;
        require(globalProfile.addMacro(globalMacro).wasOk() &&
                    globalProfile.runMacro(globalMacro.id, global, unrelatedContext).wasOk() &&
                    global.project.state().isEquivalentTo(expected.state()),
                "Global macro needs no selected insert even with stale context");
        global.undoEdit();
        require(global.project.state().isEquivalentTo(soloBefore), "Global macro is one exact Undo");
    }
    auto old = Project::empty().state();
    old.setProperty("version", 18, nullptr);
    stripPatternExtentForLegacy(old);
    require(Project::parse(old, loaded).wasOk() && !loaded.mixerSolo && loaded.mixer[1].colour == 0,
            "Genuine schema18 migration");
    auto corrupt = solo;
    corrupt.getChildWithName("MIXER_SOLO").setProperty("insert", 999, nullptr);
    require(Project::parse(corrupt, loaded).failed(), "Reject corrupt solo state");
    auto profile = Customization::familiarDefaults();
    Macro macro;
    macro.id = "macro.namedBus";
    macro.name = "Name and pan selected bus";
    CommandArgs rename;
    rename.text = "Macro bus";
    CommandArgs pan;
    pan.value = -.25;
    macro.steps = {{"mixer.rename", rename, true}, {"mixer.pan", pan, true}};
    require(profile.addMacro(macro).wasOk(), "Named Mixer macro");
    MacroContext context;
    context.insert = 5;
    const auto macroStart = c.project.state();
    require(profile.runMacro(macro.id, c, context).wasOk() &&
                mixerInsert(c.project, 5)->name == "Macro bus" && mixerInsert(c.project, 5)->pan == -.25f,
            "Ordered context Mixer macro");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(macroStart), "Whole Mixer macro is one Undo");
    // Exact meter arithmetic, stale readback and non-consuming dual readers.
    MixerMeter meter;
    juce::AudioBuffer<float> b(2, 256);
    for (int i = 0; i < 256; ++i)
    {
        b.setSample(0, i, i % 2 ? .5f : -.5f);
        b.setSample(1, i, .25f);
    }
    meter.publish(b, 0, 256, 48000);
    auto first = meter.read(), second = meter.read();
    require(first.available && !first.stale && first.blocks == second.blocks && first.peak[0] == .5f &&
                first.rms[0] == .5f && first.rms[1] == .25f,
            "Actual block peak/RMS stereo meter");
    b.clear();
    meter.publish(b, 0, 256, 48000);
    auto decay = meter.read();
    require(decay.peak[0] == 0 && decay.rms[0] == 0 && decay.display[0] < .5f && decay.display[0] > .4f,
            "Meter decay distinct from actual silent PCM");
    juce::Thread::sleep(260);
    require(meter.read().stale && meter.read().display[0] == 0, "Stopped callback expires meter");
}
static void mixerControlLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
        AudioEngine audio(false, {}, 256); // Genuine fixed256-frame synthetic callback bound.
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device, "Mixer control virtual device");
        Controller c;
        auto &p = c.project;
        auto channel = p.channels.front().id, pattern = p.patterns.front().id;
        p.channels.front().source = "builtin:bass";
        p.channels.front().sampler.loop = true;
        p.channels.front().mixer = 1;
        if (instrument)
        {
            p.channels.front().instrument = *instrument;
            p.channels.front().instrument->id = p.allocate();
        }
        p.patterns.front().notes = {{p.allocate(), channel, 0, 4, 60, .7f, false}};
        p.placements = {{p.allocate(), pattern, 0, 0, 4, 0}, {p.allocate(), pattern, 0, 4, 4, 0}};
        require(setMixerRoute(p, 1, 3, true, true).wasOk() &&
                    awaitProjection(audio, p, patternMode, pattern).wasOk(),
                "Prepare named bus live score");
        auto *projectedEdit = reinterpret_cast<te::Edit *>(audio.projectionDiagnostics().edit);
        require(projectedEdit && !projectedEdit->getMasterVolumePlugin()->isEnabled(),
                "Native Master balance DSP is disabled, not unity-enabled");
        for (auto *track : te::getAudioTracks(*projectedEdit))
        {
            require(!track->getVolumePlugin()->isEnabled(), "Native channel/Mixer balance DSP disabled");
            for (auto *plugin : track->pluginList)
                require(!dynamic_cast<te::LevelMeterPlugin *>(plugin),
                        "Unused locking native track meter removed before publication");
        }
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, loops = 0;
        double maxMs = 0, previousHead = 0;
        bool monitor = false;
        juce::String trace = "callback,ms,left,right,meterLeft,meterRight\n";
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                callbackAllocationProbe = monitor;
                auto start = juce::Time::getHighResolutionTicks();
                device->process(block);
                callbackAllocationProbe = false;
                const double ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) *
                    1000;
                maxMs = std::max(maxMs, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
                const auto head = audio.projectionDiagnostics().headSeconds;
                loops += head + .02 < previousHead;
                previousHead = head;
                auto m = audio.mixerMeter(0);
                trace += juce::String(callbacks) + "," + juce::String(ms, 6) + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 8) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 8) + "," + juce::String(m.peak[0], 8) +
                         "," + juce::String(m.peak[1], 8) + "\n";
                dispatchMessages(1);
            }
        };
        tick(20);
        const auto before = audio.projectionDiagnostics();
        const auto graphs = audio.mixerGraphInitialisations();
        const auto workers = audio.pluginDiagnostics();
        const int starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        monitor = true;
        CommandArgs a;
        a.mixer = 1;
        auto publish = [&]
        {
            require(awaitProjection(audio, p, patternMode, pattern).wasOk(), "Publish live Mixer control");
            require(audio.playing(), "Control retained playback");
            tick(10);
        };
        if (!instrument)
        {
            // The existing original bass is nonzero and sustained. Gain2 twice
            // plus hard-left balance's established surviving-side factor2 gives
            // actual left overload; right is silent after the15ms ramp.
            // The acceptance below still requires actual PCM, not just these
            // settings, to prove that this fixture genuinely overloads.
            const auto original = p.state();
            CommandArgs ch;
            ch.channel = channel;
            ch.value = 2;
            require(c.execute("channel.gain", ch).wasOk(), "Prepare actual overload Channel gain");
            CommandArgs mix;
            mix.mixer = 1;
            mix.value = 2;
            require(c.execute("mixer.gain", mix).wasOk(), "Prepare actual overload Mixer gain");
            mix.value = -1;
            require(c.execute("mixer.pan", mix).wasOk(), "Prepare asymmetric overload balance");
            publish();
            tick(1);
            const auto overloaded = audio.mixerMeter(1);
            require(overloaded.available && overloaded.overload[0] && overloaded.peak[0] >= 1 &&
                        overloaded.peak[1] < .000001f && block.getMagnitude(0, 0, 256) >= 1,
                    "Actual sustained score produces asymmetric post-pan sample overload");
            const auto model = p.state();
            const auto revision = c.revision;
            const bool dirty = c.dirty;
            const auto undo = c.undo.getUndoDescription(), redo = c.undo.getRedoDescription();
            const auto head = audio.projectionDiagnostics().headSeconds;
            std::uint64_t fence = 0;
            require(audio.clearMixerOverload(1, fence).wasOk(), "Clear actual live insert overload");
            const auto cleared = audio.mixerMeter(1);
            require(cleared.available && fence == overloaded.blocks &&
                        cleared.clearedThroughBlock == fence && !cleared.overload[0] &&
                        !cleared.overload[1] && cleared.blocks == overloaded.blocks &&
                        audio.projectionDiagnostics().headSeconds == head && audio.playing() &&
                        c.revision == revision && c.dirty == dirty &&
                        c.undo.getUndoDescription() == undo && c.undo.getRedoDescription() == redo &&
                        p.state().isEquivalentTo(model),
                    "Live clear acknowledges completed fence without document/history/playhead change");
            tick(1);
            const auto newClip = audio.mixerMeter(1);
            require(newClip.available && newClip.blocks > fence && newClip.overload[0] &&
                        newClip.lastClippedBlock[0] > fence && !newClip.overload[1],
                    "New actual overloaded callback re-latches only left after clear");
            std::uint64_t missingFence = 0;
            require(audio.clearMixerOverload(999, missingFence).failed() &&
                        c.revision == revision && p.state().isEquivalentTo(model),
                    "Invalid live meter target refuses atomically");
            report += juce::String(patternMode ? "pattern" : "song") +
                      " overload clearFence=" + juce::String(fence) +
                      " relatchBlock=" + juce::String(newClip.lastClippedBlock[0]) +
                      " actualLeftPeak=" + juce::String(newClip.peak[0], 9) + "\n";
            c.undoEdit();
            c.undoEdit();
            c.undoEdit();
            require(p.state().isEquivalentTo(original), "Only three gain/pan edits enter live history");
            publish();
            for (const auto& insert : p.mixer)
            {
                std::uint64_t ignored = 0;
                require(audio.clearMixerOverload(insert.index, ignored).wasOk(),
                        "Restore transient indications after live overload scenario");
            }
        }
        a.value = -1;
        c.execute("mixer.pan", a);
        publish();
        require(block.getMagnitude(0, 0, 256) > 0 && block.getMagnitude(1, 0, 256) < .000001f,
                "Familiar linear pan PCM");
        auto m = audio.mixerMeter(0);
        require(m.available && !m.stale && m.peak[0] > 0 && m.peak[1] < .000001f,
                "Actual graph stereo meter follows pan PCM");
        const auto ownInsertMeter = audio.mixerMeter(1);
        const bool insertPanTap = ownInsertMeter.available && !ownInsertMeter.stale &&
                                  ownInsertMeter.peak[0] > 0 && ownInsertMeter.peak[1] < .000001f;
        report += "insert pan=-1 outputRight=" + juce::String(block.getMagnitude(1, 0, 256), 9) +
                  " ownMeterRight=" + juce::String(ownInsertMeter.peak[1], 9) + "\n";
        a.value = 0;
        c.execute("mixer.pan", a);
        publish();
        a.mixer = 0;
        a.value = -1;
        c.execute("mixer.pan", a);
        publish();
        const auto ownMasterMeter = audio.mixerMeter(0);
        const bool masterPanTap = block.getMagnitude(0, 0, 256) > 0 &&
                                  block.getMagnitude(1, 0, 256) < .000001f && ownMasterMeter.available &&
                                  !ownMasterMeter.stale && ownMasterMeter.peak[0] > 0 &&
                                  ownMasterMeter.peak[1] < .000001f;
        report += "Master pan=-1 outputRight=" + juce::String(block.getMagnitude(1, 0, 256), 9) +
                  " ownMeterRight=" + juce::String(ownMasterMeter.peak[1], 9) + "\n";
        require(folder.getChildFile("mixer-meter-pan-regression.txt").replaceWithText(report),
                "Retain independent own-insert/Master meter versus actual PCM before assertion");
        require(insertPanTap && masterPanTap, "Own insert and Master meters measure after their own pan");
        a.value = 0;
        c.execute("mixer.pan", a);
        publish();
        a.mixer = 1;
        // Channel targets share the same immutable publication and must retain
        // the sounding voice while changing gain and pan jointly.
        CommandArgs channelArgs;
        channelArgs.channel = channel;
        channelArgs.value = 1;
        require(c.execute("channel.pan", channelArgs).wasOk(), "Live Channel pan canonical command");
        publish();
        require(block.getMagnitude(0, 0, 256) < .000001f && block.getMagnitude(1, 0, 256) > 0,
                "Channel balance hard-right PCM without native balance DSP");
        c.undoEdit();
        publish();
        require(block.getMagnitude(0, 0, 256) > 0 && block.getMagnitude(1, 0, 256) > 0,
                "Channel pan Undo restores both sounding sides");
        a.enabled = true;
        c.execute("mixer.mute", a);
        publish();
        require(block.getMagnitude(0, 0, 256) < .000001f && audio.mixerMeter(1).peak[0] < .000001f,
                "Muted signal and actual meter silence after ramp");
        c.undoEdit();
        publish();
        require(block.getMagnitude(0, 0, 256) > 0, "Undo mute restores sustained voice");
        for (const int index : {0, 1, 7})
        {
            a.mixer = index;
            require(c.execute("mixer.mute", a).wasOk(), "Live prepare global manual mask");
        }
        publish();
        const auto beforeAll = p.state();
        require(block.getMagnitude(0, 0, 256) < .000001f, "Live global fixture is actually muted");
        require(c.execute("mixer.unmuteAll", {}).wasOk(), "Live global command");
        publish();
        require(block.getMagnitude(0, 0, 256) > 0 && !p.mixerSolo &&
                    std::none_of(p.mixer.begin(), p.mixer.end(), [](const auto &m) { return m.muted; }),
                "Unmute All restores actual sustained PCM in both transport modes");
        c.undoEdit();
        require(p.state().isEquivalentTo(beforeAll), "Live global Undo restores full prior state");
        publish();
        require(block.getMagnitude(0, 0, 256) < .000001f, "Live global Undo actually silences PCM");
        c.redoEdit();
        publish();
        require(block.getMagnitude(0, 0, 256) > 0, "Live global Redo retains sounding voice");
        a.mixer = 1;
        for (int i = 0; i < 28; ++i)
        {
            a.text = "Live bus " + juce::String(i);
            c.execute("mixer.rename", a);
            a.value = (i % 3 - 1) * .5;
            c.execute("mixer.pan", a);
            publish();
            c.execute(i % 2 ? "mixer.soloConnected" : "mixer.solo", a);
            publish();
            c.execute("mixer.unsolo", a);
            publish();
            c.undoEdit();
            publish();
            c.redoEdit();
            publish();
        }
        const auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && starts == device->starts &&
                    stops == device->stops && graphs == audio.mixerGraphInitialisations(),
                "Mixer controls retain graph/device/prepared resources");
        if (instrument)
        {
            auto current = audio.pluginDiagnostics();
            require(current.size() == workers.size() && !current.empty() && current.front().alive &&
                        current.front().processId == workers.front().processId &&
                        current.front().packets.missed == workers.front().packets.missed,
                    "Real provider worker retained without new misses");
        }
        const auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("mixer-controls-" + mode + "-callbacks.csv").replaceWithText(trace);
        report += mode + " callbacks=" + juce::String(callbacks) + " loops=" + juce::String(loops) +
                  " maxMs=" + juce::String(maxMs, 6) + " overruns=" + juce::String(overruns) +
                  " hostAllocations=" + juce::String(callbackAllocations.load() - allocations) +
                  " hostDestructions=" + juce::String(callbackDestructions.load() - destructions) +
                  " graph/device/cache replacements=0\n";
        folder.getChildFile("mixer-controls-live.txt").replaceWithText(report);
        require(!overruns && loops >= 1 && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions,
                "Live Mixer host callback bounds");
        monitor = false;
        audio.stop();
    }
}
