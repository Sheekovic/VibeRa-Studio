// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AiHttp.h"
#include "AiSettings.h"
#include "WorkerProcess.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// Local AI music generation: ACE-Step 1.5 (REST API server) and YuE2 (CLI).
// Engines install automatically on first use into managedAiRoot(): downloads,
// extraction and package installs run as owned child processes on a job thread.
// Nothing here runs on the message thread or touches DSP.
namespace vibera::ai
{
struct MusicRequest
{
    juce::String engine = "ace-step"; // "ace-step" or "yue2"
    juce::String prompt;              // Style / genre / instruments / mood tags.
    juce::String lyrics;              // Section tags such as [Verse] / [Chorus]; empty for instrumental.
    juce::String language = "en";
    double seconds = 60;              // ACE-Step: 10..600. YuE2 is token-capped (approximate).
    double bpm = 0;                   // 0: let the model choose.
    juce::String key;                 // e.g. "C major"; empty: model chooses.
    int seed = -1;                    // -1: random.
    bool instrumental = false;
};

juce::Result validateMusicRequest(const MusicRequest &);
juce::var musicRequestJson(const MusicRequest &);
MusicRequest musicRequestFromJson(const juce::var &);
// Engine payloads (pure; unit-tested).
juce::var aceStepTaskBody(const MusicRequest &);
juce::var yueRequestBody(const MusicRequest &);
// Finds every "/v1/audio?path=..." reference in an ACE-Step query_result payload.
juce::StringArray aceStepAudioReferences(const juce::var &queryResult);
// Status of a task in a query_result payload: 0 running, 1 succeeded, 2 failed, -1 unknown.
int aceStepTaskStatus(const juce::var &queryResult, const juce::String &taskId);

struct GpuInfo
{
    bool nvidia = false;
    juce::String name;
    int vramMb = 0;
};
GpuInfo detectGpu();
const GpuInfo &cachedGpu(); // Detected once per process (nvidia-smi is slow).

// ACE-Step 1.5 model choice. Recommendation follows the official VRAM tier table
// (docs/en/GPU_COMPATIBILITY.md + INSTALL.md "Which Model Should I Choose?").
struct AceStepModels
{
    juce::String dit, lm; // lm "none" disables the planning/lyrics language model.
    bool offloadToCpu = false, offloadDitToCpu = false;
    juce::String reason;
};
AceStepModels recommendAceStep(int vramMb);
AceStepModels effectiveAceStep(const MusicEngineConfig &, int vramMb); // User choice over recommendation.
const juce::StringArray &aceStepDitModels();
const juce::StringArray &aceStepLmModels(); // Includes "none".

enum class JobState
{
    idle,
    running,
    succeeded,
    failed,
    cancelled
};
struct JobSnapshot
{
    JobState state = JobState::idle;
    double progress = 0; // 0..1, -1 when indeterminate.
    juce::String stage, message;
    juce::File audio; // Succeeded: generated audio file.
    juce::String engine;
};

class MusicEngines
{
  public:
    MusicEngines() = default;
    ~MusicEngines(); // Cancels the job and stops the owned ACE-Step server.
    MusicEngines(const MusicEngines &) = delete;
    MusicEngines &operator=(const MusicEngines &) = delete;

    // Starts one generation (installing/starting the engine first when needed).
    juce::Result start(const MusicRequest &, const MusicEngineConfig &, const juce::File &outputFolder);
    // Installs and checks an engine without generating ("Install" / "Test" in Settings).
    juce::Result prepare(const juce::String &engine, const MusicEngineConfig &);
    void cancel() noexcept;
    JobSnapshot snapshot() const;
    bool busy() const;
    void stopServer(); // Owned ACE-Step server only.
    bool serverRunning() const;

    static juce::File aceStepRoot(const MusicEngineConfig &);
    static juce::File yueRoot(const MusicEngineConfig &);
    static bool aceStepInstalled(const MusicEngineConfig &);
    static bool yueInstalled(const MusicEngineConfig &);

  private:
    mutable std::mutex mutex;
    JobSnapshot state;
    std::atomic<bool> cancelled{false};
    std::thread worker;
    std::unique_ptr<WorkerProcess> server; // Owned job-object server (descendants contained).
    AceStepModels serverModels;          // Models the owned server was started with.

    void update(JobState, double progress, const juce::String &stage, const juce::String &message = {});
    void finish(juce::Result, const juce::File &audio = {});
    bool isCancelled() const noexcept
    {
        return cancelled.load();
    }
    juce::Result runProcess(const juce::StringArray &command, const juce::File &cwd, const juce::File &log,
                            int timeoutMs, const juce::String &stage);
    juce::Result ensureAceStep(const MusicEngineConfig &);
    juce::Result ensureAceStepServer(const MusicEngineConfig &);
    juce::Result ensureYue(const MusicEngineConfig &);
    juce::Result generateAceStep(const MusicRequest &, const MusicEngineConfig &, const juce::File &out,
                                 juce::File &audio);
    juce::Result generateYue(const MusicRequest &, const MusicEngineConfig &, const juce::File &out,
                             juce::File &audio);
    juce::Result checkHardware(const juce::String &engine, juce::int64 requiredFreeBytes, const juce::File &root);
    void joinWorker();
};
} // namespace vibera::ai
