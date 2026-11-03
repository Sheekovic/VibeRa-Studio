"""Provider-neutral stdlib stdio client for owned VibeRa integration sessions.

No model SDK, mouse injection or network service. The default mode is disposable
test mode; normal MCP clients should use connection.example.json for their GUI.
"""
import json
import pathlib
import queue
import subprocess
import threading
import time


class VibeRaClient:
    def __init__(self, executable):
        self.executable = pathlib.Path(executable).resolve(strict=True)
        self.serial = 0
        self.messages = queue.Queue()
        self.stderr = []
        self.process = subprocess.Popen([str(self.executable), "--mcp-test"], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        threading.Thread(target=self._receive, daemon=True).start()
        threading.Thread(target=lambda: self.stderr.extend(self.process.stderr), daemon=True).start()
        try:
            self.server = self.request("initialize", {"protocolVersion":"2025-11-25", "capabilities":{},
                "clientInfo":{"name":"VibeRa neutral integration client","version":"0.4.0"}})["serverInfo"]
            self.process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            self.process.stdin.flush()
        except Exception:
            self.close()
            raise

    def _receive(self):
        try:
            for line in self.process.stdout:
                self.messages.put(json.loads(line))
        except Exception as failure:
            self.messages.put(failure)
        self.messages.put(RuntimeError("Owned server stdout closed"))

    def request(self, method, params):
        self.serial += 1
        self.process.stdin.write(json.dumps({"jsonrpc":"2.0","id":self.serial,"method":method,"params":params})+"\n")
        self.process.stdin.flush()
        response = self.messages.get(timeout=20)
        if isinstance(response, Exception):
            raise response
        if response.get("id") != self.serial or "error" in response:
            raise RuntimeError(str(response))
        return response["result"]

    def call(self, tool, arguments=None, allow_failure=False):
        result = self.request("tools/call", {"name":tool,"arguments":arguments or {}})
        if result.get("isError") and not allow_failure:
            raise RuntimeError(str(result.get("structuredContent", result)))
        return result

    def data(self, tool, arguments=None):
        return self.call(tool, arguments)["structuredContent"]

    def wait_job(self, job, seconds=40):
        deadline = time.monotonic()+seconds
        while job["completionState"] == "pending" and time.monotonic() < deadline:
            time.sleep(.03)
            job = self.data("vibera_project", {"action":"job","operation":job["operation"]})
        if job["completionState"] != "completed":
            raise RuntimeError(str(job))
        return job

    def close(self):
        forced = False
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                forced = True
                self.process.terminate()  # Exactly the retained task-owned process, after graceful EOF.
                self.process.wait(timeout=5)
        if forced or self.process.returncode != 0:
            raise RuntimeError(f"Owned client did not close gracefully: forced={forced}, exit={self.process.returncode}")
