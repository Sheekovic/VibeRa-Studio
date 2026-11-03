"""Actual semantic transport/length/history/project/WAV workflow. No pointer input."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import wave
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
app,output=map(lambda x:Path(x).resolve(),sys.argv[1:3])
output.mkdir(parents=True,exist_ok=True)
client_path=app.parent/"skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():client_path=root/"skills/vibera-studio/scripts/stdio_client.py"
spec=importlib.util.spec_from_file_location("neutral",client_path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
report=dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
began=time.monotonic();client=None
def check(value,message):
    if not value:raise AssertionError(message)
    report["checks"]+=1
try:
    client=module.VibeRaClient(app);data=client.data
    state=lambda:data("vibera_state")
    command=lambda name,**args:data("vibera_command",dict(command=name,args=args))
    def ready():
        deadline=time.monotonic()+10
        current=state()
        while current["pendingAudioUpdate"] and time.monotonic()<deadline:
            time.sleep(.02);current=state()
        check(not current["pendingAudioUpdate"],"Projection reached a real prepared state")
        return current
    initial=ready();pattern=initial["selectedPattern"];channel=initial["channels"][0]["id"]
    data("vibera_transport",dict(action="pattern"));initial=ready()
    bounds=initial["audio"]["loopBounds"]
    check(bounds["enabled"] and bounds["endBeat"]==4,"Pattern defaults to its complete looping range")
    check(bounds["appliedAvailable"] is False and bounds["appliedEndBeat"] is None and bounds["appliedEnabled"] is None,
          "No-device instance reports applied audio unavailable, never fake success")
    revision=initial["revision"]
    for enabled in [False,False,True,True]:
        current=data("vibera_transport",dict(action="loop",enabled=enabled))
        check(current["audio"]["loopBounds"]["enabled"]==enabled and current["revision"]==revision and current["completionState"]=="completed",
              "Idempotent loop setter changes only runtime state")
    contract=data("vibera_commands",dict(command="transport.loop",includeSchemas=True))["operations"][0]
    check(contract["supported"] and contract["arguments"]["properties"]=={} and contract["arguments"]["additionalProperties"] is False,
          "Canonical Loop toggle discovers its exact empty argument schema")
    refused=client.call("vibera_command",dict(command="transport.loop",args=dict(enabled=False)),allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["completionState"]=="failed" and refused["structuredContent"]["error"]["code"]=="invalid_arguments",
          "Toggle refuses ignored setter arguments; typed transport setter is explicit")
    command("transport.loop")
    check(not state()["audio"]["loopBounds"]["enabled"],"Canonical Loop command shares the UI toggle")
    command("transport.loop")
    profile=state()["controllerProfile"]
    custom=dict(profile)
    custom["bindings"]=[dict(action="transport.loop",label="Loop",port=1,channel=16,kind=1,number=70,layer=0,mode=0,mixer=-1,minimum=0,maximum=1,pickup=False,banked=False,enabled=True,nativeArea=0)]
    data("vibera_controller",dict(action="configure",profile=custom))
    data("vibera_controller",dict(action="inject",port=1,kind=1,channel=16,number=70,value=127))
    check(not state()["audio"]["loopBounds"]["enabled"] and state()["revision"]==revision,"Mapped virtual MIDI button uses canonical runtime Loop action")
    data("vibera_controller",dict(action="inject",port=1,kind=1,channel=16,number=70,value=0))
    data("vibera_controller",dict(action="configure",profile=profile))
    data("vibera_transport",dict(action="loop",enabled=True))
    for invalid in ["yes",1,None]:
        refused=client.call("vibera_transport",dict(action="loop",enabled=invalid),allow_failure=True)
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"]=="invalid_arguments" and state()["revision"]==revision,
              "Invalid Loop input fails structurally without document mutation")
    command("pattern.length",pattern=pattern,length=8)
    check(ready()["audio"]["loopBounds"]["endBeat"]==8,"Manual Pattern grow reaches requested loop readback")
    command("edit.undo")
    check(ready()["audio"]["loopBounds"]["endBeat"]==4,"One exact length Undo restores loop request")
    command("edit.redo")
    check(ready()["audio"]["loopBounds"]["endBeat"]==8,"Length Redo restores the new loop request")
    for invalid in [0,-1,1000001]:
        revision=state()["revision"]
        refused=client.call("vibera_command",dict(command="pattern.length",args=dict(pattern=pattern,length=invalid)),allow_failure=True)
        check(refused.get("isError") and refused["structuredContent"].get("error") and state()["revision"]==revision,
              "Invalid Pattern extent is rejected atomically")
    command("steps.set",pattern=pattern,channel=channel,beat=0,enabled=True)
    command("clip.add",pattern=pattern,beat=0,length=8)
    command("clip.add",pattern=pattern,beat=8,length=8)
    clips=data("vibera_inspect",dict(entity="clips"))["items"]
    last=max(clips,key=lambda x:x["beat"])["id"]
    data("vibera_transport",dict(action="song"))
    current=ready()
    check(not current["audio"]["loopBounds"]["enabled"] and current["audio"]["loopBounds"]["endBeat"]==16,
          "Switch to Song preserves its non-looping default and arrangement extent")
    data("vibera_transport",dict(action="loop",enabled=True))
    command("clip.move",ids=[last],length=4)
    check(ready()["audio"]["loopBounds"]["endBeat"]==20,"Playlist right-edge growth updates whole-Song loop request")
    command("edit.undo")
    check(ready()["audio"]["loopBounds"]["endBeat"]==16,"Arrangement resize Undo restores previous end")
    command("edit.redo")
    check(ready()["audio"]["loopBounds"]["endBeat"]==20,"Arrangement resize Redo restores end")
    project=output/"loop-workflow.vibra"
    job=data("vibera_project",dict(action="save",path=str(project),overwrite=True))
    check(job["completionState"]=="pending","Save request acceptance is not completion")
    client.wait_job(job)
    client.wait_job(data("vibera_project",dict(action="open",path=str(project),discardChanges=True)))
    current=ready()
    check(current["audio"]["loopBounds"]["endBeat"]==20 and current["patterns"][0]["length"]==8,
          "Actual save/reopen preserves musical extents independently of transient Loop state")
    data("vibera_transport",dict(action="song"));ready()
    data("vibera_transport",dict(action="loop",enabled=True))
    wav=output/"whole-song.wav"
    job=data("vibera_project",dict(action="export",path=str(wav),overwrite=True))
    check(job["completionState"]=="pending","WAV acceptance stays pending until actual output commit")
    client.wait_job(job)
    with wave.open(str(wav),"rb") as f:
        check((f.getframerate(),f.getnchannels(),f.getsampwidth(),f.getnframes())==(48000,2,3,480000),
              "Loop-enabled offline export writes exactly one complete ten-second arrangement")
        pcm=f.readframes(f.getnframes())
    check(any(pcm[4800*6:9600*6]) and any(pcm[196800*6:201600*6]),"Real PCM contains both linked Pattern hits")
    blocker=output/"blocked-parent";blocker.write_text("owned loop failure fixture",encoding="utf-8")
    preserved=project.read_bytes()
    job=data("vibera_project",dict(action="save",path=str(blocker/"failed.vibra")))
    check(job["completionState"]=="pending","Deferred save error begins as a real pending operation")
    deadline=time.monotonic()+10
    while job["completionState"]=="pending" and time.monotonic()<deadline:
        time.sleep(.03);job=client.call("vibera_project",dict(action="job",operation=job["operation"]),allow_failure=True)["structuredContent"]
    check(job["completionState"]=="failed" and job["error"]["code"]=="operation_failed" and
          blocker.read_text()=="owned loop failure fixture" and project.read_bytes()==preserved,
          "Actual async save failure preserves the verified project and blocker")
    report["passed"]=True
except Exception as error:report["error"]=str(error)
finally:
    if client:
        try:client.close();report["exitCode"]=client.process.returncode
        except Exception as error:report["passed"]=False;report["closeError"]=str(error)
    report["finished"]=True;report["seconds"]=time.monotonic()-began
    (output/"verification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report))
sys.exit(0 if report["passed"] else 1)
