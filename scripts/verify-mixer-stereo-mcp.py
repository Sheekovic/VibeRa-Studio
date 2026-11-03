"""Actual canonical stereo controls, gestures, projects and whole-file WAV oracles.
No OS input, installed-plugin compatibility, listening or hardware claim.
"""
from pathlib import Path
import importlib.util
import json
import struct
import sys
import time
import wave
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
def load(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def verify_batch_and_controller(client, check):
    """Actual supported semantic paths; no physical MIDI or DSP acknowledgement."""
    d = client.data
    cmd = lambda name, **args: d("vibera_command", dict(command=name, args=args))
    state = lambda: d("vibera_state")
    def musical():
        # Meters are explicitly transient; all other returned insert fields stay exact.
        return [{key: value for key, value in item.items() if key != "meter"}
                for item in d("vibera_inspect", dict(entity="mixer"))["items"]]
    def target(index=1):
        item = next(item for item in musical() if item["index"] == index)
        return tuple(item[key] for key in ("stereoSeparation", "phaseInverted", "stereoSwapped"))
    f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    original = musical()
    steps = [dict(command="mixer.stereoSeparation", args=dict(mixer=1, value=-.75)),
             dict(command="mixer.phaseInvert", args=dict(mixer=1, enabled=False)),
             dict(command="mixer.stereoSwap", args=dict(mixer=1, enabled=False)),
             dict(command="mixer.stereoSeparation", args=dict(mixer=2, value=.25)),
             dict(command="mixer.phaseInvert", args=dict(mixer=2, enabled=True))]
    answer = d("vibera_batch", dict(commands=steps, revision=state()["revision"]))
    batched = musical()
    check(not answer["gestureActive"] and target() == (-.75, False, False) and
          target(2) == (.25, True, False), "Actual stereo batch updates two stable inserts and releases ownership")
    cmd("edit.undo")
    check(musical() == original, "One actual Undo restores every insert after five stereo batch steps")
    cmd("edit.redo")
    check(musical() == batched, "One actual Redo restores complete two-insert stereo batch")
    cmd("edit.undo")
    prior = state()
    failed = client.call("vibera_batch", dict(commands=[steps[0],
        dict(command="mixer.stereoSeparation", args=dict(mixer=2, value=2))]), allow_failure=True)
    after = state()
    check(failed.get("isError") and failed.get("structuredContent", {}).get("error", {}).get("code") and
          musical() == original and not after["gestureActive"] and
          all(after[key] == prior[key] for key in ("dirty", "undoAvailable", "redoAvailable")),
          "Invalid later stereo batch step atomically rolls back musical state, dirty/history availability and owner")
    cmd("edit.redo")
    check(musical() == batched, "Rejected batch preserves previously available exact Redo")
    cmd("edit.undo")
    # Only the existing owned --mcp-test controller API is used. Empty hardware
    # identifiers are mandatory, and the original mapping is restored in finally.
    original_profile = state()["controllerProfile"]
    selected = state()["selectedInsert"]
    profile = dict(original_profile)
    profile.update(profile="Original stereo semantic fixture", input="", output="", bindings=[])
    def binding(action, mode, number, kind=0, pickup=False):
        return dict(action=action, label="Original stereo mapping", port=1, channel=16,
                    kind=kind, number=number, layer=0, mode=mode, mixer=1,
                    minimum=-1, maximum=1, pickup=pickup, banked=False, enabled=True, nativeArea=0)
    def configure(items):
        profile["bindings"] = items
        d("vibera_controller", dict(action="configure", profile=profile))
    def inject(number, value, kind=0):
        return d("vibera_controller", dict(action="inject", port=1, kind=kind,
                                           channel=16, number=number, value=value))
    try:
        d("vibera_mixer", dict(action="select", insert=2))
        check(target()[0] == .5, "Controller fixture starts at preceding canonical separation .5")
        configure([binding("mixer.stereoSeparation", 1, 70, pickup=True)])
        prior = state(); unchanged = musical()
        inject(70, 0)
        check(musical() == unchanged and state()["revision"] == prior["revision"],
              "Absolute CC pickup does not jump before reaching current stereo target")
        inject(70, 127)
        check(target()[0] == 1 and target(2) == (0, False, False) and state()["selectedInsert"] == 2,
              "Pickup crossing applies literal endpoint to fixed insert1 while insert2 remains selected")
        cmd("edit.undo")
        check(musical() == original, "Mapped absolute separation uses canonical exact Undo")
        prior = state(); inject(70, 0)
        check(musical() == original and state()["revision"] == prior["revision"],
              "Canonical Undo re-arms pickup instead of applying an out-of-date physical position")
        for mode, positive, negative, neutral in ((2, 1, 127, 0), (3, 65, 63, 64)):
            configure([binding("mixer.stereoSeparation", mode, 71)])
            prior = state(); unchanged = musical()
            inject(71, neutral)
            check(state()["revision"] == prior["revision"] and musical() == unchanged,
                  "Relative mode neutral is a true canonical no-op mode=" + str(mode))
            for value, sign in ((positive, 1), (negative, -1)):
                inject(71, value)
                # MCP serializes15 decimal places; the document stores float32.
                # Reconstruct that exact type, never accept a numerical tolerance.
                check(f32(target()[0]) == f32(.5 + sign * 2 / 127),
                      "Actual signed relative separation delta in [-1,1] mode=" + str(mode) + " value=" + str(value) + " actual=" + repr(target()[0]) + " expected=" + repr(f32(.5 + sign * 2 / 127)))
                cmd("edit.undo")
                check(musical() == original, "Relative separation has one exact Undo mode=" + str(mode))
        for action, field, number in (("mixer.phaseInvert", 1, 72), ("mixer.stereoSwap", 2, 73)):
            configure([binding(action, 0, number, kind=1)])
            baseline = target(); inject(number, 127, kind=1)
            expected = list(baseline); expected[field] = not expected[field]
            check(target() == tuple(expected), "Actual mapped button toggles fixed stereo flag " + action)
            revision = state()["revision"]; inject(number, 0, kind=1)
            check(target() == tuple(expected) and state()["revision"] == revision,
                  "Mapped note release does not toggle again " + action)
            cmd("edit.undo")
            check(musical() == original, "Mapped stereo toggle has exact canonical Undo " + action)
    finally:
        d("vibera_controller", dict(action="configure", profile=original_profile))
        d("vibera_mixer", dict(action="select", insert=selected))
    check(state()["controllerProfile"] == original_profile and
          state()["selectedInsert"] == selected and musical() == original,
          "Semantic MIDI fixture restores original mappings, workspace selection and musical state")


def main():
    if len(sys.argv)!=3 or not Path(sys.argv[1]).is_absolute():return 2
    app=Path(sys.argv[1]).resolve(strict=True)
    helper=load("stereo_owned_output",ROOT/"scripts/verify-mixer-effects-mcp.py")
    helper.OWNER={"schemaVersion":1,"owner":"VibeRa verify-mixer-stereo-mcp"}
    helper.OWNER_FILE=".vibera-mixer-stereo-mcp-owner.json"
    output,attempt=helper.prepare_output(sys.argv[2])
    checks=[];client=None;started=time.monotonic()
    report=dict(passed=False,finished=False,checks=checks,application=str(app),
        applicationSha256=helper.digest(app),scriptSha256=helper.digest(Path(__file__)),
        nativeInput=False,listening=False,hardware=False,pluginCompatibility=False,
        attemptDirectory=str(attempt),audio="Actual offline stereo24 WAV relational sample oracle, no live callback claim")
    def check(value,message):
        if not value:raise AssertionError(message)
        checks.append(message)
    try:
        module=load("stereo_neutral_client",ROOT/"skills/vibera-studio/scripts/stdio_client.py")
        client=module.VibeRaClient(app);d=client.data
        cmd=lambda name,**args:d("vibera_command",dict(command=name,args=args))
        state=lambda:d("vibera_state")
        rows=lambda:d("vibera_inspect",dict(entity="mixer"))["items"]
        row=lambda index:next(x for x in rows() if x["index"]==index)
        target=lambda index:tuple(row(index)[key] for key in ("stereoSeparation","phaseInverted","stereoSwapped"))
        error=lambda name,args:client.call(name,args,allow_failure=True)
        for name,fields in (("mixer.stereoSeparation",{"mixer","value"}),("mixer.phaseInvert",{"mixer","enabled"}),("mixer.stereoSwap",{"mixer","enabled"})):
            item=d("vibera_commands",dict(command=name,includeSchemas=True))["operations"][0]
            check(item["supported"] and set(item["arguments"]["required"])==fields and set(item["arguments"]["properties"])==fields and "resultSchema" in item,"Typed discoverable "+name)
        baseline=target(1)
        result=cmd("mixer.stereoSeparation",mixer=1,value=-.5)
        check(result["completionState"]=="completed" and result["completionScope"]=="canonicalMixerStereoTarget" and result["dspSettlingAcknowledged"] is False and target(1)[0]==-.5,"Actual canonical target completion, no DSP acknowledgement")
        cmd("mixer.phaseInvert",mixer=1,enabled=True);cmd("mixer.stereoSwap",mixer=1,enabled=True)
        changed=target(1);check(changed==(-.5,True,True),"All saved stereo controls read back")
        for _ in range(3):cmd("edit.undo")
        check(target(1)==baseline,"Exact three-operation Undo")
        for _ in range(3):cmd("edit.redo")
        check(target(1)==changed,"Exact three-operation Redo")
        for name,args in (("mixer.stereoSeparation",dict(mixer=1,value=1.000001)),("mixer.stereoSeparation",dict(mixer=1,value=True)),("mixer.stereoSeparation",dict(value=.5)),("mixer.phaseInvert",dict(mixer=1,enabled=1)),("mixer.stereoSwap",dict(mixer=999,enabled=True)),("mixer.stereoSwap",dict(mixer=1,enabled=True,value=1))):
            before=state()["revision"];prior=target(1);answer=error("vibera_command",dict(command=name,args=args))
            check(answer.get("isError") and answer.get("structuredContent",{}).get("error",{}).get("code") and state()["revision"]==before and target(1)==prior,"Structured invalid input preserves exact target "+name+str(args))
        lease=d("vibera_gesture",dict(action="begin",domain="mixerControl",sourceInsert=1));token=lease["operation"]
        for amount in (-1.,-.25,.75):d("vibera_gesture",dict(action="update",operation=token,command="mixer.stereoSeparation",args=dict(mixer=1,value=amount)))
        check(target(1)[0]==.75,"Actual continuous separation updates")
        check(error("vibera_gesture",dict(action="update",operation=token,command="mixer.stereoSeparation",args=dict(mixer=2,value=.5))).get("isError"),"Lease cannot edit a different stable insert")
        check(d("vibera_gesture",dict(action="cancel",operation=token))["completionState"]=="cancelled" and target(1)==changed,"Exact gesture cancellation baseline")
        token=d("vibera_gesture",dict(action="begin",domain="mixerControl",sourceInsert=1))["operation"]
        for amount in (.25,.5):d("vibera_gesture",dict(action="update",operation=token,command="mixer.stereoSeparation",args=dict(mixer=1,value=amount)))
        check(d("vibera_gesture",dict(action="commit",operation=token))["completionState"]=="completed","Actual gesture terminal completion")
        cmd("edit.undo");check(target(1)==changed,"One Undo for full separation gesture");cmd("edit.redo");check(target(1)==(.5,True,True),"Gesture Redo")
        verify_batch_and_controller(client, check)
        cmd("transport.record");cmd("mixer.stereoSeparation",mixer=1,value=0)
        check(state()["recordArmed"] and not state()["recordingTake"],"Stereo target preserves software arm without inventing hardware capture")
        cmd("transport.record");cmd("edit.undo")
        saved=attempt/"stereo-targets.vibra";client.wait_job(d("vibera_project",dict(action="save",path=str(saved),overwrite=False)))
        saved_target=target(1)
        client.wait_job(d("vibera_project",dict(action="new",discardChanges=True)))
        client.wait_job(d("vibera_project",dict(action="open",path=str(saved),discardChanges=True)))
        check(target(1)==saved_target,"Actual async save/new/open preserves stereo state")
        client.wait_job(d("vibera_project",dict(action="new",discardChanges=True)))
        s=state();pattern=s["selectedPattern"];channel=s["channels"][0]["id"]
        sample=attempt/"original-stereo.wav"
        with wave.open(str(sample),"wb") as wav:
            wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(48000)
            wav.writeframes(struct.pack("<hh",4096,-2048)*65536)
        cmd("channel.source",channel=channel,text=str(sample));cmd("channel.mixer",channel=channel,mixer=1)
        cmd("notes.add",pattern=pattern,channel=channel,beat=0,length=1,pitch=60,value=1)
        cmd("clip.add",pattern=pattern,beat=0,length=4)
        def render(name):
            path=attempt/name;job=d("vibera_project",dict(action="export",path=str(path),overwrite=False))
            check(job["completionState"]=="pending","Export accepted as pending "+name);client.wait_job(job)
            with wave.open(str(path),"rb") as wav:
                check(wav.getnchannels()==2 and wav.getsampwidth()==3,"Actual stereo24 WAV "+name)
                pcm=wav.readframes(wav.getnframes())
            return [int.from_bytes(pcm[i:i+3],"little",signed=True) for i in range(0,len(pcm),3)]
        base=render("identity.wav")
        check(max(map(abs,base))>100 and any(l!=r for l,r in zip(base[::2],base[1::2])),"Genuine asymmetric nonzero source, not silent oracle")
        for amount,invert,swap in ((1,False,False),(-.5,False,False),(0,True,False),(0,False,True),(-1,True,True)):
            cmd("mixer.stereoSeparation",mixer=1,value=amount);cmd("mixer.phaseInvert",mixer=1,enabled=invert);cmd("mixer.stereoSwap",mixer=1,enabled=swap)
            pcm=render("matrix-"+str(amount)+str(invert)+str(swap)+".wav")
            check(len(pcm)==len(base),"Entire render extent preserved")
            maximum=0
            for i in range(0,len(base),2):
                l,r=base[i:i+2]
                a=l+(r-l)*amount/2 if amount>=0 else l+r*amount
                b=r+(l-r)*amount/2 if amount>=0 else r+l*amount
                if invert:a,b=-a,-b
                if swap:a,b=b,a
                maximum=max(maximum,abs(pcm[i]-a),abs(pcm[i+1]-b))
            check(maximum<=2,"Whole-file independent matrix oracle within two24bit quantization counts "+str((amount,invert,swap,maximum)))
        marker=attempt/"not-a-directory";marker.write_text("original owned failure fixture")
        failed=d("vibera_project",dict(action="export",path=str(marker/"failed.wav"),overwrite=False))
        check(failed["completionState"]=="pending","Failure fixture actually admitted asynchronously")
        deadline=time.monotonic()+40
        while failed["completionState"]=="pending" and time.monotonic()<deadline:
            time.sleep(.03)
            response=client.call("vibera_project",dict(action="job",operation=failed["operation"]),allow_failure=True)
            failed=response["structuredContent"]
        check(failed["completionState"]=="failed" and failed.get("error",{}).get("code")=="operation_failed" and not (marker/"failed.wav").exists() and marker.read_text()=="original owned failure fixture","Actual structured async export failure retains owned obstruction")
        report["passed"]=True
    except Exception as failure:report["error"]=str(failure)
    finally:
        if client:
            try:client.close();report["exitCode"]=client.process.returncode
            except Exception as failure:report["passed"]=False;report["closeError"]=str(failure)
            (attempt/"stderr.txt").write_text("".join(client.stderr),encoding="utf-8")
        report.update(finished=True,seconds=time.monotonic()-started)
        payload=json.dumps(report,indent=2)+"\n"
        (attempt/"verification.json").write_text(payload,encoding="utf-8");(output/"verification.json").write_text(payload,encoding="utf-8")
    print(json.dumps({k:report.get(k) for k in ("passed","finished","seconds","error","attemptDirectory")}))
    return 0 if report["passed"] and report.get("exitCode")==0 else 1
if __name__=="__main__":raise SystemExit(main())
