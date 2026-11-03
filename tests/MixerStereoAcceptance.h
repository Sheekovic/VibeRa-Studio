// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "PreparedStereo.h"
#include "MixerControls.h"
#include "MixerStereoContracts.h"
#include <bit>
#include <limits>

namespace vibera::test::mixer_stereo
{
inline std::array<double, 2> expected(double left, double right, double separation, bool invert, bool swap)
{
    // Independent literal audio oracle, never call the production matrix.
    auto l = separation >= 0 ? left + (right - left) * separation / 2 : left + right * separation;
    auto r = separation >= 0 ? right + (left - right) * separation / 2 : right + left * separation;
    if (invert)
    {
        l = -l;
        r = -r;
    }
    return swap ? std::array<double, 2>{r, l} : std::array<double, 2>{l, r};
}
inline void tests()
{
    using Stereo = dsp::PreparedStereo;
    for (const double rate : {44100., 48000., 96000.})
        for (float amount : {-1.f, -.5f, 0.f, .5f, 1.f})
            for (bool invert : {false, true})
                for (bool swap : {false, true})
                {
                    Stereo prepared;
                    require(prepared.prepare(rate, {amount, invert, swap}), "Prepare finite stereo target");
                    for (const auto input : {std::array<float, 2>{1, .25f}, {1, 1}, {1, -1}, {0, 0}})
                    {
                        auto l = input[0], r = input[1];
                        prepared.process(l, r);
                        const auto oracle = expected(input[0], input[1], amount, invert, swap);
                        require(l == oracle[0] && r == oracle[1], "Literal stereo/mono/antiphase matrix PCM");
                    }
                }
    Stereo identity;
    require(identity.prepare(48000, {}), "Default identity prepared");
    float left = -0.f, right = .125f;
    identity.process(left, right);
    require(std::bit_cast<unsigned>(left) == std::bit_cast<unsigned>(-0.f) && right == .125f,
            "Default stereo retains original sample bits including signed zero");
    require(!identity.setTarget({std::numeric_limits<float>::quiet_NaN(), false, false}) &&
                !identity.prepare(0, {}) && !identity.setTarget({1.001f, false, false}),
            "Invalid preparation/targets refuse without changing prior state");
    for (const auto rate : {44100., 48000.})
    {
        Stereo smooth;
        require(smooth.prepare(rate, {}) && smooth.setTarget({1, false, false}),
                "Prepare original-to-mono ramp");
        const int frames = int(rate * .005); // Independent five-ms truncation:220/240.
        for (int n = 1; n <= frames + 8; ++n)
        {
            require(smooth.setTarget({1, false, false}), "Repeated target does not restart interpolation");
            float l = 1, r = 0;
            const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
            {
                const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
                smooth.process(l, r);
            }
            const double mix = std::min(1., double(n) / frames);
            require(std::abs(l - (1 - mix / 2)) < 2e-5 && std::abs(r - mix / 2) < 2e-5 &&
                        callbackAllocations.load() == allocated && callbackDestructions.load() == destroyed,
                    "Every ramp sample follows independent clock and zero C++ allocation/destruction");
        }
    }
    Controller controller;
    const auto original = controller.project.state();
    CommandArgs a;
    a.mixer = 1;
    a.value = -.5;
    require(controller.execute("mixer.stereoSeparation", a).wasOk(), "Canonical separation command");
    a.enabled = true;
    require(controller.execute("mixer.phaseInvert", a).wasOk() &&
                controller.execute("mixer.stereoSwap", a).wasOk(),
            "Canonical switches");
    const auto changed = controller.project.state();
    const auto &insert = *mixerInsert(controller.project, 1);
    require(insert.stereoSeparation == -.5f && insert.phaseInverted && insert.stereoSwapped,
            "Stable target readback");
    Project restored;
    require(Project::parse(changed, restored).wasOk() && restored.state().isEquivalentTo(changed),
            "Exact project round trip retains all stereo targets");
    for (int i = 0; i < 3; ++i)
        require(controller.undoEditChecked().wasOk(), "One Undo per canonical operation");
    require(controller.project.state().isEquivalentTo(original), "Exact Undo baseline");
    for (int i = 0; i < 3; ++i)
        require(controller.redoEditChecked().wasOk(), "Exact Redo");
    require(controller.project.state().isEquivalentTo(changed), "Exact Redo endpoints");
    const auto before = controller.project.state();
    a.value = std::numeric_limits<double>::infinity();
    require(controller.execute("mixer.stereoSeparation", a).failed() &&
                controller.project.state().isEquivalentTo(before),
            "Nonfinite separation preserves model/history");
    a.mixer = 2000000;
    a.value = 0;
    require(controller.execute("mixer.stereoSeparation", a).failed(), "Unknown stable insert refused");
    auto old = original.createCopy();
    old.setProperty("version", 20, nullptr);
    stripPatternExtentForLegacy(old);
    Project migrated;
    require(Project::parse(old, migrated).wasOk() && migrated.mixer[1].stereoSeparation == 0 &&
                !migrated.mixer[1].phaseInverted && !migrated.mixer[1].stereoSwapped,
            "Genuine schema20 without newer stereo fields migrates to identity");
    auto malformed = changed.createCopy();
    malformed.setProperty("version", 20, nullptr);
    require(Project::parse(malformed, restored).failed(), "New stereo fields under old schema refuse");
    malformed = changed.createCopy();
    malformed.getChildWithName("MIXER").getChild(1).setProperty("phaseInverted", 1, nullptr);
    require(Project::parse(malformed, restored).failed(), "Integer boolean cannot silently coerce");
}
} // namespace vibera::test::mixer_stereo
