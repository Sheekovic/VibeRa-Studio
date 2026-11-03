# SPDX-License-Identifier: AGPL-3.0-or-later
"""Analyze retained fixed256/48000 sampler fixture spans; never qualifies a release."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path


def analyze(folder):
    phases = ["wrapperBeforeMs", "setupMs", "dispatchMs", "teardownMs", "wrapperAfterMs"]
    ticks = ["outerBeganQpc", "processBeganQpc", "dispatchBeganQpc", "dispatchEndedQpc",
             "processEndedQpc", "outerEndedQpc"]
    results = []
    for path in sorted(folder.glob("sampler-*-phases.csv")):
        data = path.read_bytes()
        rows = list(csv.DictReader(io.StringIO(data.decode("utf-8-sig"))))
        if not rows:
            raise ValueError("No timing rows: " + str(path))
        maxima = dict.fromkeys(["outerMs"] + phases, 0.0)
        outliers, count = [], 0
        previous_end = None
        for index, row in enumerate(rows):
            values = {key: float(row[key]) for key in maxima}
            raw = [int(row[key]) for key in ticks]
            if (int(row["callback"]) != index or any(not math.isfinite(x) or x < 0 for x in values.values())
                    or raw != sorted(raw) or (previous_end is not None and raw[0] < previous_end)
                    or abs(sum(values[key] for key in phases) - values["outerMs"]) > .000004):
                raise ValueError("Invalid/incomplete phase interval: " + str(path) + ":" + str(index))
            previous_end = raw[-1]
            for key, value in values.items():
                maxima[key] = max(maxima[key], value)
            # CSV is rounded to six decimal places; the original in-process
            # full-precision gate remains authoritative at threshold boundaries.
            if values["outerMs"] > 256000 / 48000:
                count += 1
                if len(outliers) < 128:
                    outliers.append({"callback": index, **values, "rawTicks": raw})
        results.append({"path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
                        "callbacks": len(rows), "maxima": maxima, "roundedDeadlineOutliers": count,
                        "outliers": outliers, "omittedOutliers": count - len(outliers)})
    if not results:
        raise ValueError("No sampler phase CSVs in the specified directory")
    return {"scope": "Retained CSV analysis only; not freshness, source identity, release, hardware or listening proof",
            "fixtureRate": 48000, "fixtureBuffer": 256, "deadlineMs": 256000 / 48000,
            "csvRoundingDigits": 6, "files": results,
            "callbacks": sum(result["callbacks"] for result in results),
            "roundedDeadlineOutliers": sum(result["roundedDeadlineOutliers"] for result in results)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = Path(__file__).resolve().parent.parent / "build"
    folder, output = args.folder.resolve(), args.output.resolve()
    if not folder.is_relative_to(build) or not output.is_relative_to(build):
        raise ValueError("Keep source receipts and output under this project's build directory")
    if output.exists():
        raise ValueError("Use a fresh output name; previous evidence is preserved")
    result = analyze(folder)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ["callbacks", "roundedDeadlineOutliers", "scope"]}))
    return int(result["roundedDeadlineOutliers"] > 0)


if __name__ == "__main__":
    raise SystemExit(main())
