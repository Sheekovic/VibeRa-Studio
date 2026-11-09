"""Inventory and fail closed on incomplete semantic MCP coverage.

Registry mappings are explicit in CommandInfo. Callback candidates supplement
the registry; source heuristics cannot prove a complete custom-control audit.
Never treat pointer/key testing as a semantic adapter or waive gaps as exceptions.
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys
import tempfile
from dev_support import content_identity

ROOT = pathlib.Path(__file__).resolve().parents[1]


def verified_evidence(root, verification, callback_path):
    """A file's existence cannot qualify a review or survive changed dependencies."""
    if not isinstance(verification, dict) or verification.get("status") != "passed":
        return False
    inputs = verification.get("inputs")
    mode = verification.get("sourceHashMode", "raw")
    if mode not in ("raw", "text-lf-v1"):
        return False
    cases = verification.get("cases")
    if (not isinstance(inputs, dict) or callback_path not in inputs or
            not isinstance(cases, list) or not cases or not all(isinstance(x, str) and x for x in cases)):
        return False
    def local_file(name):
        if not isinstance(name, str):
            raise ValueError("Evidence path must be a relative project path")
        relative = pathlib.PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or ":" in name or "\\" in name:
            raise ValueError("External evidence path refused")
        path = (root / name).resolve(strict=True)
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            raise ValueError("Evidence outside the current project refused")
        return path
    try:
        for name, digest in inputs.items():
            if (not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest) or
                    (content_identity(local_file(name))["contentSha256"] if mode == "text-lf-v1" else
                     hashlib.sha256(local_file(name).read_bytes()).hexdigest()) != digest):
                return False
        receipt = json.loads(local_file(verification.get("receipt")).read_text(encoding="utf-8-sig"))
        return (receipt.get("passed") is True and receipt.get("sourceInputs") == inputs and
                receipt.get("sourceHashMode", "raw") == mode and
                isinstance(receipt.get("checks"), list) and bool(receipt["checks"]) and
                bool(re.fullmatch(r"[0-9a-fA-F]{64}", str(receipt.get("applicationSha256", "")))))
    except (OSError, ValueError, TypeError):
        return False


def inventory(root=ROOT):
    source = (root / "src/Commands.cpp").read_text(encoding="utf-8-sig")
    body = source[source.index("static const std::vector<CommandInfo>"):source.index("return commands;")]
    pattern = re.compile(r'\{\s*"([^"\n]+)",\s*"([^"\n]+)",\s*Scope::(\w+),\s*'
                         r'(true|false)(?:,\s*(true|false))?(?:,\s*"([^"\n]+)")?\s*\}')
    tool_source = (root / "src/StudioMcp.cpp").read_text(encoding="utf-8-sig")
    rack_contracts = root / "src/RackContracts.h"
    if rack_contracts.is_file():
        tool_source += rack_contracts.read_text(encoding="utf-8-sig")
    tool_source += (root / "src/GrooveContracts.h").read_text(encoding="utf-8-sig") if (root / "src/GrooveContracts.h").is_file() else ""
    macro_contracts = root / "src/MixerMacroContracts.h"
    if macro_contracts.is_file():
        tool_source += macro_contracts.read_text(encoding="utf-8-sig")
    chain_contracts = root / "src/MixerChainWorkflowContracts.h"
    if chain_contracts.is_file():
        tool_source += chain_contracts.read_text(encoding="utf-8-sig")
    automation_recording_contracts = root / "src/AutomationRecordingContracts.h"
    tool_source += automation_recording_contracts.read_text(encoding="utf-8-sig") if automation_recording_contracts.is_file() else ""
    sampler_tempo_contracts = root / "src/SamplerTempoContracts.h"
    tool_source += sampler_tempo_contracts.read_text(encoding="utf-8-sig") if sampler_tempo_contracts.is_file() else ""
    note_modes_contracts = root / "src/PianoNoteModes.h"
    if note_modes_contracts.is_file():
        tool_source += note_modes_contracts.read_text(encoding="utf-8-sig")
    ai_contracts = root / "src/StudioAi.cpp"  # vibera_ai (aiToolDefinition, listed by mcpTools)
    if ai_contracts.is_file():
        tool_source += ai_contracts.read_text(encoding="utf-8-sig")
    synth_contracts=root / "src/synth/SynthMcp.h"
    if synth_contracts.is_file(): tool_source+=synth_contracts.read_text(encoding="utf-8-sig")
    recording_contracts = root / "src/RecordingContracts.h"
    recording_ids = set()
    if recording_contracts.is_file():
        recording_source = recording_contracts.read_text(encoding="utf-8-sig")
        tool_source += recording_source
        recording_ids = set(re.findall(r'id\s*==\s*"(recording\.[a-z]+)"', recording_source))
    tool_names = set(re.findall(r'"name"\s*:\s*"(vibera_[A-Za-z_]+)"', tool_source))
    tool_names.update(re.findall(r'\bsetProperty\(\s*"name"\s*,\s*"(vibera_[A-Za-z_]+)"\s*\)', tool_source))
    semantic_tools = tool_names - {"vibera_pointer", "vibera_key", "vibera_snapshot"}
    commands = []
    for match in pattern.finditer(body):
        ident, title, scope, document, visible, mapping = match.groups()
        mapping = mapping or "unmapped"
        gap = mapping not in {"command", "action"} and mapping not in semantic_tools
        if mapping == "vibera_recording" and ident not in recording_ids:
            gap = True # New recovery IDs require a declared canonical dispatch, not just a tool label.
        readback = ("vibera_recording state; vibera_state recording; vibera_inspect notes after canonical commit" if mapping == "vibera_recording" else
                    "vibera_catalogue state / job / list / descriptor" if mapping == "vibera_catalogue" else
                    "vibera_plugin list / parameters / job; vibera_state audio / pluginControls" if mapping == "vibera_plugin" else
                    "vibera_state / vibera_inspect; operation-specific gaps require review")
        skill = ("skills/vibera-studio/references/take-recovery.md" if mapping == "vibera_recording" else
                 "skills/vibera-studio/references/catalogue.md" if mapping == "vibera_catalogue" else
                 "skills/vibera-studio/references/plugins.md" if mapping == "vibera_plugin" else
                 "skills/vibera-studio/SKILL.md")
        commands.append({"id": ident, "name": title, "scope": scope,
                         "documentEdit": document == "true", "userFacing": visible != "false",
                         "mapping": mapping, "implementedMapping": not gap,
                         "stateReadback": readback, "skill": skill,
                         "verification": "Not established per operation merely by registration"})
    if not commands or len({c["id"] for c in commands}) != len(commands):
        raise ValueError("Missing/duplicate command registry records")
    # Admission checks: every literal CommandInfo initializer must parse, so a
    # new syntax or default-mapped operation cannot silently escape the gate.
    initializers = len(re.findall(r'\{\s*"[a-z][\w.]+",', body))
    if initializers != len(commands):
        raise ValueError(f"Unparsed command definitions: {initializers} initializers, {len(commands)} records")
    reviewed_path = root / "docs/mcp/REVIEWED_SURFACES.json"
    reviewed = json.loads(reviewed_path.read_text(encoding="utf-8-sig")) if reviewed_path.exists() else {}
    mapped_ids = {command["id"] for command in commands if command["implementedMapping"]}
    surface_path = root / "docs/mcp/SURFACE_INVENTORY.json"
    if not surface_path.is_file():
        raise ValueError("Missing explicit UI surface inventory; callback heuristics are insufficient")
    surfaces = json.loads(surface_path.read_text(encoding="utf-8-sig"))
    if surfaces.get("schemaVersion") != 1 or not isinstance(surfaces.get("inventoryComplete"), bool):
        raise ValueError("Invalid UI surface inventory authority")
    explicit = surfaces.get("operations")
    if (not isinstance(explicit, list) or any(not isinstance(x, dict) or not x.get("id") for x in explicit) or
            len({x["id"] for x in explicit}) != len(explicit)):
        raise ValueError("Missing/duplicate explicit UI operations")
    manual = []
    for entry in explicit:
        ids, readback = entry.get("commands"), entry.get("readback")
        mapped = (entry.get("mcp") in semantic_tools and isinstance(ids, list) and bool(ids) and
                  all(x in mapped_ids for x in ids) and isinstance(readback, list) and bool(readback) and
                  all(isinstance(x, str) and x.split(":", 1)[0] in semantic_tools for x in readback) and
                  isinstance(entry.get("skill"), str) and (root / entry["skill"]).is_file() and
                  verified_evidence(root, entry.get("verification"), entry.get("source")))
        manual.append(dict(entry, status="reviewed" if mapped else "manual mapping/verification required"))
    handlers = []
    expression = re.compile(r'\b(?:\w+\.)?(onClick|onChange|onValueChange|onDragStart|onDragEnd|onTextChange|'
                            r'onReturnKey|onEscapeKey|onPopupMenu|navigation|change)\s*=(?!=)|'
                            r'\bvoid\s+(mouseDown|mouseDrag|mouseUp|mouseWheelMove|mouseDoubleClick|'
                            r'listBoxItemClicked|listBoxItemDoubleClicked|selectedRowsChanged)\s*\(')
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8-sig")
        for match in expression.finditer(text):
            # Not callback surfaces: a local variable declaration (auto change = ..., double change = ...)
            # and teardown that clears callbacks (x.onClick = y.onChange = nullptr;).
            line_start = text.rfind("\n", 0, match.start()) + 1
            if match.group(1) and re.search(r'\b(?:auto|double|float|int|bool)\s*&?\s*$', text[line_start:match.start()]):
                continue
            statement = text[match.end():text.find(";", match.end())]
            if match.group(1) and re.fullmatch(r'(?:\s*[\w>.-]+\s*=)*\s*nullptr\s*', statement):
                continue
            line = text.count("\n", 0, match.start()) + 1
            # Nearby source is a drift fingerprint, never inferred proof of semantics.
            excerpt = text[match.start():match.start() + 800]
            digest = hashlib.sha256(excerpt.encode()).hexdigest()
            key = f"{path.relative_to(root).as_posix()}:{line}:{digest[:12]}"
            audit = reviewed.get(key, {})
            operations = audit.get("operations", [])
            # A review must identify real admitted commands, their result readback,
            # skill material and evidence. An arbitrary nonempty label is not a
            # semantic mapping. Keep missing surfaces red rather than waive them.
            valid_review = (isinstance(operations, list) and bool(operations) and
                            all(isinstance(item, str) and item in mapped_ids for item in operations) and
                            audit.get("sourceSha256") == digest and
                            isinstance(audit.get("readback"), list) and bool(audit["readback"]) and
                            all(isinstance(item, str) and item.split(":", 1)[0] in semantic_tools
                                for item in audit["readback"]) and
                            isinstance(audit.get("skill"), str) and
                            (root / audit["skill"]).is_file() and
                            verified_evidence(root, audit.get("verification"), path.relative_to(root).as_posix()))
            handlers.append({"surface": key, "sourceSha256": digest,
                             "kind": next(g for g in match.groups() if g),
                             "operations": audit.get("operations", []),
                             "readback": audit.get("readback", []), "skill": audit.get("skill", ""),
                             "status": "reviewed" if valid_review else "manual expansion and semantic mapping required",
                             "verification": audit.get("verification", "unverified")})
    gaps = [c["id"] for c in commands if not c["implementedMapping"]]
    unreviewed = [h["surface"] for h in handlers if h["status"] != "reviewed"]
    manual_gaps = [x["id"] for x in manual if x["status"] != "reviewed"]
    return {"contractVersion": 1, "coverageComplete": bool(surfaces["inventoryComplete"]) and not gaps and not unreviewed and not manual_gaps,
            "inventoryAuthority": "Explicit source registry plus conservative callback candidates; individual gestures/menu branches still require manual expansion. No claim that heuristic enumeration proves every UI operation.",
            "registeredOperations": commands, "customSurfaceCandidates": handlers,
            "explicitUiInventoryComplete": surfaces["inventoryComplete"], "explicitUiOperations": manual,
            "explicitUiGaps": manual_gaps,
            "registeredGaps": gaps, "unreviewedCustomSurfaces": unreviewed,
            "counts": {"registered": len(commands), "mapped": len(commands)-len(gaps),
                       "registeredGaps": len(gaps), "customCandidates": len(handlers),
                       "unreviewedCustomSurfaces": len(unreviewed), "explicitUiOperations":len(manual),
                       "explicitUiGaps":len(manual_gaps)},
            "limits": ["Native third-party plugin internals without supported APIs cannot be controlled arbitrarily.",
                       "Mappings are implementation declarations, not verified feature acceptance.",
                       "Pointer/key simulation is excluded from semantic coverage."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="Write tracked inventory and concise table")
    parser.add_argument("--output", type=pathlib.Path, help="Additional diagnostic JSON output")
    parser.add_argument("--self-test", action="store_true", help="Verify the detector rejects missing/drifted mappings; does not qualify current coverage")
    args = parser.parse_args()
    if args.self_test:
        with tempfile.TemporaryDirectory(prefix="mcp-coverage-detector-", dir=ROOT / "build") as temporary:
            root = pathlib.Path(temporary)
            (root / "src").mkdir()
            source = root / "src/Commands.cpp"
            source.write_text('static const std::vector<CommandInfo> commands{\n'
                              '{"notes.add", "Add note", Scope::piano, true, true, "command"},\n'
                              '{"recording.retry", "Retry take", Scope::global, false, true, "vibera_recording"},\n'
                              '{"new.badmap", "Invalid mapping", Scope::global, false, true, "invented"},\n'
                              '{"new.unmapped", "Unmapped", Scope::global, false}};\nreturn commands;\n')
            (root / "src/StudioMcp.cpp").write_text('{"name":"vibera_inspect"}')
            recording_contract = root / "src/RecordingContracts.h"
            recording_contract.write_text('{"name":"vibera_recording"} id == "recording.retry"')
            (root / "src/Control.h").write_text('button.onClick = [this] { change(); };\n')
            (root / "docs/mcp").mkdir(parents=True, exist_ok=True)
            surface_inventory = root / "docs/mcp/SURFACE_INVENTORY.json"
            surface_inventory.write_text(json.dumps({"schemaVersion":1,"inventoryComplete":True,"operations":[]}))
            first = inventory(root)
            assert first["registeredGaps"] == ["new.badmap", "new.unmapped"] and not first["coverageComplete"]
            assert len(first["unreviewedCustomSurfaces"]) == 1
            # Chain operations have their authoritative tool schema in a header,
            # just like Rack/recording. Its absence must still expose the gap.
            chain_contract = root / "src/MixerChainWorkflowContracts.h"
            original_commands = source.read_text()
            source.write_text(original_commands.replace('"invented"', '"vibera_mixerchain"'))
            assert "new.badmap" in inventory(root)["registeredGaps"]
            chain_contract.write_text('tool.getDynamicObject()->setProperty("name", "vibera_mixerchain");')
            assert "new.badmap" not in inventory(root)["registeredGaps"]
            source.write_text(original_commands.replace('"invented"', '"vibera_automationRecording"'))
            chain_contract.write_text('tool.getDynamicObject()->setProperty("name", "vibera_automationRecording");')
            assert "new.badmap" not in inventory(root)["registeredGaps"]
            chain_contract.unlink()
            # Note-mode schema lives in its own component header. Registration
            # alone or a differently named tool must never qualify the mapping.
            source.write_text(original_commands.replace('"invented"', '"vibera_note_modes"'))
            note_modes_contract = root / "src/PianoNoteModes.h"
            assert "new.badmap" in inventory(root)["registeredGaps"]
            note_modes_contract.write_text('{"name":"vibera_wrong"}')
            assert "new.badmap" in inventory(root)["registeredGaps"]
            note_modes_contract.write_text('{"name":"vibera_note_modes"}')
            assert "new.badmap" not in inventory(root)["registeredGaps"]
            note_modes_contract.unlink()
            assert "new.badmap" in inventory(root)["registeredGaps"]
            source.write_text(original_commands)
            take_entry = next(x for x in first["registeredOperations"] if x["id"] == "recording.retry")
            assert take_entry["implementedMapping"] and "vibera_recording state" in take_entry["stateReadback"]
            recording_contract.write_text('{"name":"vibera_recording"}')
            assert "recording.retry" in inventory(root)["registeredGaps"], "Missing canonical recording dispatch falsely mapped"
            recording_contract.write_text('{"name":"vibera_wrong"}')
            assert "recording.retry" in inventory(root)["registeredGaps"], "Missing authoritative tool provider falsely mapped"
            recording_contract.write_text('{"name":"vibera_recording"} id == "recording.retry"')
            surface = first["customSurfaceCandidates"][0]
            (root / "docs/mcp").mkdir(parents=True, exist_ok=True)
            review = root / "docs/mcp/REVIEWED_SURFACES.json"
            review_record = {"sourceSha256":surface["sourceSha256"], "operations":["invented.operation"]}
            review.write_text(json.dumps({surface["surface"]:review_record}))
            assert inventory(root)["unreviewedCustomSurfaces"], "Unknown operation accepted as coverage"
            (root / "skill.md").write_text("fixture: notes.add workflow")
            (root / "src/Service.h").write_text('int service = 0;\n')
            inputs = {name:hashlib.sha256((root / name).read_bytes()).hexdigest()
                      for name in ["src/Control.h", "src/Service.h"]}
            receipt = {"fixture":True, "passed":True, "sourceInputs":inputs,
                       "applicationSha256":"0"*64, "checks":["detector fixture, not product qualification"]}
            (root / "receipt.json").write_text(json.dumps(receipt))
            # Explicit new source receipts survive CRLF/LF-only text changes;
            # binary bytes and substantive backend differences remain exact.
            binary = root / "opaque.bin"
            binary.write_bytes(b"original\r\nbytes")
            normalized = {name:content_identity(root / name)["contentSha256"]
                          for name in ["src/Control.h", "src/Service.h", "opaque.bin"]}
            normalized_review = {"status":"passed", "inputs":normalized, "cases":["fixture"],
                                 "receipt":"normalized.json", "sourceHashMode":"text-lf-v1"}
            normalized_receipt = dict(receipt, sourceInputs=normalized, sourceHashMode="text-lf-v1")
            (root / "normalized.json").write_text(json.dumps(normalized_receipt))
            saved_control = (root / "src/Control.h").read_bytes()
            saved_service = (root / "src/Service.h").read_bytes()
            (root / "src/Control.h").write_bytes(saved_control.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            assert verified_evidence(root, normalized_review, "src/Control.h"), "Text EOL-only drift refused"
            (root / "src/Service.h").write_bytes(b"int service = 1;\n")
            assert not verified_evidence(root, normalized_review, "src/Control.h"), "Substantive drift normalized away"
            (root / "src/Service.h").write_bytes(saved_service)
            binary.write_bytes(b"original\nbytes")
            assert not verified_evidence(root, normalized_review, "src/Control.h"), "Binary EOL bytes normalized"
            binary.write_bytes(b"original\r\nbytes")
            assert not verified_evidence(root, dict(normalized_review, sourceHashMode="unknown"), "src/Control.h")
            (root / "normalized.json").write_text(json.dumps(dict(normalized_receipt, sourceHashMode="raw")))
            assert not verified_evidence(root, normalized_review, "src/Control.h"), "Receipt hash mode mismatch accepted"
            (root / "src/Control.h").write_bytes(saved_control)
            review_record.update(operations=["notes.add"], readback=["vibera_inspect:notes"],
                                 skill="skill.md", verification={"receipt":"receipt.json","cases":["fixture"],
                                                                "status":"passed", "inputs":inputs})
            review.write_text(json.dumps({surface["surface"]:review_record}))
            assert not inventory(root)["unreviewedCustomSurfaces"]
            saved_commands = source.read_text()
            source.write_text('static const std::vector<CommandInfo> commands{\n'
                              '{"notes.add", "Add note", Scope::piano, true, true, "command"}};\nreturn commands;\n')
            assert inventory(root)["coverageComplete"], "Controlled fully mapped detector fixture should qualify"
            surface_inventory.write_text(json.dumps({"schemaVersion":1,"inventoryComplete":False,"operations":[]}))
            assert not inventory(root)["coverageComplete"], "Incomplete whole-app inventory falsely qualified"
            surface_inventory.write_text(json.dumps({"schemaVersion":1,"inventoryComplete":True,"operations":[]}))
            source.write_text(saved_commands)
            surface_inventory.write_text(json.dumps({"schemaVersion":1,"inventoryComplete":False,
                "operations":[{"id":"control.no_callback","source":"src/Control.h","mcp":"gap:unmapped"}]}))
            declared = inventory(root)
            assert declared["explicitUiGaps"] == ["control.no_callback"] and not declared["explicitUiInventoryComplete"]
            assert not declared["coverageComplete"], "Non-callback control gap escaped the gate"
            surface_inventory.write_text(json.dumps({"schemaVersion":1,"inventoryComplete":True,"operations":[]}))
            (root / "receipt.json").write_text(json.dumps(dict(receipt, passed=False)))
            assert inventory(root)["unreviewedCustomSurfaces"], "Failed receipt accepted as qualification"
            (root / "receipt.json").write_text(json.dumps(receipt))
            pending = dict(review_record, verification=dict(review_record["verification"], status="pending"))
            review.write_text(json.dumps({surface["surface"]:pending}))
            assert inventory(root)["unreviewedCustomSurfaces"], "Pending verification accepted as passed"
            review.write_text(json.dumps({surface["surface"]:review_record}))
            (root / "src/Service.h").write_text('int service = 1;\n')
            assert inventory(root)["unreviewedCustomSurfaces"], "Changed backend accepted under an old callback review"
            (root / "src/Service.h").write_text('int service = 0;\n')
            for field in ["skill", "readback", "verification"]:
                missing = dict(review_record)
                del missing[field]
                review.write_text(json.dumps({surface["surface"]:missing}))
                assert inventory(root)["unreviewedCustomSurfaces"], "Incomplete review accepted: " + field
            pointer_review = dict(review_record, readback=["vibera_pointer"])
            review.write_text(json.dumps({surface["surface"]:pointer_review}))
            assert inventory(root)["unreviewedCustomSurfaces"], "Pointer simulation accepted as semantic readback"
            review_record["operations"] = ["new.unmapped"]
            review.write_text(json.dumps({surface["surface"]:review_record}))
            assert inventory(root)["unreviewedCustomSurfaces"], "Unmapped command accepted as coverage"
            review_record["operations"] = ["notes.add"]
            review.write_text(json.dumps({surface["surface"]:review_record}))
            (root / "src/Control.h").write_text('button.onClick = [this] { different(); };\n')
            assert len(inventory(root)["unreviewedCustomSurfaces"]) == 1
            assert not inventory(root)["coverageComplete"]
        print("PASS detector: mappings, failed/pending receipts, backend drift, explicit non-callback controls and incomplete inventory; full readiness not qualified")
        return 0
    report = inventory()
    data = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data, encoding="utf-8")
    if args.write:
        directory = ROOT / "docs/mcp"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "COVERAGE.json").write_text(data, encoding="utf-8")
        lines = ["# MCP coverage inventory", "", "Generated by scripts/mcp-coverage.py. Full readiness gate is **FAIL** while gaps remain.",
                 "Callback candidates require manual expansion into individual controls and gestures. Registration is not verification.",
                 "", "| Operation | Canonical mapping | Readback | Skill / verification |", "| --- | --- | --- | --- |"]
        for item in report["registeredOperations"]:
            lines.append(f'| `{item["id"]}` | {item["mapping"]} | {item["stateReadback"]} | {item["skill"]}; per-operation verification incomplete |')
        lines += ["", f'Custom callback candidates: {report["counts"]["customCandidates"]}; unreviewed: {report["counts"]["unreviewedCustomSurfaces"]}.',
                  "See COVERAGE.json for every source anchor, fingerprint, mapping and review status."]
        lines += ["", f'Explicit UI inventory complete: {report["explicitUiInventoryComplete"]}; known controls: '
                  f'{report["counts"]["explicitUiOperations"]}; gaps: {report["counts"]["explicitUiGaps"]}.',
                  "Known non-callback controls and incomplete whole-app inventory also keep readiness red."]
        if report["explicitUiOperations"]:
            lines += ["", "| Explicit control/workflow | Canonical path | MCP / verification |", "| --- | --- | --- |"]
            for entry in report["explicitUiOperations"]:
                lines.append(f'| `{entry["id"]}` | {entry.get("canonical", "pending")} | {entry.get("mcp", "unmapped")}; {entry["status"]} |')
        accepted = [h for h in report["customSurfaceCandidates"] if h["status"] == "reviewed"]
        if accepted:
            lines += ["", "Reviewed custom callbacks require passed receipts bound to current source/dependency hashes.",
                      "Native mouse/hardware qualification is separate from component/MCP evidence.", "",
                      "| Callback | Canonical operations | Semantic readback | Skill / receipt |",
                      "| --- | --- | --- | --- |"]
            for h in accepted:
                lines.append(f'| `{h["surface"]}` | {", ".join(h["operations"])} | {", ".join(h["readback"])} | '
                             f'{h["skill"]}; [receipt](../../{h["verification"]["receipt"]}) |')
        (directory / "COVERAGE.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    print(json.dumps({"coverageComplete": report["coverageComplete"], **report["counts"]}))
    return 0 if report["coverageComplete"] else 1


if __name__ == "__main__":
    sys.exit(main())
