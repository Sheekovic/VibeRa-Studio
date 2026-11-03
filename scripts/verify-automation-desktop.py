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

proc = subprocess.Popen([str(app), "--mcp-test", "--window-check-project"], stdin=subprocess.PIPE,
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
    request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "automation desktop acceptance", "version": "1"}})
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n"); proc.stdin.flush()
    main = None
    for attempt in range(30):
        for hwnd in owned_windows():
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value.startswith("VibeRa Studio"): main = hwnd; break
        if main: break
        time.sleep(.1)
    if not main: raise RuntimeError("Owned main window unavailable")
    if ((c.WinDLL("kernel32").GetTickCount() - input_tick()) & 0xffffffff) < 1000:
        raise RuntimeError("Recent user input observed; foreground/input automation paused")
    u.SetWindowPos(main, None, 80, 70, 1920, 1150, 0x10)
    u.SetForegroundWindow(main); time.sleep(.3)
    if pid(u.GetForegroundWindow()) != proc.pid:
        # Windows may refuse programmatic activation. A visible owned caption is a scoped activation target.
        caption = w.POINT(); u.ClientToScreen(main, c.byref(caption))
        caption.x += 300; caption.y += 18
        if pid(u.WindowFromPoint(caption)) != proc.pid or any(u.GetAsyncKeyState(k) & 0x8000 for k in (1, 2, 16, 17, 18)):
            raise RuntimeError("Owned caption obscured or user active; activation paused")
        u.SetCursorPos(caption.x, caption.y); u.mouse_event(2, 0, 0, 0, 0); u.mouse_event(4, 0, 0, 0, 0)
        active_tick = input_tick(); time.sleep(.15)
    # Never send activation keystrokes to an unrelated foreground window.
    guard(); active_tick = input_tick()
    state = call("vibera_state")
    call("vibera_editor", {"editor": 3, "action": "maximize"})
    state = call("vibera_state")
    scale = u.GetDpiForWindow(main) / 96.
    origin = w.POINT(); u.ClientToScreen(main, c.byref(origin))
    def screen(editor, x, y):
        bounds = next(e for e in call("vibera_state")["editors"] if e["index"] == editor)["contentBounds"]
        return origin.x + (bounds[0] + x) * scale, origin.y + (36 + bounds[1] + y) * scale
    mouse(screen(3, 44, 110), "right")
    popups = [h for h in owned_windows() if h != main]
    if not popups: raise RuntimeError("Mixer fader right-click did not open an owned popup")
    popup_sizes = []
    for popup in popups:
        rect = w.RECT(); u.GetWindowRect(popup, c.byref(rect))
        popup_sizes.append((popup, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top))
    (output / "popup-bounds.json").write_text(json.dumps(popup_sizes), encoding="utf-8")
    usable = [p for p in popup_sizes if p[3] >= 160 and p[4] >= 60]
    if not usable: raise RuntimeError("Owned automation menu has no usable rectangle")
    popup, left, top, width, height = min(usable, key=lambda p: p[3] * p[4])
    capture(popup, "native-mixer-automation-menu.png")
    mouse((left + width / 2, top + 15 * scale))
    deadline = time.monotonic() + 2
    state = call("vibera_state")
    while not state["automationSources"] and time.monotonic() < deadline and proc.poll() is None:
        time.sleep(.1); state = call("vibera_state")
    (output / "menu-after-state.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
    if len(state["automationSources"]) != 1: raise RuntimeError("Native Create automation clip did not commit")
    evidence.append("Windows right-click Mixer fader -> owned popup -> left-click first menu row Create automation clip; MCP document readback confirms one source/placement.")
    call("vibera_editor", {"editor": 2, "action": "maximize"})
    state = call("vibera_state")
    clip = next(clip for clip in state["clips"] if clip["kind"] == "automation")
    bounds = clip["playlistBounds"]
    insert_x, insert_y = bounds["x"] + bounds["width"] / 2, bounds["y"] + bounds["height"] - 8
    mouse(screen(2, insert_x, insert_y), "right")
    state = call("vibera_state")
    points = state["automationSources"][0]["points"]
    if len(points) != 3: raise RuntimeError("Native right-click curve did not add point")
    middle = points[1]
    area_x, area_y = bounds["x"] + 4, bounds["y"] + 22
    area_width, area_height = bounds["width"] - 8, bounds["height"] - 25
    point_x = area_x + middle["beat"] / clip["lengthBeats"] * area_width
    point_y = area_y + (1 - middle["value"]) * area_height
    start = screen(2, point_x, point_y); finish = screen(2, point_x + 25, point_y - 6)
    guard()
    if pid(u.WindowFromPoint(w.POINT(round(start[0]), round(start[1])))) != proc.pid: raise RuntimeError("Drag target obscured")
    u.SetCursorPos(round(start[0]), round(start[1])); u.mouse_event(2, 0, 0, 0, 0)
    owned_drag_tick = input_tick()
    try:
        time.sleep(.05)
        if pid(u.GetForegroundWindow()) != proc.pid or input_tick() != owned_drag_tick:
            raise RuntimeError("User input or foreground changed during drag; input paused")
        u.SetCursorPos(round(finish[0]), round(finish[1])); time.sleep(.12)
    finally:
        # Release only the synthetic button owned by this gesture, even if foreground changes.
        u.mouse_event(4, 0, 0, 0, 0); active_tick = input_tick()
    time.sleep(.15); guard()
    changed = call("vibera_state")["automationSources"][0]["points"][1]
    if changed["value"] == middle["value"] or changed["beat"] == middle["beat"]:
        raise RuntimeError("Native point drag failed document readback")
    call("vibera_command", {"command": "edit.undo"})
    if call("vibera_state")["automationSources"][0]["points"][1] != middle: raise RuntimeError("Native gesture Undo not exact")
    call("vibera_command", {"command": "edit.redo"})
    if call("vibera_state")["automationSources"][0]["points"][1] != changed: raise RuntimeError("Native gesture Redo not exact")
    capture(main, "native-automation-points.png")
    evidence.append("Windows right-click curve -> point insertion; Windows left-drag -> changed beat/value; exact canonical Undo/Redo readback. Owned native PrintWindow captures. No human input/listening/hardware claim.")
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
