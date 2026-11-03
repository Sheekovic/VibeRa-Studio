// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vibera::test
{
// Optional diagnostics for the synthetic driver only. The external supervisor
// creates this mapping before launching its exact owned target. No production
// driver uses this header. Mapping/resource work occurs at device construction.
struct alignas(64) CallbackBeacon
{
    static constexpr LONG signature = 0x56424331;
    LONG magic = signature;
    DWORD ownerPid = 0;
    volatile LONG64 counter = 0, active = 0, began = 0;
    volatile LONG thread = 0;
    LONG reserved[7]{}; // Explicit padding keeps the shared layout64 bytes.
};

class CallbackStallProbe
{
    HANDLE mapping = nullptr;
    CallbackBeacon *beacon = nullptr;

  public:
    CallbackStallProbe()
    {
        wchar_t name[256]{};
        const auto length = GetEnvironmentVariableW(L"VIBERA_CALLBACK_PROBE", name, 256);
        if (length == 0 || length >= 256)
            return;
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (mapping)
            beacon = static_cast<CallbackBeacon *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CallbackBeacon)));
        if (beacon &&
            (beacon->magic != CallbackBeacon::signature || beacon->ownerPid != GetCurrentProcessId()))
        {
            UnmapViewOfFile(beacon);
            beacon = nullptr;
        }
    }
    ~CallbackStallProbe()
    {
        if (beacon)
            UnmapViewOfFile(beacon);
        if (mapping)
            CloseHandle(mapping);
    }
    CallbackStallProbe(const CallbackStallProbe &) = delete;
    CallbackStallProbe &operator=(const CallbackStallProbe &) = delete;

    void enter() noexcept
    {
        if (!beacon)
            return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        InterlockedExchange64(&beacon->began, now.QuadPart);
        InterlockedExchange(&beacon->thread, LONG(GetCurrentThreadId()));
        const auto token = InterlockedIncrement64(&beacon->counter);
        InterlockedExchange64(&beacon->active, token);
    }
    void leave() noexcept
    {
        if (beacon)
            InterlockedExchange64(&beacon->active, 0);
    }
};
} // namespace vibera::test
#else
namespace vibera::test
{
struct CallbackStallProbe
{
    void enter() noexcept {}
    void leave() noexcept {}
};
} // namespace vibera::test
#endif
