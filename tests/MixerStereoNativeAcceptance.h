// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual AudioEngine/native graph acceptance. Runtime evidence is separate.
// Include AFTER RoutingLifetimeAcceptance.h, readWav/awaitProjection,
// SyntheticDevice and callback C++ allocation probes. No native OS input,
// physical device, plugin worker, listening or UI/MCP qualification claimed.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace vibera::test::mixer_stereo_native
{
constexpr int quantum = 256, rate = 48000, capacity = 64 * quantum;
constexpr std::array<float, 2> bed{.125f, -.0625f};

// Independent mathematical reference: do not call PreparedStereo::matrix or
// its interpolator. Fixed 5ms at48k =240 samples, evaluated by elapsed fraction.
struct MatrixOracle
{
    std::array<double, 4> current{1, 0, 0, 1}, start = current, goal = current;
    int elapsed = 240;
    void target(const MixerInsert &insert)
    {
        const double s = insert.stereoSeparation;
        const double own = s >= 0 ? 1 - s / 2 : 1, cross = s >= 0 ? s / 2 : s;
        std::array<double, 4> requested{own, cross, cross, own};
        if (insert.stereoSwapped)
            requested = {cross, own, own, cross};
        if (insert.phaseInverted)
            for (auto &value : requested)
                value = -value;
        if (requested == goal)
            return;
        start = current;
        goal = requested;
        elapsed = 0;
    }
    std::array<double, 2> process(double left, double right)
    {
        if (elapsed < 240)
        {
            ++elapsed;
            for (std::size_t i = 0; i < 4; ++i)
                current[i] =
                    elapsed == 240 ? goal[i] : start[i] + (goal[i] - start[i]) * double(elapsed) / 240.;
        }
        return {current[0] * left + current[1] * right, current[2] * left + current[3] * right};
    }
};
// Original sampler contract, independently clocked from explicit event delivery
// at the next callback's frame0 (MidiMonitor emits local events with frame0).
// envelope=false has a natural2ms attack/12ms release even with attack=0.
struct VoiceOracle
{
    int on = -1, off = -1;
    double envelope(int frame) const
    {
        if (on < 0 || frame < on)
            return 0;
        const double atOff = off < 0 ? 0 : std::min(1., double(off - on) / 96.);
        return off >= 0 && frame >= off ? std::max(0., atOff - double(frame - off + 1) / 576.)
                                        : std::min(1., double(frame - on + 1) / 96.);
    }
};
inline void writeRaw(const juce::File &file, const juce::AudioBuffer<float> &pcm, int frames)
{
    auto stream = file.createOutputStream();
    require(bool(stream), "Owned stereo PCM evidence stream");
    require(stream->setPosition(0) && stream->truncate().wasOk(), "Fresh owned stereo PCM evidence reset");
    for (int channel = 0; channel < pcm.getNumChannels(); ++channel)
        require(stream->write(pcm.getReadPointer(channel), std::size_t(frames) * sizeof(float)),
                "Retain every planar float32 sample before oracle assertion");
    stream->flush();
    require(stream->getStatus().wasOk(), "Flush complete owned stereo PCM evidence");
}
inline juce::File writeSource(const juce::File &folder)
{
    const auto file = folder.getChildFile("original-signed-stereo.wav");
    require(!file.exists(), "New original sample, never overwrite user audio");
    juce::AudioBuffer<float> pcm(2, 16384);
    for (int channel = 0; channel < 2; ++channel)
        std::fill_n(pcm.getWritePointer(channel), pcm.getNumSamples(), bed[std::size_t(channel)]);
    auto stream = file.createOutputStream();
    require(bool(stream), "Original stereo source output");
    juce::WavAudioFormat format;
    {
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), double(rate), 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "Original PCM24 exactly representable stereo fractions");
    }
    double decodedRate = 0;
    const auto decoded = readWav(file, decodedRate);
    require(decodedRate == rate && decoded.getNumChannels() == 2 &&
                decoded.getNumSamples() == pcm.getNumSamples(),
            "Actual sample decode shape/rate prerequisite");
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < decoded.getNumSamples(); ++frame)
            require(decoded.getSample(channel, frame) == bed[std::size_t(channel)],
                    "Literal signed stereo source survives actual WAV encode/decode exactly");
    require(folder.getChildFile("source.txt")
                .replaceWithText(file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() +
                                 "\nPCM24 L=.125 R=-.0625,48k,16384frames\n"),
            "Retain exact independently authored sample identity");
    return file;
}

__declspec(noinline) inline void mode(const juce::File &output, bool patternMode, const juce::File &sample)
{
    using namespace routing_lifetime;
    // Fixture's existing AudioEngine is heap-owned; do not put its large fixed
    // MIDI queues on main's default Windows stack. Its initial score is empty.
    auto fixture = std::make_unique<Fixture>(output, false, patternMode,
                                             [&](Project &p)
                                             {
                                                 auto &channel = p.channels.front();
                                                 channel.source = sample.getFullPathName();
                                                 channel.instrument.reset();
                                                 channel.audioClip = false;
                                                 channel.gain = 1;
                                                 channel.pan = 0;
                                                 channel.sampler = {};
                                                 channel.sampler.root = 60;
                                                 channel.sampler.loop = true;
                                                 channel.sampler.envelope = false;
                                                 channel.sampler.normalize = false;
                                                 for (auto &strip : p.mixer)
                                                 {
                                                     strip.gain = 1;
                                                     strip.pan = 0;
                                                     strip.muted = false;
                                                     strip.effects.clear();
                                                     strip.stereoSeparation = 0;
                                                     strip.phaseInverted = false;
                                                     strip.stereoSwapped = false;
                                                 }
                                             });
    auto &f = *fixture;
    auto &p = f.controller.project;
    require(p.validate().wasOk() && te::getDefaultPanLaw() == te::PanLawLinear && !f.audio->metronome(),
            "Valid identity stereo project, original linear pan and silent Guide prerequisite");
    require(awaitProjection(*f.audio, p, patternMode, f.pattern).wasOk(),
            "Actual signed stereo sample fully projected BEFORE any held MIDI event");
    f.audio->setMidiTarget(f.channel);
    auto *context = f.nativeEdit().getTransport().getCurrentPlaybackContext();
    require(context && context->getNodePlayHead(),
            "Actual native context/playhead, never a fake PCM callback");
    const auto *head = context->getNodePlayHead();
    const auto referenceOrigin = head->getReferenceSampleRange().getEnd();
    auto baseline = f.audio->projectionDiagnostics();
    auto mixerInitialisations = f.audio->mixerGraphInitialisations();
    const int starts = f.device->starts, stops = f.device->stops;
    require(baseline.channels.size() == 1 && baseline.channels[0].sample && baseline.channels[0].sampler &&
                f.audio->playing() && f.device->getCurrentBufferSizeSamples() == quantum &&
                f.device->getCurrentSampleRate() == rate,
            "Prepared actual native sample and unchanged48k256 device");
    juce::AudioBuffer<float> block(2, quantum), actual(2, capacity), expected(2, capacity);
    actual.clear();
    expected.clear();
    MatrixOracle matrix;
    VoiceOracle physical, typing;
    int frames = 0, callbacks = 0, expectedKeys = 0;
    double maximumError = 0, maximumMs = 0;
    bool pinned = false;
    juce::String csv =
        "phase,callback,frame,maxError,callbackMs,new,delete,held,edit,context,sampler,sample,"
        "samplerInitialisations,mixerInitialisationsStable,headSeconds,referenceStart,referenceEnd,"
        "expectedReferenceStart,sourceCalls,separation,invert,swap,deviceStarts,deviceStops\n";
    const auto stable = [&](const AudioEngine::ProjectionDiagnostics &now)
    {
        return now.edit == baseline.edit && now.playbackContext == baseline.playbackContext &&
               now.replacements == baseline.replacements &&
               now.samplerCreations == baseline.samplerCreations && now.midiUpdates == baseline.midiUpdates &&
               now.cache.preparations == baseline.cache.preparations && now.channels.size() == 1 &&
               now.channels[0].track == baseline.channels[0].track &&
               now.channels[0].sampler == baseline.channels[0].sampler &&
               now.channels[0].sample == baseline.channels[0].sample &&
               now.channels[0].graphInitialisations == baseline.channels[0].graphInitialisations &&
               now.channels[0].timelineMisses == baseline.channels[0].timelineMisses &&
               f.audio->mixerGraphInitialisations() == mixerInitialisations && f.device->starts == starts &&
               f.device->stops == stops && f.audio->playing() && now.headPlaying && now.transportPlaying &&
               now.channels[0].heldInputKeys == expectedKeys;
    };
    const auto drive = [&](const char *phase)
    {
        require(frames + quantum <= capacity, "Bounded all-startup/transition/release evidence capacity");
        const auto before = f.audio->projectionDiagnostics();
        const auto *target = mixerInsert(p, 1);
        require(target != nullptr, "Actual stable strip1 target");
        matrix.target(*target);
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(block);
        }
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto newCalls = callbackAllocations.load() - allocated,
                   deleteCalls = callbackDestructions.load() - deleted;
        double error = 0;
        for (int n = 0; n < quantum; ++n)
        {
            const double voices = physical.envelope(frames + n) + typing.envelope(frames + n);
            const auto oracle = matrix.process(bed[0] * voices, bed[1] * voices);
            for (int c = 0; c < 2; ++c)
            {
                const auto value = block.getSample(c, n);
                actual.setSample(c, frames + n, value);
                expected.setSample(c, frames + n, float(oracle[std::size_t(c)]));
                error = std::isfinite(value)
                            ? std::max(error, std::abs(double(value) - oracle[std::size_t(c)]))
                            : std::numeric_limits<double>::infinity();
            }
        }
        const auto now = f.audio->projectionDiagnostics();
        auto *current = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        const bool sameHead = current == context && current && current->getNodePlayHead() == head;
        const auto range = sameHead ? head->getReferenceSampleRange() : juce::Range<juce::int64>{};
        const auto source =
            now.channels.size() == 1 ? now.channels.front() : AudioEngine::ChannelDiagnostic{};
        const auto beforeCalls = before.channels.size() == 1 ? before.channels.front().renderCalls : 0;
        const auto expectedReference = referenceOrigin + frames;
        const bool exactClock = range.getStart() == expectedReference && range.getLength() == quantum;
        maximumError = std::max(maximumError, error);
        maximumMs = std::max(maximumMs, elapsed);
        csv += juce::String(phase) + "," + juce::String(callbacks) + "," + juce::String(frames) + "," +
               juce::String(error, 12) + "," + juce::String(elapsed, 6) + "," + juce::String(newCalls) + "," +
               juce::String(deleteCalls) + "," + juce::String(source.heldInputKeys) + "," +
               juce::String(juce::int64(now.edit)) + "," + juce::String(juce::int64(now.playbackContext)) +
               "," + juce::String(juce::int64(source.sampler)) + "," +
               juce::String(juce::int64(source.sample)) + "," +
               juce::String(juce::int64(source.graphInitialisations)) + "," +
               juce::String(int(f.audio->mixerGraphInitialisations() == mixerInitialisations)) + "," +
               juce::String(now.headSeconds, 12) + "," + juce::String(range.getStart()) + "," +
               juce::String(range.getEnd()) + "," + juce::String(expectedReference) + "," +
               juce::String(juce::int64(source.renderCalls - beforeCalls)) + "," +
               juce::String(target->stereoSeparation, 6) + "," + juce::String(int(target->phaseInverted)) +
               "," + juce::String(int(target->stereoSwapped)) + "," + juce::String(f.device->starts) + "," +
               juce::String(f.device->stops) + "\n";
        frames += quantum;
        ++callbacks;
        require(output.getChildFile("callbacks.csv").replaceWithText(csv),
                "Retain every actual native callback before assertion");
        writeRaw(output.getChildFile("actual-planar-f32le.raw"), actual, frames);
        writeRaw(output.getChildFile("expected-planar-f32le.raw"), expected, frames);
        require(output.getChildFile("latest.txt")
                    .replaceWithText("phase=" + juce::String(phase) + " frames=" + juce::String(frames) +
                                     " maxError=" + juce::String(maximumError, 12) +
                                     " maxMs=" + juce::String(maximumMs, 6) + " expectedHeld=" +
                                     juce::String(expectedKeys) + " pinned=" + juce::String(int(pinned)) +
                                     " sameHead=" + juce::String(int(sameHead)) +
                                     " exactClock=" + juce::String(int(exactClock)) + "\n"),
                "Retain PCM/clock/owner result before musical oracle");
        require(error <= 2e-6 && newCalls == 0 && deleteCalls == 0 && elapsed <= 1000. * quantum / rate &&
                    sameHead && exactClock && now.channels.size() == 1 &&
                    source.heldInputKeys == expectedKeys && source.renderCalls == beforeCalls + 1 &&
                    (!pinned || stable(now)),
                "Actual native stereo/attack/release PCM, exact clock, retained owners, zero C++ heap and "
                "unchanged deadline; inspect retained evidence");
        dispatchMessages(6); // Existing CONTROL loop only, outside callback and measured interval.
    };
    // Explicit empty startup callbacks are ALL retained/verified as exact silence,
    // not discarded settling audio. No note exists and the sample is already ready.
    for (int n = 0; n < 4; ++n)
        drive("empty_startup_all_samples_verified");
    baseline = f.audio->projectionDiagnostics();
    mixerInitialisations = f.audio->mixerGraphInitialisations();
    pinned = true;
    const auto apply = [&](const juce::String &id, double value, bool enabled, const char *phase)
    {
        const auto before = f.audio->projectionDiagnostics();
        const auto position = f.audio->positionBeat();
        const auto revision = f.controller.revision;
        const auto count = f.controller.undo.getUndoDescriptions().size();
        CommandArgs args;
        args.mixer = 1;
        args.value = value;
        args.enabled = enabled;
        const auto result = f.controller.execute(id, args);
        require(output.getChildFile("last-command.txt")
                    .replaceWithText(id + " result=" + result.getErrorMessage() +
                                     " revisionBefore=" + juce::String(juce::int64(revision)) +
                                     " revisionAfter=" + juce::String(juce::int64(f.controller.revision)) +
                                     "\n"),
                "Record actual canonical result before assertions");
        require(result.wasOk() && f.controller.revision == revision + 1 &&
                    f.controller.undo.getUndoDescriptions().size() == count + 1,
                "Canonical target adds exactly one real Undo transaction");
        require(awaitProjection(*f.audio, p, patternMode, f.pattern).wasOk(),
                "Actual public AudioEngine live target projection");
        const auto after = f.audio->projectionDiagnostics();
        require(after.edit == before.edit && after.playbackContext == before.playbackContext &&
                    f.audio->positionBeat() == position && f.audio->playing() && f.device->starts == starts &&
                    f.device->stops == stops,
                "Target projection never restarts transport/context/device or moves cursor");
        drive(phase);
    };
    const auto history = [&](bool undoing, const char *phase)
    {
        const auto position = f.audio->positionBeat();
        const auto revision = f.controller.revision;
        require((undoing ? f.controller.undoEditChecked() : f.controller.redoEditChecked()).wasOk() &&
                    f.controller.revision == revision + 1,
                "Actual canonical one-step Undo/Redo changes stereo target");
        require(awaitProjection(*f.audio, p, patternMode, f.pattern).wasOk() &&
                    f.audio->positionBeat() == position,
                "Actual history target projection preserves playhead");
        drive(phase);
    };
    physical.on = frames;
    expectedKeys = 1;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    drive("physical_127_natural_attack_all_samples");
    typing.on = frames;
    expectedKeys = 2;
    f.audio->typingInput.handleIncomingMidiMessage(nullptr,
                                                   juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    drive("typing_127_second_bank_natural_attack");
    apply("mixer.stereoSeparation", .5, false, "separation_positive_ramp");
    apply("mixer.stereoSeparation", -1, false, "separation_negative_antiphase_ramp");
    apply("mixer.phaseInvert", 0, true, "both_polarities_invert_ramp");
    apply("mixer.stereoSwap", 0, true, "swap_ramp");
    history(true, "undo_swap");
    history(true, "undo_invert");
    history(true, "undo_separation");
    history(false, "redo_separation");
    history(false, "redo_invert");
    history(false, "redo_swap");
    apply("mixer.stereoSeparation", 0, false, "restore_separation_zero");
    apply("mixer.phaseInvert", 0, false, "restore_polarity_default");
    apply("mixer.stereoSwap", 0, false, "restore_original_signed_stereo");
    apply("mixer.stereoSeparation", -.5, false, "repeat_negative_half");
    apply("mixer.stereoSeparation", .25, false, "repeat_positive_quarter");
    history(true, "undo_repeat");
    history(false, "redo_repeat");
    apply("mixer.stereoSeparation", 0, false, "final_exact_default_identity");
    physical.off = frames;
    expectedKeys = 1;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    for (int n = 0; n < 3; ++n)
        drive("physical_release_12ms_typing_held");
    drive("typing_held_after_physical_release");
    typing.off = frames;
    expectedKeys = 0;
    f.audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    for (int n = 0; n < 3; ++n)
        drive("final_typing_release_12ms_all_samples");
    drive("released_owners_exact_silence");
    require(block.getMagnitude(0, 0, quantum) == 0 && block.getMagnitude(1, 0, quantum) == 0 &&
                f.audio->projectionDiagnostics().channels.front().heldInputKeys == 0,
            "Both actual input owners release with no stuck note and exact final silence");
    require(
        output.getChildFile("passed.txt")
            .replaceWithText(
                "PASS actual native " + juce::String(patternMode ? "Pattern" : "Song") +
                " callbacks=" + juce::String(callbacks) + " frames=" + juce::String(frames) +
                " maxError=" + juce::String(maximumError, 12) + " maxMs=" + juce::String(maximumMs, 6) +
                "; programmatic MIDI+synthetic device only; no hardware/listening/UI/MCP qualification.\n"),
        "Retain actual completion only after every musical and lifetime assertion");
}

__declspec(noinline) inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned native stereo attempt");
    const auto sample = writeSource(folder);
    routing_lifetime::Storage storage(folder.getChildFile("settings"));
    juce::String firstFailure, summary;
    for (bool patternMode : {true, false})
    {
        const juce::String name = patternMode ? "pattern" : "song";
        try
        {
            mode(folder.getChildFile(name), patternMode, sample);
            summary += name + ": PASS\n";
        }
        catch (const std::exception &error)
        {
            const auto failure = name + ": " + juce::String(error.what());
            summary += failure + "\n";
            if (firstFailure.isEmpty())
                firstFailure = failure;
        }
        catch (...)
        {
            const auto failure = name + ": unknown exception; partial PCM retained";
            summary += failure + "\n";
            if (firstFailure.isEmpty())
                firstFailure = failure;
        }
        require(folder.getChildFile("outcomes.txt").replaceWithText(summary),
                "Retain independent native mode outcomes");
    }
    require(firstFailure.isEmpty(),
            ("Actual native stereo acceptance failed: " + firstFailure).toStdString());
}
} // namespace vibera::test::mixer_stereo_native
