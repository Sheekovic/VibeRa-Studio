// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "Settings.h"
namespace vibera
{
bool Studio::noteSlicePending() const
{
    return noteSliceSession && noteSliceSession->state == "pending";
}
juce::Result Studio::beginNoteSlice(bool remote, int leaseMs)
{
    pollNoteSlice();
    if (noteSlicePending())
    {
        if (remote)
            return juce::Result::fail("Another Slice preview is active");
        finishNoteSlice(false, "Local input superseded the previous preview");
    }
    const auto *channel = controller.project.channel(selectedChannel);
    if (!controller.project.pattern(selectedPattern) || !channel || channel->audioClip)
        return juce::Result::fail("Select a Pattern and playable Channel");
    if (leaseMs < 250 || leaseMs > 120000)
        return juce::Result::fail("Slice idle lease must be 250..120000ms");
    if (controller.hasGesture() || noteStretchPending() || afterPluginState || pendingExport || audio.rendering() || midiFileBusy ||
        customization.running)
        return juce::Result::fail("Finish the active gesture, file operation or macro first");
    NoteSliceSession session;
    session.id = juce::Uuid().toString();
    session.project = controller.projectIdentity;
    session.revision = controller.revision;
    session.args.pattern = selectedPattern;
    session.args.channel = selectedChannel;
    session.remote = remote;
    session.lastActivity = juce::Time::getMillisecondCounterHiRes();
    session.leaseMs = leaseMs;
    noteSliceSession = std::move(session);
    return juce::Result::ok();
}
void Studio::pollNoteSlice()
{
    if (!noteSlicePending())
        return;
    const auto &session = *noteSliceSession;
    if (session.project != controller.projectIdentity || session.revision != controller.revision ||
        session.args.pattern != selectedPattern || session.args.channel != selectedChannel)
        finishNoteSlice(false, "Project or target context changed");
    else if (juce::Time::getMillisecondCounterHiRes() - session.lastActivity >= session.leaseMs)
        finishNoteSlice(false, "Slice idle lease expired");
    else if (afterPluginState || pendingExport || audio.rendering() || midiFileBusy ||
             controller.hasGesture())
        finishNoteSlice(false, "Another edit or file operation owns the application");
}
juce::Result Studio::updateNoteSlice(const NoteSliceLine &line, const std::vector<Id> &ids)
{
    pollNoteSlice();
    if (!noteSlicePending())
        return juce::Result::fail("Slice preview is not pending");
    auto &session = *noteSliceSession;
    std::vector<NoteSliceCut> cuts;
    if (auto r =
            planNoteSlices(controller.project, session.args.pattern, session.args.channel, ids, line, cuts);
        r.failed())
        return r;
    session.args.slice = line;
    session.args.ids = ids;
    session.cuts = std::move(cuts);
    session.lastActivity = juce::Time::getMillisecondCounterHiRes();
    repaint();
    if (windows[1])
        windows[1]->repaint();
    return juce::Result::ok();
}
juce::Result Studio::finishNoteSlice(bool commit, const juce::String &reason)
{
    if (!noteSlicePending())
        return juce::Result::fail("Slice preview is not pending");
    auto &session = *noteSliceSession;
    if (commit)
    {
        if (session.project != controller.projectIdentity || session.revision != controller.revision ||
            session.args.pattern != selectedPattern || session.args.channel != selectedChannel)
        {
            finishNoteSlice(false, "Project or target context changed");
            return juce::Result::fail("Slice source changed");
        }
        if (!session.args.slice)
            return juce::Result::fail("Update a Slice line before committing");
        std::vector<NoteSliceCut> cuts;
        if (auto r = planNoteSlices(controller.project, session.args.pattern, session.args.channel,
                                    session.args.ids, *session.args.slice, cuts);
            r.failed())
            return r;
        const juce::ScopedValueSetter<bool> committing(committingNoteSlice, true);
        command("notes.slice", session.args);
        if (status != "Ready")
            return juce::Result::fail(status);
        // Obtain actual created identities from the canonical command's appended halves.
        const auto *pattern = controller.project.pattern(session.args.pattern);
        for (size_t i = 0; i < cuts.size(); ++i)
            cuts[i].right = pattern->notes[pattern->notes.size() - cuts.size() + i].id;
        session.cuts = std::move(cuts);
        session.state = "completed";
        session.reason = "Slice committed as one canonical edit";
    }
    else
    {
        session.state = "cancelled";
        session.reason = reason.isEmpty() ? "Preview cancelled; source notes unchanged" : reason;
    }
    repaint();
    if (windows[1])
        windows[1]->repaint();
    return juce::Result::ok();
}
juce::var Studio::noteSliceState(int offset, int limit) const
{
    auto *o = new juce::DynamicObject;
    if (!noteSliceSession)
    {
        o->setProperty("completionState", "idle");
        return juce::var(o);
    }
    const auto &s = *noteSliceSession;
    o->setProperty("operation", s.id);
    o->setProperty("completionState", s.state);
    o->setProperty("reason", s.reason);
    o->setProperty("remote", s.remote);
    o->setProperty("projectIdentity", s.project);
    o->setProperty("sourceRevision", juce::int64(s.revision));
    o->setProperty("pattern", juce::int64(s.args.pattern));
    o->setProperty("channel", juce::int64(s.args.channel));
    o->setProperty("line", s.args.slice ? sliceLineState(*s.args.slice) : juce::var{});
    o->setProperty("count", int(s.cuts.size()));
    juce::Array<juce::var> rows;
    const auto end = std::min(s.cuts.size(), size_t(std::max(0, offset) + juce::jlimit(1, 512, limit)));
    for (size_t i = size_t(std::max(0, offset)); i < end; ++i)
    {
        const auto &cut = s.cuts[i];
        auto *row = new juce::DynamicObject;
        row->setProperty("left", juce::int64(cut.id));
        row->setProperty("right", juce::int64(cut.right));
        row->setProperty("beat", cut.beat);
        rows.add(juce::var(row));
    }
    o->setProperty("items", rows);
    o->setProperty("nextOffset", end < s.cuts.size() ? juce::var(int(end)) : juce::var{});
    o->setProperty("previewMutatesProject", false);
    o->setProperty("leaseMs", s.leaseMs);
    return juce::var(o);
}
juce::Result Studio::verifyNoteSlice(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    MacroPage macroBuilder(*this);
    if (auto r = macroBuilder.verifySliceBuilder(); r.failed())
        return fail(r.getErrorMessage());
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    typingKeys.enabled = false;
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    project.name = "Slice a chord - keep the source";
    const auto pattern = project.patterns.front().id, channel = project.channels.front().id,
               ghost = project.channels[1].id;
    project.channels.front().source = "builtin:bass";
    project.patterns.front().notes = {{project.allocate(), channel, 0, 2, 60, .7f, false, false},
                                      {project.allocate(), channel, 0, 2, 64, .5f, false, true},
                                      {project.allocate(), ghost, 0, 2, 67, .6f, false, false}};
    project.placements = {{project.allocate(), pattern, 0, 0, 4, 0},
                          {project.allocate(), pattern, 1, 4, 4, 0}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = channel;
    selectedNotes = {project.patterns.front().notes[0].id};
    showEditor(1);
    maximiseEditor(1);
    focus = Scope::piano;
    refresh();
    auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
    editor->viewAction("piano.fit");
    auto *button = dynamic_cast<juce::Button *>(editor->findChildWithID("piano.toolSlice"));
    if (!button || !button->isVisible() || button->getWidth() < 24)
        return fail("Slice toolbar unavailable");
    if (!keyPressed(juce::KeyPress('C', {}, 0)) || !editor->noteSliceToolActive())
        return fail("Contextual C dispatch failed");
    const auto baseline = controller.project.state();
    const auto selection = selectedNotes;
    const auto revision = controller.revision;
    auto rect = [&](int i)
    { return editor->noteBounds(controller.project.pattern(pattern)->notes[size_t(i)]); };
    auto first = rect(0), second = rect(1);
    const auto start = juce::Point<float>{first.getX() + first.getWidth() * .25f, first.getBottom() + 3};
    const auto end = juce::Point<float>{second.getX() + second.getWidth() * .75f, second.getY() - 3};
    auto event = [&](juce::Point<float> point, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, editor, editor, now, start, now,
                                1, true);
    };
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    if (!noteSlicePending() || noteSliceSession->cuts.size() != 2 || controller.revision != revision ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Actual drag preview changed source/history or lost chord targets");
    editor->mouseUp(event(end));
    const auto sliced = controller.project.state();
    if (controller.project.pattern(pattern)->notes.size() != 5 || noteSliceSession->state != "completed" ||
        selectedNotes != selection || !controller.project.pattern(pattern)->notes[4].muted)
        return fail("Actual cut commit lost identities/selection/muted half");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("Cut line was not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(sliced))
        return fail("Cut Redo changed source");
    controller.undoEdit();
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    if (!keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || noteSlicePending() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Escape did not cancel nondestructive cut");
    editor->mouseUp(event(end));
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    const auto anchor = *noteSliceSession->args.slice;
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -.125f;
    editor->mouseWheelMove(event(end), wheel);
    editor->mouseDrag(event(end));
    if (noteSliceSession->args.slice->startBeat != anchor.startBeat ||
        noteSliceSession->args.slice->startPitch != anchor.startPitch ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Viewport scrolling moved the musical cut anchor");
    editor->cancelPointerGesture();
    editor->viewAction("piano.fit");
    const auto ctrl = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::leftButtonModifier;
    const auto centre = rect(0).getCentre();
    editor->mouseDown(event(centre, ctrl));
    editor->mouseDrag(event(centre + juce::Point<float>{3, 3}, ctrl));
    editor->mouseUp(event(centre + juce::Point<float>{3, 3}, juce::ModifierKeys::ctrlModifier));
    if (selectedNotes.empty() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Ctrl selection in Slice mode changed score");
    // Shift+Alt is a single-point, unsnapped row cut.
    const auto point = juce::Point<float>{first.getX() + first.getWidth() * .173f, first.getCentreY()};
    editor->mouseDown(event(point, juce::ModifierKeys::leftButtonModifier |
                                       juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier));
    editor->mouseUp(event(point, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier));
    if (controller.project.pattern(pattern)->notes.size() != 4 ||
        std::abs(controller.project.pattern(pattern)->notes[0].length / .25 -
                 std::round(controller.project.pattern(pattern)->notes[0].length / .25)) < 1.e-5)
        return fail("Shift+Alt point cut did not bypass snap");
    controller.undoEdit();
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    keyPressed(juce::KeyPress('P', {}, 0));
    if (noteSlicePending() || editor->noteSliceToolActive() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Changing tool did not cancel preview");
    if (beginNoteSlice(true, 250).failed() || updateNoteSlice({1, 1, 59.5, 64.5, .25, false}).failed())
        return fail("Shared remote preview admission failed");
    juce::Thread::sleep(260);
    pollNoteSlice();
    if (noteSlicePending() || noteSliceSession->state != "cancelled" ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Idle preview expiry changed source");
    if (beginNoteSlice(true).failed() || updateNoteSlice({1, 1, 59.5, 64.5, .25, false}).failed())
        return fail("Second preview failed");
    selectedChannel = ghost;
    pollNoteSlice();
    selectedChannel = channel;
    if (noteSlicePending() || noteSliceSession->state != "cancelled")
        return fail("Context change did not cancel preview");
    if (beginNoteSlice(false).failed())
        return fail("Visibility preview admission failed");
    toggleEditor(1);
    if (noteSlicePending())
        return fail("Hiding Piano Roll retained an invisible cut preview");
    showEditor(1);
    if (maximisedEditor != 1)
        maximiseEditor(1);
    editor->viewAction("piano.fit");
    focus = Scope::piano;
    keyPressed(juce::KeyPress('C', {}, 0));
    if (beginNoteSlice(false).failed() || updateNoteSlice({.75, 1.25, 59.5, 64.5, .25, false}).failed())
        return fail("Showcase preview failed");
    for (auto size : {juce::Point<int>{1920, 1044}, juce::Point<int>{2560, 1404}})
    {
        setSize(size.x, size.y);
        if (maximisedEditor != 1)
            maximiseEditor(1);
        editor->viewAction("piano.fit");
        refresh();
        auto image = createComponentSnapshot(getLocalBounds(), true, size.x == 2560 ? 1.5f : 1.f);
        auto stream =
            output.getChildFile(size.x == 1920 ? "slice-preview-1080p.png" : "slice-preview-4k-150.png")
                .createOutputStream();
        if (!stream)
            return fail("Preview screenshot stream failed");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(image, *stream))
            return fail("Preview encode failed");
    }
    if (finishNoteSlice(true).failed() ||
        saveProject(controller.project, output.getChildFile("sliced-showcase.vibra")).failed())
        return fail("Showcase commit/save failed");
    auto image = createComponentSnapshot(getLocalBounds(), true, 1.f);
    auto stream = output.getChildFile("sliced-chord.png").createOutputStream();
    if (!stream)
        return fail("Commit capture failed");
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat().writeImageToStream(image, *stream);
    output.getChildFile("note-slice-gui.txt")
        .replaceWithText("PASS actual JUCE C/P/tool, non-mutating cut-line preview, chord commit, retained "
                         "selection/mute/ghost, one exact Undo/Redo, Escape/tool/context/lease cancel, "
                         "Shift+Alt unsnapped point cut, project and populated1080p/4K150 renders. Native "
                         "OS/listening/hardware separate.\n");
    return juce::Result::ok();
}
} // namespace vibera
