"""Actual stdio Mixer grouping/selection/saved-recipe acceptance, no OS input/audio.

Usage: verify-mixer-organisation-mcp.py ABS_EXECUTABLE ABS_OWNED_OUTPUT
Uses disposable --mcp-test settings through the existing provider-neutral client.
No plugin descriptors/paid providers/hardware/listening are represented here.
No existing project/profile is overwritten; each attempt is retained separately.
"""
from __future__ import annotations
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import time
import traceback
import uuid
import xml.etree.ElementTree as ET
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
OWNER={"schemaVersion":1,"owner":"VibeRa verify-mixer-organisation-mcp"}
OWNER_FILE=".vibera-mixer-organisation-mcp-owner.json"

def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()

def ordinary(path):
    for candidate in (path,*path.parents):
        try: stat=candidate.lstat()
        except FileNotFoundError: continue
        if candidate.is_symlink() or getattr(stat,"st_file_attributes",0)&0x400 or not candidate.is_dir():
            raise ValueError("Owned output requires ordinary non-reparse directories: "+str(candidate))

def prepare_output(raw):
    path=Path(raw)
    if not path.is_absolute() or any(part in (".","..") for part in raw.replace("\\","/").split("/")):
        raise ValueError("Explicit absolute output without traversal required")
    if os.name=="nt" and (not re.match(r"^[A-Za-z]:[/\\]",raw) or ":" in raw[2:]):
        raise ValueError("Ordinary local drive path required")
    ordinary(path);path.mkdir(parents=True,exist_ok=True);ordinary(path)
    marker=path/OWNER_FILE
    if marker.exists():
        if marker.is_symlink() or getattr(marker.lstat(),"st_file_attributes",0)&0x400 or json.loads(marker.read_text())!=OWNER:
            raise ValueError("Output marker identity mismatch/reparse")
    else:
        if any(path.iterdir()):raise ValueError("Refuse unmarked nonempty output")
        with marker.open("x",encoding="utf-8") as stream:json.dump(OWNER,stream)
    index=path/"verification.json"
    if index.exists() and (not index.is_file() or index.is_symlink() or getattr(index.lstat(),"st_file_attributes",0)&0x400):
        raise ValueError("Result index must be ordinary file")
    attempt=path/("attempt-"+datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ-")+uuid.uuid4().hex)
    attempt.mkdir();ordinary(attempt)
    return path,attempt

def main():
    if len(sys.argv)!=3:print(__doc__,file=sys.stderr);return 2
    try:
        if not Path(sys.argv[1]).is_absolute():raise ValueError("Explicit absolute executable required")
        app=Path(sys.argv[1]).resolve(strict=True)
        if not app.is_file():raise ValueError("Executable is not a file")
        output,attempt=prepare_output(sys.argv[2])
    except Exception as error:
        print(json.dumps({"passed":False,"finished":True,"preflightError":str(error)}));return 2
    began=time.monotonic();client=None
    report={"passed":False,"finished":False,"attemptDirectory":str(attempt),"checks":[],"failures":[],
            "application":str(app),"applicationSha256":digest(app),"scriptSha256":digest(__file__),
            "nativeInput":False,"liveAudio":False,"listening":False,"hardware":False,
            "scope":"Actual canonical Mixer groups/selection/history/project files and persisted saved macro execution only"}
    def retain(name,value):(attempt/name).write_text(json.dumps(value,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    def check(value,message):
        if not value:raise AssertionError(message)
        report["checks"].append(message)
    retain("verification.json",report)
    try:
        from dev_support import project_schema
        version=project_schema(ROOT);report["authoritativeProjectVersion"]=version
        client_path=app.parent/"skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():client_path=ROOT/"skills/vibera-studio/scripts/stdio_client.py"
        loader=importlib.util.spec_from_file_location("mixer_org_neutral_client",client_path)
        module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module)
        report["clientSha256"]=digest(client_path);client=module.VibeRaClient(app)
        report.update(pid=client.process.pid,version=client.server.get("version"))
        def call(tool,args=None,allow_failure=False):
            response=client.call(tool,args or {},allow_failure=True)
            with (attempt/"rpc.jsonl").open("a",encoding="utf-8") as stream:
                stream.write(json.dumps({"tool":tool,"args":args or {},"response":response},ensure_ascii=False)+"\n")
            if response.get("isError") and not allow_failure:raise AssertionError(str(response))
            return response
        def data(tool,args=None):return call(tool,args)["structuredContent"]
        def command(name,**args):return data("vibera_command",{"command":name,"args":args})
        def state():return data("vibera_state")
        def organisation():return data("vibera_mixer",{"action":"state"})["organisation"]
        def groups():return organisation()["groups"]
        def mixer():
            result=data("vibera_inspect",{"entity":"mixer","limit":512})
            check(result["nextOffset"] is None,"Complete canonical Mixer readback")
            return [{key:value for key,value in row.items() if key!="meter"} for row in result["items"]]
        def fail(tool,args,code):
            response=call(tool,args,True);result=response.get("structuredContent",{})
            check(response.get("isError") is True and result.get("completionState")=="failed" and
                  result.get("error",{}).get("code")==code,"Typed failure: "+code)
            report["failures"].append(result)
        def save(path):
            check(path.parent==attempt and not path.exists(),"Unique owned project Save path")
            client.wait_job(data("vibera_project",{"action":"save","path":str(path),"overwrite":False}))
            check(path.is_file() and int(ET.parse(path).getroot().attrib["version"])==version,"Real current-schema project file")
        tools=client.request("tools/list",{})["tools"];retain("tools.json",tools)
        macros_tool=next(tool for tool in tools if tool["name"]=="vibera_macros")
        check(set(macros_tool["inputSchema"]["properties"]["action"]["enum"])=={"state","create","update","remove","run","bindShortcut","unbindShortcut","resetShortcuts","exportProfile","importProfile"},"Discoverable bounded saved-recipe and shortcut service")
        fields=next(tool for tool in tools if tool["name"]=="vibera_mixer")["inputSchema"]["properties"]
        check({"selectMany","selectAll","selectGroup"}<=set(fields["action"]["enum"]),"Shared multiple/group selection discoverable")
        baseline=mixer();initial=state()
        selected=data("vibera_mixer",{"action":"selectMany","ids":[1,3],"mode":"replace","active":1})["organisation"]
        check(selected["selected"]==[1,3] and selected["active"]==1 and state()["revision"]==initial["revision"],"Selection distinct from musical history/source identity")
        command("mixer.group.create",ids=[1,3],text="Original drums")
        group=groups()[0];gid=group["id"]
        check(re.fullmatch(r"[0-9a-f]{32}",gid) and group["members"]==[1,3] and mixer()==baseline,"Grouping preserves all canonical insert/routing/control values")
        command("edit.undo");check(groups()==[],"Actual group creation Undo")
        command("edit.redo");check(groups()[0]["id"]==gid,"Actual Redo preserves stable UUID")
        command("mixer.group.rename",mixerGroup=gid,text="Original percussion")
        command("mixer.group.colour",mixerGroup=gid,value=0xff5ac8d8)
        check(groups()[0]["name"]=="Original percussion" and groups()[0]["colour"]==0xff5ac8d8,"Canonical group name/marker readback")
        command("mixer.colour",mixer=1,value=0xff735bd8)
        command("mixer.colour",mixer=3,value=0xff5ac8d8)
        colour_before=mixer();colour_groups=groups();colour_selection=organisation()
        expected_colours=[dict(row,colour=0xff735bd8) if row["index"] in (1,3) else row for row in colour_before]
        applied=command("mixer.group.colourMembers",mixerGroup=gid,mixer=1)
        check(applied["completionScope"]=="canonicalMixerOrganisation" and applied["dspSettlingAcknowledged"] is False and mixer()==expected_colours and groups()==colour_groups and organisation()==dict(colour_selection,revision=state()["revision"]),"Sampled explicit group colour, independent exact full insert readback; selection/marker/routes unchanged")
        command("edit.undo");check(mixer()==colour_before,"Actual sampled-colour single Undo")
        command("edit.redo");check(mixer()==expected_colours,"Actual sampled-colour Redo")
        no_op=state();command("mixer.group.colourMembers",mixerGroup=gid,mixer=1)
        check(state()["revision"]==no_op["revision"],"Already matching colour is a genuine no-op")
        for source,code in ((0,"invalid_arguments"),(999,"mixer.group.invalidColourSource"),(2,"mixer.group.nonMemberColourSource")):
            before_error=state();before_error_rows=mixer()
            fail("vibera_command",{"command":"mixer.group.colourMembers","args":{"mixerGroup":gid,"mixer":source}},code)
            check(state()["revision"]==before_error["revision"] and mixer()==before_error_rows and groups()==colour_groups,"Typed source refusal has no model/history effects")
        fail("vibera_command",{"command":"mixer.group.colourMembers","args":{"mixerGroup":gid,"mixer":1,"value":123}},"invalid_arguments")
        command("mixer.colour",mixer=1,value=0)
        theme_baseline=mixer()
        theme_token=data("vibera_gesture",{"action":"begin","label":"Original group theme preview"})["operation"]
        data("vibera_gesture",{"action":"update","operation":theme_token,"command":"mixer.group.colourMembers","args":{"mixerGroup":gid,"mixer":1}})
        check(all(row["colour"]==0 for row in mixer() if row["index"] in (1,3)) and groups()==colour_groups,"Theme0 means each member's defaults, not sampled UI RGB")
        check(data("vibera_gesture",{"action":"cancel","operation":theme_token})["completionState"]=="cancelled" and mixer()==theme_baseline,"Actual group theme preview cancel exact")
        command("mixer.group.colourMembers",mixerGroup=gid,mixer=1)
        check(all(row["colour"]==0 for row in mixer() if row["index"] in (1,3)),"Committed theme defaults readback")
        before=state();before_rows=mixer();before_groups=groups()
        fail("vibera_command",{"command":"mixer.group.create","args":{"ids":[0,1],"text":"Forbidden Master"}},"invalid_arguments")
        check(state()["revision"]==before["revision"] and mixer()==before_rows and groups()==before_groups,"Invalid group leaves model/history revision unchanged")
        command("mixer.group.mute",mixerGroup=gid,enabled=True)
        check(all(row["muted"] for row in mixer() if row["index"] in [1,3]),"Real group mute canonical state")
        command("edit.undo");check(mixer()==before_rows,"One Undo restores group mute")
        command("mixer.group.solo",mixerGroup=gid,enabled=True)
        check(groups()[0]["solo"],"Group owns actual solo baseline")
        command("mixer.group.unsolo",mixerGroup=gid,restore=True)
        check(mixer()==before_rows and not groups()[0]["solo"],"Unsolo restores prior mutes exactly")
        data("vibera_mixer",{"action":"selectGroup","group":gid})
        check(organisation()["selected"]==[1,3],"Select stored group through shared selection")
        token=data("vibera_gesture",{"action":"begin","label":"Original group cancellation"})["operation"]
        data("vibera_gesture",{"action":"update","operation":token,"command":"mixer.group.rename","args":{"mixerGroup":gid,"text":"Temporary preview"}})
        check(groups()[0]["name"]=="Temporary preview","Gesture exposes actual live group state")
        check(data("vibera_gesture",{"action":"cancel","operation":token})["completionState"]=="cancelled" and groups()==before_groups,"Exact shared gesture cancellation")
        profile=data("vibera_macros",{"action":"state","includeSchemas":True})
        check(any(row["id"]=="mixer.group.rename" and row["explicitArguments"]["properties"]["mixerGroup"] for row in profile["supportedCommands"]),"Registry-derived typed UUID recipe arguments")
        recipe={"id":"macro.originalMixerOrganisation","name":"Original persisted group recipe","steps":[
            {"action":"mixer.group.rename","parameters":{"mixerGroup":gid,"text":"Saved recipe result"},"currentContext":False},
            {"action":"mixer.group.colour","parameters":{"mixerGroup":gid,"value":0xff735bd8},"currentContext":False},
            {"action":"mixer.group.colourMembers","parameters":{"mixerGroup":gid,"mixer":1},"currentContext":False}]}
        saved_macro=data("vibera_macros",{"action":"create","generation":profile["generation"],"macro":recipe})
        check(saved_macro["configurationCommitted"] and saved_macro["profileMatchesMemory"] and saved_macro["profileVersion"]==5,"Actual profile5 disk publication completed before saved-recipe response")
        fail("vibera_macros",{"action":"remove","id":recipe["id"],"generation":profile["generation"]},"macro.staleConfiguration")
        colour_command=next(row for row in profile["supportedCommands"] if row["id"]=="mixer.group.colourMembers")
        check(set(colour_command["explicitArguments"]["required"])=={"mixerGroup","mixer"} and colour_command["explicitArguments"]["properties"]["mixer"]["minimum"]==1,"Authoritative explicit member+UUID command schema")
        command("mixer.colour",mixer=1,value=0xff735bd8)
        before_macro_rows=mixer()
        before_macro=groups()
        run=data("vibera_macros",{"action":"run","id":recipe["id"],"generation":saved_macro["generation"]})
        check(run["completionScope"]=="canonicalMacroRun" and groups()[0]["name"]=="Saved recipe result" and groups()[0]["colour"]==0xff735bd8 and all(row["colour"]==0xff735bd8 for row in mixer() if row["index"] in (1,3)),"Actual saved recipe runner executes canonical metadata and sampled member colour steps (not batch)")
        command("edit.undo");check(groups()==before_macro and mixer()==before_macro_rows,"Single actual macro Undo restores group metadata and sampled member colours")
        command("edit.redo");check(groups()[0]["name"]=="Saved recipe result","Actual macro Redo")
        read_recipe=data("vibera_macros",{"action":"state","id":recipe["id"]})
        roundtrip=next(row for row in read_recipe["macros"] if row["id"]==recipe["id"])
        updated={key:roundtrip[key] for key in ("id","name","steps")};updated["name"]="Updated saved recipe"
        changed=data("vibera_macros",{"action":"update","generation":read_recipe["generation"],"macro":updated})
        check(changed["configurationCommitted"] and changed["profileMatchesMemory"],"Readback recipe roundtrips through actual validator/persisted update")
        project=attempt/"original-groups-and-macro.vibra";save(project);saved_groups=groups();saved_mixer=mixer()
        client.wait_job(data("vibera_project",{"action":"new","discardChanges":True}))
        client.wait_job(data("vibera_project",{"action":"open","path":str(project),"discardChanges":True}))
        check(mixer()==saved_mixer and groups()==saved_groups and organisation()["selectionPersisted"] is False,"Project Save/new/open preserves groups but honestly resets runtime selection")
        still_saved=data("vibera_macros",{"action":"state","id":recipe["id"]})
        check(still_saved["profileMatchesMemory"] and still_saved["macros"][0]["name"]==updated["name"],"Settings recipe survives project replacement and still matches disk")
        removed=data("vibera_macros",{"action":"remove","id":recipe["id"],"generation":still_saved["generation"]})
        check(removed["configurationCommitted"] and all(row["id"]!=recipe["id"] for row in removed["macros"]),"Shared persisted removal actual readback")
        fail("vibera_macros",{"action":"run","id":recipe["id"],"generation":removed["generation"]},"macro.unavailable")
        report["passed"]=True
    except Exception as error:report.update(error=str(error),traceback=traceback.format_exc())
    finally:
        if client is not None:
            try:client.close();report.update(gracefulClose=True,exitCode=client.process.returncode)
            except Exception as error:report.update(passed=False,closeError=str(error),exitCode=client.process.poll())
            (attempt/"application-stderr.txt").write_text("".join(client.stderr),encoding="utf-8")
        report.update(finished=True,seconds=time.monotonic()-began);retain("verification.json",report)
        (output/"verification.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print(json.dumps({key:report.get(key) for key in ("passed","finished","seconds","attemptDirectory","error")}))
    return 0 if report["passed"] and report.get("exitCode")==0 else 1
if __name__=="__main__":raise SystemExit(main())
