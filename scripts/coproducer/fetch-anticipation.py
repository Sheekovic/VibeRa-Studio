# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fetch pinned, public research inputs. Does not upload projects or install globally."""
import hashlib, json, pathlib, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[2] / "build/coproducer-research/anticipation"
SOURCE = "af37397922665a0fb8d474d7988b0f3755a38d45"
MODEL = "fa800530aa1126dd6b58b38f3bdb8fcec9c9d4f5"
WEIGHTS = "45af5006d529a19af3654c729192c072f3aeb9e7e13e9eaf92c19bfd7c87e4d8"
ROOT.mkdir(parents=True, exist_ok=True)
manifest_path=ROOT/"provenance.json"
previous=json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else None
if previous and (previous["sourceRevision"]!=SOURCE or previous["modelRevision"]!=MODEL):raise RuntimeError("Existing research revision differs; choose a separate research installation")
previous_hashes={item["path"]:item["sha256"] for item in previous["files"]} if previous else {}

def digest(file):
    h=hashlib.sha256()
    with file.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):h.update(block)
    return h.hexdigest()
def fetch(url, relative, expected=None, size=None):
    expected=expected or previous_hashes.get(relative)
    target=ROOT/relative; target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        if (expected and digest(target)!=expected) or (size and target.stat().st_size!=size):raise RuntimeError("Existing pinned input differs: "+relative)
    else:
        partial=target.with_name(target.name+".partial")
        req=urllib.request.Request(url,headers={"User-Agent":"VibeRa-provider-research"})
        with urllib.request.urlopen(req,timeout=60) as response, partial.open("wb") as stream:
            for block in iter(lambda:response.read(1024*1024),b""):stream.write(block)
        if (expected and digest(partial)!=expected) or (size and partial.stat().st_size!=size):raise RuntimeError("Pinned input verification failed: "+relative)
        partial.replace(target)
    return {"path":relative,"sha256":digest(target),"bytes":target.stat().st_size,"source":url}
records=[]
for file in ["README.md","config.json","pytorch_model.bin"]:
    records.append(fetch(f"https://huggingface.co/stanford-crfm/music-small-800k/resolve/{MODEL}/{file}","model/"+file,WEIGHTS if file.endswith(".bin") else None,512433185 if file.endswith(".bin") else None))
# Download code/data-interface modules only; no upstream music examples or training data.
for file in ["LICENSE.txt","README.md","setup.py","requirements.txt","anticipation/__init__.py","anticipation/config.py","anticipation/vocab.py","anticipation/ops.py","anticipation/sample.py","anticipation/tokenize.py","anticipation/convert.py"]:
    records.append(fetch(f"https://raw.githubusercontent.com/jthickstun/anticipation/{SOURCE}/{file}","source/"+file))
(ROOT/"provenance.json").write_text(json.dumps({"sourceRevision":SOURCE,"modelRevision":MODEL,"codeLicense":"Apache-2.0","modelCardLicense":"Apache-2.0","distributionQualified":False,"purpose":"Local optional provider research, no training or private data","files":records},indent=2),encoding="utf-8")
print(json.dumps({"files":len(records),"bytes":sum(r["bytes"] for r in records),"weightsVerified":True}))
