# SPDX-License-Identifier: AGPL-3.0-or-later
"""Scoped Windows input on one retained test-owned process; never targets another app."""
import ctypes as c
from ctypes import wintypes as w
import datetime
import hashlib
import json
import pathlib
import queue
import subprocess
import sys
import threading
import time
from PIL import Image

app = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2]).resolve()
project = pathlib.Path(sys.argv[3]).resolve()
root = pathlib.Path(__file__).resolve().parent.parent
if not output.is_relative_to(root / "build"):
    raise RuntimeError("Evidence must remain in the project build directory")
output.mkdir(parents=True, exist_ok=True)
u = c.WinDLL("user32", use_last_error=True)
g = c.WinDLL("gdi32", use_last_error=True)
u.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetThreadDpiAwarenessContext.restype = w.HANDLE
u.SetThreadDpiAwarenessContext(w.HANDLE(-4))
for name, args, result in [
    ("GetForegroundWindow", [], w.HWND),
    ("WindowFromPoint", [w.POINT], w.HWND),
    ("GetWindowThreadProcessId", [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
    ("GetWindowTextW", [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ("IsWindowVisible", [w.HWND], w.BOOL),
    ("SetForegroundWindow", [w.HWND], w.BOOL),
    ("SetWindowPos", [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT], w.BOOL),
    ("ClientToScreen", [w.HWND, c.POINTER(w.POINT)], w.BOOL),
    ("GetClientRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetWindowRect", [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ("GetDpiForWindow", [w.HWND], w.UINT),
    ("GetDC", [w.HWND], w.HDC),
    ("ReleaseDC", [w.HWND, w.HDC], c.c_int),
    ("PrintWindow", [w.HWND, w.HDC, w.UINT], w.BOOL),
]:
    f = getattr(u, name); f.argtypes = args; f.restype = result
for name, args, result in [
    ("CreateCompatibleDC", [w.HDC], w.HDC),
    ("CreateCompatibleBitmap", [w.HDC, c.c_int, c.c_int], w.HBITMAP),
    ("SelectObject", [w.HDC, w.HANDLE], w.HANDLE),
    ("DeleteObject", [w.HANDLE], w.BOOL),
    ("DeleteDC", [w.HDC], w.BOOL),
    ("GetDIBits", [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT], c.c_int),
]:
    f = getattr(g, name); f.argtypes = args; f.restype = result

class LastInput(c.Structure):
    _fields_ = [("cbSize", w.UINT), ("dwTime", w.DWORD)]

def input_tick():
    info = LastInput(c.sizeof(LastInput), 0)
    if not u.GetLastInputInfo(c.byref(info)): raise RuntimeError("Input observation unavailable")
    return info.dwTime

def pid(hwnd):
    value = w.DWORD(); u.GetWindowThreadProcessId(hwnd, c.byref(value)); return value.value

proc = subprocess.Popen([str(app), "--window-check-project", str(project), "--mcp-test"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
                        creationflags=subprocess.CREATE_NO_WINDOW)
identity = {"pid": proc.pid, "launchedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(), "exitCode": None}
messages = queue.Queue(); errors = []; serial = 0; evidence = []; active_tick = None
cursor = w.POINT(); u.GetCursorPos(c.byref(cursor))
threading.Thread(target=lambda: [messages.put(json.loads(line)) for line in proc.stdout], daemon=True).start()
threading.Thread(target=lambda: errors.extend(proc.stderr), daemon=True).start()

def request(method, params=None):
    global serial
    serial += 1; item = {"jsonrpc": "2.0", "id": serial, "method": method}
    if params is not None: item["params"] = params
    proc.stdin.write(json.dumps(item) + "\n"); proc.stdin.flush()
    reply = messages.get(timeout=20)
    if reply.get("id") != serial or "error" in reply: raise RuntimeError(str(reply))
    return reply["result"]

def call(name, args=None):
    result = request("tools/call", {"name": name, "arguments": args or {}})
    if result.get("isError"): raise RuntimeError(str(result))
    return result.get("structuredContent", result)

def owned_windows():
    found = []
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def visit(hwnd, unused):
        if pid(hwnd) == proc.pid and u.IsWindowVisible(hwnd): found.append(hwnd)
        return True
    u.EnumWindows(callback_type(visit), 0)
    return found

def guard():
    if proc.poll() is not None or pid(u.GetForegroundWindow()) != proc.pid:
        raise RuntimeError("Foreground ownership lost; input paused")
    if active_tick is not None and input_tick() != active_tick:
        raise RuntimeError("New user input observed; input paused")
    if any(u.GetAsyncKeyState(key) & 0x8000 for key in (1, 2, 16, 17, 18)):
        raise RuntimeError("User control is held; input paused")

def mouse(point, button="left"):
    global active_tick
    guard(); p = w.POINT(round(point[0]), round(point[1]))
    if pid(u.WindowFromPoint(p)) != proc.pid: raise RuntimeError("Owned target is obscured; input paused")
    u.SetCursorPos(p.x, p.y)
    active_tick = input_tick(); time.sleep(.08); guard()
    u.mouse_event(8 if button == "right" else 2, 0, 0, 0, 0)
    time.sleep(.08)
    u.mouse_event(16 if button == "right" else 4, 0, 0, 0, 0)
    active_tick = input_tick(); time.sleep(.15)

def key(value):
    global active_tick
    guard(); u.keybd_event(value, 0, 0, 0); u.keybd_event(value, 0, 2, 0)
    active_tick = input_tick(); time.sleep(.15)

def capture(hwnd, name):
    if pid(hwnd) != proc.pid: raise RuntimeError("Capture ownership mismatch")
    rect = w.RECT(); u.GetClientRect(hwnd, c.byref(rect))
    width, height = rect.right, rect.bottom
    if not 0 < width * height <= 35000000: raise RuntimeError("Invalid capture dimensions")
    dc = u.GetDC(hwnd); memory = g.CreateCompatibleDC(dc); bitmap = g.CreateCompatibleBitmap(dc, width, height)
    previous = g.SelectObject(memory, bitmap)
    try:
        if not u.PrintWindow(hwnd, memory, 3): raise RuntimeError("Owned PrintWindow failed")
        header = c.create_string_buffer(40)
        import struct
        header.raw = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        pixels = c.create_string_buffer(width * height * 4)
        if g.GetDIBits(memory, bitmap, 0, height, pixels, header, 0) != height: raise RuntimeError("Capture readback failed")
        Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX").save(output / name)
    finally:
        g.SelectObject(memory, previous); g.DeleteObject(bitmap); g.DeleteDC(memory); u.ReleaseDC(hwnd, dc)

try:
    request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "parameter desktop acceptance", "version": "1"}})
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n"); proc.stdin.flush()
    main = None
    for attempt in range(60):
        for hwnd in owned_windows():
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value.startswith("VibeRa Studio"): main = hwnd; break
        if main: break
        time.sleep(.1)
    if not main: raise RuntimeError("Owned main window unavailable")
    if ((c.WinDLL("kernel32").GetTickCount() - input_tick()) & 0xffffffff) < 1000:
        raise RuntimeError("Recent user input observed; input paused")
    u.SetWindowPos(main, None, 80, 70, 1920, 1150, 0x10)
    u.SetForegroundWindow(main); time.sleep(.3)
    if pid(u.GetForegroundWindow()) != proc.pid:
        caption = w.POINT(); u.ClientToScreen(main, c.byref(caption)); caption.x += 300; caption.y += 18
        if pid(u.WindowFromPoint(caption)) != proc.pid or any(u.GetAsyncKeyState(k) & 0x8000 for k in (1, 2, 16, 17, 18)):
            raise RuntimeError("Owned caption obscured or user active; activation paused")
        u.SetCursorPos(caption.x, caption.y); u.mouse_event(2, 0, 0, 0, 0); u.mouse_event(4, 0, 0, 0, 0)
        active_tick = input_tick(); time.sleep(.15)
    guard(); active_tick = input_tick()
    call("vibera_editor", {"editor": 3, "action": "maximize"})
    scale = u.GetDpiForWindow(main) / 96.
    origin = w.POINT(); u.ClientToScreen(main, c.byref(origin))
    bounds = next(e for e in call("vibera_state")["editors"] if e["index"] == 3)["contentBounds"]
    def main_screen(x, y):
        return origin.x + (bounds[0] + x) * scale, origin.y + (36 + bounds[1] + y) * scale
    capture(main, "native-mixer-before.png")
    (output / "initial-state.json").write_text(json.dumps(call("vibera_state"), indent=2), encoding="utf-8")
    # The generated project owns its only original effect on Insert1.
    mouse(main_screen(120, 45))
    mouse(main_screen(120, 45), "right")
    capture(main, "native-mixer-after-menu.png")
    candidates = [h for h in owned_windows() if h != main]
    if not candidates: raise RuntimeError("Native Mixer slot menu unavailable")
    def rectangle(hwnd):
        r = w.RECT(); u.GetWindowRect(hwnd, c.byref(r)); return r
    candidates = [h for h in candidates if rectangle(h).right - rectangle(h).left >= 160 and rectangle(h).bottom - rectangle(h).top >= 50]
    if not candidates: raise RuntimeError("Native effect menu has no usable content rectangle")
    popup = min(candidates, key=lambda h: (rectangle(h).right - rectangle(h).left) * (rectangle(h).bottom - rectangle(h).top))
    r = rectangle(popup); capture(popup, "native-effect-menu.png")
    mouse((r.left + (r.right - r.left) / 2, r.top + 42 * scale))
    time.sleep(.3)
    submenus = [h for h in owned_windows() if h not in (main, popup) and rectangle(h).right - rectangle(h).left >= 160 and rectangle(h).bottom - rectangle(h).top >= 80]
    if not submenus: raise RuntimeError("Native effect submenu unavailable")
    submenu = max(submenus, key=lambda h: rectangle(h).bottom - rectangle(h).top)
    r = rectangle(submenu); capture(submenu, "native-effect-submenu.png")
    mouse((r.left + (r.right - r.left) / 2, r.top + 63 * scale))
    controls_window = None
    for attempt in range(60):
        for hwnd in owned_windows():
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value == "Plugin controls": controls_window = hwnd; break
        if controls_window: break
        time.sleep(.1)
    if not controls_window: raise RuntimeError("Native Plugin controls selection did not open the window")
    def control_state():
        return call("vibera_state")["audio"]["pluginControls"]
    def ready():
        deadline = time.monotonic() + 5
        state = control_state()
        while not state.get("ready") and time.monotonic() < deadline:
            time.sleep(.1); state = control_state()
        if not state.get("ready"): raise RuntimeError("Native controls acknowledgement/refresh did not settle")
        return state
    before = ready()
    if before.get("parameter") != 0 or not before["writable"]: raise RuntimeError("Native zero-ID gain not selected")
    capture(controls_window, "native-controls-before.png")
    origin_controls = w.POINT(); u.ClientToScreen(controls_window, c.byref(origin_controls))
    control_scale = u.GetDpiForWindow(controls_window) / 96.
    def control_screen(x, y):
        return origin_controls.x + x * control_scale, origin_controls.y + y * control_scale
    bx, by, width, height = before["valueBounds"]
    start = control_screen(bx + 10 + before["value"] * (width - 115), by + height / 2)
    finish = control_screen(bx + 10 + .8 * (width - 115), by + height / 2)
    guard()
    if pid(u.WindowFromPoint(w.POINT(round(start[0]), round(start[1])))) != proc.pid:
        raise RuntimeError("Native slider target obscured")
    u.SetCursorPos(round(start[0]), round(start[1])); active_tick = input_tick(); time.sleep(.08); guard()
    u.mouse_event(2, 0, 0, 0, 0); owned_drag_tick = input_tick()
    try:
        time.sleep(.08)
        if pid(u.GetForegroundWindow()) != proc.pid or input_tick() != owned_drag_tick:
            raise RuntimeError("User input/foreground changed during native drag; paused")
        u.SetCursorPos(round(finish[0]), round(finish[1])); time.sleep(.15)
    finally:
        u.mouse_event(4, 0, 0, 0, 0); active_tick = input_tick()
    time.sleep(.2); guard()
    changed = ready()
    if changed["parameter"] != 0 or changed["workerPid"] != before["workerPid"] or changed["value"] <= before["value"] + .15:
        raise RuntimeError("Native slider gesture did not update the retained zero-ID control")
    if abs(changed["controllerValue"] - changed["value"]) > .0001:
        raise RuntimeError("Native controller acknowledgement differs from the visible slider value")
    capture(controls_window, "native-controls-changed.png")
    dx, dy, dw, dh = changed["defaultBounds"]
    mouse(control_screen(dx + dw / 2, dy + dh / 2))
    restored = ready()
    if abs(restored["value"] - .5) > .0001 or abs(restored["controllerValue"] - .5) > .0001:
        raise RuntimeError("Native Default button failed controller/visible value readback")
    capture(controls_window, "native-controls-default.png")
    (output / "controls-readback.json").write_text(json.dumps({"before": before, "changed": changed, "default": restored}, indent=2), encoding="utf-8")
    evidence.append("PASS: Windows Mixer header/right-click -> effect submenu -> Plugin controls; native slider drag and Default click; native SDK controller acknowledgement matches displayed normalized value, ID0 and worker PID retained.")
    evidence.append("Owned native PrintWindow captures and OS synthetic input. No human physical input, listening, hardware or provider response-timing claim.")
    identity["passed"] = True

except Exception as error:
    identity["passed"] = False; identity["error"] = str(error)
finally:
    if proc.poll() is None:
        proc.stdin.close()
        try: proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.terminate(); proc.wait(timeout=5)  # Retained handle only, after graceful EOF.
    identity["exitCode"] = proc.returncode
    if active_tick is not None and input_tick() == active_tick: u.SetCursorPos(cursor.x, cursor.y)
    (output / "process.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
    (output / "verification.txt").write_text("\n".join(evidence) + "\n", encoding="utf-8")
    (output / "stderr.log").write_text("".join(errors), encoding="utf-8")
print(json.dumps(identity))
sys.exit(0 if identity.get("passed") and identity["exitCode"] == 0 else 1)


