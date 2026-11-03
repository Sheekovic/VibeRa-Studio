# SPDX-License-Identifier: AGPL-3.0-or-later
"""Retain all outcomes from ten original synthetic 3-arm instrument/context probes."""
import hashlib,json,pathlib,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2]
research=root/"build/coproducer-research/anticipation"
output=research/"benchmark-original-20261012"
output.mkdir(exist_ok=False)
programs=[0,32,25,128,48,80,0,32,25,128]
reports=[]
for index,program in enumerate(programs):
    name="benchmark-original-20261012/case-"+str(index)
    start=time.perf_counter()
    log=output/("case-"+str(index)+".log")
    with log.open("w",encoding="utf-8") as stream:
        result=subprocess.run([sys.executable,str(root/"scripts/coproducer/probe-anticipation.py"),"--output",name,"--program",str(program),"--seed",str(17+index),"--fixture",str(index)],stdout=stream,stderr=subprocess.STDOUT,timeout=240,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    path=research/name/"report.json"
    if result.returncode or not path.exists():raise RuntimeError("Research child failed; inspect "+str(log))
    report=json.loads(path.read_text(encoding="utf-8"));reports.append(report)
    valid=[arm for arm in report["arms"] if arm["valid"]]
    row={"case":index,"program":program,"validArms":len(valid),"seconds":time.perf_counter()-start}
    print(json.dumps(row),flush=True)
    (output/"reports.json").write_text(json.dumps(reports,indent=2),encoding="utf-8")
summary={"schema":1,"terminal":True,"cases":len(reports),"arms":sum(len(report["arms"]) for report in reports),"validArms":sum(arm["valid"] for report in reports for arm in report["arms"]),"scope":"Ten authored synthetic control/history probes, not user songs, a musical holdout or listening/quality qualification","modelRevision":reports[0]["modelRevision"],"sourceRevision":reports[0]["sourceRevision"],"programs":programs,"comparisons":[],"scriptSha256":hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()}
for report in reports:
    arms=report["arms"]
    summary["comparisons"].append({"program":report["program"],"seed":report["seed"],"valid":[arm["valid"] for arm in arms],"harmonyChanged":arms[0].get("eventHash")!=arms[1].get("eventHash") if arms[0]["valid"] and arms[1]["valid"] else None,"surroundingChanged":arms[1].get("eventHash")!=arms[2].get("eventHash") if arms[1]["valid"] and arms[2]["valid"] else None})
(output/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
print(json.dumps({key:value for key,value in summary.items() if key not in ("comparisons","scope","scriptSha256")}),flush=True)
