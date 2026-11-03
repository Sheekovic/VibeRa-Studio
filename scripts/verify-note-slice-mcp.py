"""Actual semantic cut-line preview/commit/cancel, history, project, WAV and MIDI checks.

No pointer simulation, native-input, listening or hardware claim.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
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
    source=output/"pulse.wav"
    with wave.open(str(source),"wb") as wav:
        wav.setparams((2,2,48000,4800,"NONE","not compressed"))
        wav.writeframes(b"".join(struct.pack("<hh",3000 if i%100<50 else -3000,3000 if i%100<50 else -3000)for i in range(4800)))
    client=module.VibeRaClient(app);data=client.data
    state=lambda:data("vibera_state")
    command=lambda name,**args:data("vibera_command",dict(command=name,args=args))
    pattern=state()["selectedPattern"];channel,ghost=[c["id"]for c in state()["channels"][:2]]
    notes=lambda:data("vibera_inspect",dict(entity="notes",pattern=pattern))["items"]
    for target,pan in ((channel,-1),(ghost,1)):
        command("channel.source",channel=target,text=str(source));command("channel.pan",channel=target,value=pan)
    for pitch in (60,64):command("notes.add",pattern=pattern,channel=channel,beat=0,length=2,pitch=pitch,value=.7)
    command("notes.add",pattern=pattern,channel=ghost,beat=0,length=2,pitch=67,value=.5)
    for beat in (0,4):command("clip.add",pattern=pattern,beat=beat,length=4)
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2));command("piano.toolSlice")
    check(state()["piano"]["tool"]=="slice","Semantic Slice tool and truthful editor state")
    contract=data("vibera_commands",dict(command="notes.slice",includeSchemas=True))["operations"][0]
    check(contract["supported"] and set(contract["arguments"]["properties"])=={"pattern","channel","ids","slice"},"Typed canonical Slice has only meaningful parameters")
    original=notes();revision=state()["revision"]
    line=dict(startBeat=.75,endBeat=1.25,startPitch=59.5,endPitch=64.5,grid=.25)
    def begin(**extra):return data("vibera_slice",dict(action="begin",**extra))["operation"]
    def update(token,**extra):return data("vibera_slice",dict(action="update",operation=token,slice=extra or line))
    def render(name):
        path=output/name;job=data("vibera_project",dict(action="export",path=str(path),overwrite=True))
        check(job["completionState"]=="pending","WAV job is pending before actual completion");client.wait_job(job)
        with wave.open(str(path),"rb") as wav:
            check(wav.getnchannels()==2 and wav.getsampwidth()==3 and wav.getnframes()>0,"Actual stereo24 WAV")
            return wav.readframes(wav.getnframes())
    baseline_pcm=render("before.wav")
    token=begin(projectIdentity=state()["projectIdentity"],leaseMs=10000)
    preview=update(token)
    check(preview["completionState"]=="pending" and preview["count"]==2 and notes()==original and state()["revision"]==revision,"Real preview computes cuts without changing score/history/revision")
    check([c["beat"]for c in preview["items"]]==[.75,1.25] and all(c["right"]==0 for c in preview["items"]),"Planned targets, snapped geometry and not-yet-created IDs")
    page=data("vibera_slice",dict(action="state",operation=token,offset=1,limit=1))
    check(len(page["items"])==1 and page["items"][0]==preview["items"][1] and page["nextOffset"] is None,"Stable paginated preview")
    check(data("vibera_project",dict(action="state"))["revision"]==revision,"Read-only project state remains available during preview")
    for invalid in (dict(line,grid=.00001),dict(line,startPitch=128),dict(line,allChannels="yes"),dict(line,other=1)):
        failed=client.call("vibera_slice",dict(action="update",operation=token,slice=invalid),allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["error"] and notes()==original and data("vibera_slice",dict(action="state"))["items"]==preview["items"],"Invalid line leaves valid preview/source intact with structured error")
    for tool,args in (("vibera_command",dict(command="notes.delete",args=dict(pattern=pattern,channel=channel,ids=[original[0]["id"]]))),("vibera_project",dict(action="exportMidi",path=str(output/"blocked.mid")))):
        failed=client.call(tool,args,allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["error"]["code"]=="slice_owned" and notes()==original,"Preview ownership blocks other writes and file admission")
    done=data("vibera_slice",dict(action="commit",operation=token));split=notes()
    check(done["completionState"]=="completed" and len(split)==5 and all(c["right"]>0 for c in done["items"]),"Actual canonical completion and created right IDs")
    for cut in done["items"]:
        left=next(n for n in split if n["id"]==cut["left"]);right=next(n for n in split if n["id"]==cut["right"])
        before=next(n for n in original if n["id"]==left["id"])
        check(left["beat"]==before["beat"] and left["length"]==cut["beat"] and right["beat"]==cut["beat"] and abs(right["length"]-(2-cut["beat"]))<1e-9 and right["pitch"]==before["pitch"] and right["velocity"]==before["velocity"] and not right["muted"],"Exact per-note geometry and musical properties")
    check(split[2]==original[2],"Ghost source untouched by active-part default")
    command("edit.undo");check(notes()==original,"One exact Undo for full cut line")
    command("edit.redo");check(notes()==split,"Exact Redo")
    token=begin();update(token);no_op=data("vibera_slice",dict(action="commit",operation=token))
    check(no_op["count"]==0 and notes()==split,"Repeating same boundary is a no-op")
    token=begin();update(token,**dict(line,startBeat=.25,endBeat=.5));before=state()["revision"]
    check(data("vibera_slice",dict(action="cancel",operation=token))["completionState"]=="cancelled" and notes()==split and state()["revision"]==before,"Cancellation leaves source/history unchanged")
    token=begin(leaseMs=250);update(token);time.sleep(.45)
    check(data("vibera_slice",dict(action="state",operation=token))["completionState"]=="cancelled" and notes()==split,"Real idle expiry cancels preview")
    for args in (dict(pattern=pattern,channel=channel,ids=[original[0]["id"],99999999],slice=line),dict(pattern=pattern,channel=channel,ids=[original[0]["id"],original[0]["id"]],slice=line),dict(pattern=pattern,channel=channel,slice=dict(line,grid=-1))):
        before=state()["revision"];failed=client.call("vibera_command",dict(command="notes.slice",args=args),allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["error"] and notes()==split and state()["revision"]==before,"Direct canonical Slice invalid input is atomic")
    command("edit.undo");check(notes()==original,"Prepare real canonical macro-style workflow")
    result=data("vibera_batch",dict(commands=[dict(command="notes.slice",args=dict(pattern=pattern,channel=channel,slice=line)),dict(command="notes.move",args=dict(pattern=pattern,channel=channel,ids=[original[0]["id"]],beat=.25,length=0,pitch=0))]))
    batch=notes();check(len(batch)==5 and batch[0]["beat"]==.25 and batch[0]["length"]==.75,"Semantic ordered Slice/move batch uses the same canonical operations as UI macros")
    command("edit.undo");check(notes()==original,"One exact Undo for Slice/move workflow")
    command("edit.redo");check(notes()==batch,"Exact batch Redo")
    command("edit.undo");command("notes.slice",pattern=pattern,channel=channel,slice=line)
    check(notes()==split,"Restored split has exact identities and geometry")
    after_pcm=render("sliced.wav")
    check(after_pcm!=baseline_pcm,"Slicing produces real sampler retriggers")
    check(b"".join(after_pcm[i+3:i+6]for i in range(0,len(after_pcm),6))==b"".join(baseline_pcm[i+3:i+6]for i in range(0,len(baseline_pcm),6)),"Unchanged ghost Channel renders byte-identical PCM")
    project=output/"sliced.vibra";client.wait_job(data("vibera_project",dict(action="save",path=str(project),overwrite=True)))
    client.wait_job(data("vibera_project",dict(action="open",path=str(project),discardChanges=True)))
    check(notes()==split,"Real project round trip retains original/fresh IDs and geometry")
    check(render("reopened.wav")==after_pcm,"Reopened arrangement renders exact PCM")
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2))
    mid=output/"sliced.mid";job=data("vibera_project",dict(action="exportMidi",path=str(mid),overwrite=True))
    check(job["completionState"]=="pending","Real score SMF worker admission");client.wait_job(job)
    check(mid.read_bytes().startswith(b"MThd") and mid.stat().st_size>50,"Actual sliced SMF written")
    failed=client.call("vibera_slice",dict(action="begin",slice=line),allow_failure=True)
    check(failed.get("isError") and failed["structuredContent"]["error"],"Inapplicable parameters are not silently ignored")
    failed=client.call("vibera_slice",dict(action="state",operation="old-token"),allow_failure=True)
    check(failed.get("isError") and failed["structuredContent"]["error"]["code"]=="stale_operation","Old operation cannot falsely verify a replacement session")
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
