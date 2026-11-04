// SPDX-License-Identifier: AGPL-3.0-or-later
#include "McpLoopback.h"
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <cerrno>
#endif

namespace vibera::ai
{
namespace
{
juce::var obj()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &o, const char *k, const juce::var &v)
{
    o.getDynamicObject()->setProperty(k, v);
}
juce::var reply(const juce::var &id, const juce::var &result)
{
    auto r = obj();
    set(r, "jsonrpc", "2.0");
    set(r, "id", id);
    set(r, "result", result);
    return r;
}
juce::var failure(const juce::var &id, int code, const juce::String &message)
{
    auto r = obj(), e = obj();
    set(r, "jsonrpc", "2.0");
    set(r, "id", id);
    set(e, "code", code);
    set(e, "message", message);
    set(r, "error", e);
    return r;
}
// Reads one '\n'-terminated line; false on close/error. Bounded to 4 MB.
bool readLine(juce::StreamingSocket &socket, std::string &pending, std::string &line, const CancelCheck &stop)
{
    for (;;)
    {
        if (const auto newline = pending.find('\n'); newline != std::string::npos)
        {
            line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        if (pending.size() > (4u << 20) || (stop && stop()))
            return false;
        const auto ready = socket.waitUntilReady(true, 200);
        if (ready < 0)
            return false;
        if (ready == 0)
            continue;
        char buffer[8192];
        const auto got = socket.read(buffer, sizeof(buffer), false);
        if (got <= 0)
            return false;
        pending.append(buffer, size_t(got));
    }
}
bool writeLine(juce::StreamingSocket &socket, const juce::String &text)
{
    const auto utf8 = (text + "\n").toStdString();
    return socket.write(utf8.data(), int(utf8.size())) == int(utf8.size());
}
} // namespace

McpLoopbackHost::~McpLoopbackHost()
{
    stop();
}

juce::String McpLoopbackHost::token() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return secret;
}

bool McpLoopbackHost::start(std::vector<ToolSpec> toolList, ToolExecutor run, juce::String serverName)
{
    stop();
    stopping.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex);
        tools = std::move(toolList);
        executor = std::move(run);
        name = std::move(serverName);
        juce::Random random;
        secret = juce::String::toHexString(random.nextInt64()) + juce::String::toHexString(random.nextInt64()) +
                 juce::Uuid().toDashedString().removeCharacters("-");
    }
    listener = std::make_unique<juce::StreamingSocket>();
    if (!listener->createListener(0, "127.0.0.1"))
    {
        listener.reset();
        return false;
    }
    listeningPort.store(listener->getBoundPort());
    acceptor = std::thread(
        [this]
        {
            while (!stopping.load())
            {
                std::unique_ptr<juce::StreamingSocket> client(listener->waitForNextConnection());
                if (!client || stopping.load())
                    break;
                std::shared_ptr<juce::StreamingSocket> shared(client.release());
                std::lock_guard<std::mutex> lock(mutex);
                sockets.push_back(shared);
                connections.emplace_back([this, shared] { serve(shared); });
            }
        });
    return true;
}

void McpLoopbackHost::stop()
{
    stopping.store(true);
    if (listener)
        listener->close(); // Unblocks waitForNextConnection.
    if (acceptor.joinable())
        acceptor.join();
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &socket : sockets)
            socket->close();
        threads.swap(connections);
        sockets.clear();
    }
    for (auto &thread : threads)
        if (thread.joinable())
            thread.join();
    listener.reset();
    listeningPort.store(0);
}

juce::var McpLoopbackHost::handle(Session &session, const juce::var &request, const CancelCheck &cancelled)
{
    if (!session.authenticated)
    {
        // First line must be {"vibera_bridge_token":"..."} from the relay.
        const auto offered = request["vibera_bridge_token"].toString();
        std::lock_guard<std::mutex> lock(mutex);
        session.authenticated = offered.isNotEmpty() && offered == secret;
        return {};
    }
    const auto id = request["id"];
    const auto method = request["method"].toString();
    if (!request.hasProperty("id"))
    {
        if (method == "notifications/initialized" && session.negotiated)
            session.initialized = true;
        return {};
    }
    if (method == "initialize")
    {
        auto result = obj(), capabilities = obj(), info = obj();
        const auto requested = request["params"]["protocolVersion"].toString();
        set(result, "protocolVersion", requested.isNotEmpty() ? requested : juce::String("2025-06-18"));
        set(capabilities, "tools", obj());
        set(result, "capabilities", capabilities);
        {
            std::lock_guard<std::mutex> lock(mutex);
            set(info, "name", name);
        }
        set(info, "version", "1");
        set(result, "serverInfo", info);
        set(result, "instructions", "Live VibeRa Studio session. Inspect with vibera_state/vibera_inspect, discover "
                                    "commands with vibera_commands, edit with vibera_command/vibera_batch, and "
                                    "generate audio with vibera_generate_music.");
        session.negotiated = true;
        return reply(id, result);
    }
    if (method == "ping")
        return reply(id, obj());
    if (method == "tools/list")
    {
        juce::Array<juce::var> list;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &tool : tools)
        {
            auto t = obj();
            set(t, "name", tool.name);
            set(t, "description", tool.description);
            set(t, "inputSchema", tool.inputSchema);
            list.add(t);
        }
        auto result = obj();
        set(result, "tools", list);
        return reply(id, result);
    }
    if (method == "tools/call")
    {
        const auto tool = request["params"]["name"].toString();
        auto args = request["params"]["arguments"];
        if (!args.isObject())
            args = obj();
        ToolExecutor run;
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            run = executor;
            for (const auto &t : tools)
                known = known || t.name == tool;
        }
        if (!known || !run)
            return failure(id, -32602, "Unknown tool: " + tool);
        const auto outcome = run(tool, args, cancelled);
        auto text = obj(), result = obj();
        set(text, "type", "text");
        // Below the size at which agent CLIs spill results to files the producer cannot read
        // (Claude Code has no file tools here); the note asks for a narrower readback instead.
        set(text, "text", clampToolResult(outcome.content, 24000));
        set(result, "content", juce::Array<juce::var>{text});
        if (outcome.isError)
            set(result, "isError", true);
        return reply(id, result);
    }
    return failure(id, -32601, "Method not supported: " + method);
}

void McpLoopbackHost::serve(std::shared_ptr<juce::StreamingSocket> socket)
{
    Session session;
    std::string pending, line;
    const CancelCheck stop = [this] { return stopping.load(); };
    while (!stopping.load() && readLine(*socket, pending, line, stop))
    {
        if (line.empty())
            continue;
        const auto request = juce::JSON::parse(juce::String::fromUTF8(line.data(), int(line.size())));
        const bool wasAuthenticated = session.authenticated;
        const auto response = handle(session, request, stop);
        if (!wasAuthenticated && !session.authenticated)
            break; // Wrong or missing token: drop the connection.
        if (!response.isVoid() && !writeLine(*socket, juce::JSON::toString(response, true)))
            break;
    }
    socket->close();
}

int runMcpBridge(int port, const juce::String &token)
{
#if JUCE_WINDOWS
    juce::StreamingSocket socket;
    if (!socket.connect("127.0.0.1", port, 5000))
        return 2;
    if (!writeLine(socket, "{\"vibera_bridge_token\":\"" + token + "\"}"))
        return 3;
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    std::atomic<bool> done{false};
    std::thread upstream(
        [&]
        {
            char buffer[8192];
            DWORD read = 0;
            while (!done.load() && ReadFile(input, buffer, sizeof(buffer), &read, nullptr) && read > 0)
                if (socket.write(buffer, int(read)) != int(read))
                    break;
            done.store(true);
            socket.close();
        });
    char buffer[8192];
    while (!done.load())
    {
        const auto ready = socket.waitUntilReady(true, 200);
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        const auto got = socket.read(buffer, sizeof(buffer), false);
        if (got <= 0)
            break;
        DWORD written = 0;
        if (!WriteFile(output, buffer, DWORD(got), &written, nullptr))
            break;
    }
    done.store(true);
    CancelSynchronousIo(upstream.native_handle());
    socket.close();
    upstream.join();
    return 0;
#else
    juce::StreamingSocket socket;
    if (!socket.connect("127.0.0.1", port, 5000))
        return 2;
    if (!writeLine(socket, "{\"vibera_bridge_token\":\"" + token + "\"}"))
        return 3;
    // This is the dedicated relay invocation. Block SIGPIPE in its I/O
    // threads so a disconnected parent cannot terminate it during cleanup.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, nullptr) != 0)
        return 4;
    std::atomic<bool> done{false};
    std::thread upstream([&]
    {
        char buffer[8192];
        while (!done.load())
        {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const auto ready = poll(&input, 1, 100);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0 || (input.revents & (POLLERR | POLLNVAL)))
                break;
            if (ready == 0)
                continue;
            const auto got = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                break;
            if (socket.write(buffer, int(got)) != int(got))
                break;
        }
        done.store(true);
        socket.close();
    });
    char buffer[8192];
    while (!done.load())
    {
        const auto ready = socket.waitUntilReady(true, 100);
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        const auto got = socket.read(buffer, sizeof(buffer), false);
        if (got <= 0)
            break;
        size_t at = 0;
        while (at < size_t(got) && !done.load())
        {
            pollfd output{STDOUT_FILENO, POLLOUT, 0};
            const auto writable = poll(&output, 1, 100);
            if (writable < 0 && errno == EINTR)
                continue;
            if (writable < 0 || (output.revents & (POLLERR | POLLHUP | POLLNVAL)))
            {
                done.store(true);
                break;
            }
            if (writable == 0)
                continue;
            const auto written = ::write(STDOUT_FILENO, buffer + at, std::min<size_t>(512, size_t(got) - at));
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
            {
                done.store(true);
                break;
            }
            at += size_t(written);
        }
    }
    done.store(true);
    socket.close();
    upstream.join();
    return 0;
#endif
}
} // namespace vibera::ai
