"""Export tool/command references from an actual owned application instance.

Standard library only; schema export is not end-to-end feature qualification.
"""
import argparse
import hashlib
import json
import pathlib
import queue
import subprocess
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / "skills/vibera-studio/references/generated")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    destination = args.output.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen([str(executable), "--mcp-test"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                               encoding="utf-8", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    inbox = queue.Queue()
    errors = []

    def receive():
        try:
            for line in process.stdout:
                inbox.put(json.loads(line))
        except Exception as failure:
            inbox.put(failure)
        inbox.put(RuntimeError("Owned server closed stdout"))

    threading.Thread(target=receive, daemon=True).start()
    threading.Thread(target=lambda: errors.extend(process.stderr), daemon=True).start()
    serial = 0

    def request(method, params):
        nonlocal serial
        serial += 1
        process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": serial, "method": method, "params": params})+"\n")
        process.stdin.flush()
        reply = inbox.get(timeout=20)
        if isinstance(reply, Exception):
            raise reply
        if reply.get("id") != serial or "error" in reply:
            raise RuntimeError(str(reply))
        return reply["result"]

    try:
        hello = request("initialize", {"protocolVersion":"2025-11-25", "capabilities":{},
                                       "clientInfo":{"name":"VibeRa reference export","version":"0.1.0"}})
        process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        process.stdin.flush()
        tools = request("tools/list", {})["tools"]
        operations = []
        offset = 0
        while True:
            response = request("tools/call", {"name":"vibera_commands", "arguments":{
                "offset":offset,"limit":32,"includeSchemas":True}})
            if response.get("isError"):
                raise RuntimeError(str(response))
            page = response["structuredContent"]
            if page.get("contractVersion") != 1:
                raise RuntimeError("Executable does not expose contract1; no stale fallback")
            operations.extend(page["operations"])
            if page["nextOffset"] is None:
                break
            offset = page["nextOffset"]
        process.stdin.close()
        if process.wait(timeout=10) != 0:
            raise RuntimeError("Owned export instance did not exit successfully")
        metadata = {"server":hello["serverInfo"], "contractVersion":1,
                    "applicationSha256":hashlib.sha256(executable.read_bytes()).hexdigest(),
                    "operationCount":len(operations), "toolCount":len(tools),
                    "evidence":"Actual stdio discovery only; not musical/plugin/GUI/listening qualification"}
        for name, data in [("tools.json", tools), ("commands.json", operations), ("provenance.json", metadata)]:
            (destination / name).write_text(json.dumps(data, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(metadata))
    finally:
        if process.poll() is None:
            if not process.stdin.closed:
                process.stdin.close()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # Retained Popen handle identifies exactly this task-owned instance.
                process.terminate()
                process.wait(timeout=5)
        (destination / "export-stderr.log").write_text("".join(errors), encoding="utf-8")


if __name__ == "__main__":
    main()
