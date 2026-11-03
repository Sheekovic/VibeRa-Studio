"""Owned Settings OS mouse test, paired with semantic MCP result verification.

Scoped global input requires explicit authorization; not an MCP adapter.
Requires the local Pillow test dependency for native PrintWindow PNG evidence.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import pathlib
import sys
import time
import xml.etree.ElementTree as ET
from PIL import Image

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
app, fixture, output = (pathlib.Path(x).resolve() for x in sys.argv[1:4])
if not output.is_relative_to(ROOT / "build"):
    raise RuntimeError("Native evidence must stay under this project's build directory")
output.mkdir(parents=True, exist_ok=True)
client_directory = app.parent / "skills/vibera-studio/scripts"
if not client_directory.is_dir():
    client_directory = ROOT / "skills/vibera-studio/scripts"
sys.path.insert(0, str(client_directory))
from stdio_client import VibeRaClient

u, g = c.WinDLL("user32", use_last_error=True), c.WinDLL("gdi32", use_last_error=True)
for name, args, result in [
    ("SetThreadDpiAwarenessContext", [w.HANDLE], w.HANDLE),
    ("GetWindowThreadProcessId", [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
    ("GetWindowTextW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ("IsWindowVisible", [w.HWND], w.BOOL),
    ("GetClientRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetWindowRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetDpiForWindow", [w.HWND], w.UINT),
    ("GetForegroundWindow", [], w.HWND),
    ("SetForegroundWindow", [w.HWND], w.BOOL),
    ("WindowFromPoint", [w.POINT], w.HWND),
    ("SetCursorPos", [c.c_int, c.c_int], w.BOOL),
    ("GetAsyncKeyState", [c.c_int], c.c_short),
    ("SetWindowPos", [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT], w.BOOL),
    ("mouse_event", [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_size_t], None),
    ("GetDC", [w.HWND], w.HDC), ("ReleaseDC", [w.HWND, w.HDC], c.c_int),
    ("PrintWindow", [w.HWND, w.HDC, w.UINT], w.BOOL),
    ("SendMessageTimeoutW", [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)], w.LPARAM),
]:
    f = getattr(u, name); f.argtypes, f.restype = args, result
for name, args, result in [
    ("CreateCompatibleDC", [w.HDC], w.HDC),
    ("CreateCompatibleBitmap", [w.HDC, c.c_int, c.c_int], w.HBITMAP),
    ("SelectObject", [w.HDC, w.HANDLE], w.HANDLE),
    ("DeleteObject", [w.HANDLE], w.BOOL), ("DeleteDC", [w.HDC], w.BOOL),
    ("GetDIBits", [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT], c.c_int),
]:
    f = getattr(g, name); f.argtypes, f.restype = args, result
u.SetThreadDpiAwarenessContext(w.HANDLE(-4))


class Input(c.Structure):
    _fields_ = [("size", w.UINT), ("tick", w.DWORD)]


def input_tick():
    value = Input(c.sizeof(Input), 0)
    if not u.GetLastInputInfo(c.byref(value)):
        raise RuntimeError("Input observation unavailable")
    return value.tick


def owner(hwnd):
    value = w.DWORD()
    u.GetWindowThreadProcessId(hwnd, c.byref(value))
    return value.value


def capture(hwnd, path):
    rect = w.RECT()
    if not u.GetWindowRect(hwnd, c.byref(rect)):
        raise RuntimeError("Native window bounds unavailable")
    width, height = rect.right-rect.left, rect.bottom-rect.top
    dc = u.GetDC(hwnd); memory = g.CreateCompatibleDC(dc)
    bitmap = g.CreateCompatibleBitmap(dc, width, height)
    previous = g.SelectObject(memory, bitmap)
    try:
        if not u.PrintWindow(hwnd, memory, 2):
            raise RuntimeError("Owned PrintWindow failed")
        # BITMAPINFOHEADER followed by an unused colour table; top-down BGRA.
        header = c.create_string_buffer(40)
        import struct
        header.raw = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        pixels = c.create_string_buffer(width*height*4)
        g.SelectObject(memory, previous)
        if g.GetDIBits(memory, bitmap, 0, height, pixels, header, 0) != height:
            raise RuntimeError("Owned screenshot pixels unavailable")
        Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX").save(path)
    finally:
        g.SelectObject(memory, previous); g.DeleteObject(bitmap); g.DeleteDC(memory); u.ReleaseDC(hwnd, dc)


def window_for(pid):
    found = []
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def visit(hwnd, unused):
        text = c.create_unicode_buffer(100)
        u.GetWindowTextW(hwnd, text, 100)
        if owner(hwnd) == pid and u.IsWindowVisible(hwnd) and text.value == "Settings":
            found.append(hwnd)
        return True
    u.EnumWindows(callback(visit), 0)
    return found[0] if len(found) == 1 else None


class CaptureComplete(Exception):
    pass


client = None
active_tick = None
cursor = w.POINT()
u.GetCursorPos(c.byref(cursor))
report = {"passed": False, "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
          "evidence": "Guarded own-PID/HWND OS mouse input, actual Settings callbacks and semantic MCP readback; no human physical input, device or listening qualification", "checks": []}
began = time.monotonic()
try:
    plugin = next(p for p in ET.parse(fixture).getroot().findall(".//PLUGIN") if p.get("instrument") == "1")
    folder = pathlib.Path(ET.fromstring(plugin.get("descriptor")).get("file")).resolve().parent
    invalid = output / "invalid-input"
    invalid.mkdir(exist_ok=True)
    (invalid / "Owned invalid native scan.vst3").write_text("Owned invalid scanner fixture")
    folders = [str(folder), str(invalid)]
    client = VibeRaClient(app)
    report["pid"] = client.process.pid
    def state(): return client.data("vibera_catalogue", {"action": "state"})
    def wait_job(job, expected="completed"):
        until = time.monotonic()+40
        while time.monotonic() < until:
            value = client.call("vibera_catalogue", {"action":"job", "operation":job}, allow_failure=True)["structuredContent"]
            if value["completionState"] != "pending" and not state()["scanning"]:
                if value["completionState"] != expected:
                    raise AssertionError(str(value))
                return value
            time.sleep(.01)
        raise RuntimeError("Native callback scan did not finish")
    receipt = client.data("vibera_catalogue", {"action":"scan", "customFolders":folders, "standardLocations":False})
    wait_job(receipt["operation"])
    client.data("vibera_command", {"command":"view.plugins"})
    until = time.monotonic()+5
    hwnd = None
    while not hwnd and time.monotonic() < until:
        hwnd = window_for(client.process.pid); time.sleep(.02)
    if not hwnd:
        raise RuntimeError("Owned Settings window unavailable")
    time.sleep(.3)
    capture(hwnd, output / "native-catalogue-before.png")
    rect = w.RECT(); u.GetClientRect(hwnd, c.byref(rect))
    scale = u.GetDpiForWindow(hwnd)/96
    geometry = {"hwnd":int(hwnd), "dpi":u.GetDpiForWindow(hwnd), "clientWidth":rect.right,
                "clientHeight":rect.bottom, "logicalWidth":rect.right/scale, "logicalHeight":rect.bottom/scale,
                "awaitingInspection": True}
    (output / "geometry.json").write_text(json.dumps(geometry, indent=2))
    if len(sys.argv) > 4 and sys.argv[4] == "--capture-only":
        report["evidence"] = "Owned Settings native PrintWindow capture only; no input, device or listening qualification"
        report["captureOnly"] = True
        report["dpi"] = geometry["dpi"]
        native = c.WinDLL("dwmapi")
        native.DwmGetWindowAttribute.argtypes = [w.HWND, w.DWORD, c.c_void_p, w.DWORD]
        native.DwmGetWindowAttribute.restype = w.LONG
        attributes = {}
        for name, attribute in [("darkMode", 20), ("captionColour", 35), ("textColour", 36)]:
            value = w.DWORD()
            result = native.DwmGetWindowAttribute(hwnd, attribute, c.byref(value), c.sizeof(value))
            attributes[name] = {"result":result, "value":value.value if result == 0 else None}
        report["nativeCaption"] = attributes
        report["checks"].append("Actual owned Settings window captured; DWM attributes observed, no mouse/keyboard activity")
        report["passed"] = True
        raise CaptureComplete()
    # Actual image-pixel targets are inspected before any global mouse input.
    if len(sys.argv) > 4:
        calibration = json.loads(pathlib.Path(sys.argv[4]).read_text())
        if (calibration["applicationSha256"] != report["applicationSha256"] or
            calibration["dpi"] != geometry["dpi"] or
            calibration["clientSize"] != [rect.right, rect.bottom]):
            raise RuntimeError("Prior inspected calibration does not match these exact bytes/DPI/geometry")
        targets = calibration["targets"]
        report["calibration"] = calibration
    else:
        until = time.monotonic()+60
        gate = output / "inspected-targets.json"
        while not gate.exists() and time.monotonic() < until:
            time.sleep(.05)
        if not gate.exists():
            raise RuntimeError("Window screenshot inspection not acknowledged; no native input sent")
        targets = json.loads(gate.read_text())
    if ((c.WinDLL("kernel32").GetTickCount() - input_tick()) & 0xffffffff) < 1000:
        raise RuntimeError("Recent user input; native mouse test paused")
    u.SetForegroundWindow(hwnd)
    time.sleep(.2)
    if owner(u.GetForegroundWindow()) != client.process.pid:
        u.SetWindowPos(hwnd, w.HWND(-1), 0, 0, 0, 0, 0x13)
        u.SetForegroundWindow(hwnd)
        time.sleep(.2)
    active_tick = input_tick()
    def click(name):
        global active_tick
        if (client.process.poll() is not None or owner(hwnd) != client.process.pid or
            owner(u.GetForegroundWindow()) != client.process.pid or input_tick() != active_tick or
            any(u.GetAsyncKeyState(k) & 0x8000 for k in (1, 2, 16, 17, 18))):
            raise RuntimeError("Ownership/input changed; native messages paused")
        x, y = targets[name]
        screen = w.RECT(); u.GetWindowRect(hwnd, c.byref(screen))
        if not (0 <= x < screen.right-screen.left and 0 <= y < screen.bottom-screen.top):
            raise RuntimeError("Click outside inspected owned window image")
        point = w.POINT(screen.left+round(x), screen.top+round(y))
        if owner(u.WindowFromPoint(point)) != client.process.pid:
            raise RuntimeError("Owned target obscured; input paused")
        u.SetCursorPos(point.x, point.y)
        active_tick = input_tick()
        time.sleep(.04)
        if input_tick() != active_tick:
            raise RuntimeError("User input changed before click; paused")
        u.mouse_event(2, 0, 0, 0, 0)
        u.mouse_event(4, 0, 0, 0, 0)
        time.sleep(.1)
        actual = w.POINT(); u.GetCursorPos(c.byref(actual))
        if actual.x != point.x or actual.y != point.y:
            raise RuntimeError("Cursor moved during owned click; input paused")
        active_tick = input_tick()
    for target in ["scan", "retry"]:
        previous = state()["operation"]
        click(target)
        until = time.monotonic()+3
        while state()["operation"] == previous and time.monotonic() < until:
            time.sleep(.01)
        new = state()
        if new["operation"] == previous or new["requestedFolders"] != folders or new["requestedStandardLocations"]:
            raise RuntimeError("Native " + target + " did not invoke shared configured scan")
        result = wait_job(new["operation"])
        if result["failureCount"] != (1 if target == "retry" else 0):
            raise RuntimeError("Native retry/ordinary scan did not honour blacklist policy")
        report["checks"].append("Actual Settings " + target + " callback invokes canonical scan, persists typed scope and honours blacklist policy")
    # Prime the visible draft with the broader installed root, then cancel that
    # setup operation. The native Cancel test has multiple actual vendor bundles
    # to scan, rather than depending on a single very fast factory inspection.
    setup = client.data("vibera_catalogue", {"action":"scan", "customFolders":[str(folder.parent)], "standardLocations":False})
    time.sleep(.25)
    client.data("vibera_catalogue", {"action":"cancel", "operation":setup["operation"]})
    wait_job(setup["operation"], "cancelled")
    previous = state()["operation"]
    click("scan")
    current = state()
    if current["operation"] == previous:
        raise RuntimeError("Native cancellation test did not start a new scan")
    time.sleep(.25)
    click("cancel")
    cancelled = wait_job(current["operation"], "cancelled")
    if cancelled["persisted"]:
        raise RuntimeError("Cancelled native callback unexpectedly committed settings")
    report["checks"].append("Actual Settings Cancel callback reaches cancelled terminal state with previous catalogue retained")
    time.sleep(.25)
    capture(hwnd, output / "native-catalogue-cancelled.png")
    report["dpi"] = u.GetDpiForWindow(hwnd)
    report["passed"] = True
except CaptureComplete:
    pass
except Exception as failure:
    report["error"] = str(failure)
finally:
    if client is not None:
        try:
            client.close()
            report["exitCode"] = client.process.returncode
        except Exception as failure:
            report["passed"] = False; report["shutdownError"] = str(failure)
    if active_tick is not None and input_tick() == active_tick:
        u.SetCursorPos(cursor.x, cursor.y)
    report["seconds"] = time.monotonic()-began
    (output / "verification.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report))
if not report["passed"]:
    raise SystemExit(1)
