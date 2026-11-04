// SPDX-License-Identifier: AGPL-3.0-or-later
// In-app updates from GitHub Releases: check (at most daily on start, or from the Tools
// menu), show what changed, and download/install only after the user approves. Network
// work runs on a worker thread that owns its state, so closing VibeRa never waits on it.
#include "Studio.h"
#include "ai/AiHttp.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if JUCE_WINDOWS
#include <windows.h>
#endif
#include <thread>

namespace vibera
{
struct Studio::UpdateJob
{
    std::thread thread;
    std::atomic<bool> done{false}, cancel{false};
    std::atomic<double> progress{0};
    bool download = false, manual = false, interactive = true;
    juce::Result result = juce::Result::ok();
    std::vector<updates::Release> releases;
    juce::File file;
};

namespace
{
const juce::StringPairArray &githubHeaders()
{
    static const auto headers = []
    {
        juce::StringPairArray result;
        result.set("User-Agent", "VibeRa-Studio-Updater"); // GitHub's API requires a client name.
        result.set("Accept", "application/vnd.github+json");
        return result;
    }();
    return headers;
}
updates::Version currentVersion()
{
    auto *app = juce::JUCEApplication::getInstance();
    return updates::parseVersion(app ? app->getApplicationVersion() : juce::String()).value_or(updates::Version{});
}
} // namespace

juce::File Studio::updateSettingsFile() const
{
    return profileFile().getSiblingFile("updates.json");
}
juce::var Studio::loadUpdateSettings() const
{
    auto settings = updateSettingsFile().existsAsFile() ? juce::JSON::parse(updateSettingsFile()) : juce::var();
    if (!settings.isObject())
        settings = juce::var(new juce::DynamicObject());
    auto *o = settings.getDynamicObject();
    if (!o->hasProperty("autoCheck"))
        o->setProperty("autoCheck", true);
    if (!o->hasProperty("includePreview"))
        o->setProperty("includePreview", true); // Every VibeRa release so far is a preview.
    return settings;
}
void Studio::saveUpdateSettings(const juce::var &settings)
{
    const auto file = updateSettingsFile();
    if (!file.getParentDirectory().createDirectory() || !file.replaceWithText(juce::JSON::toString(settings)))
        status = "Cannot save update settings to " + file.getFullPathName();
}

void Studio::scheduleStartupUpdateCheck()
{
    if (juce::SystemStats::getEnvironmentVariable("VIBERA_NO_UPDATE_CHECK", {}).isNotEmpty())
        return;
    const auto settings = loadUpdateSettings();
    const double last = double(settings["lastCheck"]);
    if (!bool(settings["autoCheck"]) || double(juce::Time::currentTimeMillis()) - last < 24. * 3600. * 1000.)
        return;
    juce::Component::SafePointer<Studio> safe(this);
    juce::Timer::callAfterDelay(8000, [safe]
                                {
                                    if (safe && !safe->updateJob)
                                        (void)safe->checkForUpdates(false);
                                });
}

juce::Result Studio::checkForUpdates(bool manual, bool interactive)
{
    if (updateJob)
        return juce::Result::fail("update.busy: An update check or download is already running");
    auto job = std::make_shared<UpdateJob>();
    job->manual = manual;
    job->interactive = interactive;
    updatePhase = "checking";
    updateMessage = "Checking for updates...";
    if (manual)
        status = updateMessage;
    job->thread = std::thread(
        [job]
        {
            const auto response = ai::httpRequest("GET", updates::releasesUrl(), githubHeaders(), {}, 15000,
                                                  [job] { return job->cancel.load(); });
            if (!response.ok())
                job->result = juce::Result::fail(
                    "update.network: " + (response.error.isNotEmpty() ? response.error
                                                                      : "GitHub returned " + juce::String(response.status)));
            else if (const auto json = juce::JSON::parse(response.body); !json.isArray())
                job->result = juce::Result::fail("update.response: GitHub's release list could not be read");
            else
                job->releases = updates::parseReleases(json);
            job->done = true;
        });
    updateJob = std::move(job);
    return juce::Result::ok();
}

juce::Result Studio::downloadUpdate()
{
    if (updateJob)
        return juce::Result::fail("update.busy: An update check or download is already running");
    if (!availableUpdate)
        return juce::Result::fail("update.none: Check for updates first");
    const auto *asset = updates::installerAsset(*availableUpdate);
    if (!asset)
        return juce::Result::fail("update.asset: " + availableUpdate->tag + " has no download for " +
                                  updates::platformKey());
    const auto *sum = updates::checksumAsset(*availableUpdate, *asset);
    if (!sum)
        return juce::Result::fail("update.checksum: " + asset->name +
                                  " has no published checksum; an unverifiable file is not downloaded");
    auto job = std::make_shared<UpdateJob>();
    job->download = true;
    job->file = profileFile().getSiblingFile("updates").getChildFile(availableUpdate->tag).getChildFile(asset->name);
    const auto url = asset->url, checksumUrl = sum->url, name = asset->name;
    updatePhase = "downloading";
    updateProgress = 0;
    updateMessage = "Downloading " + name + "...";
    status = updateMessage;
    job->thread = std::thread(
        [job, url, checksumUrl, name]
        {
            const auto cancelled = [job] { return job->cancel.load(); };
            const auto sum = ai::httpRequest("GET", checksumUrl, githubHeaders(), {}, 20000, cancelled);
            const auto expected = sum.ok() ? updates::parseChecksum(sum.body, name) : std::nullopt;
            if (!expected)
                job->result = juce::Result::fail("update.checksum: The published checksum could not be read");
            else if (auto got = ai::httpDownload(
                         url, job->file, [job](double fraction, double) { if (fraction >= 0) job->progress = fraction; },
                         cancelled);
                     got.failed())
                job->result = got;
            else if (updates::fileSha256(job->file) != *expected)
            {
                job->file.deleteFile();
                job->result = juce::Result::fail(
                    "update.checksum: The download does not match its published checksum and was deleted");
            }
            job->done = true;
        });
    updateJob = std::move(job);
    return juce::Result::ok();
}

void Studio::pollUpdates()
{
    if (!updateJob)
        return;
    if (!updateJob->done.load())
    {
        if (updateJob->download)
        {
            updateProgress = updateJob->progress.load();
            status = "Downloading update... " + juce::String(juce::roundToInt(updateProgress * 100)) + "%";
        }
        return;
    }
    updateJob->thread.join();
    const auto job = std::move(updateJob);
    if (job->download)
    {
        if (job->result.failed())
        {
            updatePhase = "failed";
            updateMessage = status = job->result.getErrorMessage();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Update download failed",
                                                   updateMessage, {}, this);
            return;
        }
        updateDownloaded = job->file;
        updatePhase = "ready";
        updateProgress = 1;
        updateMessage = status = "Update downloaded and verified: " + job->file.getFileName();
        offerUpdateInstall();
        return;
    }
    auto settings = loadUpdateSettings();
    settings.getDynamicObject()->setProperty("lastCheck", double(juce::Time::currentTimeMillis()));
    if (job->result.failed())
    {
        updatePhase = "failed";
        updateMessage = job->result.getErrorMessage();
        if (job->manual)
        {
            status = updateMessage;
            if (job->interactive)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Check for updates",
                                                       "Could not check for updates.\n\n" + updateMessage, {}, this);
        }
        return; // An automatic check fails quietly and retries on the next start.
    }
    saveUpdateSettings(settings);
    // A manual check also offers a version the user skipped before.
    availableUpdate = updates::newestUpdate(job->releases, currentVersion(), bool(settings["includePreview"]),
                                            job->manual ? juce::String() : settings["skipped"].toString());
    if (!availableUpdate)
    {
        updatePhase = "upToDate";
        updateMessage = "VibeRa Studio " + currentVersion().text() + " is the latest version";
        if (job->manual)
        {
            status = updateMessage;
            if (job->interactive)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Check for updates",
                                                       updateMessage + ".", {}, this);
        }
        return;
    }
    updatePhase = "available";
    updateMessage = availableUpdate->name + " is available";
    status = updateMessage + " (Tools > Check for updates)";
    if (job->interactive)
        showUpdateOffer();
}

void Studio::showUpdateOffer()
{
    if (!availableUpdate)
        return;
    const auto &release = *availableUpdate;
    auto *box = new juce::AlertWindow(
        "Update available",
        release.name + (release.prerelease ? " (preview)" : "") + " is available. You have VibeRa Studio " +
            currentVersion().text() + ".\n\nWhat changed:",
        juce::MessageBoxIconType::NoIcon, this);
    auto notes = std::make_shared<juce::TextEditor>();
    notes->setMultiLine(true, true);
    notes->setReadOnly(true);
    notes->setScrollbarsShown(true);
    notes->setText(release.notes.trim().isNotEmpty() ? release.notes.trim() : juce::String("No release notes."), false);
    notes->setSize(560, 280);
    box->addCustomComponent(notes.get());
    box->addButton("Download and install", 1, juce::KeyPress(juce::KeyPress::returnKey));
    box->addButton("Skip this version", 2);
    box->addButton("Later", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<Studio> safe(this);
    const auto tag = release.tag;
    box->enterModalState(true, juce::ModalCallbackFunction::create(
                                   [safe, notes, tag](int choice)
                                   {
                                       if (!safe)
                                           return;
                                       if (choice == 1)
                                       {
                                           if (auto started = safe->downloadUpdate(); started.failed())
                                               safe->status = started.getErrorMessage();
                                       }
                                       else if (choice == 2)
                                       {
                                           auto settings = safe->loadUpdateSettings();
                                           settings.getDynamicObject()->setProperty("skipped", tag);
                                           safe->saveUpdateSettings(settings);
                                           safe->updatePhase = "idle";
                                           safe->status = "Skipped " + tag + ". Tools > Check for updates offers it again.";
                                       }
                                   }),
                         true);
}

void Studio::offerUpdateInstall()
{
    juce::Component::SafePointer<Studio> safe(this);
#if JUCE_WINDOWS
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Install update")
            .withMessage("The update is downloaded and verified. VibeRa Studio will close, and the installer "
                         "will update it and keep your settings.")
            .withButton("Install now")
            .withButton("Later")
            .withAssociatedComponent(this),
        [safe](int result)
        {
            if (safe && result == 1)
                safe->installDownloadedUpdate();
        });
#else
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle("Update downloaded")
            .withMessage("The update is downloaded and verified:\n" + updateDownloaded.getFullPathName() +
                         "\n\nClose VibeRa Studio, then replace the app with the one in this archive.")
            .withButton("Show file")
            .withButton("OK")
            .withAssociatedComponent(this),
        [safe](int result)
        {
            if (safe && result == 1)
                safe->updateDownloaded.revealToUser();
        });
#endif
}

void Studio::installDownloadedUpdate()
{
    if (!updateDownloaded.existsAsFile())
    {
        status = "The downloaded update is missing; check for updates again";
        updatePhase = "failed";
        return;
    }
    if (controller.dirty)
    {
        // Never lose work for an update: the user saves first, then installs.
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save first",
                                               "Save your project (Ctrl+S), then choose Install again from "
                                               "Tools > Check for updates.",
                                               {}, this);
        return;
    }
#if JUCE_WINDOWS
    // The installer refuses to replace a running VibeRa: a hidden helper waits for this
    // process to exit, then starts the verified setup.
    auto quoted = updateDownloaded.getFullPathName().replace("'", "''");
    const auto command = "-NoProfile -WindowStyle Hidden -Command \"Wait-Process -Id " +
                         juce::String(juce::int64(GetCurrentProcessId())) +
                         " -ErrorAction SilentlyContinue; Start-Process -FilePath '" + quoted + "'\"";
    const auto powershell = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
                                .getChildFile("WindowsPowerShell/v1.0/powershell.exe");
    if (!powershell.existsAsFile() || !powershell.startAsProcess(command))
    {
        status = "Could not start the installer; run it yourself: " + updateDownloaded.getFullPathName();
        updateDownloaded.revealToUser();
        return;
    }
    updatePhase = "installing";
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
#else
    updateDownloaded.revealToUser();
#endif
}

void Studio::updateSettingsMenu()
{
    const auto settings = loadUpdateSettings();
    juce::PopupMenu menu;
    menu.addItem(1, "Check for updates now", !updateJob);
    if (availableUpdate && updatePhase == "available")
        menu.addItem(2, "Show " + availableUpdate->name + "...");
    if (updatePhase == "ready")
        menu.addItem(3, "Install downloaded update...");
    menu.addSeparator();
    menu.addItem(4, "Check automatically on start (daily)", true, bool(settings["autoCheck"]));
    menu.addItem(5, "Include preview releases", true, bool(settings["includePreview"]));
    juce::Component::SafePointer<Studio> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safe](int choice)
                       {
                           if (!safe || choice == 0)
                               return;
                           if (choice == 1)
                           {
                               if (auto started = safe->checkForUpdates(true); started.failed())
                                   safe->status = started.getErrorMessage();
                               return;
                           }
                           if (choice == 2)
                               return safe->showUpdateOffer();
                           if (choice == 3)
                               return safe->offerUpdateInstall();
                           auto settings = safe->loadUpdateSettings();
                           const juce::Identifier key = choice == 4 ? "autoCheck" : "includePreview";
                           settings.getDynamicObject()->setProperty(key, !bool(settings[key]));
                           safe->saveUpdateSettings(settings);
                       });
}

juce::var Studio::updateStatus() const
{
    auto *o = new juce::DynamicObject();
    const auto settings = loadUpdateSettings();
    o->setProperty("phase", updatePhase);
    o->setProperty("message", updateMessage);
    o->setProperty("currentVersion", currentVersion().text());
    o->setProperty("platform", updates::platformKey());
    o->setProperty("repository", juce::String(updates::repository));
    o->setProperty("progress", updateProgress);
    o->setProperty("autoCheck", bool(settings["autoCheck"]));
    o->setProperty("includePreview", bool(settings["includePreview"]));
    o->setProperty("skipped", settings["skipped"].toString());
    if (availableUpdate)
    {
        auto *u = new juce::DynamicObject();
        u->setProperty("tag", availableUpdate->tag);
        u->setProperty("name", availableUpdate->name);
        u->setProperty("version", availableUpdate->version.text());
        u->setProperty("prerelease", availableUpdate->prerelease);
        u->setProperty("notes", availableUpdate->notes);
        u->setProperty("page", availableUpdate->page);
        if (const auto *asset = updates::installerAsset(*availableUpdate))
            u->setProperty("asset", asset->name);
        o->setProperty("available", juce::var(u));
    }
    if (updateDownloaded != juce::File{})
        o->setProperty("downloaded", updateDownloaded.getFullPathName());
    return juce::var(o);
}

void Studio::cancelUpdateWork()
{
    if (!updateJob)
        return;
    updateJob->cancel = true;
    // The worker owns its state (shared_ptr), so it may finish after VibeRa closes.
    if (updateJob->thread.joinable())
        updateJob->thread.detach();
    updateJob.reset();
}
} // namespace vibera
