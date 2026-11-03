// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "../src/MixerRoutePlugins.h"
#include <map>

namespace initial_app_ports_acceptance
{
// Actual native graph preparation/adoption, no duplicated sampler/Host/DSP.
// Only an initialized original Gate shell is detached here; every other actual
// app source/control remains the same PluginPtr and is processed by one player.
template <class Drive>
void privateGraph(vibera::AudioEngine &audio, tracktion::Edit &edit, tracktion::EditPlaybackContext &context,
                  Drive &&drive)
{
    using namespace vibera;
    using Status = tracktion::EditPlaybackContext::ReadyGraphChangeStatus;
    const auto before = audio.initialAudioPortReadback();
    require(before.configured && before.audioBlocks > 0 && !before.terminalFault,
            "Actual complete initial app port baseline");
    tracktion::AudioTrack *activeTrack = nullptr;
    MixerMasterGate *original = nullptr;
    for (auto *track : tracktion::getAudioTracks(edit))
        if (int(track->state["viberaMixerIndex"]) == 1)
            for (auto *plugin : track->pluginList)
                if (auto *gate = dynamic_cast<MixerMasterGate *>(plugin))
                {
                    original = gate;
                    activeTrack = track;
                }
    require(original && activeTrack && !original->baseClassNeedsInitialising(),
            "Genuine active insert1 Gate baseline");
    auto *sourceTrack = tracktion::getAudioTracks(edit)[0];
    const auto midiBefore = audio.initialAudioPortReadback();
    sourceTrack->injectLiveMidiMessage(juce::MidiMessage::noteOn(3, 67, juce::uint8(100)), 17);
    drive("native_note_accounted_private_voices_unchanged", 64);
    auto native = audio.initialAudioPortReadback();
    require(native.nativeOn == midiBefore.nativeOn + 1 && native.nativeHeld == 1,
            "Actual bounded native NoteOn has one real owned terminal, no duplicate downstream delivery");
    const auto runtime = original->runtimeOwnerControl();
    const auto epoch = original->runtimeEpochControl();
    const auto attachments = original->runtimeAttachmentsControl();
    const auto make = [&]()
    {
        auto candidate = MixerMasterGate::createDetachedGraph(edit, original->state.createCopy());
        auto *gate = dynamic_cast<MixerMasterGate *>(candidate.get());
        require(gate && gate != original, "Private Gate is a real distinct native Plugin shell");
        gate->configure(original->timelineReference(), 1, 1.f);
        require(gate->shareCompatibleRuntimeFrom(*original).wasOk() && gate->runtimeOwnerControl() == runtime,
                "Private Gate retains exact original shared Runtime identity");
        auto lists = std::make_shared<std::map<const tracktion::PluginList *, tracktion::Plugin::Array>>();
        const auto add = [&](const tracktion::PluginList &list)
        {
            auto copy = list.getPlugins();
            for (int n = 0; n < copy.size(); ++n)
                if (copy[n].get() == original)
                    copy.set(n, candidate);
            lists->emplace(&list, std::move(copy));
        };
        add(edit.getMasterPluginList());
        for (auto *track : tracktion::getAudioTracks(edit))
        {
            add(track->pluginList);
            for (auto *clip : track->getClips())
                if (auto *list = clip->getPluginList())
                    add(*list);
        }
        auto overlay = std::make_shared<tracktion::EditPlaybackContext::GraphOverlay>();
        overlay->plugins = [lists](const tracktion::PluginList &list)
        {
            const auto found = lists->find(&list);
            if (found == lists->end())
                throw std::invalid_argument("Original port fixture unknown list");
            return found->second;
        };
        auto exact = std::make_shared<tracktion::Plugin::Array>(edit.getPluginCache().getPlugins());
        exact->addIfNotAlreadyThere(edit.getMasterVolumePlugin().get());
        exact->add(candidate);
        overlay->fixedInitialisationAdmission = [exact](const tracktion::Plugin &plugin)
        {
            if (!exact->contains(const_cast<tracktion::Plugin *>(&plugin)))
                throw std::invalid_argument("Original port fixture unowned plugin");
            return true;
        };
        return std::pair{overlay, candidate};
    };
    const auto oldSelection = context.getGraphSelection();
    {
        auto [overlay, candidate] = make();
        auto ready = context.prepareGraphOnly(overlay);
        require(context.validatePreparedGraph(*ready) == Status::ready && !ready->wasPublished() &&
                    context.getGraphSelection().selectedSerial == oldSelection.selectedSerial &&
                    original->runtimeAttachmentsControl() == attachments + 1 &&
                    original->runtimeEpochControl() == epoch,
                "Private actual full app graph prepares ports/retained Gate, never processes or selects");
        drive("incumbent_while_private_ports_ready", 64);
    }
    require(original->runtimeAttachmentsControl() == attachments && original->runtimeEpochControl() == epoch,
            "Discard releases candidate shell only, preserves original Runtime epoch");
    drive("after_private_ports_cancel", 65);
    sourceTrack->injectLiveMidiMessage(juce::MidiMessage::noteOff(3, 67), 17);
    drive("native_release_after_cancel", 64);
    native = audio.initialAudioPortReadback();
    require(native.nativeOff == midiBefore.nativeOff + 1 && native.nativeHeld == 0,
            "Cancel preserves queued native release and terminal ownership");
    struct Observer final : tracktion::AudioTrack::Listener
    {
        void injectLiveMidiMessage(tracktion::AudioTrack &, const tracktion::MidiMessageWithSource &,
                                   bool &) override
        {
        }
        void recordedMidiMessageSentToPlugins(tracktion::AudioTrack &, const juce::MidiMessage &) override {}
    } observer;
    bool refused = false;
    activeTrack->addListener(&observer);
    try
    {
        auto [overlay, candidate] = make();
        auto rejected = context.prepareGraphOnly(overlay);
    }
    catch (const std::invalid_argument &error)
    {
        refused = juce::String(error.what()).startsWith("appAudioPort.nativeRoute:");
    }
    activeTrack->removeListener(&observer);
    require(refused && context.getGraphSelection().selectedSerial == oldSelection.selectedSerial &&
                original->runtimeAttachmentsControl() == attachments,
            "Actual legitimate/unknown native output observer refuses before suppression/publication");
    drive("after_unknown_observer_refusal", 127);
    auto [overlay, candidate] = make();
    auto ready = context.prepareGraphOnly(overlay);
    const auto serial = ready->publicationSerial();
    require(context.publishPreparedGraph(*ready) == Status::ready && ready->wasPublished() &&
                context.getGraphSelection().selectedSerial == oldSelection.selectedSerial,
            "Accepted actual graph publication remains pending until actual callback selection");
    drive("actual_full_app_port_adoption", 64);
    require(context.getGraphSelection().selectedSerial == serial &&
                dynamic_cast<MixerMasterGate *>(candidate.get())->runtimeOwnerControl() == runtime &&
                original->runtimeEpochControl() == epoch &&
                audio.initialAudioPortReadback().nonemptyAudioMidi == 0 &&
                !audio.initialAudioPortReadback().terminalFault,
            "Actual selected detached Gate uses identical Runtime and genuine audio-only output stage");
    ready.reset(); // CONTROL. Native graph retirement pins any prior selected owner until safe.
    drive("after_app_port_candidate_retirement", 256);
    sourceTrack->injectLiveMidiMessage(juce::MidiMessage::noteOn(3, 69, juce::uint8(100)), 17);
    drive("native_note_after_actual_selection", 64);
    native = audio.initialAudioPortReadback();
    require(native.nativeOn == midiBefore.nativeOn + 2 && native.nativeHeld == 1,
            "Selected successor keeps one native ingress/listener and retained terminal ledger");
    sourceTrack->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(3), 17);
    drive("native_panic_private_voices_unchanged", 65);
    native = audio.initialAudioPortReadback();
    require(native.nativePanic == midiBefore.nativePanic + 1 && native.nativeHeld == 0 &&
                audio.projectionDiagnostics().channels[0].heldInputKeys == 2,
            "Native panic has an explicit consumer and preserves real physical/typing owners");
}
inline void captured(vibera::MidiInputRouter &input, vibera::Id channel)
{
    input.stopCapture();
    unsigned ons = 0, offs = 0;
    vibera::MidiCapture::Event event;
    while (input.capture.pop(event))
    {
        require(event.channel == channel, "Actual capture keeps canonical stable Channel target");
        if ((event.bytes[0] & 0xf0) == 0x90 && event.bytes[2])
            ++ons;
        if ((event.bytes[0] & 0xf0) == 0x80 || ((event.bytes[0] & 0xf0) == 0x90 && !event.bytes[2]))
            ++offs;
    }
    require(!input.capture.failed.load() && ons == 1 && offs == 1,
            "Actual physical/typing capture survives ports/cancel/adoption");
}
} // namespace initial_app_ports_acceptance
