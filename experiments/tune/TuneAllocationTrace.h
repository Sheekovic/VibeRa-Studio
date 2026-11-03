// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

namespace vibera::tune::allocationTrace
{
// Diagnostic mode only: fixed trace records during counted processing, followed
// by offline symbol resolution. Stack capture affects timing and is not RT proof.
struct Record
{
    size_t bytes = 0;
    const char *operation = "C++ new";
    int rate = 0, kind = 0, scenario = 0, offset = 0;
    unsigned short depth = 0;
    std::array<void *, 24> frames{};
};
inline std::array<Record, 128> records{};
inline size_t used = 0, omitted = 0;
inline bool enabled = false, capturing = false;
inline int rate = 0, kind = 0, scenario = 0, offset = 0;

inline void configure()
{
#if defined(_WIN32)
    const auto *value = std::getenv("VIBERA_TUNE_ALLOCATION_TRACE");
    enabled = value && value[0] == '1' && value[1] == '\0';
    if (enabled)
    {
        void *warmup[2]{};
        CaptureStackBackTrace(0, 2, warmup, nullptr);
    }
#endif
}
inline void context(int nextRate, int nextKind, int nextScenario, int nextOffset)
{
    rate = nextRate;
    kind = nextKind;
    scenario = nextScenario;
    offset = nextOffset;
}
inline void record(size_t bytes, const char *operation = "C++ new") noexcept
{
#if defined(_WIN32)
    if (!enabled || capturing)
        return;
    if (used == records.size())
    {
        ++omitted;
        return;
    }
    capturing = true;
    auto &entry = records[used++];
    entry.bytes = bytes;
    entry.operation = operation;
    entry.rate = rate;
    entry.kind = kind;
    entry.scenario = scenario;
    entry.offset = offset;
    entry.depth = CaptureStackBackTrace(1, DWORD(entry.frames.size()), entry.frames.data(), nullptr);
    capturing = false;
#else
    (void)bytes;
    (void)operation;
#endif
}
inline void write(const std::filesystem::path &folder)
{
    if (!enabled)
        return;
    std::ofstream out(folder / "allocation-stacks.txt", std::ios::trunc);
    out << "Diagnostic resource stacks; capture affects timing. Only explicitly instrumented entrypoints.\n";
#if defined(_WIN32)
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
    const auto symbols = std::filesystem::path(module).parent_path().wstring() + L";C:\\Windows\\System32";
    const auto process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS |
                  SYMOPT_IGNORE_NT_SYMPATH);
    const bool ready = SymInitializeW(process, symbols.c_str(), TRUE) != FALSE;
    out << "Local symbols=" << ready << " records=" << used << " capacity=" << records.size()
        << " omitted=" << omitted << '\n';
    for (size_t n = 0; n < used; ++n)
    {
        const auto &entry = records[n];
        out << "\nAllocation " << n << " operation=" << entry.operation << " bytes=" << entry.bytes
            << " rate=" << entry.rate << " kind=" << entry.kind << " scenario=" << entry.scenario
            << " offset=" << entry.offset << '\n';
        for (unsigned short i = 0; i < entry.depth; ++i)
        {
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 4096]{};
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 4095;
            const auto address = reinterpret_cast<DWORD64>(entry.frames[i]);
            DWORD64 displacement = 0;
            out << i << " pc=" << entry.frames[i];
            if (ready && SymFromAddr(process, address, &displacement, symbol))
                out << ' ' << symbol->Name;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (ready && SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                out << " source=" << line.FileName << ':' << line.LineNumber;
            out << '\n';
        }
    }
    if (ready)
        SymCleanup(process);
#endif
}
} // namespace vibera::tune::allocationTrace
