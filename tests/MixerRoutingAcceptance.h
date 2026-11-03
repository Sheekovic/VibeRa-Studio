// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/MixerRouting.h"
#include "../src/MixerMacroInput.h"
static void mixerRoutingTests(const juce::File &folder)
{
    Controller c;
    for (const auto &invalid :
         {juce::String("2:50junk"), juce::String("2:-1"), juce::String("2:201"), juce::String("nan:20")})
    {
        CommandArgs parsed;
        require(parseMixerMacro("mixer.send", invalid, parsed).failed(), "Strict Mixer macro input");
    }
    auto initial = c.project.state();
    require(c.project.mixer[0].routes.empty() && mixerRoute(c.project, 1, 0)->gain == 1,
            "Default Master routes");
    CommandArgs a;
    a.mixer = 1;
    a.target = 2;
    require(c.execute("mixer.routeOnly", a).wasOk(), "Route to bus only");
    require(!mixerRoute(c.project, 1, 0) && mixerRoute(c.project, 1, 2), "Only removes parallel Master path");
    const auto bus = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "Exact routing Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(bus), "Exact routing Redo");
    a.mixer = 2;
    a.target = 1;
    require(c.execute("mixer.route", a).failed(), "Reject feedback before mutation");
    require(c.project.state().isEquivalentTo(bus), "Rejected route is atomic");
    a.mixer = 1;
    a.target = 1;
    require(c.execute("mixer.route", a).failed(), "No self send");
    a.mixer = 0;
    a.target = 1;
    require(c.execute("mixer.route", a).failed(), "Master is destination only");
    a.mixer = 1;
    a.target = 2;
    a.value = .5;
    require(c.execute("mixer.send", a).wasOk(), "Bus send level");
    require(mixerRoute(c.project, 1, 2)->gain == .5f, "Send readback");
    for (double bad : {-1., 2.001, std::numeric_limits<double>::quiet_NaN()})
    {
        a.value = bad;
        require(c.execute("mixer.send", a).failed(), "Reject bad gain");
    }
    const auto project = folder.getChildFile("routed-project.vibra");
    require(saveProject(c.project, project).wasOk(), "Save actual routing project");
    Project loaded;
    require(loadProject(project, loaded).wasOk() && loaded.state().isEquivalentTo(c.project.state()),
            "Routing round trip");
    auto old = Project::empty().state();
    old.setProperty("version", 17, nullptr);
    stripPatternExtentForLegacy(old);
    require(Project::parse(old, loaded).wasOk() && mixerRoute(loaded, 1, 0),
            "Genuine old project retains exact Master route");
    auto masquerade = c.project.state();
    masquerade.setProperty("version", 17, nullptr);
    require(Project::parse(masquerade, loaded).failed(), "No routing under old file version");
    // Compare the real Tracktion render, not a synthetic summing implementation.
    auto p = Project::empty();
    auto &ch = p.channels.front();
    ch.mixer = 1;
    ch.gain = .4f;
    p.patterns.front().notes = {{p.allocate(), ch.id, 0, 1, 60, .8f}};
    p.placements = {{p.allocate(), p.patterns.front().id, 0, 0, 4, 0}};
    for (auto &m : p.mixer)
        m.gain = 1;
    AudioEngine engine(false);
    double rate = 0;
    juce::String lifetimeReport;
    auto render = [&](const juce::String &name)
    {
        const auto wav = folder.getChildFile(name + ".wav");
        const auto retainedBefore = engine.retainedPluginHostCount();
        require(engine.renderSync(p, wav).wasOk(), "Actual routing render");
        const auto afterReturn = engine.retainedPluginHostCount();
        const auto retireBegan = juce::Time::getMillisecondCounterHiRes();
        while (engine.retainedPluginHostCount() > retainedBefore &&
               juce::Time::getMillisecondCounterHiRes() - retireBegan < 2000.)
        {
            engine.reclaimPluginHosts();
            dispatchMessages(1);
        }
        lifetimeReport +=
            name + " before=" + juce::String(int(retainedBefore)) +
            " afterReturn=" + juce::String(int(afterReturn)) +
            " afterReap=" + juce::String(int(engine.retainedPluginHostCount())) +
            " retirementMs=" + juce::String(juce::Time::getMillisecondCounterHiRes() - retireBegan, 3) + "\n";
        require(folder.getChildFile("offline-worker-lifetime.txt").replaceWithText(lifetimeReport),
                "Retain actual offline worker retirement observations");
        require(engine.retainedPluginHostCount() == retainedBefore,
                "Finished offline workers exit without accumulating until engine destruction");
        return readWav(wav, rate);
    };
    const auto direct = render("direct");
    require(setMixerRoute(p, 1, 2, true, true).wasOk(), "Prepare real bus");
    const auto routed = render("bus");
    require(setMixerRoute(p, 1, 0, true).wasOk(), "Parallel dry path");
    const auto parallel = render("parallel");
    require(direct.getNumSamples() == routed.getNumSamples() &&
                direct.getNumSamples() == parallel.getNumSamples(),
            "Routing duration invariant");
    double errorBus = 0, errorParallel = 0;
    for (int i = 0; i < direct.getNumSamples(); ++i)
    {
        errorBus = std::max(errorBus, std::abs(double(direct.getSample(0, i)) - routed.getSample(0, i)));
        errorParallel =
            std::max(errorParallel, std::abs(double(direct.getSample(0, i)) * 2 - parallel.getSample(0, i)));
    }
    require(direct.getMagnitude(0, direct.getNumSamples()) > 0, "Real musical PCM");
    require(errorBus < 2.e-6 && errorParallel < 2.e-6, "Bus and parallel paths are sample-aligned");
    // Original SDK effect: its native reported delay is0, while isolation adds
    // one host block. This explicitly exercises parallel-path compensation and
    // bypass; nonzero/dynamically reported plugin delay remains separate work.
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto fixture = executable.getSiblingFile("VibeRaParameterFixture.vst3");
    const auto configuration = executable.getParentDirectory(),
               artefacts = configuration.getParentDirectory();
    if (!fixture.existsAsFile() && artefacts.getFileName() == "VibeRaTests_artefacts" &&
        artefacts.getParentDirectory().getFileName() == "windows")
        fixture = artefacts.getParentDirectory().getChildFile(configuration.getFileName() +
                                                              "/VibeRaParameterFixture.vst3");
    require(fixture.existsAsFile(), "Routing fixture is in the selected package or sole compilation tree");
    auto effect = parameterSpec(fixture);
    require(!effect.instrument, "Actual SDK effect descriptor");
    effect.id = p.allocate();
    p.mixer[3].effects = {effect};
    require(setMixerRoute(p, 1, 0, true, true).wasOk(), "Effect experiment dry route");
    const auto fxDry = render("effect-dry");
    require(setMixerRoute(p, 1, 3, true, true).wasOk(), "Effect bus route");
    const auto fxBus = render("effect-bus");
    require(setMixerRoute(p, 1, 0, true).wasOk(), "Effect parallel route");
    const auto fxParallel = render("effect-parallel");
    require(fxDry.getNumSamples() == fxBus.getNumSamples() &&
                fxBus.getNumSamples() == fxParallel.getNumSamples(),
            "Effect route render duration");
    double effectError = 0;
    for (int i = 0; i < fxDry.getNumSamples(); ++i)
        effectError = std::max(effectError, std::abs(double(fxDry.getSample(0, i)) + fxBus.getSample(0, i) -
                                                     fxParallel.getSample(0, i)));
    require(fxBus.getMagnitude(0, fxBus.getNumSamples()) > 0 && effectError < 3.e-6,
            "Isolated one-block effect latency aligns parallel paths");
    require(setMixerRoute(p, 1, 3, true, true).wasOk(), "Bypassed effect-only route");
    p.mixer[3].effects.front().bypass = true;
    const auto bypassed = render("effect-bypass");
    double bypassError = 0;
    require(bypassed.getNumSamples() == fxDry.getNumSamples(), "Bypass duration invariant");
    for (int i = 0; i < bypassed.getNumSamples(); ++i)
        bypassError =
            std::max(bypassError, std::abs(double(bypassed.getSample(0, i)) - fxDry.getSample(0, i)));
    require(bypassError < 3.e-6, "Bypass retains compensated dry phase");
    folder.getChildFile("mixer-routing-effect-latency.txt")
        .replaceWithText("PASS original SDK VST3 effect, isolated fixed block latency, real "
                         "direct/bus/parallel/bypass PCM alignment. Nonzero/dynamic native delay, installed "
                         "effects and listening remain separate.\n");
    folder.getChildFile("mixer-routing-core.txt")
        .replaceWithText(
            "PASS default/migration/strict routing, cycle rejection, history, project round trip and actual "
            "bus/parallel WAV sums. Live/plugin/native qualification separate.\n");
}
