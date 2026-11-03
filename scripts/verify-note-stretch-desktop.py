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

def guard(allow_left=False):
    if proc.poll() is not None or pid(u.GetForegroundWindow()) != proc.pid:
        raise RuntimeError("Foreground ownership lost; input paused")
    # This host's last-input API reports the current tick on every query, even
    # during a read-only observation. Use the existing native harness's complete
    # held-key checks, plus cursor-drift checks, rather than treating that clock
    # as a trustworthy idle detector. Never relax foreground/owned-point guards.
    point=w.POINT(); u.GetCursorPos(c.byref(point))
    if expected_cursor is not None and (point.x,point.y)!=expected_cursor:
        raise RuntimeError("User cursor movement observed; input paused")
    if (16 in owned_modifiers and (u.GetAsyncKeyState(160)&0x8000)) or any(u.GetAsyncKeyState(key) & 0x8000 for key in ((2,13,16,17,18,27,91,92,*range(32,91)) if allow_left else (1,2,13,16,17,18,27,91,92,*range(32,91))) if key not in owned_modifiers):
        raise RuntimeError("User control is held; input paused")

def mouse(point, button="left"):
    global active_tick,expected_cursor
    guard(); p = w.POINT(round(point[0]), round(point[1]))
    if pid(u.WindowFromPoint(p)) != proc.pid: raise RuntimeError("Owned target is obscured; input paused")
    u.SetCursorPos(p.x, p.y)
    expected_cursor=(p.x,p.y)
    active_tick = input_tick(); time.sleep(.08); guard()
    u.mouse_event(8 if button == "right" else 2, 0, 0, 0, 0)
    time.sleep(.08)
    u.mouse_event(16 if button == "right" else 4, 0, 0, 0, 0)
    active_tick = input_tick(); time.sleep(.15)

def key(value, allow_left=False):
    global active_tick
    guard(allow_left=allow_left)
    owner=w.DWORD();thread=u.GetWindowThreadProcessId(main,c.byref(owner))
    scan=u.MapVirtualKeyExW(value,0,u.GetKeyboardLayout(thread))
    if owner.value!=proc.pid or not 0<scan<256:raise RuntimeError("Owned keyboard scan unavailable")
    u.keybd_event(value,scan,0,0);time.sleep(.06);u.keybd_event(value,scan,2,0)
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

def chord(modifier, value):
    global active_tick
    guard()
    owner=w.DWORD();thread=u.GetWindowThreadProcessId(main,c.byref(owner))
    u.GetKeyboardLayout.argtypes=[w.DWORD];u.GetKeyboardLayout.restype=w.HANDLE
    u.MapVirtualKeyExW.argtypes=[w.UINT,w.UINT,w.HANDLE];u.MapVirtualKeyExW.restype=w.UINT
    layout=u.GetKeyboardLayout(thread)
    scans=[u.MapVirtualKeyExW(k,0,layout) for k in (modifier,value)]
    if owner.value!=proc.pid or not all(0<s<256 for s in scans):raise RuntimeError("Owned keyboard layout/scan unavailable")
    try:
        u.keybd_event(modifier, scans[0], 0, 0);time.sleep(.06)
        u.keybd_event(value, scans[1], 0, 0);time.sleep(.06)
        u.keybd_event(value, scans[1], 2, 0)
    finally:
        u.keybd_event(modifier, scans[0], 2, 0)
    active_tick = input_tick(); time.sleep(.15)


owned_modifiers=set()
pressed_modifiers=[]
expected_cursor=None
try:
    request("initialize", {"protocolVersion":"2025-11-25", "capabilities":{}, "clientInfo":{"name":"Piano selection stretch native acceptance", "version":"1"}})
    proc.stdin.write(json.dumps({"jsonrpc":"2.0", "method":"notifications/initialized"})+"\n"); proc.stdin.flush()
    main = None
    for attempt in range(40):
        for hwnd in owned_windows():
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value.startswith("VibeRa Studio"): main = hwnd; break
        if main: break
        time.sleep(.1)
    if not main: raise RuntimeError("Owned main window unavailable")
    idle=w.POINT();u.GetCursorPos(c.byref(idle));time.sleep(.5)
    fresh=w.POINT();u.GetCursorPos(c.byref(fresh))
    if (idle.x,idle.y)!=(fresh.x,fresh.y) or any(u.GetAsyncKeyState(k)&0x8000 for k in (1,2,13,16,17,18,27,91,92,*range(32,91))):
        raise RuntimeError("Actual user input observed; native test paused")
    expected_cursor=(fresh.x,fresh.y)
    # Temporary topmost applies only to this retained test-owned window. The
    # existing native harness uses the same option to keep owned targets visible.
    u.SetWindowPos(main, w.HWND(-1), 80, 70, 1920, 1150, 0x10)
    u.SetForegroundWindow(main); time.sleep(.3)
    if pid(u.GetForegroundWindow()) != proc.pid:
        caption = w.POINT(); u.ClientToScreen(main, c.byref(caption)); caption.x += 300; caption.y += 18
        if pid(u.WindowFromPoint(caption)) != proc.pid or any(u.GetAsyncKeyState(k)&0x8000 for k in (1,2,16,17,18)):
            raise RuntimeError("Owned caption obscured/user active; no activation; target PID="+str(pid(u.WindowFromPoint(caption))))
        u.SetCursorPos(caption.x,caption.y); u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0)
        expected_cursor=(caption.x,caption.y)
        active_tick=input_tick();time.sleep(.15)
    guard(); active_tick=input_tick()
    command=lambda name,**args:call("vibera_command",dict(command=name,args=args))
    state=lambda:call("vibera_state")
    current=state();pattern=current["selectedPattern"];channel=current["channels"][0]["id"]
    for beat,length,pitch in ((1,1,60),(2,.5,64)):
        command("notes.add",pattern=pattern,channel=channel,beat=beat,length=length,pitch=pitch,value=.7)
    ids=[n["id"]for n in state()["notes"]]
    command("notes.mute",pattern=pattern,channel=channel,ids=[ids[1]])
    command("pattern.length",pattern=pattern,length=8) # Keep both drag endpoints inside the owned viewport.
    command("clip.add",pattern=pattern,beat=0,lane=0,length=4)
    project=output/"native-note-stretch.vibra"
    job=call("vibera_project",dict(action="save",path=str(project),overwrite=True))
    while job["completionState"]=="pending":
        time.sleep(.03);job=call("vibera_project",dict(action="job",operation=job["operation"]))
    if job["completionState"]!="completed":raise RuntimeError("Native stretch fixture save failed")
    call("vibera_editor",dict(editor=1,action="maximize"));call("vibera_select",dict(scope=2));command("piano.fit")
    call("vibera_selection",dict(action="set",target="notes",ids=ids))
    scale=u.GetDpiForWindow(main)/96.
    origin=w.POINT();u.ClientToScreen(main,c.byref(origin))
    def screen(x,y):
        bounds=next(e for e in state()["editors"]if e["index"]==1)["contentBounds"]
        return origin.x+(bounds[0]+x)*scale,origin.y+(36+bounds[1]+y)*scale
    def score():return [{k:v for k,v in n.items()if k!="pianoBounds"}for n in state()["notes"]]
    def hold(keys):
        global active_tick
        guard();owner=w.DWORD();thread=u.GetWindowThreadProcessId(main,c.byref(owner));layout=u.GetKeyboardLayout(thread)
        if owner.value!=proc.pid:raise RuntimeError("Owned modifier target lost")
        scans=[u.MapVirtualKeyExW(value,0,layout)for value in keys]
        if not all(0<scan<256 for scan in scans):raise RuntimeError("Modifier scans unavailable before input")
        for value,scan in zip(keys,scans):
            u.keybd_event(value,scan,0,0);pressed_modifiers.append((value,scan));owned_modifiers.add(16 if value==161 else value)
        active_tick=input_tick();time.sleep(.07)
    def release(keys):
        global active_tick
        # Release only synthetic keys actually pressed here, even if focus was lost.
        for value,scan in reversed(pressed_modifiers):u.keybd_event(value,scan,2,0)
        pressed_modifiers.clear();owned_modifiers.clear();active_tick=input_tick();time.sleep(.1)
    def stroke(delta,edge=False,cancel=False,capture_preview=False):
        global active_tick,expected_cursor
        st=state();a,b=[n["pianoBounds"]for n in st["notes"][:2]];spacing=b["x"]-a["x"]
        h=st["piano"]["stretchHandleBounds"]
        if not h[2]:raise RuntimeError("Native selection stretch handle missing")
        point=(b["x"]+b["width"]-2,b["y"]+b["height"]*.5)if edge else(h[0]+h[2]*.5,h[1]+h[3]*.5)
        start=screen(*point);end=screen(point[0]+spacing*delta,point[1]);baseline=score();revision=st["revision"]
        guard()
        for point in (start,end):
            if pid(u.WindowFromPoint(w.POINT(int(point[0]),int(point[1]))))!=proc.pid:raise RuntimeError("Native stretch target obscured")
        u.SetCursorPos(int(start[0]),int(start[1]));expected_cursor=(int(start[0]),int(start[1]));active_tick=input_tick();time.sleep(.05);guard()
        keys=(161,18)if edge else()
        left_pressed=False
        try:
            if keys:hold(keys)
            guard() # Revalidate foreground/cursor after synthetic modifiers before any click.
            u.mouse_event(2,0,0,0,0);left_pressed=True
            for i in range(1,9):
                guard(allow_left=True);x=int(start[0]+(end[0]-start[0])*i/8);y=int(start[1])
                if pid(u.WindowFromPoint(w.POINT(x,y)))!=proc.pid:raise RuntimeError("Stretch stroke obscured")
                u.SetCursorPos(x,y);expected_cursor=(x,y);active_tick=input_tick();time.sleep(.025)
            preview=state()["piano"]["stretchPreview"]
            if preview["completionState"]!="pending" or score()!=baseline or state()["revision"]!=revision:raise RuntimeError("Native stretch preview changed source or failed to begin")
            if edge and abs(preview["ratio"]-1.25)>1e-6:raise RuntimeError("Actual RightShift+Alt did not choose25% ratio: "+str(preview))
            if capture_preview:capture(main,"native-stretch-preview.png")
            if cancel:
                key(27,allow_left=True)
                if score()!=baseline or state()["piano"]["stretchPreview"]["completionState"]!="cancelled":raise RuntimeError("Native Escape did not cancel held stretch")
        finally:
            if left_pressed:u.mouse_event(4,0,0,0,0)
            if pressed_modifiers:release(keys)
        active_tick=input_tick();time.sleep(.15);guard()
    original=score();stroke(1.5,capture_preview=True);stretched=score()
    if [(n["beat"],n["length"])for n in stretched]!=[(1,2),(3,1)]or not stretched[1]["muted"]:raise RuntimeError("Native handle commit lost exact music/properties")
    chord(17,90)
    if score()!=original:raise RuntimeError("Stretch was not one exact Ctrl+Z")
    chord(17,89)
    if score()!=stretched:raise RuntimeError("Stretch Ctrl+Y failed")
    chord(17,90);stroke(.4,edge=True)
    if [(n["beat"],n["length"])for n in score()]!=[(1,1.25),(2.25,.625)]:raise RuntimeError("RightShift+Alt edge gesture duplicated/resized instead of stretching")
    chord(17,90)
    if score()!=original:raise RuntimeError("Edge stretch was not one Undo")
    stroke(.5,cancel=True)
    if score()!=original:raise RuntimeError("Cancel changed source")
    stroke(1.5);chord(17,83);time.sleep(.3)
    import xml.etree.ElementTree as ET
    stored=ET.parse(project).getroot().findall("PATTERNS/PATTERN/NOTE")
    if len(stored)!=2 or sorted(float(n.get("length"))for n in stored)!=[1,2]:raise RuntimeError("Native Save did not persist stretched durations")
    capture(main,"native-stretched-phrase.png")
    (output/"final-state.json").write_text(json.dumps(state(),indent=2),encoding="utf-8")
    evidence.append("PASS owned native selected handle/non-mutating preview/release, actual RightShift+Alt edge25% ratios, exact Ctrl+Z/Y, held-stretch Escape and Ctrl+S saved durations/properties. Own PrintWindow preview/result. Not human input, listening or hardware.")
    identity["passed"]=True
except Exception as error:
    identity["passed"]=False;identity["error"]=str(error)
finally:
    if pressed_modifiers:release(())
    identity["forcedClose"]=False
    if proc.poll() is None:
        proc.stdin.close()
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            identity["forcedClose"]=True;proc.terminate();proc.wait(timeout=5)
    identity["exitCode"]=proc.returncode
    final_cursor=w.POINT();u.GetCursorPos(c.byref(final_cursor))
    if expected_cursor is not None and (final_cursor.x,final_cursor.y)==expected_cursor and not any(u.GetAsyncKeyState(k)&0x8000 for k in (1,2,16,17,18,*range(32,91))):
        u.SetCursorPos(cursor.x,cursor.y)
    (output/"process.json").write_text(json.dumps(identity,indent=2),encoding="utf-8")
    (output/"verification.txt").write_text("\n".join(evidence)+"\n",encoding="utf-8")
    (output/"stderr.log").write_text("".join(errors),encoding="utf-8")
print(json.dumps(identity))
sys.exit(0 if identity.get("passed") and identity["exitCode"]==0 and not identity["forcedClose"] else 1)
