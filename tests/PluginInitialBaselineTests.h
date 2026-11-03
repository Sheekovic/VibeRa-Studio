// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void pluginInitialBaselineTests(const juce::File &folder, const juce::File &binary)
{
    auto spec = parameterSpec(binary);
    PluginHost host;
    require(host.start(spec, 48000, 256, true).wasOk(), "initial baseline SDK worker starts");
    PluginParameterCatalogue initial;
    require(host.readInitialParameterCatalogue(initial) && initial.serial == 0 &&
                std::abs(nativeParameter(initial, 0).value - .5f) < .00001f,
            "initial snapshot is the actual pre-DSP native value, including zero ID");
    require(parameterResult(host, host.setParameter(0, .25f, initial.generation)).result == 0,
            "manual change after initial snapshot acknowledged");
    auto capture = host.requestState();
    const auto until = juce::Time::getMillisecondCounter() + 10000;
    while (!host.readState(capture, spec.state) && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep(2);
    require(spec.state.isNotEmpty(), "real stopped native state captured for restored baseline");
    PluginHost restored;
    require(restored.start(spec, 48000, 256, true).wasOk(), "restored baseline worker starts");
    PluginParameterCatalogue restoredInitial;
    require(restored.readInitialParameterCatalogue(restoredInitial) &&
                std::abs(nativeParameter(restoredInitial, 0).value - .25f) < .00001f,
            "baseline follows restored native state rather than plugin default");
    juce::AudioBuffer<float> audio(2, 256);
    const ipc::ParameterEvent event{0, 0, .75f};
    for (int channel = 0; channel < 2; ++channel)
        std::fill_n(audio.getWritePointer(channel), 256, .2f);
    host.process(audio, 0, 256, nullptr, 0, 0, 120, true, false, 4, 4, &event, 1, initial.generation);
    audio.clear();
    host.process(audio, 0, 256, nullptr, 0, 256. / 48000., 120, true, false);
    const auto current = parameterCatalogue(host, true);
    PluginParameterCatalogue stillInitial;
    float maximumError = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < 256; ++frame)
            maximumError = std::max(maximumError, std::abs(audio.getSample(channel, frame) - .15f));
    const auto observation = "controller=" + juce::String(nativeParameter(current, 0).value, 6) +
                             " pcmError=" + juce::String(maximumError, 8) + "\n";
    require(folder.getChildFile("initial-parameter-observation.txt").replaceWithText(observation),
            "retain actual controller/PCM observation before baseline assertion");
    require(maximumError < .00001f && host.readInitialParameterCatalogue(stillInitial) &&
                std::abs(nativeParameter(stillInitial, 0).value - .5f) < .00001f,
            "automation/controller values cannot overwrite the immutable initial snapshot");
    require(parameterResult(host, host.setParameter(42, 1, current.generation)).result == 0,
            "baseline schema-change fixture admitted");
    const auto changed = parameterCatalogue(host, true);
    auto sentinel = stillInitial;
    require(changed.generation > initial.generation && !host.readInitialParameterCatalogue(sentinel) &&
                sentinel.generation == stillInitial.generation &&
                sentinel.parameters == stillInitial.parameters,
            "schema replacement invalidates initial authority without overwriting caller output");
    require(folder.getChildFile("initial-parameter-baseline.txt")
                .replaceWithText("PASS original SDK worker: initial0.5, restored0.25, current automation0.75 "
                                 "with retained initial0.5; "
                                 "schema-change refusal preserves caller output. No native knob Undo or "
                                 "real-provider/listening claim.\n"),
            "initial snapshot evidence retained");
}
