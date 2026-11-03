"""Actual Channel appearance/order stdio workflow; no audio/hardware claim.

Usage: verify-channel-appearance-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Creates a fresh subattempt using the existing executable-derived output guard
and provider-neutral disposable --mcp-test client. Never edits user settings.
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
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
ICONS = ["none", "drum", "hat", "keys", "bass", "synth", "mic", "sample", "audio"]


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
    guards = load_module("appearance_output_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    output = guards.owned_output_path(sys.argv[2], app)
    guards.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    receipt = {"passed": False, "finished": False, "checks": [], "operations": [], "responses": 0,
               "scope": "Actual canonical metadata/order/history/macros/persistence; document-only stdio",
               "nativeInput": False, "hardware": False, "listening": False, "liveAudio": False,
               "application": str(app), "applicationSha256": digest(app),
               "scriptSha256": digest(Path(__file__)),
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
        client = load_module("appearance_neutral_client", client_path).VibeRaClient(app)
        receipt.update(pid=client.process.pid, clientSha256=digest(client_path), version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            receipt["responses"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected MCP refusal: " + str(response))
            return response

        def data(tool, args=None):
            return call(tool, args)["structuredContent"]

        def wait(result, tool, action, seconds=40):
            initial, current, began = result, result, time.monotonic()
            try:
                while current.get("completionState") == "pending" and time.monotonic() - began < seconds:
                    time.sleep(.02)
                    current = data(tool, {"action": action, "operation": initial["operation"]})
                    check(current["operation"] == initial["operation"], "Actual asynchronous owner token remains exact")
                check(current.get("completionState") == "completed", "Actual terminal completion: " + tool + "/" + action)
                return current
            finally:
                receipt["operations"].append({"initial": initial, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def command(name, **args):
            result = data("vibera_command", {"command": name, "args": args})
            if name in ("channel.properties", "rack.properties", "rack.sort"):
                check(result.get("completionState") == "completed" and
                      result.get("completionScope") == "canonicalChannelAppearance" and
                      isinstance(result.get("changed"), bool) and result.get("dspSettlingAcknowledged") is False,
                      "Explicit actual canonical completion/no-op readback, without a DSP-settled claim: " + name)
            if result.get("completionState") == "pending":
                # Setup Channel routing uses its existing actual coordinator job.
                check(name == "channel.mixer", "Appearance/history is synchronous canonical work, never queued-only success")
                result = wait(result, "vibera_mixer", "routingJob")
            return result

        state = lambda: data("vibera_state")
        inspect = lambda entity, **args: data("vibera_inspect", {"entity": entity, **args})["items"]
        channels = lambda: inspect("channels")
        order = lambda: [row["id"] for row in channels()]

        def document():
            current = state()
            return {"channels": channels(), "patterns": inspect("patterns"),
                    "notes": {str(row["id"]): inspect("notes", pattern=row["id"]) for row in inspect("patterns")},
                    "clips": inspect("clips"), "mixer": inspect("mixer"), "tempo": current["tempo"],
                    "samplers": {str(row["id"]): row["sampler"] for row in current["channels"]}}

        def protected():
            current = state()
            return {"document": document(), **{key: current[key] for key in
                    ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable")}}

        def no_op(name, args):
            before = protected()
            reply = command(name, **args)
            check(reply.get("changed") is False, "No-op explicitly reports changed=false")
            check(protected() == before, "No-op preserves document, dirty/revision and both history directions: " + name)

        def edit(name, args, verify):
            before, revision = document(), state()["revision"]
            command(name, **args)
            after = document()
            check(after != before and state()["revision"] == revision + 1, "One canonical edit: " + name)
            verify()
            no_op(name, args)
            command("edit.undo")
            check(document() == before, "One exact Undo restores complete metadata/order/music: " + name)
            # A singleton stable sort is a no-op even with a real Redo edge.
            no_op("rack.sort", {"ids": [a], "channelSort": {"by": "name"}})
            command("edit.redo")
            check(document() == after, "One exact Redo restores complete metadata/order/music: " + name)

        def refuse(name, args, code=None):
            before = protected()
            reply = call("vibera_command", {"command": name, "args": args}, True)
            error = reply.get("structuredContent", {}).get("error", {})
            check(reply.get("isError") is True and isinstance(error.get("code"), str) and
                  bool(error.get("message")) and (code is None or error["code"] == code),
                  "Structured actual refusal: " + name + " " + str(args))
            check(protected() == before, "Refusal preserves exact document/revision/history")

        tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
        check({"vibera_commands", "vibera_command", "vibera_rack", "vibera_inspect", "vibera_macros"} <= tools.keys(),
              "Actual discovery includes canonical commands, Rack/readback and saved macros")
        for name in ("channel.properties", "rack.properties", "rack.sort"):
            entries = data("vibera_commands", {"command": name, "includeSchemas": True})["operations"]
            check(len(entries) == 1 and entries[0]["id"] == name and entries[0]["documentEdit"] is True and
                  entries[0]["mapping"] == "vibera_command", "Registry exposes exact undoable shared command: " + name)
            schema = entries[0]["arguments"]
            check(schema["additionalProperties"] is False, "Typed command rejects foreign top-level metadata")
            if name == "rack.sort":
                sort_schema = schema["properties"]["channelSort"]
                check(sort_schema["additionalProperties"] is False and sort_schema["required"] == ["by"] and
                      sort_schema["properties"]["by"]["enum"] == ["name", "colour", "mixer", "type"] and
                      sort_schema["properties"]["descending"] == {"type": "boolean", "default": False},
                      "Actual stable sort discovery has typed criterion/direction")
            else:
                patch = schema["properties"]["appearance"]
                check(patch["additionalProperties"] is False and patch["minProperties"] == 1 and
                      patch["properties"]["icon"]["enum"] == ICONS and
                      ("name" in patch["properties"]) == (name == "channel.properties"),
                      "Appearance discovery distinguishes single names from bulk colour/icon")
        (attempt / "discovery.json").write_text(json.dumps(tools, indent=2) + "\n", encoding="utf-8")
        check(data("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})["completionState"] == "completed",
              "Actual disposable empty project created")
        initial = channels()
        check(len(initial) == 4, "Known original empty template has four Channels")
        a, b, c, d = [row["id"] for row in initial]
        pattern = state()["selectedPattern"]
        for index, channel in enumerate((a, b, c, d)):
            command("notes.add", pattern=pattern, channel=channel, beat=index * .5, length=.25, pitch=60 + index, value=.5)
        command("clip.add", pattern=pattern, beat=0, length=4)
        command("clip.add", pattern=pattern, beat=4, length=4)
        for channel, mixer in ((a, 2), (c, 1), (d, 1)):
            command("channel.mixer", channel=channel, mixer=mixer)
        command("channel.sampler.cents", channel=a, value=17)
        baseline = document()

        def row(target):
            return next(item for item in channels() if item["id"] == target)

        picker = data("vibera_rack", {"action": "state"})["colourPicker"]
        swatches = picker["palette"]
        check(len(swatches) == 16 and [item["presetIndex"] for item in swatches] == list(range(16)) and
              all(isinstance(item["colour"], int) and item["colour"] >> 24 == 255 for item in swatches) and
              picker["themeValue"] == 0 and picker["previewChangesProject"] is False and
              picker["singleCommand"] == "channel.properties" and picker["bulkCommand"] == "rack.properties",
              "Visual palette exposes exact opaque semantic choices and commit/preview policy")
        for swatch in swatches:
            command("channel.properties", channel=a, appearance={"colour": swatch["colour"]})
            check(row(a)["colour"] == swatch["colour"], "Discovered palette choice commits exact integer colour")
        command("channel.properties", channel=a, appearance={"colour": 0})
        check(document() == baseline, "Theme reset restores exact original document after all discovered choices")

        edit("channel.properties", {"channel": a, "appearance": {"name": "  Zulu  ", "colour": 0xFFE04B39, "icon": "drum"}},
             lambda: check(row(a)["name"] == "Zulu" and row(a)["colour"] == 0xFFE04B39 and row(a)["icon"] == "drum",
                           "Actual single patch trims name and applies exact ARGB/icon"))
        edit("rack.properties", {"ids": [c, a], "appearance": {"colour": 0xFF739ABD, "icon": "hat"}},
             lambda: check(row(a)["colour"] == row(c)["colour"] == 0xFF739ABD and
                           row(a)["icon"] == row(c)["icon"] == "hat" and row(a)["name"] == "Zulu" and row(b) == initial[1],
                           "Noncontiguous bulk patch changes exact members, retaining omitted names and unselected row"))
        command("channel.properties", channel=c, appearance={"name": "alpha"})
        command("channel.properties", channel=d, appearance={"name": "ALPHA"})
        selected = [d, a, c]  # Input order is deliberately not current display order.
        edit("rack.sort", {"ids": selected, "channelSort": {"by": "name"}},
             lambda: check(order() == [c, b, d, a], "Literal ascending name order; case-insensitive ties stable, unselected slot fixed"))
        edit("rack.sort", {"ids": selected, "channelSort": {"by": "name", "descending": True}},
             lambda: check(order() == [a, b, c, d], "Literal descending name order retains equal-key c,d order"))
        edit("rack.sort", {"ids": selected, "channelSort": {"by": "mixer"}},
             lambda: check(order() == [c, b, d, a], "Literal Mixer1,1,2 order is stable; unselected slot1 retained"))
        no_op("rack.sort", {"ids": [d, c, a], "channelSort": {"by": "type"}})
        for channel, colour in ((a, 0), (b, 0xFF0000FF), (c, 0xFFFF0000), (d, 0xFF808080)):
            command("channel.properties", channel=channel, appearance={"colour": colour})
        edit("rack.sort", {"ids": [d, b, a, c], "channelSort": {"by": "colour"}},
             lambda: check(order() == [a, c, b, d], "Literal assigned-colour order: theme, red, blue, grey; no sampled theme pixels"))
        edit("channel.properties", {"channel": c, "appearance": {"icon": "none"}},
             lambda: check(row(c)["icon"] == "none" and row(c)["colour"] == 0xFFFF0000 and row(c)["name"] == "alpha",
                           "Explicit icon reset preserves omitted name/colour"))
        current = document()
        normalized = json.loads(json.dumps(current))
        normalized["channels"] = sorted(normalized["channels"], key=lambda item: [a, b, c, d].index(item["id"]))
        for item, original in zip(normalized["channels"], baseline["channels"]):
            for key in ("name", "colour", "icon"):
                item[key] = original[key]
        check(normalized == baseline, "All appearance/order work preserves exact source IDs/paths, notes, linked clips, Mixer and sampler settings")
        rack = data("vibera_rack", {"action": "state"})
        check([{key: item[key] for key in ("id", "name", "colour", "icon")} for item in rack["rows"]] ==
              [{key: item[key] for key in ("id", "name", "colour", "icon")} for item in channels()],
              "Rack visible rows and canonical inspect agree on actual appearance/order")

        for name, args in (
            ("channel.properties", {"channel": 999999, "appearance": {"icon": "drum"}}),
            ("rack.properties", {"ids": [a, 999999], "appearance": {"colour": 0}}),
            ("rack.properties", {"ids": [a, a], "appearance": {"icon": "keys"}}),
            ("rack.properties", {"ids": [], "appearance": {"icon": "keys"}}),
            ("channel.properties", {"channel": a, "appearance": {"icon": "foreign-icon"}}),
            ("channel.properties", {"channel": a, "appearance": {"name": "   "}}),
            ("channel.properties", {"channel": a, "appearance": {"colour": 0x80112233}}),
            ("channel.properties", {"channel": a, "appearance": {"colour": True}}),
            ("channel.properties", {"channel": a, "appearance": {"icon": "none", "extra": 1}}),
            ("channel.properties", {"channel": a, "appearance": {}}),
            ("rack.properties", {"ids": [a], "appearance": {"name": "not bulk"}}),
            ("rack.sort", {"ids": [a, c], "channelSort": {"by": "source"}}),
            ("rack.sort", {"ids": [a, c], "channelSort": {"by": "name", "descending": 1}}),
            ("rack.sort", {"ids": [a, c], "channelSort": {"by": "name", "extra": False}}),
            ("rack.sort", {"ids": [a, 999999], "channelSort": {"by": "name"}}),
        ):
            refuse(name, args)
        stale = protected()
        reply = call("vibera_command", {"command": "channel.properties", "revision": state()["revision"] - 1,
                     "args": {"channel": a, "appearance": {"name": "stale"}}}, True)
        check(reply.get("isError") is True and reply["structuredContent"]["error"]["code"] == "stale_revision" and
              protected() == stale, "Actual stale revision cannot rename a newer target")
        before_batch = protected()
        failed_batch = call("vibera_batch", {"commands": [
            {"command": "channel.properties", "args": {"channel": a, "appearance": {"name": "must roll back"}}},
            {"command": "channel.properties", "args": {"channel": c, "appearance": {"colour": 0x80112233}}}]}, True)
        check(failed_batch.get("isError") is True and failed_batch["structuredContent"].get("error", {}).get("code") and
              protected() == before_batch, "Actual pure appearance batch refuses a later invalid alpha before publishing any edit")
        failed_target_batch = call("vibera_batch", {"commands": [
            {"command": "channel.properties", "args": {"channel": a, "appearance": {"name": "must not publish"}}},
            {"command": "rack.sort", "args": {"ids": [c, 999999], "channelSort": {"by": "name"}}}]}, True)
        check(failed_target_batch.get("isError") is True and protected() == before_batch,
              "Later unavailable sort target cannot publish earlier metadata or advance revision/history")

        before_valid_batch = document()
        batch = {"commands": [
            {"command": "channel.properties", "args": {"channel": a, "appearance": {"name": "Batch drum"}}},
            {"command": "rack.properties", "args": {"ids": [a, c], "appearance": {"icon": "synth"}}}]}
        completed = data("vibera_batch", batch)
        check(completed.get("completionState") == "completed" and
              completed.get("completionScope") == "canonicalChannelAppearanceBatch" and
              completed.get("changed") is True and completed.get("dspSettlingAcknowledged") is False,
              "Actual successful pure appearance batch reports canonical completion without a DSP claim")
        after_valid_batch = document()
        check(after_valid_batch != before_valid_batch and
              next(row for row in channels() if row["id"] == a)["name"] == "Batch drum" and
              all(row["icon"] == "synth" for row in channels() if row["id"] in (a, c)),
              "Actual batch commits both intended stable-target edits")
        command("edit.undo")
        check(document() == before_valid_batch, "Appearance batch has one exact Undo")
        command("edit.redo")
        check(document() == after_valid_batch, "Appearance batch has one exact Redo")
        protected_batch = protected()
        check(data("vibera_batch", batch).get("changed") is False and protected() == protected_batch,
              "No-op appearance batch explicitly preserves revision, dirty state and both history directions")

        config = data("vibera_macros", {"action": "state", "includeSchemas": True})
        check({"channel.properties", "rack.properties", "rack.sort"} <= {item["id"] for item in config["supportedCommands"]},
              "Saved recipe runner discovers all changed canonical edits")
        recipe = {"id": "macro.appearance-proof", "name": "Original appearance recipe", "steps": [
            {"action": "channel.properties", "parameters": {"channel": a, "appearance": {"name": "Recipe drum", "icon": "drum"}}, "currentContext": False},
            {"action": "rack.sort", "parameters": {"ids": [a, c], "channelSort": {"by": "name", "descending": False}}, "currentContext": False}]}
        before_config = protected()
        saved = data("vibera_macros", {"action": "create", "generation": config["generation"], "macro": recipe})
        check(saved["configurationCommitted"] and saved["profileExists"] and saved["profileMatchesMemory"] and
              saved["profileVersion"] == 7 and protected() == before_config,
              "Typed profile7 recipe is actually persisted without Project history or mutation")
        readback = data("vibera_macros", {"action": "state", "id": recipe["id"]})
        actual_recipe = next(item for item in readback["macros"] if item["id"] == recipe["id"])
        check(actual_recipe["steps"] == recipe["steps"] and actual_recipe["persisted"], "Saved typed appearance/sort recipe reads back exact fields")
        bad = json.loads(json.dumps(recipe))
        bad["id"] = "macro.invalid-appearance"
        bad["steps"][0]["parameters"]["appearance"]["icon"] = "foreign-icon"
        denied = call("vibera_macros", {"action": "create", "generation": readback["generation"], "macro": bad}, True)
        fresh_config = data("vibera_macros", {"action": "state"})
        check(denied.get("isError") is True and denied["structuredContent"].get("error", {}).get("code") and
              fresh_config["generation"] == readback["generation"] and fresh_config["profileMatchesMemory"] and
              not any(item["id"] == bad["id"] for item in fresh_config["macros"]) and protected() == before_config,
              "Malformed typed recipe refuses before persisted profile7 or musical state changes")
        before_run = document()
        ran = data("vibera_macros", {"action": "run", "id": recipe["id"], "generation": fresh_config["generation"]})
        check(ran["completionState"] == "completed" and ran["completionScope"] == "canonicalMacroRun" and
              row(a)["name"] == "Recipe drum" and row(a)["icon"] == "drum" and order() == [c, a, b, d],
              "Actual saved multi-command recipe applies exact metadata and literal selected-slot order")
        after_run = document()
        command("edit.undo")
        check(document() == before_run, "Saved recipe has one exact musical Undo")
        command("edit.redo")
        check(document() == after_run, "Saved recipe Redo restores exact metadata/order")
        now = data("vibera_macros", {"action": "state"})
        removed = data("vibera_macros", {"action": "remove", "id": recipe["id"], "generation": now["generation"]})
        check(removed["configurationCommitted"] and removed["profileMatchesMemory"] and
              not any(item["id"] == recipe["id"] for item in removed["macros"]) and document() == after_run,
              "Owned recipe removal persists without changing Project history/content")

        target = attempt / "original-channel-appearance.vibra"
        wait(data("vibera_project", {"action": "save", "path": str(target), "overwrite": False}), "vibera_project", "job")
        check(target.is_file() and not state()["dirty"], "Actual save job writes a clean project")
        xml = ET.parse(target).getroot()
        current_schema = load_module("appearance_project_contract", ROOT / "scripts/dev_support.py").project_schema(ROOT)
        check(int(xml.attrib["version"]) == current_schema, "Actual saved appearance uses the authoritative current schema")
        persisted = {int(item.attrib["id"]): item.attrib for item in xml.find("CHANNELS")}
        check(all(persisted[item["id"]]["name"] == item["name"] and
                  int(persisted[item["id"]].get("colour", "0")) == item["colour"] and
                  persisted[item["id"]].get("icon", "none") == item["icon"] for item in channels()),
              "Actual project XML independently retains exact name/assigned colour/icon fields")
        wait(data("vibera_project", {"action": "open", "path": str(target), "discardChanges": True}), "vibera_project", "job")
        check(document() == after_run and not state()["dirty"], "Save/reopen preserves exact appearance/order/source/notes/links/settings")
        receipt.update(project=str(target), projectSha256=digest(target), channelIds=[a, b, c, d],
                       finalOrder=order(), profileImportCoverage="Raw profile import is model-tested, not a public MCP operation")
        client.close()
        receipt.update(passed=True, exitCode=client.process.returncode)
        return 0
    except Exception as failure:
        receipt.update(error=str(failure), traceback=traceback.format_exc())
        raise
    finally:
        try:
            if client and client.process.poll() is None:
                client.close()  # Existing retained-PID client; graceful EOF before scoped termination.
        finally:
            receipt.update(finished=True, elapsedSeconds=time.monotonic() - started)
            if client:
                receipt["exitCode"] = client.process.returncode
                receipt["stderr"] = "".join(client.stderr)[-12000:]
            retain()


if __name__ == "__main__":
    raise SystemExit(main())
