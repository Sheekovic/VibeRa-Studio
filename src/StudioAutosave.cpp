// SPDX-License-Identifier: AGPL-3.0-or-later
// Autosave, rotating save backups and crash recovery (PM-02).
#include "Studio.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <limits>
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#if JUCE_MAC
#include <libproc.h>
#include <sys/proc.h>
#endif
#endif

namespace vibera
{
namespace
{
std::uint32_t currentProcessId()
{
#if JUCE_WINDOWS
    return GetCurrentProcessId();
#else
    return std::uint32_t(getpid());
#endif
}
// Creation time of a live process, 0 when it no longer runs (PID + creation identity).
std::uint64_t processCreation(std::uint32_t pid)
{
#if JUCE_WINDOWS
    auto *process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return 0;
    DWORD code = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    std::uint64_t result = 0;
    if (GetExitCodeProcess(process, &code) && code == STILL_ACTIVE &&
        GetProcessTimes(process, &created, &exited, &kernel, &user))
        result = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    CloseHandle(process);
    return result;
#else
    if (pid == 0 || pid > std::uint32_t(std::numeric_limits<pid_t>::max()))
        return 0;
#if JUCE_MAC
    proc_bsdinfo info{};
    if (proc_pidinfo(int(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info))
        return info.pbi_status == SZOMB ? 0 :
            std::uint64_t(info.pbi_start_tvsec) * 1000000 + info.pbi_start_tvusec;
#else
    const auto stat = juce::File("/proc/" + juce::String(pid) + "/stat").loadFileAsString();
    // comm (field 2) can contain whitespace and parentheses. Parse after its last ')'.
    const auto end = stat.lastIndexOfChar(')');
    if (end >= 0)
    {
        const auto fields = juce::StringArray::fromTokens(stat.substring(end + 1).trim(), " ", "");
        if (fields.size() > 19)
            return fields[0] == "Z" ? 0 : std::uint64_t(fields[19].getLargeIntValue());
    }
#endif
    // Preserve another live session when its creation metadata is inaccessible.
    return kill(pid_t(pid), 0) == 0 || errno == EPERM ? std::numeric_limits<std::uint64_t>::max() : 0;
#endif
}
} // namespace

juce::File Studio::autosaveDirectory() const
{
    return profileFile().getSiblingFile("autosave");
}

juce::File Studio::sessionMarkerFile() const
{
    return autosaveDirectory().getChildFile("session-" + juce::String(currentProcessId()) + ".json");
}

juce::File Studio::autosaveFile() const
{
    const auto stem = projectFile == juce::File{} ? juce::String("Untitled") : projectFile.getFileNameWithoutExtension();
    return autosaveDirectory().getChildFile(stem + " (" + juce::String(currentProcessId()) + ").autosave.vibra");
}

void Studio::writeSessionMarker()
{
    auto marker = std::make_unique<juce::DynamicObject>();
    marker->setProperty("pid", int(currentProcessId()));
    marker->setProperty("created", juce::String(juce::int64(processCreation(currentProcessId()))));
    marker->setProperty("autosave", lastAutosaveFile.getFullPathName());
    marker->setProperty("project", projectFile.getFullPathName());
    marker->setProperty("time", juce::Time::getCurrentTime().toISO8601(true));
    autosaveDirectory().createDirectory();
    sessionMarkerFile().replaceWithText(juce::JSON::toString(juce::var(marker.release())));
}

void Studio::endAutosaveSession()
{
    if (!autosaveEnabled)
        return;
    autosaveEnabled = false;
    // A clean exit leaves nothing to recover: the user saved or chose to discard.
    if (lastAutosaveFile.existsAsFile())
        lastAutosaveFile.deleteFile();
    sessionMarkerFile().deleteFile();
}

std::optional<Studio::CrashedSession> Studio::findCrashedSession()
{
    // Markers whose process is gone (or whose PID now belongs to another process) are crashes.
    std::vector<CrashedSession> crashes;
    for (const auto &marker : autosaveDirectory().findChildFiles(juce::File::findFiles, false, "session-*.json"))
    {
        const auto json = juce::JSON::parse(marker);
        const auto pid = std::uint32_t(int(json["pid"]));
        const auto created = std::uint64_t(json["created"].toString().getLargeIntValue());
        const auto liveCreation = processCreation(pid);
        if (pid == currentProcessId() || (pid != 0 && created != 0 &&
            (liveCreation == created || liveCreation == std::numeric_limits<std::uint64_t>::max())))
            continue; // This session, or another VibeRa still running.
        const juce::File autosave(json["autosave"].toString());
        if (json["autosave"].toString().isNotEmpty() && juce::File::isAbsolutePath(autosave.getFullPathName()) &&
            autosave.existsAsFile())
            crashes.push_back({marker, autosave,
                               json["project"].toString().isNotEmpty() ? juce::File(json["project"].toString())
                                                                       : juce::File{},
                               autosave.getLastModificationTime()});
        else
            marker.deleteFile(); // Nothing was autosaved before it closed.
    }
    if (crashes.empty())
        return std::nullopt;
    std::sort(crashes.begin(), crashes.end(), [](const auto &a, const auto &b) { return a.time > b.time; });
    for (size_t i = 1; i < crashes.size(); ++i)
        crashes[i].marker.deleteFile(); // Older autosaves stay in the folder (File > Open).
    return crashes.front();
}

void Studio::startAutosaveSession()
{
    autosaveEnabled = true;
    lastAutosaveTime = juce::Time::getMillisecondCounterHiRes();
    autosaveRevision = controller.revision;
    const auto crashed = findCrashedSession();
    writeSessionMarker();
    if (!crashed)
        return;
    const auto newest = *crashed;
    auto safe = juce::Component::SafePointer<Studio>(this);
    juce::MessageManager::callAsync(
        [safe, newest]
        {
            if (!safe)
                return;
            const auto name = newest.project == juce::File{} ? juce::String("an unsaved project")
                                                             : newest.project.getFileName();
            auto options = juce::MessageBoxOptions()
                               .withIconType(juce::MessageBoxIconType::QuestionIcon)
                               .withTitle("Recover work")
                               .withMessage("VibeRa Studio closed unexpectedly while working on " + name +
                                            ".\nRecover the autosave from " +
                                            newest.time.toString(true, true, false, true) + "?")
                               .withButton("Recover")
                               .withButton("Discard")
                               .withAssociatedComponent(safe.getComponent());
            juce::AlertWindow::showAsync(options,
                                         [safe, newest](int button)
                                         {
                                             if (!safe)
                                                 return;
                                             if (button == 1)
                                                 safe->recoverAutosave(newest.autosave, newest.project);
                                             else
                                                 newest.autosave.deleteFile();
                                             newest.marker.deleteFile();
                                         });
        });
}

juce::Result Studio::recoverAutosave(const juce::File &autosave, const juce::File &original)
{
    auto result = openProjectFile(autosave, true);
    if (result.failed())
        return result;
    // The recovered work belongs to the original file but is not saved there yet.
    projectFile = original;
    controller.dirty = true;
    status = "Recovered autosave from " + autosave.getLastModificationTime().toString(true, true, false, true) +
             (original == juce::File{} ? juce::String(": save it to keep it") : ": save to keep it in " + original.getFileName());
    // Keep the recovered file until this session autosaves or saves over it.
    lastAutosaveFile = autosave;
    writeSessionMarker();
    repaint();
    return juce::Result::ok();
}

void Studio::serviceAutosave()
{
    if (!autosaveEnabled || autosaveRunning || !controller.dirty || controller.revision == autosaveRevision)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastAutosaveTime < autosaveSeconds * 1000.)
        return;
    // Capture only between edits and with the transport stopped, so autosave never touches
    // playback, recording or an open gesture; it retries on the next timer tick.
    if (audio.playing() || audioTaking || midiTaking || hasPendingMidiTake() || controller.hasGesture() ||
        afterPluginState || pendingExport || audio.rendering() || devicePreparationBusy() || routingPending())
        return;
    lastAutosaveTime = now;
    autosaveNow(nullptr);
}

void Studio::autosaveNow(std::function<void(juce::Result)> completion)
{
    autosaveRunning = true;
    const auto previousStatus = status;
    const auto revision = controller.revision;
    const auto target = autosaveFile();
    auto safe = juce::Component::SafePointer<Studio>(this);
    withPluginState(
        [safe, target, revision, previousStatus, completion](juce::Result result, Project snapshot)
        {
            if (!safe)
                return;
            safe->autosaveRunning = false;
            if (result.wasOk())
                result = saveProjectSnapshot(snapshot, target);
            safe->status = previousStatus; // Autosave is quiet; failures retry later.
            if (result.wasOk())
            {
                if (safe->lastAutosaveFile != target && safe->lastAutosaveFile.existsAsFile())
                    safe->lastAutosaveFile.deleteFile(); // Project renamed by Save As.
                safe->lastAutosaveFile = target;
                safe->autosaveRevision = revision;
                safe->writeSessionMarker();
            }
            if (completion)
                completion(result);
        });
}

void Studio::rotateBackups(const juce::File &project)
{
    if (!project.existsAsFile())
        return;
    // FL-style backups beside the profile: <name> (1..5).vibra, 1 the most recent.
    const auto folder = profileFile().getSiblingFile("backups").getChildFile(project.getFileNameWithoutExtension());
    if (!folder.createDirectory())
        return;
    auto slot = [&](int n) { return folder.getChildFile(project.getFileNameWithoutExtension() + " (" + juce::String(n) + ").vibra"); };
    slot(backupCount).deleteFile();
    for (int n = backupCount - 1; n >= 1; --n)
        if (slot(n).existsAsFile())
            slot(n).moveFileTo(slot(n + 1));
    // Copy with absolute media paths so the backup opens from its own folder.
    Project previous;
    if (loadProject(project, previous).wasOk())
        saveProjectSnapshot(previous, slot(1));
}
void Studio::verifyAutosave(const juce::File &folder, std::function<void(juce::Result)> completion)
{
    auto fail = [completion](const juce::String &message) { completion(juce::Result::fail("Autosave: " + message)); };
    // Isolated profile: autosave, markers and backups land under `folder`.
    folder.getChildFile("profile").createDirectory();
    workflowTestSettings = std::make_unique<juce::TemporaryFile>(folder.getChildFile("profile/shortcuts.json"));
    autosaveDirectory().deleteRecursively();
    profileFile().getSiblingFile("backups").deleteRecursively();
    const auto project = folder.getChildFile("autosave-song.vibra");
    project.deleteFile();
    auto *channel = controller.project.channels.empty() ? nullptr : &controller.project.channels.front();
    if (!channel)
        return fail("no Channel to write notes to");
    const auto channelId = channel->id;
    auto writeNote = [this, channelId](double beat)
    {
        CommandArgs a;
        a.pattern = selectedPattern;
        a.channel = channelId;
        a.enabled = true;
        a.recordedNotes.push_back({0, channelId, beat, .25, 60, .8f, false});
        command("notes.import", a);
    };
    auto noteCount = [](const Project &p) { size_t n = 0; for (const auto &pattern : p.patterns) n += pattern.notes.size(); return n; };
    writeNote(0);
    autosaveEnabled = true;
    autosaveSeconds = 0;
    lastAutosaveTime = 0;
    autosaveRevision = 0;
    serviceAutosave();
    auto safe = juce::Component::SafePointer<Studio>(this);
    // Steps run from timers so asynchronous plugin-state capture can finish.
    auto after = std::make_shared<std::function<void(std::function<bool()>, std::function<void()>)>>();
    *after = [after](std::function<bool()> ready, std::function<void()> next)
    {
        juce::Timer::callAfterDelay(50, [after, ready, next] { if (ready()) next(); else (*after)(ready, next); });
    };
    (*after)([safe] { return !safe || !safe->autosaveRunning; },
        [safe, fail, completion, after, project, writeNote, noteCount]
        {
            if (!safe)
                return;
            auto &s = *safe;
            const auto autosave = s.lastAutosaveFile;
            Project saved;
            if (!autosave.existsAsFile() || loadProject(autosave, saved).failed() ||
                noteCount(saved) != noteCount(s.controller.project))
                return fail("the dirty project was not autosaved (" + autosave.getFullPathName() + ")");
            if (s.autosaveRevision != s.controller.revision)
                return fail("autosave did not record its revision");
            const auto marker = juce::JSON::parse(s.sessionMarkerFile());
            if (marker["autosave"].toString() != autosave.getFullPathName())
                return fail("session marker does not point at the autosave");
            // No new edit: no second autosave.
            const auto stamp = autosave.getLastModificationTime();
            s.serviceAutosave();
            if (s.autosaveRunning)
                return fail("autosaved again without a new edit");
            // A live session's marker is not a crash; a dead PID's marker is.
            if (s.findCrashedSession())
                return fail("the running session was taken for a crash");
            auto dead = marker.getDynamicObject()->clone();
            dead->setProperty("pid", 1);
            dead->setProperty("created", "1");
            const auto deadMarker = s.autosaveDirectory().getChildFile("session-1.json");
            deadMarker.replaceWithText(juce::JSON::toString(juce::var(dead.release())));
            const auto crashed = s.findCrashedSession();
            if (!crashed || crashed->autosave != autosave || crashed->marker != deadMarker)
                return fail("an unclean exit was not found");
            // Recovery opens the autosave as unsaved work for the original file.
            const auto notes = noteCount(s.controller.project);
            s.controller.dirty = false;
            if (auto result = s.recoverAutosave(autosave, project); result.failed())
                return fail("recover: " + result.getErrorMessage());
            if (s.projectFile != project || !s.controller.dirty || noteCount(s.controller.project) != notes)
                return fail("recovered project lost its file, dirty state or notes");
            deadMarker.deleteFile();
            juce::ignoreUnused(stamp);
            // Backups: each Save keeps the previous version, at most five.
            auto saves = std::make_shared<int>(0);
            auto saveNext = std::make_shared<std::function<void()>>();
            *saveNext = [safe, fail, completion, project, writeNote, noteCount, saves, saveNext]
            {
                if (!safe)
                    return;
                auto &st = *safe;
                if (*saves == 7)
                {
                    const auto folder = st.profileFile().getSiblingFile("backups").getChildFile("autosave-song");
                    const auto slots = folder.findChildFiles(juce::File::findFiles, false, "*.vibra");
                    Project latest, newestBackup;
                    if (slots.size() != size_t(backupCount) || folder.getChildFile("autosave-song (6).vibra").exists())
                        return fail("expected 5 rotating backups, found " + juce::String(slots.size()));
                    if (loadProject(project, latest).failed() ||
                        loadProject(folder.getChildFile("autosave-song (1).vibra"), newestBackup).failed() ||
                        noteCount(newestBackup) + 1 != noteCount(latest))
                        return fail("backup (1) is not the version before the last Save");
                    // A clean exit removes this session's autosave and marker.
                    const auto autosaveFile = st.lastAutosaveFile, marker = st.sessionMarkerFile();
                    st.endAutosaveSession();
                    if (autosaveFile.existsAsFile() || marker.existsAsFile())
                        return fail("clean exit left the autosave or marker");
                    st.workflowTestSettings.reset();
                    return completion(juce::Result::ok());
                }
                writeNote(4. + *saves);
                st.saveTo(project,
                          [fail, saves, saveNext](juce::Result result)
                          {
                              if (result.failed())
                                  return fail("save " + juce::String(*saves) + ": " + result.getErrorMessage());
                              ++*saves;
                              juce::MessageManager::callAsync([saveNext] { (*saveNext)(); });
                          });
            };
            (*saveNext)();
        });
}
} // namespace vibera
