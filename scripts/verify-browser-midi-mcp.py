"""Browser tags/preview, MIDI input device and Channel sample preview through MCP (isolated --mcp-test instance).

browser.tagFilter, browser.setTags and browser.previewSeek (vibera_editor editor 4) run the same Browser
calls as clicking a tag, the Edit tags dialog and Shift+click on the preview waveform. midi.device
(vibera_transport midiInput device) is the Settings > MIDI device list; only closing the input and
refusing an unknown identifier are exercised, no hardware port is opened. channel.previewSample
(vibera_transport previewSample) is the Channel settings Preview button; drumPads.hit (vibera_transport
drumPad) is striking a Drum Pads pad. Not a listening test.

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
    configured = raw("vibera_ai", {"action": "ideaConfigure", "installation": str(library), "runtime": sys.executable})
    check(not configured.get("isError"), "ai.idea.configure takes an installation folder and a runtime: " + json.dumps(configured)[:160])
    idea = call("vibera_ai", {"action": "ideaState"})
    idea = idea.get("idea", idea)
    check(Path(idea.get("installation", "")) == library and Path(idea.get("runtime", "")) == Path(sys.executable),
          f"ideaState reads both back ({idea.get('installation')}, {idea.get('runtime')})")
    check(raw("vibera_ai", {"action": "ideaConfigure", "installation": str(library / "missing")}).get("isError"),
          "A missing installation folder is refused")

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
