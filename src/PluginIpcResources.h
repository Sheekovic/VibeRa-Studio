// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include "PluginProtocol.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vibera
{
// Prepared on CONTROL. Only signal() is used by live host processing: a
// nonblocking OS notification; shared packet atomics remain authoritative.
class PluginIpcResources
{
#if JUCE_WINDOWS
    HANDLE mapping = nullptr, wake = nullptr;
#else
    int mapping = -1, wake = -1;
    juce::String memoryName, fifoName;
    bool ownsMemory = false, ownsFifo = false;
    void names(const juce::String &token)
    {
        const auto id = token.fromLastOccurrenceOf("VibeRa-", false, false)
                            .retainCharacters("0123456789abcdefABCDEF")
                            .substring(0, 20);
        memoryName = "/vibera-" + id;
        fifoName = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("vibera-" + id + ".wake")
                       .getFullPathName();
    }
    bool map()
    {
        if (fcntl(mapping, F_SETFD, FD_CLOEXEC) != 0)
            return false;
        struct stat info{};
        if (fstat(mapping, &info) != 0 || info.st_uid != geteuid() ||
            info.st_size != off_t(sizeof(ipc::Shared)) || (info.st_mode & 077) != 0)
            return false;
        auto *p = mmap(nullptr, sizeof(ipc::Shared), PROT_READ | PROT_WRITE, MAP_SHARED, mapping, 0);
        if (p == MAP_FAILED)
            return false;
        shared = static_cast<ipc::Shared *>(p);
        wake = ::open(fifoName.toRawUTF8(), O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        return wake >= 0 && fstat(wake, &info) == 0 && S_ISFIFO(info.st_mode) && info.st_uid == geteuid() &&
               (info.st_mode & 077) == 0;
    }
#endif
    ipc::Shared *shared = nullptr;

  public:
    PluginIpcResources() = default;
    PluginIpcResources(const PluginIpcResources &) = delete;
    PluginIpcResources &operator=(const PluginIpcResources &) = delete;
    ~PluginIpcResources()
    {
        reset();
    }
    ipc::Shared *data() const noexcept
    {
        return shared;
    }
    bool create(const juce::String &name)
    {
        reset();
#if JUCE_WINDOWS
        wake = CreateEventW(nullptr, FALSE, FALSE, (name + "-audio").toWideCharPointer());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     DWORD(sizeof(ipc::Shared)), name.toWideCharPointer());
        if (mapping)
            shared = static_cast<ipc::Shared *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ipc::Shared)));
        return shared && wake;
#else
        names(name);
        mapping = shm_open(memoryName.toRawUTF8(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (mapping < 0)
            return false;
        ownsMemory = true;
        if (ftruncate(mapping, off_t(sizeof(ipc::Shared))) != 0 || mkfifo(fifoName.toRawUTF8(), 0600) != 0)
            return false;
        ownsFifo = true;
        return map();
#endif
    }
    bool open(const juce::String &name)
    {
        reset();
#if JUCE_WINDOWS
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.toWideCharPointer());
        if (mapping)
            shared = static_cast<ipc::Shared *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ipc::Shared)));
        wake = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + "-audio").toWideCharPointer());
        return shared && wake;
#else
        names(name);
        mapping = shm_open(memoryName.toRawUTF8(), O_RDWR, 0600);
        if (mapping < 0 || !map())
            return false;
        // Both peers now own open descriptors. Remove public names immediately
        // so a later host crash cannot leave large shared-memory objects behind.
        shm_unlink(memoryName.toRawUTF8());
        ::unlink(fifoName.toRawUTF8());
        return true;
#endif
    }
    void signal() const noexcept
    {
#if JUCE_WINDOWS
        if (wake)
            SetEvent(wake);
#else
        if (wake >= 0)
        {
            const char byte = 1;
            // EAGAIN means a notification is already queued. O_RDWR keeps a
            // reader open even during worker recovery, avoiding SIGPIPE.
            const auto ignored = ::write(wake, &byte, 1);
            (void)ignored;
        }
#endif
    }
    void wait(int milliseconds) const noexcept
    {
#if JUCE_WINDOWS
        WaitForSingleObject(wake, DWORD(milliseconds));
#else
        pollfd descriptor{wake, POLLIN, 0};
        if (::poll(&descriptor, 1, milliseconds) > 0)
        {
            char bytes[256];
            // Bounded drain; never wait for a producer. Packet state is read
            // after returning, including packets racing with this drain.
            for (int i = 0; i < 16 && ::read(wake, bytes, sizeof(bytes)) > 0; ++i)
            {
            }
        }
#endif
    }
    void reset() noexcept
    {
#if JUCE_WINDOWS
        if (shared)
            UnmapViewOfFile(shared);
        if (mapping)
            CloseHandle(mapping);
        if (wake)
            CloseHandle(wake);
        mapping = wake = nullptr;
#else
        if (shared)
            munmap(shared, sizeof(ipc::Shared));
        if (mapping >= 0)
            ::close(mapping);
        if (wake >= 0)
            ::close(wake);
        if (ownsFifo)
            ::unlink(fifoName.toRawUTF8());
        if (ownsMemory)
            shm_unlink(memoryName.toRawUTF8());
        mapping = wake = -1;
        ownsFifo = ownsMemory = false;
#endif
        shared = nullptr;
    }
};

inline juce::uint32 pluginProcessId() noexcept
{
#if JUCE_WINDOWS
    return GetCurrentProcessId();
#else
    return juce::uint32(getpid());
#endif
}
class PluginParentProcess
{
#if JUCE_WINDOWS
    HANDLE handle = nullptr;
#else
    juce::uint32 pid = 0;
#endif
  public:
    ~PluginParentProcess()
    {
        reset();
    }
    void open(juce::uint32 parent)
    {
        reset();
#if JUCE_WINDOWS
        handle = OpenProcess(SYNCHRONIZE, FALSE, parent);
#else
        pid = parent;
#endif
    }
    bool exited() const noexcept
    {
#if JUCE_WINDOWS
        return handle && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
#else
        // A direct child is reparented when its owner exits; PID reuse cannot
        // make the old process our parent again.
        return pid == 0 || juce::uint32(getppid()) != pid;
#endif
    }
    void reset() noexcept
    {
#if JUCE_WINDOWS
        if (handle)
            CloseHandle(handle);
        handle = nullptr;
#else
        pid = 0;
#endif
    }
};
} // namespace vibera
