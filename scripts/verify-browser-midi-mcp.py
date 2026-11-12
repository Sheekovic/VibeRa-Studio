"""Browser tags/preview, MIDI input device and Channel sample preview through MCP (isolated --mcp-test instance).

browser.tagFilter, browser.setTags and browser.previewSeek (vibera_editor editor 4) run the same Browser
calls as clicking a tag, the Edit tags dialog and Shift+click on the preview waveform. midi.device
(vibera_transport midiInput device) is the Settings > MIDI device list; only closing the input and
refusing an unknown identifier are exercised, no hardware port is opened. channel.previewSample
(vibera_transport previewSample) is the Channel settings Preview button; drumPads.hit (vibera_transport
drumPad) is striking a Drum Pads pad. vibera_controller learn / cancel / preset / ports are the Settings >
Controller buttons (no hardware port is opened). Not a listening or hardware test.

usage: python scripts/verify-browser-midi-mcp.py [executable] [output folder]
"""
import array
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/browser-midi"
output = base / ("attempt-" + uuid.uuid4().hex)
library = output / "library"
library.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


for name in ("kick.wav", "hat.wav"):
    with wave.open(str(library / name), "wb") as w:
        w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        w.writeframes(array.array("h", [int(12000 * math.sin(2 * math.pi * 110 * i / 48000)) for i in range(48000)]).tobytes())

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    raw = lambda tool, args: client.call(tool, args, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args or {}).get("structuredContent", {})
    browser = lambda: call("vibera_state").get("browser", {})
    editor = lambda action, **a: raw("vibera_editor", dict(editor=4, action=action, **a))
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})

    check(not editor("browserFolder", path=str(library), remember=False).get("isError"), "Browser opens the test library")
    check(not editor("browserSetTags", path=str(library / "kick.wav"), tags=["Drum", "Low"]).get("isError"),
          "browser.setTags tags kick.wav (Drum, Low)")
    check({"Drum", "Low"} <= set(browser().get("tags", [])), f"The tags are known ({browser().get('tags')})")
    check(not editor("browserTagFilter", tag="Drum").get("isError") and browser().get("tagFilter") == ["Drum"],
          "browser.tagFilter selects Drum")
    check(not editor("browserTagFilter", tag="Drum").get("isError") and browser().get("tagFilter") == [],
          "A second click clears it")
    refused = editor("browserTagFilter", tag="Nope")
    check(refused.get("isError") and "unknownTag" in json.dumps(refused), "An unknown tag is refused (browser.unknownTag)")
    check(editor("browserSetTags", path=str(library / "none.wav"), tags=["x"]).get("isError"), "Tagging a missing file is refused")
    check(raw("vibera_editor", {"editor": 2, "action": "browserTagFilter", "tag": "Drum"}).get("isError"),
          "Browser actions need editor 4")

    check(not editor("browserTagSearch", text="dr").get("isError") and browser().get("visibleTags") == ["Drum"]
          and browser().get("tagSearch") == "dr", f"browser.tagSearch narrows the tag list ({browser().get('visibleTags')})")
    editor("browserTagSearch", text="")
    check({"Drum", "Low"} <= set(browser().get("visibleTags", [])), "An empty search shows every tag")

    # view.scroll: the editors' own scroll bars (Playlist zooms horizontally; the others only move).
    views = lambda: call("vibera_state")["editorViews"]
    scroll = lambda index, **a: raw("vibera_editor", dict(editor=index, action="scroll", **a))
    check(len(views()) == 4 and len(views()[2]["horizontal"]) == 3, f"Editor views read back ({views()[2]})")
    check(not scroll(2, start=8).get("isError") and abs(views()[2]["horizontal"][0] - 8) < 1e-6, "view.scroll moves the Playlist to beat 8")
    check(not scroll(2, start=4, span=16).get("isError") and abs(views()[2]["horizontal"][1] - 16) < .05
          and abs(views()[2]["horizontal"][0] - 4) < 1e-6, f"A span zooms the Playlist to 16 beats ({views()[2]['horizontal']})")
    piano_before = views()[1]["vertical"]
    target = max(0, piano_before[0] - 6)
    check(not scroll(1, vertical=True, start=target).get("isError") and abs(views()[1]["vertical"][0] - target) <= 1,
          f"The Piano roll scrolls vertically ({piano_before} -> {views()[1]['vertical']})")
    check(scroll(2, start=-1).get("isError"), "A negative start is refused")
    check(scroll(4, start=0).get("isError"), "The Browser has no editor scroll bars")

    serial = browser().get("previewSerial")
    check(editor("browserPreviewSeek", position=.5).get("isError") or browser().get("previewReady"),
          "Seeking with no sample previewed is refused")
    channels = call("vibera_inspect", {"entity": "channels", "limit": 100})["items"]
    sample = next(c for c in channels if c.get("source"))
    instrument = next((c for c in channels if not c.get("source")), None)
    check(not raw("vibera_transport", {"action": "previewSample", "channel": sample["id"]}).get("isError"),
          f"channel.previewSample previews {sample.get('name')}")
    for _ in range(50):
        if browser().get("previewReady"):
            break
        time.sleep(.1)
    check(browser().get("previewReady"), "The Channel's sample becomes the ready preview")
    check(not editor("browserPreviewSeek", position=.5).get("isError") and browser().get("previewSerial") != serial,
          "browser.previewSeek restarts the preview (new preview serial)")
    if instrument:
        check(raw("vibera_transport", {"action": "previewSample", "channel": instrument["id"]}).get("isError"),
              "A Channel without a sample is refused")

    # Sampler waveform view (sampler.view.left / right) and the Keyboard button of the open settings window.
    call("vibera_select", {"channel": sample["id"]})
    call("vibera_command", {"command": "channel.openInstrument"})
    command = lambda name: call("vibera_command", {"command": name})
    view = lambda: call("vibera_state").get("samplerView", {})
    check(view().get("channel") == sample["id"] and view().get("span") == 1, f"The sampler view reads back ({view()})")
    command("sampler.view.zoomIn")
    centred = view()
    check(abs(centred["span"] - .5) < 1e-9 and abs(centred["start"] - .25) < 1e-9, f"Zoom in halves the view ({centred})")
    command("sampler.view.right")
    check(abs(view()["start"] - .375) < 1e-9, f"sampler.view.right moves a quarter view right ({view()})")
    command("sampler.view.left")
    command("sampler.view.left")
    check(abs(view()["start"] - .125) < 1e-9, f"sampler.view.left moves back ({view()})")
    command("sampler.view.all")
    shown = command("sampler.showKeyboard")
    check(shown.get("completionState") in ("completed", "failed"), f"sampler.showKeyboard reports its outcome ({shown.get('completionState')})")

    # Picker / Mixer: select an insert (mixer.insert.select).
    check(not raw("vibera_mixer", {"action": "select", "insert": 1}).get("isError")
          and call("vibera_state").get("selectedInsert") == 1, "mixer.insert.select selects insert 1")

    # Drum Pads: pad n plays the n-th sampler Channel; striking selects it and holds its root key.
    pads = call("vibera_state")["drumPads"]
    check(pads["held"] == 0 and len(pads["channels"]) >= 2, f"Drum Pads list the sampler Channels ({pads})")
    check(not raw("vibera_transport", {"action": "drumPad", "pad": 2, "velocity": .5}).get("isError"), "drumPads.hit strikes pad 2")
    state = call("vibera_state")
    check(state["drumPads"]["held"] == 2 and state["selectedChannel"] == pads["channels"][1],
          "Pad 2 is held and its Channel becomes the selected input target")
    check(not raw("vibera_transport", {"action": "drumPad", "on": False}).get("isError")
          and call("vibera_state")["drumPads"]["held"] == 0, "Releasing the pad ends the held note")
    if len(pads["channels"]) < 16:
        empty = raw("vibera_transport", {"action": "drumPad", "pad": 16})
        check(empty.get("isError") and "emptyPad" in json.dumps(empty), "A pad without a Channel is refused")

    # Typing keys right-click menu: root octave and velocity.
    check(not raw("vibera_transport", {"action": "typing", "typingRoot": 3, "typingVelocity": 64}).get("isError"),
          "midi.typingOptions sets C3 and velocity 64")
    typing = call("vibera_state")["typingKeys"]
    check(typing["root"] == 36 and typing["velocity"] == 64, f"Typing keys read back ({typing})")
    check(raw("vibera_transport", {"action": "typing", "typingRoot": 9}).get("isError"), "An octave above C7 is refused")
    raw("vibera_transport", {"action": "typing", "typingRoot": 5, "typingVelocity": 100})

    # Audio input menu (recording.audioInput): readback and forgetting recovered takes.
    audio_state = call("vibera_audioRecording", {"action": "state"})
    check("enabled" in audio_state and "recovered" in audio_state, "recording.audioInput reads back the input options")
    check(call("vibera_audioRecording", {"action": "forgetRecovered"}).get("recovered") == [],
          "Forget the list empties the recovered takes")

    # CoProducer MIDI ideas: Models... / Runtime... (ai.idea.configure installation / runtime).
    call("vibera_ai", {"action": "panel", "visible": True, "tab": "midi"})
    for _ in range(100):  # Opening the panel reads the project context; configure is refused while busy.
        opened = call("vibera_ai", {"action": "ideaState"})
        if not opened.get("idea", opened).get("busy"):
            break
        time.sleep(.1)
    configured = raw("vibera_ai", {"action": "ideaConfigure", "installation": str(library), "runtime": sys.executable})
    check(not configured.get("isError"), "ai.idea.configure takes an installation folder and a runtime: " + json.dumps(configured)[:160])
    idea = call("vibera_ai", {"action": "ideaState"})
    idea = idea.get("idea", idea)
    check(Path(idea.get("installation", "")) == library and Path(idea.get("runtime", "")) == Path(sys.executable),
          f"ideaState reads both back ({idea.get('installation')}, {idea.get('runtime')})")
    check(raw("vibera_ai", {"action": "ideaConfigure", "installation": str(library / "missing")}).get("isError"),
          "A missing installation folder is refused")

    # Settings > Controller buttons: Learn / Cancel, Load preset (replace:true) and control ports.
    # No hardware port is opened here: only closing the ports and refusals are exercised.
    controller = lambda action, **a: raw("vibera_controller", dict(action=action, **a))
    ports = call("vibera_controller", {"action": "state"})
    check(isinstance(ports.get("controllerPorts", {}).get("inputs"), list) and ports.get("controllerLearning") == -1,
          "Controller ports and Learn state read back")
    refused = controller("preset")
    check(refused.get("isError") and "confirmationRequired" in json.dumps(refused), "controller.preset needs replace:true")
    loaded = controller("preset", replace=True)
    rows = len(loaded.get("structuredContent", {}).get("controllerProfile", {}).get("bindings", []))
    check(not loaded.get("isError") and rows > 0, f"controller.preset loads the Launchkey mappings ({rows} rows)")
    armed = controller("learn", index=0)
    check(not armed.get("isError") and armed["structuredContent"]["controllerLearning"] == 0, "controller.learn arms row 0")
    check(controller("cancel")["structuredContent"]["controllerLearning"] == -1, "Cancel disarms MIDI Learn")
    check(controller("learn", index=rows).get("isError"), "Learn on a missing row is refused")
    unknown_port = controller("ports", input="no-such-port-" + uuid.uuid4().hex)
    check(unknown_port.get("isError") and "unknownPort" in json.dumps(unknown_port), "An unknown control port is refused")
    closed = controller("ports", input="", output="")
    check(not closed.get("isError") and closed["structuredContent"]["controllerPorts"]["input"] == "",
          "controller.ports with empty identifiers turns the control ports off")

    raw("vibera_transport", {"action": "midiInput"})  # Rescan: the device list is only refreshed by this action.
    midi = call("vibera_state").get("midiInput", {})
    check(isinstance(midi.get("devices"), list) and "device" in midi, f"MIDI devices read back ({len(midi.get('devices', []))})")
    check(not raw("vibera_transport", {"action": "midiInput", "device": ""}).get("isError")
          and call("vibera_state")["midiInput"]["device"] == "", "midi.device with no identifier closes the input")
    unknown = raw("vibera_transport", {"action": "midiInput", "device": "no-such-device-" + uuid.uuid4().hex})
    check(unknown.get("isError") and "unknownDevice" in json.dumps(unknown), "An unknown MIDI device is refused")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
