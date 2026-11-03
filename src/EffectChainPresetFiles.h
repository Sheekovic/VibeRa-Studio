// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectChain.h"
#include <atomic>
#include <memory>
#include <optional>

namespace vibera::effect_chain
{
// Specialized CONTROL/background file service, never an audio-thread API.
// Input is an already captured portable Chain. No Host, Project, history or UI
// owner is retained; loading returns DATA, never a completed paste/projection.
class PresetFileJobs final
{
  public:
    enum class Phase : unsigned
    {
        queued,
        running,
        cancelling,
        committing,
        completed,
        failed,
        cancelled
    };
    enum class Error : unsigned
    {
        none,
        wrongThread,
        invalidPath,
        unavailableDirectory,
        unsupportedPath,
        outputExists,
        capacity,
        stopping,
        unknownOperation,
        activeOperation,
        encodeFailed,
        readFailed,
        tooLarge,
        decodeFailed,
        writeFailed,
        flushFailed,
        commitFailed,
        cleanupFailed,
        cancelled,
        exception
    };
    enum class Cancellation
    {
        requested,
        tooLate,
        terminal,
        unknownOperation
    };
    struct Observation
    {
        juce::String operation, path;
        bool saving = false, overwrite = false, fileCommitted = false;
        Phase phase = Phase::queued;
        Error error = Error::none;
        unsigned long nativeError = 0;
        juce::String detail;
        juce::String temporaryPath;
        bool temporaryRemaining = false;
        unsigned long cleanupError = 0;
        std::shared_ptr<const Chain> loaded;
    };
    static constexpr std::size_t maximumRecords = 32;
    static constexpr std::size_t maximumRetainedBytes = 128 * 1024 * 1024;

    // Existing explicitly selected/owned directory only. No directory creation.
    // All jobs are direct .vibfx children; root and ancestors must be ordinary
    // local-drive directories. No UNC/device/alternate-stream/reparse paths.
    explicit PresetFileJobs(const juce::String &absoluteDirectory);
    ~PresetFileJobs(); // CONTROL only: requests cancellation, drains and joins owned worker.
    PresetFileJobs(const PresetFileJobs &) = delete;
    PresetFileJobs &operator=(const PresetFileJobs &) = delete;

    // Acceptance means queued only. Failure preserves operation output. Encode
    // validates/copies on CONTROL, with path/attribute preflight. File BYTE I/O
    // and decoding are background; the worker also rechecks current paths.
    juce::Result submitSave(const juce::String &absolutePath, const Chain &, bool overwrite,
                            juce::String &operation);
    juce::Result submitLoad(const juce::String &absolutePath, juce::String &operation);
    std::optional<Observation> read(const juce::String &operation) const;
    Cancellation cancel(const juce::String &operation);
    juce::Result forget(const juce::String &operation); // Terminal only, explicit caller acknowledgement.
    juce::Result configureDirectory(const juce::String &absoluteDirectory); // All jobs must be terminal.
    juce::String configuredDirectory() const; // CONTROL snapshot; receipts keep their original paths.
    void requestStop();                       // Reject future submissions; no thread/process termination.
    bool idle() const;
    static const char *errorCode(Error) noexcept;
    static bool terminal(Phase) noexcept;

    // Exact shared arbitration used by worker and CONTROL. No I/O/test hook.
    // Cancellation accepted before committing prevents the rename/publication;
    // once committing, it reports tooLate even if I/O subsequently fails.
    static Cancellation requestCancellation(std::atomic<Phase> &) noexcept;
    static bool beginCommit(std::atomic<Phase> &) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace vibera::effect_chain
