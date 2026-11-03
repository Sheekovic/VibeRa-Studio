// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <windows.h>
#include <atomic>
#include "McpSchema.h"

namespace vibera
{
// Explicitly enabled stdio server. Pipe I/O lives on this worker; application tools
// are dispatched on JUCE's message thread. Nothing runs in the audio callback.
class McpServer final : private juce::Thread
{
    static constexpr int maximumRequestBytes = 65536;
    struct Reply
    {
        juce::WaitableEvent ready;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> started{false};
        juce::var value;
    };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::function<juce::var(const juce::String &, const juce::var &)> invoke;
    juce::var catalog;
    bool negotiated = false, initialized = false;
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    std::atomic<HANDLE> ioThread{nullptr};
    static juce::var object()
    {
        return juce::var(new juce::DynamicObject);
    }
    static void put(juce::var &v, const char *key, juce::var value)
    {
        v.getDynamicObject()->setProperty(key, std::move(value));
    }
    static juce::var error(const juce::var &id, int code, const juce::String &message, juce::var data = {})
    {
        auto result = object(), detail = object();
        put(result, "jsonrpc", "2.0");
        put(result, "id", id);
        put(detail, "code", code);
        put(detail, "message", message);
        if (!data.isVoid())
            put(detail, "data", data);
        put(result, "error", detail);
        return result;
    }
    juce::var onMessage(const juce::var &request)
    {
        const auto id = request["id"];
        const bool hasId = request.hasProperty("id");
        auto method = request["method"].toString();
        if (!request.isObject() || request["jsonrpc"].toString() != "2.0" || method.isEmpty())
            return error({}, -32600, "Invalid JSON-RPC request");
        if (!hasId)
        {
            if (method == "notifications/initialized" && negotiated)
                initialized = true;
            return {};
        }
        if (!id.isString() && !id.isInt() && !id.isInt64())
            return error({}, -32600, "Request id must be a string or integer");
        auto result = object();
        if (method == "initialize")
        {
            if (negotiated)
                return error(id, -32600, "Already initialized");
            auto requested = request["params"]["protocolVersion"].toString();
            if (requested.isEmpty())
                return error(id, -32602, "protocolVersion is required");
            juce::StringArray versions{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
            put(result, "protocolVersion", versions.contains(requested) ? requested : versions[0]);
            auto capabilities = object();
            put(capabilities, "tools", object());
            put(result, "capabilities", capabilities);
            auto info = object();
            put(info, "name", "VibeRa Studio");
            put(info, "version", juce::JUCEApplication::getInstance()->getApplicationVersion());
            put(result, "serverInfo", info);
            put(result, "instructions",
                "Opt-in application instance. Discover semantic operations with vibera_commands; gaps "
                "remain. "
                "Use stable session IDs, zero-based quarter-note beats, canonical document edits and job "
                "polling. "
                "Pointer/key tools are diagnostics, not semantic coverage. Snapshots never establish "
                "listening or hardware behavior. Stdio requests allow at most " +
                    juce::String(maximumRequestBytes) +
                    " raw bytes before newline. Chunk bulk edits within this bound; oversized frames "
                    "return request_too_large without executing a tool and the connection remains open.");
            negotiated = true;
        }
        else if (method == "ping")
        {
        }
        else if (!initialized)
            return error(id, -32000, "Complete initialize and notifications/initialized first");
        else if (method == "tools/list")
            put(result, "tools", catalog);
        else if (method == "tools/call")
        {
            auto name = request["params"]["name"].toString();
            auto args = request["params"]["arguments"];
            if (args.isVoid())
                args = object();
            if (!args.isObject())
                return error(id, -32602, "Tool arguments must be an object");
            bool known = false;
            juce::var schema;
            for (const auto &tool : *catalog.getArray())
                if (tool["name"].toString() == name)
                {
                    known = true;
                    schema = tool["inputSchema"];
                }
            if (!known)
                return error(id, -32602, "Unknown tool");
            auto pending = std::make_shared<Reply>();
            auto callback = invoke;
            auto lifetime = alive;
            juce::MessageManager::callAsync(
                [pending, callback, lifetime, name, args, schema]
                {
                    if (!lifetime->load() || pending->cancelled.load())
                        return;
                    pending->started = true;
                    try
                    {
                        validateMcpSchema(schema, args);
                        pending->value = callback(name, args);
                    }
                    catch (const std::exception &e)
                    {
                        auto failure = object();
                        auto text = object();
                        put(text, "type", "text");
                        put(text, "text", juce::String(e.what()));
                        put(failure, "content", juce::Array<juce::var>{text});
                        put(failure, "isError", true);
                        auto detail = object(), structured = object();
                        const auto *typed = dynamic_cast<const McpFailure *>(&e);
                        put(detail, "code", typed ? typed->code : juce::String("operation_failed"));
                        put(detail, "message", juce::String(e.what()));
                        put(detail, "retryable", false);
                        put(structured, "completionState", "failed");
                        put(structured, "error", detail);
                        put(failure, "structuredContent", structured);
                        pending->value = failure;
                    }
                    pending->ready.signal();
                });
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
            while (!threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
                if (pending->ready.wait(20))
                {
                    result = pending->value;
                    auto response = object();
                    put(response, "jsonrpc", "2.0");
                    put(response, "id", id);
                    put(response, "result", result);
                    return response;
                }
            pending->cancelled = true;
            auto outcome = object();
            put(outcome, "completionState", pending->started.load() ? "unknown" : "cancelled");
            put(outcome, "retryable", false);
            return error(id, -32000,
                         "Application operation timed out; inspect actual state/jobs before retrying an edit "
                         "that may have started",
                         outcome);
        }
        else
            return error(id, -32601, "Method not supported");
        auto response = object();
        put(response, "jsonrpc", "2.0");
        put(response, "id", id);
        put(response, "result", result);
        return response;
    }
    void run() override
    {
        HANDLE ownedThread = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &ownedThread, 0,
                             FALSE, DUPLICATE_SAME_ACCESS))
            return;
        ioThread = ownedThread;
        std::string line;
        bool oversized = false;
        while (!threadShouldExit())
        {
            DWORD available = 0;
            if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                break;
            if (!available)
            {
                wait(5);
                continue;
            }
            char bytes[4096];
            DWORD read = 0;
            if (!ReadFile(input, bytes, juce::jmin(DWORD(sizeof(bytes)), available), &read, nullptr))
                break;
            for (DWORD i = 0; i < read && !threadShouldExit(); ++i)
            {
                if (bytes[i] != '\n')
                {
                    if (oversized)
                        continue; // Drain this frame without accumulating unbounded input.
                    if (line.size() == size_t(maximumRequestBytes))
                    {
                        oversized = true;
                        line.clear();
                        continue;
                    }
                    line.push_back(bytes[i]);
                    continue;
                }
                juce::var reply;
                if (oversized)
                {
                    oversized = false;
                    auto detail = object();
                    put(detail, "code", "request_too_large");
                    put(detail, "maximumRequestBytes", maximumRequestBytes);
                    reply = error({}, -32600, "JSON-RPC request exceeds maximum frame bytes", detail);
                    // Never parse or dispatch any part of the rejected frame.
                }
                else
                {
                    juce::var request;
                    const auto parsed =
                        juce::JSON::parse(juce::String::fromUTF8(line.data(), int(line.size())), request);
                    line.clear();
                    reply = parsed.wasOk() ? onMessage(request) : error({}, -32700, "Malformed JSON");
                }
                if (reply.isVoid())
                    continue;
                const auto serialized = juce::JSON::toString(reply, true) + "\n";
                auto wire = serialized.toUTF8(); // Retain the owning String until the write completes.
                DWORD written = 0;
                if (!WriteFile(output, wire.getAddress(), DWORD(wire.sizeInBytes() - 1), &written, nullptr))
                {
                    signalThreadShouldExit();
                    break;
                }
            }
        }
        auto lifetime = alive;
        juce::MessageManager::callAsync(
            [lifetime]
            {
                if (lifetime->load())
                    juce::JUCEApplication::getInstance()->quit();
            });
    }

  public:
    McpServer(juce::var tools, std::function<juce::var(const juce::String &, const juce::var &)> fn)
        : juce::Thread("VibeRa MCP stdio"), invoke(std::move(fn)), catalog(std::move(tools))
    {
    }
    bool start()
    {
        input = GetStdHandle(STD_INPUT_HANDLE);
        output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE)
            return false;
        return startThread();
    }
    ~McpServer() override
    {
        *alive = false;
        signalThreadShouldExit();
        notify();
        if (auto ownedThread = ioThread.load())
            CancelSynchronousIo(ownedThread);
        stopThread(5000);
        if (auto ownedThread = ioThread.load())
            CloseHandle(ownedThread);
    }
};
} // namespace vibera
