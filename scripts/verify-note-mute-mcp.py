"""Actual semantic Note mute, history, leased strokes, project, WAV and MIDI checks.

No pointer simulation, native-input, listening or hardware claim.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from dev_support import project_schema
import time
import wave

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():
    client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("neutral", client_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0,
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False)
began = time.monotonic()
client = None


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    source = output / "pulse.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((2, 2, 48000, 4800, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<hh", 3000 if i%100<50 else -3000, 0) for i in range(4800)))
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    pattern=state()["selectedPattern"];channel,ghost=[c["id"] for c in state()["channels"][:2]]
    notes=lambda:data("vibera_inspect",dict(entity="notes",pattern=pattern))["items"]
    command("channel.source",channel=channel,text=str(source))
    for beat,pitch in [(0,60),(.5,62),(1,64)]:
        command("notes.add",pattern=pattern,channel=channel,beat=beat,length=.375,pitch=pitch,value=.7)
    command("notes.add",pattern=pattern,channel=ghost,beat=0,length=.25,pitch=48,value=.4)
    command("clip.add",pattern=pattern,beat=0,length=4)
    command("clip.add",pattern=pattern,beat=4,length=4)
    original=notes();ids=[n["id"] for n in original[:3]]
    check(state()["projectVersion"]==project_schema(root) and all(n["muted"] is False for n in original),"Typed current schema/default unmuted")
    contract=data("vibera_commands",dict(command="notes.mute",includeSchemas=True))["operations"][0]
    check(contract["supported"] and set(contract["arguments"]["properties"])=={"pattern","channel","ids"},"Meaningful typed canonical contract")
    command("notes.mute",pattern=pattern,channel=channel,ids=[ids[0]])
    muted=notes()
    check(muted[0]["muted"] and muted[1:]==original[1:] and {k:v for k,v in muted[0].items() if k!="muted"}=={k:v for k,v in original[0].items() if k!="muted"},"Mute preserves source ID/properties and unaffected notes")
    revision=state()["revision"];command("notes.mute",pattern=pattern,channel=channel,ids=[ids[0]])
    check(state()["revision"]==revision,"Idempotent mute has no phantom history")
    command("edit.undo");check(notes()==original,"Exact one Undo")
    command("edit.redo");check(notes()==muted,"Exact Redo")
    command("edit.undo")
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2))
    command("piano.toolMute")
    check(state()["piano"]["tool"]=="mute","Semantic T tool readback")
    for ending in ("cancel","commit"):
        lease=data("vibera_gesture",dict(action="begin",domain="noteMute",label="Mute notes",leaseMs=10000))["operation"]
        for ident in ids:
            result=data("vibera_gesture",dict(action="update",operation=lease,command="notes.mute",args=dict(pattern=pattern,channel=channel,ids=[ident])))
            check(result["completionState"]=="pending","Stroke update pending, not falsely completed")
        check(all(n["muted"] for n in notes()[:3]) and notes()[-1]==original[-1],"Additive semantic stroke and ghost preservation")
        result=data("vibera_gesture",dict(action=ending,operation=lease))
        check(result["completionState"]==("cancelled" if ending=="cancel" else "completed"),"Real terminal stroke outcome")
        if ending=="cancel":check(notes()==original,"Cancellation restores exact score")
    committed=notes();command("edit.undo");check(notes()==original,"Multi-update stroke one Undo")
    command("edit.redo");check(notes()==committed,"Stroke Redo")
    command("notes.unmute",pattern=pattern,channel=channel)
    check(notes()==original,"No explicit IDs restores current Channel part only")
    lease=data("vibera_gesture",dict(action="begin",domain="noteMute",leaseMs=250))["operation"]
    data("vibera_gesture",dict(action="update",operation=lease,command="notes.mute",args=dict(pattern=pattern,channel=channel,ids=ids)))
    time.sleep(.45)
    check(data("vibera_gesture",dict(action="state",operation=lease))["completionState"]=="cancelled" and notes()==original,"Lease expiry restores exact source")
    for args in [dict(ids=[ids[0],9999999]),dict(ids=[ids[0],ids[0]]),dict(ids=[1.2]),dict(value=True),dict(ids=[ids[0]],channel=9999999)]:
        revision=state()["revision"]
        target=dict(pattern=pattern,channel=channel);target.update(args)
        failed=client.call("vibera_command",dict(command="notes.mute",args=target),allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"].get("error") and state()["revision"]==revision and notes()==original,"Invalid input fails atomically with structured error")
    command("notes.mute",pattern=pattern,channel=ghost,ids=[original[-1]["id"]])
    command("notes.mute",pattern=pattern,channel=channel)
    mutedAll=notes()
    def export(action,name):
        path=output/name
        job=data("vibera_project",dict(action=action,path=str(path),overwrite=True))
        check(job["completionState"]=="pending","File operation acceptance is pending")
        done=client.wait_job(job)
        check(done["completionState"]=="completed" and done["fileExists"] and done["fileBytes"]>0,"Actual file completion/readback")
        return path
    silent=export("export","muted.wav")
    with wave.open(str(silent),"rb") as wav:
        check(wav.getnframes()>0 and not any(wav.readframes(wav.getnframes())),"Linked arrangement all-muted has real duration and silent PCM")
    midi=export("exportMidi","muted.mid")
    check(midi.read_bytes().startswith(b"MThd") and b"MTrk" in midi.read_bytes(),"Actual silent SMF metadata/end track")
    project=export("save","muted.vibra")
    client.wait_job(data("vibera_project",dict(action="open",path=str(project),discardChanges=True)))
    check(notes()==mutedAll,"Save/reopen preserves muted source IDs/flags")
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2))
    command("notes.unmute",pattern=pattern,channel=channel)
    audible=export("export","audible.wav")
    with wave.open(str(audible),"rb") as wav:
        check(any(wav.readframes(wav.getnframes())),"Unmute restores actual rendered music")
    played=export("exportMidi","played.mid")
    check(played.stat().st_size>midi.stat().st_size,"Audible SMF includes note events; mute does not leak notes")
    revision=state()["revision"]; before=notes()
    failed=client.call("vibera_project",dict(action="exportMidi",path=str(played)),allow_failure=True)
    check(failed.get("isError") and failed["structuredContent"]["error"]["code"]=="output_exists","Overwrite requires explicit authorization")
    blocker=output/"blocked-parent";blocker.write_text("retained fixture",encoding="utf-8")
    job=data("vibera_project",dict(action="exportMidi",path=str(blocker/"fail.mid")))
    check(job["completionState"]=="pending","Filesystem failure starts as pending")
    deadline=time.monotonic()+10
    while job["completionState"]=="pending":
        check(time.monotonic()<deadline,"Filesystem failure becomes terminal")
        time.sleep(.03)
        job=client.call("vibera_project",dict(action="job",operation=job["operation"]),allow_failure=True)["structuredContent"]
    check(job["completionState"]=="failed" and job["error"]["code"]=="operation_failed" and notes()==before and state()["revision"]==revision,"Real async MIDI failure retains project/history")
    receipt["passed"] = True
except Exception as error:
    receipt["error"] = str(error)
finally:
    if client:
        try:
            client.close()
            receipt["exitCode"] = client.process.returncode
        except Exception as error:
            receipt["passed"] = False
            receipt["closeError"] = str(error)
    receipt["finished"] = True
    receipt["seconds"] = time.monotonic()-began
    (output/"verification.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(receipt))
sys.exit(0 if receipt["passed"] else 1)
