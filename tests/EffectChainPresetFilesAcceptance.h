// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/EffectChainPresetFiles.h"
#include "EffectChainAcceptance.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vibera::acceptance::effectChainFiles
{
// Actual disposable file/thread acceptance; original missing-plugin DATA below
// is the existing codec fixture, not a fabricated live provider or audio proof.
inline juce::Result verify(const juce::File &output)
{
    using namespace effect_chain;
    using Jobs = PresetFileJobs;
    juce::String report;
    const auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    // File::loadFileAsData appends. Independent comparisons need fresh bytes;
    // failure preserves the previous caller-owned observation.
    const auto loadReplacing = [](const juce::File &file, juce::MemoryBlock &destination)
    {
        juce::MemoryBlock fresh;
        if (!file.loadFileAsData(fresh)) return false;
        destination.swapWith(fresh);
        return true;
    };
    const auto retain = [&]
    {
        check(output.getChildFile("file-jobs.txt").replaceWithText(report),
              "Retain original file-job outcomes");
    };
    try
    {
        check(!output.exists() && output.createDirectory().wasOk(), "Fresh owned disposable file directory");
        check(effectChains::verify().wasOk(),
              "Existing full actual EffectChain serialization/capture data units");
        Jobs jobs(output.getFullPathName());
        const auto wait = [&](const juce::String &id)
        {
            const auto started = juce::Time::getMillisecondCounter();
            auto observed = jobs.read(id);
            while (observed && !Jobs::terminal(observed->phase) &&
                   juce::Time::getMillisecondCounter() - started < 10000)
            {
                juce::Thread::sleep(1);
                observed = jobs.read(id);
            }
            if (observed)
                report += id + " path=" + observed->path + " phase=" + juce::String(int(observed->phase)) +
                          " code=" + Jobs::errorCode(observed->error) +
                          " native=" + juce::String(observed->nativeError) +
                          " committed=" + juce::String(int(observed->fileCommitted)) +
                          " temporary=" + observed->temporaryPath +
                          " remaining=" + juce::String(int(observed->temporaryRemaining)) +
                          " cleanup=" + juce::String(observed->cleanupError) + "\n";
            retain();
            check(observed && Jobs::terminal(observed->phase),
                  "Actual async job reached terminal within original bounded wait");
            return *observed;
        };
        const auto forget = [&](const juce::String &id)
        {
            check(jobs.forget(id).wasOk() && !jobs.read(id) &&
                      jobs.cancel(id) == Jobs::Cancellation::unknownOperation &&
                      jobs.forget(id).getErrorMessage() == "chain.file.unknownOperation",
                  "Explicit terminal acknowledgement expires only that actual operation");
        };
        Chain original{"A", false, {}};
        const auto preset = output.getChildFile("original.vibfx");
        juce::String operation = "existing caller token";
        check(jobs.submitSave(preset.getFullPathName(), original, false, operation).wasOk() &&
                  operation != "existing caller token",
              "Actual new async save accepted with operation identity");
        original.name = "Changed caller after admission";
        const auto saved = wait(operation);
        constexpr unsigned char golden[]{'V', 'i', 'b', 'e', 'R', 'a', 'F', 'X', 1, 0, 0, 0, 0,
                                         0,   0,   0,   1,   0,   0,   0,   'A', 0, 0, 0, 0};
        juce::MemoryBlock actual;
        check(loadReplacing(preset, actual), "Read actual saved portable bytes");
        check(saved.phase == Jobs::Phase::completed && saved.fileCommitted && !saved.loaded &&
                  !saved.temporaryRemaining && actual.getSize() == sizeof(golden) &&
                  std::memcmp(actual.getData(), golden, sizeof(golden)) == 0,
              "Async file commit equals independent golden bytes, unaffected by caller mutation");
        check(jobs.cancel(operation) == Jobs::Cancellation::terminal,
              "Completed save cannot pretend cancellable");
        const auto alternate = output.getChildFile("explicitly-selected-directory");
        check(alternate.createDirectory().wasOk() &&
                  jobs.configureDirectory(alternate.getFullPathName()).wasOk() &&
                  jobs.configuredDirectory() == alternate.getFullPathName() &&
                  jobs.read(operation)->path == preset.getFullPathName(),
              "Idle reconfiguration reuses one service and preserves unread terminal receipt identity/path");
        juce::String otherDirectoryOperation = "unchanged";
        check(jobs.submitLoad(preset.getFullPathName(), otherDirectoryOperation).failed() &&
                  otherDirectoryOperation == "unchanged",
              "Old directory is outside newly selected file scope");
        check(jobs.submitSave(alternate.getChildFile("selected.vibfx").getFullPathName(),
                              Chain{"A", false, {}}, false, otherDirectoryOperation)
                  .wasOk(),
              "Same worker accepts explicitly reconfigured directory");
        const auto otherDirectorySaved = wait(otherDirectoryOperation);
        check(otherDirectorySaved.phase == Jobs::Phase::completed && otherDirectorySaved.fileCommitted,
              "Reconfigured actual destination committed");
        forget(otherDirectoryOperation);
        check(jobs.configureDirectory(output.getFullPathName()).wasOk(),
              "Restore explicit test directory while idle");
        forget(operation);
        check(jobs.submitLoad(preset.getFullPathName(), operation).wasOk(),
              "Actual async preset read accepted");
        auto loaded = wait(operation);
        check(loaded.phase == Jobs::Phase::completed && !loaded.fileCommitted && loaded.loaded &&
                  *loaded.loaded == Chain{"A", false, {}},
              "Load completes portable data only, no graph/history claim");
        const auto retainedData = loaded.loaded;
        forget(operation);
        check(retainedData && retainedData->name == "A",
              "Caller owns immutable loaded data beyond acknowledged receipt");

        juce::PluginDescription descriptor;
        descriptor.name = "Original chain fixture";
        descriptor.pluginFormatName = "VST3";
        descriptor.fileOrIdentifier = "vibera-original-chain-fixture-NOT-INSTALLED.vst3";
        descriptor.numInputChannels = descriptor.numOutputChannels = 2;
        constexpr std::array<unsigned char, 4> opaque{0, 0x7f, 0x80, 0xff};
        Chain rich{"Original reusable " + juce::String::fromUTF8("\xc3\xa9"),
                   true,
                   {{"Original A", descriptor.createXml()->toString(),
                     juce::MemoryBlock(opaque.data(), opaque.size()).toBase64Encoding(), true, .375f}}};
        const auto preserved = actual;
        operation = "preserved token";
        check(jobs.submitSave(preset.getFullPathName(), rich, false, operation).getErrorMessage() ==
                      "chain.file.outputExists" &&
                  operation == "preserved token",
              "No implicit overwrite or accepted phantom job");
        check(loadReplacing(preset, actual) && actual == preserved,
              "Refused overwrite preserves owning destination bytes");
        check(jobs.submitSave(preset.getFullPathName(), rich, true, operation).wasOk(),
              "Explicit replacement admitted");
        const auto replaced = wait(operation);
        check(replaced.phase == Jobs::Phase::completed && replaced.fileCommitted &&
                  !replaced.temporaryRemaining,
              "Explicit actual atomic replacement completed");
        forget(operation);
        check(jobs.submitLoad(preset.getFullPathName(), operation).wasOk(),
              "Load original opaque reusable chain");
        loaded = wait(operation);
        check(loaded.loaded && *loaded.loaded == rich && !loaded.fileCommitted,
              "Exact ordering/bypass/wet/name/descriptor/opaque state roundtrip via existing codec");
        forget(operation);
        const auto richData = loaded.loaded;

        // Actual sharing violation at rename, not a production failpoint.
        struct OwnedReadLock
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            ~OwnedReadLock()
            {
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }
        } locked;
        locked.handle = CreateFileW(preset.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked.handle != INVALID_HANDLE_VALUE && loadReplacing(preset, actual),
              "Owned existing target locked against rename");
        const auto beforeFailure = actual;
        check(jobs.submitSave(preset.getFullPathName(), Chain{"other", false, {}}, true, operation).wasOk(),
              "Actual failed-commit candidate was accepted, not preflight-only test");
        const auto failedCommit = wait(operation);
        check(failedCommit.phase == Jobs::Phase::failed && failedCommit.error == Jobs::Error::commitFailed &&
                  failedCommit.nativeError != 0 && !failedCommit.fileCommitted &&
                  !failedCommit.temporaryRemaining && loadReplacing(preset, actual) &&
                  actual == beforeFailure && richData && *richData == rich,
              "Real rename failure preserves previous file/data and cleans task-owned temp");
        forget(operation);
        check(CloseHandle(locked.handle) != 0, "Release only owned test lock");
        locked.handle = INVALID_HANDLE_VALUE;

        const auto bad = output.getChildFile("bad.vibfx");
        check(bad.replaceWithText("not a portable chain"), "Original malformed owned input");
        check(jobs.submitLoad(bad.getFullPathName(), operation).wasOk(),
              "Malformed read queued without false decode success");
        const auto decoded = wait(operation);
        check(decoded.phase == Jobs::Phase::failed && decoded.error == Jobs::Error::decodeFailed &&
                  !decoded.loaded && !decoded.fileCommitted && *richData == rich,
              "Decode failure preserves prior caller data");
        forget(operation);
        const auto large = output.getChildFile("too-large.vibfx");
        {
            auto stream = large.createOutputStream();
            check(stream && stream->setPosition(juce::int64(maximumBytes)) && stream->writeByte(0),
                  "Owned oversized logical file");
            stream->flush();
            check(stream->getStatus().wasOk(), "Oversize fixture flushed");
        }
        check(jobs.submitLoad(large.getFullPathName(), operation).wasOk(),
              "Oversized read has actual owned async outcome");
        const auto oversized = wait(operation);
        check(oversized.phase == Jobs::Phase::failed && oversized.error == Jobs::Error::tooLarge &&
                  !oversized.loaded,
              "Actual input bound refuses before unbounded allocation");
        forget(operation);
        check(jobs.submitLoad(output.getChildFile("missing.vibfx").getFullPathName(), operation).wasOk(),
              "Missing read queued");
        const auto missing = wait(operation);
        check(missing.phase == Jobs::Phase::failed && missing.error == Jobs::Error::readFailed &&
                  !missing.loaded,
              "Missing input has truthful terminal failure");
        forget(operation);

        for (const auto &path :
             {juce::String("relative.vibfx"), juce::String("\\\\server\\file.vibfx"),
              preset.getFullPathName() + ":stream", output.getChildFile("wrong.vibra").getFullPathName(),
              output.getChildFile("not-created/child.vibfx").getFullPathName(),
              output.getChildFile("CON.vibfx").getFullPathName()})
        {
            operation = "unmodified";
            check(jobs.submitSave(path, rich, true, operation).failed() && operation == "unmodified" &&
                      jobs.submitLoad(path, operation).failed() && operation == "unmodified",
                  "Canonical path policy shared by read/save");
        }
        check(!output.getChildFile("not-created").exists(), "No helper directory creation");
        const auto directoryTarget = output.getChildFile("directory.vibfx");
        check(directoryTarget.createDirectory().wasOk(), "Owned actual directory target");
        operation = "unmodified directory token";
        check(jobs.submitSave(directoryTarget.getFullPathName(), rich, true, operation).getErrorMessage() ==
                      "chain.file.unsupportedPath" &&
                  operation == "unmodified directory token" && directoryTarget.isDirectory(),
              "No replacement of an actual directory");
        auto invalid = rich;
        invalid.name.clear();
        check(
            jobs.submitSave(output.getChildFile("invalid.vibfx").getFullPathName(), invalid, false, operation)
                    .getErrorMessage()
                    .startsWith("chain.file.encodeFailed:") &&
                operation == "unmodified directory token" && !output.getChildFile("invalid.vibfx").exists(),
            "Existing codec validation refuses before accepted job or file");
        check(!jobs.read("foreign") && jobs.cancel("foreign") == Jobs::Cancellation::unknownOperation,
              "Foreign/stale receipt identity refused");

        check(Jobs::maximumRetainedBytes == 128u * 1024u * 1024u,
              "Independent serialized reservation bound128MiB");
        std::array<juce::String, 2> reserved;
        for (auto &id : reserved)
            check(jobs.submitLoad(preset.getFullPathName(), id).wasOk(),
                  "Two conservative64MiB load reservations");
        operation = "budget token";
        check(
            jobs.submitSave(output.getChildFile("budget.vibfx").getFullPathName(), Chain{"A", false, {}},
                            false, operation)
                        .getErrorMessage() == "chain.file.capacity" &&
                operation == "budget token",
            "Retained payload reservations, including unread terminal records, cannot silently exceed128MiB");
        for (const auto &id : reserved)
        {
            const auto data = wait(id);
            check(data.phase == Jobs::Phase::completed && data.loaded && *data.loaded == rich,
                  "Reserved actual loads complete independently");
            forget(id);
        }

        // Deterministic tests of the EXACT CAS functions used by real jobs.
        // These are synchronization units, not fake filesystem/runtime evidence.
        for (auto initial : {Jobs::Phase::queued, Jobs::Phase::running})
        {
            std::atomic<Jobs::Phase> state{initial};
            check(Jobs::requestCancellation(state) == Jobs::Cancellation::requested &&
                      state.load() == Jobs::Phase::cancelling && !Jobs::beginCommit(state),
                  "Cancellation wins precommit CAS");
        }
        std::atomic<Jobs::Phase> committing{Jobs::Phase::running};
        check(Jobs::beginCommit(committing) &&
                  Jobs::requestCancellation(committing) == Jobs::Cancellation::tooLate,
              "Commit wins exact arbitration, no pretend rollback");

        std::vector<juce::String> batch;
        check(Jobs::maximumRecords == 32, "Independent approved receipt bound32");
        for (std::size_t n = 0; n < 32; ++n)
        {
            check(jobs.submitSave(
                          output.getChildFile("cancel-" + juce::String(int(n)) + ".vibfx").getFullPathName(),
                          Chain{"A", false, {}}, false, operation)
                      .wasOk(),
                  "Bounded real file batch admitted");
            batch.push_back(operation);
        }
        operation = "capacity token";
        check(jobs.submitSave(output.getChildFile("overflow.vibfx").getFullPathName(), rich, false, operation)
                          .getErrorMessage() == "chain.file.capacity" &&
                  operation == "capacity token",
              "32 receipts retained without silent terminal eviction");
        std::vector<Jobs::Cancellation> cancellationRequests;
        for (const auto &id : batch)
            cancellationRequests.push_back(jobs.cancel(id));
        unsigned actualCancelled = 0;
        for (std::size_t n = 0; n < batch.size(); ++n)
        {
            const auto cancellation = cancellationRequests[n];
            const auto outcome = wait(batch[n]);
            const auto file = output.getChildFile("cancel-" + juce::String(int(n)) + ".vibfx");
            if (cancellation == Jobs::Cancellation::requested)
            {
                check(outcome.phase == Jobs::Phase::cancelled && !outcome.fileCommitted && !file.exists() &&
                          !outcome.temporaryRemaining,
                      "Actual accepted cancellation prevents commit and drains temp");
                ++actualCancelled;
            }
            else
                check((cancellation == Jobs::Cancellation::tooLate ||
                       cancellation == Jobs::Cancellation::terminal) &&
                          outcome.phase == Jobs::Phase::completed && outcome.fileCommitted &&
                          file.existsAsFile(),
                      "Actual won commit remains completed, not cancelled");
            forget(batch[n]);
        }
        report += "actualCancelled=" + juce::String(actualCancelled) + " boundedAttempts=32\n";
        retain();
        check(actualCancelled > 0,
              "Real precommit cancellation must be observed; no retry or fabricated success");
        check(jobs.idle(), "All actual accepted jobs terminal and acknowledged");
        jobs.requestStop();
        operation = "stopped token";
        check(jobs.submitLoad(preset.getFullPathName(), operation).getErrorMessage() ==
                      "chain.file.stopping" &&
                  operation == "stopped token",
              "Stopped owned service never accepts another job");

        // Teardown while owned work may still be queued/running. Every surviving
        // output must be complete codec bytes; no callback captures a UI owner.
        {
            Jobs draining(output.getFullPathName());
            for (int n = 0; n < 8; ++n)
                check(draining
                          .submitSave(
                              output.getChildFile("drain-" + juce::String(n) + ".vibfx").getFullPathName(),
                              Chain{"A", false, {}}, false, operation)
                          .wasOk(),
                      "Owned teardown work admitted");
        }
        for (int n = 0; n < 8; ++n)
        {
            const auto file = output.getChildFile("drain-" + juce::String(n) + ".vibfx");
            if (!file.exists())
                continue;
            std::unique_ptr<Chain> data;
            check(loadReplacing(file, actual) &&
                      decode({static_cast<const std::byte *>(actual.getData()), actual.getSize()}, data)
                          .wasOk() &&
                      *data == Chain{"A", false, {}},
                  "No partial output survives actual owned-worker shutdown");
        }
        check(output.findChildFiles(juce::File::findFiles, true, ".vibra-fx-*.tmp").isEmpty(),
              "All task-owned temporary files reclaimed after success/failure/cancel/drain");
        report += "PASS actual file service only; no native capture/Paste/UI/MCP/audio qualification\n";
        retain();
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        report += "FAIL " + juce::String(error.what()) + "\n";
        try
        {
            if (output.isDirectory())
                retain();
        }
        catch (...)
        {
        }
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::effectChainFiles
