"""Independent byte/map/PCM audit; installed numpy and ffmpeg decode FLAC.

This checks source assets, not VibeRa playback, listening or release readiness.
Receipts default to build/windows; the input library is never modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import wave


class EnvironmentFailure(RuntimeError):
    pass


def validate(base):
    try:
        import numpy as np
    except ImportError as error:
        raise EnvironmentFailure("Installed NumPy required for independent PCM analysis: " + str(error)) from error
    started = time.perf_counter()
    manifest = json.loads((base / "library.json").read_text(encoding="utf-8"))
    errors, stats, sample_paths = [], [], set()
    frames_total, maximum_peak, loops = 0, 0.0, 0
    for instrument in manifest["instruments"]:
        covered = set()
        frames, peak = 0, 0.0
        mapping = (base / instrument["sourceMapping"]).read_bytes()
        if hashlib.sha256(mapping).hexdigest() != instrument["sourceMappingSha256"]:
            errors.append("Source map hash: " + instrument["id"])
        for region in instrument["regions"]:
            path = (base / region["sample"]).resolve()
            if not path.is_relative_to(base.resolve()):
                raise ValueError("Sample path escapes library")
            sample_paths.add(path)
            raw = path.read_bytes()
            if len(raw) != region["sourceBytes"] or hashlib.sha256(raw).hexdigest() != region["sha256"]:
                errors.append("Sample SHA256/size: " + region["sample"])
            if hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() != region["sourceGitBlobSha1"]:
                errors.append("Source Git identity: " + region["sample"])
            if region["format"] == "FLAC":
                decoder = shutil.which("ffmpeg")
                if not decoder:
                    raise EnvironmentFailure("Installed ffmpeg unavailable; FLAC decoding was not performed")
                decoded = subprocess.run([decoder, "-v", "error", "-i", str(path), "-f", "f32le", "-acodec", "pcm_f32le", "-"],
                                         capture_output=True, check=True, timeout=20,
                                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                data = np.frombuffer(decoded.stdout, dtype="<f4").astype(np.float64)
                if data.size != region["frames"] * region["channels"]:
                    errors.append("Independent FLAC decoded frame count: " + region["sample"])
            else:
                with wave.open(str(path), "rb") as reader:
                    expected = (region["sampleRate"], region["channels"], region["frames"], region["bitsPerSample"])
                    actual = (reader.getframerate(), reader.getnchannels(), reader.getnframes(), reader.getsampwidth() * 8)
                    if actual != expected:
                        errors.append("Independent WAV header: " + region["sample"])
                    pcm = reader.readframes(reader.getnframes())
                if region["bitsPerSample"] == 16:
                    data = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768
                elif region["bitsPerSample"] == 24:
                    values = np.frombuffer(pcm, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
                    values = values[:, 0] | (values[:, 1] << 8) | (values[:, 2] << 16)
                    data = np.where(values >= 8388608, values - 16777216, values).astype(np.float64) / 8388608
                else:
                    raise ValueError("Unexpected PCM width")
            if not data.size or not np.isfinite(data).all():
                errors.append("Invalid decoded PCM: " + region["sample"])
            scaled_peak = float(np.max(np.abs(data))) * 10 ** (region["gainDb"] / 20)
            peak, maximum_peak = max(peak, scaled_peak), max(maximum_peak, scaled_peak)
            frames += region["frames"]
            frames_total += region["frames"]
            offset = region["sourceOffsetFrames"]
            if not 0 <= offset < region["frames"]:
                errors.append("Offset outside source: " + region["sample"])
            loop = region["loop"]
            if loop["mode"] == "forward":
                loops += 1
                if not 0 <= loop["startFrame"] < loop["endFrame"] <= region["frames"] - offset:
                    errors.append("Loop outside prepared PCM: " + region["sample"])
                blocks = [block for block in mapping.decode("utf-8").split("<region>")[1:]
                          if region["sourcePath"].split("/")[-1] in block]
                if len(blocks) != 1:
                    errors.append("Ambiguous authored loop: " + region["sample"])
                else:
                    a = re.search(r"\bloop_start=(\d+)", blocks[0])
                    b = re.search(r"\bloop_end=(\d+)", blocks[0])
                    if not a or not b or int(a[1]) - offset != loop["startFrame"] or int(b[1]) + 1 - offset != loop["endFrame"]:
                        errors.append("Source loop conversion: " + region["sample"])
            elif loop["mode"] != "none":
                errors.append("Unsupported loop mode: " + region["sample"])
            for note in range(region["lowNote"], region["highNote"] + 1):
                for velocity in range(region["lowVelocity"], region["highVelocity"] + 1):
                    if (note, velocity) in covered:
                        errors.append("Overlapping zone: " + instrument["id"])
                    covered.add((note, velocity))
        low = min(r["lowNote"] for r in instrument["regions"])
        high = max(r["highNote"] for r in instrument["regions"])
        if instrument["family"] != "percussion" and len(covered) != (high - low + 1) * 128:
            errors.append("Melodic coverage gap: " + instrument["id"])
        stats.append({"id": instrument["id"], "regions": len(instrument["regions"]), "lowNote": low, "highNote": high,
                      "sourceFrames": frames, "decodedStereoFloatBytesUpperBound": frames * 8,
                      "largestSingleRegionPeakAfterGain": peak})
    for source in manifest["sources"]:
        if hashlib.sha256((base / source["licensePath"]).read_bytes()).hexdigest() != source["licenseSha256"]:
            errors.append("License hash: " + source["id"])
    actual = {path.resolve() for path in (base / "samples").rglob("*") if path.is_file()}
    if actual != sample_paths or len(sample_paths) != manifest["sampleCount"]:
        errors.append("Missing/unreferenced sample or count mismatch")
    report = {"passed": not errors, "errors": errors, "seconds": time.perf_counter() - started,
              "librarySha256": hashlib.sha256((base / "library.json").read_bytes()).hexdigest(),
              "sampleCount": len(sample_paths), "sampleBytes": sum(path.stat().st_size for path in sample_paths),
              "sourceFrames": frames_total, "decodedStereoFloatBytesUpperBound": frames_total * 8,
              "authoredForwardLoopRegions": loops, "maximumSingleRegionPeakAfterGain": maximum_peak, "instruments": stats,
              "checks": ["Pinned SHA256/Git blob identities and byte sizes", "Source mapping and license SHA256",
                         "Independent WAV headers/PCM and FLAC decoding/frame count", "Source offset/loop bounds and explicit authored loop conversion",
                         "Nonoverlapping note/velocity zones and contiguous declared melodic ranges", "No missing/unreferenced samples", "Measured PCM peak after declared gain"],
              "listeningPerformed": False, "enginePlaybackQualified": False}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source_root = Path(__file__).resolve().parent
    parser.add_argument("--library", type=Path, default=source_root / "library.json",
                        help="Input library.json or Content/Synthesizer directory; read only")
    parser.add_argument("--output", type=Path,
                        default=source_root.parents[1] / "build/windows/synthesizer-content/verification.json",
                        help="Receipt JSON outside the immutable input library")
    args = parser.parse_args()
    library = args.library.resolve()
    if library.is_dir():
        library /= "library.json"
    # The manifest name is stable inside installed Content/Synthesizer.
    if library.name != "library.json":
        parser.error("--library must name library.json or its containing directory")
    base = library.parent
    output = args.output.resolve()
    if output.is_relative_to(base):
        parser.error("--output must be outside the immutable input library directory")
    started = time.perf_counter()
    try:
        report = validate(base)
        report["status"] = "passed" if report["passed"] else "failed"
    except EnvironmentFailure as error:
        report = {"passed": False, "status": "environment_failure", "error": str(error),
                  "seconds": time.perf_counter() - started, "listeningPerformed": False,
                  "enginePlaybackQualified": False}
    except Exception as error:
        report = {"passed": False, "status": "failed", "error": type(error).__name__ + ": " + str(error),
                  "seconds": time.perf_counter() - started, "listeningPerformed": False,
                  "enginePlaybackQualified": False}
    report["inputLibrary"] = str(library)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "instruments"}, indent=2))
    return 0 if report["passed"] else 2 if report["status"] == "environment_failure" else 1


if __name__ == "__main__":
    raise SystemExit(main())
