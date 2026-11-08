"""Licenses, notices and checksums acceptance (RQ-H).

Checks the payload users actually receive (the installer built from the qualified
distribution): every file is listed in provenance.json with a matching SHA-256 and the
source commit is recorded; the product license and the JUCE/Tracktion licenses ship; every
shipped third-party component found in the pinned vendor sources has a license or notice
file; every Synthesizer sample source has its license at the recorded hash and every
sample region names a declared source.

usage: python scripts/verify-notices.py [setup.exe]
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
setup = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/installer/VibeRa-Studio-Setup.exe").resolve()
output = ROOT / "build/module-qualification/release-quality/notices" / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
checks = []
# Bundled into the shipped binaries by the pinned JUCE build (test-only code excluded).
JUCE_COMPONENTS = {"flac": "FLAC", "oggvorbis": "Ogg-Vorbis", "harfbuzz": "HarfBuzz", "sheenbidi": "SheenBidi",
                   "jpeg": "JPEG", "png": "PNG", "zlib": "zlib", "vst3": "VST3-SDK"}
TEST_ONLY = {"doctest"}


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def sha(data):
    return hashlib.sha256(data).hexdigest()


passed = False
try:
    extracted = output / "extracted"
    subprocess.run([str(setup), "/C", f"/T:{extracted}", "/Q"], timeout=600)
    payload = zipfile.ZipFile(extracted / "payload.zip")
    files = {i.filename.replace("\\", "/"): i for i in payload.infolist() if not i.is_dir()}
    provenance = json.loads(payload.read("provenance.json"))
    check(re.fullmatch("[0-9a-f]{40}", provenance.get("sourceCommit", "")) is not None, "The exact source commit is recorded")
    check("SOURCE.txt" in files and provenance["sourceCommit"].encode() in payload.read("SOURCE.txt"),
          "SOURCE.txt names the same source commit")
    listed = {row["path"]: row["sha256"].lower() for row in provenance["files"]}
    unlisted = sorted(set(files) - set(listed) - {"provenance.json"})
    check(not unlisted, "Every shipped file has a checksum; unlisted: " + ", ".join(unlisted[:5]))
    wrong = sorted(p for p, h in listed.items() if p not in files or sha(payload.read(p)) != h)
    check(not wrong, "Every shipped file matches its recorded SHA-256; different/missing: " + ", ".join(wrong[:5]))

    licenses = {p.lower(): p for p in files if p.lower().startswith("licenses/")}
    check("LICENSE" in files and len(payload.read("LICENSE")) > 1000, "The product license ships")
    for required in ("licenses/gpl-3.0.txt", "licenses/juce-license.md", "licenses/tracktion-license.md"):
        check(required in licenses and len(payload.read(licenses[required])) > 200, "Ships " + required)
    names = " ".join(licenses).lower()
    vendor = next((ROOT / "vendor/source").glob("tracktion_engine-*")) / "modules/3rd_party"
    tracktion_components = sorted(p.name for p in vendor.iterdir() if p.is_dir() and p.name not in TEST_ONLY)
    missing = [c for c in tracktion_components if c.replace("_", "").lower() not in names.replace("_", "").replace("-", "")]
    check(not missing, "Every bundled Tracktion third-party component has a notice; missing: " + ", ".join(missing))
    missing = [label for label in JUCE_COMPONENTS.values() if label.lower() not in names]
    check(not missing, "Every bundled JUCE codec/graphics/SDK component has a license; missing: " + ", ".join(missing))

    library = json.loads(payload.read("Content/Synthesizer/library.json"))
    sources = {s["id"]: s for s in library["sources"]}
    for source in sources.values():
        path = "Content/Synthesizer/" + source["licensePath"]
        check(path in files and sha(payload.read(path)) == source["licenseSha256"].lower(),
              f"Sample source {source['name']} ships its {source['license']} license at the recorded hash")
    undeclared = sorted({r["sourceId"] for i in library["instruments"] for r in i.get("regions", [])} - set(sources))
    check(not undeclared, "Every sample region names a declared, licensed source; undeclared: " + ", ".join(undeclared))
    check("licenses/synthesizer_sound_sources.md" in licenses, "The sample credits page ships")
    passed = True
    receipt = dict(sourceCommit=provenance["sourceCommit"], shippedFiles=len(files), licenseFiles=len(licenses),
                   tracktionComponents=tracktion_components, sampleSources=sorted(sources))
finally:
    if "payload" in locals():
        payload.close()  # Windows keeps an open ZIP undeletable.
    shutil.rmtree(output / "extracted", ignore_errors=True)
    receipt = locals().get("receipt", {})
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    receipt["dependencyHashes"] = [dict(path="scripts/verify-notices.py", mode="content",
                                        sha256=content_identity(ROOT / "scripts/verify-notices.py")["contentSha256"])]
    receipt["candidateHashes"] = [dict(path=setup.relative_to(ROOT).as_posix(), sha256=sha(setup.read_bytes()))] if setup.is_file() else []
    receipt.update(passed=passed, checks=checks, scope="Installer payload; legal review of license compatibility is not claimed")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
