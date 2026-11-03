// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "Settings.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
bool Studio::noteStretchPending() const
{
    return noteStretchSession && noteStretchSession->state == "pending";
}
juce::Result Studio::beginNoteStretch(const std::vector<Id> &ids, bool remote, int leaseMs)
{
    pollNoteStretch();
    if (noteStretchPending())
    {
        if (remote)
            return juce::Result::fail("Another stretch preview is active");
        finishNoteStretch(false, "Local input superseded preview");
    }
    if (leaseMs < 250 || leaseMs > 120000)
        return juce::Result::fail("Stretch idle lease must be 250..120000ms");
    if (controller.hasGesture() || noteSlicePending() || afterPluginState || pendingExport ||
        audio.rendering() || midiFileBusy || customization.running)
        return juce::Result::fail("Finish the active preview, edit, file operation or macro first");
    NoteStretchSession session;
    session.args.pattern = selectedPattern;
    session.args.channel = selectedChannel;
    session.args.ids = ids;
    session.args.value = 1;
    auto r = planNoteStretch(controller.project, selectedPattern, selectedChannel, ids, 0, 1, session.plan);
    if (r.failed())
        return r;
    session.args.beat = 1000000;
    for (const auto &note : session.plan.notes)
        session.args.beat = std::min(session.args.beat, note.beat);
    r = planNoteStretch(controller.project, selectedPattern, selectedChannel, ids, session.args.beat, 1,
                        session.plan);
    if (r.failed())
        return r;
    session.id = juce::Uuid().toString();
    session.project = controller.projectIdentity;
    session.revision = controller.revision;
    session.remote = remote;
    session.leaseMs = leaseMs;
    session.lastActivity = juce::Time::getMillisecondCounterHiRes();
    noteStretchSession = std::move(session);
    status = "Stretch preview100% | Release to apply; Escape cancels";
    repaint();
    if (windows[1])
        windows[1]->repaint();
    return juce::Result::ok();
}
void Studio::pollNoteStretch()
{
    if (!noteStretchPending())
        return;
    const auto &session = *noteStretchSession;
    if (session.project != controller.projectIdentity || session.revision != controller.revision ||
        session.args.pattern != selectedPattern || session.args.channel != selectedChannel)
        finishNoteStretch(false, "Project or target context changed");
    else if (juce::Time::getMillisecondCounterHiRes() - session.lastActivity >= session.leaseMs)
        finishNoteStretch(false, "Stretch idle lease expired");
    else if (controller.hasGesture() || noteSlicePending() || afterPluginState || pendingExport ||
             audio.rendering() || midiFileBusy)
        finishNoteStretch(false, "Another edit or file operation owns the application");
}
juce::Result Studio::updateNoteStretch(double ratio)
{
    pollNoteStretch();
    if (!noteStretchPending())
        return juce::Result::fail("Stretch preview is not pending");
    auto &session = *noteStretchSession;
    NoteStretchPlan plan;
    auto r = planNoteStretch(controller.project, session.args.pattern, session.args.channel, session.args.ids,
                             session.args.beat, ratio, plan);
    if (r.failed())
        return r;
    session.plan = std::move(plan);
    session.args.value = ratio;
    status = "Stretch preview " + juce::String(ratio * 100, 1) + "% | Release to apply; Escape cancels";
    session.lastActivity = juce::Time::getMillisecondCounterHiRes();
    repaint();
    if (windows[1])
        windows[1]->repaint();
    return juce::Result::ok();
}
juce::Result Studio::finishNoteStretch(bool commit, const juce::String &reason)
{
    if (!noteStretchPending())
        return juce::Result::fail("Stretch preview is not pending");
    auto &session = *noteStretchSession;
    if (commit)
    {
        if (session.project != controller.projectIdentity || session.revision != controller.revision ||
            session.args.pattern != selectedPattern || session.args.channel != selectedChannel)
        {
            finishNoteStretch(false, "Project or target context changed");
            return juce::Result::fail("Stretch source changed");
        }
        const juce::ScopedValueSetter<bool> committing(committingNoteStretch, true);
        command("notes.stretch", session.args);
        if (status != "Ready")
            return juce::Result::fail(status);
        session.state = "completed";
        session.reason = "Stretch committed as one canonical edit";
    }
    else
    {
        session.state = "cancelled";
        session.reason = reason.isEmpty() ? "Preview cancelled; source unchanged" : reason;
    }
    repaint();
    if (windows[1])
        windows[1]->repaint();
    return juce::Result::ok();
}
juce::var Studio::noteStretchState(int offset, int limit) const
{
    auto *o = new juce::DynamicObject;
    if (!noteStretchSession)
    {
        o->setProperty("completionState", "idle");
        return juce::var(o);
    }
    const auto &s = *noteStretchSession;
    o->setProperty("operation", s.id);
    o->setProperty("completionState", s.state);
    o->setProperty("reason", s.reason);
    o->setProperty("remote", s.remote);
    o->setProperty("projectIdentity", s.project);
    o->setProperty("sourceRevision", juce::int64(s.revision));
    o->setProperty("pattern", juce::int64(s.args.pattern));
    o->setProperty("channel", juce::int64(s.args.channel));
    o->setProperty("anchor", s.args.beat);
    o->setProperty("sourceEnd", s.plan.end);
    o->setProperty("ratio", s.args.value);
    o->setProperty("minimumRatio", s.plan.minimumRatio);
    o->setProperty("maximumRatio", s.plan.maximumRatio);
    o->setProperty("count", int(s.plan.notes.size()));
    o->setProperty("previewMutatesProject", false);
    o->setProperty("leaseMs", s.leaseMs);
    juce::Array<juce::var> rows;
    auto end = std::min(s.plan.notes.size(), size_t(std::max(0, offset) + juce::jlimit(1, 512, limit)));
    for (size_t i = size_t(std::max(0, offset)); i < end; ++i)
    {
        const auto &n = s.plan.notes[i];
        auto *v = new juce::DynamicObject;
        v->setProperty("id", juce::int64(n.id));
        v->setProperty("channel", juce::int64(n.channel));
        v->setProperty("beat", n.beat);
        v->setProperty("length", n.length);
        v->setProperty("pitch", n.pitch);
        v->setProperty("velocity", n.velocity);
        v->setProperty("muted", n.muted);
        v->setProperty("step", n.step);
        rows.add(juce::var(v));
    }
    o->setProperty("items", rows);
    o->setProperty("nextOffset", end < s.plan.notes.size() ? juce::var(int(end)) : juce::var{});
    return juce::var(o);
}

juce::Result Studio::verifyNoteStretch(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    MacroPage macro(*this);
    if (auto r = macro.verifyStretchBuilder(); r.failed())
        return fail(r.getErrorMessage());
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    typingKeys.enabled = false;
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    project.name = "Stretch a phrase - keep its source";
    const auto pattern = project.patterns.front().id, channel = project.channels.front().id,
               ghost = project.channels[1].id;
    project.channels.front().source = "builtin:bass";
    project.patterns.front().notes = {{project.allocate(), channel, 1, 1, 60, .7f, true, false},
                                      {project.allocate(), channel, 2, .5, 64, .4f, false, true},
                                      {project.allocate(), ghost, 0, .5, 67, .6f, false, false}};
    project.placements = {{project.allocate(), pattern, 0, 0, 4, 0},
                          {project.allocate(), pattern, 1, 4, 4, 0}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = channel;
    selectedNotes = {project.patterns.front().notes[0].id, project.patterns.front().notes[1].id};
    showEditor(1);
    maximiseEditor(1);
    focus = Scope::piano;
    refresh();
    auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
    editor->viewAction("piano.fit");
    auto baseline = controller.project.state();
    const auto selection = selectedNotes;
    const auto revision = controller.revision;
    const auto first = editor->noteBounds(controller.project.pattern(pattern)->notes[0]);
    const auto second = editor->noteBounds(controller.project.pattern(pattern)->notes[1]);
    const auto spacing = second.getX() - first.getX();
    auto handle = editor->stretchHandleBounds();
    if (handle.isEmpty() || !editor->getLocalBounds().contains(handle))
        return fail("Selected phrase stretch handle unavailable");
    auto start = handle.getCentre().toFloat();
    auto end = start + juce::Point<float>{spacing * 1.5f, 0};
    auto event = [&](juce::Point<float> point, int mods = juce::ModifierKeys::leftButtonModifier)
    {
        auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(mods), 1, 0, 0, 0, 0, editor, editor, now, start, now, 1,
                                true);
    };
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    if (!noteStretchPending() || std::abs(noteStretchSession->args.value - 2) > 1.e-6 ||
        controller.revision != revision || !controller.project.state().isEquivalentTo(baseline))
        return fail("Actual handle preview did not preserve source or scale about earliest onset");
    editor->mouseUp(event(end));
    auto stretched = controller.project.state();
    if (noteStretchSession->state != "completed" || selectedNotes != selection ||
        controller.project.pattern(pattern)->notes[1].beat != 3 ||
        !controller.project.pattern(pattern)->notes[1].muted)
        return fail("Handle commit lost musical geometry/selection/properties");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("Stretch not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(stretched))
        return fail("Stretch Redo not exact");
    controller.undoEdit();
    handle = editor->stretchHandleBounds();
    start = handle.getCentre().toFloat();
    end = start + juce::Point<float>{spacing * .33f, 0};
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    if (!noteStretchPending() || std::abs(noteStretchSession->args.value - 1.22) > 1.e-5)
        return fail("Actual Alt handle did not bypass snapping");
    if (!keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || noteStretchPending() ||
        selectedNotes != selection || !controller.project.state().isEquivalentTo(baseline))
        return fail("Escape did not cancel stretch preview");
    editor->mouseUp(event(end));
    if (beginNoteStretch(selection, false, 120000).failed() || updateNoteStretch(2).failed())
        return fail("Local preview admission");
    performAction("piano.toolSlice");
    if (noteStretchPending() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Tool switch did not cancel without history");
    performAction("tool.draw");
    if (beginNoteStretch(selection, false, 120000).failed())
        return fail("Second local preview admission");
    toggleEditor(1);
    if (noteStretchPending())
        return fail("Hiding Piano Roll did not cancel preview");
    showEditor(1);
    maximiseEditor(1);
    focus = Scope::piano;
    if (beginNoteStretch(selection, true, 250).failed())
        return fail("Remote preview admission");
    noteStretchSession->lastActivity -= 251;
    pollNoteStretch();
    if (noteStretchPending() || noteStretchSession->state != "cancelled")
        return fail("Expired session retained source ownership");
    auto parameterDialog = [&](const juce::String &text, int result)
    {
        if (!performAction("notes.stretch"))
            return false;
        auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!dialog || !dialog->getTextEditor("amount"))
            return false;
        dialog->getTextEditor("amount")->setText(text);
        dialog->exitModalState(result);
        dispatchMessages(50);
        return true;
    };
    if (!parameterDialog("2", 1) || !controller.project.state().isEquivalentTo(stretched))
        return fail("Actual stretch action dialog lost canonical targets/ratio");
    controller.undoEdit();
    if (!parameterDialog("1junk", 1) || !controller.project.state().isEquivalentTo(baseline))
        return fail("Stretch action accepted malformed input");
    if (!parameterDialog("2", 0) || !controller.project.state().isEquivalentTo(baseline))
        return fail("Stretch action cancel changed source");
    if (beginNoteStretch(selection, false, 120000).failed() || updateNoteStretch(2).failed())
        return fail("Showcase preview");
    for (auto size : {juce::Point<int>{1920, 1044}, juce::Point<int>{2560, 1392}})
    {
        setSize(size.x, size.y);
        if (maximisedEditor != 1)
            maximiseEditor(1);
        editor->viewAction("piano.fit");
        refresh();
        auto image = createComponentSnapshot(getLocalBounds(), true, size.x == 2560 ? 1.5f : 1.f);
        auto file =
            output.getChildFile(size.x == 1920 ? "stretch-preview-1080p.png" : "stretch-preview-4k-150.png");
        auto stream = file.createOutputStream();
        if (!stream)
            return fail("Preview image stream");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(image, *stream))
            return fail("Preview image encode");
    }
    if (finishNoteStretch(true).failed() ||
        saveProject(controller.project, output.getChildFile("stretched-showcase.vibra")).failed())
        return fail("Showcase commit/save");
    auto image = createComponentSnapshot(getLocalBounds(), true, 1.f);
    auto stream = output.getChildFile("stretched-phrase.png").createOutputStream();
    if (!stream)
        return fail("Result image stream");
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat().writeImageToStream(image, *stream);
    output.getChildFile("note-stretch-gui.txt")
        .replaceWithText(
            "PASS actual JUCE selected handle/drag/Alt/cancel, non-mutating preview, stable "
            "IDs/properties/selection, exact commit/Undo/Redo, tool/hide/expiry cancellation, project and "
            "populated1080p/4K150 renders. Right-Shift OS, live PCM, listening and hardware separate.\n");
    return juce::Result::ok();
}

} // namespace vibera
