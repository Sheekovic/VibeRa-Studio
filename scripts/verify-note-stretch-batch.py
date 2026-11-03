"""Actual ordered stretch/move batch, exact history and failed-batch rollback.

Uses semantic commands, not pointer simulation. No native/listening/hardware claim.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time

root=Path(__file__).resolve().parents[1]
app,output=[Path(p).resolve()for p in sys.argv[1:3]]
output.mkdir(parents=True,exist_ok=True)
module_path=root/'skills/vibera-studio/scripts/stdio_client.py'
spec=importlib.util.spec_from_file_location('neutral_batch',module_path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
report=dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
client=None;started=time.monotonic()
def check(value,message):
    if not value:raise AssertionError(message)
    report['checks']+=1
try:
    client=module.VibeRaClient(app);d=client.data
    s=d('vibera_state');p=s['selectedPattern'];c=s['selectedChannel']
    cmd=lambda k,**a:d('vibera_command',dict(command=k,args=a))
    for beat in (1,2):cmd('notes.add',pattern=p,channel=c,beat=beat,length=.5,pitch=60,value=.7)
    notes=lambda:d('vibera_inspect',dict(entity='notes',pattern=p))['items']
    baseline=notes();ids=[n['id']for n in baseline]
    stretch=dict(command='notes.stretch',args=dict(pattern=p,channel=c,ids=ids,beat=1,value=2))
    move=dict(command='notes.move',args=dict(pattern=p,channel=c,ids=ids,beat=.25,length=0,pitch=0))
    d('vibera_batch',dict(commands=[stretch,move]));changed=notes()
    check([(n['beat'],n['length'])for n in changed]==[(1.25,1),(3.25,1)],'Ordered canonical stretch/move result')
    check([n['id']for n in changed]==ids,'Batch retains source identity')
    cmd('edit.undo');check(notes()==baseline,'Batch is one exact Undo')
    cmd('edit.redo');check(notes()==changed,'Exact batch Redo')
    cmd('edit.undo')
    invalid=dict(command='notes.move',args=dict(pattern=p,channel=c,ids=[999999],beat=.25,length=0,pitch=0))
    failed=client.call('vibera_batch',dict(commands=[stretch,invalid]),allow_failure=True)
    check(failed.get('isError')and notes()==baseline,'Failed second command atomically restores source')
    report['passed']=True
except Exception as error:report['error']=str(error)
finally:
    if client:
        try:client.close();report['exitCode']=client.process.returncode
        except Exception as error:report['passed']=False;report['closeError']=str(error)
    report['finished']=True;report['seconds']=time.monotonic()-started
    (output/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report))
sys.exit(0 if report['passed']and report.get('exitCode')==0 else 1)
