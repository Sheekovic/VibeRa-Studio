// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "ScoreTools.h"
#include <juce_cryptography/juce_cryptography.h>
#include "Editors.h"
#include "MidiScoreApplication.h"
#include "MidiImportChannels.h"
#include "InstrumentWindow.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
namespace
{
class MidiImportWindow final : public juce::AlertWindow
{
    WorkspaceLook theme;

  public:
    MidiImportChannels channels;
    MidiImportWindow()
        : juce::AlertWindow("Import MIDI data",
                            "Notes, CC, pitch bend, channel pressure and program changes are imported; SysEx "
                            "is not.",
                            juce::MessageBoxIconType::NoIcon)
    {
        setLookAndFeel(&theme);
    }
    ~MidiImportWindow() override
    {
        setLookAndFeel(nullptr);
    }
};
std::unique_ptr<juce::AlertWindow> midiImportDialog(const MidiScore &score)
{
    auto dialog = std::make_unique<MidiImportWindow>();
    juce::StringArray tracks{"All tracks"};
    for (int i = 0; i < score.tracks; ++i)
    {
        auto title = "Track " + juce::String(i + 1);
        if (i < int(score.trackNames.size()) && score.trackNames[size_t(i)].isNotEmpty())
            title += " - " + score.trackNames[size_t(i)];
        tracks.add(title);
    }
    dialog->addComboBox("track", tracks, "Which tracks to import");
    dialog->addCustomComponent(&dialog->channels);
    dialog->addComboBox("blend", {"Replace selected Channel notes", "Blend with existing notes"},
                        "Existing data");
    dialog->addComboBox("align", {"Preserve original note positions", "Realign first note to beat zero"},
                        "Alignment");
    juce::String fileTempo = "Use the file's tempo";
    if (!score.tempos.empty())
        fileTempo << " (" << juce::String(score.tempos.front().second, 1) << " BPM"
                  << (score.tempos.size() > 1 ? " + " + juce::String(int(score.tempos.size()) - 1) + " changes" : "")
                  << ")";
    if (score.numerator != 0)
        fileTempo << " and " << score.numerator << "/" << score.denominator;
    dialog->addComboBox("tempo", {"Keep this project's tempo and time signature", fileTempo}, "Tempo");
    dialog->addComboBox("split", {"Into the selected Channel", "One new Channel per MIDI track"}, "Channels");
    dialog->addButton("Import", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto *window = dialog.get();
    dialog->channels.changed = [window] { window->getButton(0)->setEnabled(window->channels.mask() != 0); };
    for (auto name : {"track", "blend", "align", "tempo", "split"})
        dialog->getComboBoxComponent(name)->setSelectedItemIndex(0);
    return dialog;
}
MidiScoreOptions importOptions(juce::AlertWindow &dialog)
{
    auto *window = dynamic_cast<MidiImportWindow *>(&dialog);
    return {dialog.getComboBoxComponent("track")->getSelectedItemIndex() - 1, 0,
            dialog.getComboBoxComponent("align")->getSelectedItemIndex() == 1,
            window ? window->channels.mask() : 0};
}
juce::Result writeMidiFile(const juce::File &file, const juce::MemoryBlock &bytes)
{
    if (file.isDirectory())
        return juce::Result::fail("MIDI export target is a directory");
    juce::TemporaryFile temporary(file);
    auto stream = temporary.getFile().createOutputStream();
    if (!stream)
        return juce::Result::fail("Cannot create MIDI export");
    stream->setPosition(0);
    stream->truncate();
    if (!stream->write(bytes.getData(), bytes.getSize()))
        return juce::Result::fail("Cannot write MIDI export");
    stream->flush();
    if (stream->getStatus().failed())
        return stream->getStatus();
    stream.reset();
    return temporary.overwriteTargetFileWithTemporary()
               ? juce::Result::ok()
               : juce::Result::fail("Cannot replace MIDI export file");
}
} // namespace
void Studio::midiFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Import MIDI file...", !midiFileBusy && !(midiTaking || hasPendingMidiTake()));
    menu.addItem(2, "Export as MIDI file...", !midiFileBusy);
    menu.addSeparator();
    menu.addItem(3, "Draw tool");
    menu.addItem(4, "Paint tool");
    menu.addSeparator();
    menu.addItem(5, "Quick legato    Ctrl+L");
    menu.addItem(6, "Quick chop    Ctrl+U");
    menu.addItem(7, "Glue    Ctrl+G");
    menu.addSeparator();
    menu.addItem(8, "Quantizer...    Alt+Q");
    menu.addItem(9, "Quick quantize    Ctrl+Q");
    menu.addItem(10, "Quick quantize start times    Shift+Q");
    menu.addSeparator();
    menu.addItem(14, "Slice notes tool    C");
    menu.addItem(15, "Stretch selected notes...", !selectedNotes.empty());
    menu.addItem(11, "Mute notes tool    T");
    menu.addItem(12, "Mute selected notes");
    menu.addItem(13, "Unmute selected notes");
    {
        const auto *part = controller.project.pattern(selectedPattern);
        const bool controls = part && std::any_of(part->controllers.begin(), part->controllers.end(),
                                                  [&](const auto &c) { return c.channel == selectedChannel; });
        menu.addItem(16, "Clear recorded controllers (CC, pitch bend, pressure)", controls);
    }
    {
        juce::PopupMenu stamp;
        stamp.addItem(19, "Off (single notes)", true, pianoStamp.isEmpty());
        const auto &shapes = score::chordShapes();
        for (size_t i = 0; i < shapes.size(); ++i)
            stamp.addItem(900 + int(i), shapes[i].name, true, pianoStamp == shapes[i].name);
        menu.addSubMenu("Chord stamp (draw places a chord)", stamp);
    }
    {
        juce::PopupMenu sel;
        sel.addItem(40, "Select one more at random    Shift+R");
        sel.addItem(41, "Select odd (off-beat)    Shift+O");
        sel.addItem(42, "Select muted");
        sel.addItem(43, "Select overlapping notes");
        sel.addItem(44, "Select stacked notes");
        sel.addItem(45, "Invert selection    Shift+I");
        menu.addSubMenu("Select", sel);
        juce::PopupMenu edit;
        edit.addItem(46, "Rotate left    Shift+Ctrl+Left");
        edit.addItem(47, "Rotate right    Shift+Ctrl+Right");
        edit.addItem(48, "Discard lengths    Shift+D");
        juce::PopupMenu articulate;
        articulate.addItem(60, "Staccato (25%)");
        articulate.addItem(61, "Portato (50%)");
        articulate.addItem(62, "Tenuto (90%)");
        edit.addSubMenu("Articulate", articulate);
        juce::PopupMenu levels;
        levels.addItem(63, "Softer (velocity x 0.8)");
        levels.addItem(64, "Louder (velocity x 1.2)");
        levels.addItem(65, "Compress toward 0.7");
        levels.addItem(66, "Invert velocities");
        edit.addSubMenu("Scale levels", levels);
        edit.addItem(54, "Allow resizing from left    Ctrl+Alt+Home", true, pianoResizeLeft);
        const bool timeRange =
            static_cast<Editor *>(windows[1]->getContentComponent())->timeSelection().has_value();
        edit.addItem(74, "Group selected notes    Shift+G", selectedNotes.size() >= 2);
        {
            juce::PopupMenu toAutomation;
            if (const auto *part = controller.project.pattern(selectedPattern))
            {
                std::map<std::pair<int, int>, int> lanes;
                for (const auto &c : part->controllers)
                    if (c.channel == selectedChannel && c.type <= 2)
                        ++lanes[{c.type, c.type == 0 ? c.number : 0}];
                for (const auto &[lane, count] : lanes)
                    if (lane.first == 0 && lane.second >= 0 && lane.second <= 127)
                        toAutomation.addItem(42000 + lane.second, "CC " + juce::String(lane.second) + " (" +
                                                                       juce::String(count) + " events)");
                    else if (lane.first > 0)
                        toAutomation.addItem(42200 + lane.first, juce::String(lane.first == 1 ? "Pitch bend" : "Pressure") +
                                                                     " (" + juce::String(count) + " events)");
            }
            edit.addSubMenu("Turn into automation clip", toAutomation, toAutomation.getNumItems() > 0);
        }
        edit.addItem(75, "Ungroup selected notes    Alt+G", !selectedNotes.empty());
        edit.addItem(70, "Insert space    Ctrl+Ins", timeRange);
        edit.addItem(71, "Slice & insert space", timeRange);
        edit.addItem(72, "Delete space    Ctrl+Del", timeRange);
        edit.addItem(73, "Trim selection (keep only the time selection)", timeRange);
        sel.addItem(55, "Select time around selection    Ctrl+Enter", !selectedNotes.empty());
        juce::PopupMenu limit;
        limit.addItem(49, "C4..B5 (low)");
        limit.addItem(50, "C5..B6 (middle)");
        limit.addItem(51, "C6..B7 (high)");
        edit.addSubMenu("Limit to range", limit);
        juce::PopupMenu flam;
        flam.addItem(52, "Tight (1/64 beat)");
        flam.addItem(53, "Loose (1/32 beat)");
        edit.addSubMenu("Flam", flam);
        menu.addSubMenu("Edit", edit);
    }
    {
        juce::PopupMenu randomize;
        randomize.addItem(56, "Sparse (1/8 steps)");
        randomize.addItem(57, "Medium (1/8 steps)");
        randomize.addItem(58, "Dense (1/16 steps)");
        menu.addSubMenu("Randomize (replaces this Channel's part, guide scale or C major)", randomize);
        juce::PopupMenu riff;
        riff.addItem(41000, "Steady 1/8 riff");
        riff.addItem(41001, "Busy 1/8 riff");
        riff.addItem(41002, "Sparse 1/4 riff");
        menu.addSubMenu("Riff machine (replaces this Channel's part)", riff);
        juce::PopupMenu claw;
        claw.addItem(41010, "Gate 1/16: x.");
        claw.addItem(41011, "Gate 1/16: xx.x");
        claw.addItem(41012, "Gate 1/8: x.x..x.");
        claw.addItem(41013, "Gate 1/16: x..x..x.");
        menu.addSubMenu("Claw machine (slice into a rhythm)", claw);
    }
    {
        juce::PopupMenu lfo;
        const char *shapes[]{"Sine", "Triangle", "Square", "Saw"};
        for (int shape = 0; shape < 4; ++shape)
        {
            juce::PopupMenu periods;
            periods.addItem(40000 + shape * 10 + 0, "1 beat");
            periods.addItem(40000 + shape * 10 + 1, "1 bar");
            periods.addItem(40000 + shape * 10 + 2, "2 bars");
            lfo.addSubMenu(shapes[shape], periods);
        }
        menu.addSubMenu("LFO tool (CC 1 mod wheel, replaces that lane)", lfo);
    }
    menu.addItem(69, "Export as score sheet (PDF)...");
    menu.addItem(67, "Copy to MIDI clipboard");
    menu.addItem(68, "Paste from MIDI clipboard    Shift+Ctrl+V");
    menu.addItem(17, "Flip horizontally (reverse in time)");
    menu.addItem(18, "Flip vertically (mirror pitches)");
    // Composition tools act on the selection, or the whole Channel part without one.
    menu.addSeparator();
    {
        juce::PopupMenu chords;
        const auto &shapes = score::chordShapes();
        for (size_t i = 0; i < shapes.size(); ++i)
        {
            juce::PopupMenu inversions;
            for (int inversion = 0; inversion < juce::jmin(3, int(shapes[i].intervals.size())); ++inversion)
                inversions.addItem(1000 + int(i) * 10 + inversion,
                                   inversion == 0 ? "Root position" : juce::String(inversion) + ". inversion");
            chords.addSubMenu(shapes[i].name, inversions);
        }
        menu.addSubMenu("Chordify", chords);
        juce::PopupMenu arp;
        const char *stepNames[]{"1/32", "1/16", "1/8", "1/4"};
        for (int m = 0; m < 4; ++m)
        {
            juce::PopupMenu steps;
            for (int st = 0; st < 4; ++st)
                steps.addItem(2000 + m * 10 + st, stepNames[st]);
            const char *labels[]{"Up", "Down", "Up-down", "Random"};
            arp.addSubMenu(labels[m], steps);
        }
        menu.addSubMenu("Arpeggiate", arp);
        juce::PopupMenu strum;
        strum.addItem(3000, "Up, fast (1/64 per note)");
        strum.addItem(3001, "Up, slow (1/32 per note)");
        strum.addItem(3010, "Down, fast (1/64 per note)");
        strum.addItem(3011, "Down, slow (1/32 per note)");
        menu.addSubMenu("Strum", strum);
        juce::PopupMenu human;
        human.addItem(4000, "Light");
        human.addItem(4001, "Medium");
        human.addItem(4002, "Strong");
        menu.addSubMenu("Humanize", human);
        juce::PopupMenu scales;
        const char *roots[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu kinds;
            const auto &list = score::scaleShapes();
            for (size_t k = 0; k < list.size(); ++k)
                kinds.addItem(5000 + root * 100 + int(k), list[k].name);
            scales.addSubMenu(roots[root], kinds);
        }
        menu.addSubMenu("Snap to scale", scales);
        juce::PopupMenu maqams;
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu kinds;
            const auto &list = score::maqamat();
            for (size_t k = 0; k < list.size(); ++k)
                kinds.addItem(7000 + root * 100 + int(k), list[k].name);
            maqams.addSubMenu(roots[root], kinds);
        }
        menu.addSubMenu("Snap to maqam (quarter tones)", maqams);
        juce::PopupMenu guide;
        guide.addItem(19999, "Off", true, pianoGuide.name.isEmpty());
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu kinds;
            const auto &scaleList = score::scaleShapes();
            for (size_t k = 0; k < scaleList.size(); ++k)
                kinds.addItem(20000 + root * 100 + int(k), scaleList[k].name, true,
                              !pianoGuide.maqam && pianoGuide.root == root && pianoGuide.name == scaleList[k].name);
            kinds.addSeparator();
            const auto &maqamList = score::maqamat();
            for (size_t k = 0; k < maqamList.size(); ++k)
                kinds.addItem(30000 + root * 100 + int(k), juce::String("Maqam ") + maqamList[k].name, true,
                              pianoGuide.maqam && pianoGuide.root == root && pianoGuide.name == maqamList[k].name);
            guide.addSubMenu(roots[root], kinds);
        }
        menu.addSubMenu("Scale / maqam guide" + (pianoGuide.name.isNotEmpty()
                                                     ? " (" + juce::String(roots[pianoGuide.root]) + " " + pianoGuide.name + ")"
                                                     : juce::String()),
                        guide);
    }
    auto safe = juce::Component::SafePointer<Studio>(this);
    CommandArgs noteTargets;
    noteTargets.pattern = selectedPattern;
    noteTargets.channel = selectedChannel;
    noteTargets.ids = selectedNotes;
    const auto menuProject = controller.projectIdentity;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withParentComponent(this),
        [safe, noteTargets, menuProject](int result)
        {
            if (!safe)
                return;
            if (result >= 19999 && result < 32000)
            {
                // View only: tint the Piano Roll rows of the chosen scale or maqam.
                if (result == 19999)
                    safe->pianoGuide = {};
                else
                {
                    const bool maqam = result >= 30000;
                    const int code = result - (maqam ? 30000 : 20000);
                    safe->pianoGuide = {maqam ? juce::String(score::maqamat()[size_t(code % 100)].name)
                                              : juce::String(score::scaleShapes()[size_t(code % 100)].name),
                                        code / 100, maqam};
                }
                safe->repaint();
                return;
            }
            if (result == 15)
            {
                if(safe->controller.projectIdentity!=menuProject || safe->selectedPattern!=noteTargets.pattern ||
                   safe->selectedChannel!=noteTargets.channel || safe->selectedNotes!=noteTargets.ids)
                {safe->status="Piano Roll menu target changed; open it again";safe->refresh();return;}
                safe->focus=Scope::piano;safe->performAction("notes.stretch");return;
            }
            if (result == 14)
            {
                safe->focus = Scope::piano;
                safe->performAction("piano.toolSlice");
                return;
            }
            if (result >= 11 && result <= 13)
            {
                if (result != 11 && safe->controller.projectIdentity != menuProject)
                {
                    safe->status = "Piano Roll menu target changed; open it again";
                    safe->refresh();
                    return;
                }
                safe->focus = Scope::piano;
                if (result == 11)
                    safe->performAction("piano.toolMute");
                else
                    safe->command(result == 12 ? "notes.mute" : "notes.unmute", noteTargets);
                return;
            }
            if (result >= 41000 && result < 41020)
            {
                safe->focus = Scope::piano;
                auto args = noteTargets;
                if (result < 41010)
                {
                    args.ids.clear();
                    const auto &guide = safe->pianoGuide;
                    args.text = guide.name.isNotEmpty() && !guide.maqam ? guide.name : juce::String("minor");
                    args.pitch = 60 + (guide.name.isNotEmpty() && !guide.maqam ? guide.root : 0);
                    args.length = result == 41002 ? 1. : .5;
                    args.value = result == 41000 ? .6 : result == 41001 ? .85 : .5;
                    args.seed = std::uint32_t(juce::Time::getMillisecondCounter());
                    safe->command("notes.riff", args);
                }
                else
                {
                    const char *rhythms[]{"x.", "xx.x", "x.x..x.", "x..x..x."};
                    args.text = rhythms[result - 41010];
                    args.length = result == 41012 ? .5 : .25;
                    safe->command("notes.claw", args);
                }
                return;
            }
            if (result >= 40000 && result < 40040)
            {
                safe->focus = Scope::piano;
                auto args = noteTargets;
                args.ids.clear();
                args.text = "cc";
                args.pitch = 1;
                args.target = Id((result - 40000) / 10);
                const int period = (result - 40000) % 10;
                const auto *part = safe->controller.project.pattern(args.pattern);
                const double bar = part ? patternBarBeats(safe->controller.project, *part) : 4.;
                args.length = period == 0 ? 1. : period == 1 ? bar : 2 * bar;
                args.value = .8;
                args.offset = .5;
                safe->command("notes.controller.lfo", args);
                return;
            }
            if (result == 69)
            {
                safe->focus = Scope::piano;
                safe->performAction("notes.exportScore");
                return;
            }
            if (result == 67 || result == 68)
            {
                safe->focus = Scope::piano;
                safe->performAction(result == 67 ? "notes.copyMidiClipboard" : "notes.pasteMidiClipboard");
                return;
            }
            if (result >= 60 && result <= 66)
            {
                safe->focus = Scope::piano;
                auto args = noteTargets;
                if (result <= 62)
                {
                    args.value = result == 60 ? .25 : result == 61 ? .5 : .9;
                    safe->command("notes.articulate", args);
                }
                else
                {
                    args.value = result == 63 ? .8 : result == 64 ? 1.2 : result == 65 ? .5 : 1.;
                    args.offset = result == 65 ? .35 : 0.;
                    args.enabled = result == 66;
                    safe->command("notes.scaleLevels", args);
                }
                return;
            }
            if (result >= 56 && result <= 58)
            {
                safe->focus = Scope::piano;
                auto args = noteTargets;
                args.ids.clear();
                const auto &guide = safe->pianoGuide;
                args.text = guide.name.isNotEmpty() && !guide.maqam ? guide.name : juce::String("major");
                args.pitch = 60 + (guide.name.isNotEmpty() && !guide.maqam ? guide.root : 0);
                args.length = result == 58 ? .25 : .5;
                args.value = result == 56 ? .35 : result == 57 ? .6 : .7;
                args.seed = std::uint32_t(juce::Time::getMillisecondCounter());
                safe->command("notes.randomize", args);
                return;
            }
            if ((result >= 42000 && result <= 42127) || result == 42201 || result == 42202)
            {
                // At the Pattern's first Playlist clip (or the Song start), on the first free lane.
                auto &project = safe->controller.project;
                CommandArgs a;
                a.pattern = safe->selectedPattern;
                a.channel = safe->selectedChannel;
                a.text = result < 42200 ? "cc" : result == 42201 ? "pitchBend" : "pressure";
                a.pitch = result < 42200 ? result - 42000 : 0;
                a.beat = 1.e18;
                int lane = -1;
                for (const auto &p : project.placements)
                {
                    lane = std::max(lane, p.lane);
                    if (p.pattern == a.pattern)
                        a.beat = std::min(a.beat, p.beat);
                }
                for (const auto &p : project.audioPlacements)
                    lane = std::max(lane, p.lane);
                for (const auto &p : project.automation.placements)
                    lane = std::max(lane, p.lane);
                if (a.beat > 1.e17)
                    a.beat = 0;
                a.lane = std::min(127, lane + 1);
                safe->command("notes.toAutomation", a);
                if (safe->status == "Ready")
                    safe->status = "Automation clip created: link it from a control's automation menu";
                return;
            }
            if (result == 74 || result == 75)
            {
                safe->focus = Scope::piano;
                safe->performAction(result == 74 ? "notes.group" : "notes.ungroup");
                return;
            }
            if (result >= 70 && result <= 73)
            {
                safe->focus = Scope::piano;
                safe->performAction(juce::StringArray{"notes.insertSpaceAction", "notes.insertSpaceSliced",
                                                      "notes.deleteSpaceAction", "notes.trimAction"}[result - 70]);
                return;
            }
            if (result == 54 || result == 55)
            {
                safe->focus = Scope::piano;
                safe->performAction(result == 54 ? "piano.resizeLeft" : "selection.timeAround");
                return;
            }
            if (result >= 40 && result <= 53)
            {
                safe->focus = Scope::piano;
                const char *actions[]{"selection.random", "selection.odd", "selection.muted", "selection.overlapping",
                                      "selection.stacked", "selection.invert", "notes.rotateLeft", "notes.rotateRight",
                                      "notes.discardLengthsAction"};
                if (result <= 48)
                {
                    safe->performAction(actions[result - 40]);
                    return;
                }
                auto args = noteTargets;
                if (result <= 51)
                {
                    args.pitch = 48 + (result - 49) * 12;
                    args.value = args.pitch + 23;
                    safe->command("notes.limit", args);
                }
                else
                {
                    args.length = result == 52 ? 1. / 64 : 1. / 32;
                    args.value = .6;
                    safe->command("notes.flam", args);
                }
                return;
            }
            if (result == 19 || (result >= 900 && result < 1000))
            {
                safe->pianoStamp = result == 19 ? juce::String()
                                                : juce::String(score::chordShapes()[size_t(result - 900)].name);
                safe->status = safe->pianoStamp.isEmpty() ? "Chord stamp off"
                                                          : "Chord stamp: " + safe->pianoStamp + " (draw to place)";
                safe->repaint();
                return;
            }
            if (result == 17 || result == 18)
            {
                safe->focus = Scope::piano;
                auto args = noteTargets;
                args.text = result == 17 ? "horizontal" : "vertical";
                safe->command("notes.flip", args);
                return;
            }
            if (result == 16)
            {
                safe->focus = Scope::piano;
                safe->command("notes.clearControllers", noteTargets);
                return;
            }
            if (result >= 1000)
            {
                if (safe->controller.projectIdentity != menuProject)
                {
                    safe->status = "Piano Roll menu target changed; open it again";
                    return;
                }
                auto args = noteTargets;
                juce::String id;
                if (result < 2000)
                {
                    id = "notes.chordify";
                    args.text = score::chordShapes()[size_t((result - 1000) / 10)].name;
                    args.value = (result - 1000) % 10;
                }
                else if (result < 3000)
                {
                    const char *modes[]{"up", "down", "updown", "random"};
                    id = "notes.arpeggiate";
                    args.text = modes[(result - 2000) / 10];
                    args.length = .125 * std::pow(2., (result - 2000) % 10); // 1/32 note = 0.125 beat.
                    args.value = .9;
                }
                else if (result < 4000)
                {
                    id = "notes.strum";
                    args.text = (result - 3000) / 10 == 0 ? "up" : "down";
                    args.length = (result - 3000) % 10 == 0 ? .0625 : .125;
                    args.value = .3;
                }
                else if (result < 5000)
                {
                    const double amount = result == 4000 ? 1. : result == 4001 ? 2. : 4.;
                    id = "notes.humanize";
                    args.beat = .01 * amount;
                    args.value = .04 * amount;
                    args.offset = .03 * amount;
                    args.seed = std::uint32_t(juce::Time::getMillisecondCounter());
                }
                else if (result >= 7000)
                {
                    id = "notes.maqam";
                    args.pitch = (result - 7000) / 100;
                    args.text = score::maqamat()[size_t((result - 7000) % 100)].name;
                }
                else
                {
                    id = "notes.scaleSnap";
                    args.pitch = (result - 5000) / 100;
                    args.text = score::scaleShapes()[size_t((result - 5000) % 100)].name;
                }
                safe->focus = Scope::piano;
                safe->command(id, args);
                return;
            }
            if (result >= 8 && result <= 10)
            {
                safe->focus = Scope::piano;
                safe->performAction(result == 8   ? "notes.quantize"
                                    : result == 9 ? "notes.quickQuantize"
                                                  : "notes.quantizeStarts");
            }
            else if (result == 1)
                safe->chooseMidiImport();
            else if (result == 2)
                safe->performAction("piano.exportMidi");
            else if (result == 3 || result == 4)
                safe->selectPianoTool(result == 4);
            else if (result == 5 || result == 6 || result == 7)
            {
                safe->focus = Scope::piano;
                safe->performAction(result == 5 ? "notes.legato" : result == 6 ? "notes.chop" : "notes.glue");
            }
        });
}
void Studio::selectPianoTool(bool paint)
{
    focus = Scope::piano;
    if (auto *editor = dynamic_cast<Editor *>(windows[1]->getContentComponent()))
        editor->setPaintMode(paint);
    status = paint ? "Paint" : "Draw";
    repaint();
}
juce::Result Studio::verifyScoreTools(const juce::File &folder)
{
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels.back().id;
    selectedNotes.clear();
    auto &pattern = project.patterns[0];
    const auto first = project.allocate(), last = project.allocate();
    pattern.notes = {{first, selectedChannel, 0, .25, 60, .7f, false},
                     {last, selectedChannel, 2, 1.125, 67, .8f, false}};
    project.placements.push_back({project.allocate(), selectedPattern, 0, 0, 4, 0});
    controller.replace(project);
    refresh();
    focus = Scope::piano;
    snap = .25;
    const auto before = controller.project.state();
    if (!keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0)) ||
        controller.project.pattern(selectedPattern)->notes[0].length != 2 ||
        controller.project.channel(selectedChannel)->mixer != project.channel(selectedChannel)->mixer)
        return juce::Result::fail("Piano Ctrl+L did not perform legato independently of Rack assignment");
    selectedNotes = {last};
    if (!keyPressed(juce::KeyPress('U', juce::ModifierKeys::ctrlModifier, 0)) ||
        controller.project.pattern(selectedPattern)->notes.size() != 6 || selectedNotes.size() != 5)
        return juce::Result::fail("Piano Ctrl+U failed exact-tail chopping or new-note selection");
    const auto chopped = controller.project.state();
    if (!keyPressed(juce::KeyPress('G', juce::ModifierKeys::ctrlModifier, 0)) ||
        controller.project.pattern(selectedPattern)->notes.size() != 2 ||
        selectedNotes != std::vector<Id>{last} ||
        controller.project.pattern(selectedPattern)->notes[1].length != 1.125)
        return juce::Result::fail("Piano Ctrl+G did not rejoin chopped notes and select surviving identity");
    const auto glued = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(chopped))
        return juce::Result::fail("Piano Glue one exact Undo failed");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(glued))
        return juce::Result::fail("Piano Glue exact Redo failed");
    controller.undoEdit();
    controller.undoEdit();
    if (controller.project.pattern(selectedPattern)->notes.size() != 2)
        return juce::Result::fail("Piano chop one Undo failed");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return juce::Result::fail("Piano legato one Undo failed");
    controller.redoEdit();
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(chopped))
        return juce::Result::fail("Piano score tools Redo failed");
    selectedNotes.clear();
    for (const auto &note : controller.project.pattern(selectedPattern)->notes)
        selectedNotes.push_back(note.id);
    showEditor(1);
    maximisedEditor = 1;
    resized();
    static_cast<Editor *>(windows[1]->getContentComponent())->viewAction("piano.fit");
    refresh();
    {
        auto beforeStream = folder.getChildFile("score-tools-before-glue.png").createOutputStream();
        if (!beforeStream)
            return juce::Result::fail("Before-Glue capture stream failed");
        beforeStream->setPosition(0);
        beforeStream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()),
                                                       *beforeStream))
            return juce::Result::fail("Before-Glue capture failed");
    }
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(glued))
        return juce::Result::fail("Piano Glue final state failed");
    selectedNotes = {last};
    refresh();
    status = "Glue: Piano Roll Menu | Ctrl+G | joined notes keep the original ID and velocity";
    auto stream = folder.getChildFile("score-tools-workspace.png").createOutputStream();
    if (!stream)
        return juce::Result::fail("Score tools screenshot stream failed");
    stream->setPosition(0);
    stream->truncate();
    if (!juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()), *stream))
        return juce::Result::fail("Score tools screenshot failed");
    stream.reset();
    if (!folder.getChildFile("score-tools-gui.txt")
             .replaceWithText(
                 "PASS: actual Piano Ctrl+L/Ctrl+U/Ctrl+G handlers, current-part/selected targeting, "
                 "remainder/new selection, canonical one-step Undo/Redo and component "
                 "screenshot. Not OS input, listening or hardware.\n"))
        return juce::Result::fail("Score tools evidence write failed");
    maximisedEditor = -1;
    resized();
    startTimerHz(20);
    return juce::Result::ok();
}
void Studio::chooseMidiImport()
{
    if (midiFileBusy || (midiTaking || hasPendingMidiTake()) || controller.hasGesture())
    {
        status = "Finish the current edit or recording before MIDI import";
        repaint();
        return;
    }
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto pattern = selectedPattern, channel = selectedChannel;
    chooser = std::make_unique<juce::FileChooser>("Import MIDI file", juce::File{}, "*.mid;*.midi");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe, identity, revision, pattern, channel](const juce::FileChooser &choice)
                         {
                             if (!safe || choice.getResult() == juce::File{})
                                 return;
                             if (safe->controller.projectIdentity != identity ||
                                 safe->controller.revision != revision)
                             {
                                 safe->status = "MIDI import cancelled: project changed";
                                 safe->repaint();
                                 return;
                             }
                             safe->importMidiFile(choice.getResult(), pattern, channel);
                         });
}
void Studio::importMidiFile(const juce::File &file, Id pattern, Id channel, bool showOptions)
{
    if (midiFileBusy || (midiTaking || hasPendingMidiTake()) || controller.hasGesture())
    {
        status = "Finish the current edit or recording before MIDI import";
        repaint();
        return;
    }
    if (!pattern)
        pattern = selectedPattern;
    if (!channel)
        channel = selectedChannel;
    if (!controller.project.pattern(pattern) || !controller.project.channel(channel))
    {
        status = "MIDI import target unavailable";
        repaint();
        return;
    }
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto safe = juce::Component::SafePointer<Studio>(this);
    midiFileBusy = true;
    status = "Reading MIDI score...";
    repaint();
    safe->midiFileJobs.addJob(
        [safe, file, identity, revision, pattern, channel, showOptions]
        {
            auto bytes = std::make_shared<juce::MemoryBlock>();
            MidiScore score;
            auto result = juce::Result::fail("MIDI file is unreadable or exceeds 16 MiB");
            if (auto input = file.createInputStream();
                input && input->readIntoMemoryBlock(*bytes, 16 * 1024 * 1024 + 1))
                result = readMidiScore(*bytes, {}, score);
            juce::MessageManager::callAsync(
                [safe, bytes, score = std::move(score), result, identity, revision, pattern, channel,
                 showOptions]
                {
                    if (!safe)
                        return;
                    if (result.failed())
                    {
                        safe->midiFileBusy = false;
                        safe->status = result.getErrorMessage();
                        safe->repaint();
                        return;
                    }
                    if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                    {
                        safe->midiFileBusy = false;
                        safe->status = "MIDI import cancelled: project changed";
                        safe->repaint();
                        return;
                    }
                    if (!showOptions)
                    {
                        auto applied =
                            safe->commitMidiImport(score, false, identity, revision, pattern, channel);
                        safe->midiFileBusy = false;
                        safe->status = applied.wasOk() ? "Imported " + juce::String(int(score.notes.size())) +
                                                             " MIDI notes and " +
                                                             juce::String(int(score.controllers.size())) +
                                                             " controller events (file tempo/meter metadata not applied)"
                                                       : applied.getErrorMessage();
                        safe->refresh();
                        return;
                    }
                    auto dialog = midiImportDialog(score);
                    auto pointer = juce::Component::SafePointer<juce::AlertWindow>(dialog.get());
                    dialog->enterModalState(
                        true,
                        juce::ModalCallbackFunction::create(
                            [safe, pointer, bytes, identity, revision, pattern, channel](int accepted)
                            {
                                if (!safe)
                                    return;
                                if (!pointer || accepted != 1)
                                {
                                    safe->midiFileBusy = false;
                                    safe->status = "MIDI import cancelled";
                                    safe->repaint();
                                    return;
                                }
                                const auto options = importOptions(*pointer);
                                const bool blend =
                                    pointer->getComboBoxComponent("blend")->getSelectedItemIndex() == 1;
                                const bool useTempo =
                                    pointer->getComboBoxComponent("tempo")->getSelectedItemIndex() == 1;
                                const bool split =
                                    pointer->getComboBoxComponent("split")->getSelectedItemIndex() == 1;
                                safe->midiFileBusy = true;
                                safe->midiFileJobs.addJob(
                                    [safe, bytes, options, blend, useTempo, split, identity, revision, pattern,
                                     channel]
                                    {
                                        MidiScore imported;
                                        auto decoded = readMidiScore(*bytes, options, imported);
                                        juce::MessageManager::callAsync(
                                            [safe, imported = std::move(imported), decoded, blend, useTempo, split,
                                             identity, revision, pattern, channel]
                                            {
                                                if (!safe)
                                                    return;
                                                safe->midiFileBusy = false;
                                                auto applied = decoded;
                                                if (applied.wasOk())
                                                    applied =
                                                        safe->commitMidiImport(imported, blend, identity,
                                                                               revision, pattern, channel,
                                                                               useTempo, split);
                                                safe->status =
                                                    applied.wasOk()
                                                        ? "Imported " +
                                                              juce::String(int(imported.notes.size())) +
                                                              " MIDI notes and " +
                                                              juce::String(int(imported.controllers.size())) +
                                                              " controller events" +
                                                              (useTempo ? " with the file's tempo" : "")
                                                        : applied.getErrorMessage();
                                                safe->refresh();
                                            });
                                    });
                            }),
                        true);
                    dialog.release();
                });
        });
}
std::optional<Studio::MidiExportSnapshot> Studio::midiExportSnapshot() const
{
    const auto *pattern = controller.project.pattern(selectedPattern);
    if (!pattern || !controller.project.channel(selectedChannel))
        return {};
    return MidiExportSnapshot{*pattern,
                              selectedChannel,
                              controller.project.tempo,
                              patternNumerator(controller.project, *pattern),
                              patternDenominator(controller.project, *pattern),
                              controller.projectIdentity,
                              controller.revision};
}
juce::Result Studio::startMidiExport(const juce::File &file, MidiExportSnapshot snapshot, bool overwrite,
                                     juce::String &operation)
{
    if (midiFileBusy || afterPluginState || pendingExport || audio.rendering() || controller.hasGesture() ||
        (midiTaking || hasPendingMidiTake()) || (noteSlicePending() || noteStretchPending()))
        return juce::Result::fail(
            "Finish the active file operation, gesture, preview or recording take first");
    if (!file.hasFileExtension("mid;midi"))
        return juce::Result::fail("MIDI score export requires .mid or .midi");
    if (file.exists() && !overwrite)
        return juce::Result::fail("Output already exists; overwrite confirmation is required");
    while (mcpOperations.size() >= 64)
        mcpOperations.erase(mcpOperations.begin());
    McpOperation job;
    job.id = juce::Uuid().toString();
    job.kind = "exportMidi";
    job.path = file.getFullPathName();
    job.project = snapshot.project;
    job.revision = snapshot.revision;
    operation = job.id;
    mcpOperations.push_back(job);
    midiFileBusy = true;
    status = "Writing MIDI score...";
    auto safe = juce::Component::SafePointer<Studio>(this);
    midiFileJobs.addJob(
        [safe, snapshot = std::move(snapshot), file, overwrite, id = job.id]
        {
            juce::MemoryBlock bytes;
            int omitted = 0;
            MidiScoreOmissions details;
            auto result = writeMidiScore(snapshot.pattern, snapshot.channel, snapshot.tempo,
                                         snapshot.numerator, snapshot.denominator, bytes, &omitted, &details);
            if (result.wasOk() && file.exists() && !overwrite)
                result = juce::Result::fail("Output appeared after export was accepted");
            if (result.wasOk())
                result = writeMidiFile(file, bytes);
            juce::MessageManager::callAsync(
                [safe, result, id, omitted, details]
                {
                    if (!safe)
                        return;
                    if (result.wasOk())
                        for (auto &job : safe->mcpOperations)
                            if (job.id == id)
                            {
                                job.expressionNotesOmitted = omitted;
                                job.midiExpressionNotesOmitted = details.expressionNotes;
                                job.midiSlideNotesOmitted = details.slideNotes;
                                job.midiPortamentoNotesOmitted = details.portamentoNotes;
                            }
                    safe->midiFileBusy = false;
                    safe->completeMcpOperation(id, result);
                    safe->status = result.failed() ? result.getErrorMessage()
                                   : omitted > 0
                                       ? "MIDI score exported (unsupported native expression/modes on " +
                                             juce::String(omitted) +
                                             " notes; Slide controls skipped; muted notes omitted; 960 PPQ)"
                                       : "MIDI score exported (muted notes omitted; 960 PPQ)";
                    safe->repaint();
                });
        });
    return juce::Result::ok();
}
void Studio::chooseMidiExport()
{
    if (midiFileBusy)
        return;
    const auto snapshot = midiExportSnapshot();
    if (!snapshot)
        return;
    const bool expression =
        std::any_of(snapshot->pattern.notes.begin(), snapshot->pattern.notes.end(),
                    [&](const auto &note)
                    {
                        return note.channel == snapshot->channel && !note.muted &&
                               (note.pan != 0 || note.fineCents != 0 || note.modX != .5 || note.modY != .5);
                    });
    chooser = std::make_unique<juce::FileChooser>(expression ? "Export MIDI (Pan/Fine Pitch/Mod X/Y omitted)"
                                                             : "Export Piano Roll as MIDI file",
                                                  juce::File{}.getChildFile("score.mid"), "*.mid;*.midi");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe, snapshot = *snapshot](const juce::FileChooser &choice)
                         {
                             if (!safe || choice.getResult() == juce::File{})
                                 return;
                             juce::String operation;
                             const auto result =
                                 safe->startMidiExport(choice.getResult(), snapshot, true, operation);
                             if (result.failed())
                                 safe->status = result.getErrorMessage();
                         });
}
juce::Result Studio::commitMidiImport(const MidiScore &score, bool blend, const juce::String &identity,
                                      std::uint64_t revision, Id pattern, Id channel, bool useTempo, bool split)
{
    if (controller.projectIdentity != identity || controller.revision != revision || (midiTaking || hasPendingMidiTake()))
        return juce::Result::fail("MIDI import cancelled: project changed or recording started");
    auto result = split ? applyMidiScoreTracks(controller, pattern, channel, score, useTempo)
                        : applyMidiScore(controller, pattern, channel, score, blend, useTempo);
    if (result.wasOk())
    {
        if (selectedChannel != channel)
            releaseTypingNotes();
        selectedPattern = pattern;
        selectedChannel = channel;
        selectedNotes.clear();
        showEditor(1);
        if (auto *editor = dynamic_cast<Editor *>(windows[1]->getContentComponent()))
            editor->viewAction("piano.fit");
    }
    return result;
}
juce::Result Studio::verifyMidiInterchange(const juce::File &folder)
{
    folder.createDirectory();
    MidiScore example;
    example.tracks = 3;
    example.trackNames = {"Drums", "Bass", "Lead"};
    auto dialog = midiImportDialog(example);
    auto options = importOptions(*dialog);
    if (options.track != -1 || options.channel != 0 || options.channelMask != 65535 || options.realign ||
        dialog->getComboBoxComponent("blend")->getSelectedItemIndex() != 0)
        return juce::Result::fail("MIDI import defaults differ from documented replace/preserve");
    dialog->getComboBoxComponent("track")->setSelectedItemIndex(2);
    dynamic_cast<MidiImportWindow *>(dialog.get())->channels.setMask((1u << 2) | (1u << 9));
    dialog->getComboBoxComponent("align")->setSelectedItemIndex(1);
    options = importOptions(*dialog);
    if (options.track != 1 || options.channelMask != ((1u << 2) | (1u << 9)) || !options.realign)
        return juce::Result::fail("MIDI dialog filters incorrect");
    auto *form = dynamic_cast<MidiImportWindow *>(dialog.get());
    form->channels.setMask(0);
    if (dialog->getButton(0)->isEnabled())
        return juce::Result::fail("Empty MIDI subset permits import");
    form->channels.setMask((1u << 2) | (1u << 9));
    if (!dialog->getButton(0)->isEnabled())
        return juce::Result::fail("Valid MIDI subset disables import");
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    juce::Component &tile = form->channels.channelButton(2);
    auto point = juce::Point<float>{4, 4};
    juce::MouseEvent right(source, point, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.f,
                           0.f, 0.f, 0.f, 0.f, &tile, &tile, juce::Time::getCurrentTime(), point,
                           juce::Time::getCurrentTime(), 1, false);
    tile.mouseDown(right);
    if (form->channels.mask() != (1u << 9))
        return juce::Result::fail("MIDI right-click did not exclude only that channel");
    juce::MouseEvent left(source, point, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.f, 0.f,
                          0.f, 0.f, 0.f, &tile, &tile, juce::Time::getCurrentTime(), point,
                          juce::Time::getCurrentTime(), 1, false);
    tile.mouseDown(left);
    tile.mouseUp(left);
    if (form->channels.mask() != ((1u << 2) | (1u << 9)))
        return juce::Result::fail("MIDI left-click did not include only that channel");
    auto stream = folder.getChildFile("midi-import-dialog.png").createOutputStream();
    if (!stream)
        return juce::Result::fail("MIDI dialog PNG output");
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat png;
    if (!png.writeImageToStream(dialog->createComponentSnapshot(dialog->getLocalBounds()), *stream))
        return juce::Result::fail("MIDI dialog PNG encode");
    controller.replace(Project::demo());
    selectedPattern = controller.project.patterns.front().id;
    selectedChannel = controller.project.channels.back().id;
    const auto identity = controller.projectIdentity;
    const auto before = controller.project.state();
    const auto revision = controller.revision;
    juce::MemoryBlock bytes;
    auto result = writeMidiScore(*controller.project.pattern(selectedPattern), selectedChannel,
                                 controller.project.tempo, 4, 4, bytes);
    if (result.failed())
        return result;
    const auto file = folder.getChildFile("exported-score.mid");
    if ((result = writeMidiFile(file, bytes)).failed())
        return result;
    juce::MemoryBlock read;
    if (!file.loadFileAsData(read) || read != bytes)
        return juce::Result::fail("MIDI atomic file round trip");
    if (writeMidiFile(folder, bytes).wasOk() || !folder.isDirectory())
        return juce::Result::fail("MIDI directory protection");
    MidiScore imported;
    if ((result = readMidiScore(read, {}, imported)).failed())
        return result;
    if (commitMidiImport(imported, false, "stale-project", revision, selectedPattern, selectedChannel)
            .wasOk() ||
        !controller.project.state().isEquivalentTo(before))
        return juce::Result::fail("MIDI identity guard");
    if (commitMidiImport(imported, false, identity, revision + 1, selectedPattern, selectedChannel).wasOk() ||
        !controller.project.state().isEquivalentTo(before))
        return juce::Result::fail("MIDI revision guard");
    if ((result = commitMidiImport(imported, false, identity, revision, selectedPattern, selectedChannel))
            .failed())
        return result;
    const auto after = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return juce::Result::fail("MIDI GUI import undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(after))
        return juce::Result::fail("MIDI GUI import redo");
    const auto projectFile = folder.getChildFile("imported-project.vibra");
    if ((result = saveProject(controller.project, projectFile)).failed())
        return result;
    Project restored;
    if ((result = loadProject(projectFile, restored)).failed() || !restored.state().isEquivalentTo(after))
        return juce::Result::fail("MIDI imported project persistence");
    auto *piano = dynamic_cast<Editor *>(windows[1]->getContentComponent());
    auto *rack = dynamic_cast<Editor *>(windows[0]->getContentComponent());
    const juce::StringArray dropped{file.getFullPathName()};
    if (!piano || !rack || !piano->isInterestedInFileDrag(dropped) ||
        piano->isInterestedInFileDrag({"unrelated.wav"}))
        return juce::Result::fail("MIDI file drop target filtering");
    const auto savedBindings = customization;
    customization.unbind("tool.draw", Scope::global);
    customization.unbind("tool.paint", Scope::global);
    selectPianoTool(true);
    if (!piano->painting())
        return juce::Result::fail("MIDI menu Paint depends on key binding");
    selectPianoTool(false);
    if (piano->painting())
        return juce::Result::fail("MIDI menu Draw depends on key binding");
    customization = savedBindings;
    auto wait = [&](auto predicate)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 8000;
        while (!predicate() && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(2);
        return predicate();
    };
    auto dialogReady = [&]
    { return dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent()) != nullptr; };
    auto finished = [&] { return !midiFileBusy && juce::Component::getNumCurrentlyModalComponents() == 0; };
    const auto beforeAsync = controller.project.state();
    const auto oldCount = controller.project.pattern(selectedPattern)->notes.size();
    piano->filesDroppedWithModifiers(dropped, 80, 100, {});
    if (!wait(dialogReady))
        return juce::Result::fail("MIDI drop did not complete actual file worker/dialog: " + status);
    auto *active = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!midiFileBusy || !active->getComboBoxComponent("track")->getItemText(1).contains(
                             controller.project.pattern(selectedPattern)->name))
        return juce::Result::fail("MIDI options did not retain ownership or actual tracks");
    active->getComboBoxComponent("blend")->setSelectedItemIndex(1);
    auto actualDialogImage = folder.getChildFile("midi-async-dialog.png").createOutputStream();
    if (!actualDialogImage)
        return juce::Result::fail("MIDI asynchronous dialog PNG");
    actualDialogImage->setPosition(0);
    actualDialogImage->truncate();
    if (!png.writeImageToStream(active->createComponentSnapshot(active->getLocalBounds()),
                                *actualDialogImage))
        return juce::Result::fail("MIDI asynchronous dialog render");
    actualDialogImage.reset();
    active->exitModalState(1);
    if (!wait(finished) ||
        controller.project.pattern(selectedPattern)->notes.size() != oldCount + imported.notes.size())
        return juce::Result::fail("MIDI asynchronous blend callback failed: " + status);
    const auto blended = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeAsync))
        return juce::Result::fail("MIDI asynchronous exact undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(blended))
        return juce::Result::fail("MIDI asynchronous exact redo");
    const auto selectionBefore = selectedChannel;
    rack->filesDroppedWithModifiers(dropped, 40, 82, {});
    if (!wait(dialogReady))
        return juce::Result::fail("Rack MIDI drop did not open options: " + status);
    active = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    active->exitModalState(0);
    if (!wait(finished) || selectedChannel != selectionBefore ||
        !controller.project.state().isEquivalentTo(blended))
        return juce::Result::fail("Cancelled Rack MIDI drop changed instrument/selection/project");
    importMidiFile(file);
    if (!wait(dialogReady))
        return juce::Result::fail("MIDI stale asynchronous setup");
    CommandArgs changedTempo;
    changedTempo.value = 121;
    if ((result = controller.execute("project.tempo", changedTempo)).failed())
        return result;
    const auto retainedChange = controller.project.state();
    active = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    active->exitModalState(1);
    if (!wait(finished) || !controller.project.state().isEquivalentTo(retainedChange) ||
        !status.contains("cancelled"))
        return juce::Result::fail("MIDI asynchronous stale completion applied data");
    controller.undoEdit();
    if (midiDropShowsOptions(juce::ModifierKeys(juce::ModifierKeys::shiftModifier)) ||
        !midiDropShowsOptions(
            juce::ModifierKeys(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier)))
        return juce::Result::fail("MIDI Shift/Alt drop policy");
    const auto beforeQuick = controller.project.state();
    piano->filesDroppedWithModifiers(dropped, 80, 100, juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    if (!wait(finished) || controller.project.pattern(selectedPattern)->notes.size() != oldCount ||
        !status.contains("metadata"))
        return juce::Result::fail("MIDI Shift default replace or loss disclosure failed: " + status);
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeQuick))
        return juce::Result::fail("MIDI quick-drop exact undo");
    piano->filesDroppedWithModifiers(
        dropped, 80, 100,
        juce::ModifierKeys(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier));
    if (!wait(dialogReady))
        return juce::Result::fail("Alt did not force MIDI dialog");
    active = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    active->exitModalState(0);
    if (!wait(finished) || !controller.project.state().isEquivalentTo(beforeQuick))
        return juce::Result::fail("Forced MIDI dialog cancellation changed project");
    maximiseEditor(1);
    refresh();
    const auto library = folder.getChildFile("browser-library");
    library.createDirectory();
    library.getChildFile("Nested").createDirectory();
    const auto sample = library.getChildFile("Kick original.wav");
    {
        juce::WavAudioFormat wav;
        auto output = sample.createOutputStream();
        if (!output)
            return juce::Result::fail("Browser fixture WAV stream");
        output->setPosition(0);
        output->truncate();
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(output.release(), 48000., 1, 16, {}, 0));
        if (!writer)
            return juce::Result::fail("Browser fixture WAV writer");
        juce::AudioBuffer<float> pcm(1, 4800);
        for (int i = 0; i < 4800; ++i)
            pcm.setSample(0, i, .1f * std::sin(float(i) * .02f));
        if (!writer->writeFromAudioSampleBuffer(pcm, 0, 4800))
            return juce::Result::fail("Browser fixture WAV samples");
    }
    library.getChildFile("Unsupported.txt").replaceWithText("Not a music asset");
    auto &fileBrowser = browser->filesForTest();
    fileBrowser.filesDropped({library.getFullPathName()}, 0, 0);
    juce::DragAndDropTarget::SourceDetails builtinDrag("builtin:kick", &fileBrowser, {40, 80});
    juce::DragAndDropTarget::SourceDetails midiDrag(file.getFullPathName(), &fileBrowser, {80, 100});
    if (piano->isInterestedInDragSource(builtinDrag) || !rack->isInterestedInDragSource(builtinDrag) ||
        !piano->isInterestedInDragSource(midiDrag))
        return juce::Result::fail("Browser internal drag target classification");
    const auto browserPrefs = folder.getChildFile("browser-preferences.json");
    const auto secondRoot = library.getChildFile("Nested");
    {
        SampleFileBrowser saved(*this, browserPrefs);
        saved.setRoot(library);
        saved.setRoot(secondRoot);
        saved.setRoot(library); // duplicate folder must not add another entry
        if (saved.librariesForTest().getNumItems() != 2)
            return juce::Result::fail("Browser folder deduplication");
    }
    {
        SampleFileBrowser restored(*this, browserPrefs);
        if (restored.listingForTest().getDirectory() != library ||
            restored.librariesForTest().getNumItems() != 2)
            return juce::Result::fail("Browser persisted active folder/roots");
        restored.librariesForTest().setSelectedId(2, juce::sendNotificationSync);
        if (restored.listingForTest().getDirectory() != secondRoot)
            return juce::Result::fail("Browser actual saved-folder switch handler");
        restored.forgetForTest().onClick();
        if (restored.librariesForTest().getNumItems() != 1 || !secondRoot.isDirectory() ||
            restored.listingForTest().getDirectory() != secondRoot)
            return juce::Result::fail("Browser forget deleted files or changed active navigation");
    }
    {
        SampleFileBrowser restored(*this, browserPrefs);
        if (restored.librariesForTest().getNumItems() != 1 ||
            restored.listingForTest().getDirectory() != secondRoot)
            return juce::Result::fail("Browser removed root was not persisted");
    }
    browserPrefs.replaceWithText("{\"version\":1,\"roots\":[\"relative\",false],\"active\":\"relative\"}");
    {
        SampleFileBrowser invalid(*this, browserPrefs);
        if (invalid.hasRoot() || invalid.librariesForTest().getNumItems() != 0)
            return juce::Result::fail("Browser malformed/relative preferences accepted");
    }
    const auto missingRoot = library.getChildFile("Disconnected library");
    auto missingData = std::make_unique<juce::DynamicObject>();
    missingData->setProperty("version", 1);
    missingData->setProperty("roots", juce::Array<juce::var>{missingRoot.getFullPathName()});
    missingData->setProperty("active", missingRoot.getFullPathName());
    browserPrefs.replaceWithText(juce::JSON::toString(juce::var(missingData.release())));
    {
        SampleFileBrowser disconnected(*this, browserPrefs);
        if (disconnected.hasRoot() || disconnected.librariesForTest().getNumItems() != 1 ||
            !disconnected.librariesForTest().getItemText(0).contains("(missing)"))
            return juce::Result::fail("Browser disconnected root was lost or scanned");
        disconnected.librariesForTest().setSelectedId(1, juce::sendNotificationSync);
        if (disconnected.hasRoot() || !status.contains("missing"))
            return juce::Result::fail("Browser disconnected root selection failed safely");
        disconnected.forgetForTest().onClick();
        if (disconnected.librariesForTest().getNumItems() != 0 || missingRoot.exists())
            return juce::Result::fail("Browser disconnected root removal changed filesystem");
    }
    {
        SampleFileBrowser navigation(*this);
        navigation.setRoot(library);
        if (navigation.backForTest().isEnabled() || navigation.forwardForTest().isEnabled())
            return juce::Result::fail("Browser initial navigation limits");
        navigation.setRoot(secondRoot, false);
        if (!navigation.backForTest().isEnabled() || navigation.forwardForTest().isEnabled())
            return juce::Result::fail("Browser second folder navigation limits");
        navigation.backForTest().onClick();
        if (navigation.listingForTest().getDirectory() != library || navigation.backForTest().isEnabled() ||
            !navigation.forwardForTest().isEnabled())
            return juce::Result::fail("Browser actual Back handler");
        navigation.forwardForTest().onClick();
        if (navigation.listingForTest().getDirectory() != secondRoot)
            return juce::Result::fail("Browser actual Forward handler");
        if (!navigation.keyPressed(
                juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)) ||
            navigation.listingForTest().getDirectory() != library)
            return juce::Result::fail("Browser Alt+Left handler");
        if (!navigation.keyPressed(
                juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::shiftModifier, 0)) ||
            navigation.listingForTest().getDirectory() != secondRoot ||
            !navigation.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)) ||
            navigation.listingForTest().getDirectory() != library ||
            navigation.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey), &navigation.searchForTest()))
            return juce::Result::fail("Browser familiar history shortcut or text editing guard");
        navigation.setRoot(folder, false);
        navigation.setRoot(folder, false); // repeat current root must not duplicate history
        if (navigation.forwardForTest().isEnabled())
            return juce::Result::fail("Browser branch retained obsolete Forward history");
        navigation.backForTest().onClick();
        if (navigation.listingForTest().getDirectory() != library)
            return juce::Result::fail("Browser duplicate navigation entry");
        if (!navigation.keyPressed(
                juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)) ||
            navigation.listingForTest().getDirectory() != folder)
            return juce::Result::fail("Browser Alt+Right handler");
        const auto savedCustomization = customization;
        customization.unbind("browser.back", Scope::browser);
        if (customization
                .bind("browser.back", Scope::browser,
                      juce::KeyPress('H', juce::ModifierKeys::ctrlModifier, 0))
                .failed() ||
            navigation.keyPressed(
                juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)) ||
            !navigation.keyPressed(juce::KeyPress('H', juce::ModifierKeys::ctrlModifier, 0)) ||
            navigation.listingForTest().getDirectory() != library)
            return juce::Result::fail("Browser customized shortcut routing");
        customization = savedCustomization;
        navigation.forwardForTest().onClick();
        navigation.setRoot(missingRoot, false);
        if (navigation.listingForTest().getDirectory() != folder ||
            navigation.keyPressed(
                juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::ctrlModifier, 0)))
            return juce::Result::fail("Browser invalid-root/non-Browser shortcut handling");
    }
    auto &listing = fileBrowser.listingForTest();
    if (!wait([&] { return !listing.isStillLoading() && listing.getNumFiles() == 2; }))
        return juce::Result::fail("Browser directory scan/filter failed");
    fileBrowser.filterForTest("NO-MATCH");
    if (!wait([&] { return !listing.isStillLoading() && listing.getNumFiles() == 1; }) ||
        !listing.getFile(0).isDirectory())
        return juce::Result::fail("Browser file filter hid directory navigation or retained wrong files");
    fileBrowser.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (!wait(
            [&]
            {
                return fileBrowser.searchForTest().getText().isEmpty() && !listing.isStillLoading() &&
                       listing.getNumFiles() == 2;
            }))
        return juce::Result::fail("Browser actual Escape filter-clear handler");
    fileBrowser.filterForTest("KiCk");
    if (!wait([&] { return !listing.isStillLoading() && listing.getNumFiles() == 2; }))
        return juce::Result::fail("Browser case-insensitive filter");
    fileBrowser.treeForTest().setSelectedFile(sample);
    if (!wait([&] { return fileBrowser.treeForTest().getSelectedFile() == sample; }) ||
        fileBrowser.treeForTest().getDragAndDropDescription() != sample.getFullPathName())
        return juce::Result::fail("Browser actual selection/drag payload");
    const auto beforeLoad = controller.project.state();
    const auto channelCount = controller.project.channels.size();
    fileBrowser.loadForTest(sample, true);
    if (controller.project.channel(selectedChannel)->source != sample.getFullPathName() ||
        controller.project.channels.size() != channelCount)
        return juce::Result::fail("Browser Send to selected Channel");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeLoad))
        return juce::Result::fail("Browser replacement exact undo");
    fileBrowser.loadForTest(sample, false);
    if (controller.project.channels.size() != channelCount + 1 ||
        controller.project.channels.back().source != sample.getFullPathName())
        return juce::Result::fail("Browser new sample Channel");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeLoad))
        return juce::Result::fail("Browser add exact undo");
    refresh();
    audio.requestAudition(sample.getFullPathName());
    if (!wait([&] { return audio.pollAudition().wasOk() && audio.canReplayAudition(); }) ||
        audio.auditionInfo().name != sample.getFileName() || audio.auditionInfo().seconds <= 0)
        return juce::Result::fail("Browser actual asynchronous waveform completion");
    if (!wait([&] { return fileBrowser.playForTest().isEnabled(); }))
        return juce::Result::fail("Browser replay enable after decode");
    const auto beforeClick = audio.auditionRequestId();
    auto clickPoint = juce::Point<float>{8, 8};
    juce::MouseEvent sampleClick(
        source, clickPoint,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 1.f,
        0.f, 0.f, 0.f, 0.f, &fileBrowser, &fileBrowser, juce::Time::getCurrentTime(), clickPoint,
        juce::Time::getCurrentTime(), 1, false);
    focus = Scope::piano;
    fileBrowser.clickForTest(sample, sampleClick);
    if (!wait([&] { return audio.pollAudition().wasOk() && audio.auditionRequestId() > beforeClick; }) ||
        audio.fullAudition() || focus != Scope::browser)
        return juce::Result::fail("Browser already-selected Alt-click failed or changed Full default");
    const auto beforeReclick = audio.auditionRequestId();
    juce::MouseEvent normalClick(source, clickPoint,
                                 juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.f, 0.f, 0.f,
                                 0.f, 0.f, &fileBrowser, &fileBrowser, juce::Time::getCurrentTime(),
                                 clickPoint, juce::Time::getCurrentTime(), 1, false);
    focus = Scope::rack;
    fileBrowser.clickForTest(sample, normalClick);
    if (!wait([&] { return audio.pollAudition().wasOk() && audio.auditionRequestId() > beforeReclick; }) ||
        focus != Scope::browser)
        return juce::Result::fail("Browser normal reclick did not audition selected file again");
    const auto previewProject = controller.project.state();
    fileBrowser.volumeForTest().setValue(35, juce::sendNotificationSync);
    if (std::abs(audio.auditionVolume() - .35f) > .001f)
        return juce::Result::fail("Browser actual preview-volume slider handler");
    fileBrowser.fullForTest().triggerClick();
    if (!wait([&] { return audio.fullAudition() && fileBrowser.fullForTest().getToggleState(); }))
        return juce::Result::fail("Browser actual full-preview button handler");
    if (!fileBrowser.viewAction("browser.previewLouder") || std::abs(audio.auditionVolume() - .4f) > .001f ||
        !controller.project.state().isEquivalentTo(previewProject))
        return juce::Result::fail("Browser preview controls changed project or failed command");
    const auto beforeReplay = audio.auditionRequestId();
    fileBrowser.playForTest().triggerClick();
    if (!wait([&] { return audio.auditionRequestId() > beforeReplay; }))
        return juce::Result::fail("Browser Play button did not publish prepared PCM");
    const auto beforeSeek = audio.auditionRequestId();
    auto &waveform = fileBrowser.playerForTest();
    auto waveformPoint = juce::Point<float>{waveform.getWidth() * .75f, 24.f};
    juce::MouseEvent waveformClick(
        source, waveformPoint,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier), 1.f,
        0.f, 0.f, 0.f, 0.f, &waveform, &waveform, juce::Time::getCurrentTime(), waveformPoint,
        juce::Time::getCurrentTime(), 1, false);
    waveform.mouseUp(waveformClick);
    if (audio.auditionRequestId() <= beforeSeek || !controller.project.state().isEquivalentTo(previewProject))
        return juce::Result::fail("Browser waveform did not seek or changed musical document");
    fileBrowser.viewAction("browser.stopPreview");
    fileBrowser.viewAction("browser.fullPreview");
    fileBrowser.volumeForTest().setValue(70, juce::sendNotificationSync);
    // Indexed search must use the same production handlers as folder browsing.
    const auto nestedSample = secondRoot.getChildFile("Kick nested.wav");
    if (!sample.copyFileTo(nestedSample) ||
        !fileBrowser.keyPressed(juce::KeyPress('F', juce::ModifierKeys::ctrlModifier, 0)))
        return juce::Result::fail("Browser global-search fixture/Ctrl+F handler");
    // Retain the intentionally unsupported-version fixture from each run.
    // A repeated in-place qualification must start with a fresh owned file.
    const auto collectionPrefs = folder.getNonexistentChildFile("browser-collections", ".json", false);
    const auto missingFavorite = library.getChildFile("Lost favorite.wav");
    juce::String savedSearchId;
    {
        SampleFileBrowser collection(*this, collectionPrefs);
        collection.setLookAndFeel(&fileBrowser.getLookAndFeel());
        collection.setRoot(library);
        collection.viewAction("browser.find");
        collection.filterForTest("(k?ck OR snare) NOT absent type:wav");
        if (!wait(
                [&]
                {
                    const auto *data = collection.searchDataForTest();
                    return data && data->error.isEmpty() && data->matches.size() == 2;
                }))
            return juce::Result::fail("Browser actual wildcard/Boolean query");
        collection.starForTest(sample);
        collection.starForTest(missingFavorite);
        auto completeTabDialog = [&](const juce::String &name, int result)
        {
            if (!wait(dialogReady))
                return false;
            auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
            if (!dialog || !dialog->getTextEditor("name"))
                return false;
            dialog->getTextEditor("name")->setText(name, false);
            dialog->exitModalState(result);
            return wait([] { return juce::Component::getNumCurrentlyModalComponents() == 0; });
        };
        collection.collectionChoiceForTest(1);
        if (!completeTabDialog("Dry drums", 1) ||
            !wait([&] { return collection.viewsForTest().getNumTabs() == 3; }) ||
            collection.collectionsForTest().searches.size() != 1)
            return juce::Result::fail("Browser actual save-search dialog callback");
        savedSearchId = collection.collectionsForTest().searches[0].id;
        collection.collectionChoiceForTest(3);
        if (!completeTabDialog("Cancelled name", 0) ||
            collection.collectionsForTest().searches[0].name != "Dry drums")
            return juce::Result::fail("Browser rename cancellation changed saved search");
        collection.collectionChoiceForTest(3);
        if (!completeTabDialog("Dry kits", 1) ||
            !wait([&] { return collection.collectionsForTest().searches[0].name == "Dry kits"; }) ||
            collection.collectionsForTest().searches[0].id != savedSearchId)
            return juce::Result::fail("Browser rename callback lost stable search identity");
        collection.setRoot(secondRoot);
        collection.viewAction("browser.showStarred");
        if (!wait(
                [&]
                {
                    const auto *data = collection.searchDataForTest();
                    return data && data->error.isEmpty() && data->matches.size() == 2;
                }))
            return juce::Result::fail("Browser Starred view including missing reference");
        collection.filterForTest("NOT Lost");
        if (!wait(
                [&]
                {
                    const auto *data = collection.searchDataForTest();
                    return data && data->error.isEmpty() && data->matches.size() == 1 &&
                           data->matches[0].path == sample.getFullPathName().toStdString();
                }))
            return juce::Result::fail("Browser Starred search preserves original file identity");
        collection.viewsForTest().setCurrentTabIndex(2);
        if (!wait(
                [&]
                {
                    const auto *data = collection.searchDataForTest();
                    return data && data->error.isEmpty() && data->matches.size() == 2;
                }) ||
            collection.collectionsForTest().searches[0].roots !=
                std::vector<std::string>{library.getFullPathName().toStdString()})
            return juce::Result::fail("Browser saved tab restores expression and frozen library scope");
        if (!controller.project.state().isEquivalentTo(previewProject))
            return juce::Result::fail("Browser collection preferences changed musical project");
        collection.setSize(340, 620);
        for (const auto view : {1, 2})
        {
            collection.viewsForTest().setCurrentTabIndex(view);
            if (!wait(
                    [&]
                    {
                        const auto *data = collection.searchDataForTest();
                        return data && data->error.isEmpty() && data->matches.size() == 2;
                    }))
                return juce::Result::fail("Browser collection capture data");
            auto image = folder.getChildFile(view == 1 ? "browser-starred.png" : "browser-saved-search.png")
                             .createOutputStream();
            if (!image)
                return juce::Result::fail("Browser collection capture stream");
            image->setPosition(0);
            image->truncate();
            if (!png.writeImageToStream(
                    collection.createComponentSnapshot(collection.getLocalBounds(), true, 1.5f), *image))
                return juce::Result::fail("Browser collection capture PNG");
        }
        auto *tab = collection.viewsForTest().getTabButton(2);
        const juce::Point<float> menuPoint{8, 8};
        juce::MouseEvent rightClick(source, menuPoint,
                                    juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.f, 0.f,
                                    0.f, 0.f, 0.f, tab, tab, juce::Time::getCurrentTime(), menuPoint,
                                    juce::Time::getCurrentTime(), 1, false);
        collection.mouseDown(rightClick);
        if (!juce::PopupMenu::dismissAllActiveMenus())
            return juce::Result::fail("Browser tab right-click did not open context menu");
        dispatchMessages(5);
        collection.collectionChoiceForTest(1);
        if (!completeTabDialog("Temporary search", 1) ||
            !wait([&] { return collection.viewsForTest().getNumTabs() == 4; }))
            return juce::Result::fail("Browser second saved search dialog");
        collection.collectionChoiceForTest(4);
        if (collection.viewsForTest().getNumTabs() != 3 ||
            collection.collectionsForTest().searches[0].id != savedSearchId || !sample.existsAsFile() ||
            !nestedSample.existsAsFile())
            return juce::Result::fail("Browser search-tab deletion lost original tab or files");
    }
    {
        SampleFileBrowser restarted(*this, collectionPrefs);
        if (restarted.viewsForTest().getNumTabs() != 3 ||
            !restarted.collectionsForTest().starred(missingFavorite) ||
            restarted.collectionsForTest().searches[0].id != savedSearchId)
            return juce::Result::fail("Browser collections restart round trip");
        restarted.viewAction("browser.showStarred");
        if (!wait(
                [&]
                {
                    const auto *data = restarted.searchDataForTest();
                    return data && data->error.isEmpty() && data->matches.size() == 2;
                }))
            return juce::Result::fail("Browser restarted Starred results");
        restarted.starForTest(missingFavorite);
        if (missingFavorite.exists() || !sample.existsAsFile())
            return juce::Result::fail("Browser removing Starred reference touched sample files");
    }
    const auto tagPrefs = folder.getNonexistentChildFile("browser-tags", ".json", false);
    juce::String taggedSearchId, starredSearchId;
    const auto sampleHash = juce::SHA256(sample).toHexString();
    const auto sampleModified = sample.getLastModificationTime();
    {
        SampleFileBrowser tagging(*this, tagPrefs);
        tagging.setLookAndFeel(&fileBrowser.getLookAndFeel());
        tagging.setRoot(library);
        auto complete = [&](const char *field, const juce::String &text, int choice)
        {
            if (!wait(dialogReady))
                return false;
            auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
            if (!dialog || !dialog->getTextEditor(field))
                return false;
            dialog->getTextEditor(field)->setText(text, false);
            dialog->exitModalState(choice);
            return wait([] { return juce::Component::getNumCurrentlyModalComponents() == 0; });
        };
        auto tags = [&]
        { return tagging.collectionsForTest().tags->tagsFor(sample.getFullPathName().toStdString()); };
        tagging.editTagsForTest(sample, true);
        if (!complete("tags", "Drum, Tight, drum", 1) ||
            !wait([&] { return tags() == juce::StringArray{"Drum", "Tight"}; }))
            return juce::Result::fail("Browser Add tags dialog/normalization");
        tagging.editTagsForTest(sample, false);
        if (!complete("tags", "Cancelled", 0) || tags() != juce::StringArray{"Drum", "Tight"})
            return juce::Result::fail("Browser tag cancellation modified metadata");
        tagging.editTagsForTest(sample, false);
        if (tagging.setTagsForTest(sample, {"Concurrent"}).failed() || !complete("tags", "Overwrite", 1) ||
            !wait([&] { return status.contains("changed while"); }) ||
            tags() != juce::StringArray{"Concurrent"})
            return juce::Result::fail("Browser Edit tags overwrote concurrent metadata");
        tagging.editTagsForTest(sample, true);
        if (tagging.setTagsForTest(sample, {"Drum", "Organic"}).failed() || !complete("tags", "Tight", 1) ||
            !wait([&] { return tags() == juce::StringArray{"Drum", "Organic", "Tight"}; }))
            return juce::Result::fail("Browser Add tags did not merge latest associations");
        tagging.editTagsForTest(sample, false);
        if (!complete("tags", "Drum, Tight", 1) ||
            !wait([&] { return tags() == juce::StringArray{"Drum", "Tight"}; }) ||
            tagging.setTagsForTest(nestedSample, {"Drum", "Loose"}).failed() ||
            tagging.setTagsForTest(missingFavorite, {"Drum", "Missing"}).failed())
            return juce::Result::fail("Browser Edit tags replacement/file association");
        tagging.editTagsForTest(sample, false);
        if (!complete("tags", juce::String::repeatedString("x", 65), 1) ||
            !wait([&] { return status.contains("Tag names"); }) ||
            tags() != juce::StringArray{"Drum", "Tight"})
            return juce::Result::fail("Browser invalid tag edit changed metadata");
        tagging.viewAction("browser.showTags");
        tagging.tagNameSearchForTest().setText("tIgHt", false);
        tagging.tagNameSearchForTest().onTextChange();
        if (tagging.tagListForTest().getModel()->getNumRows() != 1)
            return juce::Result::fail("Browser tag chooser name filter");
        tagging.tagListForTest().getModel()->listBoxItemClicked(0, normalClick);
        auto matches = [&](int count)
        {
            const auto *data = tagging.searchDataForTest();
            return data && data->error.isEmpty() && int(data->matches.size()) == count;
        };
        if (!wait([&] { return matches(1); }) ||
            tagging.searchDataForTest()->matches[0].path != sample.getFullPathName().toStdString())
            return juce::Result::fail("Browser tag checkbox did not filter actual sample paths");
        tagging.tagNameSearchForTest().setText("Drum", false);
        tagging.tagNameSearchForTest().onTextChange();
        if (tagging.tagListForTest().getModel()->getNumRows() != 1)
            return juce::Result::fail("Browser tag chooser second name");
        tagging.tagListForTest().selectRow(0);
        if (!tagging.tagListForTest().keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) ||
            !wait([&] { return matches(2); }))
            return juce::Result::fail("Browser keyboard tag selection/ANY union");
        tagging.tagLogicForTest().onClick();
        if (!wait([&] { return matches(1); }) || !tagging.tagFilterForTest().all ||
            tagging.searchDataForTest()->matches[0].tags.size() != 2)
            return juce::Result::fail("Browser ALL button intersection/metadata labels");
        tagging.collectionChoiceForTest(1);
        if (!complete("name", "Tight drums", 1) ||
            !wait([&] { return tagging.collectionsForTest().searches.size() == 1; }))
            return juce::Result::fail("Browser tag-only saved search dialog");
        taggedSearchId = tagging.collectionsForTest().searches[0].id;
        tagging.tagLogicForTest().onClick();
        tagging.viewsForTest().setCurrentTabIndex(0);
        tagging.viewsForTest().setCurrentTabIndex(2);
        if (!wait([&] { return matches(1); }) || !tagging.tagFilterForTest().all)
            return juce::Result::fail("Browser saved tag-filter snapshot mutated by temporary edits");
        tagging.starForTest(sample);
        tagging.starForTest(missingFavorite);
        tagging.viewAction("browser.showStarred");
        if (tagging.filterTagsForTest({{"Missing"}, false}).failed() || !wait([&] { return matches(1); }) ||
            !tagging.searchDataForTest()->matches[0].missing)
            return juce::Result::fail("Browser Starred tagged missing reference");
        tagging.collectionChoiceForTest(1);
        if (!complete("name", "Missing favorites", 1) ||
            !wait([&] { return tagging.collectionsForTest().searches.size() == 2; }) ||
            !tagging.collectionsForTest().searches.back().starred ||
            !tagging.collectionsForTest().searches.back().roots.empty())
            return juce::Result::fail("Browser saved Starred tag-only scope");
        starredSearchId = tagging.collectionsForTest().searches.back().id;
        tagging.viewsForTest().setCurrentTabIndex(2);
        if (!wait([&] { return matches(1); }))
            return juce::Result::fail("Browser tagged render readiness");
        tagging.tagNameSearchForTest().setText({}, false);
        tagging.tagNameSearchForTest().onTextChange();
        tagging.setSize(340, 760);
        auto picture = folder.getChildFile("browser-tags-all.png").createOutputStream();
        if (!picture)
            return juce::Result::fail("Browser tags render stream");
        picture->setPosition(0);
        picture->truncate();
        if (!png.writeImageToStream(tagging.createComponentSnapshot(tagging.getLocalBounds(), true, 1.5f),
                                    *picture))
            return juce::Result::fail("Browser tags render");
        const auto longTag = juce::String::repeatedString("Long", 16);
        if (tagging.setTagsForTest(missingFavorite, {"Drum", "Missing", longTag}).failed())
            return juce::Result::fail("Browser long tag layout fixture");
        tagging.tagNameSearchForTest().setText(longTag, false);
        tagging.tagNameSearchForTest().onTextChange();
        tagging.setSize(190, 160);
        if (!tagging.tagListForTest().getViewport()->canScrollHorizontally() ||
            !tagging.viewportForTest().canScrollVertically())
            return juce::Result::fail("Browser tag panel horizontal/vertical overflow missing");
        tagging.viewportForTest().setViewPosition(0, 100000);
        if (tagging.playerForTest().getBottom() >
            tagging.viewportForTest().getViewPositionY() + tagging.viewportForTest().getHeight())
            return juce::Result::fail("Browser expanded tag panel hides preview beyond scrollbar reach");
        if (!controller.project.state().isEquivalentTo(previewProject) ||
            juce::SHA256(sample).toHexString() != sampleHash ||
            sample.getLastModificationTime() != sampleModified)
            return juce::Result::fail("Browser tags changed project/sample content or timestamp");
    }
    {
        SampleFileBrowser restarted(*this, tagPrefs);
        auto matches = [&](int count)
        {
            const auto *data = restarted.searchDataForTest();
            return data && data->error.isEmpty() && int(data->matches.size()) == count;
        };
        if (!wait([&] { return matches(1); }) || !restarted.tagFilterForTest().all ||
            restarted.collectionsForTest().searches.size() != 2 ||
            restarted.collectionsForTest().searches[0].id != taggedSearchId ||
            restarted.collectionsForTest().searches[1].id != starredSearchId)
            return juce::Result::fail("Browser v3 restart filter/catalog/stable-ID round trip");
        restarted.viewsForTest().setCurrentTabIndex(3);
        if (!wait([&] { return matches(1); }) || !restarted.searchDataForTest()->matches[0].missing)
            return juce::Result::fail("Browser restarted saved Starred tag filter");
        restarted.viewAction("browser.clearSearch");
        if (!wait([&] { return matches(2); }) || !restarted.tagFilterForTest().empty() ||
            restarted.collectionsForTest().tags->tagsFor(sample.getFullPathName().toStdString()).size() != 2)
            return juce::Result::fail("Browser Escape/clear removes filters without removing file tags");
        if (restarted.collectionsForTest().searches[1].filter.empty())
            return juce::Result::fail("Browser clear operation overwrote stored tag definition");
        restarted.viewsForTest().setCurrentTabIndex(0);
        restarted.viewsForTest().setCurrentTabIndex(3);
        if (!wait([&] { return matches(1); }) || restarted.tagFilterForTest().empty())
            return juce::Result::fail("Browser clear operation overwrote saved tag definition");
    }
    const auto legacyTagPrefs = folder.getNonexistentChildFile("browser-tags-v2", ".json", false);
    browser_search::Collections oldCollections;
    oldCollections.toggle(sample);
    if (oldCollections.save("Old kicks", "kick", {library.getFullPathName().toStdString()}).failed())
        return juce::Result::fail("Browser v2 migration fixture");
    const auto oldSearchId = oldCollections.searches[0].id;
    auto oldData = oldCollections.state();
    oldData.getDynamicObject()->setProperty("version", 2);
    for (const auto *property : {"fileTags", "allTagFilter", "starredTagFilter"})
        oldData.getDynamicObject()->removeProperty(property);
    for (auto &entry : *oldData["searches"].getArray())
    {
        entry.getDynamicObject()->removeProperty("filter");
        entry.getDynamicObject()->removeProperty("starred");
    }
    oldData.getDynamicObject()->setProperty("roots", juce::Array<juce::var>{library.getFullPathName()});
    oldData.getDynamicObject()->setProperty("active", library.getFullPathName());
    if (!legacyTagPrefs.replaceWithText(juce::JSON::toString(oldData)))
        return juce::Result::fail("Browser v2 fixture write");
    {
        SampleFileBrowser migrated(*this, legacyTagPrefs);
        if (!migrated.collectionsForTest().starred(sample) ||
            migrated.collectionsForTest().searches.size() != 1 ||
            migrated.collectionsForTest().searches[0].id != oldSearchId ||
            !migrated.collectionsForTest().tags->allTags().isEmpty())
            return juce::Result::fail("Browser actual v2 preference-file migration lost original records");
        if (migrated.setTagsForTest(sample, {"Drum"}).failed() ||
            int(juce::JSON::parse(legacyTagPrefs)["version"]) != 3)
            return juce::Result::fail("Browser migrated file did not write protected v3 metadata");
    }
    {
        SampleFileBrowser migrated(*this, legacyTagPrefs);
        if (migrated.collectionsForTest().searches[0].id != oldSearchId ||
            !migrated.collectionsForTest().starred(sample) ||
            migrated.collectionsForTest().tags->tagsFor(sample.getFullPathName().toStdString()) !=
                juce::StringArray{"Drum"})
            return juce::Result::fail("Browser migrated metadata/favorite/tab restart");
    }
    const auto unsupported = "{\"version\":99,\"roots\":[],\"active\":\"\"}";
    if (!collectionPrefs.replaceWithText(unsupported))
        return juce::Result::fail("Browser unsupported prefs fixture");
    {
        SampleFileBrowser unsupportedBrowser(*this, collectionPrefs);
        unsupportedBrowser.setRoot(library);
        if (collectionPrefs.loadFileAsString() != unsupported)
            return juce::Result::fail("Browser overwrote unsupported preferences");
        if (unsupportedBrowser.setTagsForTest(sample, {"Unsaved"}).wasOk() ||
            collectionPrefs.loadFileAsString() != unsupported)
            return juce::Result::fail("Browser tag write overwrote unsupported preferences");
    }
    fileBrowser.filterForTest("kick type:wav");
    if (!wait(
            [&]
            {
                const auto *data = fileBrowser.searchDataForTest();
                return data && data->error.isEmpty() && data->matches.size() == 2;
            }))
        return juce::Result::fail("Browser recursive search did not find both sample rows");
    auto &results = fileBrowser.resultsForTest();
    const auto searchProject = controller.project.state();
    const auto searchAudition = audio.auditionRequestId();
    results.selectRow(0);
    if (!wait([&] { return audio.pollAudition().wasOk() && audio.auditionRequestId() > searchAudition; }))
        return juce::Result::fail("Browser search-row selection did not audition");
    const auto rowFile = juce::File(fileBrowser.searchDataForTest()->matches[0].path);
    juce::SparseSet<int> rowSet;
    rowSet.addRange({0, 1});
    if (results.getModel()->getDragSourceDescription(rowSet).toString() != rowFile.getFullPathName() ||
        !fileBrowser.keyPressed(juce::KeyPress(juce::KeyPress::F3Key)) || results.getSelectedRow() != 1 ||
        !fileBrowser.keyPressed(juce::KeyPress(juce::KeyPress::F2Key)) || results.getSelectedRow() != 0 ||
        !fileBrowser.keyPressed(juce::KeyPress(juce::KeyPress::F3Key)) || results.getSelectedRow() != 1 ||
        !fileBrowser.keyPressed(
            juce::KeyPress(juce::KeyPress::F3Key, juce::ModifierKeys::shiftModifier, 0)) ||
        results.getSelectedRow() != 0)
        return juce::Result::fail("Browser search drag/F3/Shift+F3 routing");
    juce::MouseEvent middleClick(source, clickPoint,
                                 juce::ModifierKeys(juce::ModifierKeys::middleButtonModifier), 1.f, 0.f, 0.f,
                                 0.f, 0.f, &results, &results, juce::Time::getCurrentTime(), clickPoint,
                                 juce::Time::getCurrentTime(), 1, false);
    results.getModel()->listBoxItemClicked(0, middleClick);
    if (controller.project.channel(selectedChannel)->source != rowFile.getFullPathName())
        return juce::Result::fail("Browser search middle-click Send to selected");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(searchProject))
        return juce::Result::fail("Browser search replacement Undo");
    results.getModel()->listBoxItemDoubleClicked(0, normalClick);
    if (controller.project.channels.size() != channelCount + 1 ||
        controller.project.channels.back().source != rowFile.getFullPathName())
        return juce::Result::fail("Browser search double-click creates sample Channel");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(searchProject))
        return juce::Result::fail("Browser search new Channel Undo");
    fileBrowser.filterForTest("folder:\"" + secondRoot.getFullPathName() + "\" type:wav");
    if (!wait(
            [&]
            {
                const auto *data = fileBrowser.searchDataForTest();
                return data && data->error.isEmpty() && data->matches.size() == 1 &&
                       data->matches[0].path == nestedSample.getFullPathName().toStdString();
            }))
        return juce::Result::fail("Browser actual scoped search");
    fileBrowser.filterForTest("kick type:wav");
    if (!wait(
            [&]
            {
                const auto *data = fileBrowser.searchDataForTest();
                return data && data->error.isEmpty() && data->matches.size() == 2;
            }))
        return juce::Result::fail("Browser search restored for captures");
    refresh();
    {
        SampleFileBrowser compactBrowser(*this);
        compactBrowser.setSize(160, 130);
        auto &viewport = compactBrowser.viewportForTest();
        if (!viewport.canScrollVertically() || compactBrowser.treeForTest().getHeight() < 40 ||
            compactBrowser.playForTest().getWidth() < 24)
            return juce::Result::fail("Browser compact overflow or preview targets");
        if (compactBrowser.volumeForTest().getParentComponent() != &compactBrowser ||
            !compactBrowser.getLocalBounds().contains(compactBrowser.volumeForTest().getBounds()) ||
            compactBrowser.volumeForTest().getBounds().intersects(viewport.getBounds()) ||
            compactBrowser.playForTest().getBounds().intersects(viewport.getBounds()))
            return juce::Result::fail(
                "Browser audition transport must remain reachable outside scrolling content");
        viewport.setViewPosition(0, 250);
        if (viewport.getViewPositionY() <= 0 ||
            compactBrowser.playerForTest().getBottom() > viewport.getViewPositionY() + viewport.getHeight())
            return juce::Result::fail("Browser waveform inaccessible in overflow viewport");
    }
    if (const auto *pattern = controller.project.pattern(selectedPattern); pattern && !pattern->notes.empty())
    {
        selectedChannel = pattern->notes.front().channel;
        refresh();
        piano->viewAction("piano.fit");
    }
    const auto savedSize = getLocalBounds();
    for (const auto viewport : {juce::Point<int>{1280, 684}, juce::Point<int>{2560, 1356}})
    {
        setSize(viewport.x, viewport.y);
        resized();
        if (!fileBrowser.isVisible() || !fileBrowser.backForTest().isVisible() ||
            fileBrowser.backForTest().findParentComponentOfClass<SampleFileBrowser>() != &fileBrowser ||
            !fileBrowser.forwardForTest().isVisible() ||
            fileBrowser.forwardForTest().findParentComponentOfClass<SampleFileBrowser>() != &fileBrowser ||
            fileBrowser.getBottom() > browser->getHeight() - 100 ||
            fileBrowser.treeForTest().getHeight() < 40 || !fileBrowser.fullForTest().isVisible() ||
            !fileBrowser.volumeForTest().isVisible() || !fileBrowser.playerForTest().isVisible())
            return juce::Result::fail("Browser compact/4K layout overlaps footer or hides navigation");
        auto capture =
            folder
                .getChildFile(viewport.x == 1280 ? "browser-files-1080p-150.png" : "browser-files-4k-150.png")
                .createOutputStream();
        if (!capture)
            return juce::Result::fail("Browser layout capture stream");
        capture->setPosition(0);
        capture->truncate();
        if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f), *capture))
            return juce::Result::fail("Browser layout capture");
    }
    setSize(savedSize.getWidth(), savedSize.getHeight());
    resized();
    auto workspace = folder.getChildFile("midi-imported-workspace.png").createOutputStream();
    if (!workspace)
        return juce::Result::fail("MIDI workspace output");
    workspace->setPosition(0);
    workspace->truncate();
    if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds()), *workspace))
        return juce::Result::fail("MIDI workspace PNG");
    folder.getChildFile("midi-dialog-verification.txt")
        .replaceWithText(
            "PASS: actual import form defaults/filters, file encode/atomic write/read/decode, guarded "
            "application import, undo/redo/project persistence, actual asynchronous worker/modal callbacks, "
            "Piano/Rack drop handlers, cancellation/stale rejection, customized-tool semantics and populated "
            "component PNG. Browser wildcard/Boolean queries, Starred references/missing files, saved "
            "query/root scope and restart, stable tab IDs, actual Save/Rename/Cancel/Delete modal/menu "
            "callbacks, right-click tab menu, unchanged musical document and unsupported-preferences "
            "preservation passed. Add/Edit tag modal callbacks, cancellation/conflict/merge/refusal, "
            "checkbox/keyboard tag selection, ANY/ALL, tag-only/saved-Starred scopes, v3 restart and "
            "unchanged sample bytes/timestamp passed. "
            "No native file picker, physical input, playback or listening qualification.\n");

    // MCP importMidi: controllers and the optional file tempo map in one Undo step.
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(480);
        juce::MidiMessageSequence conductor, part;
        conductor.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0); // 120 -> 96 BPM at beat 8
        conductor.addEvent(juce::MidiMessage::tempoMetaEvent(625000), 480 * 8);
        conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(6, 8), 0);
        part.addEvent(juce::MidiMessage::controllerEvent(1, 74, 64), 0);
        part.addEvent(juce::MidiMessage::noteOn(1, 64, juce::uint8(90)), 0);
        part.addEvent(juce::MidiMessage::noteOff(1, 64), 480);
        midi.addTrack(conductor);
        midi.addTrack(part);
        const auto file = folder.getChildFile("mcp-import.mid");
        file.deleteFile();
        {
            juce::FileOutputStream out(file);
            if (!out.openedOk() || !midi.writeTo(out, 1))
                return juce::Result::fail("MCP MIDI fixture");
        }
        const auto beforeMcp = controller.project.state();
        auto args = juce::JSON::parse("{\"action\":\"importMidi\",\"useTempo\":true}");
        args.getDynamicObject()->setProperty("path", file.getFullPathName());
        juce::var response;
        try
        {
            response = mcpCall("vibera_project", args)["structuredContent"];
        }
        catch (const std::exception &e)
        {
            return juce::Result::fail(juce::String("MCP importMidi refused: ") + e.what());
        }
        const auto *target = controller.project.pattern(selectedPattern);
        if (int(response["notes"]) != 1 || int(response["controllers"]) != 1 || !target ||
            std::none_of(target->controllers.begin(), target->controllers.end(),
                         [&](const auto &c) { return c.number == 74 && c.channel == selectedChannel; }) ||
            controller.project.tempo != 120 || controller.project.tempoChanges.size() != 1 ||
            std::abs(controller.project.tempoChanges[0].bpm - 96) > .01 || controller.project.numerator != 6 ||
            controller.project.denominator != 8)
            return juce::Result::fail("MCP importMidi result: " + juce::JSON::toString(response, true));
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(beforeMcp))
            return juce::Result::fail("MCP importMidi should be one Undo step");
        args.getDynamicObject()->setProperty("path", folder.getChildFile("missing.mid").getFullPathName());
        bool refused = false;
        try
        {
            mcpCall("vibera_project", args);
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        if (!refused)
            return juce::Result::fail("MCP importMidi accepted a missing file");
    }

    // MCP missingMedia/relinkMedia: list a missing sample, then find it in a folder (one Undo).
    {
        const auto library = folder.getChildFile("relink-library");
        library.createDirectory();
        const auto sample = library.getChildFile("relink-me.wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(sample.createOutputStream().release(), 48000, 1, 16, {}, 0));
            juce::AudioBuffer<float> tone(1, 480);
            tone.clear();
            tone.setSample(0, 10, .5f);
            if (!writer || !writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples()))
                return juce::Result::fail("Relink fixture");
        }
        auto *target = controller.project.channel(selectedChannel);
        if (!target || target->instrument || target->audioClip)
            return juce::Result::fail("Relink check needs a sampler Channel selected");
        const auto beforeMissing = controller.project.state();
        CommandArgs load; // A sample Channel (built-in sounds are never "missing").
        load.channel = selectedChannel;
        load.text = sample.getFullPathName();
        command("channel.source", load);
        CommandArgs move;
        move.relinks = {{selectedChannel, folder.getChildFile("gone").getChildFile("relink-me.wav").getFullPathName()}};
        command("media.relink", move);
        auto args = juce::JSON::parse("{\"action\":\"missingMedia\"}");
        const auto listed = mcpCall("vibera_project", args)["structuredContent"]["missing"];
        if (!listed.isArray() || listed.size() != 1 || juce::int64(listed[0]["channel"]) != selectedChannel)
            return juce::Result::fail("MCP missingMedia should list the moved sample: " + juce::JSON::toString(listed));
        args.getDynamicObject()->setProperty("action", "relinkMedia");
        args.getDynamicObject()->setProperty("path", library.getFullPathName());
        const auto relinked = mcpCall("vibera_project", args)["structuredContent"]["missing"];
        if (!relinked.isArray() || relinked.size() != 0 ||
            controller.project.channel(selectedChannel)->source != sample.getFullPathName())
            return juce::Result::fail("MCP relinkMedia should find the sample in the folder");
        for (int step = 0; step < 3; ++step)
            controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(beforeMissing))
            return juce::Result::fail("Relinking should undo exactly");
    }
    return juce::Result::ok();
}
juce::Result Studio::verifyAudioPlaylist(const juce::File &folder)
{
    if (folder.createDirectory().failed())
        return juce::Result::fail("Audio Playlist evidence directory");
    folder.getChildFile("audio-playlist-failure.txt").deleteFile();
    controller.replace(Project::demo());
    refresh();
    const auto source = folder.getChildFile("original-wave-45s.wav");
    {
        juce::AudioBuffer<float> pcm(2, 24000 * 45);
        for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
            for (int ch = 0; ch < 2; ++ch)
                pcm.setSample(
                    ch, frame,
                    float(.25 * std::sin(frame * .04) * (.25 + .75 * std::abs(std::sin(frame * .0003)))));
        auto stream = source.createOutputStream();
        if (!stream)
            return juce::Result::fail("Audio source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 24000, 2, 16, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()))
            return juce::Result::fail("Audio source fixture");
    }
    auto *playlist = static_cast<Editor *>(windows[2]->getContentComponent());
    showEditor(2);
    const auto before = controller.project.state();
    const auto count = controller.project.audioPlacements.size();
    playlist->filesDroppedWithModifiers({source.getFullPathName()}, 190, 100, {});
    const auto deadline = juce::Time::getMillisecondCounter() + 8000;
    while (controller.project.audioPlacements.size() == count &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        pollAudioImports();
        dispatchMessages(2);
    }
    if (controller.project.audioPlacements.size() != count + 1)
        return juce::Result::fail("Actual Playlist drop/worker completion");
    const auto imported = controller.project.audioPlacements.back();
    auto *channel = controller.project.channel(imported.channel);
    if (!channel || !channel->audioClip || std::abs(channel->audioDuration - 45.) > .001 ||
        selectedClips != std::vector<Id>{imported.id})
        return juce::Result::fail("Long Audio Clip metadata/selection");
    const auto after = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return juce::Result::fail("Import single undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(after))
        return juce::Result::fail("Import redo");
    focus = Scope::playlist;
    selectedClips = {imported.id};
    keyPressed(juce::KeyPress('b', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 0));
    if (controller.project.audioPlacements.size() != count + 2 || selectedClips.size() != 1 ||
        selectedClips.front() == imported.id ||
        controller.project.audioPlacements.back().channel != imported.channel)
        return juce::Result::fail("Audio Ctrl+B linked duplicate/selection");
    controller.undoEdit();
    CommandArgs trim;
    trim.target = imported.id;
    trim.offset = 35;
    trim.length = 4;
    command("audio.trim", trim);
    if (controller.project.audioPlacements.back().offset != 35 ||
        controller.project.audioPlacements.back().duration != 4)
        return juce::Result::fail("Source trim beyond sampler limit");
    auto drag = [&](juce::Point<float> start, juce::Point<float> end)
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&](juce::Point<float> point, bool moved)
        {
            return juce::MouseEvent(source, point, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                    1.f, 0.f, 0.f, 0.f, 0.f, playlist, playlist, juce::Time::getCurrentTime(),
                                    start, juce::Time::getCurrentTime(), 1, moved);
        };
        playlist->mouseDown(event(start, false));
        playlist->mouseDrag(event(end, true));
        playlist->mouseUp(event(end, true));
    };
    auto trimBefore = controller.project.state();
    auto bounds = playlist->playlistClipBounds(imported.id);
    drag({bounds.getX() + 2, bounds.getCentreY()}, {bounds.getX() + 18, bounds.getCentreY()});
    if (controller.project.audioPlacements.back().offset <= 35 ||
        controller.project.audioPlacements.back().beat <= imported.beat)
        return juce::Result::fail("Actual left-edge source trim handler");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(trimBefore))
        return juce::Result::fail("Left trim one undo");
    bounds = playlist->playlistClipBounds(imported.id);
    drag({bounds.getRight() - 2, bounds.getCentreY()}, {bounds.getRight() - 18, bounds.getCentreY()});
    if (controller.project.audioPlacements.back().duration >= 4)
        return juce::Result::fail("Actual right-edge trim handler");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(trimBefore))
        return juce::Result::fail("Right trim one undo");
    focus = Scope::playlist;
    selectedClips = {imported.id};
    keyPressed(juce::KeyPress('s', {}, 0));
    const auto beforeSlip = controller.project.state();
    bounds = playlist->playlistClipBounds(imported.id);
    drag(bounds.getCentre(), bounds.getCentre().translated(16, 0));
    if (controller.project.audioPlacements.back().offset >= 35 ||
        controller.project.audioPlacements.back().beat != imported.beat ||
        controller.project.audioPlacements.back().duration != 4)
        return juce::Result::fail(
            "Actual contextual S/Slip handler changed timeline or failed to move source");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeSlip))
        return juce::Result::fail("Slip gesture single undo");
    keyPressed(juce::KeyPress('c', {}, 0));
    bounds = playlist->playlistClipBounds(imported.id);
    const auto beforeCut = controller.project.state();
    drag(bounds.getCentre(), bounds.getCentre().translated(0, 12));
    if (controller.project.audioPlacements.size() != 2 ||
        controller.project.audioPlacements.front().channel !=
            controller.project.audioPlacements.back().channel ||
        std::abs(controller.project.audioPlacements.front().duration +
                 controller.project.audioPlacements.back().duration - 4.) > .000001)
        return juce::Result::fail("Actual contextual C/Slice handler linked halves");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeCut))
        return juce::Result::fail("Slice gesture single undo");
    // Escape must cancel the editor state too, so a later mouse-up cannot commit.
    keyPressed(juce::KeyPress('c', {}, 0));
    bounds = playlist->playlistClipBounds(imported.id);
    const auto cancelled = controller.project.state();
    const auto mouseSource = juce::Desktop::getInstance().getMainMouseSource();
    auto pointer =
        [&](juce::Point<float> point, juce::Point<float> origin, juce::ModifierKeys mods, bool moved = false)
    {
        return juce::MouseEvent(mouseSource, point, mods, 1.f, 0.f, 0.f, 0.f, 0.f, playlist, playlist,
                                juce::Time::getCurrentTime(), origin, juce::Time::getCurrentTime(), 1, moved);
    };
    const juce::ModifierKeys leftButton(juce::ModifierKeys::leftButtonModifier);
    const auto middle = bounds.getCentre();
    playlist->mouseDown(pointer(middle, middle, leftButton));
    keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    playlist->mouseUp(pointer(middle, middle, leftButton));
    if (playlist->pointerGestureActive() || !controller.project.state().isEquivalentTo(cancelled))
        return juce::Result::fail("Escape left a pending slice that committed on mouse-up");
    // Instant Shift+Alt cut must retain an off-grid cut position.
    const auto offGrid = juce::Point<float>{bounds.getX() + bounds.getWidth() * .4375f + 3.f, middle.y};
    const juce::ModifierKeys instant(juce::ModifierKeys::leftButtonModifier |
                                     juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier);
    playlist->mouseDown(pointer(offGrid, offGrid, instant));
    playlist->mouseUp(pointer(offGrid, offGrid, instant));
    if (controller.project.audioPlacements.size() != 2 ||
        std::abs(controller.project.audioPlacements.back().beat - playlist->beatUnder(offGrid.x)) > .000001)
        return juce::Result::fail("Shift+Alt instant slice did not bypass snap");
    controller.undoEdit();
    // A diagonal line crosses both the linked Pattern and Audio Clip lanes.
    const auto multiBefore = controller.project.state();
    const auto patternId = controller.project.placements.front().id;
    const auto patternBounds = playlist->playlistClipBounds(patternId);
    bounds = playlist->playlistClipBounds(imported.id);
    const auto sharedLeft = std::max(patternBounds.getX(), bounds.getX());
    const auto sharedRight = std::min(patternBounds.getRight(), bounds.getRight());
    const auto start =
        juce::Point<float>{sharedLeft + (sharedRight - sharedLeft) * .3f, patternBounds.getCentreY()};
    const auto end = juce::Point<float>{sharedLeft + (sharedRight - sharedLeft) * .6f, bounds.getCentreY()};
    playlist->mouseDown(pointer(start, start, leftButton));
    const juce::ModifierKeys diagonal(juce::ModifierKeys::leftButtonModifier |
                                      juce::ModifierKeys::shiftModifier);
    playlist->mouseDrag(pointer(end, start, diagonal, true));
    playlist->mouseUp(pointer(end, start, diagonal, true));
    if (controller.project.placements.size() != 5 || controller.project.audioPlacements.size() != 2 ||
        controller.project.audioPlacements.back().beat <= controller.project.placements.back().beat)
        return juce::Result::fail("Diagonal slice did not cut two lanes at distinct increasing times");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(multiBefore))
        return juce::Result::fail("Multi-lane slice one undo");
    // Slip a trimmed Pattern and Audio Clip together using their common source limit.
    const auto patternLength =
        controller.project.pattern(controller.project.placements.front().pattern)->length;
    controller.project.placements.front().length = 2;
    controller.project.placements.front().offset = 1;
    controller.notify();
    refresh();
    const auto groupBefore = controller.project.state();
    const auto originalAudioOffset = controller.project.audioPlacements.front().offset;
    selectedClips = {patternId, imported.id};
    keyPressed(juce::KeyPress('s', {}, 0));
    bounds = playlist->playlistClipBounds(imported.id);
    drag(bounds.getCentre(), bounds.getCentre().translated(16, 0));
    const auto patternDelta = controller.project.placements.front().offset - 1;
    const auto audioDelta = (controller.project.audioPlacements.front().offset - originalAudioOffset) *
                            controller.project.tempo / 60.;
    if (patternDelta >= 0 || std::abs(patternDelta - audioDelta) > .000001 ||
        controller.project.placements.front().length != 2)
        return juce::Result::fail("Grouped mixed-type slip lost source alignment or timeline bounds");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(groupBefore))
        return juce::Result::fail("Grouped slip one undo");
    controller.project.placements.front().length = patternLength;
    controller.project.placements.front().offset = 0;
    controller.notify();
    refresh();
    keyPressed(juce::KeyPress('t', {}, 0));
    const auto beforeMute = controller.project.state();
    drag(middle, middle);
    if (!controller.project.audioPlacements.front().muted ||
        controller.project.channel(imported.channel)->muted)
        return juce::Result::fail("Actual T/Mute handler failed independent placement state");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeMute))
        return juce::Result::fail("Mute gesture single undo");
    // Dedicated selection/erase/zoom tools operate on mixed clip types.
    const auto toolsBefore = controller.project.state();
    auto patternTarget = playlist->playlistClipBounds(patternId);
    auto audioTarget = playlist->playlistClipBounds(imported.id);
    keyPressed(juce::KeyPress('e', {}, 0));
    const auto patternPoint = patternTarget.getCentre(), audioPoint = audioTarget.getCentre();
    auto clickWith = [&](juce::Point<float> point, juce::ModifierKeys mods)
    {
        playlist->mouseDown(pointer(point, point, mods));
        playlist->mouseUp(pointer(point, point, mods));
    };
    clickWith(patternPoint, leftButton);
    if (selectedClips != std::vector<Id>{patternId})
        return juce::Result::fail("E/Select click moves or fails to select clip");
    const juce::ModifierKeys addSelection(juce::ModifierKeys::leftButtonModifier |
                                          juce::ModifierKeys::shiftModifier);
    clickWith(audioPoint, addSelection);
    if (selectedClips.size() != 2 || !selected(selectedClips, imported.id))
        return juce::Result::fail("Shift-select mixed audio/pattern clips");
    clickWith(patternPoint, addSelection);
    if (selectedClips != std::vector<Id>{imported.id})
        return juce::Result::fail("Shift-select toggle removal");
    const auto marquee = patternTarget.getUnion(audioTarget).reduced(1.f);
    drag(marquee.getTopLeft(), marquee.getBottomRight());
    if (!selected(selectedClips, imported.id) || !selected(selectedClips, patternId) ||
        !controller.project.state().isEquivalentTo(toolsBefore))
        return juce::Result::fail("E/Select mixed marquee changed document or missed clips");
    keyPressed(juce::KeyPress('d', {}, 0));
    drag(patternPoint, audioPoint);
    if (controller.project.placements.size() + 1 !=
            toolsBefore.getChildWithName("PLACEMENTS").getNumChildren() ||
        !controller.project.audioPlacements.empty())
        return juce::Result::fail("D/Delete mixed gesture");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(toolsBefore))
        return juce::Result::fail("Delete tool grouped undo");
    controller.redoEdit();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(toolsBefore))
        return juce::Result::fail("Delete tool redo/undo");
    playlist->mouseDown(pointer(patternPoint, patternPoint, leftButton));
    keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    playlist->mouseUp(pointer(patternPoint, patternPoint, leftButton));
    if (!controller.project.state().isEquivalentTo(toolsBefore) || playlist->pointerGestureActive())
        return juce::Result::fail("Escape cancelled erase still commits");
    // Shift-drag in Delete tool selects instead of erasing.
    playlist->mouseDown(pointer(marquee.getTopLeft(), marquee.getTopLeft(), addSelection));
    playlist->mouseDrag(pointer(marquee.getBottomRight(), marquee.getTopLeft(), addSelection, true));
    playlist->mouseUp(pointer(marquee.getBottomRight(), marquee.getTopLeft(), addSelection, true));
    if (!selected(selectedClips, imported.id) || !controller.project.state().isEquivalentTo(toolsBefore))
        return juce::Result::fail("Delete Shift marquee erased content");
    const auto originalBounds = playlist->playlistClipBounds(imported.id);
    keyPressed(juce::KeyPress('z', juce::ModifierKeys::shiftModifier, 0));
    drag(marquee.getTopLeft(), marquee.getBottomRight());
    const auto zoomedBounds = playlist->playlistClipBounds(imported.id);
    if (zoomedBounds.getWidth() <= originalBounds.getWidth() ||
        !controller.project.state().isEquivalentTo(toolsBefore))
        return juce::Result::fail("Shift+Z region zoom changed project or did not zoom");
    juce::PNGImageFormat toolsPng;
    auto saveToolImage = [&](const char *name)
    {
        auto output = folder.getChildFile(name).createOutputStream();
        if (!output)
            return false;
        output->setPosition(0);
        output->truncate();
        return toolsPng.writeImageToStream(playlist->createComponentSnapshot(playlist->getLocalBounds()),
                                           *output);
    };
    if (!saveToolImage("playlist-region-zoom.png"))
        return juce::Result::fail("Region zoom screenshot");
    clickWith(playlist->playlistGridBounds().getCentre().toFloat(),
              juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier));
    if (playlist->playlistClipBounds(imported.id) != originalBounds)
        return juce::Result::fail("Zoom right-click did not restore exact viewport");
    keyPressed(juce::KeyPress('5', juce::ModifierKeys::shiftModifier, 0));
    keyPressed(juce::KeyPress('4', juce::ModifierKeys::shiftModifier, 0));
    if (playlist->playlistClipBounds(imported.id) != originalBounds)
        return juce::Result::fail("Selection zoom/out restore");
    const auto normalSize = juce::Point<int>{playlist->getWidth(), playlist->getHeight()};
    playlist->setSize(360, 300);
    std::vector<juce::Rectangle<int>> controls;
    for (int child = 0; child < playlist->getNumChildComponents(); ++child)
        if (auto *button = dynamic_cast<juce::TextButton *>(playlist->getChildComponent(child));
            button && button->isVisible())
        {
            if (!playlist->getLocalBounds().contains(button->getBounds()))
                return juce::Result::fail("Wrapped Playlist toolbar clipped");
            for (auto previous : controls)
                if (previous.intersects(button->getBounds()))
                    return juce::Result::fail("Wrapped Playlist toolbar overlap");
            controls.push_back(button->getBounds());
        }
    if (!saveToolImage("playlist-wrapped-tools.png"))
        return juce::Result::fail("Wrapped toolbar screenshot");
    playlist->setSize(normalSize.x, normalSize.y);
    folder.getChildFile("playlist-select-delete-zoom.txt")
        .replaceWithText(
            "PASS: contextual E/D/Shift+Z; mixed clip selection and Shift toggle/marquee; grouped Delete "
            "undo/redo/Escape; zoom/right-click/selection restoration; 360px wrapped toolbar. Actual "
            "component handlers; no native musical input or listening.\n");
    keyPressed(juce::KeyPress('p', {}, 0));
    openInstrument(imported.channel);
    dispatchMessages(400);
    juce::PNGImageFormat settingsPng;
    if (auto *window = instrumentWindows[imported.channel].get())
    {
        auto stream = folder.getChildFile("audio-channel-settings.png").createOutputStream();
        if (!stream)
            return juce::Result::fail("Audio settings PNG stream");
        stream->setPosition(0);
        stream->truncate();
        if (!settingsPng.writeImageToStream(window->createComponentSnapshot(window->getLocalBounds()),
                                            *stream))
            return juce::Result::fail("Audio settings PNG");
    }
    else
        return juce::Result::fail("Audio settings window missing");
    selectedChannel = controller.project.channels[3].id;
    refresh();
    if (saveProject(controller.project, folder.getChildFile("populated-audio-playlist.vibra")).failed())
        return juce::Result::fail("Populated project save");
    const auto saved = getLocalBounds();
    juce::PNGImageFormat png;
    for (const auto size : {juce::Point<int>{1280, 684}, juce::Point<int>{2560, 1356}})
    {
        setSize(size.x, size.y);
        resized();
        createComponentSnapshot(getLocalBounds());
        dispatchMessages(400);
        auto stream =
            folder.getChildFile(size.x == 1280 ? "audio-playlist-1080p-150.png" : "audio-playlist-4k-150.png")
                .createOutputStream();
        if (!stream)
            return juce::Result::fail("Audio Playlist PNG stream");
        stream->setPosition(0);
        stream->truncate();
        if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f), *stream))
            return juce::Result::fail("Audio Playlist PNG");
    }
    setSize(saved.getWidth(), saved.getHeight());
    resized();
    selectedChannel = imported.channel;
    const auto beforeReplacement = controller.project.state();
    loadSample("builtin:kick", true);
    if (!controller.project.state().isEquivalentTo(beforeReplacement) ||
        !status.startsWith("Drop a new Audio Clip"))
        return juce::Result::fail("Rack source replacement bypassed Audio Clip guard");
    auto stateBeforeFailure = controller.project.state();
    const auto invalid = folder.getChildFile("invalid.wav");
    invalid.replaceWithText("not an audio file");
    importAudioClip(invalid, 0, 0);
    const auto failureDeadline = juce::Time::getMillisecondCounter() + 8000;
    while (!status.startsWith("Unreadable") && juce::Time::getMillisecondCounter() < failureDeadline)
    {
        pollAudioImports();
        dispatchMessages(2);
    }
    if (!status.startsWith("Unreadable") || !controller.project.state().isEquivalentTo(stateBeforeFailure))
        return juce::Result::fail("Invalid audio must not mutate project");
    const auto beforeStale = controller.project.state();
    importAudioClip(source, 0, 0);
    controller.replace(controller.project);
    const auto staleDeadline = juce::Time::getMillisecondCounter() + 8000;
    while (!audioImports.idle() && juce::Time::getMillisecondCounter() < staleDeadline)
    {
        pollAudioImports();
        dispatchMessages(2);
    }
    if (!audioImports.idle() || !controller.project.state().isEquivalentTo(beforeStale))
        return juce::Result::fail("Stale audio import applied after project replacement");
    folder.getChildFile("audio-playlist-verification.txt")
        .replaceWithText(
            "PASS: actual Playlist file-drop handler, asynchronous 45-second metadata, selection, exact "
            "import undo/redo, contextual Ctrl+B linked audio duplicate/new selection, source trim past 30 "
            "seconds, actual left/right trim with exact one undo, Audio Clip settings opening, invalid-file "
            "and stale-project rejection, 1080p/4K component captures at "
            "150 percent. Component tests only; no OS drop, hardware or listening claim.\n");
    return juce::Result::ok();
}
juce::Result Studio::verifyAudioPlaylistLive(const juce::File &folder, const juce::File &fixture)
{
    if (folder.createDirectory().failed())
        return juce::Result::fail("Live Audio Clip evidence directory");
    folder.getChildFile("audio-playlist-live-failure.txt").deleteFile();
    auto project = Project::empty();
    project.channels.resize(1);
    project.channels.front().source = "builtin:bass";
    project.channels.front().pan = 1;
    project.channels.front().gain = 1;
    project.channels.front().mixer = 1;
    if (fixture != juce::File{})
    {
        Project hosted;
        if (loadProject(fixture, hosted).failed() || !hosted.channels.front().instrument)
            return juce::Result::fail("Live Audio Clip real instrument fixture");
        project.channels.front().instrument = hosted.channels.front().instrument;
        project.channels.front().instrument->id = project.allocate();
        if (!hosted.mixer.front().effects.empty())
        {
            project.mixer[2].effects = hosted.mixer.front().effects;
            for (auto &effect : project.mixer[2].effects)
                effect.id = project.allocate();
        }
    }
    project.patterns.front().length = 8;
    project.patterns.front().notes = {
        {project.allocate(), project.channels.front().id, 0, 8, 48, .5f, false}};
    project.placements = {{project.allocate(), project.patterns.front().id, 0, 0, 8, 0}};
    const auto file = folder.getChildFile("original-live-audio.wav");
    {
        juce::AudioBuffer<float> data(2, 48000 * 2);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < data.getNumSamples(); ++frame)
                data.setSample(channel, frame, float(.15 * std::sin(frame * .025)));
        auto stream = file.createOutputStream();
        if (!stream)
            return juce::Result::fail("Live source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 16, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples()))
            return juce::Result::fail("Live source fixture");
    }
    Channel clip;
    clip.id = project.allocate();
    clip.name = "Live Audio Clip";
    clip.audioClip = true;
    clip.source = file.getFullPathName();
    clip.audioDuration = 2;
    clip.pan = -1;
    clip.mixer = 2;
    project.channels.push_back(clip);
    const auto placement = project.allocate();
    project.audioPlacements.push_back({placement, clip.id, 1, .5, .25, 1});
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = clip.id;
    selectedClips = {placement};
    patternMode = false;
    focus = Scope::playlist;
    refresh();
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
    if (!device)
        return juce::Result::fail("Live Audio Clip synthetic device");
    const auto deadline = juce::Time::getMillisecondCounter() + 15000;
    while (needsRebuild && juce::Time::getMillisecondCounter() < deadline)
        dispatchMessages(5);
    if (needsRebuild || projectionError.isNotEmpty())
        return juce::Result::fail("Live Audio Clip projection: " + projectionError);
    audio.setMetronome(false);
    audio.setLooping(true);
    audio.playStop();
    juce::AudioBuffer<float> warm(2, 256);
    float warmRightPeak = 0;
    for (int index = 0; index < 20; ++index)
    {
        device->process(warm);
        warmRightPeak = std::max(warmRightPeak, warm.getMagnitude(1, 0, 256));
        dispatchMessages(6);
    }
    const auto before = audio.projectionDiagnostics();
    const auto pluginsBefore = audio.pluginDiagnostics();
    juce::String baseline = "Warm right peak=" + juce::String(warmRightPeak) + "\n";
    bool baselineWorkers = true;
    for (const auto &plugin : pluginsBefore)
    {
        baselineWorkers = baselineWorkers && plugin.alive;
        baseline += "Plugin " + juce::String(plugin.id) + " pid=" + juce::String(plugin.processId) +
                    " alive=" + juce::String(int(plugin.alive)) +
                    " submitted=" + juce::String(plugin.packets.submitted) +
                    " missed=" + juce::String(plugin.packets.missed) +
                    " processed=" + juce::String(plugin.packets.processed) +
                    " heartbeat=" + juce::String(plugin.packets.workerHeartbeat) +
                    " phase=" + juce::String(int(plugin.packets.workerPhase)) +
                    " status=" + audio.pluginStatus(plugin.id) + "\n";
    }
    folder.getChildFile("audio-playlist-live-baseline.txt").replaceWithText(baseline);
    if (warmRightPeak < .001f || !baselineWorkers)
    {
        audio.stop();
        return juce::Result::fail("Live baseline had no instrument signal or an unavailable worker before "
                                  "any edits; inspect baseline");
    }
    const auto starts = device->starts, stops = device->stops;
    class Capture final : public juce::Thread
    {
        SyntheticDevice &device;
        juce::AudioBuffer<float> block{2, 256};

      public:
        juce::AudioBuffer<float> pcm{2, 1536 * 256};
        std::atomic<int> blocks{0};
        std::atomic<bool> done{false};
        std::atomic<unsigned> controlPhase{0};
        std::array<double, 1536> callbackMs{};
        std::array<unsigned, 1536> phaseBefore{}, phaseAfter{};
        int silentRight = 0, overruns = 0;
        std::array<int, 1536> silentIndices{};
        double maxMs = 0;
        explicit Capture(SyntheticDevice &value) : Thread("Live Audio Clip virtual callback"), device(value)
        {
            pcm.clear();
        }
        ~Capture() override
        {
            signalThreadShouldExit();
            waitForThreadToExit(-1);
        }
        void run() override
        {
            for (int index = 0; index < 1536 && !threadShouldExit(); ++index)
            {
                phaseBefore[size_t(index)] = controlPhase.load(std::memory_order_relaxed);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                device.process(block);
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
                callbackMs[size_t(index)] = elapsed;
                phaseAfter[size_t(index)] = controlPhase.load(std::memory_order_relaxed);
                maxMs = std::max(maxMs, elapsed);
                overruns += elapsed > 256000. / 48000.;
                if (index > 8 && block.getMagnitude(1, 0, 256) < .0000001f)
                    silentIndices[size_t(silentRight++)] = index;
                for (int channel = 0; channel < 2; ++channel)
                    pcm.copyFrom(channel, index * 256, block, channel, 0, 256);
                blocks = index + 1;
                wait(6);
            }
            done = true;
        }
    } capture(*device);
    capture.startThread();
    auto *playlist = static_cast<Editor *>(windows[2]->getContentComponent());
    auto drag = [&](juce::Point<float> start, juce::Point<float> end)
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&](juce::Point<float> point, bool moved)
        {
            return juce::MouseEvent(source, point, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                    1.f, 0.f, 0.f, 0.f, 0.f, playlist, playlist, juce::Time::getCurrentTime(),
                                    start, juce::Time::getCurrentTime(), 1, moved);
        };
        playlist->mouseDown(event(start, false));
        playlist->mouseDrag(event(end, true));
        playlist->mouseUp(event(end, true));
    };
    auto publishAndProcess = [&]
    {
        capture.controlPhase.store(2, std::memory_order_relaxed);
        timerCallback(); // Exercise the production publication scheduler before measuring another state.
        const auto start = capture.blocks.load();
        while (!capture.done && capture.blocks.load() < start + 2)
            dispatchMessages(1);
    };
    int actions = 0, wraps = 0, lastEdit = -1;
    double previousBeat = audio.positionBeat();
    bool exactUndo = true, exactRedo = true, playing = true;
    while (!capture.done)
    {
        const auto count = capture.blocks.load();
        const auto beat = audio.positionBeat();
        wraps += beat < previousBeat - 1.;
        previousBeat = beat;
        playing = playing && audio.playing();
        if (count > 24 && count / 8 != lastEdit)
        {
            capture.controlPhase.store(1, std::memory_order_relaxed);
            lastEdit = count / 8;
            selectedClips = {placement};
            focus = Scope::playlist;
            const auto original = controller.project.state();
            const auto bounds = playlist->playlistClipBounds(placement);
            switch (actions % 7)
            {
            case 0:
                drag({bounds.getX() + 2, bounds.getCentreY()}, {bounds.getX() + 18, bounds.getCentreY()});
                break;
            case 1:
                drag({bounds.getRight() - 2, bounds.getCentreY()},
                     {bounds.getRight() - 18, bounds.getCentreY()});
                break;
            case 2:
                drag(bounds.getCentre(), bounds.getCentre().translated(16, 0));
                break;
            case 3:
                keyPressed(juce::KeyPress('b', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 0));
                break;
            case 4:
                keyPressed(juce::KeyPress('s', {}, 0));
                drag(bounds.getCentre(), bounds.getCentre().translated(16, 0));
                break;
            case 5:
                keyPressed(juce::KeyPress('c', {}, 0));
                drag(bounds.getCentre(), bounds.getCentre().translated(0, 12));
                break;
            case 6:
                keyPressed(juce::KeyPress('t', {}, 0));
                drag(bounds.getCentre(), bounds.getCentre());
                break;
            }
            keyPressed(juce::KeyPress('p', {}, 0));
            const auto changed = controller.project.state();
            publishAndProcess();
            capture.controlPhase.store(3, std::memory_order_relaxed);
            keyPressed(juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 0));
            exactUndo = exactUndo && controller.project.state().isEquivalentTo(original);
            publishAndProcess();
            capture.controlPhase.store(4, std::memory_order_relaxed);
            keyPressed(juce::KeyPress(
                'z', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier),
                0));
            exactRedo = exactRedo && controller.project.state().isEquivalentTo(changed);
            publishAndProcess();
            capture.controlPhase.store(3, std::memory_order_relaxed);
            keyPressed(juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 0));
            publishAndProcess();
            capture.controlPhase.store(5, std::memory_order_relaxed);
            const auto beforeView = controller.project.state();
            keyPressed(juce::KeyPress('e', {}, 0));
            auto viewBounds = playlist->playlistClipBounds(placement);
            drag(viewBounds.getCentre(), viewBounds.getCentre());
            keyPressed(juce::KeyPress('z', juce::ModifierKeys::shiftModifier, 0));
            drag(viewBounds.getTopLeft().translated(3, 3), viewBounds.getBottomRight().translated(-3, -3));
            playlist->viewAction("playlist.fit");
            exactUndo = exactUndo && controller.project.state().isEquivalentTo(beforeView);
            keyPressed(juce::KeyPress('p', {}, 0));
            ++actions;
        }
        capture.controlPhase.store(0, std::memory_order_relaxed);
        dispatchMessages(2);
    }
    capture.waitForThreadToExit(-1);
    {
        juce::String timing = "block,callbackMs,deadlineMs,phaseBefore,phaseAfter\n";
        for (int index = 0; index < capture.blocks.load(); ++index)
            timing += juce::String(index) + "," + juce::String(capture.callbackMs[size_t(index)], 6) +
                      "," + juce::String(256000. / 48000., 6) + "," +
                      juce::String(capture.phaseBefore[size_t(index)]) + "," +
                      juce::String(capture.phaseAfter[size_t(index)]) + "\n";
        if (!folder.getChildFile("audio-playlist-live-callbacks.csv").replaceWithText(timing))
            return juce::Result::fail("Retain every live Audio Clip callback timing");
    }
    dispatchMessages(50);
    const auto after = audio.projectionDiagnostics();
    const auto pluginsAfter = audio.pluginDiagnostics();
    juce::String workerReport;
    for (const auto &plugin : pluginsAfter)
        workerReport += "Plugin " + juce::String(plugin.id) + " pid=" + juce::String(plugin.processId) +
                        " alive=" + juce::String(int(plugin.alive)) +
                        " submitted=" + juce::String(plugin.packets.submitted) +
                        " missed=" + juce::String(plugin.packets.missed) +
                        " processed=" + juce::String(plugin.packets.processed) +
                        " heartbeat=" + juce::String(plugin.packets.workerHeartbeat) +
                        " phase=" + juce::String(int(plugin.packets.workerPhase)) +
                        " status=" + audio.pluginStatus(plugin.id) + "\n";
    folder.getChildFile("audio-playlist-live-workers.txt").replaceWithText(workerReport);
    bool sameWorkers = pluginsBefore.size() == pluginsAfter.size();
    for (size_t index = 0; sameWorkers && index < pluginsBefore.size(); ++index)
        sameWorkers =
            pluginsBefore[index].processId == pluginsAfter[index].processId && pluginsAfter[index].alive;
    folder.getChildFile("audio-playlist-live-verification.txt")
        .replaceWithText(
            "Actions=" + juce::String(actions) + "; loops=" + juce::String(wraps) + "; silent right blocks=" +
            juce::String(capture.silentRight) + "; overruns=" + juce::String(capture.overruns) +
            "; max callback ms=" + juce::String(capture.maxMs, 4) +
            "; graph notifications=" + juce::String(before.channels.front().graphInitialisations) + "/" +
            juce::String(after.channels.front().graphInitialisations) +
            "; exact undo/redo=" + juce::String(int(exactUndo && exactRedo)) +
            "; workers retained=" + juce::String(int(sameWorkers)) +
            ". Actual JUCE Playlist/shortcut handlers and concurrent virtual callback; no OS musical input, "
            "listening, physical hardware or frame-perfect unaffected-voice claim.\n");
    {
        juce::String indices;
        for (int index = 0; index < capture.silentRight; ++index)
            indices += juce::String(capture.silentIndices[size_t(index)]) + " ";
        folder.getChildFile("audio-playlist-live-silent-indices.txt").replaceWithText(indices);
        auto stream = folder.getChildFile("audio-playlist-live-capture.wav").createOutputStream();
        if (stream)
        {
            stream->setPosition(0);
            stream->truncate();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
            if (writer)
                writer->writeFromAudioSampleBuffer(capture.pcm, 0, capture.pcm.getNumSamples());
        }
    }
    audio.stop();
    if (!playing || !exactUndo || !exactRedo || actions < 80 || wraps < 2 || capture.silentRight ||
        capture.overruns || before.edit != after.edit || before.playbackContext != after.playbackContext ||
        before.replacements != after.replacements || before.samplerCreations != after.samplerCreations ||
        starts != device->starts || stops != device->stops || !sameWorkers)
        return juce::Result::fail("Live Audio Clip invariants failed; inspect receipt");
    return juce::Result::ok();
}
} // namespace vibera
