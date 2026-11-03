// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppSourceTransportAcceptance.h"
#include <tracktion_engine/model/clips/tracktion_LaunchHandle.h>
namespace tracktion::inline engine
{
class DynamicallyOffsettableNodeBase;
class LoopingMidiNode;
} // namespace tracktion::inline engine
#include <tracktion_engine/playback/graph/tracktion_SlotControlNode.h>
#include <tracktion_engine/playback/graph/tracktion_CombiningNode.h>
#include <tracktion_engine/playback/graph/tracktion_ArrangerLauncherSwitchingNode.h>

// Real native classes and hidden-input ownership. CONTROL classification only;
// actual application queue/PCM checks remain independently required.
namespace empty_mixer_lineage_acceptance
{
inline void tests(const juce::File &folder)
{
    using namespace vibera;
    using namespace tracktion::graph;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned empty-lineage fixture");
    initial_app_source_acceptance::Storage storage(folder.getChildFile("settings"));
    AudioEngine audio(false, folder.getChildFile("guide.json"));
    te::Edit::Options options{audio.engine, te::createEmptyEdit(audio.engine), {}};
    options.role = te::Edit::forRendering;
    options.numAudioTracks = 1;
    auto edit = te::Edit::createEdit(options);
    require(bool(edit) && te::getAudioTracks(*edit).size() == 1, "Actual disposable native Edit/track");
    auto *track = te::getAudioTracks(*edit)[0];
    PlayHead head;
    PlayHeadState headState(head);
    te::ProcessState process(headState, edit->tempoSequence);
    const auto id = [&]
    { return te::EditItemID::readOrCreateNewID(*edit, juce::ValueTree("ORIGINAL_FIXTURE")); };
    const auto proves = [](Node &node)
    { return te::EditNodeBuilder::hasPreparedAudioZeroOutput(node, [](te::Plugin &) { return false; }, {}); };
    struct UnprovedMidi final : Node
    {
        NodeProperties getNodeProperties() override
        {
            return {false, true, 0, 0, 0x554e50524f564544ULL};
        }
        bool isReadyToProcess() override
        {
            return true;
        }
        void process(ProcessContext &) override {}
    };
    te::CombiningNode empty(id(), process);
    require(empty.getNumInputs() == 0 && empty.getInternalNodes().empty() && proves(empty),
            "Genuine empty native combiner is structurally audio zero");
    // A future, currently inaudible input remains an owned producer. Its direct
    // input list is still empty: that list cannot certify a native combiner.
    empty.addInput(std::make_unique<SilentNode>(2),
                   te::BeatRange{te::BeatPosition::fromBeats(500), te::BeatPosition::fromBeats(501)});
    require(empty.getNumInputs() == 1 && empty.getDirectInputNodes().empty() && !proves(empty),
            "Populated future/silent-looking clip cannot reuse an earlier empty proof");
    te::CombiningNode midiOnly(id(), process);
    midiOnly.addInput(std::make_unique<UnprovedMidi>(),
                      te::BeatRange{te::BeatPosition::fromBeats(500), te::BeatPosition::fromBeats(501)});
    require(!midiOnly.getNodeProperties().hasAudio && !proves(midiOnly),
            "Populated zero-channel MIDI combiner is independently refused");
    te::ArrangerLauncherSwitchingNode emptySwitch(process, *track,
                                                  std::make_unique<te::CombiningNode>(id(), process), {});
    require(emptySwitch.getNodeProperties().numberOfChannels == 0 && emptySwitch.getInternalNodes().empty() &&
                proves(emptySwitch),
            "Exact empty zero-channel switching node has no fader PCM history");
    te::ArrangerLauncherSwitchingNode audioSwitch(process, *track, std::make_unique<SilentNode>(2), {});
    require(!proves(audioSwitch),
            "Nonzero-channel switching/fader history remains unqualified even if silent");
    te::ArrangerLauncherSwitchingNode unknownSwitch(process, *track, std::make_unique<UnprovedMidi>(), {});
    require(!proves(unknownSwitch), "Unknown zero-channel arranger source does not become a declaration");
    std::vector<std::unique_ptr<te::SlotControlNode>> hidden;
    hidden.push_back(std::make_unique<te::SlotControlNode>(
        process, std::make_shared<te::LaunchHandle>(), std::nullopt, std::function<void(te::MonotonicBeat)>{},
        id(), std::make_unique<UnprovedMidi>()));
    te::ArrangerLauncherSwitchingNode hiddenSwitch(
        process, *track, std::make_unique<te::CombiningNode>(id(), process), std::move(hidden));
    require(!hiddenSwitch.getNodeProperties().hasAudio &&
                hiddenSwitch.getNodeProperties().numberOfChannels == 0 &&
                hiddenSwitch.getDirectInputNodes().size() == 1 &&
                hiddenSwitch.getInternalNodes().size() == 1 && !proves(hiddenSwitch),
            "Actual hidden launcher input refuses independently of audio/channel properties");
    require(folder.getChildFile("verification.txt")
                .replaceWithText(
                    "PASS: real native empty combiner/switch; populated future/audio/MIDI, unknown and "
                    "hidden launcher refusals.\n"
                    "CONTROL classifier only; no callback, desktop, hardware or listening qualification.\n"),
            "Retain explicit native-class fixture scope");
}
} // namespace empty_mixer_lineage_acceptance
