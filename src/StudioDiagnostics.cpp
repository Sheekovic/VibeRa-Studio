// SPDX-License-Identifier: AGPL-3.0-or-later
// Diagnostics export (RQ-G): a plain-text report a user can attach to a bug report.
#include "Studio.h"
#include "VibeRaVersion.h"

namespace vibera
{
namespace
{
// Removes the user's home folder and account name, in both slash styles.
juce::String anonymise(juce::String text)
{
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
    for (const auto &form : {home, home.replaceCharacter('\\', '/')})
        if (form.length() > 3)
            text = text.replace(form, "%USERPROFILE%", true);
    const auto user = juce::SystemStats::getLogonName();
    if (user.length() > 1)
        text = text.replace(user, "<user>", true);
    return text;
}

juce::String pluginName(const Project &project, Id id)
{
    const auto *spec = project.plugin(id);
    if (!spec)
        return "(removed)";
    if (const auto xml = juce::XmlDocument::parse(spec->descriptor))
        if (const auto name = xml->getStringAttribute("name"); name.isNotEmpty())
            return name;
    return "(unnamed)";
}
} // namespace

void Studio::recordDiagnosticStatus()
{
    if (status == lastDiagnosticStatus)
        return;
    lastDiagnosticStatus = status;
    // Typed failures look like "area.code: message"; keep the last 20 with their time.
    const auto code = status.upToFirstOccurrenceOf(":", false, false);
    if (code.isEmpty() || code.length() == status.length() || !code.containsChar('.') || code.containsChar(' '))
        return;
    recentErrors.add(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + status);
    while (recentErrors.size() > 20)
        recentErrors.remove(0);
}

juce::String Studio::diagnosticsReport() const
{
    juce::StringArray lines;
    lines.add("VibeRa Studio diagnostics");
    lines.add("Version: " + juce::String(applicationVersion));
    lines.add("Created: " + juce::Time::getCurrentTime().toISO8601(true));
    lines.add("OS: " + juce::SystemStats::getOperatingSystemName() +
              (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : ""));
    lines.add("CPU: " + juce::SystemStats::getCpuModel() + ", " + juce::String(juce::SystemStats::getNumCpus()) +
              " logical cores");
    lines.add("Memory: " + juce::String(juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
    lines.add("Audio device: " + audio.deviceDescription());
    lines.add("Transport: " + juce::String(audio.playing() ? "playing" : "stopped") + ", " +
              (patternMode ? "Pattern" : "Song") + " mode");
    lines.add("");
    const auto &project = controller.project;
    lines.add("Project: " + juce::String(int(project.channels.size())) + " Channels, " +
              juce::String(int(project.patterns.size())) + " Patterns, " + juce::String(int(project.mixer.size())) +
              " Mixer tracks, schema " + juce::String(Project::currentVersion) +
              (controller.dirty ? ", unsaved changes" : ""));
    lines.add("");
    const auto workers = audio.pluginDiagnostics();
    lines.add("Plugin workers: " + juce::String(int(workers.size())));
    for (const auto &worker : workers)
        lines.add("  " + pluginName(project, worker.id) + ": " + (worker.alive ? "running" : "stopped") +
                  ", latency " + juce::String(worker.latency) + " samples, packets processed " +
                  juce::String(juce::int64(worker.packets.processed)) + ", missed " +
                  juce::String(juce::int64(worker.packets.missed)) + ", dropped " +
                  juce::String(juce::int64(worker.packets.dropped)) + ", status " + audio.pluginStatus(worker.id));
    lines.add("");
    lines.add("Current status: " + status);
    lines.add("Recent errors (" + juce::String(recentErrors.size()) + "):");
    for (const auto &line : recentErrors)
        lines.add("  " + line);
    lines.add("");
    lines.add("Personal folders and the account name are replaced; no project content or credentials are included.");
    return anonymise(lines.joinIntoString("\n")) + "\n";
}

juce::Result Studio::writeDiagnostics(const juce::File &file, bool overwrite) const
{
    if (!file.hasFileExtension("txt"))
        return juce::Result::fail("invalid_arguments: Diagnostics are written to a .txt file");
    if (file.exists() && !overwrite)
        return juce::Result::fail("already_exists: The file exists; pass overwrite to replace it");
    if (!file.getParentDirectory().isDirectory())
        return juce::Result::fail("invalid_arguments: The folder does not exist");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(diagnosticsReport(), false, false, "\n") || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("io_error: Could not write " + file.getFileName());
    return juce::Result::ok();
}
} // namespace vibera
