// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CallbackStallProbe.h"
#include <dbghelp.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct Handle
{
    HANDLE value = nullptr;
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct Suspension
{
    HANDLE thread;
    bool suspended;
    explicit Suspension(HANDLE h) : thread(h), suspended(SuspendThread(h) != DWORD(-1)) {}
    ~Suspension()
    {
        if (suspended)
            ResumeThread(thread);
    }
};
void closeOwnedProcess(HANDLE process, DWORD pid)
{
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || GetProcessId(process) != pid)
        return;
    EnumWindows(
        [](HWND window, LPARAM ownerPid) -> BOOL
        {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == DWORD(ownerPid))
                PostMessageW(window, WM_CLOSE, 0, 0);
            return TRUE;
        },
        pid);
    if (WaitForSingleObject(process, 2000) == WAIT_TIMEOUT)
        TerminateProcess(process, 124); // Retained exact launched handle only.
    WaitForSingleObject(process, 5000);
}
struct OwnedCleanup
{
    HANDLE process;
    DWORD pid;
    ~OwnedCleanup()
    {
        closeOwnedProcess(process, pid);
    }
};
std::wstring quoted(const std::wstring &text)
{
    // Windows CommandLineToArgvW rules, including quotes and trailing slashes.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto c : text)
    {
        if (c == L'\\')
            ++slashes;
        else
        {
            result.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
            if (c == L'\"')
                result += L'\\';
            result += c;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

__declspec(noinline) void fixtureStall(vibera::test::CallbackStallProbe &probe, DWORD duration)
{
    probe.enter();
    Sleep(duration); // Intentional self-test wait; never a performance qualification.
    probe.leave();
}
void capture(HANDLE process, DWORD pid, DWORD tid, LONG64 token, vibera::test::CallbackBeacon &beacon,
             const std::wstring &symbols, bool &symbolReady, std::ofstream &log)
{
    Handle thread{
        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid)};
    if (!thread.value || GetProcessIdOfThread(thread.value) != pid)
        return;
    // Only a retained thread belonging to our retained launched process may be
    // suspended. The active token is revalidated after suspension, before readback.
    Suspension pause(thread.value);
    if (!pause.suspended || InterlockedCompareExchange64(&beacon.active, 0, 0) != token)
        return;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread.value, &context))
    {
        log << "GetThreadContext failed=" << GetLastError() << '\n';
        return;
    }
    if (!symbolReady)
    {
        symbolReady = SymInitializeW(process, symbols.c_str(), TRUE) != FALSE;
        log << "Local symbols initialized=" << symbolReady << " error=" << (symbolReady ? 0 : GetLastError())
            << '\n';
    }
    else
        SymRefreshModuleList(process);
    if (!symbolReady)
    {
        log << "Unresolved RIP=0x" << std::hex << context.Rip << std::dec << '\n';
        return;
    }
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    for (int index = 0; index < 48 && frame.AddrPC.Offset; ++index)
    {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 4096]{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 4095;
        DWORD64 displacement = 0;
        log << index << " pc=0x" << std::hex << frame.AddrPC.Offset << std::dec;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            log << ' ' << symbol->Name << " +0x" << std::hex << displacement << std::dec;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
            log << " source=" << line.FileName << ':' << line.LineNumber;
        IMAGEHLP_MODULE64 module{};
        module.SizeOfStruct = sizeof(module);
        if (SymGetModuleInfo64(process, frame.AddrPC.Offset, &module))
            log << " module=" << module.ModuleName << " symbolType=" << int(module.SymType);
        log << '\n';
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread.value, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
    }
}
} // namespace

int run(int argc, wchar_t **argv)
{
    if (argc == 2 && (std::wstring(argv[1]) == L"--fixture" || std::wstring(argv[1]) == L"--timeout-fixture"))
    {
        vibera::test::CallbackStallProbe probe;
        fixtureStall(probe, std::wstring(argv[1]) == L"--fixture" ? 80 : 5000);
        return 0;
    }
    if (argc < 5)
        return 2; // output-directory threshold-ms timeout-seconds target [args...]
    const double thresholdMs = std::wcstod(argv[2], nullptr);
    const auto timeoutSeconds = std::wcstoul(argv[3], nullptr, 10);
    if (!(thresholdMs >= 1 && thresholdMs <= 1000) || timeoutSeconds < 1 || timeoutSeconds > 600)
        return 2;
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    std::ofstream log(output / L"callback-stacks.txt", std::ios::trunc);
    std::ofstream polls(output / L"observer-waits.csv", std::ios::trunc);
    if (!log || !polls)
        return 3;
    polls << "poll,observedQpc,previousWaitBeganQpc,waitMs,activeToken,previousToken,totalCallbacks,"
             "previousCallbacks,captures\n";
    wchar_t pauseText[16]{};
    const auto pauseSize = GetEnvironmentVariableW(L"VIBERA_CALLBACK_OBSERVER_PAUSE_MS", pauseText, 16);
    DWORD observerPauseMs = 0;
    if (pauseSize)
    {
        wchar_t *end = nullptr;
        observerPauseMs = std::wcstoul(pauseText, &end, 10);
        wchar_t ownPath[32768]{};
        if (pauseSize >= 16 || !end || *end || observerPauseMs < 1 || observerPauseMs > 500 || argc != 6 ||
            std::wstring(argv[5]) != L"--fixture" ||
            !GetModuleFileNameW(nullptr, ownPath, DWORD(std::size(ownPath))) ||
            !std::filesystem::equivalent(ownPath, argv[4]))
            throw std::runtime_error(
                "Observer pause is permitted only for this helper's exact self-test target");
    }
    const std::wstring mappingName = L"Local\\VibeRaCallbackProbe-" + std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(GetTickCount64());
    Handle mapping{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(vibera::test::CallbackBeacon), mappingName.c_str())};
    if (!mapping.value || GetLastError() == ERROR_ALREADY_EXISTS)
        return 3;
    auto *beacon = static_cast<vibera::test::CallbackBeacon *>(
        MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(vibera::test::CallbackBeacon)));
    if (!beacon)
        return 3;
    new (beacon) vibera::test::CallbackBeacon{};
    std::wstring command;
    for (int index = 4; index < argc; ++index)
        command += quoted(argv[index]) + L" ";
    SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
    Handle out{CreateFileW((output / L"target.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherited,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (out.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE)
        return 3;
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = startup.hStdError = out.value;
    startup.hStdInput = input.value;
    const DWORD previousSize = GetEnvironmentVariableW(L"VIBERA_CALLBACK_PROBE", nullptr, 0);
    std::vector<wchar_t> previous(previousSize);
    if (previousSize)
        GetEnvironmentVariableW(L"VIBERA_CALLBACK_PROBE", previous.data(), previousSize);
    if (!SetEnvironmentVariableW(L"VIBERA_CALLBACK_PROBE", mappingName.c_str()))
    {
        UnmapViewOfFile(beacon);
        return 3;
    }
    PROCESS_INFORMATION child{};
    const bool launched =
        CreateProcessW(argv[4], command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                       nullptr, nullptr, &startup, &child) != FALSE;
    const auto launchError = launched ? 0 : GetLastError();
    SetEnvironmentVariableW(L"VIBERA_CALLBACK_PROBE", previousSize ? previous.data() : nullptr);
    if (!launched)
    {
        log << "CreateProcess failed=" << launchError << '\n';
        UnmapViewOfFile(beacon);
        return 3;
    }
    Handle process{child.hProcess}, mainThread{child.hThread};
    OwnedCleanup cleanup{process.value, child.dwProcessId};
    beacon->ownerPid = child.dwProcessId;
    FILETIME creation{}, exitTime{}, kernel{}, user{};
    GetProcessTimes(process.value, &creation, &exitTime, &kernel, &user);
    log << "Owned PID=" << child.dwProcessId << " creationFILETIME="
        << ((static_cast<unsigned long long>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime)
        << "; retained exact process handle; thresholdMs=" << thresholdMs << '\n'
        << "Diagnostic suspensions affect callback timing. No release performance claim.\n";
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
    const auto symbols = std::filesystem::path(argv[4]).parent_path().wstring() + L";C:\\Windows\\System32";
    // A CREATE_SUSPENDED target has not completed loader initialization. Initialize
    // DbgHelp lazily at the first actual callback, with the owned thread paused.
    bool symbolReady = false;
    if (ResumeThread(mainThread.value) == DWORD(-1))
    {
        log << "ResumeThread failed=" << GetLastError() << '\n';
        UnmapViewOfFile(beacon);
        return 3;
    }
    Handle timer{CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE)};
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const auto began = GetTickCount64();
    LONG64 lastCaptured = 0;
    int captures = 0;
    bool timedOut = false;
    LONG64 previousWait = 0, previousToken = 0, previousCounter = 0;
    unsigned long long pollCount = 0, longWaits = 0, omittedWaits = 0;
    double maximumWaitMs = 0;
    while (WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT)
    {
        const auto token = InterlockedCompareExchange64(&beacon->active, 0, 0);
        const auto tick = InterlockedCompareExchange64(&beacon->began, 0, 0);
        const auto tid = DWORD(InterlockedCompareExchange(&beacon->thread, 0, 0));
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        ++pollCount;
        const auto counter = InterlockedCompareExchange64(&beacon->counter, 0, 0);
        if (previousWait)
        {
            // Starts immediately before our requested1ms wait, excluding prior
            // symbolization/capture/logging work. This is observer availability,
            // not evidence of the target's lock or a system-wide scheduling cause.
            const double waitMs = double(now.QuadPart - previousWait) * 1000 / double(frequency.QuadPart);
            maximumWaitMs = (std::max)(maximumWaitMs, waitMs);
            if (waitMs > thresholdMs)
            {
                ++longWaits;
                if (longWaits <= 128)
                    polls << pollCount << ',' << now.QuadPart << ',' << previousWait << ',' << std::fixed
                          << std::setprecision(6) << waitMs << ',' << token << ',' << previousToken << ','
                          << counter << ',' << previousCounter << ',' << captures << '\n';
                else
                    ++omittedWaits;
            }
        }
        const double elapsed = double(now.QuadPart - tick) * 1000 / double(frequency.QuadPart);
        if (token && token != lastCaptured && captures < 8 && elapsed > thresholdMs &&
            InterlockedCompareExchange64(&beacon->active, 0, 0) == token)
        {
            lastCaptured = token;
            ++captures;
            log << "\nCandidate token=" << token << " thread=" << tid << " preCaptureElapsedMs=" << std::fixed
                << std::setprecision(6) << elapsed << '\n';
            const auto pauseBegan = GetTickCount64();
            capture(process.value, child.dwProcessId, tid, token, *beacon, symbols, symbolReady, log);
            log << "Capture operation wallMs=" << GetTickCount64() - pauseBegan
                << "; includes debugger-induced suspension, not original DSP time\n";
            log.flush();
        }
        if (GetTickCount64() - began > timeoutSeconds * 1000ULL)
        {
            timedOut = true;
            closeOwnedProcess(process.value, child.dwProcessId);
            break;
        }
        LARGE_INTEGER due;
        due.QuadPart = -10000;
        previousToken = InterlockedCompareExchange64(&beacon->active, 0, 0);
        previousCounter = InterlockedCompareExchange64(&beacon->counter, 0, 0);
        LARGE_INTEGER waitBegan;
        QueryPerformanceCounter(&waitBegan);
        previousWait = waitBegan.QuadPart;
        if (observerPauseMs)
        {
            log << "Intentional observer-only waitMs=" << observerPauseMs << '\n';
            Sleep(observerPauseMs);
            observerPauseMs = 0;
        }
        if (!timer.value || !SetWaitableTimer(timer.value, &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.value, 2000) != WAIT_OBJECT_0)
            Sleep(1); // Supervisor only. No target priority/buffer/deadline changes.
    }
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(process.value, &code);
    log << "Owned target exit=" << code << " timedOut=" << timedOut << " candidateCaptures=" << captures
        << " totalCallbacks=" << InterlockedCompareExchange64(&beacon->counter, 0, 0) << '\n';
    log << "Observer polls=" << pollCount << " longWaits=" << longWaits << " omittedWaits=" << omittedWaits
        << " maxObserverWaitMs=" << std::fixed << std::setprecision(6) << maximumWaitMs
        << " qpcFrequency=" << frequency.QuadPart
        << "; wait includes requested timer delay and observation overhead; no system-wide cause proven\n";
    if (symbolReady)
        SymCleanup(process.value);
    UnmapViewOfFile(beacon);
    return timedOut ? 124 : int(code);
}

int wmain(int argc, wchar_t **argv)
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception &error)
    {
        // Unwinding first runs the exact-owned child guard and thread resumption.
        std::fputs(error.what(), stderr);
        return 3;
    }
}
