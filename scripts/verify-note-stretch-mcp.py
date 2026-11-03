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
    client=module.VibeRaClient(app);data=client.data
    state=lambda:data("vibera_state")
    command=lambda name,**args:data("vibera_command",dict(command=name,args=args))
    pattern=state()["selectedPattern"];channel,ghost=[c["id"]for c in state()["channels"][:2]]
    notes=lambda:data("vibera_inspect",dict(entity="notes",pattern=pattern))["items"]
    for target,pan in ((channel,-1),(ghost,1)):
        command("channel.source",channel=target,text="builtin:bass");command("channel.pan",channel=target,value=pan)
        command("channel.sampler.loop",channel=target,enabled=True)
    command("notes.add",pattern=pattern,channel=channel,beat=1,length=1,pitch=60,value=.7)
    command("notes.add",pattern=pattern,channel=channel,beat=2,length=.5,pitch=64,value=.4)
    command("notes.add",pattern=pattern,channel=ghost,beat=0,length=4,pitch=67,value=.5)
    for beat in (0,4):command("clip.add",pattern=pattern,beat=beat,length=4)
    ids=[n["id"]for n in notes()if n["channel"]==channel]
    command("notes.mute",pattern=pattern,channel=channel,ids=[ids[1]])
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2))
    contract=data("vibera_commands",dict(command="notes.stretch",includeSchemas=True))["operations"][0]
    check(contract["supported"] and set(contract["arguments"]["properties"])=={"pattern","channel","ids","beat","value"},"Meaningful typed canonical stretch schema")
    def render(name):
        path=output/name;job=data("vibera_project",dict(action="export",path=str(path),overwrite=True))
        check(job["completionState"]=="pending","Actual WAV worker admission is pending");client.wait_job(job)
        with wave.open(str(path),"rb")as wav:
            check(wav.getnchannels()==2 and wav.getsampwidth()==3 and wav.getnframes()>0,"Real stereo24 WAV")
            return wav.readframes(wav.getnframes())
    before=render("before.wav");baseline=notes();revision=state()["revision"]
    begin=lambda **extra:data("vibera_stretch",dict(action="begin",ids=ids,**extra))["operation"]
    update=lambda token,ratio:data("vibera_stretch",dict(action="update",operation=token,ratio=ratio))
    token=begin(leaseMs=10000);preview=update(token,2)
    check(preview["completionState"]=="pending" and preview["count"]==2 and preview["anchor"]==1,"Shared preview captures source selection/anchor")
    check(notes()==baseline and state()["revision"]==revision and preview["previewMutatesProject"]is False,"Preview does not change source/revision/audio/history")
    check([(n["beat"],n["length"])for n in preview["items"]]==[(1,2),(3,1)],"Absolute proportional plan")
    check(preview["minimumRatio"]>0 and preview["maximumRatio"]>=2,"Source-dependent limits are discoverable")
    page=data("vibera_stretch",dict(action="state",operation=token,offset=1,limit=1))
    check(len(page["items"])==1 and page["items"][0]["id"]==ids[1],"Bounded planned-note pagination")
    update(token,1.5);again=update(token,2)
    check(again["items"]==preview["items"],"Repeated updates derive from baseline rather than compounding")
    failed=client.call("vibera_stretch",dict(action="update",operation=token,ratio=.000001),allow_failure=True)
    check(failed.get("isError") and data("vibera_stretch",dict(action="state"))["items"]==preview["items"],"Invalid compression retains valid prior preview")
    for tool,args in [("vibera_command",dict(command="notes.delete",args=dict(pattern=pattern,ids=ids))),("vibera_project",dict(action="save",path=str(output/"blocked.vibra")))]:
        failed=client.call(tool,args,allow_failure=True);check(failed.get("isError") and failed["structuredContent"]["error"]["code"]=="stretch_owned","Unrelated mutation/file operation cannot steal preview")
    committed=data("vibera_stretch",dict(action="commit",operation=token))
    check(committed["completionState"]=="completed","Actual commit is terminal only after canonical success")
    stretched=notes();left=[n for n in stretched if n["id"]in ids]
    check([(n["beat"],n["length"])for n in left]==[(1,2),(3,1)] and left[1]["muted"],"Committed exact geometry and mute properties")
    check([n for n in stretched if n["channel"]==ghost]==[n for n in baseline if n["channel"]==ghost],"Unrelated ghost source unchanged")
    command("edit.undo");check(notes()==baseline,"One exact stretch Undo")
    command("edit.redo");check(notes()==stretched,"Exact stretch Redo")
    after=render("after.wav");check(before!=after,"Stretch changes actual rendered music")
    right=lambda pcm:b"".join(pcm[i+3:i+6]for i in range(0,len(pcm),6))
    check(right(before)==right(after),"Unaffected right Channel PCM byte-identical")
    token=begin();update(token,.5);data("vibera_stretch",dict(action="cancel",operation=token))
    check(notes()==stretched,"Cancel leaves exact source/history intact")
    revision=state()["revision"];token=begin();data("vibera_stretch",dict(action="commit",operation=token))
    check(state()["revision"]==revision and notes()==stretched,"Ratio1 is no-op without phantom revision/history")
    token=begin(leaseMs=250);update(token,.5);time.sleep(.32)
    check(data("vibera_stretch",dict(action="state",operation=token))["completionState"]=="cancelled" and notes()==stretched,"Real idle lease expiry cancels nondestructively")
    for bad in [dict(action="begin",ids=[]),dict(action="begin",ids=[999999]),dict(action="begin",ids=[ids[0],ids[0]]),dict(action="begin",ids=ids,ratio=2),dict(action="state",operation="old-token")]:
        failed=client.call("vibera_stretch",bad,allow_failure=True);check(failed.get("isError") and notes()==stretched,"Typed invalid/inapplicable/stale request preserves source")
    for bad in [dict(ids=ids,beat=2,value=2),dict(ids=[999999],beat=1,value=2),dict(ids=ids,beat=1,value=0)]:
        failed=client.call("vibera_command",dict(command="notes.stretch",args=dict(pattern=pattern,channel=channel,**bad)),allow_failure=True)
        check(failed.get("isError") and notes()==stretched,"Canonical validation is shared and atomic")
    path=output/"stretched.vibra";job=data("vibera_project",dict(action="save",path=str(path),overwrite=True));client.wait_job(job)
    check(path.exists(),"Actual project persisted")
    job=data("vibera_project",dict(action="open",path=str(path)));client.wait_job(job)
    check(notes()==stretched,"Project save/reopen retains stretched source identities/properties")
    data("vibera_select",dict(pattern=pattern,channel=channel,scope=2))
    mid=output/"stretched.mid";job=data("vibera_project",dict(action="exportMidi",path=str(mid),overwrite=True));check(job["completionState"]=="pending","MIDI export admission is pending");client.wait_job(job)
    check(mid.read_bytes().startswith(b"MThd") and mid.stat().st_size>50,"Actual score SMF written")
    blocked=output/"blocked-parent";blocked.write_text("not a directory")
    terminal=client.call("vibera_project",dict(action="exportMidi",path=str(blocked/"score.mid"),overwrite=True),allow_failure=True)["structuredContent"]
    check(terminal["completionState"]=="pending","Real failed export is first accepted as pending")
    deadline=time.monotonic()+10
    while terminal["completionState"]=="pending" and time.monotonic()<deadline:
        time.sleep(.03);terminal=client.call("vibera_project",dict(action="job",operation=terminal["operation"]),allow_failure=True)["structuredContent"]
    check(terminal["completionState"]=="failed","Async file failure is not reported completed")
    receipt["passed"]=True
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
