"""Actual note modes/Properties/history/project/MIDI/WAV through production stdio.

Original fixtures; no native input, listening, hardware or exact FL curve claim.
"""
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


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


admission = load("note_mode_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
neutral = load("note_mode_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
app = Path(sys.argv[1]).resolve()
admission.ordinary(app, False)
output_root = admission.owned_output_path(sys.argv[2], app)
output = output_root / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
receipt = dict(passed=False, finished=False, checks=[], output=str(output),
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               verifierSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False, callbackTiming=False)
started = time.monotonic()
client = None


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    receipt["checks"].append(label)


try:
    source = output / "original440.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((1, 2, 48000, 192000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<h", round(6000 * math.sin(2 * math.pi * 440 * i / 48000)))
                                 for i in range(192000)))
    client = neutral.VibeRaClient(app)
    # Control actual boundary bytes: Windows text pipes otherwise expand LF
    # into CRLF, adding one byte before the server's LF delimiter.
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    modes = lambda action, **args: data("vibera_note_modes", dict(action=action, **args))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    s = state()
    channel, pattern = s["selectedChannel"], s["selectedPattern"]
    target = dict(pattern=pattern, channel=channel)

    def padding_request(byte_count):
        frame = dict(jsonrpc="2.0", id=client.serial + 1, method="ping", params=dict(padding=""))
        size = len(json.dumps(frame).encode("utf-8"))
        return dict(padding="x" * (byte_count - size))

    check(client.request("ping", padding_request(65536)) == {},
          "Actual exact65536-byte request remains supported across multiple pipe reads")
    rejected = ""
    try:
        client.request("ping", padding_request(65537))
    except RuntimeError as error:
        rejected = str(error)
    check("request_too_large" in rejected and "65536" in rejected and "-32600" in rejected,
          "Actual65537-byte request receives structured refusal instead of closing the application")
    check(client.request("ping", {}) == {} and state()["revision"] == s["revision"],
          "Next valid request succeeds after oversized frame with unchanged project revision")
    check("vibera_note_modes" in {t["name"] for t in client.request("tools/list", {})["tools"]},
          "Production discovery includes the shared Piano Roll modes/Properties tool")
    for name in ("notes.slide", "notes.portamento", "notes.colourGroup", "notes.properties"):
        op = data("vibera_commands", dict(command=name, includeSchemas=True))["operations"][0]
        check(op["supported"] and op["mapping"] == "vibera_command" and
              not op["arguments"]["additionalProperties"], "Typed canonical operation: " + name)
    command("channel.source", channel=channel, text=str(source))
    command("channel.sampler.root", channel=channel, value=60)
    command("channel.sampler.envelope", channel=channel, enabled=True)
    for name, value in (("attack", 0), ("decay", 0), ("sustain", 1), ("release", .02)):
        command("channel.sampler." + name, channel=channel, value=value)
    command("notes.add", **target, beat=0, length=3, pitch=60, value=.8)
    first = state()["notes"][0]["id"]
    command("notes.properties", **target, ids=[first], noteProperties=dict(pan=-1))
    command("notes.add", **target, beat=0, length=3, pitch=67, value=.5, colourGroup=1)
    other = next(n["id"] for n in state()["notes"] if n["id"] != first)
    command("notes.properties", **target, ids=[other], noteProperties=dict(pan=1))
    command("clip.add", pattern=pattern, beat=0, length=4)

    def render(name):
        path = output / name
        pending = data("vibera_project", dict(action="export", path=str(path), overwrite=False))
        check(pending["completionState"] == "pending", "Actual WAV request reports pending")
        check(client.wait_job(pending)["completionState"] == "completed" and path.is_file(),
              "Actual WAV job reports completed only after file creation")
        with wave.open(str(path), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                  "Real stereo24bit48k Song WAV")
            raw = wav.readframes(wav.getnframes())
        return [[int.from_bytes(raw[i + c * 3:i + c * 3 + 3], "little", signed=True)
                 for i in range(0, len(raw), 6)] for c in range(2)]

    def frequency(samples, start, end):
        a, b = round(start * 48000), round(end * 48000)
        return sum(samples[i] <= 0 < samples[i + 1] for i in range(a, b - 1)) / (end - start)

    baseline = render("ordinary.wav")
    check(abs(frequency(baseline[0], .1, .3) - 440) < 8, "Independent original440Hz baseline")
    command("notes.add", **target, beat=.5, length=.5, pitch=72, value=.2, slide=True)
    slide = next(n["id"] for n in state()["notes"] if n["slide"])
    command("notes.properties", **target, ids=[slide], noteProperties=dict(pan=-1))
    slid = render("slide.wav")
    check(abs(frequency(slid[0], .65, .85) - 880) < 8 and slid[1] == baseline[1],
          "Silent Slide reaches880Hz while another color retains literal right PCM")
    check(len(state()["notes"]) == 3 and next(n for n in state()["notes"] if n["id"] == slide)["slide"],
          "Silent control remains explicit source metadata")
    command("notes.slide", **target, ids=[slide], enabled=False)
    command("edit.undo")
    check(render("slide-undo.wav") == slid, "Slide Undo restores exact musical WAV")
    command("edit.redo")
    check(not next(n for n in state()["notes"] if n["id"] == slide)["slide"],
          "Mode Redo restores intended raw classification")
    command("edit.undo")
    before = state()
    for name, args in (("notes.colourGroup", dict(ids=[first], value=16)),
                       ("notes.properties", dict(ids=[first, other], noteProperties=dict(beat=1))),
                       ("notes.slide", dict(ids=[first, first], enabled=True))):
        result = client.call("vibera_command", dict(command=name, args=dict(target, **args)),
                             allow_failure=True)
        check(result.get("isError") and state()["revision"] == before["revision"] and
              state()["notes"] == before["notes"], "Invalid input refuses without mutation: " + name)
    configured = modes("configure", slide=True, portamento=True, colourGroup=15)
    check(configured["slide"] and configured["portamento"] and configured["colourGroup"] == 15 and
          state()["revision"] == before["revision"], "Insertion modes are session state, not score history")
    colour_only = modes("configure", colourGroup=2)
    check(colour_only["slide"] and colour_only["portamento"] and colour_only["colourGroup"] == 2,
          "Changing only insertion color preserves both requested mode flags")
    opened = modes("propertiesOpen", **target, ids=[first])
    token = opened["operation"]
    check(opened["completionState"] == "pending", "Properties session is pending until explicit Apply/Cancel")
    modes("propertiesUpdate", operation=token, noteProperties=dict(velocity=.4, portamento=True))
    check(state()["notes"] == before["notes"], "Staged Properties cannot silently mutate playback score")
    modes("propertiesReset", operation=token)
    modes("propertiesUpdate", operation=token, noteProperties=dict(velocity=.4, portamento=True))
    applied = modes("propertiesApply", operation=token)
    check(applied["completionState"] == "completed" and
          next(n for n in state()["notes"] if n["id"] == first)["portamento"],
          "Properties Apply runs the actual atomic canonical edit")
    command("edit.undo")
    check(state()["notes"] == before["notes"], "One Properties Undo restores all changed fields")
    command("edit.redo")
    opened = modes("propertiesOpen", **target, ids=[first])
    token = opened["operation"]
    modes("propertiesUpdate", operation=token, noteProperties=dict(pitch=61))
    cancelled = modes("propertiesCancel", operation=token)
    check(cancelled["completionState"] == "cancelled" and
          next(n for n in state()["notes"] if n["id"] == first)["pitch"] == 60,
          "Properties cancellation retains document and reports cancelled")
    saved = output / "modes.vibra"
    snapshot = state()["notes"]
    client.wait_job(data("vibera_project", dict(action="save", path=str(saved), overwrite=False)))
    before_reopen = render("before-reopen.wav")
    client.wait_job(data("vibera_project", dict(action="open", path=str(saved), discardChanges=True)))
    check(state()["notes"] == snapshot and render("after-reopen.wav") == before_reopen,
          "Actual saved project retains mode/color metadata and exact musical PCM")
    unused = next(c["id"] for c in state()["channels"] if c["id"] != channel)
    data("vibera_select", dict(pattern=pattern, channel=unused, scope=2))
    failed_midi = output / "empty-channel-refused.mid"
    failed = data("vibera_project", dict(action="exportMidi", path=str(failed_midi), overwrite=False))
    check(failed["completionState"] == "pending", "Empty-source MIDI request reports accepted, not success")
    deadline = time.monotonic() + 10
    while failed["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        failed = client.call("vibera_project", dict(action="job", operation=failed["operation"]),
                             allow_failure=True)["structuredContent"]
    check(failed["completionState"] == "failed" and not failed_midi.exists() and
          failed["midiSlideNotesOmitted"] is None and failed["expressionNotesOmitted"] is None and
          state()["notes"] == snapshot, "Actual async MIDI failure retains score and unknown omission counts")
    data("vibera_select", dict(pattern=pattern, channel=channel, scope=2))
    midi = output / "ordinary-with-mode-omissions.mid"
    job = data("vibera_project", dict(action="exportMidi", path=str(midi), overwrite=False))
    check(job["completionState"] == "pending", "MIDI export acceptance is not completion")
    complete = client.wait_job(job)
    check(complete["midiSlideNotesOmitted"] == 1 and complete["midiPortamentoNotesOmitted"] == 1 and
          complete["expressionNotesOmitted"] == 3 and midi.is_file(),
          "Actual MIDI export skips Slide, truthfully reports Porta and expression omissions")
    malformed = output / "invalid-group.vibra"
    tree = ET.parse(saved)
    next(n for n in tree.iter("NOTE") if n.get("id") == str(first)).set("colourGroup", "16")
    tree.write(malformed, encoding="utf-8", xml_declaration=True)
    revision = state()["revision"]
    result = client.call("vibera_project", dict(action="open", path=str(malformed), discardChanges=True),
                         allow_failure=True)["structuredContent"]
    deadline = time.monotonic() + 10
    while result["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        result = client.call("vibera_project", dict(action="job", operation=result["operation"]),
                             allow_failure=True)["structuredContent"]
    # Note data must be intact; on-screen geometry (pianoBounds) may follow a view scroll.
    def note_data(notes):
        return [{k: v for k, v in note.items() if k != "pianoBounds"} for note in notes]
    check(result["completionState"] == "failed" and state()["revision"] == revision and
          note_data(state()["notes"]) == note_data(snapshot),
          "Malformed saved mode data fails terminally without live data loss")

    # Real wire acceptance beyond the former1024-ID decoder limit. No render of
    # this bulk fixture is attempted; its source/history/selection are the subject.
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    s = state()
    channel, pattern = s["selectedChannel"], s["selectedPattern"]
    target = dict(channel=channel, pattern=pattern)
    for start in range(0, 1050, 128):
        command("notes.import", **target, recordedNotes=[dict(channel=channel, beat=i / 16, length=.2,
                                                               pitch=48 + i % 24, velocity=.5)
                                                        for i in range(start, min(start + 128, 1050))])

    def all_notes():
        rows = []
        for offset in range(0, 1050, 512):
            rows.extend(data("vibera_inspect", dict(entity="notes", pattern=pattern,
                                                    offset=offset, limit=512))["items"])
        return rows

    bulk = all_notes()
    check(len(bulk) == 1050, "Actual bulk fixture has1050 paginated stable source IDs")
    reversed_ids = [n["id"] for n in reversed(bulk)]
    for name, args, field, expected in (
            ("notes.colourGroup", dict(value=3), "colourGroup", 3),
            ("notes.slide", dict(enabled=True), "slide", True),
            ("notes.portamento", dict(enabled=True), "portamento", True),
            ("notes.properties", dict(noteProperties=dict(velocity=.375)), "velocity", .375)):
        original = all_notes()
        command(name, **target, ids=reversed_ids, **args)
        changed = all_notes()
        check(all(n[field] == expected for n in changed), "Actual1050-ID command/readback: " + name)
        command("edit.undo")
        check(all_notes() == original, "Exact bulk Undo: " + name)
        command("edit.redo")
        check(all_notes() == changed, "Exact bulk Redo: " + name)
        command("edit.undo")
    opened = modes("propertiesOpen", **target, ids=reversed_ids)
    properties = opened["properties"]
    check(properties["ids"] == reversed_ids and
          [n["id"] for n in properties["baselineNotes"]] == reversed_ids and
          properties["baseline"]["pitch"] == bulk[-1]["pitch"],
          "Indexed Properties capture preserves1050 requested IDs and first baseline order")
    revision = state()["revision"]
    check(modes("propertiesCancel", operation=opened["operation"])["completionState"] == "cancelled" and
          state()["revision"] == revision and all_notes() == bulk,
          "Bulk Properties cancellation preserves score and history")
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
    receipt["seconds"] = time.monotonic() - started
    for path in (output / "verification.json", output_root / "verification.json"):
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(passed=receipt["passed"], checks=len(receipt["checks"]), output=str(output))))
raise SystemExit(0 if receipt["passed"] else 1)
