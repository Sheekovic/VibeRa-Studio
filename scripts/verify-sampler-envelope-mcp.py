"""Real application envelope/LFO workflow, canonical history, persistence and WAV."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
import uuid
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
from dev_support import project_schema
app = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("admission", ROOT / "scripts/verify-plugin-io-mcp.py")
admission = importlib.util.module_from_spec(spec)
spec.loader.exec_module(admission)
admission.ordinary(app, False)
output_root = admission.owned_output_path(sys.argv[2], app)
output = output_root / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
spec = importlib.util.spec_from_file_location("neutral", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
neutral = importlib.util.module_from_spec(spec)
spec.loader.exec_module(neutral)
receipt = dict(passed=False, finished=False, checks=[], output=str(output),
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False)
began = time.monotonic()
client = None


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    receipt["checks"].append(label)


try:
    source = output / "original-envelope-tone.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((1, 2, 48000, 96000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<h", round(4500 * sum(
            math.sin(2 * math.pi * hz * frame / 48000) for hz in (220, 6000)))) for frame in range(96000)))
    client = neutral.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    initial = state()
    channel = initial["selectedChannel"]
    pattern = initial["selectedPattern"]
    settings = lambda: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
    target = lambda name: next(m for m in settings()["modulation"] if m["target"] == name)
    command("channel.source", channel=channel, text=str(source))
    command("notes.add", channel=channel, pattern=pattern, beat=0, length=3, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=0, length=4)
    check(len(settings()["modulation"]) == 5 and all(not m["enabled"] and not m["lfoEnabled"]
          and m["amount"] == m["lfoAmount"] == 0 for m in settings()["modulation"]), "Five neutral destinations preserve legacy projects")
    default_project = output / "default-modulation.vibra"
    default_saved = client.wait_job(data("vibera_project", dict(action="save", path=str(default_project), overwrite=False)))
    default_xml = ET.parse(default_project)
    default_modulations = default_xml.findall("./CHANNELS/CHANNEL/SAMPLER/MODULATION")
    check(default_saved["completionState"] == "completed" and
          len(default_modulations) == len(default_xml.findall("./CHANNELS/CHANNEL")) and
          all(m.attrib == {"stored": "0"} and len(m) == 0 for m in default_modulations),
          "Actual default project stores one compact stored0 empty modulation tree per Channel")
    instance = None
    for destination in ("volume", "pan", "cutoff", "resonance", "pitch"):
        command("sampler.modulation." + destination)
        window = next(w for w in state()["samplerWindows"] if w["channel"] == channel)
        check(window["modulationTarget"] == destination and (instance is None or window["instance"] == instance),
              "Canonical destination navigation reuses actual sampler window: " + destination)
        instance = window["instance"]
    parameters = {"enabled", "tempo", "delay", "attack", "hold", "decay", "sustain", "release",
                  "attackTension", "decayTension", "releaseTension", "amount", "lfoEnabled", "lfoTempo",
                  "lfoGlobal", "lfoDelay", "lfoAttack", "lfoPeriod", "lfoAmount", "lfoShape"}
    for parameter in sorted(parameters):
        operation = data("vibera_commands", dict(command="channel.sampler.modulation." + parameter,
                                                 includeSchemas=True))["operations"][0]
        schema = operation["arguments"]
        check(operation["supported"] and not schema["additionalProperties"] and
              "text" in schema["required"] and "channel" in schema["required"], "Closed canonical schema: " + parameter)
    def set_value(destination, parameter, value):
        args = dict(channel=channel, text=destination)
        args["enabled" if isinstance(value, bool) else "value"] = value
        return command("channel.sampler.modulation." + parameter, **args)
    def render(name):
        accepted = data("vibera_project", dict(action="export", path=str(output / name), overwrite=True))
        check(accepted["completionState"] == "pending", "WAV acceptance is not completion")
        check(client.wait_job(accepted)["completionState"] == "completed", "Actual WAV terminal completion")
        with wave.open(str(output / name), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000), "Real stereo24/48k WAV")
            return wav.readframes(wav.getnframes())
    dry = render("dry.wav")
    set_value("volume", "attack", .1)
    set_value("volume", "decay", 0)
    set_value("volume", "sustain", .25)
    set_value("volume", "amount", 1)
    set_value("volume", "enabled", True)
    shaped = render("volume-envelope.wav")
    def rms(pcm, begin, end, side=0):
        values = [int.from_bytes(pcm[i:i+3], "little", signed=True) / 8388608
                  for i in range(begin * 6 + side * 3, end * 6, 6)]
        return math.sqrt(sum(v*v for v in values) / len(values))
    check(.23 < rms(shaped, 12000, 24000) / rms(dry, 12000, 24000) < .27,
          "Actual full Volume envelope applies quarter-amplitude sustain")
    command("edit.undo")
    check(render("envelope-undone.wav") == dry, "Envelope-enable Undo restores exact legacy PCM")
    command("edit.redo")
    check(render("envelope-redone.wav") == shaped, "Envelope-enable Redo restores exact shaped PCM")
    selected = data("vibera_select", dict(pattern=pattern, channel=channel, scope=2))
    before = target("cutoff")
    lease = data("vibera_gesture", dict(action="begin", domain="samplerModulation"))
    for amount in (.25, -.75, .5):
        data("vibera_gesture", dict(action="update", operation=lease["operation"],
             command="channel.sampler.modulation.amount", args=dict(channel=channel, text="cutoff", value=amount)))
    data("vibera_gesture", dict(action="cancel", operation=lease["operation"]))
    check(target("cutoff") == before, "Continuous canonical cancellation restores complete destination state")
    macro_state = data("vibera_macros", dict(action="state"))
    recipe = dict(id="macro.envelope." + uuid.uuid4().hex, name="Original envelope test", steps=[
        dict(action="channel.sampler.modulation.lfoAmount", currentContext=False,
             parameters=dict(channel=channel, text="cutoff", value=-.25)),
        dict(action="channel.sampler.modulation.lfoEnabled", currentContext=False,
             parameters=dict(channel=channel, text="cutoff", enabled=True))])
    created = data("vibera_macros", dict(action="create", generation=macro_state["generation"], macro=recipe))
    check(created["configurationCommitted"] and created["profileMatchesMemory"], "Numeric/flag target macro is actually persisted")
    saved_recipe = data("vibera_macros", dict(action="state", id=recipe["id"]))
    ran = data("vibera_macros", dict(action="run", generation=saved_recipe["generation"], id=recipe["id"]))
    check(ran["completionState"] == "completed" and target("cutoff")["lfoEnabled"] and
          target("cutoff")["lfoAmount"] == -.25, "Actual saved macro executes both modulation parameter types")
    command("edit.undo")
    check(target("cutoff") == before, "Modulation macro is one complete Undo")
    command("edit.redo")
    check(target("cutoff")["lfoEnabled"] and target("cutoff")["lfoAmount"] == -.25, "Modulation macro exact Redo")
    command("edit.undo")
    current = data("vibera_macros", dict(action="state"))
    data("vibera_macros", dict(action="remove", generation=current["generation"], id=recipe["id"]))
    for destination in ("cutoff", "resonance", "pitch"):
        set_value(destination, "tempo", True)
        set_value(destination, "attack", .5)
        set_value(destination, "hold", .25)
        set_value(destination, "decayTension", -.5)
        set_value(destination, "lfoTempo", True)
        set_value(destination, "lfoPeriod", 2)
        set_value(destination, "lfoGlobal", True)
        set_value(destination, "lfoShape", 1)
        check(target(destination)["lfoPeriod"] == 2 and target(destination)["tempo"], "Musical tempo/global/shape state: " + destination)
    for args in (dict(channel=channel, text="pan", value=.2), dict(channel=channel, text="unknown", value=.2),
                 dict(channel=channel, text="cutoff", value=True), dict(channel=channel, text="cutoff", value=65),
                 dict(channel=999999999, text="cutoff", value=.2)):
        revision = state()["revision"]
        result = client.call("vibera_command", dict(command="channel.sampler.modulation.attack", args=args), allow_failure=True)
        check(result.get("isError", False), "Invalid input refuses with a structured error")
        check(state()["revision"] == revision,
              "Invalid input leaves history/revision unchanged")
    project = output / "envelopes.vibra"
    saved = client.wait_job(data("vibera_project", dict(action="save", path=str(project), overwrite=True)))
    check(saved["completionState"] == "completed" and project.is_file(), "Save has real terminal file outcome")
    expected = settings()["modulation"]
    configured_xml = ET.parse(project)
    configured_modulation = configured_xml.find("./CHANNELS/CHANNEL[@id='" + str(channel) + "']/SAMPLER/MODULATION")
    check(configured_modulation is not None and configured_modulation.attrib == {"stored": "1"} and
          [m.get("id") for m in configured_modulation] == ["volume", "pan", "cutoff", "resonance", "pitch"] and
          all(m.tag == "TARGET" and len(m.attrib) == 21 and len(m) == 0 for m in configured_modulation),
          "Actual configured Channel stores all five complete20-parameter rows with stored1")
    before_reopen = render("before-reopen.wav")
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
    check(settings()["modulation"] == expected and render("after-reopen.wav") == before_reopen,
          "Actual save/reopen preserves all destination state and exact rendered sound")
    legacy_expanded = ET.parse(project)
    for m in legacy_expanded.findall("./CHANNELS/CHANNEL/SAMPLER/MODULATION"):
        if m.get("stored") == "1":
            m.attrib.pop("stored")  # Earlier development schema37's complete expanded Channel layout.
    legacy_expanded.getroot().set("version", "37")  # Genuine historical format, not current schema.
    legacy_project = output / "legacy-expanded37.vibra"
    legacy_expanded.write(legacy_project, encoding="utf-8", xml_declaration=True)
    legacy_opened = client.wait_job(data("vibera_project", dict(action="open", path=str(legacy_project), discardChanges=True)))
    check(legacy_opened["completionState"] == "completed" and settings()["modulation"] == expected and
          render("legacy-expanded37.wav") == before_reopen,
          "Earlier attribute-free expanded37 Channel settings reopen with exact state and actual WAV")
    tree = ET.parse(project)
    check(int(tree.getroot().get("version")) == project_schema(ROOT),
          "Saved modulation project uses the authoritative current schema")
    tree.find(".//MODULATION/TARGET").set("attack", "65")
    invalid = output / "invalid-envelope.vibra"
    tree.write(invalid, encoding="utf-8", xml_declaration=True)
    result = client.call("vibera_project", dict(action="open", path=str(invalid), discardChanges=True), allow_failure=True)["structuredContent"]
    if result["completionState"] == "pending":
        deadline = time.monotonic() + 10
        while result["completionState"] == "pending" and time.monotonic() < deadline:
            time.sleep(.03)
            result = client.call("vibera_project", dict(action="job", operation=result["operation"]), allow_failure=True)["structuredContent"]
    check(result["completionState"] == "failed" and settings()["modulation"] == expected,
          "Actual malformed-project failure is terminal and preserves the live project")
    for fault in ("flag-text", "default-with-rows", "stored-without-rows"):
        malformed = ET.parse(project)
        m = malformed.find("./CHANNELS/CHANNEL[@id='" + str(channel) + "']/SAMPLER/MODULATION")
        if fault == "stored-without-rows":
            for row in list(m):
                m.remove(row)
        else:
            m.set("stored", "false" if fault == "flag-text" else "0")
        path = output / ("invalid-compact-" + fault + ".vibra")
        malformed.write(path, encoding="utf-8", xml_declaration=True)
        revision = state()["revision"]
        result = client.call("vibera_project", dict(action="open", path=str(path), discardChanges=True), allow_failure=True)["structuredContent"]
        if result["completionState"] == "pending":
            deadline = time.monotonic() + 10
            while result["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.03)
                result = client.call("vibera_project", dict(action="job", operation=result["operation"]), allow_failure=True)["structuredContent"]
        check(result["completionState"] == "failed" and settings()["modulation"] == expected and
              state()["revision"] == revision, "Strict compact modulation failure preserves actual state/history: " + fault)
    # A non-volume envelope must release its modulation without truncating a
    # one-shot source. Keep the original long-note sustain oracles above intact.
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    fresh = state()
    channel, pattern = fresh["selectedChannel"], fresh["selectedPattern"]
    command("channel.source", channel=channel, text=str(source))
    command("channel.sampler.filterType", channel=channel, value=0)
    command("notes.add", channel=channel, pattern=pattern, beat=0, length=.125, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=0, length=4)
    one_shot = render("one-shot-baseline.wav")
    baseline_level = rms(one_shot, 9600, 16800)
    check(baseline_level > .01, "Original one-shot sample continues after the short score note")
    for destination in ("cutoff", "resonance", "pitch"):
        set_value(destination, "amount", .125)
        set_value(destination, "enabled", True)
        actual = render("one-shot-" + destination + ".wav")
        check(rms(actual, 9600, 16800) > baseline_level * .8,
              "Non-volume envelope retains one-shot source after key-off: " + destination)
        if destination != "pitch":
            check(actual == one_shot, "Filter-Off one-shot remains exact unaffected PCM: " + destination)
        set_value(destination, "enabled", False)
    set_value("volume", "amount", 1)
    set_value("volume", "release", .02)
    set_value("volume", "enabled", True)
    check(rms(render("one-shot-volume-gate.wav"), 9600, 16800) < 1.e-6,
          "Volume envelope still gates the same one-shot source at actual note-off")
    receipt["passed"] = True
except Exception as error:
    receipt["failure"] = str(error)
finally:
    if client:
        try:
            client.close()
            receipt["exitCode"] = client.process.returncode
        except Exception as error:
            receipt["passed"] = False
            receipt["closeError"] = str(error)
    receipt["finished"] = True
    receipt["seconds"] = time.monotonic() - began
    for path in (output / "verification.json", output_root / "verification.json"):
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(passed=receipt["passed"], checks=len(receipt["checks"]), output=str(output))))
raise SystemExit(0 if receipt["passed"] else 1)
