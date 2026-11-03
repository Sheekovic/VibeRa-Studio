"""Scoped Windows input on one retained test-owned process; never targets another app."""
import ctypes as c
from ctypes import wintypes as w
import datetime
import hashlib
import json
import pathlib
import queue
import re
import subprocess
import sys
import threading
import time
from PIL import Image


def select_case(arguments):
    """Only explicit affected development runs may skip the full native suite."""
    if len(arguments) not in (2, 4):
        raise ValueError("Usage: verify-mixer-controls-desktop.py APP OUTPUT [--case full|overload|reorder|group-colour]")
    if len(arguments) == 4 and (arguments[2] != "--case" or arguments[3] not in ("full", "overload", "reorder", "group-colour")):
        raise ValueError("Unsupported native acceptance case; expected --case full|overload|reorder|group-colour")
    return arguments[3] if len(arguments) == 4 else "full"


def await_command_result(result, call, clock=time.monotonic, pause=time.sleep):
    """Observe the exact admitted job; never retry a refused mutation."""
    deadline = clock() + 20  # Existing native MCP observation budget, not a product deadline.
    while result.get("completionState") == "pending":
        operation = result.get("operation")
        if clock() >= deadline:
            raise RuntimeError("Native setup command still pending: " + str(result))
        pause(.03)
        if isinstance(operation, str) and operation:
            result = call("vibera_mixerchain", dict(action="job", operation=operation))
        elif type(operation) is int and operation > 0:
            observed = call("vibera_mixer", dict(action="routingJob", operation=operation))
            result = observed["routing"]
        else:
            raise RuntimeError("Unknown asynchronous command receipt: " + str(result))
    if result.get("completionState") in ("failed", "cancelled"):
        raise RuntimeError("Native setup command did not complete: " + str(result))
    return result


def await_sample_projection(read_state, read_sources, channel, source, guard,
                            clock=time.monotonic, pause=time.sleep):
    """Observe genuine current-source callback work; never redispatch a mutation.

    This is a fixture prerequisite, not native posted/selected serial equality.
    The actual PDC predecessor guard remains authoritative after this returns.
    """
    deadline = clock() + 5  # Existing native setup observation budget.
    identity = baseline = last = None
    while clock() < deadline:
        guard()
        current = read_state()
        actual = (current["projectIdentity"], current["revision"])
        if identity is None:
            identity = actual
        if actual != identity:
            raise RuntimeError("Source prerequisite project/revision changed")
        page = read_sources()
        sources = [row for row in page["items"] if row["id"] == channel]
        if ((page["projectIdentity"], page["revision"]) != identity or len(sources) != 1 or
                sources[0]["source"] != source):
            raise RuntimeError("Source prerequisite channel/source identity changed")
        samplers = [row for row in current["audio"]["channels"] if row["channel"] == channel]
        sampler = samplers[0] if len(samplers) == 1 else {}
        calls = sampler.get("renderCalls")
        last = dict(projectIdentity=identity[0], revision=identity[1], channel=channel, source=source,
                    pendingAudioUpdate=current.get("pendingAudioUpdate"), sampler=sampler)
        if (current.get("pendingAudioUpdate") is False and type(calls) is int and calls >= 0 and
                type(sampler.get("graphInitialisations")) is int and sampler["graphInitialisations"] > 0):
            if baseline is None:
                baseline = calls
            elif calls > baseline:
                return dict(last, beforeRenderCalls=baseline, afterRenderCalls=calls,
                            evidence="Current projected sampler processed after pendingAudioUpdate=false; not exact native serial readback")
            elif calls < baseline:
                raise RuntimeError("Current source sampler changed after publication observation")
        else:
            baseline = None
        pause(.03)
    raise RuntimeError("Current source projection did not execute an actual later callback: " + str(last))



def mixer_colour_menu_contract(source):
    """Exact current full-suite menu path; refuse drift rather than guess rows."""
    marker = "void Studio::mixerControlMenu(int index)"
    colour = 'menu.addSubMenu("Colour", colours);'
    if source.count(marker) != 1:
        raise RuntimeError("Native Colour menu source unavailable/ambiguous")
    body = source.split(marker, 1)[1]
    if body.count(colour) != 1:
        raise RuntimeError("Native Colour menu target changed; review the owned input path")
    prefix = body.split(colour, 1)[0]
    items = re.findall(r'^\s*menu\.addItem\((\d+),\s*"([^"\n]+)"', prefix, re.M)
    expected = [("1", "Rename... (F2 / middle-click)"),
                ("30", "Selection / display groups..."), ("2", "Mute"),
                ("3", "Solo (Ctrl-click enable light)"),
                ("4", "Solo with connected inputs (Alt-click)"),
                ("5", "Restore previous mute state"),
                ("15", "Unmute all tracks (Ctrl-right-click enable light)")]
    normalized = re.sub(r"\s+", " ", prefix)
    conditions = ('menu.addSectionHeader(insert->name);',
                  'menu.addItem(1, "Rename... (F2 / middle-click)");',
                  'menu.addItem(30, "Selection / display groups...");',
                  'menu.addItem(2, "Mute", true, insert->muted);',
                  'menu.addItem(3, "Solo (Ctrl-click enable light)", index != 0);',
                  'menu.addItem(4, "Solo with connected inputs (Alt-click)", index != 0);',
                  'menu.addItem(5, "Restore previous mute state", controller.project.mixerSolo.has_value());',
                  'menu.addItem(15, "Unmute all tracks (Ctrl-right-click enable light)");')
    methods = re.findall(r'\bmenu\.(add\w+)\(', prefix)
    if items != expected or methods != ["addSectionHeader"] + ["addItem"] * 7 or not all(value in normalized for value in conditions):
        raise RuntimeError("Native Colour menu prefix/availability changed; review before input")
    return dict(target="Colour", downKeys=7, expectedPrefix=expected,
                prerequisites="Actual insert1 selected, no current solo restore session; Restore row disabled")


def mixer_group_colour_menu_contract(controls, organisation, groups):
    """Reviewed keyboard path under explicit selected/group/no-solo preconditions.

    Compare whole source prefixes, including disabled/conditional rows. This is
    source-defined navigation, never popup coordinates or observed row fitting.
    """
    normalize = lambda text: re.sub(r"\s+", " ", text).strip()
    control_marker = "void Studio::mixerControlMenu(int index)"
    organisation_marker = "void Studio::mixerOrganisationMenu(int index)"
    target = "menu.addItem(13, mixerGroupColourActionLabel(mixerInsert(controller.project, index)->colour));"
    if controls.count(control_marker) != 1 or organisation.count(organisation_marker) != 1:
        raise RuntimeError("Native group-colour menu source unavailable/ambiguous")
    control_body = controls.split(control_marker, 1)[1]
    main_target = 'menu.addItem(30, "Selection / display groups...");'
    if control_body.count(main_target) != 1:
        raise RuntimeError("Native Selection/display groups menu target changed")
    main_prefix = control_body.split(main_target, 1)[0] + main_target
    expected_main = '''{
        const auto *insert = mixerInsert(controller.project, index);
        if (!insert) return;
        juce::PopupMenu menu, colours;
        menu.addSectionHeader(insert->name);
        menu.addItem(1, "Rename... (F2 / middle-click)");
        menu.addItem(30, "Selection / display groups...");'''
    dispatch = '''if (choice == 30) {
        if (safe->controller.revision != revision)
        { safe->status = "mixer.group.staleIntent: Reopen Mixer menu"; return; }
        safe->mixerOrganisationMenu(index); return;
    }'''
    if normalize(main_prefix) != normalize(expected_main) or normalize(dispatch) not in normalize(control_body):
        raise RuntimeError("Native group-colour main menu prefix/availability/dispatch changed")
    body = organisation.split(organisation_marker, 1)[1]
    if body.count(target) != 1:
        raise RuntimeError("Native group-colour target changed")
    prefix = body.split(target, 1)[0] + target
    expected = '''{
        if (!mixerInsert(controller.project, index)) return;
        const auto identity = controller.projectIdentity;
        const auto revision = controller.revision;
        const auto targets = mixerSelection.ids;
        const auto source = selectedInsert;
        const auto groupId = mixerGroupFor(controller.project, index);
        const auto *group = mixerGroup(controller.project, groupId);
        const bool canGroup = !targets.empty() && std::find(targets.begin(), targets.end(), 0) == targets.end();
        const bool membershipAvailable = !(controller.project.mixerSolo && controller.project.mixerSolo->group.isNotEmpty());
        juce::PopupMenu menu, assign, select, colour, selectedColours;
        menu.addSectionHeader(juce::String(int(targets.size())) + " selected | active source " + juce::String(source));
        menu.addItem(1, "Select all inserts (Ctrl+A)");
        menu.addItem(4, "Rename selected... (F2)", !targets.empty());
        selectedColours.addItem(500, "Follow theme");
        colour.addItem(100, "Follow theme");
        for (int i = 0; i < 8; ++i) {
            selectedColours.addColouredItem(501 + i, "Colour " + juce::String(i + 1), channelColour(i));
            colour.addColouredItem(101 + i, "Colour " + juce::String(i + 1), channelColour(i));
        }
        menu.addSubMenu("Colour selected inserts", selectedColours);
        menu.addSeparator();
        menu.addItem(3, "New group from selected... (Alt+G)", canGroup && membershipAvailable);
        menu.addItem(5, "Ungroup selected", canGroup && membershipAvailable);
        if (!membershipAvailable) menu.addItem(98,"Restore group solo before editing membership",false);
        std::vector<juce::String> groupIds;
        for (const auto &entry : controller.project.mixerGroups) {
            const int offset = int(groupIds.size()); groupIds.push_back(entry.id);
            assign.addItem(1000 + offset, entry.name);
            select.addItem(2000 + offset, entry.name, !entry.members.empty());
        }
        menu.addSubMenu("Assign selected to group", assign, canGroup && membershipAvailable && !groupIds.empty());
        menu.addSubMenu("Select group", select, !groupIds.empty());
        if (group) {
            menu.addSectionHeader(group->name);
            menu.addItem(2, "Select this group", !group->members.empty());
            menu.addItem(6, "Rename group..."); menu.addSubMenu("Group marker colour", colour);
            menu.addItem(13, mixerGroupColourActionLabel(mixerInsert(controller.project, index)->colour));'''
    if normalize(prefix) != normalize(expected):
        raise RuntimeError("Native group-colour prefix/disabled rows changed; review before input")
    helper = '''inline juce::String mixerGroupColourActionLabel(juce::uint32 storedColour) {
        return storedColour == 0 ? "Reset group insert colours to theme" : "Apply this insert colour to group";
    }'''
    action = '''if (choice == 13) {
        args.mixer = index; // Original clicked member, never later selection/row.
        safe->command("mixer.group.colourMembers", args); return;
    }'''
    if normalize(helper) not in normalize(groups) or normalize(action) not in normalize(body):
        raise RuntimeError("Native group-colour label/source/canonical dispatch changed")
    enabled = ["Select all inserts", "Rename selected", "Colour selected inserts",
               "New group from selected", "Ungroup selected", "Assign selected to group",
               "Select group", "Select this group", "Rename group", "Group marker colour",
               "group.colourMembers"]
    return dict(mainDownKeys=2, groupDownKeys=len(enabled), enabledPrefix=enabled,
                labels=dict(explicit="Apply this insert colour to group",
                            theme="Reset group insert colours to theme"),
                prerequisites="selected stableIDs[1,3], active/source1, one nonempty containing group; no Mixer solo; all membership rows enabled, disabled98 absent")


def startup_observation_failure(observation):
    """Classify category-only evidence without guessing whether input was human."""
    if (not observation.get("dpiContextAccepted")
            or not observation.get("threadMatchesInputDesktop")
            or not observation.get("threadIsDefaultDesktop")
            or not observation.get("inputIsDefaultDesktop")
            or not all(observation.get("cursorQuerySucceeded", []))
            or len(observation.get("cursorQuerySucceeded", [])) != 2
            or set(observation.get("heldCategories", {})) != {"mouseButtons", "modifiers", "textNavigation"}):
        return "observation_unavailable"
    if observation.get("cursorMoved") or any(observation.get("heldCategories", {}).values()):
        return "activity_observed"
    return None


def caption_activation_diagnostic(api, main, owner_pid, *, positioned, activated,
                                  converted, origin, caption, hit, foreground, held):
    """Read-only refusal snapshot; never grants input or changes window order.

    Hit/foreground/held values are the actual refusal observations. Additional
    geometry/style reads follow them and are observational, not an atomic desktop
    snapshot. No titles, text, keys, or pixels from another app are captured.
    """
    def handle(hwnd):
        return int(getattr(hwnd, "value", hwnd) or 0)

    def window(hwnd):
        hwnd = handle(hwnd)
        owner = w.DWORD()
        thread = api.GetWindowThreadProcessId(hwnd, c.byref(owner)) if hwnd else 0
        valid = bool(hwnd and api.IsWindow(hwnd))
        style = int(api.GetWindowLongPtrW(hwnd, -16)) if valid else None
        extended = int(api.GetWindowLongPtrW(hwnd, -20)) if valid else None
        return dict(hwnd=hwnd, pid=owner.value, thread=int(thread), isWindow=valid,
                    style=style, extendedStyle=extended,
                    topmost=bool(extended & 8) if extended is not None else None)

    def with_root(hwnd):
        item = window(hwnd)
        root = api.GetAncestor(handle(hwnd), 2) if handle(hwnd) else 0  # GA_ROOT
        item["root"] = window(root)
        return item

    def rectangle(method):
        value = w.RECT()
        return [value.left, value.top, value.right, value.bottom] if method(main, c.byref(value)) else None

    window_rect = rectangle(api.GetWindowRect)
    client_rect = rectangle(api.GetClientRect)
    screen_origin = [origin.x, origin.y] if converted else None
    screen_caption = [caption.x, caption.y] if converted and caption is not None else None
    inside = None
    if screen_caption is not None and client_rect is not None:
        x, y = caption.x - origin.x, caption.y - origin.y
        inside = client_rect[0] <= x < client_rect[2] and client_rect[1] <= y < client_rect[3]
    dpi = int(api.GetDpiForWindow(main))
    return dict(ownerPid=owner_pid, positioningSucceeded=positioned,
                setForegroundWindowSucceeded=activated, clientToScreenSucceeded=converted,
                owned=with_root(main), ownedWindowRect=window_rect, ownedClientRect=client_rect,
                clientOriginScreen=screen_origin, windowDpi=dpi or None,
                captionScreen=screen_caption, captionInsideOwnedClient=inside,
                hit=with_root(hit) if hit else None, foreground=with_root(foreground),
                heldCategories=held,
                scope="Read-only refusal observations; geometry/style queries are subsequent, not atomic")


def unicode_keyboard_input_type():
    """Complete Windows INPUT ABI, including the larger mouse union member."""
    class MouseInput(c.Structure):
        _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("mouseData", w.DWORD),
                    ("dwFlags", w.DWORD), ("time", w.DWORD), ("dwExtraInfo", c.c_size_t)]

    class KeyboardInput(c.Structure):
        _fields_ = [("wVk", w.WORD), ("wScan", w.WORD), ("dwFlags", w.DWORD),
                    ("time", w.DWORD), ("dwExtraInfo", c.c_size_t)]

    class HardwareInput(c.Structure):
        _fields_ = [("uMsg", w.DWORD), ("wParamL", w.WORD), ("wParamH", w.WORD)]

    class InputData(c.Union):
        _fields_ = [("mi", MouseInput), ("ki", KeyboardInput), ("hi", HardwareInput)]

    class Input(c.Structure):
        _fields_ = [("type", w.DWORD), ("data", InputData)]

    return Input


def send_unicode_text(text, send, guard, input_type, pause=time.sleep):
    """Fixture literal text only; guard each pair, never retry inserted text.

    A partial pair releases only its accepted down event under the same guard.
    Refused cleanup sends nothing to a newly unowned/active window and fails
    explicitly. Shortcuts continue using their original layout-aware path.
    """
    encoded = text.encode("utf-16-le")
    if not encoded or len(encoded) > 128 or any(ord(char) < 32 or ord(char) == 127 for char in text):
        raise ValueError("Native fixture text must be1..64 UTF-16 units without control characters")
    for offset in range(0, len(encoded), 2):
        unit = int.from_bytes(encoded[offset:offset+2], "little")
        pair = (input_type * 2)()
        for index in range(2):
            pair[index].type = 1  # INPUT_KEYBOARD; wVk/time/extraInfo remain zero.
            pair[index].data.ki.wScan = unit
            pair[index].data.ki.dwFlags = 0x0004 | (0x0002 if index else 0)
        guard()
        sent = send(2, pair, c.sizeof(input_type))
        if sent != 2:
            if sent == 1:
                try:
                    guard()
                except Exception as error:
                    raise RuntimeError("Partial native Unicode input; key-up cleanup guard refused") from error
                release = (input_type * 1)(pair[1])
                if send(1, release, c.sizeof(input_type)) != 1:
                    raise RuntimeError("Partial native Unicode input; key-up cleanup failed")
            raise RuntimeError("Native Unicode SendInput accepted only " + str(sent) + " of2 events")
        pause(.15)


if len(sys.argv) == 2 and sys.argv[1] == "--diagnostic-self-test":
    baseline = dict(dpiContextAccepted=True, threadMatchesInputDesktop=True,
                    threadIsDefaultDesktop=True, inputIsDefaultDesktop=True,
                    cursorQuerySucceeded=[True, True], cursorMoved=False,
                    heldCategories=dict(mouseButtons=False, modifiers=False,
                                        textNavigation=False))
    assert startup_observation_failure(baseline) is None
    assert startup_observation_failure(dict(baseline, cursorMoved=True)) == "activity_observed"
    for category in baseline["heldCategories"]:
        held = dict(baseline["heldCategories"], **{category: True})
        assert startup_observation_failure(dict(baseline, heldCategories=held)) == "activity_observed"
    for key in ("dpiContextAccepted", "threadMatchesInputDesktop", "threadIsDefaultDesktop", "inputIsDefaultDesktop"):
        assert startup_observation_failure(dict(baseline, **{key: False})) == "observation_unavailable"
    assert startup_observation_failure(dict(baseline, cursorQuerySucceeded=[False, True])) == "observation_unavailable"
    assert startup_observation_failure(dict(baseline, heldCategories={})) == "observation_unavailable"
    assert startup_observation_failure({}) == "observation_unavailable"
    print("PASS startup classification: idle, cursor/held-category activity and unavailable observations; no native input")
    sys.exit(0)


selected_case = select_case(sys.argv[1:])
case_metadata = dict(selectedCase=selected_case, fullSuiteSelected=selected_case == "full",
                     verificationScope=("full native Mixer controls, group colour, reorder and overload; required release suite"
                                        if selected_case == "full" else
                                        "affected development only: native insert reorder; other controls/overload suite NOT performed"
                                        if selected_case == "reorder" else
                                        "affected development only: native group colour menus; full controls/reorder/overload NOT performed"
                                        if selected_case == "group-colour" else
                                        "affected development only: native overload reset; full controls suite NOT performed"))


if __name__ == "__main__":
    # Keep the lease across the entire native test and its owned-process cleanup.
    # run_path avoids indenting or duplicating the existing acceptance workflow.
    import runpy
    from dev_support import EngineeringOwner
    with EngineeringOwner(pathlib.Path(__file__).resolve().parent.parent,
                          "native-mixer-controls"):
        runpy.run_path(__file__, run_name="_vibera_native_owned")
    sys.exit(0)

started=time.monotonic()

app = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2]).resolve()
root = pathlib.Path(__file__).resolve().parent.parent
if not output.is_relative_to(root / "build"):
    raise RuntimeError("Evidence must remain in the project build directory")
output.mkdir(parents=True, exist_ok=True)
menu_contract = None
if selected_case == "full":
    try:
        menu_source = (root / "src/StudioMixerControls.cpp").read_bytes()
        menu_contract = mixer_colour_menu_contract(menu_source.decode("utf-8-sig"))
        menu_contract["sourceSha256"] = hashlib.sha256(menu_source).hexdigest()
    except Exception as error:
        result = dict(**case_metadata, passed=False, inputTestStarted=False, processLaunched=False,
                      error="Native full Colour source preflight refused: " + str(error), seconds=time.monotonic()-started)
        (output / "process.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result)); sys.exit(1)
group_menu_contract = None
if selected_case in ("full", "group-colour"):
    try:
        sources = {name: (root / name).read_bytes() for name in
                   ("src/StudioMixerControls.cpp", "src/StudioMixerOrganisation.inc", "src/MixerGroups.h")}
        group_menu_contract = mixer_group_colour_menu_contract(
            *(sources[name].decode("utf-8-sig") for name in sources))
        group_menu_contract["sources"] = {name: hashlib.sha256(data).hexdigest() for name, data in sources.items()}
    except Exception as error:
        result = dict(**case_metadata, passed=False, inputTestStarted=False, processLaunched=False,
                      error="Native group-colour source preflight refused: " + str(error), seconds=time.monotonic()-started)
        (output / "process.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result)); sys.exit(1)
u = c.WinDLL("user32", use_last_error=True)
g = c.WinDLL("gdi32", use_last_error=True)
u.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetThreadDpiAwarenessContext.restype = w.HANDLE
dpi_context_accepted = bool(u.SetThreadDpiAwarenessContext(w.HANDLE(-4)))
unicode_input_type = unicode_keyboard_input_type()
u.SendInput.argtypes = [w.UINT, c.POINTER(unicode_input_type), c.c_int]
u.SendInput.restype = w.UINT
for name, args, result in [
    ("GetCursorPos", [c.POINTER(w.POINT)], w.BOOL),
    ("GetAsyncKeyState", [c.c_int], c.c_short),
    ("GetThreadDesktop", [w.DWORD], w.HANDLE),
    ("OpenInputDesktop", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
    ("GetUserObjectInformationW", [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
    ("CloseDesktop", [w.HANDLE], w.BOOL),
    ("GetForegroundWindow", [], w.HWND),
    ("WindowFromPoint", [w.POINT], w.HWND),
    ("GetWindowThreadProcessId", [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
    ("IsWindow", [w.HWND], w.BOOL),
    ("GetAncestor", [w.HWND, w.UINT], w.HWND),
    ("GetWindowLongPtrW", [w.HWND, c.c_int], c.c_ssize_t),
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


def observe_startup():
    """No input/capture; retain booleans, never keys, text or cursor positions."""
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentThreadId.restype = w.DWORD
    def desktop_name(handle):
        if not handle:
            return None
        value = c.create_unicode_buffer(256); needed = w.DWORD()
        return value.value if u.GetUserObjectInformationW(handle, 2, value, c.sizeof(value), c.byref(needed)) else None
    thread_desktop = desktop_name(u.GetThreadDesktop(kernel.GetCurrentThreadId()))
    handle = u.OpenInputDesktop(0, False, 1)
    try:
        input_desktop = desktop_name(handle)
    finally:
        if handle:
            u.CloseDesktop(handle)
    first = w.POINT(); first_ok = bool(u.GetCursorPos(c.byref(first)))
    time.sleep(.5)
    fresh = w.POINT(); second_ok = bool(u.GetCursorPos(c.byref(fresh)))
    groups = dict(mouseButtons=(1, 2), modifiers=(16, 17, 18, 91, 92),
                  textNavigation=(13, 27, *range(32, 91)))
    observation = dict(dpiContextAccepted=dpi_context_accepted,
                       threadMatchesInputDesktop=thread_desktop is not None and thread_desktop == input_desktop,
                       threadIsDefaultDesktop=thread_desktop == "Default",
                       inputIsDefaultDesktop=input_desktop == "Default",
                       cursorQuerySucceeded=[first_ok, second_ok],
                       cursorMoved=first_ok and second_ok and (first.x, first.y) != (fresh.x, fresh.y),
                       heldCategories={name: any(u.GetAsyncKeyState(key) & 0x8000 for key in keys)
                                       for name, keys in groups.items()})
    observation["failureCategory"] = startup_observation_failure(observation)
    return observation, fresh


preflight_observation, cursor = observe_startup()
(output / "startup-preflight.json").write_text(json.dumps(preflight_observation, indent=2), encoding="utf-8")
if preflight_observation["failureCategory"]:
    result = dict(**case_metadata, passed=False, inputTestStarted=False, processLaunched=False,
                  error="Native startup preflight refused: " + preflight_observation["failureCategory"],
                  startupPreflight=preflight_observation, seconds=time.monotonic()-started)
    (output / "process.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))
    sys.exit(1)

proc = subprocess.Popen([str(app), "--mcp-test", "--window-check-project",
                         "--native-mixer-meter-fixture", str(output)], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
                        creationflags=subprocess.CREATE_NO_WINDOW)
identity = {**case_metadata, "pid": proc.pid, "launchedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(), "exitCode": None,
            "scriptSha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
            "activation": "owned ordinary HWND_TOP, never TOPMOST; unchanged idle/PID/point/foreground guards",
            "startupPreflight": preflight_observation, "fullColourMenuContract": menu_contract,
            "groupColourMenuContract": group_menu_contract}
messages = queue.Queue(); errors = []; serial = 0; evidence = [case_metadata["verificationScope"]]; active_tick = None
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
    point=w.POINT()
    if not u.GetCursorPos(c.byref(point)):
        raise RuntimeError("Cursor observation unavailable; input paused")
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
    u.mouse_event(8 if button == "right" else 32 if button == "middle" else 2, 0, 0, 0, 0)
    time.sleep(.08)
    u.mouse_event(16 if button == "right" else 64 if button == "middle" else 4, 0, 0, 0, 0)
    active_tick = input_tick(); time.sleep(.15)

def key(value, allow_left=False):
    global active_tick
    guard(allow_left=allow_left)
    owner=w.DWORD();thread=u.GetWindowThreadProcessId(main,c.byref(owner))
    scan=u.MapVirtualKeyExW(value,0,u.GetKeyboardLayout(thread))
    if owner.value!=proc.pid or not 0<scan<256:raise RuntimeError("Owned keyboard scan unavailable")
    extended=1 if value in (33,34,35,36,37,38,39,40,45,46) else 0
    u.keybd_event(value,scan,extended,0);time.sleep(.06);u.keybd_event(value,scan,extended|2,0)
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
    request("initialize", {"protocolVersion":"2025-11-25", "capabilities":{}, "clientInfo":{"name":"Mixer control native acceptance", "version":"1"}})
    proc.stdin.write(json.dumps({"jsonrpc":"2.0", "method":"notifications/initialized"})+"\n"); proc.stdin.flush()
    main = None
    for attempt in range(40):
        for hwnd in owned_windows():
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value.startswith("VibeRa Studio"): main = hwnd; break
        if main: break
        time.sleep(.1)
    if not main: raise RuntimeError("Owned main window unavailable")
    startup_observation, fresh = observe_startup()
    identity["startupObservation"] = startup_observation
    if startup_observation["failureCategory"]:
        raise RuntimeError("Native startup guard refused: " + startup_observation["failureCategory"])
    expected_cursor=(fresh.x,fresh.y)
    # Match an ordinary application window. Forcing native HWND_TOPMOST behind
    # JUCE's back can cover its modal windows without updating JUCE's own flag.
    # Foreground/PID/point/physical-input guards remain mandatory.
    # HWND_NOTOPMOST does not raise a window which is already ordinary. Raise
    # this verified owned window in the normal Z order so its guarded caption
    # can be clicked if Windows denies foreground activation. Never use TOPMOST
    # or alter another application's position, focus policy or input queue.
    positioned = bool(u.SetWindowPos(main, w.HWND(0), 80, 70, 1920, 1150, 0x10))
    if not positioned:
        identity["activationObservation"] = caption_activation_diagnostic(
            u, main, proc.pid, positioned=False, activated=None, converted=None,
            origin=None, caption=None, hit=None, foreground=u.GetForegroundWindow(), held=None)
        raise RuntimeError("Owned ordinary-window positioning failed")
    activated = bool(u.SetForegroundWindow(main)); time.sleep(.3)
    foreground = u.GetForegroundWindow()
    if pid(foreground) != proc.pid:
        origin = w.POINT()
        converted = bool(u.ClientToScreen(main, c.byref(origin)))
        if not converted:
            identity["activationObservation"] = caption_activation_diagnostic(
                u, main, proc.pid, positioned=True, activated=activated, converted=False,
                origin=None, caption=None, hit=None, foreground=foreground, held=None)
            raise RuntimeError("Owned caption coordinate conversion failed; no activation")
        caption = w.POINT(origin.x + 300, origin.y + 18)
        hit = u.WindowFromPoint(caption)
        held = dict(mouseButtons=any(u.GetAsyncKeyState(k)&0x8000 for k in (1,2)),
                    modifiers=any(u.GetAsyncKeyState(k)&0x8000 for k in (16,17,18)))
        if pid(hit) != proc.pid or any(held.values()):
            identity["activationObservation"] = caption_activation_diagnostic(
                u, main, proc.pid, positioned=True, activated=activated, converted=True,
                origin=origin, caption=caption, hit=hit, foreground=foreground, held=held)
            raise RuntimeError("Owned caption obscured/user active; no activation; target PID="+str(pid(hit)))
        u.SetCursorPos(caption.x,caption.y); u.mouse_event(2,0,0,0,0);u.mouse_event(4,0,0,0,0)
        expected_cursor=(caption.x,caption.y)
        active_tick=input_tick();time.sleep(.15)
    guard(); active_tick=input_tick()
    command=lambda name,**args:await_command_result(call("vibera_command",dict(command=name,args=args)), call)
    state=lambda:call("vibera_state")
    current=state();pattern=current["selectedPattern"];channel=current["channels"][0]["id"]
    command("channel.mixer",channel=channel,mixer=1)
    command("notes.add",pattern=pattern,channel=channel,beat=0,length=.5,pitch=60,value=.7)
    command("clip.add",pattern=pattern,beat=0,lane=0,length=4)
    project=output/"native-named-buses.vibra"
    job=call("vibera_project",dict(action="save",path=str(project),overwrite=True))
    while job["completionState"]=="pending":
        time.sleep(.03);job=call("vibera_project",dict(action="job",operation=job["operation"]))
    if job["completionState"]!="completed":raise RuntimeError("Native Mixer fixture save failed")
    call("vibera_editor",dict(editor=3,action="maximize"))
    call("vibera_mixer",dict(action="select",insert=2))
    scale=u.GetDpiForWindow(main)/96.
    origin=w.POINT();u.ClientToScreen(main,c.byref(origin))
    def screen(x,y):
        bounds=state()["mixerRouting"]["contentBounds"]
        return origin.x+(bounds[0]+x)*scale,origin.y+(36+bounds[1]+y)*scale
    def control(index,kind="button"):
        row=next(c for c in state()["mixerRouting"]["controls"]if c["index"]==index)
        x,y,width,height=row[kind];return x+width*.5,y+height*.5
    def routes():return {m["index"]:m["routes"]for m in call("vibera_inspect",dict(entity="mixer"))["items"]}
    def level():return next(r["gain"]for r in routes()[1]if r["target"]==3)
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
    rows=lambda:call("vibera_inspect",dict(entity="mixer"))["items"]
    row=lambda index:next(m for m in rows()if m["index"]==index)
    if selected_case == "full":
        mouse(screen(*control(1,"nameBounds")))
        if state()["selectedInsert"]!=1:raise RuntimeError("Native name selection")
        key(113) # F2, contextual Mixer rename
        capture(main,"native-after-f2-main.png")
        for hwnd in owned_windows():
            if hwnd!=main:capture(hwnd,"native-after-f2-dialog.png")
        identity["beforeRename"] = row(1)["name"]
        chord(17,65)
        # Actual layout-independent keyboard text; no clipboard or app mutation.
        send_unicode_text("drums", u.SendInput, guard, unicode_input_type)
        active_tick = input_tick()
        for hwnd in owned_windows():
            if hwnd!=main:capture(hwnd,"native-rename-typed.png")
        key(13)
        identity["afterRename"] = row(1)["name"]
        if row(1)["name"]!="drums" and row(1)["name"]!="DRUMS":raise RuntimeError("Native contextual F2 rename")
        renamed=row(1)["name"];chord(17,90)
        if row(1)["name"]==renamed:raise RuntimeError("Native rename Undo")
        chord(17,89)
        mouse(screen(*control(1,"nameBounds")),"middle");capture(main,"native-rename-main.png")
        for hwnd in owned_windows():
            if hwnd!=main:capture(hwnd,"native-rename-dialog.png")
        key(27)
        if row(1)["name"]!=renamed:raise RuntimeError("Native middle rename cancel")
        colour_state=state()
        if colour_state["selectedInsert"]!=1 or colour_state["mixerRouting"]["solo"] is not None:
            raise RuntimeError("Native Colour menu requires owned insert1/no active solo; no menu input sent")
        mouse(screen(*control(1,"nameBounds")),"right")
        for hwnd in owned_windows():
            if hwnd!=main:capture(hwnd,"native-mixer-context-menu.png")
        # Exact reviewed prefix includes Selection/groups; disabled Restore is skipped.
        for unused in range(menu_contract["downKeys"]):key(40)
        key(39);key(40);key(40);key(13)
        if row(1)["colour"]==0:raise RuntimeError("Native colour submenu did not apply")
        chord(17,90)
        mouse(screen(*control(1,"muteBounds")))
        if not row(1)["muted"]:raise RuntimeError("Native enable light mute")
        mouse(screen(*control(1,"muteBounds")))
        command("mixer.routeOnly",mixer=1,target=3);command("mixer.route",mixer=2,target=3);command("mixer.mute",mixer=7,enabled=True)
        baseline={m["index"]:m["muted"]for m in rows()}
        hold((17,))
        try:mouse(screen(*control(1,"muteBounds")))
        finally:release(())
        if row(0)["muted"] or row(3)["muted"] or not row(2)["muted"]:raise RuntimeError("Native Ctrl solo downstream")
        hold((18,))
        try:mouse(screen(*control(3,"muteBounds")))
        finally:release(())
        if row(1)["muted"] or row(2)["muted"] or not state()["mixerRouting"]["solo"]["connected"]:raise RuntimeError("Native Alt solo inputs")
        hold((18,))
        try:mouse(screen(*control(3,"muteBounds")))
        finally:release(())
        if {m["index"]:m["muted"]for m in rows()}!=baseline:raise RuntimeError("Native original mute restoration")
        command("mixer.mute",mixer=0,enabled=True)
        global_baseline={m["index"]:m["muted"] for m in rows()}
        selected_before=state()["selectedInsert"]
        hold((17,))
        try:mouse(screen(*control(2,"muteBounds")),"right")
        finally:release(())
        if any(m["muted"] for m in rows()) or state()["selectedInsert"]!=selected_before or state()["mixerRouting"]["solo"] is not None:
            raise RuntimeError("Native Ctrl-right Unmute All must retain selection and enable Master/all tracks")
        chord(17,90)
        if {m["index"]:m["muted"] for m in rows()}!=global_baseline:raise RuntimeError("Native global Unmute All Undo")
        chord(17,89)
        if any(m["muted"] for m in rows()):raise RuntimeError("Native global Unmute All Redo")
        def fader(index):
            bounds=next(item["faderBounds"] for item in state()["mixerRouting"]["controls"]
                        if item["index"]==index)
            if len(bounds)!=4 or bounds[2]<=0 or bounds[3]<=0:
                raise RuntimeError("Published native fader geometry unavailable")
            return bounds
        def gain_stroke(index,cancel=False):
            global expected_cursor,active_tick
            x,y,width,height=fader(index)
            start=screen(x+width*.5,y+height*.75)
            end=screen(x+width*.5,y+height*.25)
            left=False
            guard()
            if pid(u.WindowFromPoint(w.POINT(round(start[0]),round(start[1]))))!=proc.pid:
                raise RuntimeError("Native fader target is obscured")
            if not u.SetCursorPos(round(start[0]),round(start[1])):
                raise RuntimeError("Native fader cursor positioning failed")
            expected_cursor=(round(start[0]),round(start[1]));active_tick=input_tick();time.sleep(.06)
            try:
                guard();u.mouse_event(2,0,0,0,0);left=True;time.sleep(.08)
                if not state()["gestureActive"]:raise RuntimeError("Native gain gesture ownership")
                if cancel:
                    for i in range(1,9):
                        guard(allow_left=True)
                        point=w.POINT(round(start[0]),round(start[1]+(end[1]-start[1])*i/8))
                        if pid(u.WindowFromPoint(point))!=proc.pid:
                            raise RuntimeError("Native fader drag left owned app")
                        if not u.SetCursorPos(point.x,point.y):
                            raise RuntimeError("Native fader cursor move failed")
                        expected_cursor=(point.x,point.y);active_tick=input_tick();time.sleep(.025)
                    if abs(row(index)["gain"]-1.5)>.02:
                        raise RuntimeError("Native active gain drag did not reach published position")
                    key(27,allow_left=True)
                    if state()["gestureActive"]:raise RuntimeError("Native Escape left gain gesture active")
            finally:
                if left:u.mouse_event(4,0,0,0,0)
            active_tick=input_tick();time.sleep(.15);guard()
            if state()["gestureActive"]:raise RuntimeError("Native gain release left gesture active")
        command("mixer.gain",mixer=1,value=1.)
        gain_stroke(1)
        if abs(row(1)["gain"]-.5)>.02:raise RuntimeError("Native gain click uses published fader mapping")
        clicked_gain=row(1)["gain"]
        chord(17,90)
        if abs(row(1)["gain"]-1.)>.0001:raise RuntimeError("One native gain Undo")
        chord(17,89)
        if abs(row(1)["gain"]-clicked_gain)>.0001:raise RuntimeError("Native gain Redo")
        gain_stroke(1,cancel=True)
        if abs(row(1)["gain"]-clicked_gain)>.0001:raise RuntimeError("Native gain Escape did not restore baseline")
        chord(17,90)
        if abs(row(1)["gain"]-1.)>.0001:raise RuntimeError("Cancelled gain stroke added history")
        chord(17,89)
        if abs(row(1)["gain"]-clicked_gain)>.0001:raise RuntimeError("Native gain history after cancellation")
        before_below=state();gains_before={item["index"]:item["gain"]for item in rows()}
        x,y,width,height=fader(2);below=y+height+30
        if below>=before_below["mixerRouting"]["contentBounds"][3]:
            raise RuntimeError("Native maximized fixture lacks space below capped fader")
        mouse(screen(x+width*.5,below))
        after_below=state()
        if after_below["selectedInsert"]!=2 or after_below["gestureActive"]:
            raise RuntimeError("Native below-fader click did not exclusively select insert")
        if gains_before!={item["index"]:item["gain"]for item in rows()}:
            raise RuntimeError("Native below-fader click changed gain")
        if any(after_below[key]!=before_below[key] for key in ("revision","dirty","undoAvailable","redoAvailable")):
            raise RuntimeError("Native below-fader click changed document/history state")
        identity["faderChecks"]={"clickedGain":clicked_gain,"oneUndoRedo":True,
                                 "activeEscapeRestored":True,"cancelAddedNoHistory":True,
                                 "belowCappedFaderSelectedWithoutEdit":True}
        call("vibera_mixer",dict(action="select",insert=1))
        def pan_stroke(delta,cancel=False,fine=False):
            global expected_cursor,active_tick
            point=control(1,"panBounds");start=screen(*point);end=screen(point[0]+delta,point[1]);left=False
            guard();u.SetCursorPos(round(start[0]),round(start[1]));expected_cursor=(round(start[0]),round(start[1]));active_tick=input_tick();time.sleep(.06)
            try:
                if fine:hold((161,))
                guard();u.mouse_event(2,0,0,0,0);left=True
                for i in range(1,9):
                    guard(allow_left=True);x=round(start[0]+(end[0]-start[0])*i/8);y=round(start[1])
                    if pid(u.WindowFromPoint(w.POINT(x,y)))!=proc.pid:raise RuntimeError("Pan drag left owned app")
                    u.SetCursorPos(x,y);expected_cursor=(x,y);active_tick=input_tick();time.sleep(.025)
                if not state()["gestureActive"]:raise RuntimeError("Native pan gesture ownership")
                if cancel:key(27,allow_left=True)
            finally:
                if left:u.mouse_event(4,0,0,0,0)
                if pressed_modifiers:release(())
            time.sleep(.15);guard()
        pan_stroke(30)
        if abs(row(1)["pan"]-.3)>.015:raise RuntimeError("Native pan drag")
        chord(17,90)
        if abs(row(1)["pan"])>.0001:raise RuntimeError("One native pan Undo")
        chord(17,89);pan_stroke(-20,cancel=True)
        if abs(row(1)["pan"]-.3)>.015:raise RuntimeError("Native pan cancel")
        chord(17,90);pan_stroke(50,fine=True)
        if abs(row(1)["pan"]-.05)>.015:raise RuntimeError("Native RightShift fine pan")
        capture(main,"native-named-mixer.png")
        chord(17,83);time.sleep(.4)
        import xml.etree.ElementTree as ET
        stored=next(e for e in ET.parse(project).getroot().findall("MIXER/INSERT")if e.get("index")=="1")
        if stored.get("name")!=renamed or abs(float(stored.get("pan"))-.05)>.015 or abs(float(stored.get("gain"))-clicked_gain)>.0001:raise RuntimeError("Native Save persisted wrong Mixer state")
        evidence.append("PASS native contextual F2/middle rename/cancel/history, enable light, Ctrl downstream/Alt inputs/restore, gain click/one Undo/Redo/Escape/no-history cancel/below-capped-fader selection, pan/RightShift/Escape/one Undo/Redo and Ctrl+S gain readback. Owned PrintWindow; no listening/hardware/human input claim.")
    if selected_case in ("full", "group-colour"):
        identity["nativeGroupColour"] = dict(completionState="pending", nativeInput=False,
            scope="actual owned context/group menus; canonical stable UUID colours/selection and one native Undo/Redo; no DSP-settling claim",
            contract=group_menu_contract, observations=[])
        group_report = identity["nativeGroupColour"]
        def group_organisation():
            return call("vibera_mixer", dict(action="state"))["organisation"]
        def group_rows():
            found = call("vibera_inspect", dict(entity="mixer", limit=512))
            if found["nextOffset"] is not None:
                raise RuntimeError("Native group colour requires complete stable-ID Mixer readback")
            return [{key: value for key, value in item.items() if key != "meter"}
                    for item in found["items"]]
        def retain_group(step, expected=None):
            value = dict(step=step, state=state(), organisation=group_organisation(),
                         inserts=group_rows(), expectedInserts=expected)
            group_report["observations"].append(value)
            (output / "native-group-colour.json").write_text(json.dumps(group_report, indent=2), encoding="utf-8")
            return value
        original_groups = group_organisation()["groups"]
        if any(set(group["members"]) & {1, 3} for group in original_groups):
            raise RuntimeError("Owned native group-colour setup requires ungrouped stable inserts1/3")
        if state()["mixerRouting"]["solo"] is not None:
            raise RuntimeError("Native group-colour menu requires no active solo; no menu input sent")
        command("mixer.group.create", ids=[1, 3], text="Original native group colour")
        groups = group_organisation()["groups"]
        added = [group for group in groups if group["id"] not in {item["id"] for item in original_groups}]
        if len(added) != 1 or not re.fullmatch(r"[0-9a-f]{32}", added[0]["id"]) or added[0]["members"] != [1, 3]:
            raise RuntimeError("Actual shared group creation did not return one stable UUID and exact members")
        gid = added[0]["id"]
        marker_colour, source_colour, other_colour = 0xff49bc75, 0xff735bd8, 0xff5ac8d8
        command("mixer.group.colour", mixerGroup=gid, value=marker_colour)
        command("mixer.colour", mixer=1, value=source_colour)
        command("mixer.colour", mixer=3, value=other_colour)
        call("vibera_mixer", dict(action="selectMany", ids=[1, 3], mode="replace", active=1))
        group_report.update(group=gid, members=[1, 3], source=1, markerColour=marker_colour,
                            sourceColour=source_colour, otherColour=other_colour)
        def group_snapshot_ok(observation, baseline, expected, message):
            actual_state, base_state = observation["state"], baseline["state"]
            expected_org = dict(baseline["organisation"], revision=actual_state["revision"])
            if observation["inserts"] != expected or observation["organisation"] != expected_org:
                raise RuntimeError(message + ": colours/group marker/membership/selection/routes changed unexpectedly")
            if any(actual_state[key] != base_state[key]
                   for key in ("selectedInsert", "selectedChannel", "selectedPattern", "channels")):
                raise RuntimeError(message + ": canonical source/Channel selection changed")
            if actual_state["gestureActive"]:
                raise RuntimeError(message + ": discrete native action left a gesture active")
        def native_group_colour(label):
            current = state(); org = group_organisation()
            group = next((item for item in org["groups"] if item["id"] == gid), None)
            if (current["selectedInsert"] != 1 or org["selected"] != [1, 3]
                    or org["active"] != 1 or group is None or group["members"] != [1, 3]
                    or current["mixerRouting"]["solo"] is not None):
                raise RuntimeError("Reviewed group-colour menu prerequisites changed; no input sent")
            mouse(screen(*control(1, "nameBounds")), "right")
            group_report["nativeInput"] = True
            for hwnd in owned_windows():
                if hwnd != main:
                    capture(hwnd, "native-group-" + label + "-context.png")
            for unused in range(group_menu_contract["mainDownKeys"]): key(40)
            key(13)  # Actual Selection/display groups action opens its own menu.
            for hwnd in owned_windows():
                if hwnd != main:
                    capture(hwnd, "native-group-" + label + "-organisation.png")
            for unused in range(group_menu_contract["groupDownKeys"]): key(40)
            key(13)
        # Both actions are native menu input. MCP sets up the owning disposable
        # document and reads actual shared state, never substitutes the action.
        for label, colour in (("explicit", source_colour), ("theme", 0)):
            if label == "theme": command("mixer.colour", mixer=1, value=0)
            baseline = retain_group(label + "-before")
            if (baseline["organisation"]["selected"] != [1, 3]
                    or baseline["organisation"]["active"] != 1
                    or next(item["colour"] for item in baseline["inserts"] if item["index"] == 1) != colour
                    or next(item["colour"] for item in baseline["inserts"] if item["index"] == 3) == colour):
                raise RuntimeError("Independent native group-colour source/different member preconditions")
            expected = [dict(item, colour=colour) if item["index"] in (1, 3) else item
                        for item in baseline["inserts"]]
            native_group_colour(label)
            changed = retain_group(label + "-applied", expected)
            group_snapshot_ok(changed, baseline, expected, "Actual native " + label)
            if changed["state"]["revision"] != baseline["state"]["revision"] + 1:
                raise RuntimeError("Native group-colour action was not one canonical mutation")
            capture(main, "native-group-" + label + "-applied.png")
            chord(17, 90)
            undone = retain_group(label + "-one-undo", baseline["inserts"])
            group_snapshot_ok(undone, baseline, baseline["inserts"], "One native group colour Undo")
            chord(17, 89)
            redone = retain_group(label + "-one-redo", expected)
            group_snapshot_ok(redone, baseline, expected, "One native group colour Redo")
            capture(main, "native-group-" + label + "-redo.png")
        group_report.update(completionState="completed", explicitAndTheme=True,
                            oneUndoRedo=True, stableSelectionAndMarker=True,
                            dspSettlingAcknowledged=False)
        (output / "native-group-colour.json").write_text(json.dumps(group_report, indent=2), encoding="utf-8")
        evidence.append("PASS guarded native right-click Selection/display groups then stored member colour/theme0 actions; complete shared Mixer readback, stable group UUID/marker/selection/routes and one native Undo/Redo per branch. Owned screenshots retained for inspection; no DSP, listening, hardware or human-input qualification.")
    if selected_case in ("full", "reorder"):
        identity["nativeReorder"] = dict(completionState="pending", nativeInput=False,
                                         scope="actual owned keyboard and wheel; canonical order, not DSP")
        # Existing read-only bounds/state are sufficient: no endpoint sets order.
        # Explicit signatures also make affected-only mode independent of the
        # earlier full-suite chord() call on a 64-bit Windows keyboard layout.
        u.GetKeyboardLayout.argtypes=[w.DWORD];u.GetKeyboardLayout.restype=w.HANDLE
        u.MapVirtualKeyExW.argtypes=[w.UINT,w.UINT,w.HANDLE];u.MapVirtualKeyExW.restype=w.UINT
        def insert_order():
            observed=state(); value=observed.get("mixerOrder", {})
            actual=[item["index"] for item in rows()]
            if value.get("order")!=actual or observed["mixerRouting"].get("order")!=value:
                raise RuntimeError("Native reorder read-only order surfaces disagree")
            return actual
        def contents_by_id():
            names=("index","name","gain","pan","muted","colour","effectsBypassed","effects","routes")
            return {item["index"]:{name:item[name] for name in names} for item in rows()}
        def order_arrow(right):
            # key() supplies the required extended-arrow scan flag; chord()
            # was originally for ordinary letters and is intentionally unchanged.
            hold((18,))
            try:key(39 if right else 37)
            finally:release(())
            identity["nativeReorder"]["nativeInput"]=True
        def name_wheel(index, delta):
            global expected_cursor,active_tick
            if delta not in (-120,120):raise RuntimeError("One signed native wheel notch required")
            guard();target=screen(*control(index,"nameBounds"))
            point=w.POINT(round(target[0]),round(target[1]))
            if pid(u.WindowFromPoint(point))!=proc.pid:
                raise RuntimeError("Native reorder name is obscured; input paused")
            if not u.SetCursorPos(point.x,point.y):
                raise RuntimeError("Native reorder cursor positioning failed")
            expected_cursor=(point.x,point.y);active_tick=input_tick();time.sleep(.08);guard()
            hold((161,)) # Owned right Shift; existing guard still detects human left Shift.
            try:
                guard();u.mouse_event(0x800,0,0,c.c_ulong(delta & 0xffffffff),0)
                active_tick=input_tick();time.sleep(.2);guard()
            finally:release(())
            identity["nativeReorder"]["nativeInput"]=True
        def swapped(order,source,right):
            # Independent literal adjacent-row expectation, with Master fixed.
            result=list(order);position=result.index(source);destination=position+(1 if right else -1)
            while 0<=destination<len(result) and result[destination]==0:
                destination+=1 if right else -1
            if not 0<=destination<len(result):raise RuntimeError("Native fixture lacks adjacent non-Master row")
            result[position],result[destination]=result[destination],result[position]
            return result
        # Selection and maximization are existing navigation, never order edits.
        mouse(screen(*control(1,"nameBounds")))
        initial_state=state();initial=insert_order();baseline=contents_by_id()
        if initial_state["selectedInsert"]!=1 or 0 not in initial or 2 not in initial:
            raise RuntimeError("Native reorder fixture stable sources unavailable")
        master_position=initial.index(0)
        references={key:initial_state[key] for key in ("channels","selectedChannel","selectedPattern")}
        def require_order(expected,message,selected=1):
            observed=state();actual=insert_order()
            owner_thread=u.GetWindowThreadProcessId(main,None)
            observation=dict(step=message,expectedOrder=expected,actualOrder=actual,
                expectedSelected=selected,selectedInsert=observed.get("selectedInsert"),
                revision=observed.get("revision"),undoAvailable=observed.get("undoAvailable"),
                redoAvailable=observed.get("redoAvailable"),gestureActive=observed.get("gestureActive"),
                historyNotifications=observed.get("historyNotifications"),
                keyDispatches=observed.get("testKeyDispatches"),keyboardLayout=int(u.GetKeyboardLayout(owner_thread) or 0))
            identity["nativeReorder"].setdefault("observations",[]).append(observation)
            (output/"native-reorder-observations.json").write_text(json.dumps(identity["nativeReorder"]["observations"],indent=2),encoding="utf-8")
            if actual!=expected or actual.index(0)!=master_position:
                capture(main,"native-reorder-failure.png")
                raise RuntimeError(message+": unexpected order/Master row")
            if observed["selectedInsert"]!=selected or observed["mixerRouting"]["selectedInsert"]!=selected:
                raise RuntimeError(message+": selected/send source identity changed")
            if contents_by_id()!=baseline or any(observed[key]!=value for key,value in references.items()):
                raise RuntimeError(message+": per-ID controls, effects, routing or Channel references changed")
            if observed["gestureActive"]:
                raise RuntimeError(message+": discrete input left gesture active")
        right=swapped(initial,1,True)
        order_arrow(True);require_order(right,"Native Alt+Right")
        capture(main,"native-reorder-alt-right.png")
        chord(17,90);require_order(initial,"One native reorder Undo")
        chord(17,89);require_order(right,"One native reorder Redo")
        order_arrow(False);require_order(initial,"Native Alt+Left")
        # Hover a different source: wheel must not select it or retarget source1.
        wheeled=swapped(initial,2,True)
        name_wheel(2,-120);require_order(wheeled,"Native unselected-name Shift-wheel right")
        capture(main,"native-reorder-shift-wheel.png")
        chord(17,90);require_order(initial,"One native wheel Undo")
        chord(17,89);require_order(wheeled,"One native wheel Redo")
        name_wheel(2,120);require_order(initial,"Native Shift-wheel left")
        # The reserved Master is tested with real selection and real shortcuts.
        mouse(screen(*control(0,"nameBounds")));before_master=state()
        order_arrow(True);order_arrow(False)
        require_order(initial,"Native Master arrows refuse",selected=0)
        after_master=state()
        if any(after_master[key]!=before_master[key] for key in ("revision","dirty","undoAvailable","redoAvailable")):
            raise RuntimeError("Protected Master shortcuts changed history/document metadata")
        mouse(screen(*control(1,"nameBounds")))
        # A protected no-op did not add a hidden Undo transaction.
        chord(17,90);require_order(wheeled,"Master refusal added no native history")
        chord(17,89);require_order(initial,"Native prior wheel Redo after Master refusal")
        capture(main,"native-reorder-restored.png")
        identity["nativeReorder"].update(completionState="completed",initialOrder=initial,
            altRightOrder=right,wheelRightOrder=wheeled,restoredOrder=insert_order(),
            selectedSource=1,hoveredWheelSource=2,masterPosition=master_position,
            oneUndoRedo=True,perIdControlsAndReferencesPreserved=True,
            protectedMasterNoHistory=True)
        (output/"native-reorder.json").write_text(json.dumps(identity["nativeReorder"],indent=2),encoding="utf-8")
        evidence.append("PASS guarded native Alt+Right/Left, Ctrl+Z/Y, unselected-name right-Shift wheel both directions, stable per-ID gain/pan/mute/effect/send/Channel references and selected/send source, protected Master/no added history. Owned screenshots require inspection; no listening, hardware or DSP-adoption claim.")
    if selected_case in ("full", "overload"):
        # Requires an explicitly owned paced synthetic-driver fixture. The current
        # ordinary --mcp-test launch has NO audio device; refuse instead of deriving
        # live evidence from stopped/component meter data or opening user hardware.
        identity["nativeOverloadReset"]={"completionState":"pending","nativeInput":False}
        if "In-memory output" not in state()["audio"]["device"]:
            raise RuntimeError("Native overload reset pending: owned paced synthetic audio fixture unavailable; no hardware fallback")
        import wave
        source=output/"owned-overload-source.wav"
        with wave.open(str(source),"wb") as wav:
            wav.setnchannels(2);wav.setsampwidth(3);wav.setframerate(48000)
            wav.writeframes((6291456).to_bytes(3,"little",signed=True)*2*(48000*8)) # Original exact0.75 PCM.
        fresh=call("vibera_project",dict(action="new",template="empty",discardChanges=True))
        deadline=time.monotonic()+5
        while fresh.get("completionState")=="pending" and time.monotonic()<deadline:
            time.sleep(.03);fresh=call("vibera_project",dict(action="job",operation=fresh["operation"]))
        if fresh.get("completionState")!="completed":raise RuntimeError("Owned overload project creation did not complete")
        current=state();overload_channel=current["channels"][0]["id"];overload_pattern=current["selectedPattern"]
        command("channel.source",channel=overload_channel,text=str(source))
        identity["nativeOverloadReset"]["sourceProjection"]=await_sample_projection(
            state, lambda:call("vibera_inspect",dict(entity="channels",limit=512)),
            overload_channel, str(source), guard)
        command("channel.mixer",channel=overload_channel,mixer=1)
        command("channel.gain",channel=overload_channel,value=2.)
        command("mixer.gain",mixer=1,value=2.)
        command("mixer.gain",mixer=0,value=.1)
        command("notes.add",pattern=overload_pattern,channel=overload_channel,beat=0,length=4,pitch=60,value=1.)
        call("vibera_transport",dict(action="pattern"))
        deadline=time.monotonic()+5
        while state()["pendingAudioUpdate"] and time.monotonic()<deadline:time.sleep(.03)
        call("vibera_transport",dict(action="loop",enabled=True))
        call("vibera_transport",dict(action="play"))
        def meter():return next(item["meter"]for item in state()["mixerRouting"]["controls"]if item["index"]==1)
        def wait_meter(predicate,message):
            deadline=time.monotonic()+4
            while time.monotonic()<deadline:
                guard();value=meter()
                if value["available"] and not value["stale"] and predicate(value):return value
                time.sleep(.03)
            raise RuntimeError(message)
        clipped=wait_meter(lambda m:m["leftOverload"] and m["rightOverload"] and
                           m["leftPeak"]>=1 and m["rightPeak"]>=1,"Actual PCM did not latch stereo overload")
        # Prevent legitimate new/in-flight clipping from hiding a successful reset.
        # Transport and the synthetic callback driver continue throughout.
        command("channel.gain",channel=overload_channel,value=0.)
        quiet=wait_meter(lambda m:m["blocks"]>clipped["blocks"]+3 and
                         m["leftPeak"]<1 and m["rightPeak"]<1 and
                         m["leftOverload"] and m["rightOverload"],"Silent later callbacks did not retain actual overload")
        call("vibera_editor",dict(editor=3,action="show"))
        call("vibera_mixer",dict(action="select",insert=1))
        capture(main,"native-overload-held-during-playback.png")
        before_reset=state();before_meter=meter()
        model_before=[{k:v for k,v in item.items() if k!="meter"} for item in rows()]
        if not before_reset["playing"] or not before_reset["audio"]["headPlaying"]:
            raise RuntimeError("Native meter clear requires ongoing actual playback")
        mouse(screen(*control(1,"meterBounds")))
        identity["nativeOverloadReset"]["nativeInput"]=True
        cleared=wait_meter(lambda m:m["clearedThroughBlock"]>before_meter["clearedThroughBlock"] and
                           m["clearedThroughBlock"]>=before_meter["blocks"] and
                           m["clearedThroughBlock"]<=m["blocks"] and
                           not m["leftOverload"] and not m["rightOverload"],"Native meter click did not advance actual clear fence")
        after_reset=state()
        if any(after_reset[k]!=before_reset[k] for k in ("revision","dirty","undoAvailable","redoAvailable")):
            raise RuntimeError("Native overload clear changed document/history state")
        if model_before!=[{k:v for k,v in item.items() if k!="meter"} for item in rows()]:
            raise RuntimeError("Native overload clear changed Mixer gain/routing/model")
        if not after_reset["playing"] or not after_reset["audio"]["headPlaying"]:
            raise RuntimeError("Native overload clear stopped transport")
        if any(after_reset["audio"][k]!=before_reset["audio"][k]for k in ("device","replacements","samplerCreations")):
            raise RuntimeError("Native overload clear replaced audio resources")
        advanced=wait_meter(lambda m:m["blocks"]>cleared["blocks"]+3 and
                            not m["leftOverload"] and not m["rightOverload"],"Playback meter publication did not continue after clear")
        capture(main,"native-overload-cleared-during-playback.png")
        # The existing previous edit must still be the TOP history transaction.
        chord(17,90)
        if next(item["gain"]for item in state()["channels"]if item["id"]==overload_channel)!=2.:
            raise RuntimeError("Transient meter clear added an Undo transaction")
        chord(17,89)
        if next(item["gain"]for item in state()["channels"]if item["id"]==overload_channel)!=0.:
            raise RuntimeError("Existing gain Redo after transient meter clear")
        call("vibera_transport",dict(action="stop"))
        identity["nativeOverloadReset"].update(completionState="completed",clipped=clipped,quiet=quiet,
            before=before_meter,cleared=cleared,advanced=advanced,priorGainUndoRedo=True,
            modelUnchanged=True,revisionDirtyHistoryUnchanged=True,playbackContinued=True)
        (output/"native-overload-reset.json").write_text(json.dumps(identity["nativeOverloadReset"],indent=2),encoding="utf-8")
        evidence.append("PASS guarded Windows meter click during actual owned virtual playback; actual overload/clear fence and continuing block publication, unchanged Mixer model/revision/dirty/history/resources, prior gain Undo/Redo. Captures require inspection. No hardware/listening claim.")
    identity["passed"]=True
except Exception as error:
    identity["passed"]=False;identity["error"]=str(error)
    if identity.get("nativeGroupColour",{}).get("completionState")=="pending":
        identity["nativeGroupColour"].update(completionState="failed",error=str(error))
    if identity.get("nativeReorder",{}).get("completionState")=="pending":
        identity["nativeReorder"].update(completionState="failed",error=str(error))
    if identity.get("nativeOverloadReset",{}).get("completionState")=="pending":
        identity["nativeOverloadReset"].update(completionState="failed",error=str(error))
finally:
    if pressed_modifiers:release(())
    identity["forcedClose"]=False
    if proc.poll() is None:
        proc.stdin.close()
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            identity["forcedClose"]=True;proc.terminate();proc.wait(timeout=5)
    identity["exitCode"]=proc.returncode
    identity["seconds"]=time.monotonic()-started
    final_cursor=w.POINT(); final_cursor_ok = bool(u.GetCursorPos(c.byref(final_cursor)))
    if final_cursor_ok and expected_cursor is not None and (final_cursor.x,final_cursor.y)==expected_cursor and not any(u.GetAsyncKeyState(k)&0x8000 for k in (1,2,16,17,18,*range(32,91))):
        u.SetCursorPos(cursor.x,cursor.y)
    (output/"process.json").write_text(json.dumps(identity,indent=2),encoding="utf-8")
    if "nativeGroupColour" in identity:
        (output/"native-group-colour.json").write_text(json.dumps(identity["nativeGroupColour"],indent=2),encoding="utf-8")
    if "nativeReorder" in identity:
        (output/"native-reorder.json").write_text(json.dumps(identity["nativeReorder"],indent=2),encoding="utf-8")
    if "nativeOverloadReset" in identity:
        (output/"native-overload-reset.json").write_text(json.dumps(identity["nativeOverloadReset"],indent=2),encoding="utf-8")
    (output/"verification.txt").write_text("\n".join(evidence)+"\n",encoding="utf-8")
    (output/"stderr.log").write_text("".join(errors),encoding="utf-8")
print(json.dumps(identity))
sys.exit(0 if identity.get("passed") and identity["exitCode"]==0 and not identity["forcedClose"] else 1)
