# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bounded real-model conditioning probe using original synthetic note controls."""
import os
os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", USE_TF="0", USE_FLAX="0", HF_HUB_DISABLE_TELEMETRY="1")
import argparse, hashlib, json, pathlib, sys, time
parser=argparse.ArgumentParser();parser.add_argument("--output",default="probe");parser.add_argument("--program",type=int,default=32);parser.add_argument("--seed",type=int,default=17);parser.add_argument("--fixture",type=int,default=0);parser.add_argument("--cache",action="store_true");args=parser.parse_args()
if args.program not in (0,25,32,48,80,128):raise ValueError("Unsupported research instrument program")
root=pathlib.Path(__file__).resolve().parents[2]/"build/coproducer-research/anticipation"
provenance=json.loads((root/"provenance.json").read_text(encoding="utf-8"))
for item in provenance["files"]:
    h=hashlib.sha256()
    with (root/item["path"]).open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):h.update(block)
    if h.hexdigest()!=item["sha256"]:raise RuntimeError("Pinned input changed: "+item["path"])
sys.path.insert(0,str(root/"source"))
import torch, transformers
from transformers import AutoModelForCausalLM
from anticipation import sample, ops
from anticipation.config import TIME_RESOLUTION,MAX_DUR
from anticipation.vocab import DUR_OFFSET,NOTE_OFFSET,MAX_PITCH,MAX_NOTE,CONTROL_OFFSET,REST
from anticipation.convert import events_to_midi
output=root/args.output
output.mkdir(parents=True,exist_ok=False)
torch.set_num_threads(2);torch.set_num_interop_threads(1)
begun=time.perf_counter()
model=AutoModelForCausalLM.from_pretrained(str(root/"model"),local_files_only=True,trust_remote_code=False,weights_only=True,torch_dtype=torch.float32,attn_implementation="eager").eval()
load_seconds=time.perf_counter()-begun
def memory_usage():
    if os.name != "nt":return {}
    import ctypes
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_=[("cb",wintypes.DWORD),("faults",wintypes.DWORD)]+[(name,ctypes.c_size_t) for name in ("peakWorkingSet","workingSet","peakPaged","paged","peakNonPaged","nonPaged","pagefile","peakPagefile")]
    counters=Counters();counters.cb=ctypes.sizeof(counters)
    kernel=ctypes.WinDLL("kernel32",use_last_error=True);kernel.GetCurrentProcess.restype=wintypes.HANDLE
    get=ctypes.WinDLL("psapi",use_last_error=True).GetProcessMemoryInfo
    get.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD];get.restype=wintypes.BOOL
    if not get(kernel.GetCurrentProcess(),ctypes.byref(counters),counters.cb):raise ctypes.WinError(ctypes.get_last_error())
    return {"peakWorkingSetBytes":counters.peakWorkingSet,"peakPagefileBytes":counters.peakPagefile}
original_add=sample.add_token
class BoundedModel:
    device=model.device
    offset=0
    end=1200
    program=args.program
    calls=0
    deadline=0
    cached_tokens=None
    past=None
    cache_hits=0
    def __call__(self,tokens):
        self.calls+=1
        if self.calls>384 or time.perf_counter()>self.deadline:raise RuntimeError("Bounded inference budget exhausted")
        full=tokens[0].tolist()
        reuse=args.cache and self.cached_tokens is not None and len(full)>len(self.cached_tokens) and full[:len(self.cached_tokens)]==self.cached_tokens
        if reuse:
            result=model(tokens[:,len(self.cached_tokens):],past_key_values=self.past,use_cache=True)
            self.cache_hits+=1
        else:
            result=model(tokens,use_cache=args.cache)
        self.cached_tokens=full if args.cache else None
        self.past=result.past_key_values if args.cache else None
        logits=result.logits[0,-1]
        phase=(tokens.shape[1]-1)%3
        if phase==0:
            upper=self.end-self.offset
            if upper<0:raise RuntimeError("Invalid model time origin")
            logits[upper+1:DUR_OFFSET]=-float("inf")
        elif phase==1:
            onset=int(tokens[0,-1])+self.offset
            maximum=max(1,min(MAX_DUR-1,self.end-onset))
            logits[DUR_OFFSET+maximum+1:NOTE_OFFSET]=-float("inf")
        else:
            first=NOTE_OFFSET+self.program*MAX_PITCH
            logits[NOTE_OFFSET:first]=-float("inf")
            logits[first+MAX_PITCH:NOTE_OFFSET+MAX_NOTE]=-float("inf")
            if int(tokens[0,-1]) == DUR_OFFSET:
                logits[NOTE_OFFSET:NOTE_OFFSET+MAX_NOTE]=-float("inf") # duration-zero is a rest, not a note
            else:
                logits[REST]=-float("inf")
        return result
bounded=BoundedModel()
def add_token(adapter,z,tokens,*positional,**keywords):
    offset=ops.min_time(tokens[max(len(tokens)-1017,0):],seconds=False)
    if offset != adapter.offset: adapter.cached_tokens=None;adapter.past=None
    adapter.offset=offset
    return original_add(adapter,z,tokens,*positional,**keywords)
sample.add_token=add_token

def event(second,duration,program,pitch,control=False):
    values=[round(second*100),DUR_OFFSET+round(duration*100),NOTE_OFFSET+program*128+pitch]
    return [value+CONTROL_OFFSET for value in values] if control else values
harmony=sum((event(second,1,0,pitch,True) for second,pitches in [(10,[60,64,67]),(11,[65,69,72])] for pitch in pitches),[])
anchor=event(10.5,.3,args.program,36 if args.program==128 else (43 if args.program==32 else 60+args.fixture%7),True)
preceding=event(8.5,.5,args.program,36 if args.program==128 else (43 if args.program==32 else 60+args.fixture%7))+event(9.25,.5,args.program,42 if args.program==128 else (48 if args.program==32 else 64+args.fixture%7))
following=event(13,.5,25,67)
report={"modelRevision":provenance["modelRevision"],"sourceRevision":provenance["sourceRevision"],"backend":"CPU", "torch":torch.__version__,"transformers":transformers.__version__,"loadSeconds":load_seconds,"seed":args.seed,"program":args.program,"fixture":args.fixture,"kvCache":args.cache,"timeResolutionSeconds":.01,
 "scope":"Original synthetic conditioning/validity probe; no private input, listening, quality or production qualification", "controls":"Explicit harmony notes and target bass anchor; surrounding bass history/guitar future", "style":"unsupported", "velocity":"model vocabulary lacks velocity; MIDI converter default retained", "arms":[]}
for arm in (1,2,3):
    controls=[] if arm==1 else ops.sort(harmony+anchor)
    history=[] if arm!=3 else ops.sort(preceding+following)
    torch.manual_seed(args.seed);bounded.calls=0;bounded.cached_tokens=None;bounded.past=None;bounded.cache_hits=0;bounded.deadline=time.perf_counter()+90
    start=time.perf_counter()
    generated=[]
    try:
        generated=sample.generate(bounded,10,12,inputs=history,controls=controls,top_p=.98)
        notes=[]
        for onset,duration,note in zip(generated[0::3],generated[1::3],generated[2::3]):
            if note==REST:continue
            program,pitch=divmod(note-NOTE_OFFSET,128)
            if 1000<=onset<1200:
                if program!=args.program or duration-DUR_OFFSET<=0 or onset+duration-DUR_OFFSET>1200:raise RuntimeError("Generated event violates program/time constraints")
                notes.append({"startSeconds":(onset-1000)/100,"durationSeconds":(duration-DUR_OFFSET)/100,"program":program,"pitch":pitch})
        if not notes:raise RuntimeError("Model generated no target events")
        # Upstream combine preserves control token values; no source project is edited.
        combined=ops.combine(generated,controls)
        triples=list(zip(combined[0::3],combined[1::3],combined[2::3]))
        preserved=all(tuple(v-CONTROL_OFFSET for v in controls[i:i+3]) in triples for i in range(0,len(controls),3))
        if not preserved:raise RuntimeError("Combination failed to preserve supplied controls")
        events_to_midi(combined).save(str(output/("arm-"+str(arm)+".mid")))
        row={"arm":arm,"valid":True,"notes":notes,"generatedTokens":generated,"controls":controls,"inputs":history,"controlsPreserved":preserved,"calls":bounded.calls,"cacheHits":bounded.cache_hits,"seconds":time.perf_counter()-start,"eventHash":hashlib.sha256(json.dumps(notes,sort_keys=True).encode()).hexdigest()}
    except Exception as error:
        row={"arm":arm,"valid":False,"error":str(error),"calls":bounded.calls,"cacheHits":bounded.cache_hits,"seconds":time.perf_counter()-start,"controls":controls,"inputs":history,"generatedTokens":generated}
    row["memory"]=memory_usage()
    report["arms"].append(row)
    (output/"report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({key:value for key,value in row.items() if key not in ("notes","generatedTokens","controls","inputs")}),flush=True)
report["terminal"]=True
report["scriptSha256"]=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
(output/"report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
