// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WorkerProcess.h"
#include "PluginPolicy.h"
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
