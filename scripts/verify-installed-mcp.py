"""Qualify installed VST3 creation/effect ordering/state/export through semantic MCP.

Uses an explicit installed-plugin project fixture, never downloads/bundles plugins.
"""
import hashlib
import json
import pathlib
import sys
import tempfile
import time
import wave
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
selected_executable = pathlib.Path(sys.argv[1]).resolve()
packaged_client = selected_executable.parent / "skills/vibera-studio/scripts"
client_directory = packaged_client if (packaged_client / "stdio_client.py").exists() else ROOT / "skills/vibera-studio/scripts"
sys.path.insert(0, str(client_directory))
from stdio_client import VibeRaClient


def await_command_result(admission, call, record, clock=time.monotonic, pause=time.sleep):
    """Observe the exact admitted owner; never retry the canonical mutation."""
    started = clock()
    deadline = started + 25  # Same bound as this verifier's existing plugin job observer.
    current = admission
    token = admission.get("operation")
    asynchronous = admission.get("completionState") == "pending"
    polls = 0
    try:
        while current.get("completionState") == "pending":
            if clock() >= deadline:
                raise AssertionError("Canonical command operation timed out: " + str(current))
            if isinstance(token, str) and token:
                tool, request = "vibera_mixerchain", {"action":"job", "operation":token}
            elif type(token) is int and token > 0:
                tool, request = "vibera_mixer", {"action":"routingJob", "operation":token}
            else:
                raise AssertionError("Unknown asynchronous command operation: " + str(current))
            pause(.02)
            response = call(tool, request)
            polls += 1
            current = response if tool == "vibera_mixerchain" else response["routing"]
            if type(current.get("operation")) is not type(token) or current.get("operation") != token:
                raise AssertionError("Canonical command observation changed operation: " + str(current))
        completion = current.get("completionState")
        # Direct legacy document state needs no polling. Once pending was admitted,
        # only this exact job's explicit completed terminal can satisfy the oracle.
        if completion not in (("completed",) if asynchronous else (None, "completed")):
            raise AssertionError("Canonical command did not complete: " + str(current))
        if current.get("nativeRecoveryRequired"):
            raise AssertionError("Canonical command still requires native recovery: " + str(current))
        # A direct document commit can still own ordinary worker preparation.
        # Settle that actual projection before admitting the dependent structural
        # command, using this SAME25s budget and read-only state queries only.
        if not asynchronous and current.get("pendingAudioUpdate") is True:
            project = current.get("projectIdentity")
            while current["pendingAudioUpdate"]:
                if clock() >= deadline:
                    raise AssertionError("Canonical projection did not settle: " + str(current))
                pause(.02)
                current = call("vibera_state", {})
                polls += 1
                if type(current.get("pendingAudioUpdate")) is not bool:
                    raise AssertionError("Projection observation lacks explicit pending state: " + str(current))
                if current.get("projectIdentity") != project:
                    raise AssertionError("Projection observation changed project: " + str(current))
        return current
    finally:
        record({"initial":admission, "terminal":current, "polls":polls, "seconds":clock()-started})


def main():
    executable, fixture, output = map(lambda p:pathlib.Path(p).resolve(), sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed":False, "applicationSha256":hashlib.sha256(executable.read_bytes()).hexdigest(),
              "evidence":"Actual installed VST3 through application stdio MCP; real editor attachment/view jobs, virtual/offline audio; no listening or native OS input claim"}
    report["skillClient"] = str(client_directory / "stdio_client.py")
    report["skillClientSha256"] = hashlib.sha256((client_directory / "stdio_client.py").read_bytes()).hexdigest()
    report["commandObservations"] = []
    client = None
    try:
        xml = ET.parse(fixture).getroot()
        instrument = next(p for p in xml.findall(".//PLUGIN") if p.get("instrument") == "1")
        effect = next(p for p in xml.findall(".//PLUGIN") if p.get("instrument") != "1")
        report["providers"] = [instrument.get("name"), effect.get("name")]
        client = VibeRaClient(executable)
        report["pid"] = client.process.pid
        report["version"] = client.server["version"]
        def command(request):
            admission = client.data("vibera_command", request)
            def record(observation):
                report["commandObservations"].append({"command":request["command"], **observation})
                (output / "command-observations.json").write_text(
                    json.dumps(report["commandObservations"], indent=2)+"\n", encoding="utf-8")
            return await_command_result(
                admission,
                lambda tool, args: client.call(tool, args, allow_failure=True)["structuredContent"],
                record)
        opened = client.data("vibera_project", {"action":"open","path":str(fixture),"discardChanges":True})
        assert opened["completionState"] == "completed"
        # Open commits the document before its native workers are ready. Observe
        # that prerequisite before creating a dependent third worker; otherwise
        # creation cancels the unpublished two-worker preparation and restarts it.
        initial = client.data("vibera_state")
        project = initial["projectIdentity"]
        deadline = time.monotonic()+25
        while True:
            active = initial["audio"]["pluginWorkers"]
            assert initial["projectIdentity"] == project
            if len(active) == 2 and all(w["alive"] for w in active) and not initial["pendingAudioUpdate"]:
                report["initialWorkerCount"] = len(active)
                break
            if time.monotonic() >= deadline:
                raise AssertionError("Initial installed workers did not become ready: "+initial["status"])
            time.sleep(.05)
            initial = client.data("vibera_state")
        before = client.data("vibera_inspect", {"entity":"channels"})["items"]
        created = command({"command":"plugin.instrument.create","args":{"text":instrument.get("descriptor")}})
        channels = client.data("vibera_inspect", {"entity":"channels"})["items"]
        added = next(c for c in channels if c["id"] not in {c["id"] for c in before})
        assert len(channels) == len(before)+1 and added.get("plugin")
        mixer = client.data("vibera_inspect", {"entity":"mixer"})["items"]
        insert = next(m for m in mixer if m["effects"])
        effects_before = insert["effects"]
        command({"command":"plugin.effect.insert","args":{"text":effect.get("descriptor"),"mixer":insert["index"]}})
        ordered = client.data("vibera_inspect", {"entity":"mixer"})["items"][insert["index"]]["effects"]
        assert len(ordered) == len(effects_before)+1
        added_effect = ordered[-1]["id"]
        deadline = time.monotonic()+25
        while True:
            ready = client.data("vibera_state")
            active = ready["audio"]["pluginWorkers"]
            if len(active) == 4 and all(w["alive"] for w in active) and not ready["pendingAudioUpdate"]:
                report["createdWorkerCount"] = len(active)
                break
            if time.monotonic() >= deadline:
                report["readinessFailure"] = {"status":ready["status"], "pendingAudioUpdate":ready["pendingAudioUpdate"], "workers":active}
                (output / "readiness-failure-state.json").write_text(json.dumps(ready,indent=2),encoding="utf-8")
                raise AssertionError("Created instrument/effect workers did not become ready: "+str(ready["status"]))
            time.sleep(.05)
        command({"command":"plugin.effect.move","args":{"target":added_effect,"lane":0}})
        assert client.data("vibera_inspect", {"entity":"mixer"})["items"][insert["index"]]["effects"][0]["id"] == added_effect
        command({"command":"plugin.bypass","args":{"target":added_effect,"enabled":True}})
        assert client.data("vibera_inspect", {"entity":"mixer"})["items"][insert["index"]]["effects"][0]["bypass"]
        for _ in range(4):  # bypass, order, effect creation, instrument creation
            command({"command":"edit.undo"})
        assert client.data("vibera_inspect", {"entity":"channels"})["items"] == before
        assert client.data("vibera_inspect", {"entity":"mixer"})["items"] == mixer
        # Keep only the fixture's real instrument audible; this prevents original
        # built-ins from supplying a misleading nonzero export.
        target = next(c for c in before if c.get("plugin"))
        for channel in before:
            command({"command":"channel.mute","args":{"channel":channel["id"],"enabled":channel["id"] != target["id"]}})
        deadline = time.monotonic()+20
        while True:
            state = client.data("vibera_state")
            workers = state["audio"]["pluginWorkers"]
            if len(workers) == 2 and all(w["alive"] for w in workers) and not state["pendingAudioUpdate"]:
                break
            if time.monotonic() >= deadline:
                raise AssertionError("Installed workers did not become ready: "+str(state["status"]))
            time.sleep(.05)
        report["workers"] = [{"plugin":w["plugin"],"pid":w["pid"],"latencySamples":w["latencySamples"]} for w in workers]
        saved = output / "installed-mcp.vibra"
        preserved = client.data("vibera_inspect", {"entity":"channels"})["items"]
        client.wait_job(client.data("vibera_project", {"action":"save","path":str(saved),"overwrite":True}))
        saved_xml = ET.parse(saved).getroot()
        assert all(p.get("state") for p in saved_xml.findall(".//PLUGIN"))
        client.data("vibera_project", {"action":"open","path":str(saved),"discardChanges":True})
        assert client.data("vibera_inspect", {"entity":"channels"})["items"] == preserved
        wav_file = output / "installed-mcp.wav"
        client.wait_job(client.data("vibera_project", {"action":"export","path":str(wav_file),"overwrite":True}))
        with wave.open(str(wav_file), "rb") as wav:
            assert wav.getframerate() == 48000 and wav.getsampwidth() == 3 and wav.getnframes() > 0
            assert any(wav.readframes(wav.getnframes())), "Real-instrument-only MCP export contains no PCM signal"
            report["wavFrames"] = wav.getnframes()
        # Loaded-plugin semantic operations use the same services as Controls.
        # Offline export owns independent workers; its completion does not prove
        # that Open's live/control projection has finished. Observe that actual
        # prerequisite before selecting a native runtime for Controls.
        reopened = client.data("vibera_state")
        reopened_identity = reopened["projectIdentity"]
        deadline = time.monotonic()+25
        while True:
            workers = reopened["audio"]["pluginWorkers"]
            assert reopened["projectIdentity"] == reopened_identity
            if len(workers) == 2 and all(w["alive"] for w in workers) and not reopened["pendingAudioUpdate"]:
                report["reopenedWorkerCount"] = len(workers)
                break
            if time.monotonic() >= deadline:
                report["reopenedReadinessFailure"] = {
                    "status":reopened["status"], "pendingAudioUpdate":reopened["pendingAudioUpdate"],
                    "workers":workers}
                (output / "reopened-readiness-failure.json").write_text(
                    json.dumps(reopened, indent=2)+"\n", encoding="utf-8")
                raise AssertionError("Reopened live workers did not become ready: "+reopened["status"])
            time.sleep(.05)
            reopened = client.data("vibera_state")
        runtime_plugins = client.data("vibera_plugin", {"action":"list"})["items"]
        for plugin in runtime_plugins:
            meter = plugin["nativeOutputMeter"]
            assert meter["status"] == "unsupported" and not meter["available"]
            assert meter["leftPeak"] is None and meter["rightPeak"] is None
        target = next(p for p in runtime_plugins if not p["instrument"] and p["available"])
        target_id, instance = target["id"], target["instance"]
        def plugin_job(requested, expected="completed"):
            assert requested["completionState"] == "pending", "Accepted plugin request was falsely completed"
            until = time.monotonic()+25
            while time.monotonic() < until:
                response = client.call("vibera_plugin", {"action":"job","operation":requested["operation"]}, allow_failure=True)
                state = response["structuredContent"]
                if state["completionState"] != "pending":
                    assert state["completionState"] == expected, str(state)
                    return state
                time.sleep(.02)
            raise AssertionError("Plugin operation did not become terminal")
        # Actual installed instrument AND effect native editors share their host
        # views. Every view request observes its own terminal reply, never merely
        # the accepted operation or another instance's window.
        report["wrappers"] = []
        for wrapped in runtime_plugins:
            assert wrapped["available"]
            identifier, runtime = wrapped["id"], wrapped["instance"]
            observed_views = []
            if wrapped["instrument"]:
                client.data("vibera_select", {"channel": wrapped["channel"]})
                admitted = client.data("vibera_command", {"command": "channel.pluginControls", "args": {}})
                assert admitted["completionState"] == "pending" and admitted["kind"] == "editor", str(admitted)
                completed = plugin_job(admitted)
                state = client.data("vibera_plugin", {"action": "editorState", "plugin": identifier,
                    "instance": runtime})["nativeEditor"]
                assert completed["plugin"] == identifier and completed["requestedInstance"] == runtime
                assert completed["nativeCompletionKnown"] and state["wrapperView"] == "parameters"
                assert state["wrapperReady"] and state["exists"] and state["nativeInterfaceAvailable"] is True
                observed_views.append({"entry": "channel.pluginControls", "operation": admitted["operation"],
                    "terminal": completed, "readback": state})
            for view in ("plugin", "settings", "parameters", "plugin"):
                admitted = client.data("vibera_plugin", {"action":"wrapper", "plugin":identifier,
                    "instance":runtime, "view":view})
                completed = plugin_job(admitted)
                state = client.data("vibera_plugin", {"action":"editorState", "plugin":identifier,
                    "instance":runtime})["nativeEditor"]
                assert completed["nativeCompletionKnown"] and completed["outcome"] in ("opened", "brought_to_front")
                assert state["exists"] and state["wrapperReady"] and state["wrapperView"] == view, str(state)
                current = client.data("vibera_plugin", {"action":"list"})["items"]
                assert next(p for p in current if p["id"] == identifier)["instance"] == runtime
                observed_views.append({"view":view, "operation":admitted["operation"], "terminal":completed,
                                       "readback":state})
            invalid = client.call("vibera_plugin", {"action":"wrapper", "plugin":identifier,
                "instance":runtime, "view":"unsupported"}, allow_failure=True)
            assert invalid.get("isError"), "Unknown view cannot silently choose a native tab"
            closed = plugin_job(client.data("vibera_plugin", {"action":"closeEditor", "plugin":identifier,
                "instance":runtime}))
            assert closed["outcome"] == "closed" and not client.data("vibera_plugin",
                {"action":"editorState", "plugin":identifier, "instance":runtime})["nativeEditor"]["exists"]
            report["wrappers"].append({"plugin":identifier, "name":wrapped["name"],
                "instrument":wrapped["instrument"], "instance":runtime, "views":observed_views,
                "close":closed, "invalidViewRefused":True})
        assert {p["instrument"] for p in report["wrappers"]} == {False, True}
        fresh = plugin_job(client.data("vibera_plugin", {"action":"refresh","plugin":target_id,"instance":instance}))
        assert fresh["catalogueSerial"] > 0 and fresh["generation"] > 0
        parameters, offset = [], 0
        while True:
            page = client.data("vibera_plugin", {"action":"parameters","plugin":target_id,"instance":instance,"offset":offset,"limit":23})
            parameters.extend(page["items"])
            if page["nextOffset"] is None: break
            offset = page["nextOffset"]
        assert len(parameters) == page["total"] and len({p["id"] for p in parameters}) == len(parameters)
        assert page["manualWriteSupported"] is True and "supported scalar" in page["manualHistoryScope"]
        assert all(p["normalized"] is None or 0 <= p["normalized"] <= 1 for p in parameters)
        if not page["initialSnapshotAvailable"]:
            assert all(p["initialNormalized"] is None for p in parameters)
        revision = client.data("vibera_state")["revision"]
        previous_notifications = page["notifications"]
        changed_notifications = client.data("vibera_plugin", {"action":"notifications","plugin":target_id,
                    "instance":instance,"enabled":not previous_notifications})
        assert changed_notifications["enabled"] != previous_notifications
        assert client.data("vibera_plugin", {"action":"parameters","plugin":target_id})["notifications"] != previous_notifications
        client.data("vibera_plugin", {"action":"notifications","plugin":target_id,"instance":instance,"enabled":previous_notifications})
        assert client.data("vibera_state")["revision"] == revision, "Session notifications changed project Undo"
        wrong_instance = client.call("vibera_plugin", {"action":"refresh","plugin":target_id,"instance":"stale"}, allow_failure=True)
        assert wrong_instance.get("isError") and wrong_instance["structuredContent"]["error"]["code"] == "stale_instance"
        healthy_recovery = client.call("vibera_plugin", {"action":"recover","plugin":target_id,"instance":instance}, allow_failure=True)
        assert healthy_recovery.get("isError"), "Healthy plugin recovery should be refused"
        selected = next(p for p in parameters if p["automationSupported"] and not p["hidden"])
        ui = client.data("vibera_plugin", {"action":"controls","plugin":target_id,"instance":instance})
        assert ui["controls"]["visible"]
        until = time.monotonic()+7
        while time.monotonic() < until:
            controls = client.data("vibera_state")["audio"]["pluginControls"]
            if controls and controls["ready"]: break
            time.sleep(.02)
        assert controls and controls["ready"]
        chosen = client.data("vibera_plugin", {"action":"controls","plugin":target_id,"instance":instance,"parameter":selected["id"]})
        assert chosen["controls"]["parameter"] == selected["id"]
        assert client.data("vibera_plugin", {"action":"closeControls","plugin":target_id,"instance":instance})["closed"]
        # Missing saved binding is a real asynchronous canonical validation failure.
        before_failure = client.data("vibera_state")
        failed = plugin_job(client.data("vibera_plugin", {"action":"automate","plugin":target_id,"instance":instance,
                    "parameter":selected["id"],"generation":page["generation"],"automation":"removeInit"}), "failed")
        assert failed["error"]["code"] == "validation_failed" and not failed["error"]["effectMayHaveOccurred"]
        assert client.data("vibera_state")["revision"] == before_failure["revision"], "Failed preparation changed document"
        before_clips = client.data("vibera_inspect", {"entity":"clips"})["items"]
        created = plugin_job(client.data("vibera_plugin", {"action":"automate","plugin":target_id,"instance":instance,
                    "parameter":selected["id"],"generation":page["generation"],"automation":"create"}))
        automated_clips = client.data("vibera_inspect", {"entity":"clips"})["items"]
        assert len(automated_clips) == len(before_clips)+1 and automated_clips[-1]["kind"] == "automation"
        command({"command":"edit.undo"})
        assert client.data("vibera_inspect", {"entity":"clips"})["items"] == before_clips
        command({"command":"edit.redo"})
        assert client.data("vibera_inspect", {"entity":"clips"})["items"] == automated_clips
        automation_project = output / "native-automation-mcp.vibra"
        client.wait_job(client.data("vibera_project", {"action":"save","path":str(automation_project),"overwrite":True}))
        persisted = ET.parse(automation_project).getroot()
        persisted_target = next(p for p in persisted.findall(".//PLUGIN") if int(p.get("id")) == target_id)
        assert any(int(p.get("id")) == selected["id"] for p in persisted_target.findall("./AUTOMATION_BINDINGS/BINDING"))
        client.data("vibera_project", {"action":"open","path":str(automation_project),"discardChanges":True})
        assert client.data("vibera_inspect", {"entity":"clips"})["items"] == automated_clips
        automation_wav = output / "native-automation-mcp.wav"
        client.wait_job(client.data("vibera_project", {"action":"export","path":str(automation_wav),"overwrite":True}))
        with wave.open(str(automation_wav), "rb") as wav:
            assert wav.getnframes() > 0 and any(wav.readframes(wav.getnframes()))
        report["pluginSemantic"] = {"parameterCount":len(parameters),"generation":page["generation"],
            "initialSnapshotAvailable":page["initialSnapshotAvailable"],"target":target_id,"parameter":selected["id"],"startup":target.get("startup"),
            "refreshState":fresh["completionState"],"asyncFailure":failed["error"]["code"],
            "automationState":created["completionState"],"controlsObserved":True,"sessionNotificationsRestored":True,
            "automationProject":str(automation_project),"automationWav":str(automation_wav),"nativeManualUndo":False}
        # The wrapper's new structural service must also run against this actual
        # installed effect, not only the original SDK bus-choice fixture.
        ready_until = time.monotonic()+25
        while True:
            state = client.data("vibera_state")
            current = next(p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                           if p["id"] == target_id)
            if current["available"] and not state["pendingAudioUpdate"]:
                break
            if time.monotonic() >= ready_until:
                raise AssertionError("Installed effect not ready for structural wrapper I/O")
            time.sleep(.02)
        current_instance = current["instance"]
        original_io = client.data("vibera_plugin", {"action":"buses", "plugin":target_id,
            "instance":current_instance})["audioIo"]["requested"]
        io_wrapper = plugin_job(client.data("vibera_plugin", {"action":"wrapper", "plugin":target_id,
            "instance":current_instance, "view":"settings"}))
        assert io_wrapper["editorState"]["wrapperView"] == "settings"
        noop_revision = client.data("vibera_state")["revision"]
        io_noop = plugin_job(client.data("vibera_plugin", {"action":"audioIo", "plugin":target_id,
            "instance":current_instance, "pluginIo":original_io}))
        assert not io_noop["historyCommitted"] and not io_noop["documentChanged"]
        assert io_noop["canonicalOperation"] == "" and client.data("vibera_state")["revision"] == noop_revision
        mono_io = {**original_io, "inputChannels":1, "monoSource":3}
        assert mono_io != original_io, "Installed fixture must begin with its original stereo input"
        io_changed = plugin_job(client.data("vibera_plugin", {"action":"audioIo", "plugin":target_id,
            "instance":current_instance, "pluginIo":mono_io}))
        assert io_changed["historyCommitted"] and io_changed["documentChanged"] and io_changed["undoable"]
        assert io_changed["requestedIo"] == mono_io and io_changed["structuralOperation"]["completionState"] == "completed"
        assert not io_changed["structuralOperation"]["nativeRecoveryRequired"]
        closures = io_changed["structuralOperation"]["editorClosures"]
        assert any(c["plugin"] == target_id and c["instance"] == current_instance and
                   c["outcome"] == "closed" and c["nativeCompletionKnown"] for c in closures)
        changed_runtime = next(p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                               if p["id"] == target_id)
        assert changed_runtime["instance"] != current_instance
        negotiated = client.data("vibera_plugin", {"action":"buses", "plugin":target_id,
            "instance":changed_runtime["instance"]})["audioIo"]
        assert negotiated["requested"] == mono_io and negotiated["negotiated"] == mono_io
        mono_wav = output / "native-mono-input-mcp.wav"
        client.wait_job(client.data("vibera_project", {"action":"export", "path":str(mono_wav), "overwrite":True}))
        with wave.open(str(mono_wav), "rb") as wav:
            assert wav.getnframes() > 0 and any(wav.readframes(wav.getnframes())), "Changed native-input export contains no PCM"
        command({"command":"edit.undo"})
        restored_runtime = next(p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                                if p["id"] == target_id)
        restored_io = client.data("vibera_plugin", {"action":"buses", "plugin":target_id,
            "instance":restored_runtime["instance"]})["audioIo"]
        assert restored_io["requested"] == original_io and restored_io["negotiated"] == original_io
        durable_io = client.data("vibera_plugin", {"action":"job", "operation":io_changed["operation"]})
        assert durable_io["completionState"] == "completed" and durable_io["requestedIo"] == mono_io
        report["wrapperAudioIo"] = {"provider":current["name"], "noop":io_noop,
            "changed":io_changed, "negotiated":negotiated, "restored":restored_io,
            "survivesReplacement":True, "hostedWav":str(mono_wav)}
        # Qualify containing-chain wrapper services on the genuine installed
        # effect, in the owned pre-automation project (portable chains do not
        # silently absorb a different project's live automation dependencies).
        client.data("vibera_project", {"action":"open", "path":str(saved), "discardChanges":True})
        chain_until = time.monotonic()+25
        while True:
            chain_state = client.data("vibera_state")
            owners = client.data("vibera_plugin", {"action":"list"})["items"]
            effect_owner = next(p for p in owners if not p["instrument"])
            if effect_owner["available"] and not chain_state["pendingAudioUpdate"]:
                break
            if time.monotonic() >= chain_until:
                raise AssertionError("Installed containing-chain source not ready")
            time.sleep(.02)
        def chain_job(task, owner, **options):
            return plugin_job(client.data("vibera_plugin", {"action":"chain", "chainAction":task,
                "plugin":owner["id"], "instance":owner["instance"], **options}))
        # A fresh owned folder per run: a retained earlier attempt keeps its own presets.
        preset_folder = pathlib.Path(tempfile.mkdtemp(prefix="chain-presets-", dir=output))
        configured = chain_job("configureDirectory", effect_owner, directory=str(preset_folder))
        assert configured["configuredDirectory"] == str(preset_folder) and not configured["documentChanged"]
        independent = next(m["index"] for m in client.data("vibera_inspect", {"entity":"mixer"})["items"]
                           if m["index"] > 0 and m["index"] != effect_owner["mixer"])
        client.data("vibera_mixer", {"action":"select", "insert":independent})
        plugin_job(client.data("vibera_plugin", {"action":"wrapper", "plugin":effect_owner["id"],
            "instance":effect_owner["instance"], "view":"settings"}))
        copied = chain_job("copy", effect_owner)
        assert copied["mixer"] == effect_owner["mixer"] and not copied["documentChanged"]
        assert any(c["plugin"] == effect_owner["id"] and c["instance"] == effect_owner["instance"] and
                   c["outcome"] == "closed" and c["nativeCompletionKnown"] for c in copied["editorClosures"])
        assert all(p["stateSource"] == "nativeAcknowledgement" for p in copied["structuralOperation"]["provenance"])
        chain_file = preset_folder / "installed-effect-chain.vibfx"
        chain_saved = chain_job("save", effect_owner, path=str(chain_file), overwrite=False)
        assert chain_saved["structuralOperation"]["fileCommitted"] and chain_file.read_bytes()[:8] == b"VibeRaFX"
        original_chain = client.data("vibera_inspect", {"entity":"mixer"})["items"][effect_owner["mixer"]]["effects"]
        command({"command":"plugin.effect.mix", "args":{"target":effect_owner["id"], "value":.25}})
        # This provider test has no active device. Its deferred projection may
        # prepare another runtime after a document edit; target the exact current
        # instance rather than reusing a handle captured before that edit.
        paste_owner = next(p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                           if p["id"] == effect_owner["id"])
        pasted = chain_job("paste", paste_owner)
        after_chain = client.data("vibera_inspect", {"entity":"mixer"})["items"][effect_owner["mixer"]]["effects"]
        assert pasted["documentChanged"] and pasted["undoable"] and pasted["historyCommitted"]
        assert len(after_chain) == len(original_chain) and after_chain[0]["id"] != effect_owner["id"]
        assert after_chain[0]["wet"] == original_chain[0]["wet"]
        assert client.data("vibera_state")["selectedInsert"] == independent
        durable_chain = client.data("vibera_plugin", {"action":"job", "operation":pasted["operation"]})
        assert durable_chain["completionState"] == "completed" and durable_chain["plugin"] == effect_owner["id"]
        command({"command":"edit.undo"})
        restored_chain = client.data("vibera_inspect", {"entity":"mixer"})["items"][effect_owner["mixer"]]["effects"]
        assert restored_chain[0]["id"] == effect_owner["id"] and restored_chain[0]["wet"] == .25
        restored_owner = next(p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                              if p["id"] == effect_owner["id"])
        loaded_chain = chain_job("loadApply", restored_owner, path=str(chain_file))
        assert loaded_chain["documentChanged"] and loaded_chain["structuralOperation"]["completionState"] == "completed"
        chain_wav = output / "native-containing-chain-mcp.wav"
        client.wait_job(client.data("vibera_project", {"action":"export", "path":str(chain_wav), "overwrite":True}))
        with wave.open(str(chain_wav), "rb") as wav:
            assert wav.getnframes() > 0 and any(wav.readframes(wav.getnframes()))
        report["wrapperContainingChain"] = {"provider":effect_owner["name"], "copied":copied,
            "saved":chain_saved, "pasted":pasted, "loaded":loaded_chain,
            "historicalParentAfterReplacement":True, "independentSelectedInsert":independent,
            "hostedWav":str(chain_wav)}
        report["capabilities"] = ["actual descriptor instrument creation", "actual effect insertion/order/bypass",
            "exact canonical Undo", "owned worker readiness and latency readback", "plugin-state save/reopen", "instrument-only hosted WAV export",
            "actual installed instrument/effect native wrapper attachment, same-instance views and acknowledged close"]
        report["limits"] = ["No live audio device/listening qualification", "No arbitrary native editor control or parameter gesture qualification",
                             "MVibratoMB demo expiry limits audio-quality/long-duration conclusions"]
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        if client:
            try:
                client.close()
                report["exitCode"] = client.process.returncode
            except Exception as failure:
                report["passed"] = False
                report["shutdownError"] = str(failure)
            (output / "stderr.log").write_text("".join(client.stderr), encoding="utf-8")
        (output / "verification.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    if not report["passed"]:
        raise RuntimeError(str(report))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
