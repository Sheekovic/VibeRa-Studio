"""Real semantic Swing workflow, persistence, history and rendered PCM onsets."""
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
spec = importlib.util.spec_from_file_location('neutral', client_path)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0, applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(), nativeInput=False, physicalMidi=False, listening=False)
started = time.monotonic()
client = None
def check(ok, message):
    if not ok: raise AssertionError(message)
    receipt['checks'] += 1
try:
    pulse = output/'original-pulse.wav'
    with wave.open(str(pulse),'wb') as wav:
        wav.setparams((1,2,48000,100,'NONE','not compressed'))
        wav.writeframes(b''.join(struct.pack('<h',int(10000*min(i/8,(99-i)/8,1))) for i in range(100)))
    client = module.VibeRaClient(app); data = client.data
    receipt['version'] = client.server['version']
    state = lambda: data('vibera_state')
    command = lambda name, **args: data('vibera_command',dict(command=name,args=args))
    pattern = state()['selectedPattern']
    kick,snare = [c['id'] for c in state()['channels'][:2]]
    notes = lambda: data('vibera_inspect',dict(entity='notes',pattern=pattern))['items']
    source = lambda: [{k:n[k] for k in ['id','channel','beat','length','pitch','velocity','step']} for n in notes()]
    for channel,pan in [(kick,-1),(snare,1)]:
        command('channel.source',channel=channel,text=str(pulse))
        command('channel.pan',channel=channel,value=pan)
    for i in range(4): command('steps.set',channel=kick,pattern=pattern,beat=i*.25,enabled=True)
    command('steps.set',channel=snare,pattern=pattern,beat=0,enabled=True)
    command('channel.swingMix',channel=snare,value=0)
    command('clip.add',pattern=pattern,beat=0,length=4)
    original = source()
    check(state()['swing']==0,'Old/new straight timing default')
    contract = data('vibera_commands',dict(command='project.swing',includeSchemas=True))['operations'][0]
    check(contract['supported'] and contract['arguments']['properties']['value']['maximum']==1,'Typed canonical Swing discovery')
    def render(name):
        path = output/name
        job = data('vibera_project',dict(action='export',path=str(path),overwrite=True))
        check(job['completionState']=='pending','Export acceptance is pending')
        client.wait_job(job)
        with wave.open(str(path),'rb') as wav:
            check((wav.getframerate(),wav.getnchannels(),wav.getsampwidth(),wav.getnframes())==(48000,2,3,96000),'Actual Song WAV format/duration')
            pcm = wav.readframes(wav.getnframes())
        left = [abs(int.from_bytes(pcm[i:i+3],'little',signed=True)) for i in range(0,len(pcm),6)]
        hits = [i for i,value in enumerate(left) if value>10 and (i==0 or left[i-1]<=10)]
        return hits,b''.join(pcm[i:i+3] for i in range(3,len(pcm),6))
    straight,right = render('swing-straight.wav')
    check(len(straight)==4 and all(abs(a-b)<=3 for a,b in zip(straight,[0,6000,12000,18000])),'Measured straight pulse onsets')
    command('project.swing',value=1)
    check(source()==original,'Global Swing retains canonical source notes')
    check([n['playbackBeat'] for n in notes() if n['channel']==kick]==[0,.375,.5,.875],'Actual effective onset readback')
    swung,right_swung = render('swing-full.wav')
    check(len(swung)==4 and all(abs(a-b)<=3 for a,b in zip(swung,[0,9000,12000,21000])),'Measured half-step maximum delay in actual WAV')
    check(right==right_swung,'Unswung Channel actual PCM is byte-identical')
    command('edit.undo');check(state()['swing']==0 and source()==original,'Global Swing exact Undo')
    command('edit.redo');check(state()['swing']==1,'Global Swing exact Redo')
    command('channel.truncateSwing',channel=kick,enabled=True)
    odd=[n for n in notes() if n['channel']==kick and n['beat']==.25][0]
    check(odd['length']==.25 and odd['playbackLength']==.125,'Non-destructive truncation readback')
    command('channel.truncateSwing',channel=kick,enabled=False)
    data('vibera_rack',dict(action='select',ids=[kick,snare],activeChannel=kick))
    command('rack.swingMix',value=.25)
    check(all(c['swingMix']==.25 for c in state()['channels'] if c['id'] in [kick,snare]),'Bulk default uses shared selection')
    command('edit.undo')
    check([c['swingMix'] for c in state()['channels'][:2]]==[1,0],'Bulk exact Undo')
    before=state()['revision']
    for name,args in [('project.swing',dict(value=1.1)),('rack.swingMix',dict(ids=[kick,kick],value=.5)),('rack.swingMix',dict(ids=[kick,999999],value=.5)),('channel.swingMix',dict(channel=kick,value='heavy'))]:
        result=client.call('vibera_command',dict(command=name,args=args),allow_failure=True)
        check(result.get('isError') and result['structuredContent'].get('error') and state()['revision']==before,'Invalid Swing inputs return structured atomic failure')
    command('transport.record')
    lease=data('vibera_gesture',dict(action='begin',domain='swing'))['operation']
    def update(name,value): return data('vibera_gesture',dict(action='update',operation=lease,command=name,args=dict(value=value)))
    check(update('project.swing',.5)['completionState']=='pending' and state()['recordArmed'],'Recording-safe semantic Swing preview is pending')
    update('channel.swingMix',.5)
    refused=client.call('vibera_gesture',dict(action='update',operation=lease,command='notes.delete',args={}),allow_failure=True)
    check(refused.get('isError') and refused['structuredContent']['error']['code']=='gesture_domain','Swing lease refuses unrelated document edits')
    check(data('vibera_gesture',dict(action='commit',operation=lease))['completionState']=='completed','Real Swing gesture commit')
    command('transport.record')
    quarter,right_quarter=render('swing-quarter.wav')
    check(len(quarter)==4 and all(abs(a-b)<=3 for a,b in zip(quarter,[0,6750,12000,18750])) and right==right_quarter,'Actual PCM verifies multiplication of global and Channel Swing')
    command('edit.undo');check(state()['swing']==1 and state()['channels'][0]['swingMix']==1,'One gesture Undo restores both parameters')
    lease=data('vibera_gesture',dict(action='begin',domain='swing',leaseMs=250))['operation']
    update('project.swing',.2);time.sleep(.45)
    check(data('vibera_gesture',dict(action='state',operation=lease))['completionState']=='cancelled' and state()['swing']==1,'Real Swing lease expiry restores starting project')
    path=output/'swing-beat.vibra'
    client.wait_job(data('vibera_project',dict(action='save',path=str(path),overwrite=True)))
    client.wait_job(data('vibera_project',dict(action='open',path=str(path),discardChanges=True)))
    check(state()['swing']==1 and source()==original,'Actual schema13 save/reopen retains timing and source IDs')
    reopened,right_reopened=render('swing-reopened.wav')
    check(reopened==swung and right_reopened==right,'Reopened project produces exact timing/unaffected PCM')
    blocked=output/'blocked.wav';blocked.mkdir(exist_ok=True)
    job=data('vibera_project',dict(action='export',path=str(blocked),overwrite=True))
    until=time.monotonic()+20
    while job['completionState']=='pending' and time.monotonic()<until:
        time.sleep(.03);job=client.call('vibera_project',dict(action='job',operation=job['operation']),allow_failure=True)['structuredContent']
    check(job['completionState']=='failed' and job.get('error'),'Actual terminal async failure, not accepted-as-completed')
    receipt.update(project=str(path),straightFrames=straight,swungFrames=swung,quarterFrames=quarter,unaffectedRightExact=True)
    client.close();receipt.update(passed=True,exitCode=client.process.returncode)
except Exception as failure:
    receipt['error']=str(failure);raise
finally:
    if client and client.process.poll() is None: client.close()
    receipt.update(finished=True,elapsedSeconds=time.monotonic()-started)
    (output/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps({key:receipt[key] for key in ['passed','checks','elapsedSeconds','applicationSha256']}))
