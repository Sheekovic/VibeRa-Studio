// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "../src/AutomationGain.h"
#include "../src/MixerRoutePlugins.h"
#include "../src/MixerNativeIdentity.h"
#include <array>

// TEST ONLY. Fresh, stopped, task-owned projection BEFORE its first context.
// Does not qualify routine editing, metre equivalence, plugin/source admission
// or an unchanged ordinary diagnostic. Never called by production AudioEngine.
namespace vibera::test::cold_mixer_pruning
{
struct Proof
{
    struct Row
    {
        int insert = 0, bus = 0, pluginCount = 0;
        std::uint64_t nativeId = 0;
        bool processingBefore = false, processingAfter = true;
    };
    std::array<Row, 16> rows{};
    juce::File directory;
    std::uintptr_t editIdentity = 0;
    std::size_t remainingNodes = 0;
    std::uint64_t selectedSerial = 0;
};
inline std::unique_ptr<Proof> prepare(AudioEngine &audio, const Project &project, const juce::File &folder)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    const auto diagnostics = audio.projectionDiagnostics();
    require(messages && messages->isThisTheMessageThread() && !audio.playing() && diagnostics.edit &&
                !diagnostics.playbackContext && audio.pluginDiagnostics().empty() &&
                audio.retainedPluginHostCount() == 0,
            "Cold pruning requires exact fresh stopped no-context/no-worker CONTROL projection");
    require(project.validate().wasOk() && project.channels.size() == 1 && project.mixer.size() == 18 &&
                project.channels.front().mixer == 1 && project.channels.front().source == "builtin:bass" &&
                !project.channels.front().instrument && !project.channels.front().audioClip &&
                project.audioPlacements.empty() && project.automation.sources.empty() &&
                project.automation.placements.empty() && project.automation.initialValues.empty() &&
                !project.mixerSolo && !project.solo,
            "Exact current one-source ordinary no-plugin/no-automation fixture, not four default sources");
    for (const auto &pattern : project.patterns)
        require(pattern.notes.empty(), "Cold experiment proof includes actual zero scheduled notes");
    const auto model = project.state();
    auto &edit =
        *reinterpret_cast<te::Edit *>(diagnostics.edit); // Exact task-owned diagnostic, never external input.
    require(!edit.getTransport().getCurrentPlaybackContext(),
            "No native context or graph may already have existed");
    const auto tracks = te::getAudioTracks(edit);
    require(tracks.size() == 18, "Full fixed native track universe is preserved");
    MixerNativeIdentity identity;
    require(MixerNativeIdentity::prepare(project, identity) == MixerNativeIdentity::Error::none,
            "Actual stable bus table");
    auto proof = std::make_unique<Proof>();
    proof->directory = folder;
    proof->editIdentity = diagnostics.edit;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh cold proof directory");
    std::array<te::AudioTrack *, 16> candidates{};
    // Complete validation first. No subset of native flags is touched on a
    // failed proof. These are ONLY the closed existing ordinary fixture rows.
    for (int index = 0; index < 18; ++index)
    {
        const auto *insert = mixerInsert(project, index);
        require(insert && insert->effects.empty() && !insert->effectsBypassed &&
                    (index == 0 ? insert->routes.empty() : insert->routes == std::vector<MixerRoute>{{0, 1}}),
                "Genuine default graph with no effects or non-Master sends");
    }
    for (int index = 2; index < 18; ++index)
    {
        const int slot = *identity.slot(index);
        auto *track = tracks[slot]; // One source, fixed slot0Master; Mixer slot1 is native track1.
        const int bus = *identity.bus(index);
        require(int(track->state["viberaMixerIndex"]) == index &&
                    int(track->state["viberaMixerBus"]) == bus && track->isProcessing(false) &&
                    track->getClips().isEmpty() && track->getListeners().isEmpty() &&
                    edit.getEditInputDevices().getDevicesForTargetTrack(*track).isEmpty(),
                "Cold candidate exact stable ID/bus, no clips/listener/native input target");
        require(!track->getModifierList() || track->getModifierList()->getModifiers().isEmpty(),
                "No modifier source in omitted track");
        for (const auto &channel : project.channels)
            require(channel.mixer != index, "No canonical source assignment");
        for (const auto &insert : project.mixer)
            for (const auto &route : insert.routes)
                require(route.target != index, "No incoming send target even at zero gain");
        for (auto *source : tracks)
            require(source == track || !source->getOutput().outputsToDestTrack(*track),
                    "No native direct input source");
        for (auto *plugin : edit.getPluginCache().getPlugins())
            require(plugin->getSidechainSourceID() != track->itemID,
                    "No native plugin names candidate as detector source");
        const auto plugins = track->pluginList.getPlugins();
        require(plugins.size() == 4,
                "Exact original return/fader/disabled-volume/gate list, no hidden owner");
        int returns = 0, gains = 0, volumes = 0, gates = 0;
        for (auto *plugin : plugins)
        {
            require(plugin->itemID.isValid() && plugin->baseClassNeedsInitialising(),
                    "No prior initialized DSP or queue owner");
            if (auto *value = dynamic_cast<te::AuxReturnPlugin *>(plugin))
            {
                ++returns;
                require(int(value->busNumber) == bus, "Exact native return bus");
            }
            else if (auto *value = dynamic_cast<AutomationGain *>(plugin))
            {
                ++gains;
                require(value->runtimeAttachmentsControl() == 0, "No prior fader runtime attachment");
            }
            else if (auto *value = dynamic_cast<te::VolumeAndPanPlugin *>(plugin))
            {
                ++volumes;
                require(!value->isEnabled(), "Native balance remains already disabled");
            }
            else if (auto *value = dynamic_cast<MixerMasterGate *>(plugin))
            {
                ++gates;
                require(value->runtimeAttachmentsControl() == 0, "No prior gate runtime attachment");
            }
            else
                require(false, "Unknown native processing owner forbids cold pruning");
        }
        require(returns == 1 && gains == 1 && volumes == 1 && gates == 1,
                "Unique complete original cold ownership");
        const auto meter = audio.mixerMeter(index);
        require(!meter.available && meter.blocks == 0 && !meter.overload[0] && !meter.overload[1],
                "No prior DSP meter/overload publication may be discarded");
        candidates[size_t(index - 2)] = track;
        proof->rows[size_t(index - 2)] = {index, bus, plugins.size(), track->itemID.getRawID(), true, true};
    }
    for (size_t n = 0; n < candidates.size(); ++n)
    {
        auto *track = candidates[n];
        track->setProcessing(false); // Explicit test-only native-state change BEFORE first context.
        proof->rows[n].processingAfter = track->isProcessing(false);
        require(!proof->rows[n].processingAfter, "Native cold track flag actually changed");
        for (auto *plugin : track->pluginList)
            require(!plugin->isProcessingEnabled(),
                    "Actual native property callback excluded only this cold plugin list");
    }
    require(project.state().isEquivalentTo(model) && te::getAudioTracks(edit).size() == tracks.size() &&
                audio.retainedPluginHostCount() == 0 && !edit.getTransport().getCurrentPlaybackContext(),
            "Model, full native universe and zero Host bank unchanged; still before first context");
    return proof;
}
inline void retain(Proof &proof, AudioEngine &audio)
{
    const auto current = audio.projectionDiagnostics();
    require(current.edit == proof.editIdentity && current.edit && audio.retainedPluginHostCount() == 0,
            "Cold proof retains exact native Edit and zero required Host universe");
    auto &edit = *reinterpret_cast<te::Edit *>(current.edit);
    const auto tracks = te::getAudioTracks(edit);
    require(tracks.size() == 18, "Cold graph omission cannot delete native tracks");
    juce::String rows = "insert,nativeId,bus,plugins,sourceAssignments,incomingSends,effects,clips,listeners,"
                        "inputTargets,priorDSP,processingBefore,processingAfter,meterAvailable,meterBlocks\n";
    for (const auto &row : proof.rows)
    {
        auto found = std::find_if(tracks.begin(), tracks.end(),
                                  [&](auto *track) { return track->itemID.getRawID() == row.nativeId; });
        require(found != tracks.end() && !(*found)->isProcessing(false) &&
                    int((*found)->state["viberaMixerIndex"]) == row.insert &&
                    int((*found)->state["viberaMixerBus"]) == row.bus,
                "Cold native processing/identity/bus remains actual after callbacks");
        for (auto *plugin : (*found)->pluginList)
            require(plugin->baseClassNeedsInitialising() && !plugin->isProcessingEnabled(),
                    "No cold native DSP initialized through another path");
        const auto meter = audio.mixerMeter(row.insert);
        rows += juce::String(row.insert) + "," + juce::String(juce::int64(row.nativeId)) + "," +
                juce::String(row.bus) + "," + juce::String(row.pluginCount) + ",0,0,0,0,0,0,0," +
                juce::String(int(row.processingBefore)) + "," + juce::String(int(row.processingAfter)) + "," +
                juce::String(int(meter.available)) + "," + juce::String(juce::int64(meter.blocks)) + "\n";
        require(!meter.available && meter.blocks == 0 && !meter.overload[0] && !meter.overload[1],
                "Pruned cold meters truthfully remain dormant, no synthetic callback publications");
    }
    require(proof.directory.getChildFile("tracks.csv").replaceWithText(rows) &&
                proof.directory.getChildFile("graph.txt")
                    .replaceWithText("pruned=16\nsourceChannels=1\nscheduledNotes=0\nmixerRows="
                                     "18\nnativeTracks=18\nhosts=0\nremainingNodes=" +
                                     juce::String(juce::int64(proof.remainingNodes)) +
                                     "\nselectedSerial=" + juce::String(juce::int64(proof.selectedSerial)) +
                                     "\nqualification=DERIVATIVE cold native-state experiment, not "
                                     "ordinary189-node or production editing/meters/Host qualification\n"),
            "Retain actual preconditions/cold meter state and real graph count");
}
} // namespace vibera::test::cold_mixer_pruning
