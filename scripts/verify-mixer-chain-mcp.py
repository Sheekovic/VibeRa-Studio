"""External stdio/real original SDK effect-chain capture and preset regression.

Usage: verify-mixer-chain-mcp.py ABS_APP ABS_PRODUCTION_PRESET ABS_SDK_MODULE ABS_FRESH_OUTPUT
The seed is target.vibfx emitted by the actual verifyMixerChainWorkflow branch.
Output is a fresh ordinary child of source build/windows, or the exact executable-derived
verified staging/current distribution-verification root.
No native input, callback PCM, installed-provider, hardware or listening claim.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import time
import traceback
import uuid
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
MAXIMUM_BYTES = 64 * 1024 * 1024
FIELDS = ("id", "name", "instrument", "bypassed", "channel", "mixer", "slot", "wet",
          "chainBypassed", "effectiveBypass")


def sha(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def ordinary(path: Path, directory: bool) -> None:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("Refuse reparse input/output: " + str(path))
    if not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
        raise ValueError("Unexpected input/output type: " + str(path))


def explicit(raw: str, directory: bool = False) -> Path:
    path = Path(raw)
    if not path.is_absolute() or any(p in (".", "..") for p in raw.replace("\\", "/").split("/")):
        raise ValueError("Require explicit absolute path without traversal")
    if os.name == "nt" and (not re.match(r"^[A-Za-z]:[/\\]", raw) or ":" in raw[2:]):
        raise ValueError("Require ordinary local-drive path")
    for ancestor in path.parents:
        if ancestor.exists():
            ordinary(ancestor, True)
    ordinary(path, directory)
    return path


def verification_output_root(app: Path) -> Path:
    """Exact packaged executable selects ONE verified workspace output root.

    Other executables retain the existing source build/windows restriction.
    This never authorizes arbitrary build directories or cross-package outputs.
    """
    packages = {
        ROOT / "build/package-stage/VibeRa Studio.exe": ROOT / "build/distribution-verification/staging",
        ROOT / "dist/VibeRa Studio/VibeRa Studio.exe": ROOT / "build/distribution-verification/current",
    }
    allowed = packages.get(app)
    if allowed is None:
        return ROOT / "build/windows"
    for ancestor in app.parents:
        ordinary(ancestor, True)
    required = ("VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe")
    for name in (*required, "provenance.json"):
        ordinary(app.parent / name, False)
    provenance = app.parent / "provenance.json"
    if not 2 <= provenance.stat().st_size <= 4 * 1024 * 1024:
        raise ValueError("Package acceptance requires bounded existing provenance")
    manifest = json.loads(provenance.read_text(encoding="utf-8-sig"))
    if (not isinstance(manifest, dict) or type(manifest.get("format")) is not int or
            manifest["format"] != 1 or not isinstance(manifest.get("sourceCommit"), str) or
            not re.fullmatch(r"[0-9a-fA-F]{40}", manifest["sourceCommit"]) or
            not isinstance(manifest.get("sourceHashes"), list) or not manifest["sourceHashes"] or
            not isinstance(manifest.get("files"), list)):
        raise ValueError("Package acceptance requires existing versioned source provenance")
    for name in required:
        rows = [row for row in manifest["files"] if isinstance(row, dict) and row.get("path") == name]
        if (len(rows) != 1 or not isinstance(rows[0].get("sha256"), str) or
                not re.fullmatch(r"[0-9a-fA-F]{64}", rows[0]["sha256"])):
            raise ValueError("Package manifest omits or ambiguously identifies a required binary")
    # Reuse actual full cryptographic package verification, including packaged
    # skill references. Shape/input checks above close empty/omitted-binary cases.
    module = importlib.util.spec_from_file_location("vibera_acceptance_distribution", ROOT / "scripts/distribution.py")
    if module is None or module.loader is None:
        raise ValueError("Existing distribution verifier unavailable")
    distribution = importlib.util.module_from_spec(module)
    module.loader.exec_module(distribution)
    distribution.verify(app.parent)
    return allowed


def fresh_output(raw: str, app: Path) -> Path:
    path = Path(raw)
    # Validate spelling and EVERY existing ancestor before creating any output.
    if not path.is_absolute() or any(p in (".", "..") or p.endswith((".", " ")) for p in raw.replace("\\", "/").split("/")):
        raise ValueError("Output requires an absolute path without traversal")
    if os.name == "nt" and (not re.match(r"^[A-Za-z]:[/\\]", raw) or ":" in raw[2:]):
        raise ValueError("Output requires an ordinary local-drive path")
    for ancestor in path.parents:
        if ancestor.exists():
            ordinary(ancestor, True)
    allowed = verification_output_root(app)
    if path == allowed or not path.is_relative_to(allowed):
        raise ValueError("Output must be a fresh descendant of its executable-derived verification root")
    if path.exists():
        ordinary(path, True)
        if any(path.iterdir()):
            raise ValueError("Refuse nonempty output, including previous failed attempts")
    if len(str(path / "presets").encode("utf-16-le")) // 2 > 200:
        raise ValueError("Owned preset root exceeds actual helper UTF-16 path budget")
    path.mkdir(parents=True, exist_ok=True)
    explicit(str(path), True)
    with (path / ".vibera-chain-stdio-owner.json").open("x", encoding="utf-8") as stream:
        json.dump({"schema": 1, "owner": "VibeRa verify-mixer-chain-mcp", "output": str(path)}, stream)
    return path


def opaque_bytes(text: str) -> bytes:
    """Independent documented JUCE length-prefixed little-bit Base64 validation."""
    match = re.fullmatch(r"([0-9]{1,8})\.([.A-Za-z0-9+]*)", text)
    if not match:
        raise ValueError("Malformed original opaque state encoding")
    length = int(match[1])
    encoded = match[2]
    if length > 16 * 1024 * 1024 or len(encoded) != (length * 8 + 5) // 6:
        raise ValueError("Opaque state length/capacity mismatch")
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    result, accumulator, bits = bytearray(), 0, 0
    for char in encoded:
        value = alphabet.index(char)
        accumulator |= value << bits
        bits += 6
        if bits >= 8:
            result.append(accumulator & 255)
            accumulator >>= 8
            bits -= 8
    if accumulator or len(result) != length:
        raise ValueError("Noncanonical opaque state padding")
    return bytes(result)


def original_fixture_state(encoded: str) -> tuple[float, ...]:
    """Pinned JUCE binary XML, two SDK streams and this fixture's restore map.

    The outer VST3PluginState has no class/FUID attribute. Descriptor/module
    identity is checked separately; IComponent/IEditController identify streams.
    Never treat the outer wrapper as the native SDK's six-double payload.
    """
    binary = opaque_bytes(encoded)
    if len(binary) < 10:
        raise ValueError("Truncated native binary-XML envelope")
    magic, length = struct.unpack_from("<II", binary)
    if magic != 0x21324356 or not 0 < length <= 4096 or length != len(binary) - 9 or binary[-1:] != b"\0":
        raise ValueError("Invalid exact JUCE binary-XML magic/length/terminator")
    text = binary[8:-1].decode("utf-8", errors="strict")
    if "\0" in text or "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise ValueError("Native envelope declarations/NUL refused before XML parsing")
    envelope = ET.fromstring(text)
    if (envelope.tag != "VST3PluginState" or envelope.attrib or
            (envelope.text or "").strip() or any((child.tail or "").strip() for child in envelope) or
            [child.tag for child in envelope] != ["IComponent", "IEditController", "VibeRaParameterValues"]):
        raise ValueError("Require exact supported component/controller/restore envelope shape")
    streams = []
    for child in list(envelope)[:2]:
        if child.attrib or len(child):
            raise ValueError("SDK state stream must be one exact text-only interface")
        state = opaque_bytes(child.text or "")
        if len(state) != 48:
            raise ValueError("Original SDK interface must contain exactly six doubles")
        streams.append(state)
    if streams[0] != streams[1]:
        raise ValueError("Original combined component/controller payloads disagree")
    values = struct.unpack("<6d", streams[0])
    if any(not math.isfinite(value) or not 0 <= value <= 1 for value in values[:5]) or values[5] != -1:
        raise ValueError("Original SDK six-double ranges/unsupported-helper sentinel differ")
    if values[1:] != (0., 0., .25, 0., -1.):
        raise ValueError("Untouched original fixture defaults differ in captured native payload")
    restored = envelope[2]
    if restored.attrib != {"version": "1"} or len(restored) != 1 or (restored.text or "").strip():
        raise ValueError("Actual source fixture requires its exact version1 gain-only restore map")
    entry = restored[0]
    if (entry.tag != "PARAM" or set(entry.attrib) != {"id", "value"} or entry.get("id") != "0" or len(entry)
            or (entry.text or "").strip() or (entry.tail or "").strip()):
        raise ValueError("Original native captured gain restore identity differs")
    gain = float(entry.attrib["value"])
    if not math.isfinite(gain) or gain != values[0]:
        raise ValueError("Native component payload and acknowledged restore gain disagree")
    return values


def read_chain(path: Path) -> dict:
    """Independent strict Format1 reader; no product codec import or fake descriptor."""
    ordinary(path, False)
    size = path.stat().st_size
    if size < 24 or size > MAXIMUM_BYTES:
        raise ValueError("Preset file size outside documented Format1 bounds")
    payload = path.read_bytes()
    if len(payload) != size or not payload.startswith(b"VibeRaFX"):
        raise ValueError("Preset magic/size changed")
    offset = 8

    def u32():
        nonlocal offset
        if len(payload) - offset < 4:
            raise ValueError("Truncated preset integer")
        value = struct.unpack_from("<I", payload, offset)[0]
        offset += 4
        return value

    def text(maximum, required=False):
        nonlocal offset
        length = u32()
        if length > maximum or length > len(payload) - offset:
            raise ValueError("Invalid bounded preset string")
        raw = payload[offset:offset + length]
        value = raw.decode("utf-8", errors="strict")
        offset += length
        if "\0" in value or (required and not value) or value.encode("utf-8") != raw:
            raise ValueError("Invalid exact UTF-8 preset string")
        return value

    if u32() != 1:
        raise ValueError("Unsupported preset version")
    flags = u32()
    if flags > 1:
        raise ValueError("Unknown chain flags")
    result = {"name": text(1024, True), "effectsBypassed": bool(flags), "effects": []}
    count = u32()
    if count > 16:
        raise ValueError("Preset slot capacity exceeded")
    for _ in range(count):
        flags = u32()
        wet = struct.unpack("<f", struct.pack("<I", u32()))[0]
        if flags > 1 or not math.isfinite(wet) or not 0 <= wet <= 1:
            raise ValueError("Invalid slot flags/wet")
        slot = {"bypass": bool(flags), "wet": wet, "name": text(1024, True),
                "descriptor": text(65536, True), "state": text(24 * 1024 * 1024)}
        if "<!DOCTYPE" in slot["descriptor"].upper() or "<!ENTITY" in slot["descriptor"].upper():
            raise ValueError("Descriptor declarations refused before XML parser")
        descriptor = ET.fromstring(slot["descriptor"])
        if descriptor.tag != "PLUGIN" or descriptor.get("format") != "VST3" or descriptor.get("isInstrument") != "0":
            raise ValueError("Expected genuine Windows VST3 effect descriptor")
        if slot["state"]:
            opaque_bytes(slot["state"])
        result["effects"].append(slot)
    if offset != len(payload):
        raise ValueError("Preset has trailing bytes")
    return result


def seed_project(base: Path, destination: Path, chain: dict, version: int) -> int:
    tree = ET.parse(base)
    document = tree.getroot()
    target = [row for row in document.findall("./MIXER/INSERT") if row.get("index") == "1"]
    if document.tag != "VIBERA" or int(document.get("version", "-1")) != version or len(target) != 1 or document.findall(".//PLUGIN"):
        raise ValueError("Require actual current-schema empty server project with insert1")
    # Only canonical numeric allocator entities reserve IDs. Sampler modulation
    # TARGET.id is a destination name; Mixer GROUP.id is an independent UUID.
    numeric_id_paths = ("./CHANNELS/CHANNEL", "./CHANNELS/CHANNEL/PLUGIN",
                        "./PATTERNS/PATTERN", "./PATTERNS/PATTERN/NOTE",
                        "./PLACEMENTS/PLACEMENT", "./AUDIO_PLACEMENTS/AUDIO_PLACEMENT",
                        "./MIXER/INSERT/PLUGIN", "./CLIP_GROUPS/GROUP", "./CHANNEL_GROUPS/GROUP",
                        "./AUTOMATION/SOURCES/SOURCE", "./AUTOMATION/SOURCES/SOURCE/POINTS/POINT",
                        "./AUTOMATION/PLACEMENTS/PLACEMENT")
    used = {int(node.attrib["id"]) for path in numeric_id_paths for node in document.findall(path)}
    identity = int(document.attrib["nextId"])
    if identity <= max(used, default=0) or not 1 <= identity <= 1000000000:
        raise ValueError("Actual server nextId cannot reserve original effect")
    slot = chain["effects"][0]
    target[0].set("name", chain["name"])
    target[0].set("effectsBypassed", "1" if chain["effectsBypassed"] else "0")
    plugin = ET.SubElement(target[0], "PLUGIN", {"id": str(identity), "name": slot["name"],
        "descriptor": slot["descriptor"], "state": slot["state"], "instrument": "0",
        "bypass": "1" if slot["bypass"] else "0", "wet": repr(slot["wet"])})
    ET.SubElement(plugin, "AUTOMATION_BINDINGS")
    document.set("nextId", str(identity + 1))
    with destination.open("xb") as stream:
        tree.write(stream, encoding="utf-8", xml_declaration=True)
    return identity


def chain_schema(value, schema: dict, location="response") -> None:
    """Validate the finite vocabulary actually used by this tool's discovery.

    Refuse unknown validation keywords rather than pretending to implement a
    general JSON Schema engine. Descriptions/defaults are annotations only.
    """
    supported = {"type", "properties", "required", "additionalProperties", "items", "enum", "const",
                 "minimum", "maximum", "minLength", "maxLength", "maxItems", "description", "default", "oneOf"}
    if set(schema) - supported:
        raise ValueError("Unsupported discovered chain schema vocabulary at " + location)
    if "oneOf" in schema:
        matches = 0
        for variant in schema["oneOf"]:
            try:
                chain_schema(value, variant, location)
            except AssertionError:
                pass
            else:
                matches += 1
        if matches != 1:
            raise AssertionError("Discovered schema requires exactly one result variant: " + location)
    kind = schema.get("type")
    kinds = kind if isinstance(kind, list) else [kind]
    matches = {"object": isinstance(value, dict), "array": isinstance(value, list),
               "string": isinstance(value, str), "boolean": isinstance(value, bool), "null": value is None,
               "integer": type(value) is int, "number": type(value) in (int, float) and math.isfinite(value)}
    if kind is not None and not any(matches.get(item, False) for item in kinds):
        raise AssertionError("Discovered schema type mismatch: " + location)
    if "enum" in schema and value not in schema["enum"]:
        raise AssertionError("Discovered schema enum mismatch: " + location)
    if "const" in schema and (value != schema["const"] or type(value) is not type(schema["const"])):
        raise AssertionError("Discovered schema const mismatch: " + location)
    if type(value) in (int, float):
        if value < schema.get("minimum", -math.inf) or value > schema.get("maximum", math.inf):
            raise AssertionError("Discovered schema numeric range mismatch: " + location)
    if isinstance(value, str) and not schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", math.inf):
        raise AssertionError("Discovered schema text length mismatch: " + location)
    if isinstance(value, list):
        if len(value) > schema.get("maxItems", math.inf):
            raise AssertionError("Discovered schema array bound mismatch: " + location)
        for index, item in enumerate(value):
            chain_schema(item, schema.get("items", {}), location + "[" + str(index) + "]")
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        if set(schema.get("required", [])) - set(value):
            raise AssertionError("Discovered schema required field missing: " + location)
        if schema.get("additionalProperties") is False and set(value) - set(properties):
            raise AssertionError("Discovered schema unknown result field: " + location)
        for key, item in value.items():
            if key in properties:
                chain_schema(item, properties[key], location + "." + key)


def await_reopened_sdk_projection(read_state, read_plugins, source_project, source_owners,
                                  clock=time.monotonic, pause=time.sleep):
    """Observe THIS known-present original SDK fixture, never a missing-provider rule.

    Open completes its document first. Native control-only owners prepare async;
    every intermediate observation must exclude ALL previous-session instances.
    The existing projection observation deadline remains40s; no mutation retries.
    """
    expected = {row["id"] for row in source_owners}
    old_instances = {row["instance"] for row in source_owners}
    if (not source_project or not expected or len(expected) != len(source_owners) or
            any(row.get("available") is not True or not row.get("instance") for row in source_owners)):
        raise AssertionError("Reopen oracle requires exact known-present original SDK source owners")
    began = clock()
    reopened_project = None
    while True:
        current, actual = read_state(), read_plugins()
        identity = current.get("projectIdentity")
        if not identity or identity == source_project:
            raise AssertionError("Document-complete Open did not establish a fresh project session")
        if reopened_project is None:
            reopened_project = identity
        if identity != reopened_project:
            raise AssertionError("Reopen observation changed its exact destination project session")
        if ({row["id"] for row in actual} != expected or len(actual) != len(expected) or
                any(row.get("instance") in old_instances for row in actual)):
            raise AssertionError("Reopen exposed stale native ownership or changed exact plugin IDs")
        if (current.get("channelRouting", {}).get("audioProjectionDeferred") is not True or
                type(current.get("pendingAudioUpdate")) is not bool):
            raise AssertionError("Reopen lost explicit deferred/native preparation state")
        if any(type(row.get("available")) is not bool or
               (row["available"] and not row.get("instance")) for row in actual):
            raise AssertionError("Reopened native availability has no genuine instance identity")
        if not current["pendingAudioUpdate"]:
            if (current.get("audio", {}).get("pluginSourceState") != "controlOnly" or
                    not all(row["available"] and row.get("instance") for row in actual) or
                    len({row["instance"] for row in actual}) != len(expected)):
                raise AssertionError("Settled original SDK reopen lacks exact fresh native control-only owners")
            return current, actual
        if clock() - began >= 40:
            raise AssertionError("Reopened original SDK native preparation exceeded existing40s observation")
        pause(.03)


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    output = client = None
    report = {"passed": False, "finished": False, "checks": [], "operations": [], "nativeInput": False,
        "PCM": False, "hardware": False, "listening": False, "installedProviderQualification": False,
        "scope": "External supported stdio, original SDK state capture and real portable preset/project files"}
    began = time.monotonic()

    def retain():
        if output is not None:
            (output / "verification.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        report["checks"].append(message)

    try:
        app, seed, provider = (explicit(raw) for raw in sys.argv[1:4])
        full_seed_chain = read_chain(seed)
        check(1 <= len(full_seed_chain["effects"]) <= 16, "Production SDK seed contains bounded genuine ordered effects")
        for seed_slot in full_seed_chain["effects"]:
            seed_descriptor = ET.fromstring(seed_slot["descriptor"])
            check(seed_descriptor.get("name") == "VibeRa Parameter Fixture" and
                  Path(seed_descriptor.attrib["file"]).resolve(strict=True) == provider.resolve(strict=True),
                  "Every ordered seed slot binds the exact original SDK module")
            original_fixture_state(seed_slot["state"])
        # Preserve the original one-slot source/Copy/Save oracle independently of
        # the full production seed used later for actual multi-slot Apply.
        source_chain = {**full_seed_chain, "effects": full_seed_chain["effects"][:1]}
        slot = source_chain["effects"][0]
        descriptor = ET.fromstring(slot["descriptor"])
        check(descriptor.get("name") == "VibeRa Parameter Fixture" and
              Path(descriptor.attrib["file"]).resolve(strict=True) == provider.resolve(strict=True),
              "Production descriptor resolves to exact caller-supplied original SDK module")
        native_values = original_fixture_state(slot["state"])
        check(len(native_values) == 6, "Actual JUCE envelope contains matching exact six-double component/controller states")
        initial_gain = native_values[0]
        check(math.isfinite(initial_gain) and 0 <= initial_gain <= 1, "Original native gain is decoded independently")
        output = fresh_output(sys.argv[4], app)
        worker = explicit(str(app.parent / "VibeRa Plugin Worker.exe"))
        seed_receipt = explicit(str(seed.parent.parent / "chain-workflow.json"))
        seed_identities = explicit(str(seed.parent.parent / "identities.txt"))
        if seed_receipt.stat().st_size > 8 * 1024 * 1024 or seed_identities.stat().st_size > 65536:
            raise ValueError("Seed evidence exceeds bounded original fixture receipt size")
        source_receipt = json.loads(seed_receipt.read_text(encoding="utf-8-sig"))
        completed_saves = [entry.get("result", {}) for entry in source_receipt.get("calls", [])]
        check(source_receipt.get("passed") is True and any(row.get("action") == "save" and
              row.get("completionState") == "completed" and row.get("fileCommitted") is True and
              Path(row.get("path", "")).resolve() == seed.resolve() and
              len(row.get("provenance", [])) == len(full_seed_chain["effects"]) and
              all(entry.get("stateSource") == "nativeAcknowledgement" for entry in row["provenance"])
              for row in completed_saves), "Seed is retained successful genuine native-capture/save fixture output")
        recorded_identities = {}
        for line in seed_identities.read_text(encoding="utf-8-sig").splitlines():
            recorded_path, recorded_sha = line.rsplit(" sha256=", 1)
            recorded_identities[Path(recorded_path).resolve()] = recorded_sha.lower()
        check(all(recorded_identities.get(path.resolve()) == sha(path) for path in (app, worker, provider)),
              "Production seed receipt binds this exact app, worker and original SDK binary")
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        client_path = explicit(str(client_path))
        for label, path in (("application", app), ("worker", worker), ("provider", provider), ("seed", seed),
                            ("client", client_path), ("verifier", Path(__file__)), ("projectHeader", ROOT / "src/Project.h"),
                            ("seedReceipt", seed_receipt), ("seedIdentities", seed_identities)):
            report[label] = {"path": str(path), "sha256": sha(path)}
        header = (ROOT / "src/Project.h").read_text(encoding="utf-8-sig")
        match = re.search(r"static\s+constexpr\s+int\s+currentVersion\s*=\s*(\d+)\s*;", header)
        check(match is not None, "Derive current project schema from authoritative Project.h")
        version = int(match[1])
        report.update(authoritativeProjectVersion=version, decodedInitialGain=initial_gain, output=str(output))
        retain()
        loader = importlib.util.spec_from_file_location("vibera_chain_neutral_client", client_path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        client = module.VibeRaClient(app)
        report.update(ownedPid=client.process.pid, server=client.server)
        discovered_chain = None

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args, allow_failure=True)
            with (output / "rpc.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "arguments": args or {}, "response": response}, ensure_ascii=False) + "\n")
            if tool == "vibera_mixerchain":
                check(discovered_chain is not None, "Chain discovery precedes requests")
                if not allow_failure:
                    chain_schema(args or {}, discovered_chain["inputSchema"], "request")
                value = response["structuredContent"]
                chain_schema(value, discovered_chain["outputSchema"])
                if "documentChanged" in value and value.get("action") not in {
                        "paste", "loadApply", "replace", "history", "insertCreate", "insertDelete", "insertHistory"}:
                    check(value["documentChanged"] is False and value["undoable"] is False,
                          "Copy/save/loadClipboard and non-document replies remain exactly nonmutating")
            if response.get("isError") and not allow_failure:
                raise AssertionError("Actual MCP refusal: " + json.dumps(response, ensure_ascii=False))
            return response

        def data(tool, args=None):
            return call(tool, args)["structuredContent"]

        def chain(args, allow_failure=False):
            return call("vibera_mixerchain", args, allow_failure)["structuredContent"]

        def terminal(admission, tool="vibera_mixerchain", expected="completed"):
            current = admission
            started = time.monotonic()
            while current["completionState"] == "pending" and time.monotonic() - started < 40:
                time.sleep(.03)
                current = call(tool, {"action": "job", "operation": admission["operation"]}, True)["structuredContent"]
            report["operations"].append({"tool": tool, "admission": admission, "terminal": current,
                                         "seconds": time.monotonic() - started})
            retain()
            check(current["completionState"] == expected, "Actual job terminal is " + expected + ": " + str(current))
            return current

        def state():
            return data("vibera_state")

        def settled_projection():
            started = time.monotonic()
            current = state()
            while current["pendingAudioUpdate"] and time.monotonic() - started < 40:
                time.sleep(.03)
                current = state()
            check(not current["pendingAudioUpdate"], "Actual pending projection settled before one capture admission")
            return current

        def plugins():
            page = data("vibera_plugin", {"action": "list", "limit": 512})
            check(page["nextOffset"] is None, "All actual plugin targets fit bounded readback")
            return page["items"]

        def model():
            current = state()
            result = {key: current[key] for key in ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable",
                "playing", "patternMode", "recordArmed", "recordingTake", "selectedPattern", "selectedChannel")}
            for entity in ("channels", "patterns", "clips", "mixer"):
                page = data("vibera_inspect", {"entity": entity, "limit": 512})
                check(page["nextOffset"] is None, "Complete canonical readback: " + entity)
                # routingTargets is derived availability over the CURRENT
                # universe, not a persisted route. Membership legitimately
                # changes that list on every surviving strip. Keep exact routes.
                result[entity] = [{key: value for key, value in row.items()
                                   if key not in {"meter", "routingTargets"}} for row in page["items"]]
            result["notes"] = {}
            for pattern in result["patterns"]:
                page = data("vibera_inspect", {"entity": "notes", "pattern": pattern["id"], "limit": 512})
                check(page["nextOffset"] is None, "Complete original Pattern note readback")
                result["notes"][str(pattern["id"])] = page["items"]
            result["plugins"] = [{key: row[key] for key in FIELDS} for row in plugins()]
            return result

        def request(action, **extra):
            current = state()
            return dict(action=action, mixer=1, projectIdentity=current["projectIdentity"], revision=current["revision"], **extra)

        def failure(args, code, wire=False):
            reply = call("vibera_mixerchain", args, True)
            value = reply.get("structuredContent", {})
            check(reply.get("isError") is True and value.get("completionState") == "failed" and (value.get("error", {}).get("code") if wire else value.get("code")) == code,
                  "Typed unchanged-operation refusal: " + code)
            return value

        def save_project(path):
            check(path.parent == output and not path.exists(), "Project output is fresh task-owned file")
            terminal(data("vibera_project", {"action": "save", "path": str(path), "overwrite": False}), "vibera_project")
            check(path.is_file(), "Actual project save produced file")

        tools = client.request("tools/list", {})["tools"]
        (output / "tools.json").write_text(json.dumps(tools, indent=2) + "\n", encoding="utf-8")
        state_tool = next(row for row in tools if row["name"] == "vibera_state")
        audio_schema = state_tool["outputSchema"]["properties"]["audio"]["properties"]
        source_fields = {"pluginSourceState", "pluginTransportPackets", "maximumPhysicalFrames",
                         "maximumSourceFrames", "pluginDeviceEpoch"}
        check(source_fields <= set(audio_schema) and
              set(audio_schema["pluginSourceState"]["enum"]) ==
              {"notRequired", "controlOnly", "pending", "active", "fault", "unavailable"} and
              all(audio_schema[name]["type"] == "integer" and audio_schema[name]["minimum"] == 0
                  for name in source_fields - {"pluginSourceState"}),
              "Actual typed source readiness and callback-budget discovery")
        tool = next(row for row in tools if row["name"] == "vibera_mixerchain")
        discovered_chain = tool
        check(set(tool["inputSchema"]["properties"]["action"]["enum"]) ==
              {"state", "copy", "save", "loadClipboard", "paste", "loadApply", "configureDirectory", "job", "cancel"} and
              tool["annotations"]["destructiveHint"] and tool["outputSchema"]["type"] == "object" and
              len(tool["outputSchema"]["oneOf"]) == 2 and
              tool["outputSchema"]["oneOf"][0]["properties"]["undoable"]["type"] == "boolean" and
              "const" not in tool["outputSchema"]["oneOf"][0]["properties"]["undoable"] and
              tool["outputSchema"]["oneOf"][0]["properties"]["documentChanged"]["type"] == "boolean" and
              tool["outputSchema"]["oneOf"][0]["properties"]["nativeRecoveryRequired"]["type"] == "boolean",
              "Actual typed Copy/preset/Apply discovery distinguishes document history and native recovery")
        fields = tool["inputSchema"]["properties"]
        check(fields["revision"]["maximum"] == 9007199254740991 and fields["mixer"]["minimum"] == 0 and
              fields["path"]["maxLength"] == 240 and fields["projectIdentity"]["minLength"] == 1,
              "Actual exact targeting/path ranges retain authoritative new schema")
        for command, action in (("mixer.chain.copy", "copy"), ("mixer.chain.save", "save"),
                                ("mixer.chain.loadClipboard", "loadClipboard"), ("mixer.chain.directory", "configureDirectory"),
                                ("mixer.chain.cancel", "cancel")):
            contract = data("vibera_commands", {"command": command, "includeSchemas": True})["operations"][0]
            check(contract["supported"] and not contract["documentEdit"] and not contract["macroAvailable"] and
                  contract["invocation"] == {"tool": "vibera_mixerchain", "action": action} and contract["stateReadback"],
                  "Canonical UI/MCP registry mapping: " + command)
        for command, action in (("mixer.chain.paste", "paste"), ("mixer.chain.loadApply", "loadApply")):
            contract = data("vibera_commands", {"command": command, "includeSchemas": True})["operations"][0]
            check(contract["supported"] and contract["documentEdit"] is True and not contract["macroAvailable"] and
                  contract["invocation"] == {"tool": "vibera_mixerchain", "action": action} and contract["stateReadback"],
                  "Canonical mutating Apply registry mapping: " + command)
        check(state()["projectVersion"] == version and not state()["playing"] and
              data("vibera_catalogue", {"action": "state"})["disposableSettings"], "Actual stopped disposable settings/current-schema server")
        base = output / "base.vibra"
        save_project(base)
        fixture = output / "original-sdk.vibra"
        plugin_id = seed_project(base, fixture, source_chain, version)
        report["fixture"] = {"path": str(fixture), "sha256": sha(fixture), "reservedPluginId": plugin_id}
        terminal(data("vibera_project", {"action": "open", "path": str(fixture), "discardChanges": True}), "vibera_project")
        deadline = time.monotonic() + 40
        actual = []
        while time.monotonic() < deadline:
            actual = plugins()
            if len(actual) == 1 and actual[0]["id"] == plugin_id and actual[0]["available"] and actual[0]["instance"] and not state()["pendingAudioUpdate"]:
                break
            time.sleep(.03)
        report["actualPlugin"] = actual
        retain()
        check(len(actual) == 1 and actual[0]["id"] == plugin_id and actual[0]["mixer"] == 1 and
              actual[0]["available"] and actual[0]["instance"] and not state()["pendingAudioUpdate"], "Real SDK Host is healthy/current before Copy; no placeholder success")
        source_state = state()["audio"]
        check(source_state["pluginSourceState"] == "controlOnly" and
              all(type(source_state[name]) is int and source_state[name] == 0
                  for name in source_fields - {"pluginSourceState"}),
              "Device-free real SDK controls truthfully report no live source completion or callback budget")
        instance = actual[0]["instance"]
        metadata = data("vibera_plugin", {"action": "parameters", "plugin": plugin_id, "instance": instance})
        check(next(p for p in metadata["items"] if p["id"] == 0)["normalized"] == initial_gain,
              "Actual restored native catalogue matches independently decoded seed gain")
        data("vibera_command", {"command": "mixer.gain", "args": {"mixer": 1, "value": .5}})
        data("vibera_command", {"command": "edit.undo"})
        settled_projection()
        baseline = model()
        check(baseline["redoAvailable"], "Real musical Redo exists before non-document chain jobs")
        presets = output / "presets"
        presets.mkdir()
        check(chain({"action": "configureDirectory", "directory": str(presets)})["completionState"] == "completed", "Explicit local owned preset directory configured")
        copy_admission = chain(request("copy"))
        check(copy_admission["completionState"] == "pending" and copy_admission["operation"],
              "Real native Copy admission explicitly remains pending until actual acknowledgement")
        copied = terminal(copy_admission)
        check(copied["provenance"] == [{"plugin": plugin_id, "instance": instance,
              "revision": copied["provenance"][0]["revision"], "stateSource": "nativeAcknowledgement"}], "Real targeted native capture receipt, never retained-placeholder provenance")
        copied_clipboard = chain({"action": "state"})["state"]["clipboard"]
        copy_token = copied["operation"]
        destination = presets / "fresh.vibfx"
        save_admission = chain(request("save", path=str(destination), overwrite=False))
        check(save_admission["completionState"] == "pending" and not destination.exists(),
              "Fresh Save admission is not a fabricated file completion")
        saved = terminal(save_admission)
        check(saved["fileCommitted"] and read_chain(destination) == source_chain, "Actual fresh native preset contains exact independent ordered descriptor/state/wet/bypass bytes")
        destination_sha = sha(destination)
        refused = terminal(chain(request("save", path=str(destination), overwrite=False)), expected="failed")
        check(refused["code"] == "chain.file.outputExists" and sha(destination) == destination_sha,
              "Asynchronous no-overwrite failure preserves previous exact file")
        terminal(chain(request("save", path=str(destination), overwrite=True)))
        check(read_chain(destination) == source_chain, "Explicit overwrite completes actual exact portable save")
        empty = request("copy")
        empty["mixer"] = 2
        terminal(chain(empty))
        check(chain({"action": "state"})["state"]["clipboard"]["slots"] == [], "Distinct empty clipboard precedes genuine load")
        terminal(chain({"action": "loadClipboard", "path": str(destination)}))
        loaded_clipboard = chain({"action": "state"})["state"]["clipboard"]
        check(loaded_clipboard["slots"] == copied_clipboard["slots"] and
              loaded_clipboard["effectsBypassed"] == copied_clipboard["effectsBypassed"], "Actual file decode replaces distinct clipboard with exact portable metadata")
        malformed = presets / "malformed.vibfx"
        with malformed.open("xb") as stream:
            stream.write(b"Not a VibeRa chain")
        bad = terminal(chain({"action": "loadClipboard", "path": str(malformed)}), expected="failed")
        check(bad["code"] and chain({"action": "state"})["state"]["clipboard"] == loaded_clipboard,
              "Malformed actual file terminal failure preserves clipboard")
        failure({"action": "job", "operation": "foreign-operation"}, "effectChain.unknownOperation")
        failure({**request("copy"), "revision": baseline["revision"] + 1}, "effectChain.staleCapture")
        failure({"action": "state", "mixer": 1}, "effectChain.invalidArguments")
        for invalid in ({**request("copy"), "mixer": True},
                        {**request("copy"), "revision": "1"}, {"action": "job", "operation": 9},
                        {**request("save", path=str(destination)), "overwrite": "true"},
                        {"action": "state", "foreignField": 1}):
            failure(invalid, "invalid_arguments", wire=True)
        check(chain({"action": "state"})["state"]["clipboard"] == loaded_clipboard,
              "Wrong types/action-specific extra fields preserve exact published clipboard")
        pending = chain(request("copy"))
        cancel_reply = call("vibera_mixerchain", {"action": "cancel", "operation": pending["operation"]}, True)
        cancellation = cancel_reply["structuredContent"]
        if cancellation["completionState"] == "pending" or cancellation["completionState"] == "cancelled":
            terminal(cancellation, expected="cancelled")
            report["acceptedCaptureCancellation"] = True
            check(chain({"action": "state"})["state"]["clipboard"] == loaded_clipboard,
                  "Actually accepted cancellation preserves prior exact clipboard")
        else:
            check(cancel_reply.get("isError") is True and cancellation["code"] == "effectChain.terminal",
                  "Sequential stdio cancellation honestly refuses already-terminal capture")
            terminal(pending)
            report["acceptedCaptureCancellation"] = False
        check(model() == baseline and sha(destination) == destination_sha,
              "Chain success/failure/cancel preserve inspected canonical model/notes, real history and preset bytes")
        data("vibera_command", {"command": "edit.redo"})
        check(next(row for row in model()["mixer"] if row["index"] == 1)["gain"] == .5, "Existing exact musical Redo survives every chain job")
        data("vibera_command", {"command": "edit.undo"})
        settled_projection()
        saved_project = output / "saved-sdk.vibra"
        save_project(saved_project)
        old_session = state()["projectIdentity"]
        clipboard_before_open = chain({"action": "state"})["state"]["clipboard"]
        terminal(data("vibera_project", {"action": "open", "path": str(saved_project), "discardChanges": True}), "vibera_project")
        reopened = state()
        check(reopened["projectIdentity"] != old_session and not reopened["undoAvailable"] and not reopened["redoAvailable"] and
              [row["id"] for row in plugins()] == [plugin_id], "Actual project reopen retains effect stable ID and resets document history/session")
        reopened_model = model()
        check(all(reopened_model[key] == baseline[key] for key in ("mixer", "plugins", "notes")),
              "Actual project reopen restores exact inspected Mixer/effect mix/order/flags and original notes")
        check(chain({"action": "state"})["state"]["clipboard"] == clipboard_before_open,
              "Same-Studio clipboard is runtime workspace data, independent of reopened document")
        failure(dict(action="copy", mixer=1, projectIdentity=old_session, revision=state()["revision"]), "effectChain.staleCapture")
        historical = chain({"action": "job", "operation": copy_token})
        check({k: v for k, v in historical.items() if k != "state"} == {k: v for k, v in copied.items() if k != "state"},
              "Exact compact terminal job persists across actual project save/reopen; current workspace state is separate")
        project_xml = ET.parse(saved_project).getroot()
        saved_slot = next(row for row in project_xml.findall("./MIXER/INSERT/PLUGIN") if int(row.attrib["id"]) == plugin_id)
        saved_insert = next(row for row in project_xml.findall("./MIXER/INSERT") if row.attrib["index"] == "1")
        check(saved_slot.attrib["state"] == slot["state"] and saved_slot.attrib["descriptor"] == slot["descriptor"] and
              saved_slot.attrib["name"] == slot["name"] and float(saved_slot.get("wet", "1")) == slot["wet"] and
              saved_slot.attrib["bypass"] == ("1" if slot["bypass"] else "0") and
              saved_insert.get("effectsBypassed", "0") == ("1" if source_chain["effectsBypassed"] else "0"),
              "Actual project save/reopen retains genuine opaque state, descriptor, name, wet and independent slot/chain flags")
        # NEW B workflow: actual destination mutation, explicitly no physical
        # device. Deferred document application is never live-audio completion.
        apply_before = model()
        apply_clipboard = chain({"action": "state"})["state"]["clipboard"]

        def apply_args(action, **extra):
            return {**request(action, **extra), "mixer": 2}

        def row_chain(snapshot, insert):
            return next(row for row in snapshot["mixer"] if row["index"] == insert)

        def unchanged_outside_chain(before, after, insert):
            check(all(before[key] == after[key] for key in ("channels", "patterns", "clips", "notes")),
                  "Apply/history preserves exact source Channels, Patterns, placement and notes")
            def nonchain(rows):
                return [{key: value for key, value in row.items()
                         if row["index"] != insert or key not in {"effects", "effectsBypassed"}} for row in rows]
            check(nonchain(before["mixer"]) == nonchain(after["mixer"]) and
                  [p for p in before["plugins"] if p["mixer"] != insert] ==
                  [p for p in after["plugins"] if p["mixer"] != insert],
                  "Apply/history preserves routes, insert names/colours, source effect IDs and non-target chains")

        def applied(admission):
            check(admission["completionState"] == "pending" and admission.get("operation"),
                  "Actual Apply/history admission returns its owned pending operation")
            result = terminal(admission)
            check(result["operation"] == admission["operation"] and result["documentChanged"] is True and
                  result["undoable"] is True and result.get("nativeRecoveryRequired") is False and
                  result.get("audioState") == "deferredNoDevice" and not state()["playing"],
                  "Exact terminal Apply/history owns a one-Undo document change; device-free audio stays deferred")
            check(chain({"action": "state"})["state"]["clipboard"] == apply_clipboard,
                  "Actual Apply/history never replaces the portable clipboard")
            return model()

        def history(command, action="history"):
            admission = data("vibera_command", {"command": command})
            # edit.undo/redo MUST return this chain job, not a generic success or
            # unrelated routing token. Poll the authoritative chain service.
            chain_schema(admission, discovered_chain["outputSchema"])
            check(admission.get("action") == action, "Undo/Redo returns the exact structural-history job: " + action)
            return applied(admission)

        def exact_slots(snapshot, portable):
            row = row_chain(snapshot, 2)
            check(row["effectsBypassed"] == portable["effectsBypassed"] and
                  [{key: item[key] for key in ("name", "bypass", "wet")} for item in row["effects"]] ==
                  [{key: item[key] for key in ("name", "bypass", "wet")} for item in portable["effects"]],
                  "Literal ordered destination names/wet/bypass and chain flags match genuine portable data")
            return [item["id"] for item in row["effects"]]

        check(row_chain(apply_before, 2)["effects"] == [], "Actual Paste destination initially empty")
        pasted = applied(chain(apply_args("paste")))
        pasted_ids = exact_slots(pasted, source_chain)
        check(len(pasted_ids) == 1 and plugin_id not in pasted_ids and len(set(pasted_ids)) == len(pasted_ids),
              "Empty-target Paste allocates a fresh stable effect ID, never aliases its source")
        unchanged_outside_chain(apply_before, pasted, 2)
        undone = history("edit.undo")
        check(row_chain(undone, 2) == row_chain(apply_before, 2), "One actual Undo restores the empty destination")
        redone = history("edit.redo")
        check(exact_slots(redone, source_chain) == pasted_ids,
              "Actual chain Redo restores exact allocated IDs/order rather than pasting a duplicate")
        unchanged_outside_chain(apply_before, redone, 2)

        full_input = presets / "genuine-full-seed.vibfx"
        with full_input.open("xb") as stream:
            stream.write(seed.read_bytes())
        check(read_chain(full_input) == full_seed_chain, "Owned full Apply input is exact genuine seed bytes")
        full_input_sha = sha(full_input)
        occupied_before = model()
        loaded = applied(chain(apply_args("loadApply", path=str(full_input))))
        loaded_ids = exact_slots(loaded, full_seed_chain)
        check(len(loaded_ids) == len(full_seed_chain["effects"]) and len(set(loaded_ids)) == len(loaded_ids) and
              not set(loaded_ids) & {p["id"] for p in occupied_before["plugins"]},
              "Occupied Load-and-apply replaces with fresh unique IDs and preserves genuine ordered slot count")
        unchanged_outside_chain(occupied_before, loaded, 2)
        loaded_undo = history("edit.undo")
        check(exact_slots(loaded_undo, source_chain) == pasted_ids,
              "One occupied Apply Undo restores prior whole chain and exact IDs")
        loaded_redo = history("edit.redo")
        check(exact_slots(loaded_redo, full_seed_chain) == loaded_ids,
              "Occupied Apply Redo restores the full chain with original allocated IDs")
        unchanged_outside_chain(apply_before, loaded_redo, 2)
        replacement_contract = data("vibera_commands", {
            "command": "plugin.effect.replace", "includeSchemas": True})["operations"][0]
        check(replacement_contract["documentEdit"] and replacement_contract["asynchronous"] and
              not replacement_contract["macroAvailable"] and
              set(replacement_contract["arguments"]["required"]) == {"target", "text"},
              "Single-slot Replace advertises typed target/descriptor, actual async history and no synchronous macro")
        replace_admission = data("vibera_command", {
            "command": "plugin.effect.replace", "revision": state()["revision"],
            "args": {"target": loaded_ids[0], "text": full_seed_chain["effects"][0]["descriptor"]}})
        check(replace_admission["action"] == "replace", "Replace returns its exact chain-operation receipt")
        replaced = applied(replace_admission)
        replaced_slots = row_chain(replaced, 2)["effects"]
        old_slots = row_chain(loaded_redo, 2)["effects"]
        replacement_id = replaced_slots[0]["id"]
        check(len(replaced_slots) == len(old_slots) and replacement_id not in loaded_ids and
              replaced_slots[1:] == old_slots[1:] and
              all(replaced_slots[0][key] == old_slots[0][key] for key in ("wet", "bypass")),
              "Single-slot Replace allocates one new ID and preserves every neighbour and host slot control")
        unchanged_outside_chain(loaded_redo, replaced, 2)
        replacement_undo = history("edit.undo")
        check(exact_slots(replacement_undo, full_seed_chain) == loaded_ids,
              "Single-slot Undo restores original complete chain IDs/order/flags")
        replacement_redo = history("edit.redo")
        check(row_chain(replacement_redo, 2)["effects"][0]["id"] == replacement_id and
              row_chain(replacement_redo, 2)["effects"][1:] == old_slots[1:],
              "Single-slot Redo restores allocated replacement ID and untouched neighbours")
        loaded_redo = history("edit.undo")
        check(exact_slots(loaded_redo, full_seed_chain) == loaded_ids,
              "Restore loaded portable state before independent exact persistence oracle")
        report["appliedSeedSlots"] = len(full_seed_chain["effects"])
        report["unequalMultiSlotApplyObserved"] = (len(full_seed_chain["effects"]) >= 2 and
            len({item["state"] for item in full_seed_chain["effects"]}) >= 2)
        failures_before = model()
        bad_apply = terminal(chain(apply_args("loadApply", path=str(malformed))), expected="failed")
        check(bad_apply["documentChanged"] is False and bad_apply["undoable"] is False and
              model() == failures_before and sha(full_input) == full_input_sha and
              chain({"action": "state"})["state"]["clipboard"] == apply_clipboard,
              "Malformed asynchronous Load-and-apply preserves exact old model/history/clipboard/file")
        failure({**apply_args("paste"), "revision": state()["revision"] + 1}, "effectChain.staleCapture")
        failure({**apply_args("loadApply", path=str(full_input)), "projectIdentity": old_session}, "effectChain.staleCapture")
        failure({**apply_args("paste"), "path": str(full_input)}, "effectChain.invalidArguments")
        failure({**apply_args("loadApply", path=str(full_input)), "mixer": True}, "invalid_arguments", wire=True)
        check(model() == failures_before, "Invalid/stale Apply arguments preserve real history and canonical content")

        # Cancellation of a real malformed-file load cannot race into a valid
        # mutation: retain either accepted cancellation or exact failed job.
        cancel_load = chain(apply_args("loadApply", path=str(malformed)))
        cancelled_reply = call("vibera_mixerchain", {"action": "cancel", "operation": cancel_load["operation"]}, True)
        cancelled_load = cancelled_reply["structuredContent"]
        if cancelled_load["completionState"] in {"pending", "cancelled"}:
            terminal(cancelled_load, expected="cancelled")
            report["acceptedApplyFileCancellation"] = True
        else:
            check(cancelled_reply.get("isError") is True and
                  (cancelled_load["code"] in {"effectChain.terminal", "effectChain.tooLate"} or
                   (cancelled_load["operation"] == cancel_load["operation"] and
                    cancelled_load["code"] == "chain.file.decodeFailed" and
                    cancelled_load["documentChanged"] is False)),
                  "Late structural file cancellation returns refusal or exact already-failed decode job")
            terminal(cancel_load, expected="failed")
            report["acceptedApplyFileCancellation"] = False
        check(model() == failures_before and chain({"action": "state"})["state"]["clipboard"] == apply_clipboard,
              "Cancelled/terminal malformed Apply preserves prior exact destination/history/clipboard")

        applied_project = output / "applied-sdk.vibra"
        applied_snapshot = model()
        save_project(applied_project)
        applied_xml = ET.parse(applied_project).getroot()
        stored = next(row for row in applied_xml.findall("./MIXER/INSERT") if row.get("index") == "2")
        stored_slots = stored.findall("PLUGIN")
        check(len(stored_slots) == len(full_seed_chain["effects"]) and
              [int(row.attrib["id"]) for row in stored_slots] == loaded_ids and
              stored.get("effectsBypassed", "0") == ("1" if full_seed_chain["effectsBypassed"] else "0"),
              "Saved applied destination has exact ordered new IDs and chain-bypass flag")
        for actual_slot, expected_slot in zip(stored_slots, full_seed_chain["effects"]):
            check(actual_slot.get("state") == expected_slot["state"] and
                  actual_slot.get("descriptor") == expected_slot["descriptor"] and
                  actual_slot.get("name") == expected_slot["name"] and
                  float(actual_slot.get("wet", "1")) == expected_slot["wet"] and
                  actual_slot.get("bypass") == ("1" if expected_slot["bypass"] else "0") and
                  len(actual_slot.find("AUTOMATION_BINDINGS")) == 0 and
                  original_fixture_state(actual_slot.attrib["state"]) == original_fixture_state(expected_slot["state"]),
                  "Saved Apply preserves exact original opaque envelope/six-double payload with no copied bindings")
        pre_reopen_project = state()["projectIdentity"]
        pre_reopen_owners = plugins()
        terminal(data("vibera_project", {"action": "open", "path": str(applied_project), "discardChanges": True}), "vibera_project")
        reopened_state, reopened_owners = await_reopened_sdk_projection(
            state, plugins, pre_reopen_project, pre_reopen_owners)
        check(reopened_state["channelRouting"]["audioProjectionDeferred"] and
              not reopened_state["pendingAudioUpdate"] and
              {row["id"] for row in reopened_owners} == {row["id"] for row in pre_reopen_owners} and
              all(row["available"] and row["instance"] for row in reopened_owners),
              "Deferred project Open excludes every old-session native owner and asynchronously prepares exact fresh SDK control-only instances")
        reopened_applied = model()
        check(exact_slots(reopened_applied, full_seed_chain) == loaded_ids and
              all(reopened_applied[key] == applied_snapshot[key] for key in ("mixer", "plugins", "notes")) and
              not reopened_applied["undoAvailable"] and not reopened_applied["redoAvailable"] and
              chain({"action": "state"})["state"]["clipboard"] == apply_clipboard,
              "Actual applied-project reopen preserves exact chains/IDs and clears history while retaining session clipboard")
        report["appliedProject"] = {"path": str(applied_project), "sha256": sha(applied_project)}

        # Variable membership is a real native-universe operation, not a UI-only
        # display row. This branch deliberately has no physical device: require
        # exact deferred publication and public history/persistence, not PCM.
        for command in ("mixer.insert.create", "mixer.insert.delete"):
            contract = data("vibera_commands", {"command": command, "includeSchemas": True})["operations"][0]
            check(contract["supported"] and contract["documentEdit"] and not contract["macroAvailable"] and
                  contract["stateReadback"], "Discoverable canonical async lifecycle: " + command)

        def insert_command(command, args):
            current = state()
            return data("vibera_command", {"command": command, "args": args, "revision": current["revision"]})

        membership_before = model()
        create_admission = insert_command("mixer.insert.create", {"text": "Original semantic auxiliary"})
        check(create_admission.get("action") == "insertCreate", "Create returns its exact lifecycle job")
        membership = applied(create_admission)
        prior_ids = {row["index"] for row in membership_before["mixer"]}
        added = [row for row in membership["mixer"] if row["index"] not in prior_ids]
        check(len(added) == 1 and added[0]["name"] == "Original semantic auxiliary" and
              [row for row in membership["mixer"] if row["index"] in prior_ids] == membership_before["mixer"] and
              all(membership[key] == membership_before[key] for key in ("channels", "patterns", "clips", "notes", "plugins")),
              "Create preserves all original rows, native chains and musical content")
        created_id = added[0]["index"]
        undone = history("edit.undo", "insertHistory")
        check(all(undone[key] == membership_before[key] for key in
                  ("mixer", "channels", "patterns", "clips", "notes", "plugins")),
              "Actual lifecycle Undo restores the complete previous document content")
        redone = history("edit.redo", "insertHistory")
        check(redone["mixer"] == membership["mixer"], "Actual lifecycle Redo preserves stable insert identity")
        membership_project = output / "insert-membership.vibra"
        save_project(membership_project)
        membership_source_project = state()["projectIdentity"]
        membership_source_owners = plugins()
        terminal(data("vibera_project", {"action": "open", "path": str(membership_project),
                                         "discardChanges": True}), "vibera_project")
        await_reopened_sdk_projection(state, plugins, membership_source_project, membership_source_owners)
        restored_membership = model()
        check(all(restored_membership[key] == redone[key] for key in
                  ("mixer", "channels", "patterns", "clips", "notes", "plugins")) and
              not restored_membership["undoAvailable"] and not restored_membership["redoAvailable"],
              "Actual save/reopen persists variable membership and chains with a clean history")
        delete_admission = insert_command("mixer.insert.delete", {"mixer": created_id})
        check(delete_admission.get("action") == "insertDelete", "Delete returns its exact lifecycle job")
        deleted = applied(delete_admission)
        check(all(deleted[key] == membership_before[key] for key in
                  ("mixer", "channels", "patterns", "clips", "notes", "plugins")),
              "Unused insert deletion preserves the full original musical document")
        restored = history("edit.undo", "insertHistory")
        check(restored["mixer"] == membership["mixer"], "Deletion Undo restores exact stable insert")
        deleted_again = history("edit.redo", "insertHistory")
        check(deleted_again["mixer"] == membership_before["mixer"], "Deletion Redo removes the same insert")
        protected_before = model()
        protected_reply = call("vibera_command", {"command": "mixer.insert.delete", "args": {"mixer": 0}}, True)
        if protected_reply["structuredContent"].get("completionState") == "pending":
            protected_terminal = terminal(protected_reply["structuredContent"], expected="failed")
            check("masterProtected" in protected_terminal.get("message", "") or
                  "masterProtected" in protected_terminal.get("code", ""), "Master deletion has a structured refusal")
        else:
            check(protected_reply.get("isError") is True, "Master deletion refuses synchronously")
        check(model() == protected_before, "Master refusal changes no model/history/selection")
        report["insertMembershipProject"] = {"path": str(membership_project), "sha256": sha(membership_project)}
        check(sha(seed) == report["seed"]["sha256"] and sha(provider) == report["provider"]["sha256"], "Read-only source preset/provider identities preserved")
        report.update(passed=True, savedPresetSha256=sha(destination), savedProjectSha256=sha(saved_project),
            limits=["No PCM/playback/native-input/listening/hardware evidence", "Original SDK only, not arbitrary installed plugins",
                    "Sequential client can race capture completion; accepted cancellation reported only if actually observed",
                    "Clipboard/job history is session-only; Project persistence does not imply clipboard persistence",
                    "Paste/occupied Load-and-apply here are document-only deferredNoDevice; no active graph/PCM/voice continuity qualification",
                    "Multi-slot unequal-state coverage is reported only when actually present in the genuine seed"])
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
    finally:
        if client is not None:
            try:
                client.close()
                report.update(gracefulClose=True, ownedExitCode=client.process.returncode)
            except Exception as error:
                report.update(passed=False, closeError=str(error), gracefulClose=False, ownedExitCode=client.process.poll())
            if output is not None:
                (output / "application-stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        report.update(finished=True, seconds=time.monotonic() - began)
        retain()
    print(json.dumps({key: report.get(key) for key in ("passed", "finished", "seconds", "output", "error")}))
    return 0 if report["passed"] and report.get("gracefulClose") else 1


def prepare_seed(arguments):
    """Serial CTest driver; genuine fresh SDK capture before the external client.

    The enclosing engineering owner/CTest owns the process budget. This driver
    never terminates unrelated processes or reuses a previous successful seed.
    """
    if len(arguments) != 3:
        raise ValueError("--prepare-seed requires ABS_APP ABS_SDK_MODULE ABS_OUTPUT_PARENT")
    app, provider = (explicit(raw) for raw in arguments[:2])
    output = fresh_output(str(Path(arguments[2]) / ("attempt-" + uuid.uuid4().hex)), app)
    receipt = {"passed": False, "releaseQualification": False, "steps": [],
               "applicationSha256": sha(app), "providerSha256": sha(provider)}
    began = time.monotonic()
    try:
        def execute(label, command):
            start = time.monotonic()
            with (output / (label + ".log")).open("x", encoding="utf-8") as log:
                result = subprocess.run([str(item) for item in command], stdout=log, stderr=subprocess.STDOUT)
            receipt["steps"].append(dict(name=label, exitCode=result.returncode,
                                         seconds=time.monotonic() - start))
            if result.returncode:
                raise RuntimeError(label + " failed; original evidence retained")
        execute("actual-sdk-seed", [app, "--mixer-chain-workflow", output / "seed", provider])
        attempts = list((output / "seed").glob("attempt-*"))
        if len(attempts) != 1:
            raise RuntimeError("Expected exactly one genuinely fresh owned SDK seed attempt")
        seed = attempts[0] / "presets/target.vibfx"
        execute("external-stdio", [sys.executable, Path(__file__).resolve(), app, seed, provider,
                                    output / "external"])
        actual = json.loads((output / "external/verification.json").read_text(encoding="utf-8"))
        if not actual.get("passed") or not actual.get("finished") or not actual.get("gracefulClose"):
            raise RuntimeError("External semantic workflow did not actually finish successfully")
        receipt["passed"] = True
    except Exception as error:
        receipt.update(error=str(error), traceback=traceback.format_exc())
    finally:
        receipt["seconds"] = time.monotonic() - began
        (output / "driver.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(output=str(output), **receipt)))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(prepare_seed(sys.argv[2:]) if sys.argv[1:2] == ["--prepare-seed"] else main())
