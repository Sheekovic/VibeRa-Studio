// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppSourceTransportAcceptance.h"
namespace initial_app_active_effect_acceptance
{
inline vibera::PluginSpec capturedEffect(const vibera::PluginSpec &input)
{
    using namespace vibera;
    require(!input.instrument && input.name == "VibeRa Parameter Fixture",
            "Exact original affine effect identity");
    auto specification = input;
    specification.state.clear();
    specification.automationBindings.clear();
    specification.bypass = false;
    specification.wet = 1;
    PluginHost seed;
    require(seed.start(specification, 48000, 256, true).wasOk(), "Actual original effect opaque-state seed");
    const auto catalogue = parameterCatalogue(seed);
    require(parameterResult(seed, seed.setParameter(0, .5f, catalogue.generation)).result == 0 &&
                parameterResult(seed, seed.setParameter(0xfedcba98u, 0.f, catalogue.generation)).result == 0,
            "Actual native gain.5/bias0 acknowledged through supported parameters");
    const auto token = seed.requestState();
    require(token != 0, "Actual supported effect capture admitted");
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    bool captured = false;
    while (!(captured = seed.readState(token, specification.state)) &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        seed.service();
        dispatchMessages(2);
    }
    const auto actual = parameterCatalogue(seed, true);
    require(captured && specification.state.isNotEmpty() && nativeParameter(actual, 0).value == .5 &&
                nativeParameter(actual, 0xfedcba98u).value == 0 && seed.declaredLatency().nativeSamples == 0,
            "Real captured affine native state, no manufactured base64/configuration");
    return specification;
}
inline void tests(const juce::File &folder, const vibera::PluginSpec &instrument,
                  const vibera::PluginSpec &effect)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual active-FX app-port attempt");
    require(instrument.instrument && instrument.name == "VibeRa Instrument Runtime Fixture",
            "Exact original native instrument");
    juce::String identities;
    const auto retainIdentity = [&](const juce::File &file)
    {
        require(file.existsAsFile(),
                "Exact existing fixture/application/worker binary, no provider scan/download");
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    };
    retainIdentity(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
    retainIdentity(vibera::pluginWorkerExecutable());
    for (const auto *spec : {&instrument, &effect})
    {
        const auto xml = juce::parseXML(spec->descriptor);
        juce::PluginDescription actual;
        require(xml && actual.loadFromXml(*xml) && actual.name == spec->name &&
                    actual.isInstrument == spec->instrument && actual.pluginFormatName == "VST3" &&
                    juce::File::isAbsolutePath(actual.fileOrIdentifier),
                "Actual scanned original descriptor/format/role and absolute binary target");
        retainIdentity(juce::File(actual.fileOrIdentifier));
        require(folder.getChildFile(spec->instrument ? "instrument-descriptor.xml" : "effect-descriptor.xml")
                    .replaceWithText(spec->descriptor),
                "Retain exact actual scanned target descriptor");
    }
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
            "Exact app/worker/two-fixture hashes before audio setup");
    const auto source = initial_app_source_acceptance::capturedOriginal(instrument);
    const auto affine = capturedEffect(effect);
    require(
        folder.getChildFile("oracle-settings.txt")
            .replaceWithText(
                "original instrument param77=.25; original ParameterFixture param0=.5, param0xfedcba98=0; "
                "native latency0 each\n"
                "effect on actual source insert1; bypass=false; wet=1; channel/insert/Master gain1, pan0\n"
                "48k, device/workerQ256 unchanged; bound256=>K2,total1024; bound512=>K3,total1536\n"
                "wanted L=.125*(61/512+1/4096), R=-.125*(68/512+1/8192); literal two-worker delay; exact0 "
                "tolerance\n"
                "sourceStateSha256=" +
                juce::SHA256(source.state.toRawUTF8(), source.state.getNumBytesAsUTF8()).toHexString() +
                "\neffectStateSha256=" +
                juce::SHA256(affine.state.toRawUTF8(), affine.state.getNumBytesAsUTF8()).toHexString() +
                "\n"),
        "Independent static musical oracle and actual captured-state identities retained");
    std::exception_ptr first;
    juce::String results;
    for (bool pattern : {true, false})
        for (int bound : {256, 512})
        {
            const auto name = juce::String(pattern ? "pattern" : "song") + "-bound" + juce::String(bound);
            try
            {
                initial_app_source_acceptance::run(folder.getChildFile(name), source, affine, pattern, bound,
                                                   true, true);
                results += name + "=completed\n";
            }
            catch (const std::exception &error)
            {
                if (!first)
                    first = std::current_exception();
                results += name + "=failed:" + juce::String(error.what()) + "\n";
            }
            require(folder.getChildFile("cases.txt").replaceWithText(results),
                    "Every independent active-FX outcome retained");
        }
    if (first)
        std::rethrow_exception(first);
}
} // namespace initial_app_active_effect_acceptance
