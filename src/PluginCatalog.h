// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <vector>
#include <deque>
#include <optional>
namespace vibera
{
class PluginCatalog final : private juce::Thread
{
    juce::File file;
    mutable juce::CriticalSection lock;
    juce::StringArray folders, blacklist, failures;
    std::vector<juce::PluginDescription> plugins;
    juce::String progress;
    int completed = 0, total = 0;
    juce::File scannerExecutable;
    bool includeStandard = true;
    int timeoutMs = 15000;
    void run() override;
    juce::Result save();

  public:
    explicit PluginCatalog(juce::File settingsFile, juce::File scanner = {}, bool standard = true,
                           int timeout = 15000);
    ~PluginCatalog() override;
    static juce::StringArray standardFolders();
    struct Snapshot
    {
        juce::StringArray folders, blacklist, failures;
        std::vector<juce::PluginDescription> plugins;
        juce::String progress;
        int completed = 0, total = 0;
        bool scanning = false;
        bool standardLocations = true, requestedStandardLocations = true;
        juce::String operation, completionState = "idle", errorCode, error;
        juce::StringArray requestedFolders;
        bool cancelRequested = false, persisted = false;
    };
    Snapshot snapshot() const;
    juce::Result scan(juce::StringArray customFolders, bool retryBlacklisted = false,
                      std::optional<bool> standardLocations = {}, juce::String *operation = nullptr);
    juce::Result cancel(const juce::String &operation);
    bool operation(const juce::String &, Snapshot &) const;
    const juce::File &settingsFile() const noexcept
    {
        return file;
    }
    static juce::Result scanOne(const juce::File &binary, std::vector<juce::PluginDescription> &,
                                int timeoutMs = 15000, juce::File scanner = {},
                                const std::function<bool()> &cancelled = {});

  private:
    Snapshot current;
    std::deque<Snapshot> history;
    juce::StringArray scanFolders;
    bool scanStandard = true, retryBlacklist = false, commitStarted = false;
};
} // namespace vibera
