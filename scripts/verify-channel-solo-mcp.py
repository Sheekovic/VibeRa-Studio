"""Actual Channel solo/restore/bulk mute, preference, persistence and WAV acceptance."""
import hashlib
import importlib.util
import json
import pathlib
import sys
import time
import wave

sys.dont_write_bytecode = True
root = pathlib.Path(__file__).resolve().parents[1]
app, output = (pathlib.Path(p).resolve() for p in sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():
    client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("vibera_neutral_client", client_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = {"passed": False, "checks": 0, "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
           "clientSha256": hashlib.sha256(client_path.read_bytes()).hexdigest(),
           "nativeInput": False, "listening": False, "hardware": False}
began, client = time.monotonic(), None


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    client = module.VibeRaClient(app)
    receipt["version"] = client.server["version"]
    command = lambda name, **args: client.data("vibera_command", {"command": name, "args": args})
    rack = lambda **args: client.data("vibera_rack", {"action": "state", **args})
    state = lambda: client.data("vibera_state")
    mask = lambda: {c["id"]: c["muted"] for c in state()["channels"]}
    ids = [c["id"] for c in state()["channels"]]
    kick, snare, hat, bass = ids
    pattern = state()["selectedPattern"]
    discovery = client.data("vibera_commands", {"command": "channel.solo", "includeSchemas": True})
    check("restore" in json.dumps(discovery) and "live midi" in json.dumps(discovery).lower(), "Solo prerequisites/absolute enabled/restore discoverable")
    preference = client.data("vibera_commands", {"command": "rack.soloRestore", "includeSchemas": True})["operations"][0]
    check(preference["mapping"] == "vibera_rack" and preference["invocation"]["action"] == "configure" and
          preference["arguments"]["properties"]["restoreMuteAfterSolo"]["type"] == "boolean",
          "Workflow preference has a stable registry ID, typed service invocation and readback")
    command("channel.mute", channel=snare, enabled=True)
    for channel in ids:
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=2, pitch=60, value=.7)
        command("channel.pan", channel=channel, value=-1 if channel==kick else 1)
    for beat in [0,4]: command("clip.add", pattern=pattern, beat=beat, length=4)
    client.data("vibera_select", {"channel":bass,"scope":1})
    command("rack.group.assign", ids=[kick,hat], text="Drums")
    group=next(g["id"] for g in rack()["groups"] if g["name"]=="Drums")
    rack(action="filter",kind="group",group=group)
    before=mask()
    def save(name):
        target=output/name
        job=client.data("vibera_project", {"action":"save","path":str(target),"overwrite":True})
        check(job["completionState"]=="pending", "Save acceptance is not completion")
        client.wait_job(job)
        return target
    baseline=save("baseline-beat.vibra")
    command("channel.solo",channel=kick,enabled=True)
    check(mask()=={i:i!=kick for i in ids} and rack()["soloChannel"]==kick, "Solo includes hidden Channels using stable identities")
    check(rack()["activeChannel"]==bass and rack()["scoreMuteKeepsLiveMidi"] and rack()["soloRestoreMuted"]==[snare], "Solo does not redirect hidden MIDI recipient")
    rev=state()["revision"]
    command("channel.solo",channel=kick,enabled=True)
    check(state()["revision"]==rev, "Explicit repeated solo idempotent")
    def render(name):
        target=output/name
        job=client.data("vibera_project", {"action":"export","path":str(target),"overwrite":True})
        check(job["completionState"]=="pending", "Export acceptance is not completion")
        client.wait_job(job)
        with wave.open(str(target),'rb') as wav:
            check(wav.getframerate()==48000 and wav.getnframes()==192000 and wav.getsampwidth()==3 and wav.getnchannels()==2, "Actual linked arrangement WAV timing/format")
            data=wav.readframes(wav.getnframes())
        return [max(abs(int.from_bytes(data[i+c*3:i+c*3+3], 'little', signed=True)) for i in range(0,len(data),6))/8388608 for c in [0,1]]
    solo_peaks=render("solo-kick.wav")
    check(solo_peaks[0]>.01 and solo_peaks[1]<.0001, "Offline solo audibly isolates left score; muted right score below dither allowance")
    command("channel.solo",channel=hat,enabled=True)
    saved=save("active-solo.vibra")
    client.data("vibera_project", {"action":"open","path":str(saved),"discardChanges":True})
    check(rack()["soloChannel"]==hat and mask()=={i:i!=hat for i in ids}, "Active solo target/mute baseline survive actual reopen")
    command("channel.unsolo")
    check(mask()==before and rack()["soloChannel"]==0, "Reopened solo restores original mixed mute state")
    restored=save("restored-beat.vibra")
    check(restored.read_bytes()==baseline.read_bytes(), "Exact full project round trip including notes/links/allocator")
    command("edit.undo")
    check(rack()["soloChannel"]==hat, "Unsolo Undo restores active snapshot")
    command("edit.redo")
    check(mask()==before, "Unsolo Redo exact mute mask")
    for name,args in [("channel.solo",{"channel":999999}), ("channel.mute",{"ids":[kick,999999]}), ("channel.mute",{"ids":[kick,kick]}), ("channel.unsolo",{"restore":"false"})]:
        result=client.call("vibera_command",{"command":name,"args":args},allow_failure=True)
        check(result.get("isError") and result["structuredContent"]["error"]["code"] and mask()==before, "Structured atomic invalid target/type: "+name)
    rack(action="select",ids=[kick,hat])
    command("rack.muteSelected")
    check(mask()[kick] and mask()[hat] and mask()[snare] and rack()["activeChannel"]==bass, "Bulk mute defaults to shared independent selection")
    command("edit.undo")
    check(mask()==before, "Bulk mute one Undo")
    bad=client.call("vibera_rack",{"action":"configure","restoreMuteAfterSolo":"false"},allow_failure=True)
    check(bad.get("isError") and rack()["restoreMuteAfterSolo"], "Invalid Workflow preference type refuses")
    wrong=client.call("vibera_rack",{"action":"state","restoreMuteAfterSolo":False},allow_failure=True)
    check(wrong.get("isError") and rack()["restoreMuteAfterSolo"], "Preference cannot be silently ignored on a state read")
    rev=state()["revision"]
    rack(action="configure",restoreMuteAfterSolo=False)
    check(not rack()["restoreMuteAfterSolo"] and state()["revision"]==rev, "Shared Workflow preference completes without musical Undo")
    command("channel.solo",channel=kick)
    command("channel.unsolo")
    check(not any(mask().values()), "Restore-off preference unmutes all through canonical service")
    command("edit.undo")
    command("channel.unsolo",restore=True)
    check(mask()==before, "Explicit restore override uses same saved baseline")
    rack(action="configure",restoreMuteAfterSolo=True)
    peaks=render("restored-mix.wav")
    check(peaks[0]>.01 and peaks[1]>.01, "Restored score renders on both sides")
    (output/"blocked-parent").write_text("Owned failure fixture",encoding="utf-8")
    job=client.data("vibera_project",{"action":"export","path":str(output/"blocked-parent"/"fail.wav"),"overwrite":True})
    deadline=time.monotonic()+40
    while job["completionState"]=="pending" and time.monotonic()<deadline:
        time.sleep(.03)
        job=client.call("vibera_project",{"action":"job","operation":job["operation"]},allow_failure=True)["structuredContent"]
    check(job["completionState"]=="failed" and job["error"]["code"]=="operation_failed", "Actual failed writer has terminal failure, never fake completed")
    receipt.update(project=str(baseline),soloProject=str(saved),soloPeaks=solo_peaks,restoredPeaks=peaks)
    client.close()
    receipt.update(passed=True,exitCode=client.process.returncode)
except Exception as error:
    receipt["error"]=str(error)
    raise
finally:
    if client and client.process.poll() is None: client.close()
    receipt["elapsedSeconds"]=time.monotonic()-began
    (output/"verification.json").write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
print(json.dumps({k:receipt[k] for k in ["passed","checks","elapsedSeconds","applicationSha256"]}))
