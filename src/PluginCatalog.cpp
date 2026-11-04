// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginCatalog.h"
#include "PluginPolicy.h"
#include "WorkerProcess.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace vibera
{
namespace
{
#if JUCE_WINDOWS
constexpr bool ignorePathCase = true;
#else
constexpr bool ignorePathCase = false;
#endif
}
PluginCatalog::PluginCatalog(juce::File settingsFile, juce::File scanner, bool standard, int timeout)
    : Thread("VST3 isolated scan"), file(std::move(settingsFile)), scannerExecutable(std::move(scanner)),
      includeStandard(standard), timeoutMs(timeout)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml && xml->hasTagName("VIBERA_PLUGINS") && xml->getIntAttribute("version") == 1)
    {
        includeStandard = xml->getBoolAttribute("standardLocations", standard);
        for (auto *child : xml->getChildIterator())
        {
            if (child->hasTagName("FOLDER"))
                folders.add(child->getStringAttribute("path"));
            else if (child->hasTagName("BLACKLIST"))
                blacklist.add(child->getStringAttribute("path"));
            else
            {
                juce::PluginDescription type;
                if (type.loadFromXml(*child) && type.pluginFormatName == "VST3")
                    plugins.push_back(type);
            }
        }
    }
}
PluginCatalog::~PluginCatalog()
{
    signalThreadShouldExit();
    stopThread(17000);
}
juce::StringArray PluginCatalog::standardFolders()
{
#if JUCE_WINDOWS
    return {"C:/Program Files/Common Files/VST3",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("AppData/Local/Programs/Common/VST3")
                .getFullPathName()};
#elif JUCE_MAC
    return {"/Library/Audio/Plug-Ins/VST3",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/Audio/Plug-Ins/VST3").getFullPathName()};
#else
    return {"/usr/lib/vst3", "/usr/local/lib/vst3",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile(".vst3").getFullPathName()};
#endif
}
PluginCatalog::Snapshot PluginCatalog::snapshot() const
{
    const juce::ScopedLock guard(lock);
    auto result = current;
    result.folders = folders;
    result.blacklist = blacklist;
    result.failures = failures;
    result.plugins = plugins;
    result.progress = progress;
    result.completed = completed;
    result.total = total;
    result.scanning = isThreadRunning() || current.completionState == "pending";
    result.standardLocations = includeStandard;
    return result;
}
juce::Result PluginCatalog::scan(juce::StringArray customFolders, bool retry, std::optional<bool> standard,
                                 juce::String *operationId)
{
    customFolders.trim();
    customFolders.removeEmptyStrings();
    if (customFolders.size() > 128)
        return juce::Result::fail("At most128 custom VST3 folders are supported");
    for (auto &path : customFolders)
        if (path.length() > 4096 || !juce::File::isAbsolutePath(path))
            return juce::Result::fail("Custom VST3 folders must be absolute paths up to4096 characters");
    for (int i = 0; i < customFolders.size(); ++i)
        customFolders.set(i, juce::File(customFolders[i]).getFullPathName());
    customFolders.removeDuplicates(ignorePathCase);
    const juce::ScopedLock guard(lock);
    if (isThreadRunning() || current.completionState == "pending")
        return juce::Result::fail("A VST3 scan is already running");
    if (current.operation.isNotEmpty())
    {
        history.push_back(snapshot());
        if (history.size() > 16)
            history.pop_front();
    }
    scanFolders = std::move(customFolders);
    scanStandard = standard.value_or(includeStandard);
    retryBlacklist = retry;
    commitStarted = false;
    current = {};
    current.operation = juce::Uuid().toString();
    current.completionState = "pending";
    current.requestedFolders = scanFolders;
    current.requestedStandardLocations = scanStandard;
    failures.clear();
    completed = total = 0;
    progress = "Finding VST3 bundles...";
    if (!startThread())
    {
        current.completionState = "failed";
        current.errorCode = "worker_start_failed";
        current.error = "Cannot start VST3 discovery thread";
        return juce::Result::fail(current.error);
    }
    if (operationId)
        *operationId = current.operation;
    return juce::Result::ok();
}
juce::Result PluginCatalog::cancel(const juce::String &id)
{
    const juce::ScopedLock guard(lock);
    if (id.isEmpty() || id != current.operation)
        return juce::Result::fail("Unknown/expired active VST3 scan operation");
    if (current.completionState == "pending")
    {
        if (commitStarted)
            return juce::Result::fail("Scan settings commit has started; observe the actual result");
        current.cancelRequested = true;
        signalThreadShouldExit();
    }
    return juce::Result::ok();
}
bool PluginCatalog::operation(const juce::String &id, Snapshot &out) const
{
    const juce::ScopedLock guard(lock);
    if (id.isNotEmpty() && id == current.operation)
    {
        out = snapshot();
        return true;
    }
    for (const auto &item : history)
        if (item.operation == id)
        {
            out = item;
            return true;
        }
    return false;
}
juce::Result PluginCatalog::scanOne(const juce::File &binary, std::vector<juce::PluginDescription> &types,
                                    int timeoutMs, juce::File scanner, const std::function<bool()> &cancelled)
{
    if (cancelled && cancelled())
        return juce::Result::fail("VST3 scan cancelled");
    auto output = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getNonexistentChildFile("vibera-scan-", ".xml");
    juce::TemporaryFile temporary(output);
    WorkerProcess worker;
    if (scanner == juce::File{})
        scanner = pluginWorkerExecutable();
    if (!worker.start({scanner.getFullPathName(), "--scan", binary.getFullPathName(),
                       temporary.getFile().getFullPathName()},
                      0))
        return juce::Result::fail("Cannot start isolated VST3 scanner");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    bool finished = false;
    while (!(cancelled && cancelled()) && juce::Time::getMillisecondCounterHiRes() < deadline)
        if (worker.waitForProcessToFinish(
                juce::jlimit(1, 50, int(deadline - juce::Time::getMillisecondCounterHiRes()))))
        {
            finished = true;
            break;
        }
    if (!finished)
    {
        worker.requestGracefulShutdown();
        if (!worker.waitForProcessToFinish(2000) && allowForcedWorkerTermination)
            worker.kill();
        return juce::Result::fail(cancelled && cancelled() ? "VST3 scan cancelled" : "VST3 scan timed out");
    }
    if (worker.getExitCode() != 0)
        return juce::Result::fail("VST3 scanner failed or crashed (exit " +
                                  juce::String(worker.getExitCode()) + ")");
    auto xml = juce::XmlDocument::parse(temporary.getFile());
    if (!xml || !xml->hasTagName("VST3_SCAN"))
        return juce::Result::fail("Invalid scanner response");
    for (auto *child : xml->getChildIterator())
    {
        juce::PluginDescription type;
        if (type.loadFromXml(*child) && type.pluginFormatName == "VST3")
            types.push_back(type);
    }
    return types.empty() ? juce::Result::fail("No VST3 classes found") : juce::Result::ok();
}
void PluginCatalog::run()
{
    juce::Array<juce::File> candidates;
    juce::StringArray paths, blocked;
    bool retry = false;
    {
        const juce::ScopedLock guard(lock);
        paths = scanStandard ? standardFolders() : juce::StringArray{};
        paths.addArray(scanFolders);
        blocked = blacklist;
        retry = retryBlacklist;
    }
    paths.removeDuplicates(ignorePathCase);
    auto walk = [&](auto &&self, const juce::File &directory, int depth) -> void
    {
        if (depth > 16 || threadShouldExit())
            return;
        for (auto &f : directory.findChildFiles(juce::File::findFilesAndDirectories, false))
        {
            if (f.hasFileExtension("vst3"))
            {
                if (!candidates.contains(f))
                    candidates.add(f);
                continue;
            }
#if JUCE_WINDOWS
            auto attributes = GetFileAttributesW(f.getFullPathName().toWideCharPointer());
            if (f.isDirectory() && attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
#else
            if (f.isDirectory() && !f.isSymbolicLink())
#endif
                self(self, f, depth + 1);
        }
    };
    for (auto &path : paths)
        walk(walk, juce::File(path), 0);
    {
        const juce::ScopedLock guard(lock);
        total = candidates.size();
    }
    std::vector<juce::PluginDescription> found;
    for (auto &candidate : candidates)
    {
        if (threadShouldExit())
            break;
        bool skip = false;
        {
            const juce::ScopedLock guard(lock);
            skip = !retry && blocked.contains(candidate.getFullPathName(), ignorePathCase);
            progress =
                skip ? "Blacklisted: " + candidate.getFileName() : "Scanning: " + candidate.getFileName();
        }
        if (!skip)
        {
            std::vector<juce::PluginDescription> result;
            auto scanResult = scanOne(candidate, result, timeoutMs, scannerExecutable,
                                      [this] { return threadShouldExit(); });
            if (threadShouldExit())
                break; // Cancellation is not evidence that this plugin is defective.
            const juce::ScopedLock guard(lock);
            if (scanResult.failed())
            {
                blocked.addIfNotAlreadyThere(candidate.getFullPathName(), ignorePathCase);
                failures.add(candidate.getFullPathName() + ": " + scanResult.getErrorMessage());
            }
            else
            {
                blocked.removeString(candidate.getFullPathName(), ignorePathCase);
                found.insert(found.end(), result.begin(), result.end());
            }
        }
        {
            const juce::ScopedLock guard(lock);
            ++completed;
        }
    }
    {
        const juce::ScopedLock guard(lock);
        if (threadShouldExit())
        {
            current.completionState = "cancelled";
            current.cancelRequested = true;
            progress = "Scan cancelled; previous catalogue/settings retained";
            return;
        }
        // Cancellation after this admission is too late to roll back the settings
        // commit. cancel() reports the actual terminal outcome, not an invented undo.
        commitStarted = true;
        folders = scanFolders;
        includeStandard = scanStandard;
        blacklist = std::move(blocked);
        plugins = std::move(found);
        progress = "Saving scan results...";
    }
    auto saved = save(); // No catalogue lock held across disk I/O.
    {
        const juce::ScopedLock guard(lock);
        current.persisted = saved.wasOk();
        current.completionState = saved.wasOk() ? "completed" : "failed";
        if (saved.failed())
        {
            current.errorCode = "persistence_failed";
            current.error = saved.getErrorMessage();
        }
        progress = saved.wasOk() ? "Scan complete" : "Scan results could not be saved";
    }
}
juce::Result PluginCatalog::save()
{
    auto data = snapshot();
    juce::XmlElement xml("VIBERA_PLUGINS");
    xml.setAttribute("version", 1);
    xml.setAttribute("standardLocations", data.standardLocations);
    for (auto &path : data.folders)
        xml.createNewChildElement("FOLDER")->setAttribute("path", path);
    for (auto &path : data.blacklist)
        xml.createNewChildElement("BLACKLIST")->setAttribute("path", path);
    for (auto &type : data.plugins)
        xml.addChildElement(type.createXml().release());
    if (auto created = file.getParentDirectory().createDirectory(); created.failed())
        return created;
    juce::TemporaryFile temporary(file);
    if (!xml.writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot atomically save VST3 catalogue: " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace vibera
