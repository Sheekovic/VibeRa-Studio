// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EffectChainPresetFiles.h"
#include <algorithm>
#include <array>
#include <cwchar>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#endif

namespace vibera::effect_chain
{
bool PresetFileJobs::terminal(Phase p) noexcept
{
    return p == Phase::completed || p == Phase::failed || p == Phase::cancelled;
}
PresetFileJobs::Cancellation PresetFileJobs::requestCancellation(std::atomic<Phase> &phase) noexcept
{
    auto seen = phase.load(std::memory_order_acquire);
    for (;;)
    {
        if (terminal(seen))
            return Cancellation::terminal;
        if (seen == Phase::committing)
            return Cancellation::tooLate;
        if (seen == Phase::cancelling)
            return Cancellation::requested;
        if (phase.compare_exchange_weak(seen, Phase::cancelling, std::memory_order_acq_rel))
            return Cancellation::requested;
    }
}
bool PresetFileJobs::beginCommit(std::atomic<Phase> &phase) noexcept
{
    auto running = Phase::running;
    return phase.compare_exchange_strong(running, Phase::committing, std::memory_order_acq_rel);
}
const char *PresetFileJobs::errorCode(Error code) noexcept
{
    switch (code)
    {
    case Error::none:
        return "none";
    case Error::wrongThread:
        return "chain.file.wrongThread";
    case Error::invalidPath:
        return "chain.file.invalidPath";
    case Error::unavailableDirectory:
        return "chain.file.unavailableDirectory";
    case Error::unsupportedPath:
        return "chain.file.unsupportedPath";
    case Error::outputExists:
        return "chain.file.outputExists";
    case Error::capacity:
        return "chain.file.capacity";
    case Error::stopping:
        return "chain.file.stopping";
    case Error::unknownOperation:
        return "chain.file.unknownOperation";
    case Error::activeOperation:
        return "chain.file.activeOperation";
    case Error::encodeFailed:
        return "chain.file.encodeFailed";
    case Error::readFailed:
        return "chain.file.readFailed";
    case Error::tooLarge:
        return "chain.file.tooLarge";
    case Error::decodeFailed:
        return "chain.file.decodeFailed";
    case Error::writeFailed:
        return "chain.file.writeFailed";
    case Error::flushFailed:
        return "chain.file.flushFailed";
    case Error::commitFailed:
        return "chain.file.commitFailed";
    case Error::cleanupFailed:
        return "chain.file.cleanupFailed";
    case Error::cancelled:
        return "chain.file.cancelled";
    case Error::exception:
        return "chain.file.exception";
    }
    return "chain.file.unknownError";
}
struct PresetFileJobs::Impl
{
    struct Record
    {
        juce::String operation, path;
        bool saving = false, overwrite = false;
        std::size_t weight = 0;
        std::unique_ptr<juce::MemoryBlock> bytes;
        std::atomic<Phase> phase{Phase::queued};
        Error error = Error::none;
        unsigned long nativeError = 0;
        bool fileCommitted = false;
        juce::String detail;
        juce::String temporaryPath;
        bool temporaryRemaining = false;
        unsigned long cleanupError = 0;
        std::shared_ptr<const Chain> loaded;
    };
    static_assert(std::atomic<Phase>::is_always_lock_free);
    const std::thread::id controlThread = std::this_thread::get_id();
    juce::File directory;
    std::array<std::unique_ptr<Record>, maximumRecords> records;
    std::array<Record *, maximumRecords> queue{};
    std::size_t head = 0, count = 0, retainedBytes = 0;
    std::mutex queueMutex;
    std::atomic<bool> stopping{false};
    juce::WaitableEvent workReady;
    std::thread worker; // Checked creation; JUCE ThreadPool discards startup failure.

    static bool localAbsolute(const juce::String &input)
    {
#if JUCE_WINDOWS
        const auto text = input.replaceCharacter('/', '\\');
        return text.length() >= 3 && text.length() <= 240 && std::wcslen(text.toWideCharPointer()) <= 240 &&
               ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z')) && text[1] == ':' &&
               text[2] == '\\' && !text.substring(2).containsChar(':') &&
               juce::CharPointer_UTF8::isValidString(text.toRawUTF8(), int(text.getNumBytesAsUTF8())) &&
               juce::File(text).getFullPathName().replaceCharacter('/', '\\') == text;
#else
        if (!input.startsWithChar('/') || input.startsWith("//") || input.getNumBytesAsUTF8() > 4000)
            return false;
        const auto parts = juce::StringArray::fromTokens(input, "/", "");
        for (const auto &part : parts)
            if (part == "." || part == "..")
                return false;
        return juce::File(input).getFullPathName() == input;
#endif
    }
#if !JUCE_WINDOWS
    static int openDirectory(const juce::File &root)
    {
        // Resolve each component through an owned descriptor. Refuse symlinks
        // in ancestors as well as at the final directory.
        int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0)
            return -1;
        const auto parts = juce::StringArray::fromTokens(root.getFullPathName(), "/", "");
        for (const auto &part : parts)
        {
            if (part.isEmpty())
                continue;
            const int next = openat(fd, part.toRawUTF8(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            const auto error = errno;
            ::close(fd);
            if (next < 0)
            {
                errno = error;
                return -1;
            }
            fd = next;
        }
        return fd;
    }
#endif
    static Error directories(const juce::File &root)
    {
#if JUCE_WINDOWS
        // Reserve headroom for the generated same-directory UUID temporary,
        // independently of a shorter requested target. Mapped drives are not
        // local merely because their spelling begins with a drive letter.
        const juce::String rootName =
            root.getFullPathName(); // Wide conversion mutates only this LOCAL String object.
        if (std::wcslen(rootName.toWideCharPointer()) > 200)
            return Error::unsupportedPath;
        const auto drive = GetDriveTypeW(root.getFullPathName().substring(0, 3).toWideCharPointer());
        if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK &&
            drive != DRIVE_CDROM)
            return Error::unsupportedPath;
        auto current = root;
        for (unsigned depth = 0; depth < 128; ++depth)
        {
            const auto attributes = GetFileAttributesW(current.getFullPathName().toWideCharPointer());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return Error::unavailableDirectory;
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
                return Error::unsupportedPath;
            const auto parent = current.getParentDirectory();
            if (parent == current)
                return Error::none;
            current = parent;
        }
        return Error::unsupportedPath;
#else
        if (root.getFullPathName().getNumBytesAsUTF8() > 3900)
            return Error::unsupportedPath;
        const int fd = openDirectory(root);
        if (fd < 0)
            return errno == ELOOP || errno == ENOTDIR ? Error::unsupportedPath : Error::unavailableDirectory;
        ::close(fd);
        return Error::none;
#endif
    }
    Error path(const juce::String &text, bool saving, bool overwrite) const
    {
        if (!localAbsolute(text))
            return Error::invalidPath;
        const juce::File file(text);
        const auto name = file.getFileName(),
                   stem = name.upToFirstOccurrenceOf(".", false, false).toUpperCase();
        if (file.getParentDirectory() != directory || !file.hasFileExtension("vibfx") ||
            name != name.trim() || name.endsWithChar('.') || name.containsAnyOf("<>\"|?*"))
            return Error::invalidPath;
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.length() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) && stem[3] >= '1' &&
             stem[3] <= '9'))
            return Error::unsupportedPath;
        if (const auto valid = directories(directory); valid != Error::none)
            return valid;
#if JUCE_WINDOWS
        const auto attributes = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
                return Error::unsupportedPath;
            if (saving && !overwrite)
                return Error::outputExists;
        }
        else if (const auto native = GetLastError(); native != ERROR_FILE_NOT_FOUND)
            return saving ? Error::writeFailed : Error::readFailed;
#else
        struct stat attributes{};
        if (lstat(file.getFullPathName().toRawUTF8(), &attributes) == 0)
        {
            if (!S_ISREG(attributes.st_mode))
                return Error::unsupportedPath;
            if (saving && !overwrite)
                return Error::outputExists;
        }
        else if (errno != ENOENT)
            return saving ? Error::writeFailed : Error::readFailed;
#endif
        return Error::none;
    }
    explicit Impl(const juce::String &root) : directory(localAbsolute(root) ? juce::File(root) : juce::File{})
    {
        if (!localAbsolute(root) || directories(directory) != Error::none)
            throw std::invalid_argument(
                "chain.file.invalidRoot: Existing canonical local ordinary directory required");
        worker =
            std::thread([this] { run(); }); // Failure throws before service publication/any accepted job.
    }
    ~Impl()
    {
        jassert(onControl());
        stop();
        workReady.signal();
        if (worker.joinable())
            worker.join(); // CONTROL shutdown only; never force a thread.
    }
    bool onControl() const noexcept
    {
        return std::this_thread::get_id() == controlThread;
    }
    void checkControl() const
    {
        if (!onControl())
            throw std::logic_error("chain.file.wrongThread: CONTROL owner required");
    }
    void stop() noexcept
    {
        stopping.store(true, std::memory_order_release);
        for (const auto &record : records)
            if (record)
                requestCancellation(record->phase);
        workReady.signal();
    }
    static Phase fail(Record &r, Error error, unsigned long native = 0)
    {
        r.error = error;
        r.nativeError = native;
        return r.phase.load(std::memory_order_acquire) == Phase::cancelling ? Phase::cancelled
                                                                            : Phase::failed;
    }
    static bool cancelled(const Record &r) noexcept
    {
        return r.phase.load(std::memory_order_acquire) == Phase::cancelling;
    }
#if JUCE_WINDOWS
    struct Temporary
    {
        Record &record;
        explicit Temporary(Record &r) : record(r) {}
        juce::String path;
        HANDLE file = INVALID_HANDLE_VALUE;
        bool created = false;
        ~Temporary()
        {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            if (created && !DeleteFileW(path.toWideCharPointer()))
            {
                record.cleanupError = GetLastError();
                record.temporaryRemaining = true; // Never hide a failed owned-temp cleanup.
            }
        }
    };
    Phase save(Record &r)
    {
        const juce::String destinationName = r.path;
        if (const auto valid = path(destinationName, true, r.overwrite); valid != Error::none)
            return fail(r, valid);
        Temporary temporary(r);
        temporary.path =
            directory.getChildFile(".vibra-fx-" + juce::Uuid().toString() + ".tmp").getFullPathName();
        r.temporaryPath = temporary.path;
        temporary.file = CreateFileW(temporary.path.toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (temporary.file == INVALID_HANDLE_VALUE)
            return fail(r, Error::writeFailed, GetLastError());
        temporary.created = true;
        const auto *data = static_cast<const unsigned char *>(r.bytes->getData());
        for (std::size_t at = 0; at < r.bytes->getSize();)
        {
            if (cancelled(r))
                return Phase::cancelled;
            const auto amount = DWORD(std::min<std::size_t>(65536, r.bytes->getSize() - at));
            DWORD written = 0;
            if (!WriteFile(temporary.file, data + at, amount, &written, nullptr) || written != amount)
                return fail(r, Error::writeFailed, GetLastError());
            at += written;
        }
        if (cancelled(r))
            return Phase::cancelled;
        if (!FlushFileBuffers(temporary.file))
            return fail(r, Error::flushFailed, GetLastError());
        if (!CloseHandle(temporary.file))
            return fail(r, Error::flushFailed, GetLastError());
        temporary.file = INVALID_HANDLE_VALUE;
        // Fresh path checks, not a cached admission. Atomic no-replace remains
        // authoritative if a destination appears AFTER this check.
        if (const auto valid = path(destinationName, true, r.overwrite); valid != Error::none)
            return fail(r, valid);
        const auto *destination = destinationName.toWideCharPointer(); // LOCAL converter, before commit CAS.
        if (!beginCommit(r.phase))
            return Phase::cancelled;
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (r.overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(temporary.path.toWideCharPointer(), destination, flags))
        {
            const auto native = GetLastError();
            return fail(r,
                        !r.overwrite && (native == ERROR_ALREADY_EXISTS || native == ERROR_FILE_EXISTS)
                            ? Error::outputExists
                            : Error::commitFailed,
                        native);
        }
        temporary.created = false;
        r.fileCommitted = true; // Only after the real successful atomic rename.
        return Phase::completed;
    }
#else
    struct Temporary
    {
        Record &record;
        int directory = -1, file = -1;
        juce::String name;
        bool created = false;
        explicit Temporary(Record &r) : record(r) {}
        ~Temporary()
        {
            if (file >= 0)
                ::close(file);
            if (created && unlinkat(directory, name.toRawUTF8(), 0) != 0)
            {
                record.cleanupError = errno;
                record.temporaryRemaining = true;
            }
            if (directory >= 0)
                ::close(directory);
        }
    };
    Phase save(Record &r)
    {
        if (const auto valid = path(r.path, true, r.overwrite); valid != Error::none)
            return fail(r, valid);
        Temporary temporary(r);
        temporary.directory = openDirectory(directory);
        if (temporary.directory < 0)
            return fail(r, Error::unavailableDirectory, errno);
        temporary.name = ".vibra-fx-" + juce::Uuid().toString() + ".tmp";
        r.temporaryPath = directory.getChildFile(temporary.name).getFullPathName();
        temporary.file = openat(temporary.directory, temporary.name.toRawUTF8(),
                                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (temporary.file < 0)
            return fail(r, Error::writeFailed, errno);
        temporary.created = true;
        const auto *data = static_cast<const unsigned char *>(r.bytes->getData());
        for (size_t at = 0; at < r.bytes->getSize();)
        {
            if (cancelled(r))
                return Phase::cancelled;
            const auto written = ::write(temporary.file, data + at,
                                          std::min<size_t>(65536, r.bytes->getSize() - at));
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return fail(r, Error::writeFailed, errno);
            at += size_t(written);
        }
        if (cancelled(r))
            return Phase::cancelled;
        if (fsync(temporary.file) != 0)
            return fail(r, Error::flushFailed, errno);
        const int fd = temporary.file;
        temporary.file = -1;
        if (::close(fd) != 0)
            return fail(r, Error::flushFailed, errno);
        if (const auto valid = path(r.path, true, r.overwrite); valid != Error::none)
            return fail(r, valid);
        const auto destination = juce::File(r.path).getFileName();
        if (!beginCommit(r.phase))
            return Phase::cancelled;
        // linkat is an atomic no-replace commit on the same filesystem.
        // renameat replaces the directory entry itself, never follows a target symlink.
        const int committed = r.overwrite
            ? renameat(temporary.directory, temporary.name.toRawUTF8(), temporary.directory, destination.toRawUTF8())
            : linkat(temporary.directory, temporary.name.toRawUTF8(), temporary.directory, destination.toRawUTF8(), 0);
        if (committed != 0)
            return fail(r, !r.overwrite && errno == EEXIST ? Error::outputExists : Error::commitFailed, errno);
        r.fileCommitted = true;
        if (r.overwrite)
            temporary.created = false;
        else if (unlinkat(temporary.directory, temporary.name.toRawUTF8(), 0) == 0)
            temporary.created = false;
        if (fsync(temporary.directory) != 0)
            return fail(r, Error::flushFailed, errno);
        return Phase::completed;
    }
#endif
    Phase load(Record &r)
    {
        const juce::String inputName = r.path;
        if (const auto valid = path(inputName, false, false); valid != Error::none)
            return fail(r, valid);
#if JUCE_WINDOWS
        auto input = juce::File(inputName).createInputStream();
        if (!input || input->getStatus().failed())
            return fail(r, Error::readFailed);
        const auto length = input->getTotalLength();
        if (length < 0 || std::uint64_t(length) > maximumBytes)
            return fail(r, Error::tooLarge);
        juce::MemoryBlock data(std::size_t(length), false);
        auto *bytes = static_cast<char *>(data.getData());
        for (std::size_t at = 0; at < data.getSize();)
        {
            if (cancelled(r))
                return Phase::cancelled;
            const int amount = int(std::min<std::size_t>(65536, data.getSize() - at));
            const int read = input->read(bytes + at, amount);
            if (read <= 0)
                return fail(r, Error::readFailed);
            at += std::size_t(read);
        }
        char extra = 0;
        if (input->read(&extra, 1) != 0 || input->getStatus().failed())
            return fail(r, Error::readFailed);
#else
        Temporary input(r);
        input.directory = openDirectory(directory);
        if (input.directory < 0)
            return fail(r, Error::unavailableDirectory, errno);
        input.file = openat(input.directory, juce::File(inputName).getFileName().toRawUTF8(),
                            O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (input.file < 0)
            return fail(r, Error::readFailed, errno);
        struct stat attributes{};
        if (fstat(input.file, &attributes) != 0 || !S_ISREG(attributes.st_mode))
            return fail(r, Error::unsupportedPath, errno);
        if (attributes.st_size < 0 || std::uint64_t(attributes.st_size) > maximumBytes)
            return fail(r, Error::tooLarge);
        juce::MemoryBlock data(size_t(attributes.st_size), false);
        auto *bytes = static_cast<char *>(data.getData());
        for (size_t at = 0; at < data.getSize();)
        {
            if (cancelled(r))
                return Phase::cancelled;
            const auto got = ::read(input.file, bytes + at, std::min<size_t>(65536, data.getSize() - at));
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                return fail(r, Error::readFailed, errno);
            at += size_t(got);
        }
        char extra = 0;
        ssize_t remaining;
        do { remaining = ::read(input.file, &extra, 1); } while (remaining < 0 && errno == EINTR);
        if (remaining != 0)
            return fail(r, Error::readFailed, errno);
#endif
        if (cancelled(r))
            return Phase::cancelled;
        std::unique_ptr<Chain> decoded;
        const auto result = decode({static_cast<const std::byte *>(data.getData()), data.getSize()}, decoded);
        if (result.failed())
        {
            r.detail = result.getErrorMessage();
            return fail(r, Error::decodeFailed);
        }
        auto ready = std::shared_ptr<const Chain>(std::move(decoded));
        if (!beginCommit(r.phase))
            return Phase::cancelled;
        r.loaded = std::move(ready);
        return Phase::completed; // Data only. No model/history/native completion.
    }
    void run() noexcept
    {
        for (;;)
        {
            Record *record = nullptr;
            {
                std::lock_guard lock(queueMutex);
                if (count)
                {
                    record = queue[head];
                    head = (head + 1) % maximumRecords;
                    --count;
                }
                else if (stopping.load(std::memory_order_acquire))
                    return;
            }
            if (!record)
            {
                workReady.wait();
                continue;
            }
            Phase outcome = Phase::cancelled;
            auto queued = Phase::queued;
            if (record->phase.compare_exchange_strong(queued, Phase::running, std::memory_order_acq_rel))
            {
                try
                {
                    outcome = record->saving ? save(*record) : load(*record);
                }
                catch (...)
                {
                    outcome = fail(*record, Error::exception);
                }
            }
            auto prior = record->phase.load(std::memory_order_acquire);
            for (;;)
            {
                auto terminalOutcome = prior == Phase::cancelling ? Phase::cancelled : outcome;
                if (terminalOutcome == Phase::cancelled)
                    record->error = Error::cancelled;
                if (record->temporaryRemaining)
                {
                    record->error = Error::cleanupFailed;
                    terminalOutcome = Phase::failed;
                }
                // Also arbitrate failure-vs-cancel publication. A plain store
                // could overwrite a successfully accepted late cancellation.
                if (record->phase.compare_exchange_weak(prior, terminalOutcome, std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
                    break;
            }
            // LAST access to record. CONTROL can now evict terminal records;
            // no background destruction/callback can subsequently touch one.
        }
    }
    juce::Result enqueue(std::unique_ptr<Record> record, juce::String &output)
    {
        if (stopping.load(std::memory_order_acquire))
            return juce::Result::fail(errorCode(Error::stopping));
        std::size_t slot = maximumRecords;
        for (std::size_t i = 0; i < maximumRecords; ++i)
            if (!records[i])
            {
                slot = i;
                break;
            }
        if (slot == maximumRecords || record->weight > maximumRetainedBytes - retainedBytes)
            return juce::Result::fail(errorCode(Error::capacity));
        auto accepted = record->operation; // All fallible prep before accepted owner/queue insertion.
        std::lock_guard lock(queueMutex);
        if (count == maximumRecords)
            return juce::Result::fail(errorCode(Error::capacity));
        retainedBytes += record->weight;
        records[slot] = std::move(record); // No implicit active or terminal receipt eviction.
        queue[(head + count) % maximumRecords] = records[slot].get();
        ++count;
        output.swapWith(accepted);
        workReady.signal();
        return juce::Result::ok();
    }
};
PresetFileJobs::PresetFileJobs(const juce::String &directory) : impl(std::make_unique<Impl>(directory)) {}
PresetFileJobs::~PresetFileJobs() = default;
juce::Result PresetFileJobs::submitSave(const juce::String &path, const Chain &chain, bool overwrite,
                                        juce::String &operation)
{
    if (!impl->onControl())
        return juce::Result::fail(errorCode(Error::wrongThread));
    if (impl->stopping.load(std::memory_order_acquire))
        return juce::Result::fail(errorCode(Error::stopping));
    try
    {
        if (const auto valid = impl->path(path, true, overwrite); valid != Error::none)
            return juce::Result::fail(errorCode(valid));
        auto record = std::make_unique<Impl::Record>();
        if (const auto valid = encode(chain, record->bytes); valid.failed())
            return juce::Result::fail(juce::String(errorCode(Error::encodeFailed)) + ": " +
                                      valid.getErrorMessage());
        record->weight = record->bytes->getSize();
        record->saving = true;
        record->overwrite = overwrite;
        record->path = juce::File(path).getFullPathName();
        record->operation = juce::Uuid().toString();
        return impl->enqueue(std::move(record), operation);
    }
    catch (...)
    {
        return juce::Result::fail(errorCode(Error::exception));
    }
}
juce::Result PresetFileJobs::submitLoad(const juce::String &path, juce::String &operation)
{
    if (!impl->onControl())
        return juce::Result::fail(errorCode(Error::wrongThread));
    if (impl->stopping.load(std::memory_order_acquire))
        return juce::Result::fail(errorCode(Error::stopping));
    try
    {
        if (const auto valid = impl->path(path, false, false); valid != Error::none)
            return juce::Result::fail(errorCode(valid));
        auto record = std::make_unique<Impl::Record>();
        record->weight = maximumBytes; // Conservative fixed reservation BEFORE any file allocation.
        record->path = juce::File(path).getFullPathName();
        record->operation = juce::Uuid().toString();
        return impl->enqueue(std::move(record), operation);
    }
    catch (...)
    {
        return juce::Result::fail(errorCode(Error::exception));
    }
}
std::optional<PresetFileJobs::Observation> PresetFileJobs::read(const juce::String &operation) const
{
    impl->checkControl();
    for (const auto &r : impl->records)
        if (r && r->operation == operation)
        {
            Observation value;
            value.operation = r->operation;
            value.path = r->path;
            value.saving = r->saving;
            value.overwrite = r->overwrite;
            value.phase = r->phase.load(std::memory_order_acquire);
            if (terminal(value.phase))
            {
                value.error = r->error;
                value.nativeError = r->nativeError;
                value.detail = r->detail;
                value.fileCommitted = r->fileCommitted;
                value.loaded = r->loaded;
                value.temporaryPath = r->temporaryPath;
                value.temporaryRemaining = r->temporaryRemaining;
                value.cleanupError = r->cleanupError;
            }
            return value;
        }
    return {};
}
PresetFileJobs::Cancellation PresetFileJobs::cancel(const juce::String &operation)
{
    impl->checkControl();
    for (const auto &r : impl->records)
        if (r && r->operation == operation)
            return requestCancellation(r->phase);
    return Cancellation::unknownOperation;
}
juce::Result PresetFileJobs::forget(const juce::String &operation)
{
    if (!impl->onControl())
        return juce::Result::fail(errorCode(Error::wrongThread));
    for (auto &record : impl->records)
        if (record && record->operation == operation)
        {
            if (!terminal(record->phase.load(std::memory_order_acquire)))
                return juce::Result::fail(errorCode(Error::activeOperation));
            impl->retainedBytes -= record->weight;
            record.reset(); // CONTROL acknowledgement after worker's final publication.
            return juce::Result::ok();
        }
    return juce::Result::fail(errorCode(Error::unknownOperation));
}
juce::Result PresetFileJobs::configureDirectory(const juce::String &absoluteDirectory)
{
    if (!impl->onControl())
        return juce::Result::fail(errorCode(Error::wrongThread));
    try
    {
        if (impl->stopping.load(std::memory_order_acquire))
            return juce::Result::fail(errorCode(Error::stopping));
        if (!idle())
            return juce::Result::fail(errorCode(Error::activeOperation));
        if (!Impl::localAbsolute(absoluteDirectory))
            return juce::Result::fail(errorCode(Error::invalidPath));
        juce::File candidate(absoluteDirectory);
        if (const auto valid = Impl::directories(candidate); valid != Error::none)
            return juce::Result::fail(errorCode(valid));
        impl->directory = std::move(candidate); // All prior worker uses ended before terminal release.
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail(errorCode(Error::exception));
    }
}
juce::String PresetFileJobs::configuredDirectory() const
{
    impl->checkControl();
    return impl->directory.getFullPathName();
}
void PresetFileJobs::requestStop()
{
    impl->checkControl();
    impl->stop();
}
bool PresetFileJobs::idle() const
{
    impl->checkControl();
    for (const auto &record : impl->records)
        if (record && !terminal(record->phase.load(std::memory_order_acquire)))
            return false;
    return true;
}
} // namespace vibera::effect_chain
