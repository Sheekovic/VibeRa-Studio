// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserSearchCore.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vibera::browser_search
{
// One owned background thread. No component pointers, callbacks into UI, or DSP
// access. Superseded searches/roots cannot publish. Shutdown is cooperative and
// does not wait for filesystem I/O on the UI thread.
class Service
{
  public:
    enum class Phase
    {
        idle,
        indexing,
        searching,
        ready,
        canceled,
        error
    };
    struct Result
    {
        std::uint64_t serial = 0;
        std::vector<Entry> matches;
        IndexStats stats;
        juce::String error, warning;
        double milliseconds = 0;
        bool fromCache = false;
        std::size_t widestRow = 0;
    };

  private:
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::vector<std::string> roots;
        Query query;
        bool queued = false, refresh = false;
        std::atomic<bool> quit{false};
        std::atomic<std::uint64_t> serial{0}, rootsSerial{0}, cancelSerial{0}, files{0};
        std::atomic<std::uint64_t> preparations{0};
        std::atomic<Phase> phase{Phase::idle};
        std::shared_ptr<const Result> completed;
        std::shared_ptr<std::atomic<bool>> terminal = std::make_shared<std::atomic<bool>>(false);
    };
    std::shared_ptr<Shared> state = std::make_shared<Shared>();
    std::thread worker;
    static void run(std::shared_ptr<Shared> shared, juce::File cache)
    {
        {
            Index index;
            std::uint64_t indexedRoots = ~std::uint64_t(0);
            std::vector<std::shared_ptr<const Result>> retired;
            while (!shared->quit.load())
            {
                std::erase_if(retired, [](const auto &p) { return p.use_count() == 1; });
                std::vector<std::string> roots;
                Query query;
                std::uint64_t serial = 0, rootsSerial = 0, cancelSerial = 0;
                bool refresh = false;
                {
                    std::unique_lock lock(shared->mutex);
                    shared->wake.wait_for(lock, std::chrono::milliseconds(50),
                                          [&] { return shared->quit || shared->queued; });
                    if (shared->quit)
                        break;
                    if (!shared->queued)
                        continue;
                    // Debounce latest query without retaining a UI object/timer.
                    auto observed = shared->serial.load();
                    while (
                        shared->wake.wait_for(lock, std::chrono::milliseconds(150), [&]
                                              { return shared->quit || observed != shared->serial.load(); }))
                    {
                        if (shared->quit)
                            break;
                        observed = shared->serial.load();
                    }
                    if (shared->quit)
                        break;
                    if (!shared->queued)
                        continue;
                    roots = shared->roots;
                    query = shared->query;
                    serial = shared->serial;
                    rootsSerial = shared->rootsSerial;
                    cancelSerial = shared->cancelSerial;
                    refresh = shared->refresh;
                    shared->refresh = shared->queued = false;
                }
                auto output = std::make_shared<Result>();
                output->serial = serial;
                const auto began = juce::Time::getMillisecondCounterHiRes();
                auto canceledRoots = [&]
                {
                    return shared->quit || shared->rootsSerial != rootsSerial ||
                           shared->cancelSerial != cancelSerial;
                };
                try
                {
                    if (query.error.isNotEmpty())
                        output->error = query.error;
                    else if (query.favoritesOnly)
                    {
                        // Explicit user references only. Forgetting a library
                        // does not lose Starred files; no parent folder scan or
                        // channel/sample preparation is needed for this view.
                        {
                            std::lock_guard lock(shared->mutex);
                            if (shared->quit || shared->serial != serial)
                                continue;
                            shared->phase = Phase::searching;
                        }
                        Index favorites;
                        if (query.favoritePaths.size() > 512)
                            output->error = "Starred limit reached (512)";
                        else
                            for (const auto &path : query.favoritePaths)
                            {
                                if (shared->quit || shared->serial != serial)
                                    break;
                                if (!juce::File::isAbsolutePath(juce::String(path)))
                                    continue;
                                const juce::File file{juce::String(path)};
                                if (!file.hasFileExtension(audioExtensions))
                                    continue;
                                const bool exists = file.existsAsFile();
                                favorites.insert(
                                    {path, file.getFileName().toStdString(),
                                     file.getFileName().toLowerCase().toStdString(),
                                     file.getFileExtension().substring(1).toLowerCase().toStdString(),
                                     file.getParentDirectory().getFullPathName().toStdString(),
                                     exists ? file.getSize() : 0,
                                     exists ? file.getLastModificationTime().toMilliseconds() : 0, !exists});
                            }
                        output->stats.files = favorites.size();
                        if (!favorites.match(query, output->matches,
                                             [&] { return shared->quit || shared->serial != serial; }))
                            continue;
                    }
                    else if (roots.empty())
                        output->error = "Choose or drop a sample folder first";
                    else
                    {
                        if (indexedRoots != rootsSerial)
                        {
                            ++shared->preparations;
                            {
                                std::lock_guard lock(shared->mutex);
                                if (canceledRoots())
                                    continue;
                                shared->phase = Phase::indexing;
                            }
                            shared->files = 0;
                            Index candidate;
                            output->fromCache = !refresh && loadCache(candidate, cache, roots);
                            bool complete = output->fromCache;
                            if (!complete)
                            {
                                Scanner scanner(candidate, roots, [&](const IndexStats &stats)
                                                { shared->files = stats.files; });
                                complete = scanner.run(canceledRoots, output->stats);
                            }
                            else
                                output->stats.files = candidate.size();
                            if (canceledRoots())
                                continue;
                            if (!complete)
                                output->error = output->stats.error;
                            else
                            {
                                index = std::move(candidate);
                                indexedRoots = rootsSerial;
                                if (!output->fromCache && !saveCache(cache, roots, index.snapshot()))
                                    output->warning = "Index ready; cache could not be saved";
                            }
                        }
                        else
                            output->stats.files = index.size();
                        if (output->error.isEmpty())
                        {
                            // Query changes do not restart a folder scan. Once the
                            // index is ready, search the newest queued query.
                            {
                                std::lock_guard lock(shared->mutex);
                                if (canceledRoots())
                                    continue;
                                if (shared->query.favoritesOnly)
                                {
                                    shared->queued = true;
                                    continue;
                                }
                                query = shared->query;
                                serial = shared->serial;
                                output->serial = serial;
                                shared->queued = false;
                                shared->phase = Phase::searching;
                            }
                            if (query.error.isNotEmpty())
                                output->error = query.error;
                            else if (!index.match(query, output->matches,
                                                  [&] { return shared->quit || shared->serial != serial; }))
                                continue;
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    output->error = "Browser search failed: " + juce::String(e.what());
                }
                catch (...)
                {
                    output->error = "Browser search failed";
                }
                output->milliseconds = juce::Time::getMillisecondCounterHiRes() - began;
                for (const auto &entry : output->matches)
                    output->widestRow = std::max(
                        {output->widestRow, entry.name.size(),
                         std::size_t(
                             juce::File(juce::String(entry.parent)).getFileName().getNumBytesAsUTF8())});
                {
                    std::lock_guard lock(shared->mutex);
                    if (canceledRoots() || shared->serial != output->serial)
                        continue;
                    shared->completed = output;
                    retired.push_back(output); // Reclaim large result strings here, not on UI.
                    shared->phase = output->error.isEmpty() ? Phase::ready : Phase::error;
                }
            }
            {
                std::lock_guard lock(shared->mutex);
                shared->completed.reset();
            }
            // Consumers may retain a result briefly after service destruction.
            // Its last large-data reference is released by this owned worker.
            while (!retired.empty())
            {
                std::erase_if(retired, [](const auto &p) { return p.use_count() == 1; });
                if (!retired.empty())
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        shared->terminal->store(true);
    }

  public:
    explicit Service(juce::File cache = {})
    {
        try
        {
            worker = std::thread(
                [shared = state, cache]
                {
                    try
                    {
                        run(shared, cache);
                    }
                    catch (...)
                    {
                        shared->phase = Phase::error;
                        shared->terminal->store(true);
                    }
                });
        }
        catch (...)
        {
            state->phase = Phase::error;
            state->terminal->store(true);
        }
    }
    ~Service()
    {
        state->quit = true;
        ++state->serial;
        ++state->rootsSerial;
        state->wake.notify_all();
        if (worker.joinable())
            worker.detach(); // Worker owns all data; never touches a component.
    }
    std::uint64_t request(std::vector<std::string> roots, Query query, bool refresh = false)
    {
        const auto valid = validateTags(query.tagFilter.selected);
        if (valid.failed())
            query.error = valid.getErrorMessage();
        std::lock_guard lock(state->mutex);
        if (refresh || roots != state->roots)
            ++state->rootsSerial;
        state->roots = std::move(roots);
        state->query = std::move(query);
        state->refresh = state->refresh || refresh;
        state->queued = true;
        const auto serial = ++state->serial;
        state->wake.notify_one();
        return serial;
    }
    void cancel()
    {
        std::lock_guard lock(state->mutex);
        ++state->serial;
        // Cancel this job without invalidating an already completed root index.
        ++state->cancelSerial;
        state->queued = false;
        state->phase = Phase::canceled;
        state->wake.notify_one();
    }
    std::shared_ptr<const Result> takeResult()
    {
        std::unique_lock lock(state->mutex, std::try_to_lock);
        if (!lock || !state->completed)
            return {};
        auto result = std::move(state->completed);
        return result->serial == state->serial ? result : std::shared_ptr<const Result>{};
    }
    Phase phase() const
    {
        return state->phase.load();
    }
    std::uint64_t indexedFiles() const
    {
        return state->files.load();
    }
    std::uint64_t indexPreparations() const
    {
        return state->preparations.load();
    }
    std::shared_ptr<std::atomic<bool>> terminalFlag() const
    {
        return state->terminal;
    }
};
} // namespace vibera::browser_search
