"""Reproduce VibeRa's small, pinned CC0 recorded-instrument selection.

Run without arguments before --fetch. Downloads only declared WAV/FLAC files and source
maps/notices. No audio normalization, trimming, transcoding or loop invention.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import struct
import time
import urllib.parse
import urllib.request
import wave

ROOT = Path(__file__).resolve().parent
PINS = {
    "vsco2ce": ("VSCO-2-CE", "6dd651d55dde97fd4028699be9d4481f26917891"),
    "vcsl": ("VCSL", "dfcf4a4918771eee884b96ad4493de82ef84daf6"),
    "freepats-guitar": ("electric-guitar-FSBS-clean", "192cf0d9bf2c4ba6ead8e3524ba3f78818e4fe91"),
}
OWNERS = {"freepats-guitar": "freepats"}
# VCSL's sfz branch declares CC0 in README but does not carry the full legal
# text. Retain the publisher's same-repository master license at this pin.
LICENSE_PINS = {"vcsl": "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e"}
LICENSE_SHA256 = {"vcsl": "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499"}
# Exact MIDI roots are obtained from upstream SFZ pitch_keycenter, not filenames.
# The numbers below are desired coverage centres, resolved to recorded roots.
SELECTION = [
    ("piano-upright", "Recorded Upright Piano", "keys", "vsco2ce", "UprightPiano.sfz", [36, 48, 60, 72, 84, 96], [40, 100]),
    ("violin-solo", "Recorded Solo Violin", "strings", "vsco2ce", "SViolinVib.sfz", [55, 62, 69, 76, 83, 90], [40, 100]),
    ("contrabass-pizz", "Recorded Pizzicato Contrabass", "bass", "vsco2ce", "ContrabassPizz.sfz", [34, 40, 45, 50, 55, 59], [85]),
    ("cello-ensemble", "Recorded Cello Ensemble", "strings", "vsco2ce", "CelloEnsSusVib.sfz", [36, 45, 54, 63], [85]),
    ("harp", "Recorded Harp", "plucked", "vsco2ce", "Harp.sfz", [36, 48, 60, 72, 84], [85]),
    ("flute", "Recorded Flute", "woodwind", "vsco2ce", "FluteSusNV.sfz", [60, 69, 76, 84, 91], [85]),
    ("clarinet", "Recorded Clarinet", "woodwind", "vsco2ce", "ClarinetSus.sfz", [50, 58, 66, 74, 82], [85]),
    ("trumpet-staccato", "Recorded Staccato Trumpet", "brass", "vsco2ce", "TrumpetStac.sfz", [57, 62, 67, 72, 77, 81], [85]),
    ("horn", "Recorded French Horn", "brass", "vsco2ce", "FHornSus.sfz", [41, 50, 60, 69], [85]),
    ("trombone", "Recorded Tenor Trombone", "brass", "vsco2ce", "TromboneSus.sfz", [34, 46, 58, 69], [85]),
    ("tuba", "Recorded Tuba", "brass", "vsco2ce", "TubaSus.sfz", [29, 38, 47, 55], [85]),
    ("marimba", "Recorded Marimba", "mallets", "vsco2ce", "Marimba.sfz", [41, 48, 59, 65, 72, 84], [85]),
    ("strumstick", "Recorded Strumstick", "fretted", "vcsl", "Chordophones/Composite Chordophones/Strumstick.sfz", [50, 55, 60, 65, 70, 76], [85]),
    ("kalimba", "Recorded Tanzanian Kalimba", "world", "vcsl", "Idiophones/Plucked Idiophones/Kalimba, Tanzania.sfz", [43, 50, 58, 65, 73, 80], [85]),
    ("balafon", "Recorded Balafon", "world", "vcsl", "Idiophones/Struck Idiophones/Balafon - Traditional Mallet.sfz", [61, 65, 72, 77, 84, 89], [85]),
    ("conga", "Recorded Conga", "percussion", "vcsl", "Membranophones/Struck Membranophones/Conga.sfz", [60, 62, 64], [85]),
    ("bass-drum", "Recorded Bass Drum", "percussion", "vcsl", "Membranophones/Struck Membranophones/Bass Drum 1.sfz", [60], [85]),
    ("snare-drum", "Recorded Snare Drum", "percussion", "vcsl", "Membranophones/Struck Membranophones/Snare Drum, Modern 1.sfz", [60, 62, 64], [85]),
    ("electric-piano-fm", "Recorded FM Electric Piano", "electric-piano", "vcsl", "Electrophones/TX81Z - FM Piano.sfz", [36, 48, 60, 72, 84, 96], [40, 100]),
    ("guitar-clean", "Recorded Clean Electric Guitar", "guitar", "freepats-guitar", "EGuitarFSBS-clean bridge small 20260807.sfz", [36, 41, 45, 48, 52, 55, 59, 64, 67, 71, 74, 80, 85], [85]),
]


def request(url):
    req = urllib.request.Request(url, headers={"User-Agent": "VibeRa-Studio-CC0-asset-curation"})
    with urllib.request.urlopen(req, timeout=60) as response:
        return response.read()


def raw_url(source, path):
    repo, revision = PINS[source]
    owner = OWNERS.get(source, "sgossner")
    return f"https://raw.githubusercontent.com/{owner}/{repo}/{revision}/" + urllib.parse.quote(path)


def tree(source):
    repo, revision = PINS[source]
    cached = ROOT / "sources" / (repo + "-sfz-tree.json")
    if cached.exists():
        data = json.loads(cached.read_text())
    else:
        owner = OWNERS.get(source, "sgossner")
        data = json.loads(request(f"https://api.github.com/repos/{owner}/{repo}/git/trees/{revision}?recursive=1"))
    if data.get("sha") != revision or data.get("truncated"):
        raise ValueError("Incomplete or unpinned upstream tree")
    return {entry["path"]: entry for entry in data["tree"] if entry["type"] == "blob"}


def parse_sfz(text):
    control, global_ops, group_ops, regions = {}, {}, {}, []
    text = re.sub(r"//[^\n]*", "", text)
    for tag, body in re.findall(r"<(\w+)>([^<]*)", text):
        ops = {key: value.strip() for key, value in re.findall(r"\b(\w+)\s*=\s*(.*?)(?=\s+\w+\s*=|\Z)", body, re.S)}
        if tag == "control":
            control.update(ops)
        elif tag == "global":
            global_ops = ops
            group_ops = {}
        elif tag == "group":
            group_ops = ops
        elif tag == "region":
            regions.append({**global_ops, **group_ops, **ops})
    return control, regions


def build_plan(trees):
    plan = []
    for ident, name, family, source, mapping, targets, velocities in SELECTION:
        repo, _ = PINS[source]
        path = ROOT / "sources" / repo / mapping
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(request(raw_url(source, mapping)))
        map_raw = path.read_bytes()
        if hashlib.sha1(b"blob " + str(len(map_raw)).encode() + b"\0" + map_raw).hexdigest() != trees[source][mapping]["sha"]:
            raise ValueError("Pinned source map identity mismatch: " + mapping)
        controls, rows = parse_sfz(map_raw.decode("utf-8"))
        rows = [row for row in rows if row.get("seq_position", "1") == "1" and float(row.get("lorand", "0")) == 0]
        roots = sorted({int(row["pitch_keycenter"]) for row in rows})
        chosen = sorted({min(roots, key=lambda root: (abs(root - target), root)) for target in targets})
        percussion = family == "percussion"
        regions = []
        for index, root in enumerate(chosen):
            candidates = [row for row in rows if int(row["pitch_keycenter"]) == root]
            picked = []
            for velocity in velocities:
                row = min(candidates, key=lambda item: (0 if int(item.get("lovel", 0)) <= velocity <= int(item.get("hivel", 127)) else 1, abs((int(item.get("lovel", 0)) + int(item.get("hivel", 127))) / 2 - velocity)))
                if row["sample"] not in [item["sample"] for item in picked]:
                    picked.append(row)
            picked.sort(key=lambda item: int(item.get("lovel", 0)))
            for layer, row in enumerate(picked):
                relative = controls.get("default_path", "") + row["sample"]
                source_path = str(PurePosixPath(mapping).parent / relative.replace("\\", "/"))
                if source_path.startswith("./"):
                    source_path = source_path[2:]
                if source_path not in trees[source]:
                    raise ValueError("SFZ sample absent from pinned source: " + source_path)
                mode = row.get("loop_mode", "no_loop")
                if mode not in ("no_loop", "loop_continuous"):
                    raise ValueError("Loop requires explicit reviewed conversion: " + source_path)
                source_offset = int(row.get("offset", 0))
                loop = {"mode": "none", "startFrame": 0, "endFrame": 0}
                if mode == "loop_continuous":
                    loop = {"mode": "forward", "startFrame": int(row["loop_start"]) - source_offset,
                            "endFrame": int(row["loop_end"]) + 1 - source_offset,
                            "provenance": "Explicit pinned source SFZ loop_start/loop_end; inclusive source end converted to exclusive."}
                low = root if percussion else ((chosen[index - 1] + root) // 2 + 1 if index else max(0, root - 4))
                high = root if percussion else ((root + chosen[index + 1]) // 2 if index + 1 < len(chosen) else min(127, root + 4))
                regions.append({
                    "sample": "samples/" + source + "/" + source_path,
                    "rootNote": root, "lowNote": low, "highNote": high,
                    "lowVelocity": 0 if layer == 0 else int(row.get("lovel", 0)),
                    "highVelocity": 127 if layer + 1 == len(picked) else int(picked[layer + 1].get("lovel", 0)) - 1,
                    "gainDb": float(row.get("volume", 0)) - 12.0,
                    "pan": float(row.get("pan", 0)) / 100,
                    "sourceOffsetFrames": source_offset,
                    "tuneCents": float(row.get("tune", 0)),
                    "loop": loop,
                    "sourceId": source, "sourcePath": source_path,
                    "sourceGitBlobSha1": trees[source][source_path]["sha"],
                    "sourceBytes": trees[source][source_path]["size"],
                    "sourceVelocityRange": [int(row.get("lovel", 0)), int(row.get("hivel", 127))],
                })
        plan.append({"id": ident, "name": name, "family": family, "sourceKind": "recorded-sample", "sourceId": source,
                     "sourceMapping": "sources/" + repo + "/" + mapping,
                     "sourceMappingSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                     "mappingPolicy": "Selected roots with nearest-root key zones; one recorded dynamic except two piano/violin/FM-keyboard layers; first round robin only; source tuning/offset/gain minus12dB headroom; only explicit authored source loops.",
                     "regions": regions})
    return plan


def wav_metadata(raw):
    if raw.startswith(b"fLaC"):
        if len(raw) < 42 or raw[4] & 127 or int.from_bytes(raw[5:8], "big") != 34:
            raise ValueError("Invalid FLAC STREAMINFO")
        packed = int.from_bytes(raw[18:26], "big")
        info = {"format": "FLAC", "sampleRate": packed >> 44, "channels": ((packed >> 41) & 7) + 1,
                "bitsPerSample": ((packed >> 36) & 31) + 1, "frames": packed & ((1 << 36) - 1)}
        if info["channels"] not in (1, 2) or info["frames"] <= 0 or info["sampleRate"] <= 0:
            raise ValueError("Unsupported FLAC layout")
        return info
    with wave.open(io.BytesIO(raw), "rb") as wav:
        info = {"format": "WAV", "sampleRate": wav.getframerate(), "channels": wav.getnchannels(), "frames": wav.getnframes(), "bitsPerSample": wav.getsampwidth() * 8}
    if info["channels"] not in (1, 2) or info["frames"] <= 0:
        raise ValueError("Unsupported WAV layout")
    # Retain embedded sampler metadata as evidence; the curated map does not
    # enable it without a source-SFZ loop instruction.
    offset = 12
    while offset + 8 <= len(raw):
        kind, size = struct.unpack_from("<4sI", raw, offset)
        if kind == b"smpl" and size >= 36:
            info["embeddedMidiUnityNote"] = struct.unpack_from("<I", raw, offset + 20)[0]
            info["embeddedLoopCount"] = struct.unpack_from("<I", raw, offset + 36)[0]
        offset += 8 + size + (size & 1)
    return info


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--fetch", action="store_true")
    opts = args.parse_args()
    started = time.perf_counter()
    trees = {source: tree(source) for source in PINS}
    instruments = build_plan(trees)
    samples = {region["sample"]: region for instrument in instruments for region in instrument["regions"]}
    print(json.dumps({"instruments": len(instruments), "samples": len(samples), "bytes": sum(row["sourceBytes"] for row in samples.values()),
                      "byInstrument": {item["id"]: len(item["regions"]) for item in instruments}}, indent=2), flush=True)
    if not opts.fetch:
        return

    def obtain(pair):
        local, region = pair
        target = ROOT / local
        raw = target.read_bytes() if target.exists() else request(raw_url(region["sourceId"], region["sourcePath"]))
        git_sha = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
        if len(raw) != region["sourceBytes"] or git_sha != region["sourceGitBlobSha1"]:
            raise ValueError("Pinned upstream byte identity mismatch: " + local)
        info = wav_metadata(raw)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(raw)
        return local, {**info, "sha256": hashlib.sha256(raw).hexdigest()}

    with ThreadPoolExecutor(max_workers=4) as executor:
        metadata = dict(executor.map(obtain, samples.items()))
    for instrument in instruments:
        for region in instrument["regions"]:
            region.update(metadata[region["sample"]])
            if region["sourceOffsetFrames"] >= region["frames"]:
                raise ValueError("Source offset outside recorded PCM")
            if region["loop"]["mode"] == "forward" and not (0 <= region["loop"]["startFrame"] < region["loop"]["endFrame"] <= region["frames"] - region["sourceOffsetFrames"]):
                raise ValueError("Authored source loop outside prepared PCM")
    sources = []
    for source, (repo, revision) in PINS.items():
        notices = ["LICENSE.txt", "README.txt"] if source == "freepats-guitar" else ["LICENSE", "README.md"] + (["Readme.txt"] if source == "vsco2ce" else [])
        for notice in notices:
            target = ROOT / "sources" / (repo + "-" + notice)
            target.parent.mkdir(parents=True, exist_ok=True)
            url = raw_url(source, notice)
            if notice == "LICENSE" and source in LICENSE_PINS:
                url = f"https://raw.githubusercontent.com/sgossner/{repo}/{LICENSE_PINS[source]}/LICENSE"
            discovered_copy = ROOT / "sources" / repo / notice
            raw = target.read_bytes() if target.exists() else discovered_copy.read_bytes() if discovered_copy.exists() else request(url)
            if notice == "LICENSE" and source in LICENSE_SHA256:
                valid = hashlib.sha256(raw).hexdigest() == LICENSE_SHA256[source]
            else:
                valid = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() == trees[source][notice]["sha"]
            if not valid:
                raise ValueError("Pinned source notice identity mismatch: " + source + "/" + notice)
            if not target.exists():
                target.write_bytes(raw)
        license_path = "sources/" + repo + "-" + notices[0]
        sources.append({"id": source, "name": repo, "url": "https://github.com/" + OWNERS.get(source, "sgossner") + "/" + repo,
                        "revision": revision, "license": "CC0-1.0", "licensePath": license_path,
                        "licenseRevision": LICENSE_PINS.get(source, revision),
                        "licenseSha256": hashlib.sha256((ROOT / license_path).read_bytes()).hexdigest(),
                        "credit": "FreePats contributors; recorded FSBS electric guitar" if source == "freepats-guitar" else "Sam Gossner / Versilian Studios; Simon Dalzell / Ivy Audio; sample cutting Elan Hickler / Soundemote" if source == "vsco2ce" else "Sam Gossner / Versilian Studios and VCSL contributors"})
    manifest = {"schemaVersion": 1, "id": "vibera-recorded-acoustic-starter", "license": "CC0-1.0",
                "pathBase": "assets/synthesizer", "sources": sources, "instruments": instruments,
                "limitations": ["Curated partial key and articulation coverage; not complete upstream libraries.", "No invented loops; only guitar uses explicit source SFZ loops; other finite recordings may end during held notes.", "No round-robin, release-trigger, pedal or SFZ modulation equivalence.", "Source offsets/cents must be applied during offthread preparation; loop ends are exclusive.", "Original WAV/FLAC bytes retained; map key spans/dynamic simplification/headroom are VibeRa curation.", "FM electric piano is a recorded TX81Z FM-keyboard timbre, not a Rhodes or acoustic piano.", "Listening and full engine/runtime qualification remain separate."],
                "sampleCount": len(samples), "sampleBytes": sum(row["sourceBytes"] for row in samples.values())}
    (ROOT / "library.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (ROOT / "acquisition.json").write_text(json.dumps({"completed": True, "seconds": time.perf_counter() - started,
        "measurementScope": "Final complete verification/notice refresh invocation; initial acquisition failures are retained separately.",
        "sampleCount": len(samples), "sampleBytes": manifest["sampleBytes"], "librarySha256": hashlib.sha256((ROOT / "library.json").read_bytes()).hexdigest(),
        "checks": ["Every WAV/FLAC size and Git blob SHA1 matches the pinned upstream Git tree.", "Every source file has positive frames and supported mono/stereo layout metadata.", "Every region has SHA256 and exact source mapping/root/velocity/offset/loop provenance."], "preservedFailures": sorted(path.name for path in ROOT.glob("acquisition-attempt-*.json")), "listeningPerformed": False}, indent=2) + "\n", encoding="utf-8")
    print("Verified and retained", len(samples), "original WAV/FLAC files;", round(time.perf_counter() - started, 3), "seconds", flush=True)


if __name__ == "__main__":
    main()
