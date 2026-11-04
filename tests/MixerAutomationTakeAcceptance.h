// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/MixerAutomationTake.h"
#include "../src/PatternAutomation.h"
#include "../src/MidiControlInput.h"
#include <stdexcept>

namespace vibera::test::automation_touch
{
inline void require(bool value, const char *why)
{
    if (!value)
        throw std::runtime_error(why);
}
inline void publish(Controller &c, std::unique_ptr<PreparedControllerChange> &p)
{
    require(p && c.validatePreparedChange(*p).wasOk(), "Exact prepared source validation");
    require(c.commitPreparedChange(*p), "Actual canonical commit");
    c.deliverPendingNotifications();
}
// Song loop overdub: loop beats 4..8 at 120 BPM; pass 1 rewrites the span pass 0 touched.
inline void verifyLoopOverdub()
{
    auto p = Project::empty();
    p.tempo = 120;
    std::unique_ptr<MixerAutomationTake> take;
    require(MixerAutomationTake::prepare(p, false, 1, 2, "loop", "loop-take", 7, take).wasOk(),
            "Prepare loop take");
    take->rate = 48000;
    take->originSeconds = 10;
    take->originBeat = 4;
    take->looped = true;
    take->loopStartBeat = 4;
    take->loopEndBeat = 8;
    take->phase = MixerAutomationTake::Phase::capturing;
    const auto gain = *MixerAutomationTake::keyForCommand("mixer.gain", 1, 0);
    const auto pan = *MixerAutomationTake::keyForCommand("mixer.pan", 1, 0);
    auto add = [&](const AutomationKey &key, double seconds, float value)
    {
        const MixerAutomationTake::Event e{key, seconds, value};
        require(take->admitEvent(e).wasOk(), "Loop event admission");
        auto expected = std::make_unique<Project>(p);
        take->accepted(e, expected);
    };
    add(gain, 10.5, .2f);  // Pass 0, beat 5.
    add(gain, 11., .4f);   // Pass 0, beat 6.
    add(pan, 11.5, -.5f);  // Pass 0, beat 7 (not touched again).
    add(gain, 12.5, .8f);  // Pass 1, song beat 9 -> loop beat 5.
    add(gain, 13., .9f);   // Pass 1, beat 6.
    add(gain, 13.5, .7f);  // Pass 1, beat 7.
    require(take->fold(12.5).pass == 1 && std::abs(take->fold(12.5).beat - 5) < 1.e-9, "Loop fold position");
    take->stopSeconds = 14; // Song beat 12: wrapped twice.
    take->phase = MixerAutomationTake::Phase::ready;
    Project recorded;
    require(take->destination(false, recorded).wasOk(), "Loop take destination");
    require(recorded.automation.placements.size() == 2, "Gain and pan clips");
    for (const auto &placement : recorded.automation.placements)
        require(std::abs(placement.beat - 4) < 1.e-9 && std::abs(placement.length - 4) < 1.e-9,
                "A wrapped take covers the whole loop");
    AutomationProgram program;
    std::string error;
    require(AutomationProgram::compile(recorded.automation, true, program, error, recorded.automationDefaults()),
            "Compile loop take");
    auto approx = [](std::optional<float> v, float x) { return v && std::abs(*v - x) < 1.e-5f; };
    require(approx(program.valueAt(gain, 4.5), 1.f), "Before the first touch: manual gain");
    require(approx(program.valueAt(gain, 5.5), .8f), "Latest pass replaces beat 5");
    require(approx(program.valueAt(gain, 6.5), .9f), "Latest pass replaces beat 6");
    require(approx(program.valueAt(gain, 7.5), .7f), "Latest pass adds beat 7");
    require(approx(program.valueAt(pan, 7.5), -.5f), "An earlier pass of another control survives");
}
// Pattern mode: the take becomes Pattern automation that plays with the Pattern everywhere.
inline void verifyPatternTake()
{
    auto p = Project::empty();
    p.tempo = 120;
    auto &part = p.patterns.front();
    part.length = 4;
    std::unique_ptr<MixerAutomationTake> take;
    require(MixerAutomationTake::prepare(p, false, 1, 0, "pattern", "pattern-take", 9, take).wasOk(),
            "Prepare Pattern take");
    take->rate = 48000;
    take->originSeconds = 10;
    take->originBeat = 0;
    take->pattern = part.id;
    take->looped = true;
    take->loopStartBeat = 0;
    take->loopEndBeat = 4;
    take->phase = MixerAutomationTake::Phase::capturing;
    const auto gain = *MixerAutomationTake::keyForCommand("mixer.gain", 1, 0);
    auto add = [&](double seconds, float value)
    {
        const MixerAutomationTake::Event e{gain, seconds, value};
        require(take->admitEvent(e).wasOk(), "Pattern event admission");
        auto expected = std::make_unique<Project>(p);
        take->accepted(e, expected);
    };
    add(10.5, .5f); // Beat 1.
    add(12.5, .7f); // Beat 5 -> second pass, Pattern beat 1 (replaces .5).
    take->stopSeconds = 11.5; // Beat 3: one pass only; still covers the whole Pattern.
    take->phase = MixerAutomationTake::Phase::ready;
    Project recorded;
    take->stopSeconds = 13;
    require(take->destination(false, recorded).wasOk(), "Pattern take destination");
    const auto *recordedPart = recorded.pattern(part.id);
    require(recordedPart && recordedPart->automation.size() == 1 && recorded.automation.placements.empty() &&
                recordedPart->automation[0].beat == 0 && recordedPart->automation[0].length == 4,
            "The take is Pattern automation covering the Pattern, not a Playlist clip");
    require(recorded.validate().wasOk(), "Recorded Pattern automation validates");
    Project reloaded;
    require(Project::parse(recorded.state(), reloaded).wasOk() && reloaded.pattern(part.id) &&
                reloaded.pattern(part.id)->automation.size() == 1,
            "Pattern automation survives save and reload");
    auto program = [](const AudibleAutomation &audible, const Project &project)
    {
        AutomationProgram result;
        std::string error;
        require(AutomationProgram::compile(audible.document, audible.songMode, result, error,
                                           project.automationDefaults()),
                "Compile audible automation");
        return result;
    };
    auto approx = [](std::optional<float> v, float x) { return v && std::abs(*v - x) < 1.e-5f; };
    const auto inPattern = program(audibleAutomation(recorded, true, part.id), recorded);
    require(approx(inPattern.valueAt(gain, .5), 1.f) && approx(inPattern.valueAt(gain, 1.5), .7f),
            "Pattern mode plays the Pattern automation (latest pass)");
    recorded.placements.push_back({recorded.allocate(), part.id, 2, 8, 4, 0});
    recorded.placements.push_back({recorded.allocate(), part.id, 3, 16, 2, 1}); // Window: Pattern beats 1..3.
    const auto inSong = program(audibleAutomation(recorded, false, 0), recorded);
    require(approx(inSong.valueAt(gain, 8.5), 1.f) && approx(inSong.valueAt(gain, 9.5), .7f),
            "Song mode plays it where the Pattern is placed");
    require(approx(inSong.valueAt(gain, 16.5), .7f), "A placement window with an offset starts inside the Pattern");
    const auto otherPattern = program(audibleAutomation(recorded, true, 0), recorded);
    require(!otherPattern.valueAt(gain, 1.5) || approx(otherPattern.valueAt(gain, 1.5), 1.f),
            "Another Pattern does not play it");
    // Deleting the source removes the Pattern windows (no dangling reference).
    auto deleted = recorded;
    deleted.automation.sources.clear();
    require(deleted.validate().failed(), "A dangling Pattern window is invalid until pruned");
}
__declspec(noinline) inline juce::Result verify(const juce::File &output)
{
    try
    {
        verifyLoopOverdub();
        verifyPatternTake();
        require(output.createDirectory().wasOk(), "Owned touch output");
        auto c = std::make_unique<Controller>();
        auto p = Project::empty();
        auto row = std::find_if(p.mixer.begin(), p.mixer.end(), [](const auto &r) { return r.index == 1; });
        row->routes = {{0, 1}, {2, .25f}};
        c->replace(p);
        c->dirty = false;
        const auto baseline = c->project.state();
        std::unique_ptr<MixerAutomationTake> take;
        require(MixerAutomationTake::prepare(c->project, c->dirty, 1, 3, c->projectIdentity, "original-take",
                                             1, take)
                    .wasOk(),
                "Prepare exact Mixer target universe");
        require(take->targets.size() == 5, "Three original controls plus two actual sends");
        take->rate = 48000;
        take->originSeconds = 10;
        take->originBeat = 2;
        take->phase = MixerAutomationTake::Phase::capturing;
        const auto preview = [&](const char *id, double seconds, double value, int destination = 0)
        {
            const auto undo = c->undo.getUndoDescription(), redo = c->undo.getRedoDescription();
            const auto units = c->undo.getNumberOfUnitsTakenUpByStoredCommands();
            CommandArgs args;
            args.mixer = 1;
            args.target = destination;
            args.value = value;
            const auto key = MixerAutomationTake::keyForCommand(id, 1, destination);
            require(bool(key), "Supported original target");
            const MixerAutomationTake::Event event{*key, seconds, float(value)};
            require(take->admitEvent(event).wasOk(), "Literal producer-time event admission");
            std::unique_ptr<PreparedControllerChange> prepared;
            require(c->prepareAutomationTouchPreview(*take, id, args, prepared).wasOk(),
                    "Shared canonical preview");
            auto expected = std::make_unique<Project>(*prepared->destinationBeforeCommit());
            publish(*c, prepared);
            take->accepted(event, expected);
            const auto historyDetail = juce::String("Preview ") + id + ": Undo [" +
                c->undo.getUndoDescription() + "] expected [" + undo + "]; Redo [" +
                c->undo.getRedoDescription() + "] expected [" + redo + "]";
            require(c->undo.getUndoDescription() == undo && c->undo.getRedoDescription() == redo &&
                        c->undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                    historyDetail.toRawUTF8());
        };
        preview("mixer.gain", 10.25, .5);
        preview("mixer.pan", 10.5, -.5);
        preview("mixer.stereoSeparation", 10.75, .5);
        preview("mixer.send", 11, 1.25, 2);
        const auto captured = c->project.state();
        require(!captured.isEquivalentTo(baseline), "Actual audible-manual preview state changes");
        CommandArgs foreign;
        foreign.mixer = 2;
        foreign.value = .2;
        std::unique_ptr<PreparedControllerChange> refused;
        require(c->prepareAutomationTouchPreview(*take, "mixer.gain", foreign, refused).failed() &&
                    !refused && c->project.state().isEquivalentTo(captured),
                "Foreign target refuses without history or model mutation");
        take->stopSeconds = 12;
        take->phase = MixerAutomationTake::Phase::ready;
        std::unique_ptr<PreparedControllerChange> final;
        require(c->prepareAutomationTouchFinish(*take, false, final).wasOk(),
                "Prepare one actual take history action");
        publish(*c, final);
        const auto recorded = c->project.state();
        require(c->project.automation.sources.size() == 4 && c->project.automation.placements.size() == 4,
                "Exactly four touched controls, no clip for untouched Master send");
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(c->project.automation, true, program, error,
                                           c->project.automationDefaults()),
                "Actual canonical captured source compilation");
        // Independent literal input facts: startSong2,120BPM. Event positions
        // 2.5/3/3.5/4; values .5/-.5/.5/1.25. No output-fit or point-derived oracle.
        const std::array<AutomationKey, 4> keys{{{AutomationParameter::mixerGain, 1, 0},
                                                 {AutomationParameter::mixerPan, 1, 0},
                                                 {AutomationParameter::mixerStereoSeparation, 1, 0},
                                                 {AutomationParameter::mixerSend, 1, 2}}};
        const std::array<double, 4> starts{2.5, 3, 3.5, 4};
        const std::array<float, 4> before{1, 0, 0, .25f}, after{.5f, -.5f, .5f, 1.25f};
        for (size_t k = 0; k < keys.size(); ++k)
            for (int frame = 0; frame < 144000; ++frame)
            {
                const double beat = double(frame) / 24000.;
                const auto actual = program.valueAt(keys[k], beat);
                require(actual && *actual == (beat < starts[k] ? before[k] : after[k]),
                        "Every 48k frame matches independent held-step target units including exact edges");
            }
        const auto file = output.getChildFile("original-recorded-song.vibra");
        require(!file.exists() && saveProject(c->project, file).wasOk(),
                "Save original recorded project without overwrite");
        Project disk;
        require(loadProject(file, disk).wasOk() && disk.state().isEquivalentTo(recorded),
                "Actual persisted points/links/initialization/manual values exact");
        require(c->undoEditChecked().wasOk() && c->project.state().isEquivalentTo(baseline),
                "One real Undo restores exact pre-take model using existing saved/dirty history semantics");
        require(c->redoEditChecked().wasOk() && c->project.state().isEquivalentTo(recorded),
                "One real Redo restores exact recorded source IDs and values");
        std::unique_ptr<MixerAutomationTake> overlap;
        require(
            MixerAutomationTake::prepare(c->project, true, 1, 0, c->projectIdentity, "overlap", 2, overlap)
                    .failed() &&
                !overlap,
            "Existing recorded links/initialization refuse before a second arm");
        require(c->undoEditChecked().wasOk(), "Return to exact original baseline");
        c->dirty = false; // Explicit fresh clean baseline for the independent Cancel scenario.
        const auto cancelUndo = c->undo.getUndoDescription(), cancelRedo = c->undo.getRedoDescription();
        const auto cancelUnits = c->undo.getNumberOfUnitsTakenUpByStoredCommands();
        require(
            MixerAutomationTake::prepare(c->project, c->dirty, 1, 0, c->projectIdentity, "cancel", 3, take)
                .wasOk(),
            "Prepare fresh cancel pass");
        take->rate = 48000;
        take->originSeconds = 10;
        take->originBeat = 0;
        take->phase = MixerAutomationTake::Phase::capturing;
        preview("mixer.pan", 10.125, .75);
        require(c->prepareAutomationTouchFinish(*take, true, final).wasOk(),
                "Prepare original cancel baseline");
        publish(*c, final);
        require(c->project.state().isEquivalentTo(baseline) && !c->dirty &&
                    c->undo.getUndoDescription() == cancelUndo && c->undo.getRedoDescription() == cancelRedo &&
                    c->undo.getNumberOfUnitsTakenUpByStoredCommands() == cancelUnits,
                "Cancel restores manual/dirty/history and preserves the actual entry Redo branch");
        require(MixerAutomationTake::prepare(c->project, false, 1, 0, c->projectIdentity, "empty", 4, take)
                    .wasOk(),
                "Prepare empty pass");
        take->externalChangesBefore = c->externalChangeSerial();
        c->markExternalChange();
        require(c->prepareAutomationTouchFinish(*take, false, final).wasOk(),
                "Prepare empty Finish after actual external dirty notification");
        publish(*c, final);
        require(c->dirty && c->project.state().isEquivalentTo(baseline),
                "Empty Finish never clears newer unsaved native/external ownership");
        auto inbox = std::make_unique<MidiControlInput>();
        MidiClock clock;
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.;
        clock.publish(17.25, now, 0, true);
        require(inbox->beginAutomationStamp(clock, 41), "Prepare actual producer stamp owner");
        inbox->observe(juce::MidiMessage::controllerEvent(1, 23, 99), 1);
        inbox->endAutomationStamp();
        ControlMessage event;
        require(inbox->automationStampIdle() && inbox->pop(1, event) && event.automationClockValid &&
                    event.automationSeconds == 17.25 && event.automationSession == 41 && event.number == 23 &&
                    event.value == 99,
                "Producer callback retains exact real source clock/session independently of drain time");
        clock.publish(99, now, 0, true);
        require(event.automationSeconds == 17.25, "Later source clock never retimestamps queued input");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
} // namespace vibera::test::automation_touch
