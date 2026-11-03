"""Actual semantic Quantizer baseline previews, history, persistence and WAV timing."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time
import wave
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / 'skills/vibera-studio/scripts/stdio_client.py'
if not client_path.exists(): client_path = root/'skills/vibera-studio/scripts/stdio_client.py'
spec = importlib.util.spec_from_file_location('neutral',client_path)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
receipt = dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
started = time.monotonic(); client = None
def check(ok,message):
    if not ok: raise AssertionError(message)
    receipt['checks'] += 1
try:
    pulse = output/'original-pulse.wav'
    with wave.open(str(pulse),'wb') as wav:
        wav.setparams((1,2,48000,100,'NONE','not compressed'))
        wav.writeframes(b''.join(struct.pack('<h',int(10000*min(i/8,(99-i)/8,1))) for i in range(100)))
    client = module.VibeRaClient(app); data = client.data
    state = lambda: data('vibera_state')
    command = lambda name, **args: data('vibera_command',dict(command=name,args=args))
    pattern = state()['selectedPattern']; channel,ghost = [c['id'] for c in state()['channels'][:2]]
    notes = lambda: data('vibera_inspect',dict(entity='notes',pattern=pattern))['items']
    for target,pan in [(channel,-1),(ghost,1)]:
        command('channel.source',channel=target,text=str(pulse)); command('channel.pan',channel=target,value=pan)
    for beat,length in [(.08,.34),(.63,.4)]:
        command('notes.add',pattern=pattern,channel=channel,beat=beat,length=length,pitch=60,value=.8)
    command('notes.add',pattern=pattern,channel=ghost,beat=.13,length=.28,pitch=67,value=.6)
    command('clip.add',pattern=pattern,beat=0,length=4)
    original = notes(); params = dict(pattern=pattern,channel=channel,text='duration',length=.25)
    contract = data('vibera_commands',dict(command='notes.quantize',includeSchemas=True))['operations'][0]
    check(contract['supported'] and contract['arguments']['properties']['strength']['maximum']==1,'Typed advanced contract and ranges')
    command('notes.quantizeStarts',**params)
    check(notes()[0]['beat']==0 and notes()[0]['length']==original[0]['length'],'Real quick-start semantic command')
    command('edit.undo');check(notes()==original,'Quick-start exact Undo')
    command('notes.quickQuantize',**params)
    check(notes()[0]['beat']==0 and notes()[0]['length']==.25,'Real quick start/duration command')
    command('edit.undo');check(notes()==original,'Quick timing exact Undo')
    command('notes.quantize',**dict(params,ids=[original[0]['id'],original[-1]['id']]))
    check(notes()[-1]['beat']==.25 and notes()[1]==original[1],'Real explicit editable-ghost selection')
    command('edit.undo');check(notes()==original,'Explicit selection exact Undo')
    lease=data('vibera_gesture',dict(action='begin',label='Quantizer',leaseMs=10000))['operation']
    def update(**changes):
        return data('vibera_gesture',dict(action='update',operation=lease,command='notes.quantize',args=dict(params,**changes)))
    check(update(strength=.5)['completionState']=='pending','Preview remains pending until Accept')
    check(abs(notes()[0]['beat']-.04)<1.e-9 and abs(notes()[0]['length']-.25)<1.e-9,'Independent start and duration strengths')
    update(strength=0,durationStrength=0)
    check(notes()==original,'Zero strengths restore exact original baseline')
    update(sensitivity=.1)
    check(notes()==original,'Sensitivity excludes distant notes')
    update(text='end')
    check(abs(notes()[0]['length']-.5)<1.e-9,'End-time mode quantizes absolute note end')
    update(text='leaveEnd')
    check(abs(notes()[0]['length']-.42)<1.e-9,'Leave end preserves original end')
    update(text='leaveDuration')
    check(notes()[0]['length']==original[0]['length'],'Leave duration preserves source length')
    update(); good=notes(); revision=state()['revision']
    for invalid in [dict(strength=-.1),dict(sensitivity=1.1),dict(durationStrength='all'),dict(text='unsupported'),dict(length=0),dict(ids=[999999]),dict(ids=[original[0]['id'],original[0]['id']])]:
        failed=client.call('vibera_gesture',dict(action='update',operation=lease,command='notes.quantize',args=dict(params,**invalid)),allow_failure=True)
        check(failed.get('isError') and failed['structuredContent'].get('error') and notes()==good and state()['revision']==revision,'Invalid preview is atomic with structured error')
    check(data('vibera_gesture',dict(action='cancel',operation=lease))['completionState']=='cancelled' and notes()==original,'Cancel restores exact score')
    lease=data('vibera_gesture',dict(action='begin',label='Quantizer',leaseMs=10000))['operation'];update()
    check(data('vibera_gesture',dict(action='commit',operation=lease))['completionState']=='completed','Accept is real completion')
    quantized=notes()
    check(quantized[-1]==original[-1],'Empty selection targets only current Channel')
    command('edit.undo');check(notes()==original,'One exact Undo after many previews')
    command('edit.redo');check(notes()==quantized,'Exact Redo')
    def render(name):
        path=output/name
        job=data('vibera_project',dict(action='export',path=str(path),overwrite=True))
        check(job['completionState']=='pending','WAV request pending before job completion');client.wait_job(job)
        with wave.open(str(path),'rb') as wav:
            check(wav.getframerate()==48000 and wav.getnchannels()==2 and wav.getsampwidth()==3,'Actual rendered WAV format')
            pcm=wav.readframes(wav.getnframes())
        left=[abs(int.from_bytes(pcm[i:i+3],'little',signed=True)) for i in range(0,len(pcm),6)]
        return [i for i,v in enumerate(left) if v>10 and (i==0 or left[i-1]<=10)],b''.join(pcm[i:i+3] for i in range(3,len(pcm),6))
    quantized_frames,right=render('quantized.wav')
    check(len(quantized_frames)==2 and all(abs(x-y)<=3 for x,y in zip(quantized_frames,[0,18000])),'Actual PCM quantized onsets')
    command('edit.undo');straight_frames,right_before=render('original.wav')
    check(len(straight_frames)==2 and all(abs(x-y)<=3 for x,y in zip(straight_frames,[1920,15120])) and right==right_before,'Original onsets and byte-identical unaffected Channel')
    command('edit.redo')
    path=output/'quantized.vibra'
    client.wait_job(data('vibera_project',dict(action='save',path=str(path),overwrite=True)))
    client.wait_job(data('vibera_project',dict(action='open',path=str(path),discardChanges=True)))
    check(notes()==quantized,'Save/reopen retains exact score IDs and state')
    reopened,right_reopened=render('reopened.wav');check(reopened==quantized_frames and right_reopened==right,'Reopened export exact')
    command('transport.record')
    refused=client.call('vibera_command',dict(command='notes.quantize',args=params),allow_failure=True)
    check(refused.get('isError') and state()['recordArmed'] and notes()==quantized,'Armed take refuses Quantizer without silently stopping recording')
    command('transport.record')
    lease=data('vibera_gesture',dict(action='begin',leaseMs=250))['operation'];update(strength=.5);time.sleep(.45)
    check(data('vibera_gesture',dict(action='state',operation=lease))['completionState']=='cancelled' and notes()==quantized,'Expired preview restores starting project')
    blocked=output/'blocked.wav';blocked.mkdir(exist_ok=True)
    job=data('vibera_project',dict(action='export',path=str(blocked),overwrite=True));until=time.monotonic()+20
    while job['completionState']=='pending' and time.monotonic()<until:
        time.sleep(.03);job=client.call('vibera_project',dict(action='job',operation=job['operation']),allow_failure=True)['structuredContent']
    check(job['completionState']=='failed' and job.get('error'),'Actual terminal async export failure')
    receipt.update(project=str(path),originalFrames=straight_frames,quantizedFrames=quantized_frames,unaffectedRightExact=True)
    client.close();receipt.update(passed=True,exitCode=client.process.returncode)
except Exception as error:
    receipt['error']=str(error);raise
finally:
    if client and client.process.poll() is None: client.close()
    receipt.update(finished=True,elapsedSeconds=time.monotonic()-started)
    (output/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt))
