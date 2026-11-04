// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StoppedEffectChangePlanAcceptance.h"
#include "NativeEffectModelAcceptance.h"
#include "Commands.h"
#include "AudioEngine.h"
#include "Sampler.h"
#include "Customization.h"
#include "SyntheticDevice.h"
#include "PluginCatalog.h"
#include "LiveMidi.h"
#include "TypingKeyboard.h"
#include "PerformanceInput.h"
#include "WorkerProcess.h"
#include "Theme.h"
#include "WorkspaceTiling.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include "CallbackAllocationProbe.h"
#include "MidiNoteOwners.h"
#include "LegacySamplerVoices.h"

using namespace vibera;
static int assertions = 0;
static void require(bool result, const std::string &message)
{
    ++assertions;
    if (!result)
        throw std::runtime_error(message);
}
#include "ControllerMappingTests.h"
#include "ChannelLoopAcceptance.h"
#include "PatternExtentAcceptance.h"
#include "HistoryDestinationAcceptance.h"
#include "ControllerPreparedHistoryAcceptance.h"
#include "ControllerStructuralPreparationAcceptance.h"
#include "MidiTakeRecoveryAcceptance.h"
#include "BuildAcceptanceOutput.h"
#include "BuildAcceptanceOutputAcceptance.h"
#include "MixerTargetChoicesAcceptance.h"
#include "MixerAutomationTakeAcceptance.h"
#include "PreparedTimelineAcceptance.h"
#include "ReadyGraphAcceptance.h"
#include "ForwardStageDrainAcceptance.h"
#include "NodePrefixClearAcceptance.h"
#include "SendAutomationNativeExperiment.h"
#include "StableBusStageAcceptance.h"
#include "OutputStageDifferenceAcceptance.h"
#include "WarmSourceRelocationAcceptance.h"
#include "ZeroHistoryCreationAcceptance.h"
#include "ZeroStageExchangeAcceptance.h"
#include "ZeroChannelDelayAcceptance.h"
#include "MixerAudioPortAcceptance.h"
#include <tracktion_graph/tracktion_graph/tracktion_TestUtilities.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestNodes.h>
#include "SendGainStateAcceptance.h"
#include "SendGainTimeAcceptance.h"
#include "PublicationSerialAcceptance.h"
#include "AdoptedGraphLatencyAcceptance.h"
#include "GraphOutputRoutingAcceptance.h"
#include "GraphControlRuntimeOwnershipChecks.h"
#include "FixedInitialisationLifecycleAcceptance.h"
#include "BrowserSearchTests.h"
#include "ParameterAcceptance.h"
#include "PluginBusChoiceAcceptance.h"
#include "PluginMainIoModelAcceptance.h"
#include "PluginMainIoMacroAcceptance.h"
#include "CapturedStructuralHistoryAcceptance.h"
#include "StoppedEffectForwardSdkAcceptance.h"
#include "PhysicalWorkerAcceptance.h"
#include "PreparedPluginTransportAcceptance.h"
#include "PreparedPluginTransportCoverageAcceptance.h"
#include "ControllerRoutingCaptureAcceptance.h"
#include "ControllerNativeInverseAcceptance.h"
#include "RemoteEffectRuntimeAcceptance.h"
#include "InstrumentRuntimeAcceptance.h"
#include "DormantSamplerRuntimeAcceptance.h"
#include "GuideRuntimeAcceptance.h"
#include "ControlRuntimeAcceptance.h"
#include "MixerSendRuntimeAcceptance.h"
#include "PluginBindingTests.h"
#include "PluginGestureTests.h"
#include "PluginControlRequestsTests.h"
#include "PluginScalarHistorySupportAcceptance.h"
#include "NativeScalarCommandAcceptance.h"
#include "TestDispatchPacingTests.h"
#include "CallbackTimingTests.h"
#include "RetainedMidiBoundaryAcceptance.h"
#include "MidiCompensationHistoryTests.h"
#include "MidiCompensationGraphTests.h"
static void tilingTests()
{
    for (auto area : {juce::Rectangle<int>(0, 0, 1000, 550), juce::Rectangle<int>(0, 0, 1800, 950),
                      juce::Rectangle<int>(-1800, -100, 1280, 800)})
        for (int mask = 1; mask < 32; ++mask)
            for (int primary = -1; primary < 5; ++primary)
            {
                std::array<bool, 5> visible{};
                for (int i = 0; i < 5; ++i)
                    visible[size_t(i)] = (mask & (1 << i)) != 0;
                const auto tiles = tileWorkspace(area, visible, primary);
                for (int i = 0; i < 5; ++i)
                    if (visible[size_t(i)])
                    {
                        const auto bounds = tiles.bounds[size_t(i)];
                        require(!bounds.isEmpty() && area.contains(bounds),
                                "tiling retains visible editors inside work area");
                        for (int j = i + 1; j < 5; ++j)
                            if (visible[size_t(j)])
                                require(!bounds.intersects(tiles.bounds[size_t(j)]),
                                        "tiling has no panel overlaps");
                        if (tiles.fits)
                            require(bounds.getWidth() >= (i == 4 ? 190 : 300) && bounds.getHeight() >= 160,
                                    "tiling fits practical frame minima");
                    }
            }
    const auto focused = tileWorkspace({0, 0, 1000, 550}, {true, true, true, true, false}, 2);
    require(focused.focused && focused.bounds[2].getWidth() > focused.bounds[0].getWidth(),
            "focused Playlist uses a prominent area and balanced fallback side regions");
    require(!tileWorkspace({0, 0, 100, 100}, {true, true, true, true, true}).fits,
            "insufficient area does not falsely claim practical minima");
}
#include "MidiScoreAcceptance.h"
static juce::AudioBuffer<float> readWav(const juce::File &f, double &rate)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(f.createInputStream().release(), true));
    require(bool(reader), "WAV reader");
    rate = reader->sampleRate;
    juce::AudioBuffer<float> b(int(reader->numChannels), int(reader->lengthInSamples));
    reader->read(&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}
static juce::Result awaitProjection(AudioEngine &, const Project &, bool, Id);
#include "RetainedContextExperimentAcceptance.h"
#include "ConcurrentRetainedContextAcceptance.h"
#include "RetainedInstrumentContextAcceptance.h"
#include "RetainedInstrumentApplicationRoutingAcceptance.h"
#include "RoutingLifetimeAcceptance.h"
#include "MixerStereoNativeAcceptance.h"
#include "MixerAutomationNativeAcceptance.h"
#include "MixerSendProductionAcceptance.h"
#include "MixerSendModelAcceptance.h"
#include "CapturedMixerStructuralAcceptance.h"
#include "NativeIdleOverheadAcceptance.h"
#include "StoppedEffectNativeAcceptance.h"
#include "StoppedEffectOrdinaryForwardAcceptance.h"
#include "PluginDryHistoryAcceptance.h"
#include "PluginDryHistoryHostAcceptance.h"
#include "ProductionMembershipSdkAcceptance.h"
#include "StoppedEffectInverseSdkAcceptance.h"
#include "AppAudioPortMidiOwnershipAcceptance.h"
#include "NativeMidiIngressDspAcceptance.h"
#include "LegacyMidiRuntimeAcceptance.h"
#include "AppMixerAudioPortAcceptance.h"
#include "NativeMidiTerminalLedgerAcceptance.h"
#include "EffectChainAcceptance.h"
#include "MixerChainCommandPreparationAcceptance.h"
#include "MixerEffectReplacementPreparationAcceptance.h"
#include "MixerInsertLifecycleAcceptance.h"
#include "EffectChainPresetFilesAcceptance.h"
#include "SourceBurstAcceptance.h"
#include "SourceBurstWorkerAcceptance.h"
#include "SourceBurstWorkerOverheadAcceptance.h"
#include "LocalSourceInitialAcceptance.h"
#include "PreparedSourceHostsAcceptance.h"
#include "PreparedSourceInitialHostsAcceptance.h"
#include "InitialAppSourceTransportAcceptance.h"
#include "InitialAppSourceControlsAcceptance.h"
#include "InitialAppAudioPortAcceptance.h"
#include "InitialAppZeroHistoryAcceptance.h"
#include "InitialAppJointSourceRouteAcceptance.h"
#include "InitialAppStableTrackMidiRouteAcceptance.h"
#include "InitialAppActiveEffectPortAcceptance.h"
#include "MixerOrganisationAcceptance.h"
#include "MixerMacroConfigurationAcceptance.h"
#include "MixerNativeIdentityAcceptance.h"
#include "MixerNativeIdentityExtendedAcceptance.h"
#include "SummingPropertiesAcceptance.h"
#include "MixerInsertOrderAcceptance.h"
#include "PluginPcmReceiptAcceptance.h"
#include "PluginPcmPreparedReceiptAcceptance.h"
#include "DynamicInstrumentLatencyAcceptance.h"
#include "PluginAutomationPcmCaptureAcceptance.h"
#include "FixedSourcePcmEpochGuardAcceptance.h"
#include "SourceRenderTimeAcceptance.h"
#include "InitialSourceEpochFenceAcceptance.h"
#include "InitialFiniteEffectBaselineAcceptance.h"
#include "InstalledEffectBusReadinessAcceptance.h"
#include "DeferredDeviceCandidateFaultAcceptance.h"
#include "ChannelRoutingCoordinatorAcceptance.h"
#include "StoppedEffectCommandPreparationAcceptance.h"
#include "MixerChainControlsAcceptance.h"
#include "PluginHostWetMixAcceptance.h"
#include "MixerChainLiveAcceptance.h"
#include "PluginTurnaround512Acceptance.h"
#include "PluginTurnaroundEffectsAcceptance.h"
#include "RetainedWaveContextAcceptance.h"
#include "MidiScoreLiveAcceptance.h"
#include "ScoreToolsAcceptance.h"
#include "NoteGlueAcceptance.h"
#include "RackGroupAcceptance.h"
#include "StepFillAcceptance.h"
#include "StepComposeAcceptance.h"
#include "StepComposeCanonicalAcceptance.h"
#include "StepPitchAcceptance.h"
#include "StepExpressionModelAcceptance.h"
#include "StepShiftAcceptance.h"
#include "StepShiftAudioAcceptance.h"
#include "StepRepeatAcceptance.h"
#include "StepRepeatAudioAcceptance.h"
#include "StepReleaseAcceptance.h"
#include "StepReleaseAudioAcceptance.h"
#include "NoteReleaseMidiAcceptance.h"
#include "NoteReleaseScoreLogAcceptance.h"
#include "NoteReleaseWorkerAcceptance.h"
#include "LiveMidiFrameOrderAcceptance.h"
#include "SamplerNoteExpressionAcceptance.h"
#include "StepActivationCanonicalTests.h"
#include "StepMemoryControllerTests.h"
#include "StepMemoryProfileTests.h"
#include "ChannelSoloAcceptance.h"
#include "ChannelScopedSoloAcceptance.h"
#include "ChannelManagementAcceptance.h"
#include "ChannelAppearanceAcceptance.h"
#include "StepVelocityAcceptance.h"
#include "SwingAcceptance.h"
#include "QuantizerAcceptance.h"
#include "GrooveAcceptance.h"
#include "SamplePreviewAcceptance.h"
#include "AudioClipAcceptance.h"
#include "WaveformWorkerAcceptance.h"
#include "PlaylistEditAcceptance.h"
#include "ClipGroupsAcceptance.h"
#include "ClipGroupsLiveAcceptance.h"
#include "NoteMuteAcceptance.h"
#include "NoteSliceAcceptance.h"
#include "NoteStretchAcceptance.h"
#include "MixerRoutingAcceptance.h"
#include "MixerDetectorModelAcceptance.h"
#include "MixerRoutingLiveAcceptance.h"
#include "MixerControlsAcceptance.h"
#include "MixerStereoAcceptance.h"
#include "MixerAutomationCoreAcceptance.h"
#include "MixerRiskAcceptance.h"
#include "PluginSidechainTransportAcceptance.h"
#include "PluginSidechainGraphAcceptance.h"
#include "PluginDynamicLatencyAcceptance.h"
#include "PluginDefaultDynamicLatencyAcceptance.h"
#include "PluginDynamicWaveformAcceptance.h"
#include "PluginEditorRequestsTests.h"
#include "PluginEditorProtocolAcceptance.h"
#include "PluginWrapperProtocolAcceptance.h"
#include "PluginWrapperViewAcceptance.h"
#include "PluginEditorHostAcceptance.h"
#include "PluginStateAdmissionTests.h"
#include "PluginNativeEditorAttachmentTests.h"
#include "GraphPublicationTests.h"
#include "PluginBypassAcceptance.h"
#include "NoteSliceLiveAcceptance.h"
#include "NoteStretchLiveAcceptance.h"
#include "NoteMuteLiveAcceptance.h"
#include "PatternManagementAcceptance.h"
#include "SamplerAcceptance.h"
#include "SamplerVoicesAcceptance.h"
#include "SamplerVoiceModeModelAcceptance.h"
#include "SamplerMonoPortamentoAcceptance.h"
#include "SamplerNoteModesModelAcceptance.h"
#include "ChannelPresetAcceptance.h"
#include "PatternBulkAcceptance.h"
#include "SamplerFadeAcceptance.h"
#include "AiAcceptance.h"
#include "AudioCaptureAcceptance.h"
#include "AudioCompAcceptance.h"
#include "CompositionToolsAcceptance.h"
#include "PlaylistMarkerAcceptance.h"
#include "TempoMapAcceptance.h"
#include "FlpImportAcceptance.h"
#include "ChordProgressionAcceptance.h"
#include "ControllerRecordingAcceptance.h"
#include "ChannelTuningAcceptance.h"
#include "AudioStretchAcceptance.h"
#include "UpdaterAcceptance.h"
#include "SamplerStretchAcceptance.h"
#include "ArrangementAcceptance.h"
#include "PlaylistTrackAcceptance.h"
#include "MidiImportExtrasAcceptance.h"
#include "MissingMediaAcceptance.h"
#include "SamplerPitchBendAcceptance.h"
#include "SongMeterAcceptance.h"
#include "SlicerAcceptance.h"
#include "SamplerNoteModesAudioAcceptance.h"
#include "SamplerNoteModesMidiAcceptance.h"
#include "SamplerVoiceModesLiveAcceptance.h"
#include "SamplerNormalizeAcceptance.h"
#include "SamplerFilterModelAcceptance.h"
#include "SamplerFilterAudioAcceptance.h"
#include "SamplerModulationModelAcceptance.h"
#include "SamplerModulationAudioAcceptance.h"
#include "SamplerEnvelopeModelAcceptance.h"
#include "SamplerControllerLinksAcceptance.h"
#include "SamplerControllerRuntimeAcceptance.h"
#include "SamplerControllerContracts.h"
#include "SamplerEnvelopeAudioAcceptance.h"
#include "SamplerEnvelopeVoiceAcceptance.h"
#include "SamplerModulationAutomationAcceptance.h"
#include "SamplerAutomationMacroAcceptance.h"
#include "SamplerAutomationParametersAcceptance.h"
#include "SamplerLfoPhaseBlockAcceptance.h"
#include "SamplerAutomationEventReferenceAcceptance.h"
#include "LoopBoundsAcceptance.h"
#include "SamplerPreviewAcceptance.h"
#include "AutomationProjectAcceptance.h"
#include "OwnedGestureAcceptance.h"
#include "AutomationPlaybackAcceptance.h"
static void scoreLogTests(const juce::File &folder)
{
    auto p = Project::empty();
    ScoreLog log;
    auto input = [&](Id channel, unsigned source, double time, int pitch, bool on, unsigned epoch = 1)
    {
        log.add({channel,
                 time,
                 1,
                 epoch,
                 {static_cast<unsigned char>(on ? 0x90 : 0x80), static_cast<unsigned char>(pitch), 100},
                 source});
    };
    input(1, 0, 100, 60, true);
    input(1, 1, 100.1, 60, true);
    input(2, 0, 100.2, 64, true);
    input(1, 0, 101, 60, false);
    input(1, 1, 101.1, 60, false);
    input(2, 0, 101.2, 64, false);
    auto notes = log.dump(102, 180, 120, p);
    require(notes.size() == 3 && notes[0].channel == 1 && notes[2].channel == 2,
            "score history retains mixed Channel/source ownership");
    require(notes[0].beat == 0 && std::abs(notes[1].beat - .2) < 1e-9 && std::abs(notes[0].length - 2) < 1e-9,
            "score history aligns first note and preserves gaps/duration");
    Controller controller;
    controller.replace(p);
    auto old = p.state();
    CommandArgs args;
    args.pattern = p.patterns[0].id;
    args.enabled = false;
    args.recordedNotes = notes;
    require(controller.execute("notes.record", args).wasOk(), "score dump uses canonical one-undo command");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(old), "score dump one undo");
    controller.redoEdit();
    require(controller.project.patterns[0].notes.size() == 3, "score dump redo");
    auto file = folder.getChildFile("score-dumped.vibra");
    require(saveProject(controller.project, file).wasOk(), "score dump save");
    Project reopened;
    require(loadProject(file, reopened).wasOk() &&
                reopened.state().isEquivalentTo(controller.project.state()),
            "score dump exact project round trip");
    log.clear();
    input(1, 0, 0, 60, true);
    input(1, 0, 1900, 60, false);
    auto clipped = log.dump(1901, 1800, 120, p);
    require(clipped.size() == 1 && std::abs(clipped[0].length - 3598) < 1e-9,
            "long held note is clipped to thirty-minute window");
    input(1, 0, 1902, 62, true);
    log.interrupt(1, 0, 1903);
    require(log.dump(1904, 3, 120, p).size() == 1, "source/Channel switch closes held score at boundary");
    log.prune(4000);
    require(log.dump(4000, 1800, 120, p).empty(), "old history expires");
    log.clear();
    input(1, 0, 100, 60, true);
    input(1, 0, 101, 62, true, 2);
    input(1, 0, 102, 62, false, 2);
    auto epochs = log.dump(103, 180, 120, p);
    require(epochs.size() == 2 && epochs[0].length == 2,
            "epoch changes close old held keys rather than making stuck notes");
    p.channels.erase(p.channels.begin());
    require(log.dump(103, 180, 120, p).empty(), "removed Channel history is not misassigned");
    ScoreQueue queue;
    unsigned char on[]{0x90, 60, 100};
    queue.push(1, 1, on);
    MidiCapture::Event event;
    require(queue.pop(event) && event.channel == 1 && event.seconds > 0,
            "history logs input without transport/device clock");
    for (int i = 0; i < 4097; ++i)
        queue.push(1, 1, on);
    require(queue.failed.load(), "score history queue capacity/overflow is explicit");
    queue.clear();
    require(!queue.pop(event) && !queue.failed.load(), "clear invalidates pending history safely");
    MidiInputRouter router(false);
    router.setHistoryTarget(1);
    router.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    require(router.history.pop(event) && event.channel == 1,
            "always-on history works before instrument lane/audio device preparation");
    ScoreLog capacityLog;
    for (int i = 0; i < 65536; ++i)
    {
        double t = i * .01;
        capacityLog.add({1, t, 1, 1, {0x90, 60, 100}, 0});
        capacityLog.add({1, t + .005, 1, 1, {0x80, 60, 0}, 0});
    }
    capacityLog.add({1, 700, 1, 1, {0x90, 62, 100}, 0});
    require(capacityLog.dump(701, 1800, 120, Project::empty()).size() == 65537,
            "dump snapshot includes held notes without evicting complete history at capacity");
    auto longProject = Project::empty();
    longProject.patterns[0].length = 1800. * 522 / 60.;
    require(longProject.validate().wasOk(),
            "thirty-minute high-tempo score is not limited to old 4096-beat pattern cap");
    folder.getChildFile("score-log-verification.txt")
        .replaceWithText("PASS: mixed-source/Channel timestamp pairing, selected-pattern dump/one "
                         "undo/redo/exact persistence, history clipping/expiry, held/switch/epoch recovery, "
                         "clock-independent bounded input queue/overflow/clear and long-pattern validation. "
                         "Synthetic timestamps; no hardware/listening.\n");
}
static void typingKeyTests()
{
    PerformanceInput owners;
    int ons = 0, offs = 0;
    owners.emit = [&](int, bool on, int)
    {
        if (on)
            ++ons;
        else
            ++offs;
    };
    owners.set(1, 60, true);
    owners.set(0, 60, true);
    owners.set(1, 60, false);
    require(ons == 1 && offs == 0, "preview/typing ownership prevents premature release");
    owners.release(0);
    require(offs == 1, "last performance owner releases voice");
    TypingKeyboard keys;
    int on = 0, off = 0, last = 0;
    keys.emit = [&](int pitch, bool active, int)
    {
        if (active)
            ++on;
        else
            ++off;
        last = pitch;
    };
    require(!keys.press('Z'), "disabled typing keyboard does not intercept editing");
    keys.enabled = true;
    require(keys.press('Z') && last == 60, "Z maps to root C");
    keys.press('Z');
    require(on == 1, "typing repeat does not retrigger");
    keys.press('X');
    require(last == 62, "typing chromatic key layout");
    keys.poll([](int key) { return key == 'X'; });
    require(off == 1, "key state releases missing key");
    keys.root = 48;
    keys.release('X');
    require(last == 62 && off == 2, "release uses original pitch after root change");
    keys.root = 60;
    keys.press(',');
    keys.press('Q');
    require(on == 3, "duplicate keyboard pitch has one note-on");
    keys.release(',');
    require(off == 2, "one overlapping typing key does not cut another");
    keys.release('Q');
    require(off == 3, "last owner releases typing pitch");
    keys.press('Z');
    keys.press('Q');
    keys.releaseAll();
    require(off == 5, "focus release clears all held typing notes");
    MidiTake take;
    take.begin(1, 0, 0, 120, 0);
    take.add({1, 0, 1, 1, {0x90, 60, 100}, 0});
    take.add({1, .1, 1, 20, {0x90, 60, 70}, 1});
    take.add({1, .2, 1, 1, {0x80, 60, 0}, 0});
    take.add({1, .3, 1, 20, {0x80, 60, 0}, 1});
    auto notes = take.finish(.4);
    require(notes.size() == 2 && std::abs(notes[0].length - .4) < 1e-10 &&
                std::abs(notes[1].length - .4) < 1e-10,
            "physical and typing capture use independent note/epoch ownership");
}
static void guideDspTests(const juce::File &folder)
{
    auto prefs = folder.getChildFile("guide-prefs.json");
    {
        AudioEngine writer(false, prefs);
        writer.setMetronome(true);
        writer.setGuideSound(1);
        writer.setGuideGain(.37f);
        writer.setCountInBars(2);
        writer.setCountInBars(0);
    }
    {
        AudioEngine reader(false, prefs);
        require(reader.metronome() && reader.guideSound() == 1 &&
                    std::abs(reader.guideGain() - .37f) < 1.e-6 && reader.countInBars() == 0 &&
                    reader.preferredCountInBars() == 2,
                "isolated guide preference round trip preserves disabled count's selected bar length");
    }
    Controller meter;
    CommandArgs signature;
    signature.numerator = 7;
    signature.denominator = 8;
    auto initial = meter.project.state();
    require(meter.execute("project.meter", signature).wasOk(), "canonical 7/8 meter command");
    auto signatureState = meter.project.state();
    meter.undoEdit();
    require(meter.project.state().isEquivalentTo(initial), "meter undo retains notes/pattern lengths");
    meter.redoEdit();
    require(meter.project.state().isEquivalentTo(signatureState), "meter redo exact");
    require(meter.execute("pattern.add").wasOk() && meter.project.patterns.back().length == 3.5,
            "new 7/8 pattern is one bar");
    signature.denominator = 3;
    auto validState = meter.project.state();
    require(meter.execute("project.meter", signature).failed() &&
                meter.project.state().isEquivalentTo(validState),
            "invalid meter rejected without document mutation");
    for (auto rate : {48000., 44100.})
    {
        GuideControl controls;
        controls.enabled = true;
        GuideDsp dsp;
        std::vector<int> positions;
        std::vector<bool> accents;
        juce::AudioBuffer<float> buffer(2, 211);
        int total = int(rate * 2);
        float peak = 0;
        for (int frame = 0; frame < total;)
        {
            int count = std::min(211, total - frame);
            buffer.clear();
            dsp.process(buffer, 0, count, frame / rate, rate, true, 120, 4, 4, controls);
            for (int i = 0; i < dsp.hitCount; ++i)
            {
                positions.push_back(frame + dsp.lastHits[size_t(i)].frame);
                accents.push_back(dsp.lastHits[size_t(i)].accent);
            }
            peak = std::max(peak, buffer.getMagnitude(0, count));
            frame += count;
        }
        require(positions == std::vector<int>{0, int(rate / 2), int(rate), int(rate * 1.5)},
                "metronome exact sample positions across uneven buffers at two rates");
        require(accents == std::vector<bool>{true, false, false, false} && peak > .05f,
                "bar accent and actual original click PCM");
        controls.enabled = false;
        buffer.clear();
        dsp.process(buffer, 0, 211, 2, rate, true, 120, 4, 4, controls);
        require(buffer.getMagnitude(0, 211) == 0, "disabled guide is silent");
    }
    GuideControl controls;
    controls.begin(-1.75, 0);
    controls.ready = true;
    GuideDsp dsp;
    juce::AudioBuffer<float> buffer(2, 256);
    std::vector<int> positions;
    for (int frame = 0; frame < 84000;)
    {
        int count = std::min(256, 84000 - frame);
        buffer.clear();
        dsp.process(buffer, 0, count, -1.75 + frame / 48000., 48000, true, 120, 7, 8, controls);
        for (int i = 0; i < dsp.hitCount; ++i)
            positions.push_back(frame + dsp.lastHits[size_t(i)].frame);
        frame += count;
    }
    require(positions == std::vector<int>{0, 12000, 24000, 36000, 48000, 60000, 72000},
            "7/8 count-in has seven eighth-note pulses, not seven quarters");
    require(controls.remaining.load() == 0, "countdown completes at exact meter boundary");
    buffer.clear();
    dsp.process(buffer, 0, 256, 0, 48000, true, 120, 7, 8, controls);
    require(buffer.getMagnitude(0, 256) == 0 && dsp.hitCount == 0,
            "count-in alone produces no continuing click");
    GuideControl shifted;
    shifted.begin(-1.75, 0);
    shifted.ready = true;
    GuideDsp shiftedDsp;
    for (int frame = 0; frame < 84000; frame += 256)
    {
        buffer.clear();
        shiftedDsp.process(buffer, 0, std::min(256, 84000 - frame), -1.75 + (frame + 1) / 48000., 48000, true,
                           120, 7, 8, shifted);
    }
    require(shifted.hits.load() == 7,
            "one-sample engine timeline offset retains the first count beat without duplicate boundary hits");
    folder.getChildFile("guide-dsp.txt")
        .replaceWithText(
            "PASS: exact 4/4 beat frames at 48/44.1 kHz across 211-frame blocks; 7/8 seven-pulse lead-in; "
            "accent, PCM, off and post-count silence. Original prepared waves; no listening claim.\n");
}
static void guideIntegrationTests(const juce::File &folder, const PluginSpec *plugin = nullptr,
                                  int startupMs = 20)
{
    AudioEngine audio(false, {}, 256);
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
    require(device != nullptr, "guide virtual device");
    juce::AudioBuffer<float> buffer(2, 256);
    juce::String evidence;
    int providerCase = 0;
    for (bool pattern : {true, false})
        for (double target : {0., 2.})
        {
            auto p = Project::empty();
            p.channels[0].source = "builtin:bass";
            p.patterns[0].notes.push_back({p.allocate(), p.channels[0].id, 0, 4, 60, .8f, false});
            p.placements.push_back({p.allocate(), p.patterns[0].id, 0, 0, 4, 0});
            if (plugin)
            {
                p.nextId += 1000 * ++providerCase; // Fresh instrument: no tail from a previous fixture.
                p.channels[0].instrument = *plugin;
                p.channels[0].instrument->id = p.allocate();
            }
            audio.stop();
            require(awaitProjection(audio, p, pattern, p.patterns[0].id).wasOk(), "guide projection ready");
            audio.setMidiTarget(p.channels[0].id);
            audio.seekBeat(target);
            dispatchMessages(20);
            for (int i = 0; i < (plugin ? 180 : 8); ++i)
            {
                device->process(buffer);
                dispatchMessages(plugin ? 6 : 1);
            }
            auto before = audio.projectionDiagnostics();
            int starts = device->starts, stops = device->stops;
            audio.setGuideGain(0);
            audio.playStop(1);
            dispatchMessages(startupMs);
            int first = -1;
            juce::String trace;
            double maxMs = 0;
            for (int frame = 0; frame < 99072; frame += 256)
            {
                auto time = juce::Time::getMillisecondCounterHiRes();
                device->process(buffer);
                maxMs = std::max(maxMs, juce::Time::getMillisecondCounterHiRes() - time);
                for (int i = 0; i < 256; ++i)
                    if (first < 0 && std::abs(buffer.getSample(0, i)) > .00001f)
                        first = frame + i;
                if (frame % (256 * 16) == 0)
                {
                    auto d = audio.projectionDiagnostics();
                    trace += "frame=" + juce::String(frame) +
                             " transport=" + juce::String(int(audio.playing())) +
                             " head=" + juce::String(int(d.headPlaying)) +
                             " pending=" + juce::String(int(d.playPending)) +
                             " headTime=" + juce::String(d.headSeconds, 6) +
                             " graph=" + juce::String(juce::int64(d.channels[0].graphInitialisations)) +
                             " initialGraph=" +
                             juce::String(juce::int64(before.channels[0].graphInitialisations)) +
                             " fc=" + juce::String(int(d.channels[0].lastPlaying)) +
                             " time=" + juce::String(d.channels[0].lastTime, 6) +
                             " skip=" + juce::String(d.channels[0].lastLeadSkip) + "\n";
                }
                dispatchMessages(plugin ? 6 : 1);
            }
            folder
                .getChildFile(juce::String(pattern ? "guide-pattern" : "guide-song") + "-" +
                              juce::String(target) + "-trace.txt")
                .replaceWithText(trace);
            evidence += juce::String(pattern ? "Pattern" : "Song") + " target=" + juce::String(target) +
                        " firstMusicFrame=" + juce::String(first) +
                        " expected=96000 maxCallbackMs=" + juce::String(maxMs, 4) + "\n";
            folder.getChildFile(plugin ? "guide-vst3-integration.txt" : "guide-integration.txt")
                .replaceWithText(evidence);
            auto diagnostic = audio.projectionDiagnostics();
            evidence += "position=" + juce::String(audio.positionBeat(), 6) +
                        " remaining=" + juce::String(audio.countInRemaining(), 6) + "\n";
            for (auto &lane : diagnostic.channels)
                evidence += "channel=" + juce::String(lane.channel) +
                            " time=" + juce::String(lane.lastTime, 6) +
                            " skip=" + juce::String(lane.lastLeadSkip) +
                            " playing=" + juce::String(int(lane.lastPlaying)) +
                            " calls=" + juce::String(juce::int64(lane.renderCalls)) + "\n";
            folder.getChildFile(plugin ? "guide-vst3-integration.txt" : "guide-integration.txt")
                .replaceWithText(evidence);
            require(first >= 95998 && first < (plugin ? 100096 : 96256),
                    "music begins after exact one-bar count; earlier Song clips suppressed");
            auto after = audio.projectionDiagnostics();
            require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                        before.channels[0].graphInitialisations == after.channels[0].graphInitialisations &&
                        device->starts == starts && device->stops == stops && audio.playing(),
                    "count-in preserves prepared graph/device and continues playback");
            audio.setMetronome(true);
            audio.setGuideSound(2);
            audio.setGuideGain(.65f);
            require(audio.guideSound() == 2 && audio.guideGain() == .65f,
                    "live guide controls accepted without document changes");
            audio.stop();
            audio.setMetronome(false);
        }
}
static void precisionPersistenceTests(const juce::File &folder)
{
    auto project = Project::empty();
    project.tempo = std::nextafter(120., 121.);
    project.patterns[0].length = 2048;
    project.patterns[0].autoLength = false; // Preserve the literal precision-test source horizon.
    for (double base : {.001, .01, .1, .25, 1., 4.5866666666666535, 10., 100., 1000.})
    {
        double value = base;
        for (int i = 0; i < 24; ++i)
        {
            project.patterns[0].notes.push_back({project.allocate(), project.channels[0].id, value,
                                                 std::nextafter(.2666666666666595, double(i)), 60, 80.f / 127,
                                                 false});
            value = std::nextafter(value, base + 1);
        }
    }
    project.placements.push_back({project.allocate(), project.patterns[0].id, 0, std::nextafter(1., 2.), 2048,
                                  std::nextafter(.1, .2)});
    auto file = folder.getChildFile("precision-roundtrip.vibra");
    require(saveProject(project, file).wasOk(), "save arbitrary recorded timing values");
    Project loaded;
    require(loadProject(file, loaded).wasOk(), "read precision project");
    require(loaded.tempo == project.tempo && loaded.placements[0].beat == project.placements[0].beat &&
                loaded.placements[0].offset == project.placements[0].offset,
            "tempo/placement binary64 precision survives XML");
    for (size_t i = 0; i < project.patterns[0].notes.size(); ++i)
        require(loaded.patterns[0].notes[i].beat == project.patterns[0].notes[i].beat &&
                    loaded.patterns[0].notes[i].length == project.patterns[0].notes[i].length,
                "exact note timing round trip across adjacent binary64 values");
    require(loaded.state().isEquivalentTo(project.state()), "full project canonical precision round trip");
    folder.getChildFile("precision-roundtrip.txt")
        .replaceWithText(
            "PASS: 216 adjacent binary64 note starts/durations, fractional tempo, placement/offset, exact "
            "canonical state. Locale-independent max_digits10 save; no widened comparison tolerance.\n");
}
static void modelTests(const juce::File &folder)
{
    auto expanded = Project::empty();
    require(expanded.mixer.size() == 18 && expanded.mixer.back().index == 17,
            "Master plus seventeen inserts");
    Controller highInsert;
    highInsert.replace(expanded);
    auto beforeAssignment = highInsert.project.state();
    CommandArgs route;
    route.channel = expanded.channels.front().id;
    route.mixer = 17;
    require(highInsert.execute("channel.mixer", route).wasOk(), "route Channel to Insert 17");
    route.value = .33;
    require(highInsert.execute("mixer.gain", route).wasOk(), "Insert 17 gain control");
    Project highRestored;
    require(Project::parse(highInsert.project.state(), highRestored).wasOk() &&
                highRestored.state().isEquivalentTo(highInsert.project.state()),
            "Insert 17 exact project round trip");
    highInsert.undoEdit();
    highInsert.undoEdit();
    require(highInsert.project.state().isEquivalentTo(beforeAssignment),
            "Insert 17 assignment and gain undo");
    auto legacyMixer = expanded.state();
    auto strips = legacyMixer.getChildWithName("MIXER");
    while (strips.getNumChildren() > 9)
        strips.removeChild(strips.getNumChildren() - 1, nullptr);
    strips.getChild(2).setProperty("gain", .42, nullptr);
    require(Project::parse(legacyMixer, highRestored).wasOk() && highRestored.mixer.size() == 9 &&
                std::abs(highRestored.mixer[2].gain - .42f) < .0001f,
            "Current nine-strip project preserves its exact universe and controls");
    // Explicit pre-group encoding: a current-schema project is not a legacy
    // fixture merely because its strip count resembles the old default.
    legacyMixer.setProperty("version", 22, nullptr);
    stripSamplerFilterForLegacy(legacyMixer);
    for (auto pattern : legacyMixer.getChildWithName("PATTERNS"))
        for (const auto *field : {"autoLength", "numerator", "denominator"})
            pattern.removeProperty(field, nullptr);
    legacyMixer.removeChild(legacyMixer.getChildWithName("MIXER_GROUPS"), nullptr);
    require(Project::parse(legacyMixer, highRestored).wasOk() && highRestored.mixer.size() == 18 &&
                std::abs(highRestored.mixer[2].gain - .42f) < .0001f,
            "old default nine-strip project expands without losing controls");
    const auto originalBackground = bg;
    bg = juce::Colour(0xff223344);
    auto appearance = folder.getChildFile("appearance.json");
    require(saveTheme(appearance).wasOk(), "appearance profile save");
    bg = originalBackground;
    require(loadTheme(appearance).wasOk() && bg == juce::Colour(0xff223344), "appearance profile round trip");
    auto invalidTheme = juce::JSON::parse(appearance);
    invalidTheme.getDynamicObject()->setProperty("Primary text", "bad");
    require(appearance.replaceWithText(juce::JSON::toString(invalidTheme)), "invalid appearance fixture");
    require(loadTheme(appearance).failed() && bg == juce::Colour(0xff223344),
            "invalid appearance has no partial application");
    resetTheme();
    Controller c;
    c.replace(Project::demo());
    auto original = c.project.state();
    require(c.project.validate().wasOk(), "demo validation");
    auto notes = sequence(c.project, false, c.project.patterns.front().id);
    require(notes.size() == c.project.patterns.front().notes.size() * 4, "linked sequence expansion");
    require(notes.front().beat == 0, "first event beat");
    CommandArgs a;
    a.pattern = c.project.patterns.front().id;
    a.channel = c.project.channels[3].id;
    a.beat = .25;
    a.length = .5;
    a.pitch = 64;
    a.value = .7;
    require(c.execute("notes.add", a).wasOk(), "add note command");
    require(sequence(c.project, false, a.pattern).size() == notes.size() + 4,
            "source edit affects all linked placements");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "undo restores entire canonical project");
    c.redoEdit();
    require(c.project.pattern(a.pattern)->notes.size() == notes.size() / 4 + 1, "redo restores note");
    a.target = c.project.placements[1].id;
    require(c.execute("clip.unique", a).wasOk(), "Make Unique command");
    require(c.project.placements[1].pattern != a.pattern, "unique source identity");
    auto uniqueId = c.project.placements[1].pattern;
    auto uniqueCount = c.project.pattern(uniqueId)->notes.size();
    a.beat = .5;
    require(c.execute("notes.add", a).wasOk(), "edit linked source again");
    require(c.project.pattern(uniqueId)->notes.size() == uniqueCount, "unique pattern unaffected");
    auto beforeGesture = c.project.state();
    auto noteId = c.project.pattern(a.pattern)->notes.back().id;
    c.beginGesture();
    a.ids = {noteId};
    a.beat = .25;
    a.length = 0;
    a.pitch = 0;
    require(c.execute("notes.move", a).wasOk(), "gesture move one");
    require(c.execute("notes.move", a).wasOk(), "gesture move two");
    c.endGesture("drag");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(beforeGesture), "one undo for complete drag");
    a.mixer = 2;
    a.channel = c.project.channels[0].id;
    require(c.execute("channel.mixer", a).wasOk(), "shared insert assignment");
    require(c.project.channels[0].mixer == c.project.channels[1].mixer, "many Channels to one insert");
    auto saved = c.project.state();
    auto file = folder.getChildFile("acceptance.vibra");
    require(saveProject(c.project, file).wasOk(), "atomic project save");
    Project loaded;
    require(loadProject(file, loaded).wasOk(), "project load");
    require(loaded.state().isEquivalentTo(saved), "project semantic round trip");
    auto invalid = saved.createCopy();
    invalid.setProperty("version", 999, nullptr);
    auto stable = loaded.state();
    require(Project::parse(invalid, loaded).failed(), "future version rejected");
    require(loaded.state().isEquivalentTo(stable), "failure leaves document unchanged");
    require(folder.getChildFile("corrupt.vibra").replaceWithText("<broken>"), "corrupt fixture write");
    require(loadProject(folder.getChildFile("corrupt.vibra"), loaded).failed(), "corruption reported");
    auto window = Project::empty();
    auto &pat = window.patterns.front();
    pat.length = 8;
    pat.notes.push_back({window.allocate(), window.channels[0].id, 1, .5, 60, 1, false});
    pat.notes.push_back({window.allocate(), window.channels[0].id, 3, .5, 60, 1, false});
    window.placements.push_back({window.allocate(), pat.id, 1, 8, 2, 2});
    auto cropped = sequence(window, false, pat.id);
    require(cropped.size() == 1 && cropped[0].beat == 9, "placement source offset and window");
}
static void projectionTests(const juce::File &folder)
{
    AudioEngine audio(false);
    require(audio.engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio() == 1,
            "Live and render graphs have no auxiliary worker completion dependency");
    require(audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr,
            "headless engine opens no physical device");
    auto p = Project::demo();
    auto pattern = p.patterns[0].id;
    require(audio.rebuild(p, true, pattern).wasOk(), "initial live projection");
    auto initial = audio.projectionDiagnostics();
    require(initial.replacements == 1 && initial.cache.preparations == 4 && initial.channels.size() == 4,
            "initial projection resources");
    audio.seekBeat(3);
    p.mixer[0].gain = .5f;
    p.channels[0].gain = .3f;
    p.channels[1].pan = -.4f;
    require(audio.rebuild(p, true, pattern).wasOk(), "incremental gain and pan projection");
    auto d = audio.projectionDiagnostics();
    require(d.edit == initial.edit && d.replacements == 1 && d.midiUpdates == 0,
            "gain updates retain Edit and MIDI");
    require(d.cache.preparations == 4 && d.samplerCreations == 4,
            "gain updates reuse prepared samples and plugins");
    require(std::abs(audio.positionBeat() - 3) < .00001, "gain updates preserve transport position");
    for (size_t i = 0; i < initial.channels.size(); ++i)
        require(d.channels[i].track == initial.channels[i].track &&
                    d.channels[i].sampler == initial.channels[i].sampler,
                "incremental channel identities retained");
    p.patterns[0].notes.push_back({p.allocate(), p.channels[3].id, 2, .5, 64, .8f, false});
    require(audio.rebuild(p, false, pattern).wasOk(), "Song mode and linked note projection");
    d = audio.projectionDiagnostics();
    require(d.channels[3].midiNotes == 36 && d.edit == initial.edit,
            "linked placements projected in retained channel");
    require(d.cache.preparations == 4 && d.samplerCreations == 4,
            "note edits do not prepare instruments again");
    p.channels[0].mixer = 2;
    require(audio.rebuild(p, false, pattern).wasOk(), "incremental many-to-one routing");
    p.tempo = 90;
    require(audio.rebuild(p, false, pattern).wasOk() && std::abs(audio.positionBeat() - 3) < .00001,
            "tempo update keeps musical cursor");
    auto beforeFailure = audio.projectionDiagnostics();
    auto unavailable = p;
    unavailable.channels[3].source = folder.getChildFile("unavailable-live.wav").getFullPathName();
    unavailable.channels[0].gain = .01f;
    require(audio.rebuild(unavailable, false, pattern).failed(),
            "missing source rejects whole projection before mutation");
    d = audio.projectionDiagnostics();
    require(d.edit == beforeFailure.edit && d.updates == beforeFailure.updates &&
                d.channels[0].sample == beforeFailure.channels[0].sample,
            "failed projection keeps working Edit and sources");
    p.channels[0].source = "builtin:snare";
    require(audio.rebuild(p, false, pattern).wasOk(), "prepared source replacement");
    d = audio.projectionDiagnostics();
    require(d.channels[0].sampler != initial.channels[0].sampler &&
                d.channels[0].track == initial.channels[0].track,
            "source replacement changes only affected plugin");
    require(d.channels[0].sample == d.channels[1].sample && d.cache.preparations == 4,
            "channels share immutable sample data");
    auto current = d;
    p.channels.push_back({p.allocate(), "New hat", "builtin:hat", 0, .5f, 0, false});
    require(audio.rebuild(p, false, pattern).wasOk(), "structural edit fallback");
    d = audio.projectionDiagnostics();
    require(d.replacements == 2 && d.edit != current.edit && d.cache.preparations == 4,
            "structural fallback prepares no duplicate sample data");
    auto stable = d;
    unavailable = p;
    unavailable.channels.push_back({unavailable.allocate(), "Missing",
                                    folder.getChildFile("missing-structural.wav").getFullPathName(), 0, .5f,
                                    0, false});
    require(audio.rebuild(unavailable, false, pattern).failed(), "invalid structural replacement rejected");
    require(audio.projectionDiagnostics().edit == stable.edit,
            "failed structural replacement preserves previous Edit");
    SampleCache tiny(1);
    std::shared_ptr<const Sample> sample;
    require(tiny.prepare("builtin:bass", sample).wasOk() && tiny.statistics().entries == 0,
            "bounded cache evicts oversized entry");
    VoiceBank voices;
    voices.prepare(*sample, 48000);
    voices.noteOn(60, .8f);
    juce::AudioBuffer<float> b(2, 256);
    b.clear();
    voices.render(b, 0, 256);
    require(b.getMagnitude(0, 256) > .05f, "eviction preserves pinned immutable sample lifetime");
    auto held = sample;
    require(tiny.prepare("builtin:unknown", sample).failed() && sample == held,
            "failed preparation preserves caller resource");
}
static juce::Result awaitProjection(AudioEngine &audio, const Project &project, bool mode, Id pattern)
{
    auto deadline = juce::Time::getMillisecondCounter() + 30000;
    for (;;)
    {
        auto result = audio.rebuild(project, mode, pattern);
        if (result.wasOk() || result.getErrorMessage() != "Preparing VST3 workers...")
            return result;
        if (juce::Time::getMillisecondCounter() > deadline)
            return juce::Result::fail("Plugin preparation test deadline");
        dispatchMessages(5);
    }
}
static void callbackProjectionTests(const juce::File &folder, bool patternMode,
                                    const std::vector<PluginSpec> *external = nullptr,
                                    bool automation = false)
{
    AudioEngine audio(false, {}, 256);
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement settings("DEVICESETUP");
    settings.setAttribute("deviceType", "VibeRa synthetic");
    settings.setAttribute("audioOutputDeviceName", "In-memory output");
    settings.setAttribute("audioDeviceRate", 48000.);
    settings.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, settings);
    manager.initialise(0, 2);
    dispatchMessages(30);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device != nullptr, "synthetic output selected without physical hardware");
    auto p = Project::empty();
    p.channels.resize(1);
    p.channels[0].source = "builtin:bass";
    p.channels[0].gain = 1;
    p.mixer[0].gain = 1;
    p.patterns[0].length = 2;
    p.patterns[0].notes.push_back({p.allocate(), p.channels[0].id, 0, 2, 48, .8f, false});
    if (external)
    {
        p.channels[0].instrument = external->at(0);
        p.channels[0].instrument->id = p.allocate();
        p.mixer[0].effects.push_back(external->at(1));
        p.mixer[0].effects.back().id = p.allocate();
    }
    p.placements = {{p.allocate(), p.patterns[0].id, 0, 0, 2, 0},
                    {p.allocate(), p.patterns[0].id, 0, 2, 2, 0}};
    if (automation)
    {
        const auto source = p.allocate();
        p.automation.sources.push_back({source,
                                        4,
                                        {{p.allocate(), 0, .6f}, {p.allocate(), 4, 1.f}},
                                        {{{AutomationParameter::mixerGain, 0, 0}, .5f, 1.f}}});
        p.automation.placements.push_back({p.allocate(), source, 0, 4, 0});
        p.automation.initialValues.push_back({{AutomationParameter::mixerGain, 0, 0}, 1.f});
    }
    require(awaitProjection(audio, p, patternMode, p.patterns[0].id).wasOk(),
            "synthetic playback projection");
    audio.setLooping(true);
    dispatchMessages(30);
    audio.playStop();
    juce::AudioBuffer<float> prime(2, 256);
    for (int i = 0; i < 4; ++i)
    {
        device->process(prime);
        dispatchMessages(2);
    }
    dispatchMessages(20);
    require(audio.playing(), "synthetic transport started");
    auto baseline = audio.projectionDiagnostics();
    const auto gainStages = audio.mixerGraphInitialisations();
    const int starts = device->starts, stops = device->stops;
    class Capture final : public juce::Thread
    {
        SyntheticDevice &device;
        juce::AudioBuffer<float> block{2, 256};

      public:
        juce::AudioBuffer<float> pcm{2, 256 * 512};
        std::atomic<int> blocks{0};
        std::atomic<bool> done{false};
        bool pacingFailed = false;
        int silent = 0;
        std::array<int, 512> silentIndices{};
        bool finite = true;
        float jump = 0;
        double maxCallbackMs = 0;
        int pacing = 2;
        explicit Capture(SyntheticDevice &d) : Thread("Synthetic audio callback"), device(d)
        {
            pcm.clear();
        }
        ~Capture() override
        {
            signalThreadShouldExit();
            waitForThreadToExit(5000);
        }
        void run() override
        {
#if JUCE_WINDOWS
            // The driver owns its timer until its actual exit, including failure.
            std::unique_ptr<void, decltype(&CloseHandle)> pacingTimer{
                CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_MODIFY_STATE | SYNCHRONIZE),
                &CloseHandle};
            if (!pacingTimer)
            {
                pacingFailed = true;
                done = true;
                return;
            }
#endif
            float previous = 0;
            for (int i = 0; i < 512 && !threadShouldExit(); ++i)
            {
                auto begin = juce::Time::getHighResolutionTicks();
                device.process(block);
                maxCallbackMs =
                    std::max(maxCallbackMs, 1000. * juce::Time::highResolutionTicksToSeconds(
                                                        juce::Time::getHighResolutionTicks() - begin));
                if (i > 4 && block.getMagnitude(0, 256) < .000001f)
                    silentIndices[size_t(silent++)] = i;
                for (int n = 0; n < 256; ++n)
                {
                    auto v = block.getSample(0, n);
                    finite = finite && std::isfinite(v);
                    if (i != 0 || n != 0)
                        jump = std::max(jump, std::abs(v - previous));
                    previous = v;
                }
                for (int c = 0; c < 2; ++c)
                    pcm.copyFrom(c, i * 256, block, c, 0, 256);
                blocks = i + 1;
                // Test-only virtual device pacing, outside the callback.
#if JUCE_WINDOWS
                // Preserve the original requested2/6ms load. Coarse Sleep can
                // inflate each pause to15ms in an occluded Windows test process.
                LARGE_INTEGER due;
                due.QuadPart = -LONGLONG(pacing) * 10000;
                if (!SetWaitableTimer(pacingTimer.get(), &due, 0, nullptr, nullptr, FALSE) ||
                    WaitForSingleObject(pacingTimer.get(), 2000) != WAIT_OBJECT_0)
                {
                    pacingFailed = true;
                    break;
                }
#else
                juce::Thread::sleep(pacing);
#endif
            }
#if JUCE_WINDOWS
            pacingTimer.reset();
#endif
            done = true;
        }
    } callback(*device);
    callback.pacing = external ? 6 : 2;
    callback.startThread();
    bool gainEdited = false, midiEdited = false;
    int editCount = 0, lastBlock = -1;
    Controller commands;
    commands.replace(p);
    while (!callback.done)
    {
        auto count = callback.blocks.load();
        if (count >= 16 && !gainEdited)
        {
            p.channels[0].gain = .8f;
            require(audio.rebuild(p, patternMode, p.patterns[0].id).wasOk(),
                    "message-thread gain edit while audio thread runs");
            gainEdited = true;
        }
        if (count >= 48 && !midiEdited)
        {
            p.patterns[0].notes.push_back({p.allocate(), p.channels[0].id, 6, 1, 67, .6f, false});
            require(audio.rebuild(p, patternMode, p.patterns[0].id).wasOk(),
                    "message-thread MIDI edit while audio thread runs");
            midiEdited = true;
        }
        if (count > 64 && count != lastBlock)
        {
            lastBlock = count;
            commands.beginGesture();
            if (automation)
                commands.project.automation.sources[0].points.back().value = editCount % 2 ? .8f : 1.f;
            auto &notes = commands.project.patterns[0].notes;
            if (notes.size() == 1)
                notes.push_back({commands.project.allocate(), p.channels[0].id, .25, .25, 72, .1f, true});
            else
            {
                auto &n = notes.back();
                switch (editCount % 6)
                {
                case 0:
                    n.beat = .5;
                    break;
                case 1:
                    n.length = .125;
                    break;
                case 2:
                    n.velocity = .15f;
                    break;
                case 3:
                    n.pitch = 74;
                    break;
                case 4:
                    notes.pop_back();
                    break;
                case 5:
                    n.beat = .75;
                    break;
                }
            }
            commands.endGesture("Rapid live note edit");
            if (editCount % 7 == 0)
                commands.undoEdit();
            else if (editCount % 7 == 1)
                commands.redoEdit();
            require(audio.rebuild(commands.project, patternMode, p.patterns[0].id).wasOk(),
                    "rapid add/delete/move/resize/velocity/step/undo/redo publication");
            ++editCount;
        }
        dispatchMessages(1);
    }
    require(callback.waitForThreadToExit(5000) && gainEdited && midiEdited,
            "concurrent projection fixture completed");
    require(!callback.pacingFailed, "live-edit driver completed without a pacing wait failure");
    require(callback.finite, "finite PCM across concurrent projection updates");
    auto &capture = callback.pcm;
    int silentBlocks = callback.silent;
    float maximumJump = callback.jump;
    auto after = audio.projectionDiagnostics();
    require(gainStages == audio.mixerGraphInitialisations(), "callback edits retain Mixer gain graph stages");
    {
        juce::String report = "Silent blocks: " + juce::String(silentBlocks) + " | max callback ms " +
                              juce::String(callback.maxCallbackMs, 6) + "\n" +
                              "Rapid publications: " + juce::String(editCount) +
                              " | callback blocks: " + juce::String(callback.blocks.load()) + "\n";
        for (int i = 0; i < callback.silent; ++i)
            report += "Silent block index: " + juce::String(callback.silentIndices[size_t(i)]) + "\n";
        for (auto &channel : after.channels)
            report += "Channel " + juce::String(channel.channel) + " | graph init " +
                      juce::String(juce::int64(channel.graphInitialisations)) + " | timeline misses " +
                      juce::String(juce::int64(channel.timelineMisses)) + " | render calls " +
                      juce::String(juce::int64(channel.renderCalls)) + "\n";
        for (auto &plugin : audio.pluginDiagnostics())
            report += "Plugin " + juce::String(plugin.id) + " | submitted " +
                      juce::String(juce::int64(plugin.packets.submitted)) + " | missed " +
                      juce::String(juce::int64(plugin.packets.missed)) + " | dropped " +
                      juce::String(juce::int64(plugin.packets.dropped)) + "\n";
        folder.getChildFile(patternMode ? "pattern-live-diagnostics.txt" : "song-live-diagnostics.txt")
            .replaceWithText(report);
    }
    require(capture.getMagnitude(0, capture.getNumSamples()) > .05f,
            "live callback produces original instrument signal");
    require(silentBlocks == 0, "gain and future MIDI edits produce no completely silent callback block");
    require(maximumJump < .15f, "bounded sample discontinuity during projection edits");
    require(audio.playing() && after.edit == baseline.edit &&
                after.playbackContext == baseline.playbackContext,
            "live edits retain playing Edit and playback context");
    require(device->starts == starts && device->stops == stops, "live edits do not restart output driver");
    require(after.samplerCreations == baseline.samplerCreations &&
                after.cache.preparations == baseline.cache.preparations,
            "live edits preserve prepared instrument resources");
    require(editCount > 100, "sustained rapid edit coverage");
    require(after.channels[0].graphInitialisations == baseline.channels[0].graphInitialisations,
            "routine edits never initialise a replacement graph");
    require(callback.maxCallbackMs < 256000. / 48000., "no measured synthetic callback overruns");
    if (external)
    {
        juce::String report;
        for (auto &plugin : audio.pluginDiagnostics())
        {
            report += "Plugin " + juce::String(plugin.id) + " | latency " + juce::String(plugin.latency) +
                      " samples | submitted " + juce::String(juce::int64(plugin.packets.submitted)) +
                      " | missed " + juce::String(juce::int64(plugin.packets.missed)) + " | dropped " +
                      juce::String(juce::int64(plugin.packets.dropped)) + "\n";
            require(plugin.alive && plugin.packets.submitted > 100 && plugin.packets.dropped == 0,
                    "real VST3 live worker remains active through edits");
        }
        folder.getChildFile(patternMode ? "pattern-plugin-packets.txt" : "song-plugin-packets.txt")
            .replaceWithText(report);
    }
    audio.stop();
    auto file = folder.getChildFile(patternMode ? "pattern-live-edit.wav" : "song-live-edit.wav");
    juce::TemporaryFile temporary(file);
    auto stream = temporary.getFile().createOutputStream();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
    require(writer && writer->writeFromAudioSampleBuffer(capture, 0, capture.getNumSamples()),
            "captured synthetic callback WAV");
    writer.reset();
    require(temporary.overwriteTargetFileWithTemporary(), "callback evidence commit");
    folder.getChildFile(patternMode ? "pattern-live-edit.txt" : "song-live-edit.txt")
        .replaceWithText("In-memory JUCE output through live Tracktion callback; 512 blocks x 256 frames @ "
                         "48000 Hz on separate callback thread with concurrent message-thread gain/MIDI "
                         "edits and loop boundaries. Rapid publications: " +
                         juce::String(editCount) +
                         ". Maximum measured callback duration (not hardware latency): " +
                         juce::String(callback.maxCallbackMs, 6) +
                         " ms. Silent blocks after warmup: " + juce::String(silentBlocks) +
                         "; maximum adjacent sample jump: " + juce::String(maximumJump, 8) +
                         ". Gain and future MIDI edits retained Edit/context/device/plugin/sample "
                         "identities; graph initialisations unchanged, driver starts/stops unchanged. Zero "
                         "measured overruns against 5.333 ms block budget. No physical output, listening or "
                         "latency qualification.\n");
}
static void scannerContainmentTests(const juce::File &folder)
{
    auto fixture = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getSiblingFile("VibeRaScannerFixture.exe");
    require(fixture.existsAsFile(), "scanner fault fixture built");
    auto scans = folder.getChildFile("scanner-fixtures");
    scans.createDirectory();
    for (auto name : {"valid.vst3", "crash.vst3", "hang.vst3"})
        scans.getChildFile(name).replaceWithText("Fault-containment fixture; not a real plugin");
    auto settings = folder.getChildFile("fixture-plugin-catalog.xml");
    {
        PluginCatalog catalog(settings, fixture, false, 150);
        require(catalog.scan({scans.getFullPathName()}, true).wasOk(),
                "isolated fixture scan starts asynchronously");
        auto deadline = juce::Time::getMillisecondCounter() + 12000;
        while (catalog.snapshot().scanning && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(5);
        auto result = catalog.snapshot();
        require(!result.scanning && result.completed == 3 && result.plugins.size() == 1,
                "scan progress and valid result survive crash/timeout");
        require(result.blacklist.size() == 2 && result.failures.size() == 2,
                "crashed and timed-out workers blacklisted");
        require(result.completionState == "completed" && result.persisted && result.operation.isNotEmpty(),
                "scan is completed only after atomic settings persistence despite isolated class failures");
    }
    {
        PluginCatalog reopened(settings, fixture, false, 150);
        require(reopened.snapshot().blacklist.size() == 2 && reopened.snapshot().plugins.size() == 1,
                "plugin results and blacklist persist across restart");
        require(reopened.scan({scans.getFullPathName()}).wasOk(), "rescan with persisted blacklist");
        auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (reopened.snapshot().scanning && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(5);
        require(reopened.snapshot().completed == 3 && reopened.snapshot().failures.isEmpty(),
                "rescan skips blacklisted workers");
        const auto previous = reopened.snapshot();
        const auto previousXml = settings.loadFileAsString();
        require(reopened.scan({"relative/plugin/folder"}).failed() &&
                    reopened.snapshot().operation == previous.operation,
                "relative folder refusal leaves catalogue and operation identity unchanged");
        auto cancelFolder = folder.getChildFile("cancel-scanner-fixture");
        require(
            cancelFolder.createDirectory().wasOk() &&
                cancelFolder.getChildFile("hang.vst3").replaceWithText("Owned scanner cancellation fixture"),
            "cancellation fixture prepared");
        juce::String cancelledId;
        require(reopened.scan({cancelFolder.getFullPathName()}, true, false, &cancelledId).wasOk() &&
                    cancelledId.isNotEmpty(),
                "cancellable scan returns an exact operation identity");
        const auto cancelDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (reopened.snapshot().total == 0 && reopened.snapshot().scanning &&
               juce::Time::getMillisecondCounter() < cancelDeadline)
            dispatchMessages(1);
        require(reopened.cancel(cancelledId).wasOk(), "cancellation admitted without waiting on scanner");
        while (reopened.snapshot().scanning && juce::Time::getMillisecondCounter() < cancelDeadline)
            dispatchMessages(5);
        const auto cancelled = reopened.snapshot();
        require(!cancelled.scanning && cancelled.completionState == "cancelled" && !cancelled.persisted &&
                    cancelled.blacklist == previous.blacklist && cancelled.folders == previous.folders &&
                    cancelled.plugins.size() == previous.plugins.size() &&
                    settings.loadFileAsString() == previousXml,
                "actual cancellation preserves prior settings/catalogue/blacklist and never blacklists "
                "cancellation");
        PluginCatalog::Snapshot oldReceipt;
        require(reopened.operation(previous.operation, oldReceipt) &&
                    oldReceipt.completionState == "completed" && oldReceipt.persisted &&
                    !reopened.operation("unknown-receipt", oldReceipt),
                "historical exact-token result survives later cancellation; unknown receipts refuse");
    }
    {
        auto validFolder = folder.getChildFile("valid-scanner-fixture");
        require(validFolder.createDirectory().wasOk() &&
                    validFolder.getChildFile("valid.vst3").replaceWithText("Owned success fixture"),
                "persistence failure scanner input prepared");
        auto invalidSettings = folder.getChildFile("unwritable-plugin-catalog.xml");
        require(invalidSettings.createDirectory().wasOk() &&
                    invalidSettings.getChildFile("preserve.txt").replaceWithText("Owned failure sentinel"),
                "non-file settings target refuses replacement without relying on OS permissions");
        PluginCatalog failed(invalidSettings, fixture, false, 150);
        require(failed.scan({validFolder.getFullPathName()}).wasOk(), "persistence failure scan accepted");
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (failed.snapshot().scanning && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(5);
        const auto result = failed.snapshot();
        require(!result.scanning && result.completionState == "failed" &&
                    result.errorCode == "persistence_failed" && !result.persisted &&
                    result.plugins.size() == 1 && invalidSettings.getChildFile("preserve.txt").existsAsFile(),
                "asynchronous persistence failure is explicit with memory readback and untouched target");
    }
    {
        const auto validFolder = folder.getChildFile("valid-scanner-fixture");
        auto scopeSettings = folder.getChildFile("scoped-retry-catalog.xml");
        juce::XmlElement seed("VIBERA_PLUGINS");
        seed.setAttribute("version", 1);
        seed.setAttribute("standardLocations", false);
        for (const auto &path : {scans.getChildFile("crash.vst3"), scans.getChildFile("hang.vst3"),
                                 validFolder.getChildFile("valid.vst3")})
            seed.createNewChildElement("BLACKLIST")->setAttribute("path", path.getFullPathName());
        require(seed.writeTo(scopeSettings), "owned scoped blacklist fixture persisted");
        PluginCatalog scoped(scopeSettings, fixture, false, 150);
        require(scoped.scan({validFolder.getFullPathName()}, true).wasOk(), "targeted retry starts");
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (scoped.snapshot().scanning && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(5);
        const auto result = scoped.snapshot();
        require(
            !result.scanning && result.completionState == "completed" && result.persisted &&
                result.plugins.size() == 1 && result.blacklist.size() == 2 &&
                result.blacklist.contains(scans.getChildFile("crash.vst3").getFullPathName(), true) &&
                result.blacklist.contains(scans.getChildFile("hang.vst3").getFullPathName(), true),
            "targeted retry removes recovered candidate but preserves blacklist entries outside its scope");
    }
    WorkerProcess first, second;
    juce::StringArray args{fixture.getFullPathName(), "--scan",
                           scans.getChildFile("hang.vst3").getFullPathName(),
                           scans.getChildFile("unused.xml").getFullPathName()};
    require(first.start(args) && second.start(args) && first.processId() != second.processId(),
            "distinct owned worker handles");
    first.requestGracefulShutdown();
    require(!first.waitForProcessToFinish(50) && first.kill() && first.waitForProcessToFinish(2000),
            "unresponsive owned worker terminated after graceful attempt");
    require(second.isRunning(), "recovering one worker leaves another instance alive");
    second.requestGracefulShutdown();
    if (!second.waitForProcessToFinish(50))
        second.kill();
    require(second.waitForProcessToFinish(2000), "second owned fixture independently cleaned up");
    folder.getChildFile("scanner-containment.txt")
        .replaceWithText(
            "PASS: controlled scanner crash-exit/timeout fixtures; asynchronous progress/results; persisted "
            "blacklist; rescan skips failures; retained handle recovery terminates only the selected owned "
            "worker after graceful attempt. Other owned instance remains alive. These fixtures do not "
            "establish third-party plugin compatibility.\n");
}
static void pluginModelTests(const juce::File &folder)
{
    Controller c;
    c.replace(Project::demo());
    juce::PluginDescription type;
    type.name = "Unavailable test instrument";
    type.pluginFormatName = "VST3";
    type.fileOrIdentifier = folder.getChildFile("not-installed.vst3").getFullPathName();
    type.uniqueId = 42;
    type.isInstrument = true;
    CommandArgs a;
    a.channel = c.project.channels.back().id;
    a.text = type.createXml()->toString();
    auto before = c.project.state();
    require(c.execute("plugin.instrument.replace", a).wasOk(), "VST3 instrument command");
    auto id = c.project.channels.back().instrument->id;
    c.project.channels.back().instrument->state =
        juce::MemoryBlock("recoverable-state", 17).toBase64Encoding();
    auto model = c.project.state();
    Project parsed;
    require(Project::parse(model, parsed).wasOk() && parsed.state().isEquivalentTo(model),
            "missing VST3 descriptor/state project round trip");
    AudioEngine audio(false);
    require(awaitProjection(audio, c.project, true, c.project.patterns.front().id).wasOk(),
            "missing instrument remains recoverable placeholder");
    require(audio.pluginStatus(id).startsWith("Placeholder:"), "missing instrument status explicit");
    auto file = folder.getChildFile("missing-plugin.vibra");
    require(saveProject(c.project, file).wasOk(), "missing plugin state save");
    require(loadProject(file, parsed).wasOk() &&
                parsed.channels.back().instrument->state == c.project.channels.back().instrument->state,
            "missing state survives reopen");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before), "plugin insertion undo");
    c.redoEdit();
    type.isInstrument = false;
    type.name = "Unavailable effect";
    a.text = type.createXml()->toString();
    a.mixer = 1;
    require(c.execute("plugin.effect.insert", a).wasOk() && c.execute("plugin.effect.insert", a).wasOk(),
            "ordered VST3 effects insertion");
    auto first = c.project.mixer[1].effects[0].id;
    a.target = first;
    a.lane = 1;
    require(c.execute("plugin.effect.move", a).wasOk() && c.project.mixer[1].effects[1].id == first,
            "ordered effect move");
    a.enabled = true;
    require(c.execute("plugin.bypass", a).wasOk() && c.project.mixer[1].effects[1].bypass,
            "effect bypass persistence");
    require(c.execute("plugin.remove", a).wasOk() && c.project.mixer[1].effects.size() == 1,
            "effect removal");
    auto legacy = Project::demo().state();
    legacy.setProperty("version", 1, nullptr);
    stripNoteMuteForLegacy(legacy);
    legacy.removeChild(legacy.getChildWithName("CLIP_GROUPS"), nullptr);
    legacy.removeChild(legacy.getChildWithName("MIXER_GROUPS"), nullptr);
    for (auto channel : legacy.getChildWithName("CHANNELS"))
        channel.removeChild(channel.getChildWithName("SAMPLER"), nullptr);
    require(Project::parse(legacy, parsed).wasOk() && parsed.version == Project::currentVersion,
            "Genuine v1 project migrates to the current schema");
    LiveMidi midi;
    auto p = Project::empty();
    p.patterns[0].notes = {{p.allocate(), p.channels[0].id, 0, 4, 60, .8f, false}};
    auto doc = compileLiveDocument(p, true, p.patterns[0].id, 1);
    require(midi.process(*doc, p.channels[0].id, 0, 256, 48000, true) == 1 && midi.events[0].bytes[0] == 0x90,
            "fixed MIDI note delivery");
    p.patterns[0].notes.clear();
    doc = compileLiveDocument(p, true, p.patterns[0].id, 2);
    require(midi.process(*doc, p.channels[0].id, 256. / 48000, 256, 48000, true) == 1 &&
                midi.events[0].bytes[0] == 0x80,
            "sounding hosted note deletion emits note-off");
    p.patterns[0].notes.clear();
    for (int i = 0; i < 600; ++i)
        p.patterns[0].notes.push_back({p.allocate(), p.channels[0].id, 0, 2, 60 + i % 12, .8f, false});
    doc = compileLiveDocument(p, true, p.patterns[0].id, 3);
    midi.reset();
    auto count = midi.process(*doc, p.channels[0].id, 0, 256, 48000, true);
    require(count == ipc::events && midi.overflow > 0 && midi.events[size_t(count - 1)].bytes[0] == 0xb0 &&
                midi.events[size_t(count - 1)].bytes[1] == 123,
            "hosted MIDI overflow retains a final all-notes-off");
}
static HWND ownedPluginWindow(juce::uint32 pid)
{
    struct Search
    {
        DWORD pid;
        HWND window = nullptr;
    } search{pid};
    EnumWindows(
        [](HWND window, LPARAM pointer) -> BOOL
        {
            auto &s = *reinterpret_cast<Search *>(pointer);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == s.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
            {
                s.window = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}
static void nativePluginEditorTest(PluginHost &host, const juce::File &folder, bool instrument)
{
    struct DpiScope
    {
        DPI_AWARENESS_CONTEXT previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope()
        {
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } dpiScope;
    const auto instance = host.runtimeIdentity();
    const auto editorRequest = [&](bool open)
    {
        const auto request =
            host.requestEditor(instance, open ? PluginEditorAction::open : PluginEditorAction::close);
        require(request.serial != 0, "Actual installed native editor request admitted as pending");
        PluginEditorResult result;
        const auto began = juce::Time::getMillisecondCounter();
        while (!host.readEditorResult(instance, request.serial, result) &&
               juce::Time::getMillisecondCounter() - began <= pluginEditorTimeoutMs + 1000)
            juce::Thread::sleep(10);
        folder.getChildFile(instrument ? "native-instrument-outcome.txt" : "native-effect-outcome.txt")
            .replaceWithText(juce::String("outcome=") + pluginEditorOutcomeName(result.outcome) +
                             " completionKnown=" + juce::String(int(result.completionKnown)) +
                             " exists=" + juce::String(int(result.window.exists)) +
                             " visible=" + juce::String(int(result.window.visible)) + "\n");
        require(result.serial == request.serial && result.succeeded() && result.completionKnown,
                "Actual installed native editor acknowledges completion: " +
                    std::string(pluginEditorOutcomeName(result.outcome)));
        return result;
    };
    require(editorRequest(true).outcome == PluginEditorOutcome::opened,
            "Actual installed native view attached");
    HWND window = nullptr;
    auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (!(window = ownedPluginWindow(host.processId())) && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(20);
    require(window != nullptr, "actual owned plugin native editor opens");
    require(editorRequest(true).outcome == PluginEditorOutcome::broughtToFront &&
                ownedPluginWindow(host.processId()) == window,
            "Repeated installed native-editor open reuses the existing actual window");
    juce::Thread::sleep(500);
    RECT rect{};
    GetWindowRect(window, &rect);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    require(width > 100 && height > 100 && width < 8192 && height < 8192, "native editor bounds");
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    auto source = GetDC(window);
    auto dc = CreateCompatibleDC(source);
    void *pixels = nullptr;
    auto bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto previous = SelectObject(dc, bitmap);
    auto printed = PrintWindow(window, dc, 2);
    juce::Image image(juce::Image::RGB, width, height, true);
    if (printed && pixels)
    {
        auto *bytes = static_cast<unsigned char *>(pixels);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                auto *p = bytes + (y * width + x) * 4;
                image.setPixelAt(x, y, juce::Colour(p[2], p[1], p[0]));
            }
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(window, source);
    require(printed != 0 && pixels != nullptr, "actual native plugin PrintWindow capture");
    auto file = folder.getChildFile(instrument ? "native-instrument-editor.png" : "native-effect-editor.png");
    auto output = file.createOutputStream();
    require(bool(output), "native editor PNG output");
    output->setPosition(0);
    output->truncate();
    require(juce::PNGImageFormat().writeImageToStream(image, *output), "native editor PNG capture saved");
    output.reset();
    require(editorRequest(false).outcome == PluginEditorOutcome::closed,
            "Installed native close is acknowledged");
    deadline = juce::Time::getMillisecondCounter() + 5000;
    while (ownedPluginWindow(host.processId()) && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(20);
    require(!ownedPluginWindow(host.processId()) && host.alive(),
            "native editor closes without killing DSP worker");
}
static void midiRecordingTests(const juce::File &folder, const PluginSpec *plugin = nullptr)
{
    MidiTake take;
    take.begin(7, 10, 3.9, 120, 4);
    take.add({1, 10, 7, 1, {0x90, 60, 100}});
    take.add({1, 10.15, 7, 1, {0x80, 60, 0}});
    auto split = take.finish(10.15);
    require(split.size() == 2 && std::abs(split[0].beat - 3.9) < 1.e-8 &&
                std::abs(split[0].length - .1) < 1.e-8 && split[1].beat < 1.e-8 &&
                std::abs(split[1].length - .2) < 1.e-8,
            "held recorded note splits exactly across loop");
    take.begin(8, 0, 0, 120, 0, .25, .25);
    take.add({1, .08, 8, 2, {0x90, 64, 50}});
    take.add({1, .21, 8, 2, {0x90, 64, 0}});
    auto quantized = take.finish(.21);
    require(quantized.size() == 1 && quantized[0].beat == .25 && quantized[0].length == .25 &&
                std::abs(quantized[0].velocity - 50.f / 127) < 1.e-6,
            "independent input start/end quantization, velocity and velocity-zero note-off");
    take.begin(9, 0, 0, 120, 0);
    take.add({1, 0, 8, 1, {0x90, 60, 100}});
    take.add({1, 0, 9, 1, {0x90, 60, 100}});
    take.add({1, .1, 9, 1, {0x91, 60, 80}});
    take.interrupt(1, .2);
    auto chord = take.finish(3);
    require(chord.size() == 2 && chord[0].length <= .4 && chord[1].length <= .4,
            "stale sessions ignored, MIDI-channel voices independent, target/panic closes held keys");
    MidiInputRouter router(false);
    auto lane = router.createLane();
    lane->channel = 5;
    router.select(lane.get());
    router.capture.begin();
    lane->clock.publish(1, juce::Time::getMillisecondCounterHiRes() / 1000., .005, false);
    router.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    MidiCapture::Event event;
    require(!router.capture.pop(event), "armed stopped input remains monitoring-only");
    lane->clock.publish(1, juce::Time::getMillisecondCounterHiRes() / 1000., .005, true);
    for (int i = 0; i < 4097; ++i)
        router.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    router.stopCapture();
    require(router.capture.failed.load(),
            "bounded capture overflow is explicit; never commit a truncated take");
    int captured = 0;
    while (router.capture.pop(event))
        ++captured;
    require(captured == 4096, "capture queue has fixed capacity independent of monitor queue");

    AudioEngine audio(false, {}, 256);
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
    require(device != nullptr, "recording uses synthetic output only");
    juce::AudioBuffer<float> buffer(2, 256);
    juce::String evidence;
    for (bool patternMode : {true, false})
    {
        Controller c;
        c.project.channels[0].source = "builtin:bass";
        c.project.patterns[0].length = patternMode ? .25 : 4;
        if (plugin)
        {
            c.project.channels[0].instrument = *plugin;
            c.project.channels[0].instrument->id = c.project.allocate();
        }
        auto pattern = c.project.patterns[0].id, channel = c.project.channels[0].id;
        audio.stop();
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "recording projection ready");
        audio.setMidiTarget(channel);
        audio.seekBeat(0);
        dispatchMessages(20);
        for (int i = 0; i < 8; ++i)
        {
            device->process(buffer);
            dispatchMessages(plugin ? 6 : 1);
        }
        MidiClock::Position clock;
        require(audio.midiInput.recordingPosition(clock), "recording clock published");
        audio.midiInput.capture.begin();
        take.begin(audio.midiInput.capture.session.load(), clock.seconds, 0, 120, patternMode ? .25 : 0);
        auto before = audio.projectionDiagnostics();
        int starts = device->starts, stops = device->stops;
        audio.playStop();
        dispatchMessages(10);
        double maxMs = 0;
        float peak = 0;
        for (int i = 0; i < (patternMode ? 72 : 500); ++i)
        {
            if (i == 20)
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
            if (i == 45)
                audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
            if (!patternMode && i == 430)
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::noteOn(1, 62, juce::uint8(80)));
            if (!patternMode && i == 455)
                audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 62));
            auto start = juce::Time::getMillisecondCounterHiRes();
            device->process(buffer);
            maxMs = std::max(maxMs, juce::Time::getMillisecondCounterHiRes() - start);
            peak = std::max(peak, buffer.getMagnitude(0, 256));
            dispatchMessages(plugin ? 6 : 1);
            while (audio.midiInput.capture.pop(event))
                take.add(event);
        }
        audio.midiInput.stopCapture();
        while (audio.midiInput.capture.pop(event))
            take.add(event);
        require(audio.midiInput.recordingPosition(clock), "finish recording clock");
        auto notes = take.finish(clock.seconds);
        require(!notes.empty() && !audio.midiInput.capture.failed.load() && !take.overflowed(),
                "notes retained from real callback clock");
        double duration = 0;
        for (auto &note : notes)
        {
            duration += note.length;
            require(note.channel == channel && (note.pitch == 60 || (!patternMode && note.pitch == 62)),
                    "recording retains Channel/pitch");
        }
        require(std::abs(duration - 25 * 256. / 48000 * 2 * (patternMode ? 1 : 2)) < .04,
                "recorded duration follows rendered samples, independent of GUI wall pacing");
        require(peak > .001f, "recorded performance produces actual PCM");
        auto original = c.project.state();
        CommandArgs args;
        args.pattern = pattern;
        args.recordedNotes = notes;
        args.enabled = !patternMode;
        require(c.execute("notes.record", args).wasOk(), "atomic take command");
        require(patternMode ? c.project.placements.empty() : c.project.placements.size() == 1,
                "Song takes create Playlist placement; Pattern takes do not");
        if (!patternMode)
            require(c.project.pattern(pattern)->length == 8 && c.project.placements[0].length == 8 &&
                        audio.positionBeat() > 4,
                    "Song recording continues beyond existing clips and extends Pattern/placement");
        auto recorded = c.project.state();
        c.undoEdit();
        require(c.project.state().isEquivalentTo(original),
                "one undo removes entire take and auto-placement");
        c.redoEdit();
        require(c.project.state().isEquivalentTo(recorded), "redo restores exact take and IDs");
        auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit &&
                    before.channels[0].graphInitialisations == after.channels[0].graphInitialisations &&
                    starts == device->starts && stops == device->stops && audio.playing(),
                "capturing MIDI does not restart graph, device or transport");
        auto file = folder.getChildFile(patternMode ? "recorded-pattern.vibra" : "recorded-song.vibra");
        require(saveProject(c.project, file).wasOk(), "recorded project saves");
        Project reopened;
        require(loadProject(file, reopened).wasOk() && reopened.state().isEquivalentTo(recorded),
                "recorded project round trip");
        if (patternMode)
            c.project.placements.push_back(
                {c.project.allocate(), pattern, 0, 0, c.project.patterns[0].length, 0});
        require(audio
                    .renderSync(c.project, folder.getChildFile(patternMode ? "recorded-pattern.wav"
                                                                           : "recorded-song.wav"))
                    .wasOk(),
                "recorded take offline export");
        evidence += juce::String(patternMode ? "Pattern" : "Song") +
                    ": notes=" + juce::String(int(notes.size())) +
                    " durationBeats=" + juce::String(duration, 6) +
                    " maxCallbackMs=" + juce::String(maxMs, 4) + " PCMpeak=" + juce::String(peak, 5) + "\n";
    }
    folder.getChildFile(plugin ? "midi-recording-vst3.txt" : "midi-recording.txt")
        .replaceWithText("PASS: injected MIDI through real Tracktion virtual output; audio-clock timing, "
                         "Pattern loops, Song placement, atomic undo/redo, persistence and export.\n" +
                         evidence +
                         "NOT physical MIDI/audio, native OS input or listening qualification. Callback "
                         "maxima measured on synchronous virtual processing.\n");
}
static void midiInputTests(const juce::File &folder, const PluginSpec *plugin = nullptr)
{
    MidiInputRouter router(false);
    auto lane = router.createLane();
    router.select(lane.get());
    MidiMonitor monitor;
    monitor.bind(lane);
    auto sample = makeOriginalSample("bass");
    VoiceBank voices;
    voices.prepare(sample, 48000);
    juce::AudioBuffer<float> block(2, 256);
    auto send = [&](const juce::MidiMessage &message) { router.handleIncomingMidiMessage(nullptr, message); };
    auto tick = [&]
    {
        block.clear();
        monitor.read();
        monitor.render(voices, block, 0, 256);
        return block.getMagnitude(0, 256);
    };
    send(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    require(tick() > .001f, "MIDI callback -> bounded queue -> builtin audio");
    send(juce::MidiMessage::controllerEvent(1, 64, 127));
    send(juce::MidiMessage::noteOff(1, 60));
    for (int i = 0; i < 6; ++i)
        tick();
    require(tick() > .001f, "MIDI sustain retains released key");
    send(juce::MidiMessage::controllerEvent(1, 64, 0));
    for (int i = 0; i < 8; ++i)
        tick();
    require(tick() == 0, "MIDI pedal release has no stuck voice");
    router.panic();
    for (int i = 0; i < 8; ++i)
        tick();
    send(juce::MidiMessage::noteOn(1, 60, juce::uint8(30)));
    auto quiet = tick();
    router.panic();
    for (int i = 0; i < 8; ++i)
        tick();
    send(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    auto loud = tick();
    require(quiet > 0 && loud > quiet * 3, "MIDI velocity changes builtin output level");
    router.panic();
    for (int i = 0; i < 8; ++i)
        tick();
    router.setChannel(2);
    send(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    require(tick() == 0, "MIDI input channel filter");
    send(juce::MidiMessage::noteOn(2, 60, juce::uint8(100)));
    require(tick() > .001f, "filtered MIDI channel receives notes");
    for (int i = 0; i < 1200; ++i)
        send(juce::MidiMessage::noteOn(2, 60, juce::uint8(100)));
    require(lane->overflows.load() > 0, "MIDI queue capacity is bounded");
    tick();
    for (int i = 0; i < 8; ++i)
        tick();
    require(tick() == 0, "MIDI queue overflow panics rather than dropping note-offs");
    send(juce::MidiMessage::noteOn(2, 60, juce::uint8(100)));
    tick();
    router.select(nullptr);
    for (int i = 0; i < 8; ++i)
        tick();
    require(tick() == 0, "Channel switch releases previous MIDI voices");
    require(router.setDevice("vibera-nonexistent-device-fixture").failed(),
            "missing MIDI device is reported");
    router.setDevice({});
    require(router.identifier().isEmpty(), "MIDI device can be disabled");

    AudioEngine audio(false, {}, 256);
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
    require(device != nullptr, "MIDI monitor uses virtual device only");
    auto project = Project::empty();
    project.channels.resize(1);
    project.channels[0].source = "builtin:bass";
    project.channels[0].gain = 1;
    project.mixer[0].gain = 1;
    project.patterns[0].notes.clear();
    project.patterns[0].length = 16;
    if (plugin)
    {
        project.channels[0].instrument = *plugin;
        project.channels[0].instrument->id = project.allocate();
    }
    const auto monitoredProjection = awaitProjection(audio, project, true, project.patterns[0].id);
    require(monitoredProjection.wasOk(),
            "MIDI monitoring projection: " + monitoredProjection.getErrorMessage().toStdString());
    audio.setMidiTarget(project.channels[0].id);
    dispatchMessages(20);
    auto renderBlocks = [&](int count)
    {
        float peak = 0;
        for (int i = 0; i < count; ++i)
        {
            block.clear();
            device->process(block);
            peak = std::max(peak, block.getMagnitude(0, 256));
            dispatchMessages(plugin ? 6 : 1);
        }
        return peak;
    };
    renderBlocks(8);
    auto before = audio.projectionDiagnostics();
    int starts = device->starts, stops = device->stops;
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    require(renderBlocks(24) > .001f, "MIDI input audible PCM with stopped transport");
    require(!audio.playing() && audio.positionBeat() == 0, "MIDI input does not start or move transport");
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    renderBlocks(plugin ? 100 : 8);
    if (!plugin)
        require(renderBlocks(4) == 0, "stopped monitoring note-off releases builtin output");
    audio.playStop();
    dispatchMessages(10);
    renderBlocks(8);
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 62, juce::uint8(100)));
    require(renderBlocks(12) > .001f, "MIDI input audible PCM while playing");
    auto position = audio.positionBeat();
    project.patterns[0].notes.push_back({project.allocate(), project.channels[0].id, 12, 1, 65, .5f, false});
    require(audio.rebuild(project, true, project.patterns[0].id).wasOk(),
            "live note edits with MIDI monitoring");
    require(audio.playing() && audio.positionBeat() >= position, "MIDI edit retains transport");
    require(renderBlocks(8) > .001f, "held MIDI note survives routine timeline publication");
    auto after = audio.projectionDiagnostics();
    require(before.edit == after.edit &&
                before.channels[0].graphInitialisations == after.channels[0].graphInitialisations &&
                device->starts == starts && device->stops == stops,
            "MIDI input/edit does not reset graph or device");
    audio.midiInput.panic();
    renderBlocks(plugin ? 100 : 8);
    if (!plugin)
        require(renderBlocks(4) == 0, "All Notes Off releases monitored notes during playback");
    TypingKeyboard keys;
    keys.enabled = true;
    keys.emit = [&](int pitch, bool on, int velocity)
    {
        audio.typingInput.handleIncomingMidiMessage(
            nullptr, on ? juce::MidiMessage::noteOn(1, pitch, juce::uint8(velocity))
                        : juce::MidiMessage::noteOff(1, pitch));
    };
    audio.stop();
    renderBlocks(plugin ? 180 : 12);
    keys.press('Z');
    require(renderBlocks(24) > .001f, "typing input produces actual instrument PCM while stopped");
    keys.releaseAll();
    renderBlocks(plugin ? 180 : 12);
    if (!plugin)
        require(renderBlocks(4) == 0, "typing note release has no stuck built-in voice");
    audio.playStop();
    renderBlocks(8);
    keys.press('Z');
    require(renderBlocks(16) > .001f, "typing input works while playing");
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    renderBlocks(16);
    keys.releaseAll();
    require(renderBlocks(16) > .001f, "physical MIDI survives overlapping typing note release");
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    renderBlocks(plugin ? 180 : 12);
    if (!plugin)
        require(renderBlocks(4) == 0, "both input sources release without stuck voice");
    auto typingAfter = audio.projectionDiagnostics();
    require(before.edit == typingAfter.edit &&
                before.channels[0].graphInitialisations == typingAfter.channels[0].graphInitialisations &&
                device->starts == starts && device->stops == stops,
            "typing input retains graph/device lifecycle");
    ScoreLog liveLog;
    MidiCapture::Event logEvent;
    for (unsigned source = 0; source < 2; ++source)
    {
        auto &queue = source ? audio.typingInput.history : audio.midiInput.history;
        while (queue.pop(logEvent))
        {
            logEvent.source = source;
            liveLog.add(logEvent);
        }
    }
    require(!audio.midiInput.history.failed.load() && !audio.typingInput.history.failed.load(),
            "normal stopped/playing history capture has no overflow");
    auto recoveredNotes =
        liveLog.dump(juce::Time::getMillisecondCounterHiRes() / 1000., 180, project.tempo, project);
    require(!recoveredNotes.empty(), "unarmed stopped/playing real input route retains score history");
    Controller score;
    score.replace(project);
    CommandArgs dump;
    dump.pattern = project.patterns[0].id;
    dump.enabled = false;
    dump.recordedNotes = recoveredNotes;
    auto logBefore = audio.projectionDiagnostics();
    auto logPosition = audio.positionBeat();
    require(score.execute("notes.record", dump).wasOk() &&
                audio.rebuild(score.project, true, dump.pattern).wasOk(),
            "score dump publishes during live playback");
    auto logAfter = audio.projectionDiagnostics();
    require(audio.playing() && audio.positionBeat() >= logPosition && logBefore.edit == logAfter.edit &&
                logBefore.channels[0].graphInitialisations == logAfter.channels[0].graphInitialisations &&
                device->starts == starts && device->stops == stops,
            "live score dump retains transport/graph/device");
    folder.getChildFile(plugin ? "score-vst3-verification.txt" : "score-live-verification.txt")
        .replaceWithText("PASS: unarmed physical/typing input histories retain stopped/playing performance; "
                         "canonical dump during live playback preserves transport/graph/device. Virtual "
                         "output, no hardware/listening.\n");
    folder.getChildFile(plugin ? "typing-vst3-verification.txt" : "typing-input-verification.txt")
        .replaceWithText(
            "PASS: typing layout/state -> separate queue -> real Tracktion virtual output, stopped/playing; "
            "physical input overlap; graph/device retained. No OS key input, hardware or listening test.\n");
    folder.getChildFile(plugin ? "midi-vst3-verification.txt" : "midi-input-verification.txt")
        .replaceWithText(
            "PASS: injected JUCE MIDI callback to bounded input queue to real Tracktion virtual-device "
            "output, stopped and playing; held note survives timeline edit; graph/device retained. "
            "Queue/sustain/filter/switch/overflow/missing-device tests passed. "
            "No physical MIDI keyboard, hardware latency or listening test. Live monitoring only; MIDI "
            "recording is qualified separately.\n");
}
static void actualPluginTests(const juce::File &folder, const juce::File &instrument,
                              const juce::File &effect)
{
    folder.createDirectory();
    juce::String report;
    std::vector<PluginSpec> specs;
    for (auto binary : {instrument, effect})
    {
        std::vector<juce::PluginDescription> found;
        auto scan = PluginCatalog::scanOne(binary, found);
        require(scan.wasOk(), "actual installed VST3 isolated scan: " + binary.getFileName().toStdString());
        auto type = found.front();
        PluginSpec spec{1, type.name, type.createXml()->toString(), {}, type.isInstrument, false};
        PluginHost host;
        auto start = host.start(spec, 48000, 256, true);
        if (start.wasOk())
        {
            const auto startup = host.statistics();
            require(startup.workerHeartbeat > 0 && startup.workerPhase != ipc::DspPhase::preparing,
                    "actual worker readiness requires entered DSP loop");
        }
        require(start.wasOk(), "actual VST3 worker initialize: " + start.getErrorMessage().toStdString());
        {
            PluginHost nativeHost;
            require(nativeHost.start(spec, 48000, 256, false).wasOk(),
                    "Separate LIVE native editor owner; offline render provider remains untouched");
            nativePluginEditorTest(nativeHost, folder, type.isInstrument);
        }
        juce::AudioBuffer<float> audio(2, 256), capture(2, 256 * 160);
        float magnitude = 0;
        for (int block = 0; block < 160; ++block)
        {
            audio.clear();
            if (!type.isInstrument)
                for (int n = 0; n < 256; ++n)
                    for (int ch = 0; ch < 2; ++ch)
                        audio.setSample(ch, n,
                                        float(.2 * std::sin((block * 256 + n) * 2 *
                                                            juce::MathConstants<double>::pi * 220 / 48000.)));
            ipc::MidiEvent event{0, {0x90, 60, 100}, 3};
            host.process(audio, 0, 256, block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                         block * 256. / 48000, 120, true, false);
            magnitude = std::max(magnitude, audio.getMagnitude(0, 256));
            for (int ch = 0; ch < 2; ++ch)
                capture.copyFrom(ch, block * 256, audio, ch, 0, 256);
        }
        require(magnitude > .001f,
                "actual VST3 produces finite non-silent signal: " + type.name.toStdString());
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < capture.getNumSamples(); ++n)
                require(std::isfinite(capture.getSample(ch, n)), "finite plugin PCM");
        auto serial = host.requestState();
        auto deadline = juce::Time::getMillisecondCounter() + 10000;
        while (!host.readState(serial, spec.state) && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep(5);
        require(spec.state.isNotEmpty(), "actual VST3 state captured");
        auto stats = host.statistics();
        require(stats.missed == 0 && stats.dropped == 0,
                "actual offline VST3 packets completed without loss");
        report += type.name + " " + type.version +
                  " | isolated scan/init/MIDI-or-audio/PCM/state: PASS | peak " + juce::String(magnitude) +
                  " | latency " + juce::String(host.latencySamples()) + " samples | packets " +
                  juce::String(juce::int64(stats.submitted)) + "\n";
        specs.push_back(spec);
    }
    // Force a host burst faster than the worker without dropping any new MIDI.
    // A late PCM block must not panic a note already playing in the worker.
    {
        PluginHost late;
        auto opened = late.start(specs[0], 48000, 256, false);
        require(opened.wasOk(), "late-PCM fixture uses actual installed VST3");
        juce::AudioBuffer<float> block(2, 256);
        int frame = 0;
        auto tick = [&](const ipc::MidiEvent *event, int count)
        {
            block.clear();
            late.process(block, 0, 256, event, count, frame * 256. / 48000., 120, true, false);
            ++frame;
            return block.getMagnitude(0, 256);
        };
        ipc::MidiEvent note{0, {0x90, 60, 100}, 3};
        tick(&note, 1);
        dispatchMessages(20);
        float initial = 0;
        for (int i = 0; i < 32; ++i)
        {
            initial = std::max(initial, tick(nullptr, 0));
            dispatchMessages(6);
        }
        require(initial > .001f, "actual sustained note before forced late PCM");
        auto reset = late.midiResetGeneration();
        auto previousMisses = late.statistics().missed;
        for (int i = 0; i < 32; ++i)
            tick(nullptr, 0);
        dispatchMessages(30);
        float recovered = 0;
        for (int i = 0; i < 32; ++i)
        {
            recovered = std::max(recovered, tick(nullptr, 0));
            dispatchMessages(6);
        }
        require(late.statistics().missed > previousMisses,
                "late-PCM fixture actually produced worker misses");
        require(late.midiResetGeneration() == reset,
                "audio-only packet miss/drop must not reset instrument MIDI");
        require(recovered > .001f,
                "held actual instrument voice resumes after late-PCM burst without new note-on");
        ipc::MidiEvent off{0, {0x80, 60, 0}, 3};
        tick(&off, 1);
        dispatchMessages(20);
        folder.getChildFile("late-pcm-voice-recovery.txt")
            .replaceWithText(
                "PASS: actual VST3 held note survives deliberately unpaced audio-only burst; misses=" +
                juce::String(juce::int64(late.statistics().missed - previousMisses)) +
                ", reset generation unchanged; recovered peak=" + juce::String(recovered) +
                ". Burst deliberately creates missing PCM; this does not prove zero underruns or hardware "
                "reliability.\n");
    }
    auto p = Project::empty();
    p.channels.resize(1);
    p.channels[0].instrument = specs[0];
    p.channels[0].instrument->id = p.allocate();
    p.mixer[0].effects.push_back(specs[1]);
    p.mixer[0].effects.back().id = p.allocate();
    p.patterns[0].length = 4;
    p.patterns[0].notes = {{p.allocate(), p.channels[0].id, 0, 2, 60, .8f, false}};
    p.placements = {{p.allocate(), p.patterns[0].id, 0, 0, 4, 0}};
    auto file = folder.getChildFile("hosted-project.vibra");
    require(saveProject(p, file).wasOk(), "actual hosted project save");
    Project loaded;
    require(loadProject(file, loaded).wasOk(), "actual hosted project reopen");
    // This fixture calls other engine-owning fixtures below. Keep its engine on
    // the control-thread heap so nested virtual callbacks retain normal Windows
    // stack headroom; do not increase stack reserves or alter callback budgets.
    auto engineOwner = std::make_unique<AudioEngine>(false);
    auto &engine = *engineOwner;
    auto wav = folder.getChildFile("hosted-export.wav");
    auto render = engine.renderSync(loaded, wav);
    require(render.wasOk(),
            "actual instrument/effect hosted WAV export: " + render.getErrorMessage().toStdString());
    double rate = 0;
    auto pcm = readWav(wav, rate);
    require(pcm.getMagnitude(0, pcm.getNumSamples()) > .001f && pcm.getNumSamples() == 96000,
            "actual hosted export signal/duration");
    report += "Project state save/reopen and Tracktion instrument -> Master effect WAV export: PASS.\n"
              "Actual native SDK editor open, PrintWindow capture and close with worker retained: PASS.\n"
              "No native editor mouse/keyboard interaction, physical audio, listening or broad compatibility "
              "qualification.\n";
    folder.getChildFile("actual-plugin-verification.txt").replaceWithText(report);
    // Qualify streamed Playlist media through a real effect alongside an isolated instrument.
    {
        const auto source = folder.getChildFile("mixed-original-audio.wav");
        juce::AudioBuffer<float> samples(2, 48000 * 2);
        for (int frame = 0; frame < samples.getNumSamples(); ++frame)
            for (int channel = 0; channel < 2; ++channel)
                samples.setSample(channel, frame, float(.2 * std::sin(frame * .0288)));
        auto stream = source.createOutputStream();
        require(bool(stream), "actual mixed audio fixture stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
                "actual mixed original WAV");
        writer.reset();
        auto mixed = loaded;
        mixed.channels[0].mixer = 1;
        mixed.channels[0].pan = 1;
        mixed.mixer[0].effects.clear();
        mixed.mixer[2].effects = {specs[1]};
        mixed.mixer[2].effects[0].id = mixed.allocate();
        Channel clip;
        clip.id = mixed.allocate();
        clip.name = "Original Audio Clip";
        clip.source = source.getFullPathName();
        clip.audioClip = true;
        clip.audioDuration = 2;
        clip.gain = 1;
        clip.pan = -1;
        clip.mixer = 2;
        mixed.channels.push_back(clip);
        mixed.audioPlacements.push_back({mixed.allocate(), clip.id, 7, 2.5, .5, .25});
        const auto project = folder.getChildFile("mixed-audio-plugin.vibra");
        require(saveProject(mixed, project).wasOk(), "actual mixed project save");
        Project reopened;
        require(loadProject(project, reopened).wasOk(), "actual mixed project reopen");
        auto *collected = reopened.channel(clip.id);
        require(collected && juce::SHA256(juce::File(collected->source)).toHexString() ==
                                 juce::SHA256(source).toHexString(),
                "actual mixed portable source bytes match");
        auto expected = mixed;
        expected.channel(clip.id)->source = collected->source;
        require(reopened.state().isEquivalentTo(expected.state()),
                "actual mixed portable project round trip");
        const auto output = folder.getChildFile("mixed-audio-plugin.wav");
        require(engine.renderSync(reopened, output).wasOk(), "actual mixed instrument/audio/effect export");
        auto rendered = readWav(output, rate);
        const auto left = rendered.getMagnitude(0, 64800, 7200);
        const auto right = rendered.getMagnitude(1, 0, 24000);
        require(rendered.getNumSamples() == 96000 && left > .001f && right > .001f,
                "actual mixed left Audio Clip/right instrument signal and duration");
        reopened.mixer[2].muted = true;
        const auto mutedOutput = folder.getChildFile("mixed-audio-muted.wav");
        require(engine.renderSync(reopened, mutedOutput).wasOk(), "actual mixed Audio Clip bus mute render");
        auto muted = readWav(mutedOutput, rate);
        require(muted.getMagnitude(0, 64800, 7200) < .0001f && muted.getMagnitude(1, 0, 24000) > .001f,
                "Audio Clip follows assigned effect bus, Playlist lane independent, VST3 survives bus mute");
        folder.getChildFile("mixed-audio-plugin-verification.txt")
            .replaceWithText(
                "PASS: actual installed VST3 instrument on right bus, streamed source-window "
                "Audio Clip on left effect bus, versioned project save/reopen, stereo WAV timing/signal "
                "and independent Audio Clip bus mute. Left peak=" +
                juce::String(left) + "; right instrument peak=" + juce::String(right) +
                ". Offline processing evidence; no live Audio Clip gestures, hardware or "
                "listening qualification.\n");
    }
    midiInputTests(folder, &specs[0]);
    midiRecordingTests(folder, &specs[0]);
    midiScoreLiveTests(folder, &specs[0]);
    scoreToolsLiveTests(folder, &specs[0]);
    guideIntegrationTests(folder, &specs[0]);
    callbackProjectionTests(folder, true, &specs);
    callbackProjectionTests(folder, false, &specs);
}
static void soundingEditTests()
{
    auto p = Project::empty();
    p.channels.resize(1);
    auto id = p.allocate();
    p.patterns[0].notes = {{id, p.channels[0].id, 0, 8, 60, .8f, false}};
    auto sample = makeOriginalSample("bass");
    VoiceBank edited, control;
    edited.prepare(sample, 48000);
    control.prepare(sample, 48000);
    LiveSequencer scheduler, original;
    juce::AudioBuffer<float> a(2, 256), b(2, 256);
    auto doc = compileLiveDocument(p, true, p.patterns[0].id, 1);
    double time = 0;
    auto process = [&]
    {
        a.clear();
        scheduler.process(*doc, p.channels[0].id, time, 256, 48000, true, edited, a, 0);
        time += 256. / 48000;
    };
    for (int block = 0; block < 20; ++block)
    {
        b.clear();
        original.process(*doc, p.channels[0].id, time, 256, 48000, true, control, b, 0);
        process();
    }
    // Editing an unrelated future note must retain the exact sample cursor/envelope.
    p.patterns[0].notes.push_back({p.allocate(), p.channels[0].id, 6, .5, 72, .5f, false});
    doc = compileLiveDocument(p, true, p.patterns[0].id, 2);
    b.clear();
    original.process(*doc, p.channels[0].id, time, 256, 48000, true, control, b, 0);
    process();
    float difference = 0;
    for (int i = 0; i < 256; ++i)
        difference = std::max(difference, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    require(difference == 0, "unaffected sustained voice is bit-identical after publication");
    p.patterns[0].notes[0].length = .01;
    doc = compileLiveDocument(p, true, p.patterns[0].id, 3);
    for (int block = 0; block < 6; ++block)
        process();
    require(a.getMagnitude(0, 256) == 0,
            "shortening sounding note past playhead releases without stuck note");
    p.patterns[0].notes[0].length = 8;
    p.patterns[0].notes[0].pitch = 67;
    doc = compileLiveDocument(p, true, p.patterns[0].id, 4);
    process();
    require(a.getMagnitude(0, 256) > .01f, "extended/moved sounding note chased at preserved playhead");
    p.patterns[0].notes.erase(p.patterns[0].notes.begin());
    doc = compileLiveDocument(p, true, p.patterns[0].id, 5);
    for (int block = 0; block < 6; ++block)
        process();
    require(a.getMagnitude(0, 256) == 0, "deleted sounding note has bounded release and no stuck voice");
    LiveTimeline handoff;
    auto one = compileLiveDocument(p, true, p.patterns[0].id, 1);
    require(handoff.publish(one), "immutable timeline publication");
    auto first = handoff.acquire();
    auto two = compileLiveDocument(p, true, p.patterns[0].id, 2);
    require(handoff.publish(two), "publish while old consumer pinned");
    auto second = handoff.acquire();
    auto three = compileLiveDocument(p, true, p.patterns[0].id, 3);
    require(handoff.publish(three), "third immutable slot");
    auto fourth = compileLiveDocument(p, true, p.patterns[0].id, 4);
    require(!handoff.publish(fourth) && fourth && first.document->generation == 1 &&
                second.document->generation == 2,
            "busy publication preserves all pinned documents without blocking/destruction");
    first = std::move(second);
    require(first.document && first.document->generation == 2 && !second.document &&
                handoff.publish(fourth) && first.document->generation == 2,
            "cached lease replacement frees the previous slot and pins the retained snapshot");
}
static void dspTests()
{
    auto sample = makeOriginalSample("bass");
    VoiceBank bank;
    bank.prepare(sample, 48000);
    juce::AudioBuffer<float> b(2, 1024);
    b.clear();
    bank.noteOn(60, .8f);
    bank.render(b, 0, 1024);
    require(b.getMagnitude(0, 1024) > .1f, "prepared fixed sampler produces signal");
    bank.noteOff(60);
    b.clear();
    bank.render(b, 0, 1024);
    b.clear();
    bank.render(b, 0, 1024);
    require(b.getMagnitude(0, 1024) == 0, "note release finishes");
    bank.noteOn(60, 1);
    bank.reset();
    b.clear();
    bank.render(b, 0, 1024);
    require(b.getMagnitude(0, 1024) == 0, "panic clears voices");
}
static void mediaTests(const juce::File &folder)
{
    auto sample = makeOriginalSample("kick");
    auto source = folder.getChildFile("external-kick.wav");
    auto stream = source.createOutputStream();
    require(bool(stream), "sample fixture stream");
    stream->setPosition(0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
    require(bool(writer), "sample fixture writer");
    require(writer->writeFromAudioSampleBuffer(sample.data, 0, sample.data.getNumSamples()),
            "original sample fixture write");
    writer.reset();
    auto project = Project::empty();
    project.channels[0].source = source.getFullPathName();
    project.patterns[0].notes.push_back({project.allocate(), project.channels[0].id, 0, .25, 60, 1, true});
    project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0, 4, 0});
    auto portable = folder.getChildFile("portable");
    require(saveProject(project, portable.getChildFile("project.vibra")).wasOk(),
            "external sample collection");
    auto relocated = folder.getChildFile("relocated");
    require(portable.copyDirectoryTo(relocated), "portable project relocation");
    Project loaded;
    require(loadProject(relocated.getChildFile("project.vibra"), loaded).wasOk(), "relocated project load");
    auto collected = juce::File(loaded.channels[0].source);
    require(collected.existsAsFile() && collected.isAChildOf(relocated), "relocated sample resolves locally");
    juce::MemoryBlock original, copy;
    source.loadFileAsData(original);
    collected.loadFileAsData(copy);
    require(original == copy, "collected media bytes preserved");
    AudioEngine engine(false);
    auto output = folder.getChildFile("relocated-song.wav");
    require(engine.renderSync(loaded, output).wasOk(), "external sample renders after relocation");
    double rate = 0;
    auto audio = readWav(output, rate);
    require(audio.getMagnitude(0, audio.getNumSamples()) > .05f, "relocated media audible signal");
}
static void customizationTests(const juce::File &folder)
{
    auto legacy = Customization::familiarDefaults().state();
    legacy.getDynamicObject()->removeProperty("knownActions");
    auto *legacyBindings = legacy["bindings"].getArray();
    for (int index = legacyBindings->size() - 1; index >= 0; --index)
        if ((*legacyBindings)[index]["action"].toString().startsWith("playlist.") ||
            (*legacyBindings)[index]["action"] == juce::var("tool.select") ||
            (*legacyBindings)[index]["action"] == juce::var("tool.delete") ||
            (*legacyBindings)[index]["action"] == juce::var("tool.zoom"))
            legacyBindings->remove(index);
    Customization migrated;
    require(migrated.importState(legacy).wasOk(), "legacy shortcut profile migration");
    require(migrated.resolve(juce::KeyPress('E'), Scope::playlist) == "tool.select" &&
                migrated.resolve(juce::KeyPress('D'), Scope::playlist) == "tool.delete",
            "new tool defaults reach old profiles");
    migrated.unbind("tool.select", Scope::playlist);
    Customization disabled;
    require(disabled.importState(migrated.state()).wasOk() &&
                disabled.resolve(juce::KeyPress('E'), Scope::playlist).isEmpty(),
            "intentional new-tool unbinding survives profile reload");
    auto settings = Customization::familiarDefaults();
    juce::KeyPress key('B', juce::ModifierKeys::ctrlModifier, 0);
    require(settings.resolve(key, Scope::piano) == "notes.duplicate", "Piano shortcut scope");
    require(settings.resolve(key, Scope::playlist) == "clip.duplicate", "Playlist shortcut scope");
    require(settings.bind("project.save", Scope::global, key).wasOk() &&
                settings.resolve(key, Scope::piano) == "notes.duplicate" &&
                settings.resolve(key, Scope::rack) == "project.save",
            "contextual command shadows global fallback without changing other contexts");
    require(settings.bind("notes.delete", Scope::piano, key).failed() &&
                settings.resolve(key, Scope::piano) == "notes.duplicate",
            "same-context shortcut collision preserves existing command");
    require(
        settings
            .bind("project.save", Scope::global,
                  juce::KeyPress('S', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0))
            .wasOk(),
        "custom binding accepted");
    require(
        settings.resolve(juce::KeyPress('S', juce::ModifierKeys::ctrlModifier, 0), Scope::piano).isEmpty(),
        "old binding removed");
    Controller c;
    c.replace(Project::demo());
    MacroContext context{c.project.patterns.front().id,
                         c.project.channels.back().id,
                         4,
                         {c.project.patterns.front().notes.back().id},
                         {}};
    Macro macro{"macro.variation", "Duplicate and transpose", {}};
    CommandArgs duplicate;
    duplicate.beat = 8;
    macro.steps.push_back({"notes.duplicate", duplicate, true});
    CommandArgs transpose;
    transpose.pitch = 12;
    transpose.length = 0;
    macro.steps.push_back({"notes.move", transpose, true});
    require(settings.addMacro(macro).wasOk(), "user-defined action sequence");
    auto before = c.project.state();
    require(settings.runMacro(macro.id, c, context).wasOk(), "macro executes");
    auto *pattern = c.project.pattern(context.pattern);
    require(pattern->notes.back().pitch == 67, "macro targets newly duplicated note");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before), "whole macro has one undo");
    auto path = folder.getChildFile("shortcuts.json");
    require(settings.save(path).wasOk(), "profile save");
    Customization loaded;
    require(loaded.load(path).wasOk(), "profile load");
    require(juce::JSON::toString(settings.state()) == juce::JSON::toString(loaded.state()),
            "shortcut and macro round trip");
    context.notes = {c.project.pattern(context.pattern)->notes.back().id};
    int polls = 0;
    require(settings.runMacro(macro.id, c, context, [&] { return ++polls == 2; }).failed(),
            "macro cancellation");
    require(c.project.state().isEquivalentTo(before), "cancel rolls back edits");
    Macro bad{"macro.bad",
              "Invalid target",
              {{"notes.duplicate", duplicate, true}, {"channel.mixer", CommandArgs{}, false}}};
    require(settings.addMacro(bad).wasOk(), "failure fixture registered");
    require(settings.runMacro(bad.id, c, context).failed(), "failed step reported");
    require(c.project.state().isEquivalentTo(before), "failed macro rolls back edits");
    Macro recursive{"macro.recursive", "Recursion", {{"macro.variation", {}, true}}};
    require(settings.addMacro(recursive).failed(), "recursive macro registration rejected");
    auto future = settings.state();
    future.getDynamicObject()->setProperty("version", 999);
    require(loaded.importState(future).failed(), "future profile rejected");
}
static void renderTests(const juce::File &folder)
{
    AudioEngine engine(false);
    const auto stages = folder.getChildFile("render-stages.txt");
    stages.replaceWithText("Fresh render-only phase timing; no callback/listening qualification.\n");
    auto render = [&](const Project &project, const juce::File &target, double sampleRate = 48000,
                      int bits = 24, int block = 512)
    {
        stages.appendText("BEGIN " + target.getFileName() + " CPUs=" +
                          juce::String(engine.engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio()) +
                          " block=" + juce::String(block) + "\n");
        const auto began = juce::Time::getHighResolutionTicks();
        auto result = engine.renderSync(project, target, sampleRate, bits, block);
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        stages.appendText("END " + target.getFileName() + " ms=" + juce::String(elapsed, 6) + " ok=" +
                          juce::String(int(result.wasOk())) + " error=" + result.getErrorMessage() + "\n");
        return result;
    };
    auto p = Project::demo();
    auto output = folder.getChildFile("eight-bars.wav");
    require(render(p, output, 48000, 24, 512).wasOk(), "Tracktion render success");
    double rate = 0;
    auto b = readWav(output, rate);
    require(rate == 48000 && b.getNumChannels() == 2, "stereo and sample rate");
    require(b.getNumSamples() == 768000, "eight-bar duration at 120 BPM");
    require(b.getMagnitude(0, b.getNumSamples()) > .05, "render is not silence");
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(c, i)))
                throw std::runtime_error("nonfinite rendered sample");
    auto repeat = folder.getChildFile("repeat.wav");
    require(render(p, repeat, 48000, 24, 256).wasOk(), "second render with smaller block");
    double otherRate = 0;
    auto other = readWav(repeat, otherRate);
    require(other.getNumSamples() == b.getNumSamples(), "block size independent duration");
    float maximum = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        maximum = std::max(maximum, std::abs(b.getSample(0, i) - other.getSample(0, i)));
    require(maximum < .0002f, "block size independent PCM");
    auto highInsert = p;
    for (auto &channel : highInsert.channels)
        channel.mixer = 17;
    auto highOutput = folder.getChildFile("insert17.wav");
    require(render(highInsert, highOutput, 48000, 24, 256).wasOk(), "Insert 17 actual WAV routing");
    auto highAudio = readWav(highOutput, otherRate);
    require(highAudio.getMagnitude(0, highAudio.getNumSamples()) > .05f, "Insert 17 signal reaches Master");
    highInsert.mixer[17].gain = 0;
    require(render(highInsert, highOutput, 48000, 24, 256).wasOk(), "Insert 17 zero-gain render");
    highAudio = readWav(highOutput, otherRate);
    require(highAudio.getMagnitude(0, highAudio.getNumSamples()) < .000001f,
            "Insert 17 gain controls routed output");
    auto cdOutput = folder.getChildFile("44100-16bit.wav");
    require(render(p, cdOutput, 44100, 16, 512).wasOk(), "44.1 kHz / 16-bit render");
    auto cdAudio = readWav(cdOutput, otherRate);
    require(otherRate == 44100 && cdAudio.getNumSamples() == 705600, "sample rate change preserves duration");
    require(cdAudio.getMagnitude(0, cdAudio.getNumSamples()) > .05f, "resampled instrument signal");
    require(render(p, folder.getChildFile("invalid-settings.wav"), 1, 7, 0).failed(),
            "invalid render configuration rejected");
    for (auto &m : p.mixer)
        m.gain = 0;
    auto silent = folder.getChildFile("muted.wav");
    require(render(p, silent).wasOk(), "muted render succeeds");
    auto silence = readWav(silent, rate);
    require(silence.getMagnitude(0, silence.getNumSamples()) < .000001f, "Mixer gain affects render");
    auto unavailable = Project::demo();
    unavailable.channels[0].source = folder.getChildFile("missing.wav").getFullPathName();
    require(render(unavailable, folder.getChildFile("missing-output.wav")).failed(),
            "missing resource fails explicitly");
    juce::MemoryBlock before, after;
    output.loadFileAsData(before);
    require(render(unavailable, output).failed(), "failed overwrite reported");
    output.loadFileAsData(after);
    require(before == after, "failed render preserves existing output");
}
// Keep the large native fixture off the dispatcher stack. An owned debugger
// diagnosed __chkstk at pluginStateAdmissionTests after its frame combined
// with a second AudioEngine frame in main. No stack limit/oracle is increased.
__declspec(noinline) static void runFixedInitialisationLifecycle(const juce::File &attempt)
{
    const auto receipt = attempt.getChildFile("verification.json");
    require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
            "Retain lifecycle attempt before constructing any native object");
    try
    {
        struct Storage
        {
            juce::String old =
                juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
            explicit Storage(const juce::File &folder)
            {
                require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                                   folder.getFullPathName().toWideCharPointer()) == 0,
                        "Lifecycle engine uses disposable preferences and cache");
            }
            ~Storage()
            {
                _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", old.toWideCharPointer());
            }
        } storage(attempt.getChildFile("storage"));
        AudioEngine audio(false, attempt.getChildFile("guide.json"));
        te::Edit::Options options{audio.engine, te::createEmptyEdit(audio.engine), {}};
        options.role = te::Edit::forEditing;
        options.numAudioTracks = 0;
        auto edit = te::Edit::createEdit(options);
        require(bool(edit), "Create genuine disposable native Edit without a device/graph");
        juce::ValueTree sampler(te::IDs::PLUGIN), send(te::IDs::PLUGIN);
        const auto samplerID = te::EditItemID::readOrCreateNewID(*edit, sampler);
        const auto sendID = te::EditItemID::readOrCreateNewID(*edit, send);
        require(samplerID.isValid() && sendID.isValid() && samplerID != sendID,
                "Original lifecycle fixtures have distinct actually reserved native identities");
        const auto before = edit->state.toXmlString();
        const auto cache = edit->getPluginCache().getPlugins();
        tracktion::graph::PlayHead head;
        tracktion::graph::PlayHeadState headState(head);
        te::ProcessState processState(headState, edit->tempoSequence);
        vibera::test::fixed_initialisation::tests(*edit, sampler, send, processState);
        require(edit->state.toXmlString() == before && edit->getPluginCache().getPlugins() == cache,
                "Private lifecycle failures/retries preserve native Edit and cache");
        require(receipt.replaceWithText(
                    "{\"passed\":true,\"scope\":\"Actual native PluginNode/AuxSendNode lifecycle "
                    "with original synthetic probes; no DSP/device/provider/native input/listening "
                    "qualification\"}\n"),
                "Retain successful original lifecycle outcome");
    }
    catch (const std::exception &error)
    {
        auto failure = std::make_unique<juce::DynamicObject>();
        failure->setProperty("passed", false);
        failure->setProperty("failure", juce::String(error.what()));
        receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
        throw;
    }
}
#include "SynthHostAcceptance.h"

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        if (argc == 3 && juce::String(argv[1]) == "--synth-host-check")
        {
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            synthHostAcceptance(attempt);
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--native-effect-model-check")
        {
            const auto result = vibera::acceptance::nativeEffectModel::verify();
            require(result.wasOk(), result.getErrorMessage().toStdString());
            std::cout << "PASS native effect project/chain model\n";
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--plugin-main-io-model-check")
        {
            const auto model = vibera::acceptance::pluginMainIoModel::verify();
            require(model.wasOk(), model.getErrorMessage().toStdString());
            const auto macro = vibera::acceptance::pluginMainIoMacro::verify();
            require(macro.wasOk(), macro.getErrorMessage().toStdString());
            const auto history = vibera::acceptance::capturedStructuralHistory::verify();
            require(history.wasOk(), history.getErrorMessage().toStdString());
            return 0;
        }
        if (argc > 1 && juce::String(argv[1]).startsWith("--plugin-bus-choice"))
        {
            require(argc == 4 && juce::String(argv[1]) == "--plugin-bus-choice-check" &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Exact original bus-choice fixture and owned output required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            pluginBusChoiceTests(attempt, juce::File(argv[3]));
            return 0;
        }
        bool streamPcmRequested = false, fixedSourcePcmRequested = false, sourceTimeRequested = false,
             chainFilesRequested = false, finiteBaselineRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            streamPcmRequested |= juce::String(argv[i]).startsWith("--plugin-stream-pcm");
            fixedSourcePcmRequested |= juce::String(argv[i]).startsWith("--fixed-source-pcm-epoch");
            sourceTimeRequested |= juce::String(argv[i]).startsWith("--source-render-time");
            chainFilesRequested |= juce::String(argv[i]).startsWith("--mixer-chain-file-jobs");
            finiteBaselineRequested |= juce::String(argv[i]).startsWith("--finite-effect-baseline");
        }
        if (finiteBaselineRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--finite-effect-baseline-check" &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Exact owned output and ORIGINAL source-built finite fixture required");
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            const juce::File binary(argv[3]);
            const auto record = [&](bool finished, bool passed, const juce::String& message)
            {
                juce::var value(new juce::DynamicObject());
                auto& report = *value.getDynamicObject();
                report.setProperty("finished", finished);
                report.setProperty("passed", passed);
                report.setProperty("productionActivated", false);
                report.setProperty("appendQualified", false);
                report.setProperty("hardware", false);
                report.setProperty("listening", false);
                report.setProperty("fixtureSha256", juce::SHA256(binary).toHexString());
                report.setProperty("message", message);
                require(attempt.getChildFile("verification.json").replaceWithText(juce::JSON::toString(value)),
                        "Retain actual finite baseline terminal outcome");
            };
            record(false, false, "Original sample/finite-anchor baseline pending");
            try
            {
                initialFiniteEffectBaselineTests(attempt.getChildFile("baseline"), binary);
                record(true, true, "Actual Pattern/Song baseline/cancel/adoption; no effect append claim");
            }
            catch (const std::exception& e)
            {
                record(true, false, e.what());
                throw;
            }
            catch (...)
            {
                record(true, false, "Unknown original finite baseline failure");
                throw;
            }
            return 0;
        }
        if (streamPcmRequested || fixedSourcePcmRequested || sourceTimeRequested || chainFilesRequested)
        {
            require(chainFilesRequested
                        ? argc == 3 && juce::String(argv[1]) == "--mixer-chain-file-jobs-check"
                        : sourceTimeRequested
                        ? argc == 3 && juce::String(argv[1]) == "--source-render-time-check"
                        : fixedSourcePcmRequested
                        ? argc == 3 && juce::String(argv[1]) == "--fixed-source-pcm-epoch-check"
                        : argc == 5 && juce::String(argv[1]) == "--plugin-stream-pcm-check" &&
                              juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile() &&
                              juce::File::isAbsolutePath(argv[4]) && juce::File(argv[4]).existsAsFile(),
                    "Exact owned-output PCM invocation and applicable original SDK fixtures required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("listening", false);
            report.setProperty("hardware", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain actual Stream PCM terminal outcome");
            };
            retain();
            try
            {
                if (chainFilesRequested)
                {
                    const auto result = vibera::acceptance::effectChainFiles::verify(attempt.getChildFile("file-jobs"));
                    require(result.wasOk(), result.getErrorMessage().toStdString());
                }
                else if (sourceTimeRequested)
                    source_render_time_acceptance::sourceRenderTimeTests(attempt.getChildFile("source-time"));
                else if (fixedSourcePcmRequested)
                    fixedSourcePcmEpochGuardTests();
                else
                    plugin_stream_pcm::tests(attempt.getChildFile("stream-pcm"), juce::File(argv[3]),
                                             juce::File(argv[4]));
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            return 0;
        }
        bool chainMusical = false;
        for (int i = 1; i < argc; ++i)
            chainMusical |= juce::String(argv[i]).startsWith("--mixer-chain-live-export");
        if (chainMusical)
        {
            require(argc == 4 && juce::String(argv[1]) == "--mixer-chain-live-export-check" &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Exact musical-chain flag, owned output and existing absolute original fixture required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            const auto live = attempt.getChildFile("live"), offline = attempt.getChildFile("offline");
            const auto fixture = juce::File(argv[3]);
            bool livePassed = false, offlinePassed = false;
            auto record = [&](const juce::String &message)
            {
                auto value = std::make_unique<juce::DynamicObject>();
                value->setProperty("passed", livePassed && offlinePassed);
                value->setProperty("livePassed", livePassed);
                value->setProperty("offlinePassed", offlinePassed);
                value->setProperty("message", message);
                value->setProperty("scope", "Original VST3, actual Pattern/Song callbacks and separate "
                                            "saved-project WAV renders; no hardware/listening proof");
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(juce::var(value.release()))),
                        "Retain separate live and export musical outcomes");
            };
            record("started");
            try
            {
                mixerChainLiveTests(live, fixture);
                livePassed = true;
                record("live passed; export pending");
                mixerChainOfflineTests(offline, live, fixture);
                offlinePassed = true;
                record("completed");
            }
            catch (const std::exception &error)
            {
                record(error.what());
                throw;
            }
            return 0;
        }
        bool mixerCoreCheck = false;
        for (int i = 1; i < argc; ++i)
            mixerCoreCheck |= juce::String(argv[i]).startsWith("--routing-coordinator") ||
                              juce::String(argv[i]).startsWith("--stable-bus-stage") ||
                              juce::String(argv[i]).startsWith("--deferred-device-candidate-fault") ||
                              juce::String(argv[i]).startsWith("--plugin-recovery") ||
                              juce::String(argv[i]).startsWith("--mixer-chain-controls") ||
                              juce::String(argv[i]).startsWith("--plugin-wet-mix");
        if (mixerCoreCheck)
        {
            const juce::String flag = argv[1];
            const bool coordinator = flag == "--routing-coordinator-check";
            const bool stableStages = flag == "--stable-bus-stage-check";
            const bool deviceFault = flag == "--deferred-device-candidate-fault-check";
            const bool recovery = flag == "--plugin-recovery-check";
            const bool chain = flag == "--mixer-chain-controls-check";
            const bool wet = flag == "--plugin-wet-mix-check";
            require(
                (coordinator || stableStages || deviceFault || recovery || chain || wet) &&
                    argc == (wet || deviceFault || recovery ? 4 : 3),
                "Exact Mixer core check flag, owned output and applicable original native fixture required");
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            if (wet || deviceFault || recovery)
                require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                        "Wet mix original native fixture must be an existing absolute binary");
            const auto receipt = attempt.getChildFile("verification.json");
            auto record = [&](bool passed, const juce::String &message)
            {
                auto value = std::make_unique<juce::DynamicObject>();
                value->setProperty("passed", passed);
                value->setProperty("check", flag);
                value->setProperty("message", message);
                value->setProperty("scope", "Bounded original model/native experiment; not production UI, "
                                            "route-wide PDC, hardware or listening qualification");
                require(receipt.replaceWithText(juce::JSON::toString(juce::var(value.release()))),
                        "Retain actual Mixer core result");
            };
            record(false, "started");
            try
            {
                const auto folder = attempt.getChildFile("evidence");
                if (coordinator)
                    channelRoutingCoordinatorTests(folder);
                else if (stableStages)
                    stableBusStageTests(folder);
                else if (deviceFault)
                    deferredDeviceCandidateFaultTests(folder, parameterSpec(juce::File(argv[3])));
                else if (recovery)
                    pluginRecoveryTests(folder, juce::File(argv[3]));
                else if (chain)
                {
                    const auto result = vibera::acceptance::chainControls::verify(folder);
                    require(result.wasOk(), result.getErrorMessage().toStdString());
                }
                else
                    pluginHostWetMixTests(folder, juce::File(argv[3]));
                record(true, "completed");
            }
            catch (const std::exception &error)
            {
                record(false, error.what());
                throw;
            }
            return 0;
        }
        bool organisationRequested = false, macroConfigurationRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            organisationRequested |= juce::String(argv[i]).startsWith("--mixer-organisation");
            macroConfigurationRequested |= juce::String(argv[i]).startsWith("--mixer-macro-configuration");
        }
        if (organisationRequested || macroConfigurationRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == (macroConfigurationRequested
                                                               ? "--mixer-macro-configuration-check"
                                                               : "--mixer-organisation-check"),
                    "Exact owned-output organisation acceptance invocation required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("listening", false);
            report.setProperty("hardware", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain actual organisation/configuration outcome");
            };
            retain();
            try
            {
                if (macroConfigurationRequested)
                    vibera::test::mixer_macro_configuration::verify(attempt.getChildFile("profile"));
                else
                {
                    vibera::test::mixer_organisation::verify(attempt.getChildFile("organisation"));
                    const auto detector = vibera::acceptance::mixerDetectorModel::verify();
                    require(detector.wasOk(), detector.getErrorMessage().toStdString());
                    const auto structural = vibera::acceptance::capturedMixerStructural::verify();
                    require(structural.wasOk(), structural.getErrorMessage().toStdString());
                }
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            return 0;
        }
        bool initialSourceRequested = false, initialPortsRequested = false, initialActiveFxRequested = false,
             initialZeroHistoryRequested = false, initialJointRouteRequested = false,
             initialStableTrackMidiRequested = false, initialEpochRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            initialSourceRequested |= juce::String(argv[i]).startsWith("--initial-app-source");
            initialPortsRequested |= juce::String(argv[i]).startsWith("--initial-app-audio-port");
            initialActiveFxRequested |= juce::String(argv[i]).startsWith("--initial-app-active-fx-port");
            initialZeroHistoryRequested |= juce::String(argv[i]).startsWith("--initial-app-zero-history");
            initialJointRouteRequested |= juce::String(argv[i]).startsWith("--initial-source-joint-route");
            initialStableTrackMidiRequested |= juce::String(argv[i]).startsWith("--initial-stable-track-midi");
            initialEpochRequested |= juce::String(argv[i]).startsWith("--initial-source-epoch-fence");
        }
        if (initialSourceRequested || initialPortsRequested || initialActiveFxRequested ||
            initialZeroHistoryRequested || initialJointRouteRequested || initialStableTrackMidiRequested ||
            initialEpochRequested)
        {
            const bool controls = argc >= 2 && juce::String(argv[1]) == "--initial-app-source-controls-check";
            const bool ports = argc >= 2 && juce::String(argv[1]) == "--initial-app-audio-port-check";
            const bool activeFx = argc >= 2 && juce::String(argv[1]) == "--initial-app-active-fx-port-check";
            const bool zeroHistory = argc >= 2 && juce::String(argv[1]) == "--initial-app-zero-history-check";
            const bool jointRoute = argc >= 2 && juce::String(argv[1]) == "--initial-source-joint-route-check";
            const bool stableTrackMidi = argc >= 2 && juce::String(argv[1]) == "--initial-stable-track-midi-check";
            const bool epochFence = argc >= 2 && juce::String(argv[1]) == "--initial-source-epoch-fence-check";
            require(((jointRoute || stableTrackMidi || epochFence) ? argc == 4 : argc == 5) &&
                        (controls || ports || activeFx || zeroHistory || jointRoute || stableTrackMidi || epochFence ||
                         juce::String(argv[1]) == "--initial-app-source-check") &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile() &&
                        (jointRoute || stableTrackMidi || epochFence || (juce::File::isAbsolutePath(argv[4]) && juce::File(argv[4]).existsAsFile())),
                    "Exact owned-output and original SDK fixture paths required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("productionActivated", false);
            report.setProperty("hardware", false);
            report.setProperty("listening", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain initial application source experiment");
            };
            retain();
            try
            {
                if (epochFence)
                    initial_source_epoch_acceptance::tests(attempt.getChildFile("epoch-fence"), juce::File(argv[3]));
                else if (stableTrackMidi)
                    initial_app_stable_track_midi_route_acceptance::tests(attempt.getChildFile("stable-track-midi"),
                                                                         parameterSpec(juce::File(argv[3])));
                else if (jointRoute)
                    initial_app_joint_source_route_acceptance::tests(attempt.getChildFile("joint-route"),
                                                                    parameterSpec(juce::File(argv[3])));
                else if (zeroHistory)
                    initial_app_zero_history_acceptance::tests(attempt.getChildFile("zero-history"),
                                                               parameterSpec(juce::File(argv[3])));
                else if (activeFx)
                    initial_app_active_effect_acceptance::tests(attempt.getChildFile("active-fx"),
                                                                parameterSpec(juce::File(argv[3])),
                                                                parameterSpec(juce::File(argv[4])));
                else if (ports)
                    initialAppAudioPortTests(attempt.getChildFile("ports"),
                                             parameterSpec(juce::File(argv[3])),
                                             parameterSpec(juce::File(argv[4])));
                else if (controls)
                    initialAppSourceControlsTests(attempt.getChildFile("controls"),
                                                  parameterSpec(juce::File(argv[3])),
                                                  parameterSpec(juce::File(argv[4])));
                else
                    initialAppSourceTransportTests(attempt.getChildFile("initial"),
                                                   parameterSpec(juce::File(argv[3])),
                                                   parameterSpec(juce::File(argv[4])));
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            catch (...)
            {
                report.setProperty("finished", true);
                report.setProperty("error", "Unknown contained source experiment failure");
                retain();
                throw;
            }
            return 0;
        }
        bool inverseRequested = false, idleRequested = false, stereoRequested = false,
             stereoNativeRequested = false, stageRequested = false, automationCoreRequested = false,
             automationNativeRequested = false, phaseRequested = false, forwardRequested = false,
             prefixRequested = false, sendRequested = false, coldIdleRequested = false,
             coldPhaseRequested = false, warmRelocationRequested = false, zeroHistoryRequested = false,
             forwardSdkRequested = false, inverseSdkRequested = false, audioPortRequested = false,
             appMidiOwnershipRequested = false, boundedMidiOwnershipRequested = false,
             nativeMidiIngressRequested = false, appMixerPortRequested = false, chainDataRequested = false,
             zeroExchangeRequested = false, zeroChannelDelayRequested = false, dynamicInstrumentRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            inverseRequested |= juce::String(argv[i]).startsWith("--native-history-inverse");
            idleRequested |= juce::String(argv[i]).startsWith("--native-idle-overhead");
            phaseRequested |= juce::String(argv[i]).startsWith("--native-phase-overhead");
            coldIdleRequested |= juce::String(argv[i]).startsWith("--native-idle-cold-pruning");
            coldPhaseRequested |= juce::String(argv[i]).startsWith("--native-phase-cold-pruning");
            forwardRequested |= juce::String(argv[i]).startsWith("--forward-stage-drain");
            warmRelocationRequested |= juce::String(argv[i]).startsWith("--warm-source-relocation");
            zeroHistoryRequested |= juce::String(argv[i]).startsWith("--zero-history-creation");
            zeroExchangeRequested |= juce::String(argv[i]).startsWith("--zero-stage-exchange");
            zeroChannelDelayRequested |= juce::String(argv[i]).startsWith("--zero-channel-delay");
            dynamicInstrumentRequested |= juce::String(argv[i]).startsWith("--dynamic-instrument-latency");
            forwardSdkRequested |= juce::String(argv[i]).startsWith("--stopped-effect-forward-sdk");
            inverseSdkRequested |= juce::String(argv[i]).startsWith("--stopped-effect-inverse-sdk");
            audioPortRequested |= juce::String(argv[i]).startsWith("--mixer-audio-port");
            appMidiOwnershipRequested |= juce::String(argv[i]).startsWith("--app-audio-port-midi-ownership");
            boundedMidiOwnershipRequested |=
                juce::String(argv[i]).startsWith("--bounded-native-midi-ownership");
            nativeMidiIngressRequested |= juce::String(argv[i]).startsWith("--native-midi-ingress-dsp");
            appMixerPortRequested |= juce::String(argv[i]).startsWith("--app-mixer-audio-port");
            chainDataRequested |= juce::String(argv[i]).startsWith("--mixer-chain-data");
            prefixRequested |= juce::String(argv[i]).startsWith("--node-prefix-clear");
            sendRequested |= juce::String(argv[i]).startsWith("--send-automation-native");
            stereoRequested |= juce::String(argv[i]).startsWith("--mixer-stereo");
            stereoNativeRequested |= juce::String(argv[i]).startsWith("--mixer-stereo-native");
            stageRequested |= juce::String(argv[i]).startsWith("--output-stage-difference");
            automationCoreRequested |= juce::String(argv[i]).startsWith("--mixer-automation-core");
            automationNativeRequested |= juce::String(argv[i]).startsWith("--mixer-automation-native");
        }
        if (inverseRequested || idleRequested || stereoRequested || stageRequested ||
            automationCoreRequested || automationNativeRequested || phaseRequested || forwardRequested ||
            prefixRequested || sendRequested || coldIdleRequested || coldPhaseRequested ||
            warmRelocationRequested || zeroHistoryRequested || forwardSdkRequested || inverseSdkRequested ||
            audioPortRequested || appMidiOwnershipRequested || boundedMidiOwnershipRequested ||
            nativeMidiIngressRequested || appMixerPortRequested || chainDataRequested || zeroExchangeRequested || zeroChannelDelayRequested || dynamicInstrumentRequested)
        {
            require(
                ((inverseRequested || forwardSdkRequested || inverseSdkRequested || dynamicInstrumentRequested) ? argc == 4 : argc == 3) &&
                    juce::String(argv[1]) ==
                        (dynamicInstrumentRequested      ? "--dynamic-instrument-latency-check"
                         : zeroChannelDelayRequested     ? "--zero-channel-delay-check"
                         : zeroExchangeRequested         ? "--zero-stage-exchange-check"
                         : chainDataRequested            ? "--mixer-chain-data-check"
                         : appMixerPortRequested         ? "--app-mixer-audio-port-check"
                         : nativeMidiIngressRequested    ? "--native-midi-ingress-dsp-check"
                         : boundedMidiOwnershipRequested ? "--bounded-native-midi-ownership-check"
                         : appMidiOwnershipRequested     ? "--app-audio-port-midi-ownership-check"
                         : audioPortRequested            ? "--mixer-audio-port-check"
                         : inverseSdkRequested           ? "--stopped-effect-inverse-sdk-check"
                         : forwardSdkRequested           ? "--stopped-effect-forward-sdk-check"
                         : zeroHistoryRequested          ? "--zero-history-creation-check"
                         : warmRelocationRequested       ? "--warm-source-relocation-check"
                         : inverseRequested              ? "--native-history-inverse-check"
                         : coldIdleRequested             ? "--native-idle-cold-pruning-check"
                         : coldPhaseRequested            ? "--native-phase-cold-pruning-check"
                         : sendRequested                 ? "--send-automation-native-check"
                         : prefixRequested               ? "--node-prefix-clear-check"
                         : phaseRequested                ? "--native-phase-overhead-check"
                         : forwardRequested              ? "--forward-stage-drain-check"
                         : idleRequested                 ? "--native-idle-overhead-check"
                         : stereoNativeRequested         ? "--mixer-stereo-native-check"
                         : stageRequested                ? "--output-stage-difference-check"
                         : automationCoreRequested       ? "--mixer-automation-core-check"
                         : automationNativeRequested     ? "--mixer-automation-native-check"
                                                         : "--mixer-stereo-check"),
                "Exact owned-output diagnostic/control acceptance invocation required");
            if (forwardSdkRequested || inverseSdkRequested || dynamicInstrumentRequested)
                require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                        "Original absolute SDK latency fixture is required");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("listening", false);
            report.setProperty("hardware", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain actual control/diagnostic terminal outcome");
            };
            retain();
            try
            {
                if (inverseRequested)
                {
                    const juce::String selected(argv[3]);
                    require(selected == "manager" || selected == "controller",
                            "Exact inverse acceptance case");
                    if (selected == "manager")
                        vibera::draft::prepared_history_inverse::tests();
                    else
                        controller_native_inverse_acceptance::tests();
                }
                else if (coldIdleRequested || coldPhaseRequested)
                    vibera::test::native_idle::tests(attempt.getChildFile("cold-pruned"), coldPhaseRequested,
                                                     true);
                else if (phaseRequested || idleRequested)
                    vibera::test::native_idle::tests(attempt.getChildFile("ordinary"), phaseRequested);
                else if (warmRelocationRequested)
                    warm_source_relocation_acceptance::tests(attempt.getChildFile("warm-relocation"));
                else if (zeroHistoryRequested)
                    zero_history_creation_acceptance::tests(attempt.getChildFile("zero-history"));
                else if (dynamicInstrumentRequested)
                    dynamicInstrumentLatencyTests(attempt.getChildFile("dynamic-instrument"), juce::File(argv[3]));
                else if (zeroChannelDelayRequested)
                    zero_channel_delay_acceptance::tests(attempt.getChildFile("zero-channel"));
                else if (zeroExchangeRequested)
                    zero_stage_exchange_acceptance::tests(attempt.getChildFile("zero-exchange"));
                else if (audioPortRequested)
                    mixer_audio_port_acceptance::tests(attempt.getChildFile("audio-port"));
                else if (appMidiOwnershipRequested)
                {
                    vibera::test::app_audio_port::tests(attempt.getChildFile("app-midi-ownership"));
                    vibera::test::legacy_midi_runtime::tests(attempt.getChildFile("legacy-runtime"));
                }
                else if (boundedMidiOwnershipRequested)
                    vibera::test::app_audio_port::boundedTests(
                        attempt.getChildFile("bounded-midi-ownership"));
                else if (nativeMidiIngressRequested)
                    vibera::test::native_midi_ingress::tests(attempt.getChildFile("native-midi-ingress"));
                else if (appMixerPortRequested)
                {
                    vibera::test::terminal_ledger::tests(attempt.getChildFile("terminal-ledger"));
                    vibera::test::app_mixer_audio_port::tests(attempt.getChildFile("app-mixer-audio-port"));
                }
                else if (chainDataRequested)
                {
                    const auto result = vibera::acceptance::effectChains::verify();
                    require(result.wasOk(), result.getErrorMessage().toStdString());
                    const auto model = vibera::acceptance::mixerChainCommand::verify();
                    require(model.wasOk(), model.getErrorMessage().toStdString());
                    const auto replacement = vibera::acceptance::mixerEffectReplacement::verify();
                    require(replacement.wasOk(), replacement.getErrorMessage().toStdString());
                    const auto inserts = vibera::acceptance::mixerInsertLifecycle::verify();
                    require(inserts.wasOk(), inserts.getErrorMessage().toStdString());
                }
                else if (forwardSdkRequested)
                    stopped_effect_forward_sdk_acceptance::tests(attempt.getChildFile("sdk-forward"),
                                                                 juce::File(argv[3]));
                else if (inverseSdkRequested)
                    stopped_effect_inverse_sdk_acceptance::tests(attempt.getChildFile("sdk-inverse"),
                                                                 juce::File(argv[3]));
                else if (forwardRequested)
                    forwardStageDrainTests(attempt.getChildFile("forward"));
                else if (prefixRequested)
                    vibera::test::node_prefix_clear::tests(attempt.getChildFile("prefix"));
                else if (sendRequested)
                    vibera::test::send_automation_native_experiment::tests(attempt.getChildFile("send"));
                else if (stereoNativeRequested)
                    vibera::test::mixer_stereo_native::tests(attempt.getChildFile("native"));
                else if (stageRequested)
                    outputStageDifferenceTests(attempt.getChildFile("stage"));
                else if (automationCoreRequested)
                {
                    vibera::test::mixer_automation_core::tests(attempt.getChildFile("automation"));
                    const auto sendModel = vibera::acceptance::mixerSendModel::verify();
                    require(sendModel.wasOk(), sendModel.getErrorMessage().toStdString());
                    controllerMappingTests();
                }
                else if (automationNativeRequested)
                {
                    vibera::test::mixer_automation_native::tests(attempt.getChildFile("native"));
                    vibera::test::mixer_send_production::tests(attempt.getChildFile("production-send"));
                }
                else
                    vibera::test::mixer_stereo::tests();
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            catch (...)
            {
                report.setProperty("finished", true);
                report.setProperty("error", "Unknown contained acceptance failure");
                retain();
                throw;
            }
            return 0;
        }
        if (argc > 1 && juce::String(argv[1]) == "--production-membership-sdk")
        {
            require(argc == 4 && juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Production membership SDK requires owned output and genuine absolute VST3 fixture");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            productionMembershipSdkTests(attempt.getChildFile("scenarios"), juce::File(argv[3]));
            std::cout << "Actual ordinary membership SDK cancellation PASS; graph/PDC/UI/listening remain "
                         "separate\n";
            return 0;
        }
        bool dryHistoryHostRequested = false;
        for (int i = 1; i < argc; ++i)
            dryHistoryHostRequested |= juce::String(argv[i]).startsWith("--plugin-dry-history-host");
        if (dryHistoryHostRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--plugin-dry-history-host-check" &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "SDK dry-history check needs exact flag, owned output and original absolute SDK binary");
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("nativeGraphRetirementQualified", false);
            report.setProperty("bypassTransitionSamples", 240);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain actual SDK dry-history terminal outcome");
            };
            retain();
            try
            {
                vibera::test::dry_history_host::tests(attempt, juce::File(argv[3]));
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            catch (...)
            {
                report.setProperty("finished", true);
                report.setProperty("error", "Unknown SDK dry-history failure");
                retain();
                throw;
            }
            return 0;
        }
        bool dryHistoryRequested = false;
        for (int i = 1; i < argc; ++i)
            dryHistoryRequested |= juce::String(argv[i]).startsWith("--plugin-dry-history");
        if (dryHistoryRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--plugin-dry-history-check",
                    "Dry history component check requires exact flag and owned build output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            vibera::test::plugin_dry_history::tests(attempt.getChildFile("component"));
            std::cout
                << "Actual dry ring component handoff PASS; SDK/graph/history/UI/listening remain separate\n";
            return 0;
        }
        bool ordinaryForwardRequested = false;
        for (int i = 1; i < argc; ++i)
            ordinaryForwardRequested |=
                juce::String(argv[i]).startsWith("--stopped-effects-ordinary-forward");
        if (ordinaryForwardRequested)
        {
            require(
                argc == 4 && juce::String(argv[1]) == "--stopped-effects-ordinary-forward-check" &&
                    juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                "Ordinary forward check requires exact flag, owned output and absolute original SDK fixture");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("ordinaryForwardPcmQualified", false);
            report.setProperty("inverseQualified", false);
            report.setProperty("productionActivated", false);
            report.setProperty("expectedDelayFrames", 799);
            report.setProperty("sampleRate", 48000);
            report.setProperty("callbackFrames", 256);
            report.setProperty("executableSha256",
                               juce::SHA256(juce::File::getSpecialLocation(juce::File::currentExecutableFile))
                                   .toHexString());
            report.setProperty("providerSha256", juce::SHA256(juce::File(argv[3])).toHexString());
            const auto worker = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                    .getSiblingFile("VibeRa Plugin Worker.exe");
            require(worker.existsAsFile(), "Actual sibling owned plugin worker binary");
            report.setProperty("workerSha256", juce::SHA256(worker).toHexString());
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain actual ordinary-forward result before rethrowing");
            };
            retain();
            try
            {
                stoppedEffectOrdinaryForwardTests(attempt, juce::File(argv[3]));
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                report.setProperty("ordinaryForwardPcmQualified", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            catch (...)
            {
                report.setProperty("finished", true);
                report.setProperty("error", "Unknown ordinary-forward failure");
                retain();
                throw;
            }
            return 0;
        }
        bool stoppedEffectNativeRequested = false;
        for (int i = 1; i < argc; ++i)
            stoppedEffectNativeRequested |= juce::String(argv[i]).startsWith("--stopped-effects-native");
        if (stoppedEffectNativeRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--stopped-effects-native-check" &&
                        juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Stopped effects native check requires exact flag, owned output and original absolute "
                    "SDK fixture");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var value(new juce::DynamicObject());
            auto &report = *value.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("graphTransitionPcmQualified", false);
            report.setProperty("productionActivated", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(value, true)),
                        "Retain native transaction actual terminal result");
            };
            retain();
            try
            {
                stoppedEffectChangeNativeTests(attempt, juce::File(argv[3]));
                report.setProperty("finished", true);
                report.setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                report.setProperty("finished", true);
                report.setProperty("error", juce::String(error.what()));
                retain();
                throw;
            }
            catch (...)
            {
                report.setProperty("finished", true);
                report.setProperty("error", "Unknown native transaction failure");
                retain();
                throw;
            }
            return 0;
        }
        bool stoppedEffectPlanRequested = false;
        for (int i = 1; i < argc; ++i)
            stoppedEffectPlanRequested |= juce::String(argv[i]).startsWith("--stopped-effect-plan");
        if (stoppedEffectPlanRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--stopped-effect-plan-check",
                    "Stopped effect planner requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            const auto result = vibera::acceptance::stoppedEffects::verifyPlan();
            juce::var value(new juce::DynamicObject());
            value.getDynamicObject()->setProperty("finished", true);
            value.getDynamicObject()->setProperty("passed", result.wasOk());
            value.getDynamicObject()->setProperty("error", result.getErrorMessage());
            value.getDynamicObject()->setProperty("nativeAudioQualified", false);
            require(
                attempt.getChildFile("verification.json").replaceWithText(juce::JSON::toString(value, true)),
                "Retain actual stopped-effect planner result");
            require(result.wasOk(), result.getErrorMessage().toStdString());
            return 0;
        }
        bool localSourceInitialRequested = false;
        bool mixerNativeIdentityRequested = false;
        bool pluginPcmReceiptRequested = false;
        bool mixerInsertOrderRequested = false;
        bool mixerNativeExtendedRequested = false;
        bool summingPropertiesRequested = false;
        bool installedEffectBusRequested = false;
        bool preparedSourceHostsRequested = false, sourceOverheadRequested = false;
        bool graphContractsRequested = false, guideRuntimeRequested = false, controlRuntimeRequested = false,
             instrumentContextRequested = false, waveContextRequested = false,
             routingLifetimeRequested = false, turnaroundRequested = false,
             turnaroundEffectsRequested = false, physicalWorkerRequested = false,
             routingCaptureRequested = false, preparedTransportRequested = false,
             preparedTransportCoverageRequested = false, sourceBurstRequested = false,
             mixerTargetChoicesRequested = false, dormantSamplerRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            graphContractsRequested |= juce::String(argv[i]).startsWith("--structural-graph-contracts");
            guideRuntimeRequested |= juce::String(argv[i]).startsWith("--guide-runtime");
            controlRuntimeRequested |= juce::String(argv[i]).startsWith("--control-runtime");
            instrumentContextRequested |= juce::String(argv[i]).startsWith("--instrument-context");
            waveContextRequested |= juce::String(argv[i]).startsWith("--wave-context");
            routingLifetimeRequested |= juce::String(argv[i]).startsWith("--routing-lifetime");
            turnaroundRequested |= juce::String(argv[i]).startsWith("--worker-turnaround");
            turnaroundEffectsRequested |= juce::String(argv[i]).startsWith("--effect-turnaround");
            physicalWorkerRequested |= juce::String(argv[i]).startsWith("--physical-worker");
            preparedTransportRequested |= juce::String(argv[i]).startsWith("--prepared-plugin-transport");
            preparedTransportCoverageRequested |=
                juce::String(argv[i]).startsWith("--prepared-plugin-transport-coverage");
            sourceBurstRequested |= juce::String(argv[i]).startsWith("--source-burst");
            localSourceInitialRequested |= juce::String(argv[i]).startsWith("--local-source-initial");
            mixerNativeIdentityRequested |= juce::String(argv[i]).startsWith("--mixer-native-identity");
            pluginPcmReceiptRequested |= juce::String(argv[i]).startsWith("--plugin-pcm-receipt");
            mixerInsertOrderRequested |= juce::String(argv[i]).startsWith("--mixer-insert-order");
            mixerNativeExtendedRequested |= juce::String(argv[i]).startsWith("--mixer-native-extended");
            summingPropertiesRequested |= juce::String(argv[i]).startsWith("--summing-properties");
            installedEffectBusRequested |= juce::String(argv[i]).startsWith("--installed-effect-bus");
            preparedSourceHostsRequested |= juce::String(argv[i]).startsWith("--prepared-source-hosts");
            sourceOverheadRequested |= juce::String(argv[i]).startsWith("--source-overhead");
            dormantSamplerRequested |= juce::String(argv[i]).startsWith("--dormant-sampler-runtime");
            mixerTargetChoicesRequested |= juce::String(argv[i]).startsWith("--mixer-target-choices");
            routingCaptureRequested |= juce::String(argv[i]).startsWith("--controller-routing-capture");
        }
        if (preparedSourceHostsRequested || sourceOverheadRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == (preparedSourceHostsRequested
                                                               ? "--prepared-source-hosts-check"
                                                               : "--source-overhead-check"),
                    "Source acceptance requires exact flag, owned output and absolute original SDK fixture");
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Explicit original source fixture input exists");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::var receipt(new juce::DynamicObject());
            auto &report = *receipt.getDynamicObject();
            report.setProperty("finished", false);
            report.setProperty("passed", false);
            report.setProperty("productionActivated", false);
            report.setProperty("nativeInput", false);
            report.setProperty("listening", false);
            report.setProperty("hardware", false);
            report.setProperty("kind", preparedSourceHostsRequested
                                           ? "fixed initial prepared worker membership"
                                           : "separate unchanged-oracle callback overhead diagnostic");
            juce::Array<juce::var> cases;
            const auto retain = [&]
            {
                report.setProperty("cases", cases);
                require(attempt.getChildFile("verification.json")
                            .replaceWithText(juce::JSON::toString(receipt, true)),
                        "Retain actual source acceptance terminal outcomes");
            };
            retain();
            std::exception_ptr failure;
            const auto runCase = [&](const char *name, auto &&body)
            {
                juce::var item(new juce::DynamicObject());
                item.getDynamicObject()->setProperty("case", name);
                item.getDynamicObject()->setProperty("passed", false);
                try
                {
                    body();
                    item.getDynamicObject()->setProperty("passed", true);
                }
                catch (const std::exception &error)
                {
                    item.getDynamicObject()->setProperty("error", juce::String(error.what()));
                    if (!failure)
                        failure = std::current_exception();
                }
                catch (...)
                {
                    item.getDynamicObject()->setProperty("error", "Unknown contained acceptance failure");
                    if (!failure)
                        failure = std::current_exception();
                }
                cases.add(item);
                retain();
            };
            if (preparedSourceHostsRequested)
            {
                runCase("coordinator", [&] { preparedSourceHostsTests(); });
                runCase("actual-native-two-host",
                        [&]
                        {
                            prepared_source_initial_hosts_acceptance::tests(
                                attempt.getChildFile("initial-hosts"), juce::File(argv[3]));
                        });
            }
            else
                runCase("worker-overhead",
                        [&]
                        {
                            source_burst_acceptance::workerOverheadTests(attempt.getChildFile("worker"),
                                                                         juce::File(argv[3]));
                        });
            report.setProperty("finished", true);
            report.setProperty("passed", !failure);
            retain();
            if (failure)
                std::rethrow_exception(failure);
            return 0;
        }
        if (installedEffectBusRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--installed-effect-bus-check",
                    "Installed effect probe requires exact flag, owned output, absolute binary and metadata");
            require(juce::File::isAbsolutePath(argv[3]) && juce::File::isAbsolutePath(argv[4]),
                    "Provider inputs must be explicit absolute paths; no discovery scan");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            installedEffectBusReadinessTests(attempt.getChildFile("provider"), argv[3], argv[4]);
            return 0;
        }
        if (summingPropertiesRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--summing-properties-check",
                    "Summing properties require exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            vibera::test::summing_properties::tests(attempt.getChildFile("properties"));
            return 0;
        }
        if (mixerNativeExtendedRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--mixer-native-extended-check",
                    "Extended Mixer identity requires exact flag, owned output and exact case");
            const juce::String selected(argv[3]);
            require(selected == "offline-pattern" || selected == "offline-song" ||
                        selected == "native129-pattern" || selected == "native129-song",
                    "Unknown extended native Mixer identity case");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            juce::DynamicObject::Ptr result = new juce::DynamicObject;
            result->setProperty("case", selected);
            result->setProperty("passed", false);
            const auto retain = [&]
            {
                require(attempt.getChildFile("case.json")
                            .replaceWithText(juce::JSON::toString(juce::var(result.get()))),
                        "Retain separate extended case outcome");
            };
            retain();
            try
            {
                const bool pattern = selected.endsWith("pattern");
                if (selected.startsWith("offline"))
                    vibera::test::mixer_native_identity_extended::sparseOfflineCase(
                        attempt.getChildFile("case"), pattern);
                else
                    vibera::test::mixer_native_identity_extended::fullUniverseCase(
                        attempt.getChildFile("case"), pattern);
                result->setProperty("passed", true);
                retain();
            }
            catch (const std::exception &error)
            {
                result->setProperty("failure", juce::String(error.what()));
                retain();
                throw;
            }
            return 0;
        }
        if (mixerInsertOrderRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--mixer-insert-order-check",
                    "Mixer insert order requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            vibera::test::mixer_insert_order::verify(attempt);
            return 0;
        }
        if (pluginPcmReceiptRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--plugin-pcm-receipt-check",
                    "PCM receipts require exact flag, owned output and two genuine absolute SDK fixtures");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            for (int i : {3, 4})
                require(juce::File::isAbsolutePath(argv[i]) && juce::File(argv[i]).existsAsFile(),
                        "PCM receipt fixture must be an existing absolute SDK binary");
            std::exception_ptr firstFailure;
            juce::Array<juce::var> outcomes;
            const auto runCase = [&](const juce::String &name, const auto &execute)
            {
                auto row = std::make_unique<juce::DynamicObject>();
                row->setProperty("case", name);
                try
                {
                    execute(attempt.getChildFile(name));
                    row->setProperty("passed", true);
                }
                catch (const std::exception &error)
                {
                    row->setProperty("passed", false);
                    row->setProperty("error", juce::String(error.what()));
                    if (!firstFailure)
                        firstFailure = std::current_exception();
                }
                outcomes.add(juce::var(row.release()));
                require(attempt.getChildFile("cases.json").replaceWithText(juce::JSON::toString(outcomes)),
                        "Retain independent Host PCM receipt outcomes");
            };
            runCase("output", [&](const juce::File &folder)
                    { pluginPcmReceiptTests(folder, juce::File(argv[3]), juce::File(argv[4])); });
            runCase("prepared", [&](const juce::File &folder)
                    { pluginPcmPreparedReceiptTests(folder, juce::File(argv[4])); });
            if (firstFailure)
                std::rethrow_exception(firstFailure);
            return 0;
        }
        if (mixerNativeIdentityRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--mixer-native-identity-check",
                    "Native Mixer identity requires exact flag, owned output and original SDK binary");
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Native Mixer identity requires a genuine absolute SDK fixture");
            const auto spec = parameterSpec(juce::File(argv[3]));
            require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
                    "Exact original SDK effect required, no placeholder qualification");
            vibera::test::mixer_native_identity::tableTests();
            std::exception_ptr firstFailure;
            juce::Array<juce::var> outcomes;
            const auto runCase = [&](const juce::String &name, const auto &execute)
            {
                auto row = std::make_unique<juce::DynamicObject>();
                row->setProperty("case", name);
                try
                {
                    execute(attempt.getChildFile(name));
                    row->setProperty("passed", true);
                }
                catch (const std::exception &error)
                {
                    row->setProperty("passed", false);
                    row->setProperty("error", juce::String(error.what()));
                    if (!firstFailure)
                        firstFailure = std::current_exception();
                }
                outcomes.add(juce::var(row.release()));
                require(attempt.getChildFile("cases.json").replaceWithText(juce::JSON::toString(outcomes)),
                        "Retain every native identity case");
            };
            for (bool provider : {false, true})
                if (firstFailure)
                    std::rethrow_exception(firstFailure);
            return 0;
        }
        if (dormantSamplerRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--dormant-sampler-runtime-check",
                    "Dormant sampler check requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            vibera::test::dormantSamplerRuntimeTests(attempt);
            return 0;
        }
        if (mixerTargetChoicesRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--mixer-target-choices-check",
                    "Mixer target model check requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            mixerTargetChoicesTests(attempt);
            return 0;
        }
        if (localSourceInitialRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--local-source-initial-check",
                    "Local initial source requires exact flag, owned output and original SDK binary");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original local initial SDK fixture must be an existing absolute binary");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain local initial source attempt before processing");
            try
            {
                local_source_initial_acceptance::tests(attempt.getChildFile("local"), juce::File(argv[3]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual LOCAL-CLOSED initial native/source/original "
                            "SDK experiment; not production, hardware or listening\"}\n"),
                        "Retain actual local initial source result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (sourceBurstRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--source-burst-check",
                    "Native source admission requires exact flag, owned output and original SDK fixture");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original native source fixture must be an existing absolute binary");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain native source attempt before processing");
            try
            {
                source_burst_acceptance::tests(attempt.getChildFile("native"));
                source_burst_acceptance::workerTests(attempt.getChildFile("worker"), juce::File(argv[3]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual native source conversion/two-context "
                            "admission and original SDK graph; not production policy, hardware or "
                            "listening\"}\n"),
                        "Retain actual native source result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (preparedTransportCoverageRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--prepared-plugin-transport-coverage-check",
                    "Prepared coverage requires exact flag, owned output and two original SDK fixtures");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            for (int index : {3, 4})
                require(juce::File::isAbsolutePath(argv[index]) && juce::File(argv[index]).existsAsFile(),
                        "Original coverage fixtures must be existing absolute native binaries");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain prepared coverage before worker startup");
            try
            {
                preparedPluginTransportCoverageTests(attempt.getChildFile("scenarios"), juce::File(argv[3]),
                                                     juce::File(argv[4]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual SDK K2..8 and pending MIDI/parameters "
                            "through abort/prune/resume; not product graph, hardware or listening\"}\n"),
                        "Retain actual prepared transport coverage");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (preparedTransportRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--prepared-plugin-transport-check",
                    "Prepared transport requires exact flag, owned output and original SDK fixture");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original prepared transport fixture must be an existing absolute native binary");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain prepared transport attempt before worker startup");
            try
            {
                preparedPluginTransportTests(attempt.getChildFile("scenarios"), juce::File(argv[3]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual physical reader and original SDK prepared "
                            "K3 transport/scopes; not product graph, installed provider, hardware or "
                            "listening\"}\n"),
                        "Retain actual prepared transport result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (routingCaptureRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--controller-routing-capture-check",
                    "Routing capture history requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Preserve routing capture history attempt");
            try
            {
                controller_routing_capture_acceptance::tests(attempt);
                require(receipt.replaceWithText("{\"passed\":true,\"scope\":\"Actual canonical model "
                                                "history; no hosted plugin, hardware or listening\"}\n"),
                        "Preserve routing capture history result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (physicalWorkerRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--physical-worker-check",
                    "Physical worker check requires exact flag, owned output and two original fixtures");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            for (int index : {3, 4})
                require(juce::File::isAbsolutePath(argv[index]) && juce::File(argv[index]).existsAsFile(),
                        "Original physical-worker fixture must be an existing absolute native binary");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain physical-worker attempt before provider startup");
            try
            {
                physicalWorkerTurnaroundTests(attempt.getChildFile("scenarios"), juce::File(argv[3]),
                                              juce::File(argv[4]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual synthetic physical callback and explicit "
                            "2Q original workers; not product graph, generic K, hardware or listening\"}\n"),
                        "Retain physical-worker outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (turnaroundEffectsRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--effect-turnaround-check",
                    "Effect turnaround requires exact flag, owned output and two original native fixtures");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            for (int index : {3, 4})
                require(juce::File::isAbsolutePath(argv[index]) && juce::File(argv[index]).existsAsFile(),
                        "Original effect fixture must be an existing absolute native binary");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain effect experiment before provider startup");
            try
            {
                pluginTurnaroundEffect512Tests(attempt.getChildFile("effects"), juce::File(argv[3]),
                                               juce::File(argv[4]));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Explicit two-quantum original effects; not "
                            "product, graph, physical callback deadline or listening qualification\"}\n"),
                        "Retain actual effect turnaround result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            return 0;
        }
        if (turnaroundRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--worker-turnaround-check",
                    "Worker turnaround requires exact flag, owned output and original instrument");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).exists(),
                    "Original worker fixture must be an existing absolute native input");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain worker experiment before scanning/startup");
            try
            {
                pluginTurnaround512Tests(attempt, parameterSpec(juce::File(argv[3])));
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Explicit two-quantum original SDK direct Host "
                            "experiment; default product remains one quantum; no native graph, outer-device "
                            "burst, hardware or listening qualification\"}\n"),
                        "Retain actual worker turnaround experiment outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS original explicit two-quantum worker experiment\n";
            return 0;
        }
        if (routingLifetimeRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--routing-lifetime-check",
                    "Routing lifetime requires exact flag and owned output");
            juce::File attempt;
            const auto admission = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admission.wasOk(), admission.getErrorMessage().toStdString());
            // This closed CONTROL lifecycle experiment can throw from native
            // async preparation/destructors. Retain the actual fatal reason;
            // a crash is never converted into a passing test or retried blindly.
            struct FatalDiagnostic
            {
                std::terminate_handler previous = std::set_terminate(
                    []
                    {
                        try
                        {
                            if (auto error = std::current_exception())
                                std::rethrow_exception(error);
                        }
                        catch (const std::exception &error)
                        {
                            std::cerr << "FATAL lifecycle exception: " << error.what() << '\n';
                        }
                        catch (...)
                        {
                            std::cerr << "FATAL unknown lifecycle exception\n";
                        }
                        std::cerr << juce::SystemStats::getStackBacktrace() << std::endl;
                        std::_Exit(86);
                    });
                ~FatalDiagnostic()
                {
                    std::set_terminate(previous);
                }
            } fatalDiagnostic;
            juce::DynamicObject::Ptr report = new juce::DynamicObject;
            report->setProperty("passed", false);
            report->setProperty("phase", "started");
            const auto receipt = attempt.getChildFile("verification.json");
            const auto record = [&]
            {
                require(receipt.replaceWithText(juce::JSON::toString(juce::var(report.get()))),
                        "Retain routing lifetime phase before executing it");
            };
            try
            {
                record();
                report->setProperty("phase", "external-owner-lifetime");
                record();
                routingLifetimeTests(attempt.getChildFile("lifetime"));
                report->setProperty("phase", "missing-instrument");
                record();
                missingInstrumentRoutingLifetimeTests(attempt.getChildFile("missing"));
                report->setProperty("phase", "device-preparation-fence");
                record();
                routingDeviceFenceTests(attempt.getChildFile("device"));
                report->setProperty("passed", true);
                report->setProperty("phase", "completed");
                report->setProperty(
                    "scope", "Actual native lifetime, missing-placeholder route, and synthetic device "
                             "change; not hosted-device restoration, concurrency, hardware or listening");
                record();
            }
            catch (const std::exception &error)
            {
                report->setProperty("failure", juce::String(error.what()));
                record();
                throw;
            }
            std::cout << "PASS actual routing lifetime and device fence\n";
            return 0;
        }
        if (waveContextRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--wave-context-check",
                    "Wave context requires exact flag and owned output");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain Wave attempt before native graph work");
            try
            {
                retainedWaveContextTests(attempt);
                require(receipt.replaceWithText("{\"passed\":true,\"scope\":\"Actual native Wave decoder and "
                                                "canonical routing/history; "
                                                "not concurrent preparation, hardware "
                                                "or listening\"}\n"),
                        "Retain actual native Wave outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS actual native Wave context scenarios\n";
            return 0;
        }
        if (instrumentContextRequested)
        {
            const bool packetBaseline =
                argc >= 2 && juce::String(argv[1]) == "--instrument-context-packet-boundary-check";
            const bool original =
                packetBaseline || (argc >= 2 && juce::String(argv[1]) == "--instrument-context-check");
            require(
                argc == 4 && (original || juce::String(argv[1]) == "--instrument-context-provider-check"),
                "Instrument context requires exact original/provider flag, owned output and native plugin");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).exists(),
                    "Instrument provider must be an existing absolute native input");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain instrument context before scanning or execution");
            try
            {
                const auto scannedSpec = parameterSpec(juce::File(argv[3]));
                if (!packetBaseline)
                    retainedInstrumentContextTests(attempt, scannedSpec, original);
                retainedInstrumentApplicationRoutingTests(attempt, scannedSpec, original, !packetBaseline);
                require(receipt.replaceWithText(
                            original ? "{\"passed\":true,\"scope\":\"Original SDK actual same-Edit graph "
                                       "adoption, Pattern/Song held MIDI and partial packets; not concurrent "
                                       "preparation or listening\"}\n"
                                     : "{\"passed\":true,\"scope\":\"Installed provider actual same-Edit "
                                       "graph lifecycle and finite nonzero PCM; not exact DSP equivalence, "
                                       "concurrent preparation or listening\"}\n"),
                        "Retain actual instrument graph outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS actual instrument context scenarios\n";
            return 0;
        }
        if (controlRuntimeRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--control-runtime-check",
                    "Control runtime requires exact flag and owned output");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain controls before component work");
            try
            {
                vibera::test::controlRuntimeTests(attempt);
                require(receipt.replaceWithText(
                            "{\"passed\":true,\"scope\":\"Actual balance, gain, master and meter component "
                            "continuity; not graph publication, native input or listening\"}\n"),
                        "Retain actual control outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS actual control runtime component scenarios\n";
            return 0;
        }
        if (guideRuntimeRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--guide-runtime-check",
                    "Guide runtime requires exact flag and owned output");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain guide attempt before component work");
            try
            {
                vibera::test::guideRuntimeTests(attempt);
                require(
                    receipt.replaceWithText(
                        "{\"passed\":true,\"scope\":\"Actual Guide component and native lifecycle, "
                        "independent analytic click PCM; not graph adoption, native input or listening\"}\n"),
                    "Retain actual guide outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS actual guide runtime component scenarios\n";
            return 0;
        }
        if (graphContractsRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--structural-graph-contracts-check",
                    "Structural graph contracts require exact flag and owned output");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain graph contract attempt before execution");
            try
            {
                vibera::test::selected_graph_latency::playerTests(attempt.getChildFile("selection"));
                vibera::test::graphControlRuntimeOwnershipChecks(require);
                vibera::test::graphOutputRoutingTests(attempt);
                vibera::test::graphOutputRoutingPcmTests(attempt);
                vibera::test::mixer_send_runtime::tests(attempt.getChildFile("send-runtime"));
                vibera::test::send_gain_state::tests(attempt.getChildFile("native-send-state"));
                vibera::test::send_gain_time::tests(attempt.getChildFile("native-send-time"));
                vibera::test::publication_serial::tests(attempt.getChildFile("publication-identity"));
                require(
                    receipt.replaceWithText("{\"passed\":true,\"scope\":\"Actual native player latency PCM, "
                                            "Edit routing admission and CONTROL ownership; not production "
                                            "routing, recording, native input or listening\"}\n"),
                    "Retain actual graph contract outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS actual structural graph contracts " << assertions << " assertions\n";
            return 0;
        }
        bool effectRuntimeRequested = false, structuralPreparationRequested = false,
             instrumentRuntimeRequested = false;
        for (int i = 1; i < argc; ++i)
        {
            effectRuntimeRequested |= juce::String(argv[i]).startsWith("--remote-effect-runtime");
            instrumentRuntimeRequested |= juce::String(argv[i]).startsWith("--instrument-runtime");
            structuralPreparationRequested |=
                juce::String(argv[i]).startsWith("--structural-command-preparation");
        }
        if (instrumentRuntimeRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--instrument-runtime-check",
                    "Instrument runtime requires exact flag, owned output and original SDK instrument");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original instrument fixture must be an existing absolute input");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain instrument attempt before provider/component work");
            try
            {
                vibera::test::instrumentRuntimeTests(attempt, parameterSpec(juce::File(argv[3])));
                require(
                    receipt.replaceWithText("{\"passed\":true,\"scope\":\"Original SDK component and offline "
                                            "worker; not graph adoption or installed provider\"}\n"),
                    "Retain actual original instrument outcome");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS original instrument runtime experiment\n";
            return 0;
        }
        if (effectRuntimeRequested)
        {
            require(argc == 5 && juce::String(argv[1]) == "--remote-effect-runtime-check",
                    "Effect runtime requires exact flag, owned output and two original SDK fixtures");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile() &&
                        juce::File::isAbsolutePath(argv[4]) && juce::File(argv[4]).existsAsFile(),
                    "Both original SDK fixture binaries must be existing absolute inputs");
            remoteEffectRuntimeTests(attempt, juce::File(argv[3]), juce::File(argv[4]));
            std::cout << "PASS original effect partial-packet runtime " << assertions << " assertions\n";
            return 0;
        }
        if (structuralPreparationRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--structural-command-preparation-check",
                    "Structural preparation requires exact flag and one owned output directory");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            controller_structural_preparation_acceptance::tests(attempt);
            std::cout << "PASS canonical structural command preparation " << assertions << " assertions\n";
            return 0;
        }
        // Reserved prefix must refuse malformed requests before any legacy path.
        bool fixedInitialisationRequested = false;
        for (int i = 1; i < argc; ++i)
            fixedInitialisationRequested |= juce::String(argv[i]).startsWith("--fixed-initialisation");
        if (fixedInitialisationRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--fixed-initialisation-check",
                    "Fixed initialisation requires exact flag and one owned output directory");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            runFixedInitialisationLifecycle(attempt);
            std::cout << "PASS original actual fixed initialisation lifecycle " << assertions
                      << " assertions\n";
            return 0;
        }
        bool concurrentRetainedRequested = false;
        for (int i = 1; i < argc; ++i)
            concurrentRetainedRequested |= juce::String(argv[i]).startsWith("--concurrent-retained-context");
        if (concurrentRetainedRequested)
        {
            require(
                argc == 4 && juce::String(argv[1]) == "--concurrent-retained-context-check",
                "Concurrent retained context requires exact flag, owned output and original fixture binary");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original SDK fixture must be an existing absolute input");
            concurrentRetainedContextTests(attempt, juce::File(argv[3]));
            std::cout << "PASS actual concurrent retained context experiment " << assertions
                      << " assertions\n";
            return 0;
        }
        bool retainedContextRequested = false;
        for (int i = 1; i < argc; ++i)
            retainedContextRequested |= juce::String(argv[i]).startsWith("--retained-context");
        if (retainedContextRequested)
        {
            require(argc == 4 && juce::String(argv[1]) == "--retained-context-check",
                    "Retained context requires exact flag, owned output and original fixture binary");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Original SDK fixture must be an existing absolute input");
            retainedContextExperimentTests(attempt, juce::File(argv[3]));
            std::cout << "PASS original actual retained context experiment " << assertions << " assertions\n";
            return 0;
        }
        bool preparedTimelineRequested = false;
        for (int i = 1; i < argc; ++i)
            preparedTimelineRequested |= juce::String(argv[i]).startsWith("--prepared-timeline");
        if (preparedTimelineRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--prepared-timeline-check",
                    "Prepared timeline requires exact flag and one owned output directory");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            preparedTimelineAcceptance();
            require(attempt.getChildFile("prepared-timeline.txt")
                        .replaceWithText("PASS prepared graph-scoped timeline ownership primitive; "
                                         "stale/cancel/refusal/allocation and retained-reader cases. "
                                         "Ownership-only scope test; no actual NodePlayer adoption, DSP, "
                                         "native input, listening or hardware qualification.\n"),
                    "Retain prepared timeline ownership result");
            std::cout << "PASS prepared timeline ownership " << assertions << " assertions\n";
            return 0;
        }
        bool midiTakeRecoveryRequested = false;
        for (int i = 1; i < argc; ++i)
            midiTakeRecoveryRequested |= juce::String(argv[i]).startsWith("--midi-take-recovery");
        if (midiTakeRecoveryRequested)
        {
            require(argc == 3 && juce::String(argv[1]) == "--midi-take-recovery-check",
                    "MIDI recovery primitive requires exact flag and one owned output");
            juce::File attempt;
            const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
            midi_take_recovery_acceptance::tests(attempt);
            std::cout << "PASS original MIDI take retention primitive " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && (juce::String(argv[1]) == "--retained-midi-check" ||
                          juce::String(argv[1]) == "--bounded-midi-check"))
        {
            const auto folder = juce::String(argv[1]) == "--bounded-midi-check"
                                    ? juce::File(argv[2]).getChildFile("attempt-" + juce::Uuid().toString())
                                    : juce::File(argv[2]);
            require(juce::File::isAbsolutePath(argv[2]) && folder.createDirectory().wasOk(),
                    "Retained MIDI diagnostic requires explicit owned output directory");
            require(!folder.getChildFile("retained-midi.txt").exists() &&
                        !folder.getChildFile("retained-midi-half-open.txt").exists() &&
                        !folder.getChildFile("retained-midi-runner.txt").exists(),
                    "Use a fresh owned diagnostic folder; preserve prior MIDI failure receipts");
            bool originalPassed = false, boundaryPassed = false;
            juce::String summary;
            try
            {
                vibera::draft::retained_midi::retainedMidiDraftTests(folder);
                originalPassed = true;
            }
            catch (const std::exception &error)
            {
                summary += "original20: " + juce::String(error.what()) + "\n";
            }
            catch (...)
            {
                summary += "original20: unclassified exception\n";
            }
            // Always exercise the independent boundary contract, even when the
            // original queue-transition/panic experiment is red. No oracle changes.
            try
            {
                vibera::draft::retained_midi::retainedMidiHalfOpenTests(folder);
                boundaryPassed = true;
            }
            catch (const std::exception &error)
            {
                summary += "halfOpen4: " + juce::String(error.what()) + "\n";
            }
            catch (...)
            {
                summary += "halfOpen4: unclassified exception\n";
            }
            summary += "original20Passed=" + juce::String(int(originalPassed)) +
                       " halfOpen4Passed=" + juce::String(int(boundaryPassed)) +
                       "\nExperimental generic graph MIDI diagnostic only; production opt-in stays OFF. "
                       "No realtime heap/deadline, device, native input or listening qualification.\n";
            require(folder.getChildFile("retained-midi-runner.txt").replaceWithText(summary),
                    "Retain both diagnostic outcomes before aggregate assertion");
            require(originalPassed && boundaryPassed,
                    "Retained MIDI diagnostic failed; inspect both preserved receipts");
            if (juce::String(argv[1]) == "--bounded-midi-check")
            {
                midiCompensationHistoryTests();
                vibera::draft::bounded_midi_graph::admissionTests(folder);
                vibera::draft::bounded_midi_graph::callbackTests(folder);
            }
            std::cout << "PASS retained MIDI original20 plus independent half-open4 cases\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--test-pacing-check")
        {
            const juce::File folder(argv[2]);
            require(juce::File::isAbsolutePath(argv[2]) && folder.createDirectory().wasOk(),
                    "test pacing output directory");
            testDispatchPacingTests(folder);
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--browser-search-check")
        {
            require(juce::File::isAbsolutePath(argv[2]), "Browser output must be absolute");
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "Browser test output");
            browserSearchTests(folder);
            std::cout << "PASS Browser search, " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--write-demo")
        {
            auto result = saveProject(Project::demo(), juce::File(argv[2]));
            if (result.failed())
                throw std::runtime_error(result.getErrorMessage().toStdString());
            return 0;
        }
        if (argc == 3 && (juce::String(argv[1]) == "--sampler-preview-check" ||
                          juce::String(argv[1]) == "--sampler-live-check" ||
                          juce::String(argv[1]) == "--audio-clip-check"))
        {
            const auto folder = juce::File(argv[2]);
            require(folder.createDirectory().wasOk(), "isolated musical output directory");
            if (juce::String(argv[1]) == "--sampler-preview-check")
                samplerPreviewTests(folder);
            else if (juce::String(argv[1]) == "--sampler-live-check")
                samplerLiveTests(folder);
            else
                audioClipTests(folder);
            std::cout << "PASS isolated musical check, " << assertions << " assertions\n";
            return 0;
        }
        if (argc >= 4 && juce::String(argv[1]) == "--render")
        {
            Project p;
            auto load = loadProject(juce::File(argv[2]), p);
            if (load.failed())
                throw std::runtime_error(load.getErrorMessage().toStdString());
            AudioEngine engine(false);
            double rate = 48000;
            int bits = 24;
            for (int i = 4; i < argc; i += 2)
            {
                if (i + 1 >= argc || !juce::String(argv[i + 1]).containsOnly("0123456789"))
                    throw std::runtime_error("Expected numeric render option value");
                if (juce::String(argv[i]) == "--sample-rate")
                    rate = juce::String(argv[i + 1]).getDoubleValue();
                else if (juce::String(argv[i]) == "--bit-depth")
                    bits = juce::String(argv[i + 1]).getIntValue();
                else
                    throw std::runtime_error("Unknown render option");
            }
            auto result = engine.renderSync(p, juce::File(argv[3]), rate, bits);
            if (result.failed())
                throw std::runtime_error(result.getErrorMessage().toStdString());
            std::cout << "Rendered " << argv[3] << "\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--plugin-bypass-check")
        {
            pluginBypassTransitionTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS " << assertions << " Plugin bypass assertions\n";
            return 0;
        }
        if (argc >= 2 && juce::String(argv[1]) == "--history-destination-check")
        {
            require(argc == 3, "History destination check requires one owned output directory");
            require(juce::File::isAbsolutePath(argv[2]), "History destination output must be absolute");
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "Owned history destination output directory");
            historyDestinationTests(folder);
            std::cout << "PASS read-only history destination admission " << assertions << " assertions\n";
            return 0;
        }
        if (argc >= 2 && juce::String(argv[1]) == "--controller-prepared-check")
        {
            require(argc == 3, "Prepared Controller check requires one owned output directory");
            require(juce::File::isAbsolutePath(argv[2]), "Prepared Controller output must be absolute");
            const auto root = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory()
                                  .getParentDirectory()
                                  .getParentDirectory();
            const juce::File output(argv[2]);
            require(root.getFileName() == "windows" && output.isAChildOf(root), "Owned build/windows output");
            const auto attempt = output.getChildFile("attempt-" + juce::Uuid().toString());
            require(attempt.createDirectory().wasOk(), "Create unique prepared Controller output");
            controller_prepared_acceptance::tests(attempt);
            std::cout << "PASS prepared Controller " << assertions << " assertions\n";
            return 0;
        }
        if (argc >= 2 && juce::String(argv[1]) == "--ready-graph-check")
        {
            require(argc == 3, "Ready graph check requires one owned output directory");
            require(juce::File::isAbsolutePath(argv[2]), "Ready graph output must be absolute");
            const auto root = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory()
                                  .getParentDirectory()
                                  .getParentDirectory();
            const juce::File output(argv[2]);
            require(root.getFileName() == "windows" && output.isAChildOf(root), "Owned build/windows output");
            const auto attempt = output.getChildFile("attempt-" + juce::Uuid().toString());
            require(attempt.createDirectory().wasOk(), "Create unique ready graph output");
            readyGraphTests(attempt);
            std::cout << "PASS ready graph " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--graph-publication-check")
        {
            graphPublicationTests(juce::File(argv[2]));
            std::cout << "PASS graph publication " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && (juce::String(argv[1]) == "--dynamic-latency-check" ||
                          juce::String(argv[1]) == "--dynamic-latency-phase-check" ||
                          juce::String(argv[1]) == "--default-dynamic-latency-check"))
        {
            const bool phaseDiagnostic = juce::String(argv[1]) == "--dynamic-latency-phase-check";
            if (juce::String(argv[1]) == "--default-dynamic-latency-check")
            {
                pluginDefaultDynamicLatencyTests(juce::File(argv[2]), juce::File(argv[3]));
                std::cout << "PASS default AudioEngine dynamic latency " << assertions << " assertions\n";
                return 0;
            }
            pluginDynamicLatencyTests(juce::File(argv[2]), juce::File(argv[3]), phaseDiagnostic);
            std::cout << (phaseDiagnostic ? "PASS diagnostic instrumented live dynamic latency "
                                          : "PASS live dynamic latency ")
                      << assertions << " assertions\n";
            return 0;
        }
        if (argc == 5 && juce::String(argv[1]) == "--dynamic-waveform-check")
        {
            pluginDynamicWaveformTests(juce::File(argv[2]), juce::File(argv[3]), juce::File(argv[4]));
            std::cout << "PASS variable waveform latency " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--stopped-effect-command-prepare-check")
        {
            const auto result = vibera::acceptance::stoppedEffectCommand::verify();
            require(result.wasOk(), result.getErrorMessage().toRawUTF8());
            std::cout << "PASS canonical stopped-effect command/history preparation\n";
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--plugin-editor-requests-check")
        {
            pluginEditorRequestsTests();
            pluginEditorProtocolAcceptance();
            pluginWrapperProtocolTests();
            pluginWrapperViewTests();
            std::cout << "PASS native editor request ownership " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--plugin-state-admission-check")
        {
            pluginStateAdmissionTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS plugin state admission " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--plugin-editor-attachment-check")
        {
            pluginNativeEditorAttachmentTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS native editor attachment " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 5 && juce::String(argv[1]) == "--plugin-editor-deadline-check")
        {
            const auto spec = parameterSpec(juce::File(argv[3]));
            require(spec.name == "VibeRa Parameter Fixture",
                    "Deadline uses the genuine original native editor fixture");
            const auto mode = juce::String(argv[4]);
            require(mode == "timely" || mode == "late" || mode == "timeout",
                    "Explicit editor deadline scenario");
            struct Mode
            {
                juce::String previous =
                    juce::SystemStats::getEnvironmentVariable("VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", {});
                explicit Mode(bool timely)
                {
                    require(_wputenv_s(L"VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", timely ? L"200" : L"5500") ==
                                0,
                            "Set scoped original editor native-work delay");
                }
                ~Mode()
                {
                    _wputenv_s(L"VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", previous.toWideCharPointer());
                }
            } fixtureMode(mode == "timely");
            pluginEditorHostDeadlineAcceptance(spec,
                                               mode == "timely" ? PluginEditorDeadlineCase::timelyLatePolling
                                               : mode == "late"
                                                   ? PluginEditorDeadlineCase::genuinelyLate
                                                   : PluginEditorDeadlineCase::timeoutThenLateReply,
                                               juce::File(argv[2]));
            const auto folder = juce::File(argv[2]);
            require(folder.createDirectory().wasOk() &&
                        folder.getChildFile("native-editor-deadline.txt")
                            .replaceWithText("PASS original actual native attachment delay case=" + mode +
                                             "; strict5000ms admission/completion;200ms timely or5500ms "
                                             "late; no listening/hardware/native-input claim\n"),
                    "Retain actual deadline outcome");
            std::cout << "PASS native editor deadline " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--sidechain-graph-check")
        {
            pluginSidechainGraphTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS " << assertions << " Sidechain graph assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--sidechain-transport-check")
        {
            pluginSidechainTransportTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS " << assertions << " Sidechain transport assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--mixer-risk-check")
        {
            mixerRiskTests(juce::File(argv[2]), juce::File(argv[3]));
            std::cout << "PASS " << assertions << " Mixer risk assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--mixer-controls-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Mixer control output");
            mixerControlTests(output);
            mixerControlLiveTests(output);
            std::cout << "PASS Mixer controls " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--native-scalar-core-check")
        {
            pluginScalarHistorySupportTests();
            const auto result = vibera::acceptance::nativeScalarCommand::verify();
            require(result.wasOk(), result.getErrorMessage().toStdString());
            std::cout << "PASS supported native scalar transport/state/Controller history tests\n";
            return 0;
        }
        // Proposed Acceptance.cpp integration only; root owns the actual dispatcher.
        // Include NoteReleaseWorkerAcceptance.h after require is declared.
        if (argc >= 2 && juce::String(argv[1]).startsWith("--note-release-worker"))
        {
            require(argc == 4 && juce::String(argv[1]) == "--note-release-worker-check",
                    "Release worker requires exact flag, owned output and explicit original fixture");
            juce::File attempt;
            const auto admitted = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            require(juce::File::isAbsolutePath(argv[3]) && juce::File(argv[3]).existsAsFile(),
                    "Release fixture must be an existing absolute native input");
            const auto receipt = attempt.getChildFile("verification.json");
            require(receipt.replaceWithText("{\"passed\":false,\"phase\":\"started\"}\n"),
                    "Retain release attempt before actual scan/worker work");
            try
            {
                noteReleaseWorkerTests(attempt, parameterSpec(juce::File(argv[3])));
                require(
                    receipt.replaceWithText(
                        "{\"passed\":true,\"scope\":\"Original VST3 fixture, score preparation, LiveMidi, "
                        "ownership and offline worker release-byte PCM; no arbitrary installed-plugin, "
                        "Synth/Sampler, live device timing or listening qualification\"}\n"),
                    "Retain actual release worker terminal result");
            }
            catch (const std::exception &error)
            {
                auto failure = std::make_unique<juce::DynamicObject>();
                failure->setProperty("passed", false);
                failure->setProperty("failure", juce::String(error.what()));
                receipt.replaceWithText(juce::JSON::toString(juce::var(failure.release())));
                throw;
            }
            std::cout << "PASS original VST3 release-velocity worker proof\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--step-compose-check")
        {
            juce::File attempt;
            const auto admitted = test::prepareBuildAcceptanceAttempt(juce::String(argv[2]), attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            stepComposeTests();
            stepComposeCanonicalTests();
            stepPitchTests();
            stepExpressionModelTests(attempt);
            stepShiftTests(attempt);
            stepShiftAudioTests();
            stepRepeatTests(attempt);
            stepRepeatAudioTests();
            stepReleaseTests(attempt);
            stepReleaseAudioTests(attempt);
            liveMidiFrameOrderTests();
            noteReleaseMidiTests(attempt);
            noteReleaseScoreLogTests(attempt);
            samplerNoteExpressionTests();
            stepActivationCanonicalTests(attempt);
            stepMemoryControllerTests();
            stepMemoryProfileTests(attempt);
            std::cout << "PASS Advanced Fill " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--pattern-extent-check")
        {
            juce::File attempt;
            const auto admitted = test::prepareBuildAcceptanceAttempt(juce::String(argv[2]), attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            patternExtentTests(attempt);
            std::cout << "PASS Pattern extent/meter " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--channel-loop-check")
        {
            juce::File attempt;
            const auto admitted = test::prepareBuildAcceptanceAttempt(juce::String(argv[2]), attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            runChannelLoopAcceptance(attempt);
            std::cout << "PASS Pattern Channel loop " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--automation-source-clone-check")
        {
            juce::File attempt;
            const auto admitted = test::prepareBuildAcceptanceAttempt(juce::String(argv[2]), attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            automationSourceCloneTests(attempt);
            std::cout << "PASS independent Automation source clone " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--automation-touch-core-check")
        {
            juce::File attempt;
            const auto admitted = test::prepareBuildAcceptanceAttempt(juce::String(argv[2]), attempt);
            require(admitted.wasOk(), admitted.getErrorMessage().toStdString());
            const auto result = test::automation_touch::verify(attempt);
            require(result.wasOk(), result.getErrorMessage().toStdString());
            std::cout << "PASS original Mixer automation take model/history/persistence tests\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--mixer-routing-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Mixer routing output");
            mixerRoutingTests(output);
            const auto detectorModel = vibera::acceptance::mixerDetectorModel::verify();
            require(detectorModel.wasOk(), detectorModel.getErrorMessage().toStdString());
            mixerRoutingLiveTests(output);
            std::cout << "PASS Mixer routing " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--note-stretch-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Stretch output");
            noteStretchTests(output);
            noteStretchLiveTests(output);
            std::cout << "PASS Note Stretch " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--note-slice-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Slice output");
            noteSliceTests(output);
            noteSliceLiveTests(output);
            std::cout << "PASS Note Slice " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && (juce::String(argv[1]) == "--mixer-routing-provider-check" ||
                          juce::String(argv[1]) == "--mixer-routing-phase-provider-check"))
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual Mixer VST3 fixture");
            const auto instrument = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                                 [](const auto &c) { return bool(c.instrument); });
            require(instrument != fixture.channels.end(), "Installed Mixer instrument present");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Mixer provider output");
            mixerRoutingLiveTests(output, &*instrument->instrument,
                                  juce::String(argv[1]) == "--mixer-routing-phase-provider-check");
            mixerControlLiveTests(output, &*instrument->instrument);
            std::cout << (juce::String(argv[1]) == "--mixer-routing-phase-provider-check"
                              ? "PASS diagnostic instrumented hosted Mixer routing "
                              : "PASS hosted Mixer routing ")
                      << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--note-stretch-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual Stretch VST3 fixture");
            const auto instrument = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                                 [](const auto &c) { return bool(c.instrument); });
            require(instrument != fixture.channels.end(), "Installed Stretch instrument present");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Stretch provider output");
            noteStretchLiveTests(output, &*instrument->instrument);
            std::cout << "PASS hosted Note Stretch " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--note-slice-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual Slice VST3 fixture");
            const auto instrument = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                                 [](const auto &c) { return bool(c.instrument); });
            require(instrument != fixture.channels.end(), "Installed Slice instrument present");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Slice provider output");
            noteSliceLiveTests(output, &*instrument->instrument);
            std::cout << "PASS hosted Note Slice " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--note-mute-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Note mute output");
            noteMuteTests(output);
            noteMuteLiveTests(output);
            std::cout << "PASS Note mute " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--note-mute-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual mute VST3 fixture");
            const auto plugin = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                             [](const auto &c) { return bool(c.instrument); });
            require(plugin != fixture.channels.end(), "Installed instrument for mute");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Note mute provider output");
            noteMuteLiveTests(output, &*plugin->instrument);
            std::cout << "PASS hosted Note mute " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--clip-groups-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Clip group output");
            clipGroupTests(output);
            clipGroupsLiveTests(output);
            std::cout << "PASS Clip groups " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--clip-groups-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual grouped VST3 fixture");
            const auto plugin = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                             [](const auto &channel) { return bool(channel.instrument); });
            require(plugin != fixture.channels.end(), "Installed instrument present");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Grouped provider output");
            clipGroupsLiveTests(output, &*plugin->instrument);
            std::cout << "PASS hosted Clip groups " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-modulation-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned sampler modulation output");
            samplerModulationModelTests(output);
            samplerModulationAudioTests();
            std::cout << "PASS sampler modulation " << assertions << " assertions\n";
            return 0;
        }
        if ((argc == 3 && juce::String(argv[1]) == "--sampler-note-modes-check") ||
            (argc == 4 && juce::String(argv[1]) == "--sampler-note-modes-provider-check"))
        {
            Project fixture;
            const bool provider = argc == 4;
            if (provider)
                require(juce::File::isAbsolutePath(argv[2]) &&
                            test::ordinaryAcceptanceInput(juce::File(argv[2])) &&
                            loadProject(juce::File(argv[2]), fixture).wasOk(),
                        "Genuine absolute note-mode provider fixture");
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[provider ? 3 : 2], output).wasOk(),
                    "Owned original note modes output");
            samplerNoteModesModelTests(output);
            sampler_note_modes_audio_acceptance::tests();
            sampler_note_modes_midi_acceptance::tests();
            samplerVoiceModesLiveTests(output, provider ? &fixture : nullptr, true);
            std::cout << "PASS original note modes/model/PCM/MIDI/live " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--ai-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned AI output");
            aiModelTests(output);
            std::cout << "PASS AI model " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--slicer-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned slicer output");
            slicer_acceptance::tests(output);
            std::cout << "PASS slicer " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--song-meter-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned song meter output");
            song_meter_acceptance::tests(output);
            std::cout << "PASS song_meter " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-pitch-bend-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned pitch bend output");
            sampler_pitch_bend_acceptance::tests(output);
            std::cout << "PASS sampler_pitch_bend " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--missing-media-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned missing media output");
            missing_media_acceptance::tests(output);
            std::cout << "PASS missing_media " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--midi-import-extras-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned MIDI import output");
            midi_import_extras_acceptance::tests(output);
            std::cout << "PASS midi_import_extras " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--playlist-tracks-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned playlist track output");
            playlist_track_acceptance::tests(output);
            std::cout << "PASS playlist_tracks " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--arrangements-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned arrangement output");
            arrangement_acceptance::tests(output);
            std::cout << "PASS arrangements " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-stretch-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned sampler stretch output");
            sampler_stretch_acceptance::tests(output);
            std::cout << "PASS sampler stretch " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--updater-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned updater output");
            updater_acceptance::tests(output);
            std::cout << "PASS updater " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--audio-stretch-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned stretch output");
            audio_stretch_acceptance::tests(output);
            std::cout << "PASS audio stretch " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--channel-tuning-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned tuning output");
            channel_tuning_acceptance::tests(output);
            std::cout << "PASS channel tuning " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--controller-recording-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned controller output");
            controller_recording_acceptance::tests(output);
            std::cout << "PASS controller recording " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--chord-progression-check")
        {
            chord_progression_acceptance::tests();
            std::cout << "PASS chord progression " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--flp-import-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned FLP output");
            flp_import_acceptance::tests(output);
            std::cout << "PASS flp import " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--tempo-map-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned tempo map output");
            tempo_map_acceptance::tests(output);
            std::cout << "PASS tempo map " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--playlist-markers-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned marker output");
            playlist_marker_acceptance::tests(output);
            std::cout << "PASS playlist markers " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--composition-tools-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned composition tools output");
            composition_tools_acceptance::tests(output);
            std::cout << "PASS composition tools " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--audio-capture-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned audio capture output");
            audio_capture_acceptance::tests(output);
            audio_comp_acceptance::tests(output);
            std::cout << "PASS audio capture and comping " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-fade-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(), "Owned sampler fade output");
            sampler_fade_acceptance::tests(output);
            std::cout << "PASS sampler fades " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--pattern-management-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned Pattern management output");
            patternManagementModelTests(output);
            std::cout << "PASS Pattern management " << assertions << " assertions" << std::endl;
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--channel-preset-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned Channel preset output");
            channelPresetModelTests(output);
            std::cout << "PASS Channel presets " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-voice-modes-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned sampler voice modes output");
            samplerVoiceModeModelTests(output);
            samplerMonoPortamentoTests();
            samplerVoiceModesLiveTests(output);
            std::cout << "PASS original sampler voice model/runtime " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--sampler-voice-modes-provider-check")
        {
            Project fixture;
            require(juce::File::isAbsolutePath(argv[2]) && test::ordinaryAcceptanceInput(juce::File(argv[2])),
                    "Ordinary absolute installed voice-mode fixture");
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual hosted voice-mode project");
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[3], output).wasOk(),
                    "Owned installed sampler voice-mode output");
            samplerVoiceModeModelTests(output);
            samplerMonoPortamentoTests();
            samplerVoiceModesLiveTests(output, &fixture);
            std::cout << "PASS installed-provider sampler voice model/live " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-controller-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned sampler controller output");
            samplerControllerLinksTests(output);
            const auto runtime = test::sampler_controller_runtime::verify(output.getChildFile("runtime"));
            require(runtime.wasOk(), runtime.getErrorMessage().toRawUTF8());
            for (const auto *command :
                 {"channel.sampler.controller.create", "channel.sampler.controller.update",
                  "channel.sampler.controller.remove"})
                require(!Customization::macroAllowed(command),
                        "Project link configuration cannot leak into untyped global macros");
            std::cout << "PASS project sampler controller model/runtime " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-envelope-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned sampler envelope output");
            samplerEnvelopeModelTests(output);
            sampler_envelope_audio_acceptance::tests(output);
            samplerEnvelopeVoiceTests(output);
            sampler_modulation_automation_acceptance::tests(output);
            samplerAutomationMacroTests(output);
            samplerAutomationParameterTests();
            samplerModulationAutomationModelTests(output);
            sampler_lfo_phase_block_acceptance::tests(output);
            sampler_automation_event_reference_acceptance::tests(output);
            std::cout << "PASS sampler envelope " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-filter-check")
        {
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[2], output).wasOk(),
                    "Owned sampler filter output");
            samplerFilterModelTests(output);
            samplerFilterAudioTests();
            samplerFilterLiveTests(output);
            std::cout << "PASS sampler filter " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--sampler-filter-provider-check")
        {
            Project fixture;
            require(juce::File::isAbsolutePath(argv[2]) && test::ordinaryAcceptanceInput(juce::File(argv[2])),
                    "Ordinary absolute installed filter fixture");
            require(loadProject(juce::File(argv[2]), fixture).wasOk(),
                    "Actual sampler filter hosted fixture");
            const auto source = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                             [](const auto &c) { return bool(c.instrument); });
            require(source != fixture.channels.end(), "Actual VST3 beside filtered sampler");
            juce::File output;
            require(test::prepareBuildAcceptanceAttempt(argv[3], output).wasOk(),
                    "Owned hosted filter output");
            samplerFilterLiveTests(output, &*source->instrument);
            std::cout << "PASS hosted sampler filter " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--normalize-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Normalize output");
            samplerNormalizeTests(output);
            samplerNormalizeLiveTests(output);
            samplerTests(output);
            samplerVoiceTests(output);
            std::cout << "PASS Normalize " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--normalize-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual Normalize hosted fixture");
            const auto source = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                             [](const auto &c) { return bool(c.instrument); });
            require(source != fixture.channels.end(), "Actual VST3 beside normalized sampler");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Hosted Normalize output");
            samplerNormalizeLiveTests(output, &*source->instrument);
            std::cout << "PASS hosted Normalize " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--loop-bounds-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Loop bounds output");
            loopBoundsTests(output);
            loopBoundsLiveTests(output);
            loopBoundsConcurrentTests(output);
            std::cout << "PASS loop bounds " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--loop-bounds-provider-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "Actual hosted loop fixture");
            const auto source = std::find_if(fixture.channels.begin(), fixture.channels.end(),
                                             [](const auto &c) { return bool(c.instrument); });
            require(source != fixture.channels.end(), "Actual VST3 loop instrument");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "Hosted loop output");
            loopBoundsLiveTests(output, &*source->instrument);
            std::cout << "PASS hosted loop bounds " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--sampler-voices-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Sampler voice output");
            samplerVoiceTests(output);
            samplerVoicesLiveTests(output);
            std::cout << "PASS sampler voices " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--quantizer-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Quantizer output");
            quantizerTests(output);
            grooveTests(output);
            channelSoloLiveTests(output, nullptr, false, false, false, true);
            std::cout << "PASS Quantizer " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--swing-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Swing output");
            swingTests(output);
            channelSoloLiveTests(output, nullptr, false, false, true);
            std::cout << "PASS Swing " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--step-velocity-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Step velocity output");
            stepVelocityTests(output);
            channelSoloLiveTests(output, nullptr, false, true);
            std::cout << "PASS Step velocity " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--channel-management-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Channel management output");
            channelManagementTests(output);
            channelAppearanceTests(output);
            channelSoloLiveTests(output, nullptr, true);
            std::cout << "PASS Channel management " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--channel-solo-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Solo output");
            channelSoloTests(output);
            channelScopedSoloTests(output);
            midiNoteOwnerTests();
            channelSoloLiveTests(output);
            channelSoloLiveTests(output, nullptr, false, false, false, false, true);
            std::cout << "PASS Channel solo " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--step-fill-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Step fill output");
            stepFillTests(output);
            rackGroupsLiveTests(output, nullptr, true);
            std::cout << "PASS Step fill " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--rack-groups-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "Rack output");
            rackGroupTests(output);
            rackGroupsLiveTests(output);
            std::cout << "PASS Rack groups " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 4 && (juce::String(argv[1]) == "--score-tools-provider-check" ||
                          juce::String(argv[1]) == "--rack-groups-provider-check" ||
                          juce::String(argv[1]) == "--step-fill-provider-check" ||
                          juce::String(argv[1]) == "--channel-solo-provider-check" ||
                          juce::String(argv[1]) == "--channel-management-provider-check" ||
                          juce::String(argv[1]) == "--step-velocity-provider-check" ||
                          juce::String(argv[1]) == "--swing-provider-check" ||
                          juce::String(argv[1]) == "--quantizer-provider-check"))
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "score tools provider fixture");
            const auto instrument =
                std::find_if(fixture.channels.begin(), fixture.channels.end(),
                             [](const auto &channel) { return channel.instrument.has_value(); });
            require(instrument != fixture.channels.end(), "Actual score tool instrument required");
            const juce::File output(argv[3]);
            require(output.createDirectory().wasOk(), "provider score tools output");
            if (juce::String(argv[1]) == "--quantizer-provider-check")
                channelSoloLiveTests(output, &*instrument->instrument, false, false, false, true);
            else if (juce::String(argv[1]) == "--swing-provider-check")
                channelSoloLiveTests(output, &*instrument->instrument, false, false, true);
            else if (juce::String(argv[1]) == "--step-velocity-provider-check")
                channelSoloLiveTests(output, &*instrument->instrument, false, true);
            else if (juce::String(argv[1]) == "--channel-management-provider-check")
                channelSoloLiveTests(output, &*instrument->instrument, true);
            else if (juce::String(argv[1]) == "--channel-solo-provider-check")
            {
                channelSoloLiveTests(output, &*instrument->instrument);
                channelSoloLiveTests(output, &*instrument->instrument, false, false, false, false, true);
            }
            else if (juce::String(argv[1]) == "--step-fill-provider-check")
                rackGroupsLiveTests(output, &*instrument->instrument, true);
            else if (juce::String(argv[1]) == "--rack-groups-provider-check")
                rackGroupsLiveTests(output, &*instrument->instrument);
            else
                scoreToolsLiveTests(output, &*instrument->instrument);
            std::cout << "PASS provider score tools " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--score-tools-check")
        {
            const juce::File output(argv[2]);
            require(output.createDirectory().wasOk(), "score tools output");
            scoreToolsTests(output);
            noteGlueTests(output);
            scoreToolsLiveTests(output);
            std::cout << "PASS score tools " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--guide-phase-check")
        {
            auto output = juce::File(argv[2]);
            output.createDirectory();
            for (int delay : {0, 5, 20, 35, 50, 80, 150, 250})
            {
                auto folder = output.getChildFile(juce::String(delay));
                folder.createDirectory();
                guideIntegrationTests(folder, nullptr, delay);
            }
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--guide-check")
        {
            auto output = juce::File(argv[2]);
            output.createDirectory();
            guideDspTests(output);
            guideIntegrationTests(output);
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--live-edit-check")
        {
            auto output = juce::File(argv[2]);
            output.createDirectory();
            callbackProjectionTests(output, true);
            callbackProjectionTests(output, false);
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--automation-hosted-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "automation hosted fixture load");
            std::vector<PluginSpec> specs;
            for (const auto &channel : fixture.channels)
                if (channel.instrument && specs.empty())
                    specs.push_back(*channel.instrument);
            for (const auto &insert : fixture.mixer)
                for (const auto &fx : insert.effects)
                    if (specs.size() == 1)
                        specs.push_back(fx);
            require(specs.size() == 2, "automation installed instrument and effect fixture");
            const auto output = juce::File(argv[3]);
            require(output.createDirectory().wasOk(), "automation hosted output");
            callbackProjectionTests(output, true, &specs, true);
            callbackProjectionTests(output, false, &specs, true);
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--sample-preview-project-check")
        {
            Project fixture;
            require(loadProject(juce::File(argv[2]), fixture).wasOk(), "preview provider fixture");
            const auto instrument =
                std::find_if(fixture.channels.begin(), fixture.channels.end(),
                             [](const auto &channel) { return channel.instrument.has_value(); });
            require(instrument != fixture.channels.end(), "preview actual instrument available");
            const auto output = juce::File(argv[3]);
            require(output.createDirectory().wasOk(), "preview provider output");
            samplePreviewTests(output, &*instrument->instrument);
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--midi-import-project-check")
        {
            Project project;
            require(loadProject(juce::File(argv[2]), project).wasOk(), "MIDI import provider project load");
            const auto instrument =
                std::find_if(project.channels.begin(), project.channels.end(),
                             [](const auto &channel) { return channel.instrument.has_value(); });
            require(instrument != project.channels.end(),
                    "MIDI import actual instrument descriptor/state available");
            const auto output = juce::File(argv[3]);
            require(output.createDirectory().wasOk(), "MIDI import output directory");
            midiScoreLiveTests(output, &*instrument->instrument);
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--schema-notification-check")
        {
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "schema notification output");
            pluginSchemaNotificationTests(folder, juce::File(argv[3]));
            return 0;
        }
        if (argc == 4 && (juce::String(argv[1]) == "--parameter-check" ||
                          juce::String(argv[1]) == "--parameter-provider-check"))
        {
            isolatedParameterTests(juce::File(argv[2]), juce::File(argv[3]),
                                   juce::String(argv[1]) == "--parameter-check");
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--scanner-check")
        {
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "scanner check output directory");
            scannerContainmentTests(folder);
            std::cout << "PASS scanner containment " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 5 && juce::String(argv[1]) == "--plugin-check")
        {
            // --plugin-check output-directory instrument-path effect-path
            actualPluginTests(juce::File(argv[2]), juce::File(argv[3]), juce::File(argv[4]));
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--render-check")
        {
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "render-only output directory");
            renderTests(folder);
            std::cout << "PASS render-only " << assertions << " assertions\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--callback-timing-check")
        {
            const juce::File folder(argv[2]);
            require(folder.createDirectory().wasOk(), "callback timing output directory");
            callbackTimingTests(folder);
            std::cout << "PASS callback timing attribution " << assertions << " assertions\n";
            return 0;
        }
        if (argc > 2 || (argc == 2 && juce::String(argv[1]).startsWith("--")))
            throw std::runtime_error("Unknown test option or wrong argument count");
        auto folder = argc > 1 ? juce::File(argv[1])
                               : juce::File::getCurrentWorkingDirectory().getChildFile("acceptance");
        require(folder.createDirectory().wasOk(), "test output directory");
        const auto stageLog = folder.getChildFile("stage-timings.jsonl");
        require(stageLog.replaceWithText(""), "truncate stage diagnostic log");
        auto processCpuMs = []
        {
#if JUCE_WINDOWS
            FILETIME creation, exit, kernel, user;
            if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
            {
                ULARGE_INTEGER k{}, u{};
                k.LowPart = kernel.dwLowDateTime;
                k.HighPart = kernel.dwHighDateTime;
                u.LowPart = user.dwLowDateTime;
                u.HighPart = user.dwHighDateTime;
                return (k.QuadPart + u.QuadPart) / 10000.;
            }
#endif
            return 0.;
        };
        auto runStage = [&](const char *name, auto operation)
        {
            const auto began = juce::Time::getMillisecondCounterHiRes();
            const auto cpu = processCpuMs();
            const auto before = testDispatchStatistics();
            auto write = [&](const char *state)
            {
                const auto after = testDispatchStatistics();
                auto *record = new juce::DynamicObject;
                record->setProperty("stage", name);
                record->setProperty("state", state);
                record->setProperty("wallMs", juce::Time::getMillisecondCounterHiRes() - began);
                record->setProperty("processCpuMs", processCpuMs() - cpu);
                record->setProperty("dispatchCalls", juce::int64(after.calls - before.calls));
                record->setProperty("dispatchRequestedMs", after.requestedMs - before.requestedMs);
                record->setProperty("dispatchTotalMs", after.totalMs - before.totalMs);
                record->setProperty("timerWaitMs", after.waitMs - before.waitMs);
                record->setProperty("timerWaits", juce::int64(after.waits - before.waits));
                record->setProperty("timerLongWaits", juce::int64(after.longWaits - before.longWaits));
                record->setProperty("maximumTimerWaitMs", after.maxWaitMs);
                record->setProperty("messageDispatchMs", after.messageMs - before.messageMs);
                record->setProperty("messages", juce::int64(after.messages - before.messages));
                record->setProperty("scope", "Test/control thread diagnostics; physical callback and "
                                             "operation budgets unchanged; aggregate harness budget is defined by CTest");
                const auto json = juce::JSON::toString(juce::var(record), true);
                require(stageLog.appendText(json + "\n"), "write stage diagnostic");
            };
            std::cout << "STAGE " << name << std::endl;
            write("began");
            operation();
            write("completed");
        };
        runStage("testDispatchPacingTests", [&] { testDispatchPacingTests(folder); });
        runStage("callbackTimingTests", [&] { callbackTimingTests(folder); });
        runStage("modelTests", [&] { modelTests(folder); });
        runStage("pluginBindingTests", [&] { pluginBindingTests(folder); });
        runStage("pluginGestureCoreTests", [&] { pluginGestureCoreTests(); });
        runStage("buildAcceptanceOutputTests", [&] { buildAcceptanceOutputTests(); });
        runStage("pluginControlRequestsTests", [&] { pluginControlRequestsTests(); });
        runStage("pluginScalarHistorySupportTests", [&] { pluginScalarHistorySupportTests(); });
        runStage("nativeScalarCommandTests", [&]
        {
            const auto result = vibera::acceptance::nativeScalarCommand::verify();
            require(result.wasOk(), result.getErrorMessage().toStdString());
        });
        runStage("pluginEditorRequestsTests", [&] { pluginEditorRequestsTests(); });
        runStage("pluginEditorProtocolAcceptance", [&] { pluginEditorProtocolAcceptance(); });
        runStage("pluginWrapperProtocolTests", [&] { pluginWrapperProtocolTests(); });
        runStage("pluginWrapperViewTests", [&] { pluginWrapperViewTests(); });
        runStage("automationProjectTests", [&] { automationProjectTests(folder); });
        runStage("ownedGestureTests", [&] { ownedGestureTests(); });
        runStage("automationPlaybackTests", [&] { automationPlaybackTests(folder); });
        runStage("tilingTests", [&] { tilingTests(); });
        runStage("controllerMappingTests", [&] { controllerMappingTests(); });
        runStage("patternManagementTests", [&] { patternManagementTests(folder); });
        runStage("samplerTests", [&] { samplerTests(folder); });
        runStage("mixerRoutingTests",
                 [&]
                 {
                     mixerRoutingTests(folder);
                     mixerRoutingLiveTests(folder);
                 });
        runStage("noteStretchTests",
                 [&]
                 {
                     noteStretchTests(folder);
                     noteStretchLiveTests(folder);
                 });
        runStage("noteSliceTests",
                 [&]
                 {
                     noteSliceTests(folder);
                     noteSliceLiveTests(folder);
                 });
        runStage("noteMuteTests",
                 [&]
                 {
                     noteMuteTests(folder);
                     noteMuteLiveTests(folder);
                 });
        runStage("clipGroupTests",
                 [&]
                 {
                     clipGroupTests(folder);
                     clipGroupsLiveTests(folder);
                 });
        runStage("samplerNormalizeTests",
                 [&]
                 {
                     samplerNormalizeTests(folder);
                     samplerNormalizeLiveTests(folder);
                 });
        runStage("samplerLiveTests", [&] { samplerLiveTests(folder); });
        runStage("samplerPreviewTests", [&] { samplerPreviewTests(folder); });
        runStage("precisionPersistenceTests", [&] { precisionPersistenceTests(folder); });
        runStage("scoreLogTests", [&] { scoreLogTests(folder); });
        runStage("midiScoreTests", [&] { midiScoreTests(folder); });
        runStage("midiScoreLiveTests", [&] { midiScoreLiveTests(folder); });
        runStage("scoreToolsTests", [&] { scoreToolsTests(folder); });
        runStage("noteGlueTests", [&] { noteGlueTests(folder); });
        runStage("rackGroupTests", [&] { rackGroupTests(folder); });
        runStage("rackGroupsLiveTests", [&] { rackGroupsLiveTests(folder); });
        runStage("stepFillTests", [&] { stepFillTests(folder); });
        runStage("stepFillLiveTests", [&] { rackGroupsLiveTests(folder, nullptr, true); });
        runStage("channelSoloTests", [&] { channelSoloTests(folder); });
        runStage("channelScopedSoloTests", [&] { channelScopedSoloTests(folder); });
        runStage("channelManagementTests", [&] { channelManagementTests(folder); });
        runStage("channelAppearanceTests", [&] { channelAppearanceTests(folder); });
        runStage("swingTests", [&] { swingTests(folder); });
        runStage("quantizerTests", [&] { quantizerTests(folder); });
        runStage("grooveTests", [&] { grooveTests(folder); });
        runStage("loopBoundsTests",
                 [&]
                 {
                     loopBoundsTests(folder);
                     loopBoundsLiveTests(folder);
                     loopBoundsConcurrentTests(folder);
                 });
        runStage("samplerVoiceTests", [&] { samplerVoiceTests(folder); });
        runStage("samplerVoicesLiveTests", [&] { samplerVoicesLiveTests(folder); });
        runStage("quantizerLiveTests",
                 [&] { channelSoloLiveTests(folder, nullptr, false, false, false, true); });
        runStage("swingLiveTests", [&] { channelSoloLiveTests(folder, nullptr, false, false, true); });
        runStage("stepVelocityTests", [&] { stepVelocityTests(folder); });
        runStage("stepVelocityLiveTests", [&] { channelSoloLiveTests(folder, nullptr, false, true); });
        runStage("channelReorderLiveTests", [&] { channelSoloLiveTests(folder, nullptr, true); });
        runStage("midiNoteOwnerTests", [&] { midiNoteOwnerTests(); });
        runStage("channelSoloLiveTests", [&] { channelSoloLiveTests(folder); });
        runStage("channelScopedSoloLiveTests",
                 [&] { channelSoloLiveTests(folder, nullptr, false, false, false, false, true); });
        runStage("scoreToolsLiveTests", [&] { scoreToolsLiveTests(folder); });
        runStage("samplePreviewTests", [&] { samplePreviewTests(folder); });
        runStage("audioClipTests", [&] { audioClipTests(folder); });
        runStage("waveformWorkerTests", [&] { waveformWorkerTests(folder); });
        runStage("playlistEditTests", [&] { playlistEditTests(folder); });
        runStage("typingKeyTests", [&] { typingKeyTests(); });
        runStage("guideDspTests", [&] { guideDspTests(folder); });
        runStage("guideIntegrationTests", [&] { guideIntegrationTests(folder); });
        runStage("projectionTests", [&] { projectionTests(folder); });
        runStage("callbackProjectionTests", [&] { callbackProjectionTests(folder, true); });
        runStage("callbackProjectionTests", [&] { callbackProjectionTests(folder, false); });
        runStage("scannerContainmentTests", [&] { scannerContainmentTests(folder); });
        runStage("pluginModelTests", [&] { pluginModelTests(folder); });
        runStage("midiInputTests", [&] { midiInputTests(folder); });
        runStage("midiRecordingTests", [&] { midiRecordingTests(folder); });
        runStage("midiTakeRecoveryPrimitive", [&] { midi_take_recovery_acceptance::tests(folder); });
        runStage("preparedTimelinePublication", [&] { preparedTimelineAcceptance(); });
        runStage("soundingEditTests", [&] { soundingEditTests(); });
        runStage("dspTests", [&] { dspTests(); });
        runStage("mediaTests", [&] { mediaTests(folder); });
        runStage("customizationTests", [&] { customizationTests(folder); });
        runStage("renderTests", [&] { renderTests(folder); });
        std::cout << "PASS " << assertions
                  << " assertions: sequencing, commands/undo, persistence/failure, shortcuts/macros, fixed "
                     "sampler, incremental callback projection and Tracktion WAV rendering\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL after " << assertions << " assertions: " << e.what() << "\n";
        return 1;
    }
}
