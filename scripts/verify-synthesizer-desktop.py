# SPDX-License-Identifier: AGPL-3.0-or-later
"""Owned native Synth input/captures with the exact 48k/256 virtual-device fixture.

Run on the authorized interactive Default desktop under the engineering lease.
Pointer input is diagnostic acceptance, never semantic MCP coverage or hearing.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import queue
import stat
import subprocess
import sys
import threading
import time
import traceback

from dev_support import content_identity, owned_job_members, process_identity

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True

OVERWRITE_UIA_QUERY = r'''param([long]$DialogHandle, [int]$ExpectedProcessId)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$rootElement = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$DialogHandle)
$rootCurrent = $rootElement.Current
$rootHandle = ([long]$rootCurrent.NativeWindowHandle) -band 4294967295
if ($rootHandle -ne $DialogHandle -or $rootCurrent.ProcessId -ne $ExpectedProcessId -or
    $rootCurrent.Name -ne 'Confirm Save As' -or $rootCurrent.ClassName -ne '#32770') {
    throw 'Exact worker overwrite root identity mismatch'
}
$elements = $rootElement.FindAll([System.Windows.Automation.TreeScope]::Subtree,
                               [System.Windows.Automation.Condition]::TrueCondition)
if ($elements.Count -gt 256) { throw 'Bounded overwrite UI Automation subtree exceeded' }
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
$rows = @()
foreach ($element in $elements) {
    $current = $element.Current
    $path = @()
    $ancestor = $element
    $withinRoot = $false
    for ($depth = 0; $null -ne $ancestor -and $depth -lt 32; ++$depth) {
        $item = $ancestor.Current
        $handleValue = ([long]$item.NativeWindowHandle) -band 4294967295
        $path += [ordered]@{ hwnd=$handleValue; pid=$item.ProcessId; type=$item.ControlType.Id; class=$item.ClassName }
        if ($handleValue -eq $DialogHandle -and $item.ProcessId -eq $ExpectedProcessId -and $item.ClassName -eq '#32770') {
            $withinRoot = $true
            break
        }
        $ancestor = $walker.GetParent($ancestor)
    }
    $name = [string]$current.Name
    if ($name.Length -gt 1024) { $name = $name.Substring(0, 1024) }
    $bounds = $current.BoundingRectangle
    $rows += [ordered]@{ name=$name; type=$current.ControlType.Id; hwnd=(([long]$current.NativeWindowHandle) -band 4294967295);
                         pid=$current.ProcessId; class=$current.ClassName; enabled=$current.IsEnabled;
                         offscreen=$current.IsOffscreen; bounds=@($bounds.X,$bounds.Y,$bounds.Width,$bounds.Height);
                         withinRoot=$withinRoot; path=$path }
}
[ordered]@{ hwnd=$rootHandle; pid=$rootCurrent.ProcessId; title=$rootCurrent.Name; class=$rootCurrent.ClassName; elements=$rows } |
    ConvertTo-Json -Depth 10 -Compress
'''


class NativeForegroundLost(RuntimeError):
    def __init__(self, hwnd, pid):
        super().__init__("Exact worker lost foreground; native input paused")
        self.hwnd, self.pid = hwnd, pid


class Windows:
    def __init__(self):
        self.u = c.WinDLL("user32", use_last_error=True)
        self.k = c.WinDLL("kernel32", use_last_error=True)
        self.g = c.WinDLL("gdi32", use_last_error=True)
        self.s = c.WinDLL("shcore", use_last_error=True)
        for library, definitions in [
            (self.u, [
                ("SetThreadDpiAwarenessContext", [w.HANDLE], w.HANDLE),
                ("GetThreadDesktop", [w.DWORD], w.HANDLE),
                ("OpenInputDesktop", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
                ("CloseDesktop", [w.HANDLE], w.BOOL),
                ("GetUserObjectInformationW", [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
                ("GetCursorPos", [c.POINTER(w.POINT)], w.BOOL),
                ("SetCursorPos", [c.c_int, c.c_int], w.BOOL),
                ("GetAsyncKeyState", [c.c_int], c.c_short),
                ("GetForegroundWindow", [], w.HWND),
                ("WindowFromPoint", [w.POINT], w.HWND),
                ("GetAncestor", [w.HWND, w.UINT], w.HWND),
                ("GetWindowThreadProcessId", [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
                ("GetWindowTextW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
                ("GetClassNameW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
                ("GetParent", [w.HWND], w.HWND),
                ("GetDlgCtrlID", [w.HWND], c.c_int),
                ("IsWindowEnabled", [w.HWND], w.BOOL),
                ("GetWindowRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
                ("SendMessageTimeoutW", [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)], w.LPARAM),
                ("IsWindow", [w.HWND], w.BOOL),
                ("IsWindowVisible", [w.HWND], w.BOOL),
                ("SetForegroundWindow", [w.HWND], w.BOOL),
                ("SetWindowPos", [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT], w.BOOL),
                ("GetDpiForWindow", [w.HWND], w.UINT),
                ("GetClientRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
                ("ClientToScreen", [w.HWND, c.POINTER(w.POINT)], w.BOOL),
                ("GetDC", [w.HWND], w.HDC),
                ("ReleaseDC", [w.HWND, w.HDC], c.c_int),
                ("PrintWindow", [w.HWND, w.HDC, w.UINT], w.BOOL),
                ("GetKeyboardLayout", [w.DWORD], w.HANDLE),
                ("MapVirtualKeyExW", [w.UINT, w.UINT, w.HANDLE], w.UINT),
                ("mouse_event", [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_size_t], None),
                ("keybd_event", [w.BYTE, w.BYTE, w.DWORD, c.c_size_t], None),
            ]),
            (self.k, [
                ("GetCurrentThreadId", [], w.DWORD),
                ("OpenProcess", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
                ("CloseHandle", [w.HANDLE], w.BOOL),
                ("WaitForSingleObject", [w.HANDLE, w.DWORD], w.DWORD),
                ("QueryFullProcessImageNameW", [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)], w.BOOL),
                ("TerminateProcess", [w.HANDLE, w.UINT], w.BOOL),
            ]),
            (self.g, [
                ("CreateCompatibleDC", [w.HDC], w.HDC),
                ("CreateCompatibleBitmap", [w.HDC, c.c_int, c.c_int], w.HBITMAP),
                ("SelectObject", [w.HDC, w.HANDLE], w.HANDLE),
                ("DeleteObject", [w.HANDLE], w.BOOL),
                ("DeleteDC", [w.HDC], w.BOOL),
                ("GetDIBits", [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT], c.c_int),
            ]),
        ]:
            for name, arguments, result in definitions:
                function = getattr(library, name)
                function.argtypes, function.restype = arguments, result

    def cursor(self):
        point = w.POINT()
        if not self.u.GetCursorPos(c.byref(point)):
            raise RuntimeError("Cursor observation unavailable")
        return point.x, point.y

    def pid(self, hwnd):
        value = w.DWORD()
        self.u.GetWindowThreadProcessId(hwnd, c.byref(value))
        return value.value

    def desktop(self, handle):
        value = c.create_unicode_buffer(256)
        needed = w.DWORD()
        if handle and self.u.GetUserObjectInformationW(handle, 2, value, c.sizeof(value), c.byref(needed)):
            return value.value
        return None

    def preflight(self):
        aware = bool(self.u.SetThreadDpiAwarenessContext(w.HANDLE(-4)))
        thread = self.desktop(self.u.GetThreadDesktop(self.k.GetCurrentThreadId()))
        handle = self.u.OpenInputDesktop(0, False, 1)
        try:
            active = self.desktop(handle)
        finally:
            if handle:
                self.u.CloseDesktop(handle)
        first = self.cursor()
        time.sleep(.5)
        second = self.cursor()
        held = any(self.u.GetAsyncKeyState(key) & 0x8000 for key in (1, 2, 4, 8, 9, 13, 16, 17, 18, 27, 91, 92, *range(32, 91)))
        return dict(dpiContextAccepted=aware, threadMatchesInputDesktop=thread == active and thread is not None,
                    threadIsDefaultDesktop=thread == "Default", inputIsDefaultDesktop=active == "Default",
                    cursorMoved=first != second, heldControl=held), second

    def monitors(self):
        class Info(c.Structure):
            _fields_ = [("size", w.DWORD), ("bounds", w.RECT), ("work", w.RECT), ("flags", w.DWORD), ("device", w.WCHAR * 32)]
        callback = c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
        self.u.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(Info)]
        self.s.GetDpiForMonitor.argtypes = [w.HANDLE, c.c_int, c.POINTER(w.UINT), c.POINTER(w.UINT)]
        found = []
        def visit(handle, dc, rect, unused):
            info = Info()
            info.size = c.sizeof(info)
            x, y = w.UINT(), w.UINT()
            if self.u.GetMonitorInfoW(handle, c.byref(info)) and self.s.GetDpiForMonitor(handle, 0, c.byref(x), c.byref(y)) == 0:
                found.append(dict(device=info.device, dpi=x.value, dpiY=y.value,
                                  work=[info.work.left, info.work.top, info.work.right, info.work.bottom]))
            return True
        self.u.EnumDisplayMonitors.argtypes = [w.HDC, c.POINTER(w.RECT), callback, w.LPARAM]
        if not self.u.EnumDisplayMonitors(None, None, callback(visit), 0):
            raise RuntimeError("Monitor observation unavailable")
        return found

    def windows(self, owner):
        callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        result = []
        def visit(hwnd, unused):
            if self.pid(hwnd) == owner and self.u.IsWindowVisible(hwnd):
                title = c.create_unicode_buffer(512)
                self.u.GetWindowTextW(hwnd, title, len(title))
                result.append((hwnd, title.value))
            return True
        self.u.EnumWindows.argtypes = [callback, w.LPARAM]
        self.u.EnumWindows(callback(visit), 0)
        return result

    def image_path(self, handle):
        buffer = c.create_unicode_buffer(32768)
        size = w.DWORD(len(buffer))
        if not self.k.QueryFullProcessImageNameW(handle, 0, buffer, c.byref(size)):
            raise RuntimeError("Worker image identity unavailable")
        return Path(buffer.value).resolve()

    def children(self, parent, ready_only=True):
        callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        result = []
        owner = self.pid(parent)
        def visit(hwnd, unused):
            if self.pid(hwnd) == owner:
                visible, enabled = bool(self.u.IsWindowVisible(hwnd)), bool(self.u.IsWindowEnabled(hwnd))
                if ready_only and not (visible and enabled):
                    return True
                kind = c.create_unicode_buffer(64)
                self.u.GetClassNameW(hwnd, kind, len(kind))
                result.append(dict(hwnd=hwnd, kind=kind.value, controlId=self.u.GetDlgCtrlID(hwnd),
                                   visible=visible, enabled=enabled))
            return True
        self.u.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
        self.u.EnumChildWindows(parent, callback(visit), 0)
        return result

    def focus(self, hwnd):
        class Gui(c.Structure):
            _fields_ = [("size", w.DWORD), ("flags", w.DWORD),
                        ("active", w.HWND), ("focus", w.HWND), ("capture", w.HWND),
                        ("menu", w.HWND), ("moveSize", w.HWND), ("caret", w.HWND), ("caretRect", w.RECT)]
        info = Gui()
        info.size = c.sizeof(info)
        owner = w.DWORD()
        thread = self.u.GetWindowThreadProcessId(hwnd, c.byref(owner))
        self.u.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(Gui)]
        if not self.u.GetGUIThreadInfo(thread, c.byref(info)):
            raise RuntimeError("Owned GUI focus observation unavailable")
        return info.focus

    def text(self, hwnd):
        # WM_GETTEXT via a bounded read; GetWindowText cannot read another process's Edit.
        value = c.create_unicode_buffer(32768)
        result = c.c_size_t()
        if not self.u.SendMessageTimeoutW(hwnd, 13, len(value), c.addressof(value), 2, 1000, c.byref(result)):
            raise RuntimeError("Owned native filename readback unavailable")
        return value.value

    def parent_pid(self, target):
        class Entry(c.Structure):
            _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("pid", w.DWORD), ("heap", c.c_size_t),
                        ("module", w.DWORD), ("threads", w.DWORD), ("parent", w.DWORD),
                        ("priority", w.LONG), ("flags", w.DWORD), ("image", w.WCHAR * 260)]
        self.k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
        self.k.CreateToolhelp32Snapshot.restype = w.HANDLE
        self.k.Process32FirstW.argtypes = self.k.Process32NextW.argtypes = [w.HANDLE, c.POINTER(Entry)]
        handle = self.k.CreateToolhelp32Snapshot(2, 0)
        if not handle or handle == c.c_void_p(-1).value:
            raise RuntimeError("Worker parent identity unavailable")
        try:
            entry = Entry()
            entry.size = c.sizeof(entry)
            valid = self.k.Process32FirstW(handle, c.byref(entry))
            while valid:
                if entry.pid == target:
                    return entry.parent
                valid = self.k.Process32NextW(handle, c.byref(entry))
        finally:
            self.k.CloseHandle(handle)
        raise RuntimeError("Exact worker no longer exists")


def filename_control_kind(ancestors, dialog_title=None):
    """Recognize only the legacy filename ID or the observed modern filename ancestry."""
    if any(control["controlId"] == 0x0480 for control in ancestors):
        return "legacy-edt1"
    shape = [(control["kind"], control["controlId"]) for control in ancestors[:4]]
    if shape == [("Edit", 1001), ("ComboBox", 0), ("FloatNotifySink", 0), ("DirectUIHWND", 0)]:
        return "modern-Edit1001-ComboBox0"
    # Observed Open dialog uses SDK cmb13=0x047c (1148), not edt1.
    if (dialog_title == "Load synthesizer preset" and
            [(control["kind"], control["controlId"]) for control in ancestors] ==
            [("Edit", 1148), ("ComboBox", 1148), ("ComboBoxEx32", 1148)]):
        return "load-cmb13-ComboBoxEx32"
    return None


def overwrite_uia_controls(snapshot, dialog, worker, filename):
    """Select uniquely named real buttons and the exact file question in one owned subtree."""
    if (snapshot.get("hwnd") != dialog or snapshot.get("pid") != worker or
            snapshot.get("title") != "Confirm Save As" or snapshot.get("class") != "#32770"):
        raise RuntimeError("UI Automation root escaped the exact worker overwrite dialog")
    owned = []
    for element in snapshot.get("elements", []):
        path = element.get("path", [])
        if (element.get("pid") == worker and element.get("withinRoot") is True and path and
                path[-1].get("hwnd") == dialog and path[-1].get("class") == "#32770" and
                all(ancestor.get("pid") == worker for ancestor in path)):
            owned.append(element)
    question = any(element.get("type") == 50020 and not element.get("offscreen", True) and
                   filename in element.get("name", "") for element in owned)
    selected = {}
    for label in ("yes", "no"):
        candidates = [element for element in owned if
                      (element.get("type") == 50000 or
                       element.get("type") == 50033 and element.get("class") == "CCPushButton" and
                       type(element.get("hwnd")) is int and element["hwnd"] > 0) and
                      element.get("enabled") is True and element.get("offscreen") is False and
                      element.get("name", "").replace("&", "").strip().casefold() == label]
        if len(candidates) > 1:
            raise RuntimeError("Ambiguous UI Automation overwrite " + label + " buttons")
        if not candidates:
            return None
        bounds = candidates[0].get("bounds", [])
        if (len(bounds) != 4 or not all(type(value) in (int, float) and math.isfinite(value) for value in bounds) or
                bounds[2] <= 0 or bounds[3] <= 0):
            return None
        selected[label] = candidates[0]
    return selected if question else None


def main():
    preset_io = len(sys.argv) == 4 and sys.argv[3] == "--preset-io"
    if len(sys.argv) != 3 and not preset_io:
        raise ValueError("Usage: verify-synthesizer-desktop.py ABSOLUTE_BUILD_APP FRESH_BUILD_WINDOWS_OUTPUT [--preset-io]")
    app, output = [Path(value).resolve() for value in sys.argv[1:3]]
    if (not app.is_relative_to(ROOT / "build/windows") or app.name != "VibeRa Studio.exe" or not app.is_file() or
            not output.is_relative_to(ROOT / "build/windows") or output == ROOT / "build/windows" or output.exists()):
        raise ValueError("Exact build executable and a fresh owned build/windows child directory required")
    for ancestor in (output, *output.parents):
        if ancestor.exists() and (ancestor.is_symlink() or getattr(ancestor.stat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError("Linked output ancestry refused")
        if ancestor == ROOT:
            break
    output.mkdir(parents=True)
    began = time.monotonic()
    deadline = began + 95
    report = dict(passed=False, checks=[], jobs=[], captures=[], telemetry=[], inputTestStarted=False,
                  nativeInput=False, humanInput=False, virtualAudio=False, virtualAudioRequested=True, hardware=False, listening=False,
                  semanticCoverage=False, imageInspection="pending human/agent inspection of saved captures",
                  applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
                  workerSha256=hashlib.sha256(app.with_name("VibeRa Plugin Worker.exe").read_bytes()).hexdigest(),
                  softwareWrapperRendererRequested=True, budgetSeconds=110, workflowBudgetSeconds=95, stageTimings=[],
                  phase="preset-io" if preset_io else "controls-dpi")
    source_names = ["scripts/verify-synthesizer-desktop.py", "scripts/verify-installed-mcp.py", "scripts/dev_support.py",
                    "src/synth/SynthEditor.cpp", "src/synth/SynthEditor.h", "src/synth/SynthBrowser.h", "src/synth/SynthPlugin.cpp",
                    "src/synth/SynthPlugin.h", "src/synth/SynthEngine.cpp", "src/synth/SynthEngine.h", "src/synth/SynthPresets.cpp",
                    "src/synth/SynthPresets.h", "src/synth/SynthState.h", "src/synth/SynthUserPresets.h", "src/synth/SynthRegistry.h",
                    "src/synth/SynthContracts.h",
                    "src/synth/SynthMcp.h", "src/StudioSynth.inc", "src/NativeMixerMeterDriver.h",
                    "src/PluginWorker.cpp", "src/PluginHost.cpp", "src/PluginHost.h", "src/PluginNativeMeter.h", "src/PluginProtocol.h",
                    "src/PluginParameterState.h", "src/NativeScalarHistory.h", "src/CommandsNativeScalarHistory.inc",
                    "src/StudioPluginParameters.cpp", "src/StudioNativeScalar.inc", "src/Commands.cpp", "src/Commands.h", "src/Main.cpp",
                    "src/Studio.cpp", "src/StudioController.cpp", "src/Project.cpp", "src/Project.h",
                    "src/MidiNoteOwners.h", "src/MidiInput.h", "src/MidiInput.cpp", "src/LiveMidi.h", "src/Sampler.h",
                    "src/PreparedTimeline.h", "src/PluginAutomationStream.h", "src/RemoteEffect.h",
                    "src/AudioEngine.cpp", "src/AudioEngine.h", "src/StudioMcp.cpp", "src/Studio.h", "src/NativeProcessor.h",
                    "src/PluginWrapperView.h", "src/PluginWrapperProtocol.h", "assets/synthesizer/library.json"]
    juce = "vendor/source/JUCE-37c894f83d379179b2070d437ccd0f1cd9af9576/modules/"
    source_names.extend(juce + name for name in (
        "juce_audio_utils/gui/juce_MidiKeyboardComponent.cpp",
        "juce_gui_basics/filebrowser/juce_FileChooser.cpp",
        "juce_gui_basics/native/juce_FileChooser_windows.cpp",
        "juce_gui_basics/native/juce_Windowing_windows.cpp",
        "juce_gui_basics/windows/juce_DocumentWindow.cpp",
        "juce_gui_basics/windows/juce_ResizableWindow.cpp"))
    report["sourceHashMode"] = "text-lf-v1"
    report["sourceInputs"] = {name: content_identity(ROOT / name)["contentSha256"] for name in source_names}
    receipt = output / "verification.json"
    receipt.write_text(json.dumps(report, indent=2), encoding="utf-8")
    proc = None
    worker_handle = None
    windows = None
    errors, replies = [], queue.Queue()
    serial = 0
    input_armed = False
    initial_foreground = None
    owned_keys = set()
    left_down = False
    expected_cursor = original_cursor = None
    native = instance = worker_pid = worker_identity = None
    active_dialog = None
    workflow_passed = False
    previous_stage = began

    def remaining():
        seconds = deadline - time.monotonic()
        if seconds <= 0:
            raise TimeoutError("Owned native workflow budget exhausted; stopping before fixture capacity")
        return seconds

    def check(condition, description):
        if not condition:
            raise AssertionError(description)
        report["checks"].append(description)

    def stage(name):
        nonlocal previous_stage
        now = time.monotonic()
        report["stageTimings"].append(dict(stage=name, seconds=now - previous_stage, totalSeconds=now - began))
        previous_stage = now

    def request(method, params=None):
        nonlocal serial
        remaining()
        serial += 1
        proc.stdin.write(json.dumps(dict(jsonrpc="2.0", id=serial, method=method, params=params or {})) + "\n")
        proc.stdin.flush()
        response = replies.get(timeout=min(15, remaining()))
        if isinstance(response, BaseException):
            raise response
        if response.get("id") != serial or "error" in response:
            raise RuntimeError(str(response))
        return response["result"]

    def call(tool, args=None, failure=False):
        result = request("tools/call", dict(name=tool, arguments=args or {}))
        with (output / "calls.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(dict(tool=tool, arguments=args, result=result)) + "\n")
        if result.get("isError") and not failure:
            raise RuntimeError(str(result.get("structuredContent", result)))
        return result.get("structuredContent", result)

    def poll(predicate, label, seconds=8):
        end = time.monotonic() + min(seconds, remaining())
        value = None
        while time.monotonic() < end:
            value = predicate()
            if value:
                report["checks"].append(label)
                return value
            time.sleep(.04)
        raise TimeoutError(label + ": " + str(value))

    def job(admission):
        result = admission
        end = time.monotonic() + min(15, remaining())
        while result.get("completionState") == "pending":
            if time.monotonic() >= end:
                raise TimeoutError("Native plugin job still pending: " + str(result))
            time.sleep(.03)
            result = call("vibera_plugin", dict(action="job", operation=admission["operation"]))
        check(result.get("completionState") == "completed", "actual plugin job terminal completion")
        report["jobs"].append(result)
        return result

    def ready_plugin():
        rows = call("vibera_plugin", dict(action="list"))["items"]
        return next((row for row in rows if row["id"] == plugin and row["available"]), None)

    def settled_plugin():
        state = call("vibera_state")
        return ready_plugin() if state.get("pendingAudioUpdate") is False else None

    def parameter_page():
        row = poll(settled_plugin, "Exact Synth worker and application audio preparation")
        job(call("vibera_plugin", dict(action="refresh", plugin=plugin, instance=row["instance"])))
        return row, call("vibera_synth", dict(action="parameters", plugin=plugin, instance=row["instance"]))

    def telemetry(voices=None, expected=None):
        expected = patch if expected is None else expected
        observed = call("vibera_synth", dict(action="status", plugin=plugin)).get("synthesizer", {})
        if not (observed.get("available") is True and observed.get("presetId") == expected["id"] and
                observed.get("sourceKind") == expected["sourceKind"] and observed.get("contentReady") is True and
                observed.get("sampleAvailable") is bool(expected["soundSource"]) and
                observed.get("parameterError") == "" and type(observed.get("activeVoices")) is int):
            return None
        if voices is not None and (observed["activeVoices"] > 0) != voices:
            return None
        if voices:
            runtime = ready_plugin()
            if not runtime:
                return None
            meter = runtime.get("nativeOutputMeter", {})
            peaks = [meter.get("leftPeak"), meter.get("rightPeak")]
            if not (meter.get("status") == "ready" and all(type(value) in (float, int) and math.isfinite(value) for value in peaks) and
                    max(peaks) > 1e-6):
                return None
            observed["nativeOutputMeter"] = meter
        check(0 <= observed["activeVoices"] <= 64, "finite bounded actual native voice telemetry")
        report["telemetry"].append(observed)
        return observed

    def playing_patch(next_patch):
        before_state = call("vibera_state")
        check(before_state["playing"], "actual device is playing before switch to " + next_patch["id"])
        row, page = parameter_page()
        terminal = job(call("vibera_synth", dict(action="loadPreset", plugin=plugin, instance=row["instance"],
                                                 generation=page["generation"], preset=next_patch["id"])))
        check(terminal.get("historyCommitted") is True, "playing full-patch switch commits captured history")
        _, actual_page = parameter_page()
        actual = {item["id"]: item["normalized"] for item in actual_page["items"]}
        expected = {}
        for spec, value in zip(discovery["parameters"], next_patch["settings"]):
            fraction = (value - spec["minimum"]) / (spec["maximum"] - spec["minimum"])
            expected[spec["id"]] = min(1., max(0., fraction)) ** spec["skew"]
        check(len(expected) == 46 and actual.keys() == expected.keys() and
              all(abs(actual[key] - value) < 2e-5 for key, value in expected.items()),
              "playing switch restores all46 actual scalars of " + next_patch["id"])
        after_state = call("vibera_state")
        check(after_state["playing"] and not after_state["pendingAudioUpdate"] and
              after_state["revision"] == before_state["revision"] + 1,
              "playing switch recovers actual transport with one exact history revision")
        poll(lambda: telemetry(False, next_patch), "Playing source change reaches actual callback: " + next_patch["id"])
        call("vibera_synth", dict(action="previewOn", plugin=plugin, note=60, velocity=90))
        try:
            sounding = poll(lambda: telemetry(True, next_patch), "Playing switch fresh voice produces actual native PCM")
        finally:
            call("vibera_synth", dict(action="previewOff", plugin=plugin, note=60))
            call("vibera_synth", dict(action="panic", plugin=plugin))
        released = poll(lambda: telemetry(False, next_patch), "Playing switched-source preview actually releases")
        report.setdefault("livePresetSwitches", []).append(dict(
            presetId=next_patch["id"], sourceKind=next_patch["sourceKind"], terminal=terminal,
            revisionBefore=before_state["revision"], revisionAfter=after_state["revision"],
            sounding=sounding, released=released,
            scope="Actual48k/256 callback, full scalar/history ACK and fresh-note output peak recovery; no seamless held-note claim"))
        stage("actual playing switch and voice/PCM recovery: " + next_patch["id"])

    def exact_worker():
        remaining()
        state = call("vibera_plugin", dict(action="editorState", plugin=plugin, instance=instance))["nativeEditor"]
        if (state["instance"] != instance or state.get("nativeProcessId") != worker_pid or
                not state["exists"] or not state["visible"] or
                process_identity(worker_pid) != worker_identity or
                windows.k.WaitForSingleObject(worker_handle, 0) != 258 or
                windows.image_path(worker_handle) != app.with_name("VibeRa Plugin Worker.exe") or
                not windows.u.IsWindow(native) or windows.pid(native) != worker_pid):
            raise RuntimeError("Exact native worker/window/creation/instance ownership changed; input paused")

    def quiet(require_foreground=True):
        remaining()
        if proc.poll() is not None:
            raise RuntimeError("Owned Studio exited")
        if expected_cursor is not None:
            actual_cursor = windows.cursor()
            if actual_cursor != expected_cursor:
                report["cursorMismatch"] = dict(expected=list(expected_cursor), actual=list(actual_cursor))
                raise RuntimeError("User cursor movement observed; native input paused")
        keys = (1, 2, 4, 8, 9, 13, 16, 17, 18, 27, 91, 92, *range(32, 91))
        if any(windows.u.GetAsyncKeyState(key) & 0x8000 for key in keys
               if key not in owned_keys and not (key == 1 and left_down)):
            raise RuntimeError("User control held; native input paused")
        foreground = windows.u.GetForegroundWindow()
        foreground_pid = windows.pid(foreground)
        expected_window = native
        if active_dialog is not None:
            expected_window = active_dialog["hwnd"]
            validate_dialog(expected_window, active_dialog["title"])
        if (require_foreground or input_armed) and (foreground_pid != worker_pid or
                                                   windows.u.GetAncestor(foreground, 2) != expected_window):
            raise NativeForegroundLost(windows.u.GetAncestor(foreground, 2), foreground_pid)
        if not input_armed and not require_foreground and foreground != initial_foreground and windows.pid(foreground) not in (proc.pid, worker_pid):
            raise RuntimeError("Foreground changed before activation; native input paused")

    def activate():
        nonlocal input_armed
        quiet(False)
        exact_worker()
        windows.u.SetForegroundWindow(native)
        time.sleep(.15)
        if windows.u.GetAncestor(windows.u.GetForegroundWindow(), 2) != native:
            # Ordinary owned caption activation only; never TOPMOST or another app.
            point = w.POINT()
            windows.u.ClientToScreen(native, c.byref(point))
            point.x += 200
            point.y += round(18 * windows.u.GetDpiForWindow(native) / 96)
            if windows.pid(windows.u.WindowFromPoint(point)) != worker_pid:
                raise RuntimeError("Owned caption obscured; activation paused")
            mouse_at((point.x, point.y), require_foreground=False)
        quiet()
        input_armed = True

    def geometry():
        rect, origin = w.RECT(), w.POINT()
        if not windows.u.GetClientRect(native, c.byref(rect)) or not windows.u.ClientToScreen(native, c.byref(origin)):
            raise RuntimeError("Owned native geometry unavailable")
        scale = windows.u.GetDpiForWindow(native) / 96
        # Pinned resizable DocumentWindow:4px border,30px custom title;
        # PluginWrapperView toolbar44/footer40. These are logical coordinates.
        return dict(x=origin.x, y=origin.y, scale=scale, width=rect.right, height=rect.bottom,
                    synthWidth=rect.right / scale - 8, synthHeight=rect.bottom / scale - 122)

    def point_in_synth(x, y):
        area = geometry()
        return round(area["x"] + (4 + x) * area["scale"]), round(area["y"] + (78 + y) * area["scale"])

    def mouse_at(point, require_foreground=True, hold=False):
        nonlocal expected_cursor, left_down
        quiet(require_foreground)
        exact_worker()
        physical = w.POINT(round(point[0]), round(point[1]))
        target = windows.u.WindowFromPoint(physical)
        root_window = native if active_dialog is None else active_dialog["hwnd"]
        if windows.pid(target) != worker_pid or windows.u.GetAncestor(target, 2) != root_window:
            raise RuntimeError("Native input point obscured or outside exact worker")
        if not windows.u.SetCursorPos(physical.x, physical.y):
            raise RuntimeError("Owned cursor placement failed")
        expected_cursor = physical.x, physical.y
        time.sleep(.04)
        quiet(require_foreground)
        windows.u.mouse_event(2, 0, 0, 0, 0)
        left_down = True
        report["inputTestStarted"] = True
        report["nativeInput"] = True
        if not hold:
            time.sleep(.04)
            release_mouse()

    def release_mouse():
        nonlocal left_down
        if left_down:
            # Only the matching release of this driver's tracked injected press.
            windows.u.mouse_event(4, 0, 0, 0, 0)
            left_down = False

    def tap(key):
        quiet()
        exact_worker()
        if active_dialog is not None and key != 27:
            if active_dialog.get("mode") != "filename" or key not in (13, 65):
                raise RuntimeError("Only guarded filename entry/confirmation or Escape is permitted in this chooser")
            if windows.focus(active_dialog["hwnd"]) != active_dialog["edit"]:
                raise RuntimeError("Exact native filename field lost keyboard focus; input paused")
            if key == 65 and 17 not in owned_keys:
                raise RuntimeError("Only Ctrl+A selection is permitted in the native filename field")
            if key == 13 and windows.text(active_dialog["edit"]) != active_dialog["file"]:
                raise RuntimeError("Native filename must exactly match the one owned preset path before confirmation")
        owner = w.DWORD()
        target = native if active_dialog is None else active_dialog["hwnd"]
        thread = windows.u.GetWindowThreadProcessId(target, c.byref(owner))
        scan = windows.u.MapVirtualKeyExW(key, 0, windows.u.GetKeyboardLayout(thread))
        if owner.value != worker_pid or not 0 < scan < 256:
            raise RuntimeError("Exact worker keyboard mapping unavailable")
        try:
            windows.u.keybd_event(key, scan, 0, 0)
            owned_keys.add(key)
            time.sleep(.025)
        finally:
            windows.u.keybd_event(key, scan, 2, 0)
            owned_keys.discard(key)

    def computer_keyboard():
        check(active_dialog is None, "computer note input targets the editor only")
        before = call("vibera_state")["revision"]
        area = geometry()
        mouse_at(point_in_synth(120, area["synthHeight"] - 60))
        poll(lambda: telemetry(False), "Mouse focus click releases before computer-key note")
        sounding = []
        for attempt in range(2):
            quiet()
            exact_worker()
            owner = w.DWORD()
            thread = windows.u.GetWindowThreadProcessId(native, c.byref(owner))
            layout = windows.u.GetKeyboardLayout(thread)
            scan = windows.u.MapVirtualKeyExW(65, 0, layout)
            check(owner.value == worker_pid and 0 < scan < 256, "exact worker computer-key mapping")
            try:
                windows.u.keybd_event(65, scan, 0, 0)
                owned_keys.add(65)
                sounding.append(poll(lambda: telemetry(True), "Actual computer A key hold reaches Synth DSP"))
                time.sleep(.12)
                quiet()
                check(bool(telemetry(True)), "computer key remains sounding while OS key is held")
            finally:
                # Matching release of this driver's tracked press, including failure cleanup.
                windows.u.keybd_event(65, scan, 2, 0)
                owned_keys.discard(65)
            poll(lambda: telemetry(False), "Actual computer A key release reaches Synth DSP")
        check(call("vibera_state")["revision"] == before, "computer keyboard preview leaves score history unchanged")
        report["computerKeyboard"] = dict(virtualKey=65, keyboardLayout=int(layout), attempts=2, sounding=sounding,
                                           scope="Owned OS synthetic key hold/release and same-key re-press, actual virtual DSP; no human/physical MIDI claim")
        stage("actual computer-key hold/release and same-key re-press")

    def dialog_observation(hwnd, title, phase):
        foreground = windows.u.GetForegroundWindow()
        owner = windows.pid(hwnd) if hwnd else 0
        exists = bool(hwnd and windows.u.IsWindow(hwnd))
        caption, kind = c.create_unicode_buffer(512), c.create_unicode_buffer(64)
        # Read captions/classes only from the exact worker; never inspect another app's text.
        if exists and owner == worker_pid:
            windows.u.GetWindowTextW(hwnd, caption, len(caption))
            windows.u.GetClassNameW(hwnd, kind, len(kind))
        rect = w.RECT()
        bounds = ([rect.left, rect.top, rect.right, rect.bottom]
                  if exists and owner == worker_pid and windows.u.GetWindowRect(hwnd, c.byref(rect)) else None)
        state = dict(hwnd=int(hwnd or 0), pid=owner, exists=exists,
                     visible=bool(exists and windows.u.IsWindowVisible(hwnd)),
                     ancestor=int(windows.u.GetAncestor(hwnd, 2) or 0) if exists else 0,
                     title=caption.value if owner == worker_pid else None,
                     nativeClass=kind.value if owner == worker_pid else None,
                     bounds=bounds,
                     foreground=int(foreground or 0), foregroundPid=windows.pid(foreground) if foreground else 0,
                     expectedTitle=title, phase=phase)
        observations = report.setdefault("dialogObservations", [])
        if not observations or {key: value for key, value in observations[-1].items() if key != "seconds"} != state:
            if len(observations) < 512:
                observations.append(dict(**state, seconds=time.monotonic() - began))
            else:
                report["dialogObservationsTruncated"] = True
        return state

    def validate_dialog(hwnd, title, allow_unshown=False, phase="before guarded input"):
        # Only one explicitly observed native chooser may temporarily receive input.
        # A shell broker or any other process/window is never adopted.
        state = dialog_observation(hwnd, title, phase)
        if (not hwnd or hwnd == native or not state["exists"] or state["pid"] != worker_pid or
                state["ancestor"] != hwnd):
            raise RuntimeError("Exact worker-owned native chooser unavailable; input paused")
        if (state["nativeClass"] != "#32770" or
                state["title"] != title and not (allow_unshown and not state["visible"] and state["title"] == "")):
            raise RuntimeError("Native chooser title/class changed; input paused")
        if not state["visible"]:
            if allow_unshown:
                return False  # Read-only startup/closing observation. This HWND receives no input.
            raise RuntimeError("Exact worker-owned native chooser is not visible; input paused")
        return True

    def open_chooser(action, x):
        nonlocal active_dialog
        title = ("Save" if action == "save" else "Load") + " synthesizer preset"
        previous_windows = {hwnd for hwnd, unused in windows.windows(worker_pid)}
        mouse_at(point_in_synth(x, 66))

        def opened():
            nonlocal active_dialog
            exact_worker()
            foreground = windows.u.GetForegroundWindow()
            target = windows.u.GetAncestor(foreground, 2)
            dialog_observation(target, title, "chooser startup foreground")
            if target == native:
                quiet()
                return None
            if windows.pid(foreground) != worker_pid or target in previous_windows:
                raise RuntimeError("Chooser foreground is not a new exact worker-owned window; input paused")
            if not validate_dialog(target, title, allow_unshown=True, phase="chooser startup readiness"):
                return None
            active_dialog = dict(hwnd=target, title=title, mode="cancel")
            quiet()
            return target

        return poll(opened, "Actual native " + action + " chooser opened under exact worker", 5)

    def chooser_returned(dialogs):
        nonlocal active_dialog

        def temporary_parent(state, modals_closed):
            editor_class = c.create_unicode_buffer(64)
            windows.u.GetClassNameW(native, editor_class, len(editor_class))
            bounds = state["bounds"]
            return (modals_closed and state["hwnd"] != native and state["exists"] and
                    state["pid"] == worker_pid and state["foregroundPid"] == worker_pid and
                    state["ancestor"] == state["hwnd"] and state["title"] == "" and
                    state["nativeClass"] == editor_class.value and editor_class.value.startswith("JUCE_") and
                    bounds is not None and (bounds[0] == bounds[2] or bounds[1] == bounds[3]))

        def allowed_read_only_foreground(target, observed_pid, modals_closed, phase):
            state = dialog_observation(target, "original native editor", phase)
            if observed_pid != worker_pid:
                return False
            if target == native:
                return state["exists"] and state["pid"] == worker_pid and state["ancestor"] == native
            if target in dialogs:
                if not state["exists"]:
                    return True  # Previously identified chooser finished destruction after the sampled refusal.
                validate_dialog(target, dialogs[target], allow_unshown=True, phase=phase + " known closing dialog")
                return True
            if temporary_parent(state, modals_closed):
                return True
            if not state["exists"] and modals_closed:
                return any(old["hwnd"] == int(target or 0) and temporary_parent(old, True)
                           for old in report.get("dialogObservations", []))
            return False

        def returned():
            nonlocal active_dialog
            exact_worker()
            foreground = windows.u.GetForegroundWindow()
            target = windows.u.GetAncestor(foreground, 2)
            modals_closed = all(not windows.u.IsWindow(hwnd) or not windows.u.IsWindowVisible(hwnd) for hwnd in dialogs)
            if target == native and modals_closed:
                active_dialog = None
                try:
                    quiet()
                except NativeForegroundLost as refusal:
                    # Only this read-only completion predicate may resample a
                    # foreground-only refusal. Cursor/key/identity failures still
                    # propagate, and every actual input keeps strict quiet().
                    exact_worker()
                    closed_now = all(not windows.u.IsWindow(hwnd) or not windows.u.IsWindowVisible(hwnd) for hwnd in dialogs)
                    if not allowed_read_only_foreground(refusal.hwnd, refusal.pid, closed_now,
                                                        "chooser readiness sampled foreground refusal"):
                        raise
                    current = windows.u.GetForegroundWindow()
                    current_target = windows.u.GetAncestor(current, 2)
                    current_pid = windows.pid(current)
                    if not allowed_read_only_foreground(current_target, current_pid, closed_now,
                                                        "chooser readiness foreground resample"):
                        raise
                    report.setdefault("chooserCompletionForegroundRaces", []).append(dict(
                        refusedHwnd=int(refusal.hwnd or 0), refusedPid=refusal.pid,
                        resampledHwnd=int(current_target or 0), resampledPid=current_pid,
                        seconds=time.monotonic() - began, inputPermitted=False))
                    return None
                return True
            if target not in dialogs or windows.pid(foreground) != worker_pid:
                state = dialog_observation(target, "original native editor", "chooser completion foreground")
                # Pinned FileChooser::Native creates a zero-sized unnamed JUCE
                # desktop Component. Its async exitModalState/pimpl teardown can
                # briefly foreground that parent after the real dialog closes.
                # Observe it only; never whitelist/activate it or send it input.
                if temporary_parent(state, modals_closed):
                    report["observedReadOnlyChooserParentTeardown"] = True
                    return None
                raise RuntimeError("Native chooser lost exact foreground during completion; input paused")
            if not validate_dialog(target, dialogs[target], allow_unshown=True, phase="chooser closing transition"):
                return None
            active_dialog = dict(hwnd=target, title=dialogs[target], mode="closing")
            quiet()
            return None
        poll(returned, "Owned native chooser closed and editor foreground restored", 5)
        time.sleep(.25)  # Empty-result or successful-result callback and editor timer.
        quiet()
        exact_worker()

    def enter_preset_path(file):
        # Legacy SDK edt1=0x0480 or the exact modern filename ancestry retained
        # in the native receipt. Search/address edits must never become candidates.
        check(active_dialog is not None, "exact native chooser is active for owned path entry")
        check(file.parent == output / "user" and file.suffix == ".vrsynth" and len(file.stem) <= 100,
              "native filename is the one explicit owned user preset")
        hwnd = active_dialog["hwnd"]

        def discover_field():
            quiet()
            exact_worker()
            tree = windows.children(hwnd)
            edits, inspected = [], []
            for child in tree:
                if child["kind"] != "Edit":
                    continue
                ancestor = child["hwnd"]
                ancestry = []
                while ancestor and ancestor != hwnd:
                    if (len(ancestry) >= 32 or windows.pid(ancestor) != worker_pid or
                            windows.u.GetAncestor(ancestor, 2) != hwnd):
                        raise RuntimeError("Native filename ancestry escaped the exact worker-owned chooser")
                    kind = c.create_unicode_buffer(64)
                    windows.u.GetClassNameW(ancestor, kind, len(kind))
                    ancestry.append(dict(hwnd=int(ancestor), kind=kind.value, controlId=windows.u.GetDlgCtrlID(ancestor)))
                    ancestor = windows.u.GetParent(ancestor)
                selected_kind = filename_control_kind(ancestry, active_dialog["title"]) if ancestor == hwnd else None
                inspected.append(dict(ancestry=ancestry, selectedKind=selected_kind))
                if selected_kind is not None:
                    edits.append(child["hwnd"])
            observation = dict(dialog=int(hwnd), candidates=[int(edit) for edit in edits], inspectedEdits=inspected,
                               controls=[dict(kind=child["kind"], controlId=child["controlId"]) for child in tree])
            observations = report.setdefault("nativeFilenameFields", [])
            if not observations or observations[-1] != observation:
                observations.append(observation)
            if len(edits) > 1:
                raise RuntimeError("Ambiguous native filename fields; input paused")
            return edits[0] if edits else None

        edit = poll(discover_field, "unique actual native filename Edit under validated dialog-specific ancestry", 5)
        active_dialog.update(mode="filename", edit=edit, file=str(file))
        rect = w.RECT()
        check(bool(windows.u.GetWindowRect(edit, c.byref(rect))) and rect.right > rect.left and rect.bottom > rect.top,
              "actual filename field has visible bounds")
        mouse_at(((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2))
        check(windows.focus(hwnd) == edit, "native filename Edit owns real keyboard focus")
        select_all()
        type_text(str(file))
        check(windows.text(edit) == str(file), "actual native filename readback exactly matches owned preset path")
        tap(13)

    def native_preset_file(action, file, overwrite=False):
        nonlocal active_dialog
        if action == "save":
            check(file.exists() is overwrite, "native Save may overwrite only the exact existing owned preset")
        else:
            check(file.is_file(), "native Load targets the exact previously written owned preset")
        hwnd = open_chooser(action, 380 if action == "save" else 510)
        title = active_dialog["title"]
        previous_windows = {window for window, unused in windows.windows(worker_pid)}
        enter_preset_path(file)
        dialogs = {hwnd: title}
        if overwrite:
            modern_snapshot = None

            def query_modern_prompt(target):
                helper = output / "read-only-overwrite-uia.ps1"
                helper.write_text(OVERWRITE_UIA_QUERY, encoding="utf-8")
                powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
                command = [str(powershell), "-NoLogo", "-NoProfile", "-NonInteractive", "-Mta", "-ExecutionPolicy", "Bypass",
                           "-File", str(helper), "-DialogHandle", str(target), "-ExpectedProcessId", str(worker_pid)]
                started = time.monotonic()
                # Read-only, exact-HWND subtree query. subprocess.run retains and,
                # on timeout, terminates/waits only its own helper process handle.
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                        timeout=min(3, remaining()), creationflags=subprocess.CREATE_NO_WINDOW)
                observation = dict(command=command, exitCode=result.returncode, seconds=time.monotonic() - started,
                                   scriptSha256=hashlib.sha256(helper.read_bytes()).hexdigest(), stderr=result.stderr)
                report.setdefault("readOnlyOverwriteUiaQueries", []).append(observation)
                exact_worker()
                validate_dialog(target, "Confirm Save As", phase="after exact-HWND read-only UIA query")
                if result.returncode != 0:
                    raise RuntimeError("Exact overwrite UI Automation read failed: " + result.stderr)
                snapshot = json.loads(result.stdout.lstrip("\ufeff"))
                observation["snapshot"] = snapshot
                return snapshot

            def overwrite_prompt():
                nonlocal active_dialog, modern_snapshot
                exact_worker()
                foreground = windows.u.GetForegroundWindow()
                target = windows.u.GetAncestor(foreground, 2)
                dialog_observation(target, "Confirm Save As", "overwrite startup foreground")
                if target == hwnd:
                    if not validate_dialog(hwnd, title, allow_unshown=True, phase="overwrite parent transition"):
                        return None
                    quiet()
                    return None
                if windows.pid(foreground) != worker_pid or target in previous_windows:
                    raise RuntimeError("Overwrite confirmation is not a new exact worker-owned dialog; input paused")
                if not validate_dialog(target, "Confirm Save As", allow_unshown=True, phase="overwrite startup readiness"):
                    return None
                children = windows.children(target, ready_only=False)
                for child in children:
                    if child["kind"] == "Button" and child["visible"]:
                        child["nativeName"] = windows.text(child["hwnd"])[:128]
                matches_file = any(file.name in windows.text(child["hwnd"]) for child in children
                                   if child["kind"] == "Static" and child["visible"])
                observation = dict(dialog=int(target), pid=worker_pid, controls=children,
                                   questionNamesOwnedFile=matches_file)
                observations = report.setdefault("nativeOverwritePromptObservations", [])
                if not observations or {key: value for key, value in observations[-1].items() if key != "seconds"} != observation:
                    observations.append(dict(**observation, seconds=time.monotonic() - began))
                candidates = {identity: [child["hwnd"] for child in children
                                         if child["kind"] == "Button" and child["controlId"] == identity and
                                         child["visible"] and child["enabled"] and
                                         windows.u.GetAncestor(child["hwnd"], 2) == target]
                              for identity in (6, 7)}
                if any(len(group) > 1 for group in candidates.values()):
                    raise RuntimeError("Ambiguous native overwrite confirmation controls; input paused")
                if any(not group for group in candidates.values()) or not matches_file:
                    if not any(child["kind"] == "DirectUIHWND" and child["visible"] for child in children):
                        return None  # Same exact legacy prompt; read-only until controls are ready.
                    if modern_snapshot is None:
                        modern_snapshot = query_modern_prompt(target)
                    modern = overwrite_uia_controls(modern_snapshot, target, worker_pid, file.name)
                    if modern is None:
                        return None
                    mapped = {}
                    native_bounds = {}
                    prompt_rect = w.RECT()
                    check(bool(windows.u.GetWindowRect(target, c.byref(prompt_rect))), "exact overwrite prompt native bounds")
                    for label, identity in (("yes", 6), ("no", 7)):
                        element = modern[label]
                        bounds = element["bounds"]
                        matches = []
                        for child in children:
                            if not (child["kind"] == "Button" and windows.pid(child["hwnd"]) == worker_pid and
                                    windows.u.IsWindowVisible(child["hwnd"]) and windows.u.IsWindowEnabled(child["hwnd"]) and
                                    windows.u.GetAncestor(child["hwnd"], 2) == target):
                                continue
                            if element["type"] == 50033 and child.get("nativeName", "").replace("&", "").strip().casefold() != label:
                                continue
                            rect = w.RECT()
                            if (not windows.u.GetWindowRect(child["hwnd"], c.byref(rect)) or
                                    rect.right <= rect.left or rect.bottom <= rect.top or
                                    rect.left < prompt_rect.left or rect.top < prompt_rect.top or
                                    rect.right > prompt_rect.right or rect.bottom > prompt_rect.bottom):
                                continue
                            if element.get("hwnd"):
                                same = element["hwnd"] == child["hwnd"]
                            else:
                                same = all(abs(actual - expected) <= 2 for actual, expected in
                                           zip((rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top), bounds))
                            if same:
                                matches.append(child["hwnd"])
                                native_bounds[label] = [rect.left, rect.top, rect.right, rect.bottom]
                        if len(matches) != 1:
                            raise RuntimeError("UI Automation overwrite button has no unique exact owned native HWND")
                        mapped[identity] = matches
                    if mapped[6] == mapped[7]:
                        raise RuntimeError("UI Automation Yes/No resolved to the same HWND")
                    candidates, matches_file = mapped, True
                    report["modernOverwriteAccessibleSelection"] = modern
                    report["modernOverwriteNativeButtonBounds"] = native_bounds
                    report["modernOverwriteCoordinateScope"] = "Input uses exact native HWND physical bounds; UIA provider bounds are retained separately."
                check(all(len(group) == 1 for group in candidates.values()),
                      "native overwrite prompt has unique visible enabled semantic Yes/No controls")
                check(matches_file, "native overwrite question names the exact owned preset")
                active_dialog = dict(hwnd=target, title="Confirm Save As", mode="overwrite")
                quiet()
                return target, candidates[6][0]
            prompt, yes = poll(overwrite_prompt, "Actual native owned-file overwrite confirmation", 5)
            dialogs[prompt] = "Confirm Save As"
            rect = w.RECT()
            check(bool(windows.u.GetWindowRect(yes, c.byref(rect))), "actual owned IDYES bounds")
            mouse_at(((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2))
        chooser_returned(dialogs)
        return dict(action=action, file=str(file), dialog=int(hwnd), explicitNativeOverwrite=overwrite)

    def scalar_values():
        _, page = parameter_page()
        values = {item["id"]: item["normalized"] for item in page["items"]}
        check(len(values) == 46, "actual current snapshot contains all46 Synth scalars")
        return values

    def same_scalars(actual, expected, label):
        check(actual.keys() == expected.keys() and all(abs(actual[key] - value) < 2e-5 for key, value in expected.items()), label)

    def user_metadata(file, expected_id=None):
        listed = call("vibera_synth", dict(action="listUserPresets"))
        check(not listed["errors"] and len(listed["presets"]) == 1, "shared strict user codec lists one valid native-saved file")
        row = listed["presets"][0]
        check(Path(row["path"]).resolve() == file and row["factory"] is False and row["schemaVersion"] == 1 and
              len(row["id"]) == 32 and all(char in "0123456789abcdef" for char in row["id"]) and
              row["basePreset"] == patch["id"] and row["source"] == patch["sourceKind"],
              "native-saved preset has exact managed path, stable UUID and truthful factory/source metadata")
        if expected_id is not None:
            check(row["id"] == expected_id, "UI/MCP overwrite preserves the same stable user preset UUID")
        return row

    def set_tone(value):
        row, page = parameter_page()
        result = job(call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"],
                                              generation=page["generation"], key="tone", value=value)))
        check(result.get("historyCommitted") is True, "owned user-preset fixture Tone mutation captures canonical history")
        return scalar_values()

    def native_user_roundtrip():
        nonlocal native
        before_files = user_files()
        file = output / "user/Native Desktop Sound.vrsynth"
        check(not file.exists(), "native Save starts with an absent owned preset file")
        original = scalar_values()
        original_revision = call("vibera_state")["revision"]
        saved = native_preset_file("save", file)
        poll(lambda: file.is_file(), "Actual native Save creates the managed .vrsynth file")
        first = user_metadata(file)
        first_digest = hashlib.sha256(file.read_bytes()).hexdigest()
        same_scalars(scalar_values(), original, "native Save leaves all46 actual scalars unchanged")
        check(call("vibera_state")["revision"] == original_revision, "successful native Save does not create musical history")
        after_save = user_files()
        check(set(after_save) == set(before_files) | {file.name} and
              all(after_save[name] == digest for name, digest in before_files.items()),
              "native Save writes only its exact managed user preset")
        tone = next(spec for spec in discovery["parameters"] if spec["key"] == "tone")
        target = .18 if original[tone["id"]] > .5 else .82
        changed = set_tone(target)
        row, page = parameter_page()
        refused_revision = call("vibera_state")["revision"]
        refused = call("vibera_synth", dict(action="saveUserPreset", plugin=plugin, instance=row["instance"],
                                           name=file.stem), failure=True)
        check(refused.get("error", {}).get("code") == "synth.overwriteRequired" and
              hashlib.sha256(file.read_bytes()).hexdigest() == first_digest and
              call("vibera_state")["revision"] == refused_revision,
              "MCP refuses overwriting the native-saved file without explicit permission and preserves bytes/history")
        same_scalars(scalar_values(), changed, "overwrite refusal retains every actual scalar")
        overwritten = job(call("vibera_synth", dict(action="saveUserPreset", plugin=plugin, instance=row["instance"],
                                                   name=file.stem, overwrite=True)))
        second = user_metadata(file, first["id"])
        second_digest = hashlib.sha256(file.read_bytes()).hexdigest()
        check(second_digest != first_digest, "explicit MCP overwrite changes actual native-saved sound bytes")
        final_values = set_tone(.41)
        native_overwrite_revision = call("vibera_state")["revision"]
        native_overwritten = native_preset_file("save", file, overwrite=True)
        third = user_metadata(file, first["id"])
        third_digest = hashlib.sha256(file.read_bytes()).hexdigest()
        check(third_digest != second_digest and call("vibera_state")["revision"] == native_overwrite_revision,
              "actual native overwrite changes only owned file bytes and preserves musical revision")
        same_scalars(scalar_values(), final_values, "native overwrite leaves the complete exposed sound unchanged")
        before_load = set_tone(target)
        before_load_revision = call("vibera_state")["revision"]
        loaded = native_preset_file("load", file)
        def load_committed():
            state = call("vibera_state")
            return state if state["revision"] > before_load_revision and not state["nativeScalarPending"] else None
        load_state = poll(load_committed, "Actual native Load commits captured host scalar history")
        check(load_state["revision"] == before_load_revision + 1, "native user Load creates one exact history revision")
        same_scalars(scalar_values(), final_values, "actual native Load restores every one of the46 saved scalars")
        check(hashlib.sha256(file.read_bytes()).hexdigest() == third_digest, "native user Load preserves preset bytes")
        capture("synth-native-user-loaded.png")
        job(call("vibera_synth", dict(action="closeEditor", plugin=plugin, instance=instance)))
        native = None  # History may replace a worker. No further native input is permitted.
        for action, expected in (("edit.undo", before_load), ("edit.redo", final_values)):
            admission = call("vibera_command", dict(command=action))
            commands.await_command_result(admission, call, lambda observation: report.setdefault("commandObservations", []).append(observation))
            same_scalars(scalar_values(), expected, "shared " + action + " restores complete native user Load sound")
        set_tone(target)
        row, page = parameter_page()
        stable_load = job(call("vibera_synth", dict(action="loadUserPreset", plugin=plugin, instance=row["instance"],
                                                   generation=page["generation"], preset=first["id"])))
        check(stable_load.get("historyCommitted") is True, "MCP stable-ID load of native-saved file commits captured history")
        same_scalars(scalar_values(), final_values, "MCP stable-ID and native chooser Load restore the same complete sound")
        final_files = user_files()
        check(set(final_files) == set(before_files) | {file.name} and
              all(final_files[name] == digest for name, digest in before_files.items()) and
              final_files[file.name] == third_digest,
              "complete native/MCP user-preset workflow changes only its exact owned preset file")
        report["nativeUserPreset"] = dict(saved=saved, explicitMcpOverwrite=overwritten, nativeOverwrite=native_overwritten,
                                          loaded=loaded, stableMcpLoad=stable_load, metadata=[first, second, third],
                                          fileDigests=[first_digest, second_digest, third_digest], scalarCount=46)
        stage("actual native Save/overwrite/Load, shared codec and complete history/MCP roundtrip")

    def user_files():
        directory = output / "user"
        metadata = directory.lstat()
        if (not stat.S_ISDIR(metadata.st_mode) or directory.is_symlink() or
                getattr(metadata, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise RuntimeError("Exact owned user directory unavailable")

        def unreadable(error):
            raise error

        result, total_bytes, entries = {}, 0, 0
        for base, directories, files in os.walk(directory, onerror=unreadable, followlinks=False):
            for name in directories + files:
                path = Path(base) / name
                metadata = path.lstat()
                entries += 1
                if (entries > 1024 or path.is_symlink() or
                        getattr(metadata, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
                    raise RuntimeError("Bounded owned user-file inspection refused unexpected content")
                if name in files:
                    total_bytes += metadata.st_size
                    if not stat.S_ISREG(metadata.st_mode) or total_bytes > 8 * 1024 * 1024:
                        raise RuntimeError("Bounded owned user-file inspection refused unexpected size/type")
                    result[path.relative_to(directory).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        return result

    def cancel_chooser(action, x):
        observation = dict(action=action, status="pending", dialogOpened=False, cancelled=False)
        report.setdefault("chooserCases", []).append(observation)
        if remaining() < 8:
            observation["status"] = "not_run_budget"
            raise TimeoutError("Insufficient native workflow budget for " + action + " chooser cancellation")
        title = ("Save" if action == "save" else "Load") + " synthesizer preset"
        before_revision = call("vibera_state")["revision"]
        before_files = user_files()
        hwnd = open_chooser(action, x)
        observation.update(dialogOpened=True, hwnd=int(hwnd), title=title, nativeClass="#32770", workerPid=worker_pid,
                           workerCreationIdentity=worker_identity, instance=instance)
        tap(27)  # Escape only: no file name, path, Save/Load confirmation or other process input.
        chooser_returned({hwnd: title})
        after_revision = call("vibera_state")["revision"]
        after_files = user_files()
        observed = call("vibera_synth", dict(action="status", plugin=plugin)).get("synthesizer", {})
        check(after_revision == before_revision, action + " chooser cancellation preserves exact project revision")
        check(after_files == before_files, action + " chooser cancellation preserves all managed user-file bytes")
        check(observed.get("available") is True and observed.get("contentReady") is True and
              observed.get("parameterError") == "", action + " chooser cancellation leaves actual processor ready without error")
        image_name = "synth-native-" + action + "-cancelled.png"
        capture(image_name)
        from PIL import Image
        area = geometry()
        scale = area["scale"]
        # SynthEditor::layout places the local-error status Label in the final26px
        # of its10px-inset footer; its explicit error colour is0xffffac98.
        status_bounds = tuple(round(value * scale) for value in
                              (14, 78 + area["synthHeight"] - 36,
                               4 + area["synthWidth"] - 10, 78 + area["synthHeight"] - 10))
        with Image.open(output / image_name) as image:
            pixels = image.crop(status_bounds).convert("RGB")
            error_pixels = sum(red >= 190 and red - green >= 35 and green - blue >= 8
                               for red, green, blue in pixels.getdata())
        check(error_pixels == 0, action + " cancellation has no editor status error-colour indicator")
        observation.update(status="passed", cancelled=True, revisionBefore=before_revision, revisionAfter=after_revision,
                           userFilesBefore=before_files, userFilesAfter=after_files, processorStatus=observed,
                           editorStatusErrorPixels=error_pixels, editorStatusPixelBounds=status_bounds,
                           localEditorErrorReadback="visual colour diagnostic; exact label text not exposed by MCP")
        stage("native " + action + " chooser open/cancel and unchanged state")

    def select_all():
        quiet()
        exact_worker()
        try:
            windows.u.keybd_event(17, 0x1d, 0, 0)
            owned_keys.add(17)
            tap(65)
        finally:
            windows.u.keybd_event(17, 0x1d, 2, 0)
            owned_keys.discard(17)

    def type_text(text):
        class KeyboardInput(c.Structure):
            _fields_ = [("vk", w.WORD), ("scan", w.WORD), ("flags", w.DWORD), ("time", w.DWORD), ("extra", c.c_size_t)]
        class MouseInput(c.Structure):
            _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("data", w.DWORD), ("flags", w.DWORD), ("time", w.DWORD), ("extra", c.c_size_t)]
        class Payload(c.Union):
            _fields_ = [("keyboard", KeyboardInput), ("mouse", MouseInput)]
        class Input(c.Structure):
            _anonymous_ = ("payload",)
            _fields_ = [("kind", w.DWORD), ("payload", Payload)]
        windows.u.SendInput.argtypes = [w.UINT, c.POINTER(Input), c.c_int]
        windows.u.SendInput.restype = w.UINT
        if active_dialog is not None and (active_dialog.get("mode") != "filename" or text != active_dialog.get("file")):
            raise RuntimeError("Only the exact explicitly owned preset path may be typed in a native chooser")
        for char in text:
            quiet()
            exact_worker()
            if active_dialog is not None and windows.focus(active_dialog["hwnd"]) != active_dialog["edit"]:
                raise RuntimeError("Exact native filename Edit lost focus during owned path entry")
            events = (Input * 2)()
            for event, flags in zip(events, (4, 6)):
                event.kind = 1
                event.keyboard = KeyboardInput(0, ord(char), flags, 0, 0)
            if windows.u.SendInput(2, events, c.sizeof(Input)) != 2:
                raise RuntimeError("Owned Unicode input was not accepted")
            time.sleep(.025)

    def capture(name):
        quiet()
        exact_worker()
        area = geometry()
        check(0 < area["width"] * area["height"] <= 35000000, "bounded owned capture dimensions")
        results = queue.Queue()
        hwnd = native
        def render():
            dc = memory = bitmap = previous = None
            try:
                from PIL import Image
                import struct
                windows.u.SetThreadDpiAwarenessContext(w.HANDLE(-4))
                dc = windows.u.GetDC(hwnd)
                memory = windows.g.CreateCompatibleDC(dc)
                bitmap = windows.g.CreateCompatibleBitmap(dc, area["width"], area["height"])
                previous = windows.g.SelectObject(memory, bitmap)
                if not dc or not memory or not bitmap or not previous or previous == c.c_void_p(-1).value:
                    raise RuntimeError("Owned capture GDI resources unavailable")
                if not windows.u.PrintWindow(hwnd, memory, 3):
                    raise RuntimeError("Owned PrintWindow failed")
                header = c.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, area["width"], -area["height"], 1, 32, 0,
                                                          area["width"] * area["height"] * 4, 0, 0, 0, 0))
                pixels = c.create_string_buffer(area["width"] * area["height"] * 4)
                if windows.g.GetDIBits(memory, bitmap, 0, area["height"], pixels, header, 0) != area["height"]:
                    raise RuntimeError("Owned capture pixels unavailable")
                image = Image.frombytes("RGB", (area["width"], area["height"]), pixels.raw, "raw", "BGRX")
                if not any(low != high for low, high in image.getextrema()):
                    raise RuntimeError("Owned capture is uniform; renderer evidence unavailable")
                image.save(output / name)
                results.put(True)
            except BaseException as error:
                results.put(error)
            finally:
                if previous:
                    windows.g.SelectObject(memory, previous)
                if bitmap:
                    windows.g.DeleteObject(bitmap)
                if memory:
                    windows.g.DeleteDC(memory)
                if dc:
                    windows.u.ReleaseDC(hwnd, dc)
        threading.Thread(target=render, daemon=True).start()
        result = results.get(timeout=min(3, remaining()))
        if isinstance(result, BaseException):
            raise result
        quiet()
        exact_worker()
        report["captures"].append(dict(path=name, dpi=round(area["scale"] * 96), geometry=area,
                                       sha256=hashlib.sha256((output / name).read_bytes()).hexdigest()))

    def place(monitor, size):
        quiet(False)
        exact_worker()
        dpi, work = monitor["dpi"], monitor["work"]
        width, height = round((size[0] + 8) * dpi / 96), round((size[1] + 122) * dpi / 96)
        check(width + 40 <= work[2] - work[0] and height + 40 <= work[3] - work[1], "native layout fits observed monitor work area")
        for attempt in range(2):
            if not windows.u.SetWindowPos(native, w.HWND(0), work[0] + 20, work[1] + 20, width, height, 0x10):
                raise RuntimeError("Owned ordinary worker positioning failed")
            poll(lambda: windows.u.GetDpiForWindow(native) == dpi, "Actual worker DPI adoption", 3)
            time.sleep(.12)
        actual = geometry()
        check(round(actual["scale"] * 96) == dpi and abs(actual["synthWidth"] - size[0]) < 2 and
              abs(actual["synthHeight"] - size[1]) < 2, "actual worker DPI and requested native content dimensions")
        activate()

    def default_editor_checks():
        nonlocal native, expected_cursor
        for monitor in targets:
            for name, size in (("normal", (1380, 920)), ("small", (760, 540))):
                place(monitor, size)
                capture("synth-native-" + str(monitor["dpi"]) + "dpi-" + name + ".png")
                if name == "small":
                    mouse_at(point_in_synth(120, size[1] - 60), hold=True)
                    try:
                        poll(lambda: telemetry(True), "Actual native keyboard note reaches virtual DSP")
                    finally:
                        release_mouse()
                    poll(lambda: telemetry(False), "Actual native keyboard mouse release reaches DSP")
                stage(str(monitor["dpi"]) + "dpi " + name + " capture" + (" and keyboard release" if name == "small" else ""))
        place(targets[0], (1380, 920))
        computer_keyboard()
        before_search = call("vibera_state")["revision"]
        mouse_at(point_in_synth(250, 132))
        select_all()
        type_text("warm")
        poll(lambda: call("vibera_synth", dict(action="browser"))["browser"]["search"] == "warm", "Native search reaches shared browser state")
        check(call("vibera_state")["revision"] == before_search, "native search does not create score history")
        capture("synth-native-search.png")
        call("vibera_synth", dict(action="filter", search=""))
        time.sleep(.3)
        row, page = parameter_page()
        tone_id = next(item["id"] for item in discovery["parameters"] if item["key"] == "tone")
        tone_before = next(item["normalized"] for item in page["items"] if item["id"] == tone_id)
        revision_before = call("vibera_state")["revision"]
        area = geometry()
        content_width = max(1040, round(area["synthWidth"]) - 20 - 14)
        browser_width = min(390, max(300, content_width // 3))
        cell = (content_width - browser_width - 171 - 16) // 4
        start = point_in_synth(182 + browser_width + cell / 2, 292)
        finish = point_in_synth(182 + browser_width + cell / 2, 252)
        mouse_at(start, hold=True)
        try:
            quiet()
            exact_worker()
            target = windows.u.WindowFromPoint(w.POINT(*finish))
            check(windows.pid(target) == worker_pid and windows.u.GetAncestor(target, 2) == native, "macro drag remains inside owned worker")
            windows.u.SetCursorPos(*finish)
            expected_cursor = finish
            time.sleep(.1)
            quiet()
        finally:
            release_mouse()
        poll(lambda: call("vibera_state")["revision"] > revision_before, "Native macro gesture history commit")
        check(call("vibera_state")["revision"] == revision_before + 1, "native macro drag creates one exact history revision")
        row, changed_page = parameter_page()
        tone_after = next(item["normalized"] for item in changed_page["items"] if item["id"] == tone_id)
        check(tone_after > tone_before + .03, "native Tone drag changes actual exposed scalar")
        capture("synth-native-tone-changed.png")
        stage("actual native search and Tone history gesture")
        # Header button centres from the pinned normal1380x920 Synth layout.
        # Successful native writes/loads run in the separate bounded preset-io launch.
        cancel_chooser("save", 380)
        cancel_chooser("load", 510)
        job(call("vibera_synth", dict(action="closeEditor", plugin=plugin, instance=instance)))
        native = None  # No native input after the following structural history operation.
        call("vibera_transport", dict(action="stop"))
        admission = call("vibera_command", dict(command="edit.undo"))
        commands.await_command_result(admission, call, lambda observation: report.setdefault("commandObservations", []).append(observation))
        _, restored = parameter_page()
        tone_restored = next(item["normalized"] for item in restored["items"] if item["id"] == tone_id)
        check(abs(tone_restored - tone_before) < 2e-5, "shared Undo restores native macro gesture exactly")
        report["macro"] = dict(before=tone_before, changed=tone_after, undo=tone_restored)
        stage("native editor close and actual shared Undo")

    try:
        windows = Windows()
        preflight, original_cursor = windows.preflight()
        expected_cursor = original_cursor
        initial_foreground = windows.u.GetForegroundWindow()
        report["preflight"] = preflight
        check(all(preflight[key] for key in ("dpiContextAccepted", "threadMatchesInputDesktop", "threadIsDefaultDesktop", "inputIsDefaultDesktop")) and
              not preflight["cursorMoved"] and not preflight["heldControl"], "interactive Default desktop and idle input preflight")
        monitors = windows.monitors()
        report["monitors"] = monitors
        targets = []
        for dpi in (96, 144):
            choices = [monitor for monitor in monitors if monitor["dpi"] == dpi and monitor["dpiY"] == dpi and
                       monitor["work"][2] - monitor["work"][0] >= (1380 + 8) * dpi / 96 + 40 and
                       monitor["work"][3] - monitor["work"][1] >= (920 + 122) * dpi / 96 + 40]
            check(bool(choices), "actual monitor with adequate work area at " + str(dpi) + " DPI")
            targets.append(max(choices, key=lambda item: item["work"][2] - item["work"][0]))
        owned_job_members()  # Establish descendant provenance BEFORE launching anything.
        environment = os.environ.copy()
        environment["VIBERA_SYNTH_USER_DIRECTORY"] = str(output / "user")
        environment["VIBERA_WRAPPER_SOFTWARE_RENDERER"] = "1"
        command = [str(app), "--mcp-test", "--window-check-project", "--native-mixer-meter-fixture", str(output)]
        proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
        report.update(pid=proc.pid, creationIdentity=process_identity(proc.pid), command=command)
        deadline = time.monotonic() + 95
        def receive():
            try:
                for line in proc.stdout:
                    replies.put(json.loads(line))
            except BaseException as error:
                replies.put(error)
            replies.put(RuntimeError("Owned Studio stdout closed"))
        threading.Thread(target=receive, daemon=True).start()
        threading.Thread(target=lambda: errors.extend(proc.stderr), daemon=True).start()
        report["server"] = request("initialize", dict(protocolVersion="2025-11-25", capabilities={},
                                                       clientInfo=dict(name="Synth native acceptance", version="1")))
        proc.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        proc.stdin.flush()
        discovery = call("vibera_synth", dict(action="list", genre="All Genres", family="All", search="", favoritesOnly=False))
        check(Path(discovery["userDirectory"]).resolve() == output / "user", "owned Synth user directory honored before writes")
        patch = next(item for item in discovery["presets"] if item["sourceKind"] == "SYNTHESIS" and item["family"] == "Bass")
        spec = importlib.util.spec_from_file_location("synth_native_commands", ROOT / "scripts/verify-installed-mcp.py")
        commands = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(commands)
        created = call("vibera_synth", dict(action="create"))
        commands.await_command_result(created, call, lambda observation: report.setdefault("commandObservations", []).append(observation))
        instruments = [item for item in call("vibera_plugin", dict(action="list"))["items"] if item["instrument"]]
        check(len(instruments) == 1, "fresh fixture owns exactly one created native instrument")
        plugin = instruments[0]["id"]
        row, page = parameter_page()
        instance = row["instance"]
        job(call("vibera_synth", dict(action="loadPreset", plugin=plugin, instance=instance, generation=page["generation"], preset=patch["id"])))
        call("vibera_synth", dict(action="filter", genre="All Genres", family="All", search="", favoritesOnly=False))
        navigation = dict(effectPage=4, octave=0, scrollX=0, scrollY=0, presetScrollY=0)
        navigated = call("vibera_synth", dict(action="navigate", **navigation))["browser"]
        check(all(navigated[key] == value for key, value in navigation.items()), "actual shared editor navigation readback")
        call("vibera_transport", dict(action="pattern"))
        poll(settled_plugin, "Pattern mode and native instrument audio preparation settled before Play")
        report["playAdmission"] = call("vibera_transport", dict(action="play"))
        def playing_ready():
            state = call("vibera_state")
            return state if state.get("playing") is True and state.get("pendingAudioUpdate") is False else None
        poll(playing_ready, "Actual shared playing state after any asynchronous device preparation")
        poll(lambda: telemetry(False), "Live changed-source telemetry through virtual callback")
        call("vibera_synth", dict(action="previewOn", plugin=plugin, note=60, velocity=90))
        poll(lambda: telemetry(True), "Actual semantic preview voice on")
        call("vibera_synth", dict(action="previewOff", plugin=plugin, note=60))
        call("vibera_synth", dict(action="panic", plugin=plugin))
        poll(lambda: telemetry(False), "Actual semantic preview release")
        stage("startup, preparation and actual semantic preview")
        sampled_patch = next(item for item in discovery["presets"] if item["sourceKind"] == "SAMPLE" and item["family"] == "Piano")
        playing_patch(sampled_patch)
        playing_patch(patch)
        job(call("vibera_synth", dict(action="openEditor", plugin=plugin, instance=instance)))
        editor_state = call("vibera_plugin", dict(action="editorState", plugin=plugin, instance=instance))["nativeEditor"]
        worker_pid = editor_state["nativeProcessId"]
        check(worker_pid != proc.pid and worker_pid in owned_job_members() and windows.parent_pid(worker_pid) == proc.pid,
              "exact prepared worker is a distinct direct owned child")
        worker_handle = windows.k.OpenProcess(0x1000 | 0x100000 | 1, False, worker_pid)
        check(bool(worker_handle), "retained exact worker process handle")
        worker_identity = process_identity(worker_pid)
        check(worker_identity is not None and windows.image_path(worker_handle) == app.with_name("VibeRa Plugin Worker.exe"),
              "worker creation identity and exact sibling executable path")
        report["workerIdentity"] = dict(pid=worker_pid, creationIdentity=worker_identity, parent=proc.pid, instance=instance)
        native = poll(lambda: next((hwnd for hwnd, title in windows.windows(worker_pid) if title == "VibeRa | VibeRa Synthesizer"), None),
                      "Exact owned Synth native HWND")
        stage("native editor open and exact worker ownership")
        if preset_io:
            place(targets[0], (1380, 920))
            computer_keyboard()
            call("vibera_transport", dict(action="stop"))
            check(call("vibera_state")["playing"] is False, "native user-preset history phase owns stopped transport")
            native_user_roundtrip()
        else:
            default_editor_checks()
        workflow_passed = True
    except BaseException as error:
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
    finally:
        cleanup_errors = []
        try:
            if windows:
                release_mouse()
                for key in list(owned_keys):
                    windows.u.keybd_event(key, 0, 2, 0)
                    owned_keys.discard(key)
        except BaseException as error:
            cleanup_errors.append("owned input release: " + repr(error))
        try:
            if proc:
                if proc.poll() is None:
                    try:
                        proc.stdin.close()
                    except OSError:
                        pass  # The retained process may have already closed its pipe.
                    try:
                        proc.wait(timeout=8)
                        report["closedGracefully"] = True
                    except subprocess.TimeoutExpired:
                        report["closedGracefully"] = False
                        proc.terminate()  # Exact retained task-created process HANDLE only.
                        proc.wait(timeout=2)
                report["exitCode"] = proc.returncode
                (output / "stderr.log").write_text("".join(errors), encoding="utf-8")
        except BaseException as error:
            cleanup_errors.append("owned Studio shutdown: " + repr(error))
        try:
            if worker_handle:
                worker_exit = windows.k.WaitForSingleObject(worker_handle, 1500)
                if worker_exit == 258:
                    report["forcedExactWorkerShutdown"] = True
                    windows.k.TerminateProcess(worker_handle, 1)
                    worker_exit = windows.k.WaitForSingleObject(worker_handle, 1000)
                if worker_exit != 0:
                    cleanup_errors.append("Exact retained worker exit could not be confirmed")
                report["workerExitConfirmed"] = worker_exit == 0
                windows.k.CloseHandle(worker_handle)
        except BaseException as error:
            cleanup_errors.append("retained worker shutdown: " + repr(error))
        try:
            if windows and expected_cursor is not None and windows.cursor() == expected_cursor and original_cursor is not None:
                windows.u.SetCursorPos(*original_cursor)
            fixture_file = output / "native-meter-driver.json"
            if fixture_file.exists():
                report["fixture"] = json.loads(fixture_file.read_text(encoding="utf-8-sig"))
                report["virtualAudio"] = report["fixture"].get("virtualAudio") is True
        except BaseException as error:
            cleanup_errors.append("cursor/fixture readback: " + repr(error))
        report["cleanupErrors"] = cleanup_errors
        try:
            report["sourceInputsAfter"] = {name: content_identity(ROOT / name)["contentSha256"] for name in source_names}
            report["sourceInputsStable"] = report["sourceInputsAfter"] == report["sourceInputs"]
            report["binaryInputsStable"] = (hashlib.sha256(app.read_bytes()).hexdigest() == report["applicationSha256"] and
                                             hashlib.sha256(app.with_name("VibeRa Plugin Worker.exe").read_bytes()).hexdigest() == report["workerSha256"])
        except BaseException as error:
            report["sourceInputsStable"] = False
            cleanup_errors.append("source identity readback: " + repr(error))
        report["seconds"] = time.monotonic() - began
        fixture = report.get("fixture", {})
        fixture_valid = (fixture.get("completionState") == "completed" and fixture.get("virtualAudio") is True and
                         fixture.get("sampleRate") == 48000 and fixture.get("bufferFrames") == 256 and
                         fixture.get("callbacks", 0) > 0 and fixture.get("lifecycleRetained") is True and
                         fixture.get("nonfiniteSamples") == 0 and not fixture.get("exhausted", True) and
                         not fixture.get("processingFailure", True))
        report["passed"] = (workflow_passed and report.get("closedGracefully") is True and report.get("exitCode") == 0 and
                            not cleanup_errors and report["sourceInputsStable"] and report.get("binaryInputsStable") is True and
                            not report.get("forcedExactWorkerShutdown") and fixture_valid and
                            report["seconds"] < 110)
        receipt.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(dict(passed=report["passed"], output=str(output), seconds=report["seconds"], error=report.get("error"))))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
