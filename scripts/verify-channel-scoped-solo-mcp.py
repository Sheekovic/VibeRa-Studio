"""Actual engineering-app Channel scoped-solo/lock/history/persistence workflow.

Usage: verify-channel-scoped-solo-mcp.py ABSOLUTE_ENGINEERING_APP ABSOLUTE_OWNED_OUTPUT
Uses the existing provider-neutral disposable --mcp-test client and output guard.
Original built-in score and offline WAV; no live playback, native input or hardware claim.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Required existing helper unavailable: " + str(path))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    app = Path(sys.argv[1])
    if not app.is_absolute() or not app.is_relative_to(ROOT / "build/windows"):
        raise ValueError("Current engineering build/windows executable required; qualified dist/package-stage is not this probe's target")
    admission = load_module("scoped_solo_output_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    output = admission.owned_output_path(sys.argv[2], app)
    admission.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    receipt = {"passed": False, "finished": False, "checks": [], "observations": [], "jobs": [], "responses": 0,
               "scope": "Actual canonical built-in score, group/global/Unsorted solo, locks, history, saved recipes and file jobs",
               "nativeInput": False, "physicalMidi": False, "hardware": False, "listening": False, "liveAudio": False,
               "application": str(app), "applicationSha256": digest(app), "scriptSha256": digest(Path(__file__)),
               "outputAdmissionSha256": digest(ROOT / "scripts/verify-plugin-io-mcp.py")}
    started, client = time.monotonic(), None

    def retain():
        text = json.dumps(receipt, indent=2, ensure_ascii=False) + "\n"
        (attempt / "verification.json").write_text(text, encoding="utf-8")
        (output / "verification.json").write_text(text, encoding="utf-8")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        receipt["checks"].append(message)

    retain()
    try:
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        client = load_module("scoped_solo_neutral_client", client_path).VibeRaClient(app)
        receipt.update(pid=client.process.pid, clientSha256=digest(client_path), version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            receipt["responses"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected actual refusal: " + str(response))
            return response

        data = lambda tool, args=None: call(tool, args)["structuredContent"]
        state = lambda: data("vibera_state")
        rack = lambda: data("vibera_rack", {"action": "state"})
        inspect = lambda entity, **args: data("vibera_inspect", {"entity": entity, **args})["items"]

        def wait(job, tool, action):
            initial, current, began = job, job, time.monotonic()
            try:
                while current["completionState"] == "pending" and time.monotonic() - began < 40:
                    time.sleep(.02)
                    current = data(tool, {"action": action, "operation": initial["operation"]})
                    check(current["operation"] == initial["operation"], "Actual asynchronous job retains its accepted owner token")
                check(current["completionState"] == "completed", "Actual terminal file/Channel job completion")
                return current
            finally:
                receipt["jobs"].append({"initial": initial, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def command(name, **args):
            result = data("vibera_command", {"command": name, "args": args})
            check(result.get("completionState") in (None, "completed"), "Canonical solo/lock/history edit completed, not queued: " + name)
            if name in {"channel.solo", "channel.unsolo", "channel.muteLock", "rack.muteLock",
                        "channel.mute", "rack.muteSelected", "rack.unmuteSelected"}:
                check(result.get("completionState") == "completed" and
                      result.get("completionScope") == "canonicalChannelMute" and
                      result.get("dspSettlingAcknowledged") is False,
                      "Explicit canonical commit, without a false DSP-settling acknowledgement: " + name)
            return result

        def solo_fields(view):
            return {key: view[key] for key in ("soloChannel", "soloScope", "soloGroup", "soloAffected", "soloRestoreMuted")}

        def document():
            current = state()
            return {"channels": inspect("channels"), "solo": solo_fields(rack()), "groups": rack()["groups"],
                    "patterns": inspect("patterns"), "notes": inspect("notes", pattern=current["selectedPattern"]),
                    "clips": inspect("clips"), "samplers": {str(item["id"]): {
                        key: value for key, value in item["sampler"].items()
                        if key not in {"normalizationAvailable", "sourcePeak", "normalizationGain"}}
                        for item in current["channels"]}}

        def protected():
            current = state()
            return {"document": document(), **{key: current[key] for key in
                    ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable")}}

        def no_op(name, args):
            before = protected()
            command(name, **args)
            check(protected() == before, "No-op preserves exact solo baseline/mutes/locks/revision/dirty/Undo/Redo: " + name)

        def edit(name, args, verify):
            before, revision = document(), state()["revision"]
            command(name, **args)
            after = document()
            check(after != before and state()["revision"] == revision + 1, "One actual canonical history change: " + name)
            verify()
            command("edit.undo")
            check(document() == before, "One exact Undo restores music, group, mutes, locks and solo ownership: " + name)
            # A real pending Redo must survive a no-op lock write.
            locked = next(item["muteLocked"] for item in inspect("channels") if item["id"] == b)
            no_op("channel.muteLock", {"channel": b, "enabled": locked})
            command("edit.redo")
            check(document() == after, "One exact Redo restores the same operation: " + name)

        def refuse(name, args):
            before = protected()
            reply = call("vibera_command", {"command": name, "args": args}, True)
            error = reply.get("structuredContent", {}).get("error", {})
            check(reply.get("isError") is True and bool(error.get("code")) and bool(error.get("message")),
                  "Actual structured refusal: " + name + " " + str(args))
            check(protected() == before, "Refusal leaves exact document, first baseline and history unchanged")

        def expect(label, muted, owner=0, scope=None, group=None, affected=(), locks=(False, True, False, False), baseline=()):
            current, view, rows = state(), rack(), inspect("channels")
            expected_mask, expected_locks = dict(zip(ids, muted)), dict(zip(ids, locks))
            actual_mask = {item["id"]: item["muted"] for item in rows}
            actual_locks = {item["id"]: item["muteLocked"] for item in rows}
            actual_solo = solo_fields(view)
            receipt["observations"].append({"case": label, "mask": actual_mask, "locks": actual_locks, "solo": actual_solo})
            retain()  # Preserve actual facts even if the following independent literal oracle fails.
            check(actual_mask == expected_mask and actual_locks == expected_locks,
                  label + ": literal mute/lock masks match, including protected peers")
            check(actual_solo["soloChannel"] == owner and actual_solo["soloScope"] == scope and
                  actual_solo["soloGroup"] == group and set(actual_solo["soloAffected"]) == set(affected) and
                  set(actual_solo["soloRestoreMuted"]) == set(baseline), label + ": exact owner/scope/group/affected/first baseline")
            check(solo_fields(current) == actual_solo, label + ": state and Rack expose identical canonical solo ownership")
            check({item["id"]: (item["muted"], item["muteLocked"]) for item in current["channels"]} ==
                  {key: (expected_mask[key], expected_locks[key]) for key in ids}, label + ": state and inspect Channel flags agree")
            check(all((item["muted"], item["muteLocked"]) == (expected_mask[item["id"]], expected_locks[item["id"]])
                      for item in view["rows"]), label + ": filtered Rack rows retain real stable-ID flags")

        tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
        check({"vibera_command", "vibera_commands", "vibera_state", "vibera_rack", "vibera_macros", "vibera_project"} <= tools.keys(),
              "Actual discovery includes shared canonical/readback/configuration/file tools")
        (attempt / "discovery.json").write_text(json.dumps(tools, indent=2) + "\n", encoding="utf-8")
        for name in ("channel.solo", "channel.unsolo", "channel.muteLock", "rack.muteLock"):
            entries = data("vibera_commands", {"command": name, "includeSchemas": True})["operations"]
            check(len(entries) == 1 and entries[0]["id"] == name and entries[0]["documentEdit"] is True and
                  entries[0]["mapping"] == "vibera_command", "Discoverable exact shared undoable command: " + name)
            if name == "channel.solo":
                fields = entries[0]["arguments"]["properties"]
                check(fields["soloScope"]["enum"] == ["global", "group"] and fields["soloScope"]["default"] == "global" and
                      fields["enabled"]["type"] == fields["restore"]["type"] == "boolean",
                      "Discovery distinguishes absolute enable, group/global default and restoration")
        check(data("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})["completionState"] == "completed",
              "Actual disposable built-in project created")
        ids = [item["id"] for item in inspect("channels")]
        check(len(ids) == 4, "Original empty template supplies four independent built-in Channels")
        a, b, c, d = ids
        pattern = state()["selectedPattern"]
        for index, channel in enumerate(ids):
            command("notes.add", pattern=pattern, channel=channel, beat=index * .5, length=1, pitch=60 + index, value=.7)
        command("clip.add", pattern=pattern, beat=0, length=4)
        command("clip.add", pattern=pattern, beat=4, length=4)
        command("rack.group.assign", ids=[a, c], text="Original drums")
        command("rack.group.assign", ids=[b], text="Other group")
        drums = next(item["id"] for item in rack()["groups"] if item["name"] == "Original drums")
        other = next(item["id"] for item in rack()["groups"] if item["name"] == "Other group")
        command("channel.mute", channel=b, enabled=True)
        command("channel.mute", channel=c, enabled=True)
        command("channel.muteLock", channel=b, enabled=True)
        data("vibera_rack", {"action": "select", "ids": [d, b], "activeChannel": d})
        data("vibera_rack", {"action": "filter", "kind": "group", "group": other})
        independent_view = {key: rack()[key] for key in ("activeChannel", "selectedChannels", "filter")}
        original_music = {"notes": inspect("notes", pattern=pattern), "clips": inspect("clips"),
                          "sources": {item["id"]: item["source"] for item in inspect("channels")}}
        first = [b, c]
        baseline_mask = (False, True, True, False)
        expect("Mixed groups/Unsorted and initial locked muted peer", baseline_mask)

        group_a = {"channel": a, "enabled": True, "soloScope": "group"}
        edit("channel.solo", group_a, lambda: expect("Group uses hidden target membership, not filter/selection", baseline_mask,
             a, "group", drums, [a, c], baseline=first))
        no_op("channel.solo", group_a)
        edit("channel.solo", {"channel": c, "enabled": True, "soloScope": "group"},
             lambda: expect("Switch target keeps first mixed baseline", (True, True, False, False), c, "group", drums, [a, c], baseline=first))
        edit("channel.solo", {"channel": d, "enabled": True, "soloScope": "global"},
             lambda: expect("Switch scope restores old affected before global application", (True, True, True, False),
                            d, "global", None, [a, c, d], baseline=first))
        edit("channel.unsolo", {"restore": True}, lambda: expect("Exit restores only owned affected to first baseline", baseline_mask))
        edit("channel.solo", {"channel": d, "enabled": True, "soloScope": "group"},
             lambda: expect("Unsorted0 is a valid actual target scope", baseline_mask, d, "group", 0, [d], baseline=first))
        command("channel.unsolo", restore=True)
        command("channel.solo", channel=c, enabled=True, soloScope="group")
        edit("channel.unsolo", {"restore": False},
             lambda: expect("Restore-off unmutes affected only, preserving locked outsider", (False, True, False, False)))
        command("edit.undo")
        command("channel.unsolo", restore=True)
        edit("channel.solo", {"channel": a, "enabled": True},
             lambda: expect("Omitted scope retains legacy global default", (False, True, True, True), a, "global", None, [a, c, d], baseline=first))
        command("channel.unsolo", restore=True)
        check({key: rack()[key] for key in independent_view} == independent_view, "Solo/scope/restore never redirect active MIDI recipient, bulk selection or display filter")

        command("channel.solo", **group_a)
        no_op("channel.muteLock", {"channel": b, "enabled": True})
        edit("channel.muteLock", {"channel": c, "enabled": True},
             lambda: expect("Actual lock change cancels ownership without rewriting audible mask", baseline_mask,
                            locks=(False, True, True, False)))
        command("channel.solo", **group_a)
        expect("Locked in-group peer excluded from affected", baseline_mask, a, "group", drums, [a],
               locks=(False, True, True, False), baseline=first)
        refuse("channel.solo", {"channel": c, "enabled": True, "soloScope": "group"})
        refuse("channel.mute", {"channel": b, "enabled": False})
        no_op("rack.muteLock", {"ids": [c, b], "enabled": True})
        no_op("rack.muteSelected", {"ids": [b, c]})
        no_op("rack.unmuteSelected", {"ids": [c, b]})
        edit("rack.muteSelected", {"ids": [a, b, c, d]},
             lambda: expect("Bulk mute skips locks and ends effective manual ownership", (True, True, True, True), locks=(False, True, True, False)))
        edit("rack.unmuteSelected", {"ids": [d, b, a, c]},
             lambda: expect("Bulk unmute skips locked peers", baseline_mask, locks=(False, True, True, False)))
        command("channel.solo", **group_a)
        edit("rack.muteLock", {"ids": [a, d], "enabled": True},
             lambda: expect("Actual bulk lock preserves mutes while ending solo", baseline_mask, locks=(True, True, True, True)))
        refuse("channel.solo", {"channel": a, "enabled": True})
        command("edit.undo")
        edit("channel.mute", {"channel": a, "enabled": True},
             lambda: expect("Manual mute ends restore session without reverting peers", (True, True, True, False), locks=(False, True, True, False)))
        command("edit.undo")
        command("channel.unsolo", restore=True)
        command("channel.muteLock", channel=c, enabled=False)
        command("channel.mute", channel=c, enabled=False)

        second_mask = (False, True, False, False)
        command("channel.solo", **group_a)
        expect("Second group baseline contains only original muted b", (False, True, True, False), a, "group", drums, [a, c], baseline=[b])
        edit("rack.group.assign", {"ids": [c], "target": 0, "text": ""},
             lambda: expect("Leaving peer restores its first mute and leaves ownership", second_mask, a, "group", drums, [a], baseline=[b]))
        edit("rack.group.assign", {"ids": [d], "target": drums},
             lambda: expect("Joining peer enters affected and is muted", (False, True, False, True), a, "group", drums, [a, d], baseline=[b]))
        edit("rack.group.assign", {"ids": [a], "target": other},
             lambda: expect("Moving target out of captured group exits and restores", second_mask))
        command("edit.undo")  # Restore active a,d scope.
        command("edit.undo")  # Restore d to Unsorted.
        command("edit.undo")  # Restore c to active Drums scope.
        before_delete_group = document()
        edit("rack.group.delete", {"target": drums}, lambda: expect("Deleting captured group exits and restores", second_mask))
        check(not any(item["id"] == drums for item in rack()["groups"]), "Actual scoped group was deleted, not merely hidden")
        command("edit.undo")
        check(document() == before_delete_group, "Undo group deletion restores exact membership/solo/notes")
        before_delete_target = document()
        prepared_fields = ("normalizationAvailable", "sourcePeak", "normalizationGain")
        before_prepared = next(item["sampler"] for item in state()["channels"] if item["id"] == a)
        before_prepared = {key: before_prepared[key] for key in prepared_fields}
        deleted = data("vibera_rack", {"action": "delete", "ids": [a]})
        check(deleted["completionState"] == "pending" and deleted["modelCommitted"] is False, "Actual target deletion admission is not completion")
        wait(deleted, "vibera_rack", "job")
        survivors = inspect("channels")
        check([item["id"] for item in survivors] == [b, c, d] and
              {item["id"]: item["muted"] for item in survivors} == {b: True, c: False, d: False} and
              solo_fields(rack())["soloChannel"] == 0 and rack()["soloScope"] is None and not rack()["soloAffected"],
              "Actual deleted target restores surviving affected only and clears ownership")
        command("edit.undo")
        check(document() == before_delete_target, "Target deletion Undo restores captured scope/locks/notes/identities")
        began = time.monotonic()
        while True:
            actual_prepared = next(item["sampler"] for item in state()["channels"] if item["id"] == a)
            actual_prepared = {key: actual_prepared[key] for key in prepared_fields}
            if actual_prepared == before_prepared or time.monotonic() - began >= 5:
                break
            time.sleep(.01)
        check(actual_prepared == before_prepared,
              "After structural Undo, asynchronous sample preparation restores exact source peak/gain/availability")
        receipt["samplePreparationRecoverySeconds"] = time.monotonic() - began
        command("edit.redo")
        check([item["id"] for item in inspect("channels")] == [b, c, d], "Target deletion Redo removes same ID")
        command("edit.undo")
        check(document() == before_delete_target, "Target restored before persistence and invalid-input checks")

        for name, args in (
            ("channel.solo", {"channel": 999999, "enabled": True, "soloScope": "group"}),
            ("channel.solo", {"channel": a, "enabled": True, "soloScope": "selection"}),
            ("channel.solo", {"channel": a, "enabled": True, "soloScope": 0}),
            ("channel.solo", {"channel": a, "enabled": True, "soloScope": "group", "group": other}),
            ("channel.solo", {"channel": a, "enabled": "true"}),
            ("channel.solo", {"channel": d, "enabled": False, "restore": True}),
            ("channel.unsolo", {"restore": "false"}),
            ("channel.muteLock", {"channel": a, "enabled": 1}),
            ("rack.muteLock", {"ids": [a, 999999], "enabled": True}),
            ("rack.muteLock", {"ids": [a, a], "enabled": True}),
            ("rack.muteLock", {"ids": [], "enabled": True}),
            ("rack.muteLock", {"ids": [a], "enabled": True, "soloScope": "group"}),
        ):
            refuse(name, args)
        check(inspect("notes", pattern=pattern) == original_music["notes"] and inspect("clips") == original_music["clips"] and
              {item["id"]: item["source"] for item in inspect("channels")} == original_music["sources"],
              "Completed solo/lock/group/delete-Undo scenarios preserve exact original notes, linked arrangement and source identity")

        target = attempt / "original-active-group-solo.vibra"
        saved_document = document()
        wait(data("vibera_project", {"action": "save", "path": str(target), "overwrite": False}), "vibera_project", "job")
        xml = ET.parse(target).getroot()
        schema = load_module("scoped_solo_project_contract", ROOT / "scripts/dev_support.py").project_schema(ROOT)
        check(schema >= 28 and int(xml.attrib["version"]) == schema,
              "Actual scoped-solo file uses the authoritative current schema (scope/lock introduced28)")
        stored = {int(item.attrib["id"]): item.attrib for item in xml.find("CHANNELS")}
        check(stored[b]["muteLocked"] in ("1", "true") and all(stored[key].get("muteLocked", "0") in ("0", "false") for key in (a, c, d)),
              "Independent saved XML retains literal Channel lock flags")
        saved_solo = xml.find("CHANNEL_SOLO")
        check(int(saved_solo.attrib["channel"]) == a and saved_solo.attrib["groupScoped"] in ("1", "true") and
              int(saved_solo.attrib["group"]) == drums and
              {int(item.attrib["channel"]) for item in saved_solo.findall("AFFECTED")} == {a, c} and
              {int(item.attrib["channel"]) for item in saved_solo.findall("MUTED")} == {b},
              "Independent saved XML contains exact active group, affected IDs and original mute baseline")
        wait(data("vibera_project", {"action": "open", "path": str(target), "discardChanges": True}), "vibera_project", "job")
        check(document() == saved_document and not state()["dirty"], "Actual Save/reopen retains active scoped ownership, first baseline, affected IDs and score")
        expect("Reopened active Drums scope", (False, True, True, False), a, "group", drums, [a, c], baseline=[b])

        def render(name):
            path = attempt / name
            wait(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}),
                 "vibera_project", "job")
            with wave.open(str(path), "rb") as wav:
                check(wav.getframerate() == 48000 and wav.getsampwidth() == 3 and
                      wav.getnchannels() == 2 and wav.getnframes() > 0,
                      "Actual offline stereo 48kHz/24-bit arrangement WAV: " + name)
                pcm = wav.readframes(wav.getnframes())
            check(any(pcm), "Export contains actual nonzero score PCM: " + name)
            receipt.setdefault("renders", []).append({"path": str(path), "sha256": digest(path),
                                                       "pcmSha256": hashlib.sha256(pcm).hexdigest()})
            return pcm

        scoped_pcm = render("scoped-group.wav")
        command("channel.unsolo", restore=True)
        restored_pcm = render("restored-group.wav")
        check(restored_pcm != scoped_pcm, "Restored peer makes an actual audible-score PCM difference")
        command("channel.mute", channel=c, enabled=True)
        check(render("manual-mute-reference.wav") == scoped_pcm,
              "Scoped solo renders exactly the independently applied literal peer-mute reference")
        command("edit.undo")
        command("edit.undo")
        check(document() == saved_document, "Render comparison history restores exact reopened scope, score and lock baseline")
        edit("channel.unsolo", {"restore": True}, lambda: expect("Reopened session restores pre-solo mutes exactly", second_mask))
        command("edit.undo")

        configuration = data("vibera_macros", {"action": "state", "includeSchemas": True})
        check({"channel.solo", "channel.muteLock", "rack.muteLock"} <= {item["id"] for item in configuration["supportedCommands"]},
              "Actual saved macro runner discovers scoped-solo and lock commands")
        recipe = {"id": "macro.scoped-solo-proof", "name": "Original scope switch", "steps": [
            {"action": "channel.solo", "parameters": {"channel": a, "enabled": True, "soloScope": "global"}, "currentContext": False},
            {"action": "channel.solo", "parameters": {"channel": c, "enabled": True, "soloScope": "group"}, "currentContext": False}]}
        before_configuration = protected()
        persisted = data("vibera_macros", {"action": "create", "generation": configuration["generation"], "macro": recipe})
        check(persisted["profileVersion"] == 8 and persisted["profileMatchesMemory"] and persisted["configurationCommitted"] and
              protected() == before_configuration, "Explicit soloScope recipe persists as profile8 without Project history")
        readback = data("vibera_macros", {"action": "state", "id": recipe["id"]})
        stored_recipe = next(item for item in readback["macros"] if item["id"] == recipe["id"])
        check(stored_recipe["persisted"] and [step["parameters"]["soloScope"] for step in stored_recipe["steps"]] == ["global", "group"] and
              [step["parameters"]["channel"] for step in stored_recipe["steps"]] == [a, c] and
              all(step["currentContext"] is False for step in stored_recipe["steps"]), "Persisted recipe preserves explicit typed scope and stable targets")
        malformed = json.loads(json.dumps(recipe))
        malformed["id"] = "macro.invalid-scope-proof"
        malformed["steps"][0]["parameters"]["soloScope"] = "filter"
        denied = call("vibera_macros", {"action": "create", "generation": readback["generation"], "macro": malformed}, True)
        unchanged_configuration = data("vibera_macros", {"action": "state"})
        check(denied.get("isError") is True and denied["structuredContent"].get("error", {}).get("code") and
              unchanged_configuration["generation"] == readback["generation"] and protected() == before_configuration,
              "Malformed profile8 recipe refuses without saved settings or musical mutation")
        before_run = document()
        ran = data("vibera_macros", {"action": "run", "id": recipe["id"], "generation": unchanged_configuration["generation"]})
        check(ran["completionState"] == "completed" and ran["completionScope"] == "canonicalMacroRun", "Actual saved scope-switch recipe completes canonically")
        expect("Saved two-step macro retains first baseline across scopes", (True, True, False, False), c, "group", drums, [a, c], baseline=[b])
        after_run = document()
        command("edit.undo")
        check(document() == before_run, "Saved scope-switch recipe is one exact Undo")
        command("edit.redo")
        check(document() == after_run, "Saved recipe Redo restores same scoped baseline and mask")
        now = data("vibera_macros", {"action": "state"})
        removed = data("vibera_macros", {"action": "remove", "id": recipe["id"], "generation": now["generation"]})
        check(removed["configurationCommitted"] and removed["profileMatchesMemory"] and document() == after_run and
              not any(item["id"] == recipe["id"] for item in removed["macros"]), "Owned recipe cleanup persists without altering Project")
        command("channel.unsolo", restore=True)
        expect("Final restore after saved recipe", second_mask)
        inactive = attempt / "original-restored-mix.vibra"
        wait(data("vibera_project", {"action": "save", "path": str(inactive), "overwrite": False}), "vibera_project", "job")
        inactive_solo = ET.parse(inactive).getroot().find("CHANNEL_SOLO")
        check(int(inactive_solo.attrib["channel"]) == 0 and len(inactive_solo) == 0 and
              "groupScoped" not in inactive_solo.attrib and "group" not in inactive_solo.attrib,
              "Inactive persistence retains no stale affected/baseline/group ownership")
        receipt.update(project=str(target), projectSha256=digest(target), channelIds=ids, groupIds=[drums, other],
                       restoredProject=str(inactive), restoredProjectSha256=digest(inactive),
                       rawProfileImport="No public raw profile import claimed; compatibility/migration belongs to model tests")
        client.close()
        receipt.update(passed=True, exitCode=client.process.returncode)
        return 0
    except Exception as failure:
        receipt.update(error=str(failure), traceback=traceback.format_exc())
        raise
    finally:
        try:
            if client and client.process.poll() is None:
                client.close()
        finally:
            receipt.update(finished=True, elapsedSeconds=time.monotonic() - started)
            if client:
                receipt["exitCode"] = client.process.returncode
                receipt["stderr"] = "".join(client.stderr)[-12000:]
            retain()


if __name__ == "__main__":
    raise SystemExit(main())
