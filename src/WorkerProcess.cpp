// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WorkerProcess.h"
#include "PluginPolicy.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vibera
{
struct WorkerProcess::Impl
{
    HANDLE process = nullptr, thread = nullptr, job = nullptr;
    DWORD pid = 0;
    bool owned() const
    {
        return process && pid != 0 && GetProcessId(process) == pid;
    }
    ~Impl()
    {
        if (thread)
            CloseHandle(thread);
        if (process)
            CloseHandle(process);
        if (job)
            CloseHandle(job); // only explicitly assigned worker; descendants break away
    }
};
WorkerProcess::WorkerProcess() : impl(std::make_unique<Impl>()) {}
WorkerProcess::~WorkerProcess()
{
    if (isRunning())
    {
        requestGracefulShutdown();
        if (!waitForProcessToFinish(2000))
            kill();
    }
}
static juce::String quoteArgument(const juce::String &value)
{
    // Windows CommandLineToArgvW backslash/quote rules; no shell is involved.
    juce::String result = "\"";
    int slashes = 0;
    for (auto character : value)
    {
        if (character == '\\')
        {
            ++slashes;
            continue;
        }
        if (character == '"')
        {
            result += juce::String::repeatedString("\\", 2 * slashes + 1);
            result += '"';
        }
        else
        {
            result += juce::String::repeatedString("\\", slashes);
            result += juce::String::charToString(character);
        }
        slashes = 0;
    }
    return result + juce::String::repeatedString("\\", 2 * slashes) + "\"";
}
bool WorkerProcess::start(const juce::StringArray &args, int, WorkerLimits resources)
{
    if (args.isEmpty() || impl->process)
        return false;
    juce::String command;
    for (auto &arg : args)
    {
        if (command.isNotEmpty())
            command += ' ';
        command += quoteArgument(arg);
    }
    auto text = std::wstring(command.toWideCharPointer());
    impl->job = CreateJobObjectW(nullptr, nullptr);
    if (!impl->job)
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        resources.containDescendants ? 0 : JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (resources.memoryMegabytes != 0)
    {
        if (resources.memoryMegabytes < 64 || resources.memoryMegabytes > 8192)
            return false;
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = resources.memoryMegabytes * size_t(1024 * 1024);
    }
    if (allowForcedWorkerTermination)
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(impl->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return false;
    if (resources.cpuPercent != 0)
    {
        if (resources.cpuPercent > 100)
            return false;
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpu.CpuRate = resources.cpuPercent * 100;
        if (!SetInformationJobObject(impl->job, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu)))
            return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const auto priority = resources.belowNormalPriority ? BELOW_NORMAL_PRIORITY_CLASS : 0;
    if (!CreateProcessW(nullptr, text.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | priority, nullptr, nullptr, &startup, &process))
        return false;
    impl->process = process.hProcess;
    impl->thread = process.hThread;
    impl->pid = process.dwProcessId;
    if (!AssignProcessToJobObject(impl->job, impl->process))
    {
        kill();
        return false;
    }
    return ResumeThread(impl->thread) != DWORD(-1);
}
bool WorkerProcess::isRunning() const
{
    return impl->owned() && WaitForSingleObject(impl->process, 0) == WAIT_TIMEOUT;
}
bool WorkerProcess::waitForProcessToFinish(int ms) const
{
    return !impl->process || WaitForSingleObject(impl->process, DWORD(juce::jmax(0, ms))) == WAIT_OBJECT_0;
}
juce::uint32 WorkerProcess::getExitCode() const
{
    DWORD code = 0;
    if (impl->owned())
        GetExitCodeProcess(impl->process, &code);
    return code;
}
juce::uint32 WorkerProcess::processId() const
{
    return impl->owned() ? impl->pid : 0;
}
void WorkerProcess::requestGracefulShutdown()
{
    if (!isRunning() || !impl->thread)
        return;
    DWORD code = 0;
    if (GetExitCodeThread(impl->thread, &code) && code == STILL_ACTIVE)
        PostThreadMessageW(GetThreadId(impl->thread), WM_QUIT, 0, 0);
}
bool WorkerProcess::kill()
{
    return allowForcedWorkerTermination && impl->owned() && TerminateProcess(impl->process, 3) != FALSE;
}
} // namespace vibera
#else
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace vibera
{
struct WorkerProcess::Impl
{
    mutable std::mutex mutex;
    pid_t pid = 0;
    mutable std::atomic<bool> owned{false}, exited{false};
    mutable std::atomic<juce::uint32> exitCode{259}; // Same running sentinel as the Windows branch.

    // Keep an exited child waitable until destruction. Its PID cannot be reused
    // between checking identity and signalling it, even after a crash.
    bool refresh() const
    {
        if (!owned || pid <= 0)
            return false;
        siginfo_t status{};
        int result;
        do
            result = waitid(P_PID, id_t(pid), &status, WEXITED | WNOHANG | WNOWAIT);
        while (result < 0 && errno == EINTR);
        if (result < 0)
        {
            // Another reaper or an unexpected OS failure revokes permission to
            // signal this numeric PID; never fall back to executable-name lookup.
            owned = false;
            return false;
        }
        if (status.si_pid == pid)
        {
            exited = true;
            exitCode = status.si_code == CLD_EXITED ? juce::uint32(status.si_status)
                                                  : juce::uint32(128 + status.si_status);
        }
        return !exited;
    }
};

WorkerProcess::WorkerProcess() : impl(std::make_unique<Impl>()) {}
WorkerProcess::~WorkerProcess()
{
    if (isRunning())
    {
        requestGracefulShutdown();
        if (!waitForProcessToFinish(2000) && kill())
            waitForProcessToFinish(2000);
    }
    const std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->owned || impl->pid <= 0)
        return;
    const auto pid = impl->pid;
    if (impl->exited)
    {
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    else
    {
        // A child stuck in kernel I/O must not make CONTROL shutdown unbounded.
        // Transfer only reaping (never signalling) to this retained child owner.
        std::thread([pid]
        {
            int status;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
    }
}

bool WorkerProcess::start(const juce::StringArray &args, int, WorkerLimits resources)
{
    const std::lock_guard<std::mutex> lock(impl->mutex);
    if (args.isEmpty() || impl->pid != 0)
        return false;
    // posix_spawn cannot enforce Windows Job memory/CPU/descendant restrictions.
    // Fail closed for constrained providers instead of silently weakening them.
    if (resources.memoryMegabytes != 0 || resources.cpuPercent != 0 ||
        resources.belowNormalPriority || resources.containDescendants)
        return false;
    std::vector<std::string> storage;
    storage.reserve(size_t(args.size()));
    for (const auto &arg : args)
    {
        auto bytes = arg.toUTF8();
        storage.emplace_back(bytes.getAddress(), size_t(bytes.sizeInBytes() - 1));
    }
    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (auto &arg : storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
        return false;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
    {
        posix_spawnattr_destroy(&attributes);
        return false;
    }
    sigset_t mask, defaults;
    sigemptyset(&mask);
    sigemptyset(&defaults);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD})
        sigaddset(&defaults, signal);
    // A separate group prevents a worker inheriting the application's terminal
    // group. It is not represented as an inescapable descendant sandbox.
    int error = posix_spawnattr_setflags(&attributes,
        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (error == 0) error = posix_spawnattr_setpgroup(&attributes, 0);
    if (error == 0) error = posix_spawnattr_setsigmask(&attributes, &mask);
    if (error == 0) error = posix_spawnattr_setsigdefault(&attributes, &defaults);
    // Isolated plugins must never consume host MCP input or write into its JSON
    // stdout. Scanner/provider outcomes are communicated through owned files/IPC.
    if (error == 0) error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (error == 0) error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (error == 0) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    if (error == 0)
        error = posix_spawnp(&pid, argv.front(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error != 0 || pid <= 0)
        return false;
    impl->pid = pid;
    impl->owned = true;
    return true;
}

bool WorkerProcess::isRunning() const
{
    // waitid(WNOHANG | WNOWAIT) never waits or reaps. Liveness readers do not
    // acquire the CONTROL-only signalling mutex, including live DSP readers.
    return impl->refresh();
}
bool WorkerProcess::waitForProcessToFinish(int milliseconds) const
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + juce::jmax(0, milliseconds);
    for (;;)
    {
        if (!isRunning())
            return impl->pid == 0 || (impl->owned && impl->exited);
        if (juce::Time::getMillisecondCounterHiRes() >= deadline)
            return false;
        juce::Thread::sleep(1);
    }
}
juce::uint32 WorkerProcess::getExitCode() const
{
    const std::lock_guard<std::mutex> lock(impl->mutex);
    impl->refresh();
    return impl->owned ? impl->exitCode.load() : (impl->pid == 0 ? 0u : 255u);
}
juce::uint32 WorkerProcess::processId() const
{
    impl->refresh();
    return impl->owned ? juce::uint32(impl->pid) : 0;
}
void WorkerProcess::requestGracefulShutdown()
{
    const std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->refresh())
        ::kill(impl->pid, SIGTERM);
}
bool WorkerProcess::kill()
{
    const std::lock_guard<std::mutex> lock(impl->mutex);
    return allowForcedWorkerTermination && impl->refresh() && ::kill(impl->pid, SIGKILL) == 0;
}
} // namespace vibera
#endif
