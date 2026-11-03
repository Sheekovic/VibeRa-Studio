"""Real groove capture/reuse, rest-preview recovery, history, persistence and WAV timing."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time
import wave
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
app,output=[Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True,exist_ok=True)
client_path=app.parent/'skills/vibera-studio/scripts/stdio_client.py'
if not client_path.exists():client_path=root/'skills/vibera-studio/scripts/stdio_client.py'
spec=importlib.util.spec_from_file_location('neutral',client_path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
receipt=dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
client=None;started=time.monotonic()
def check(ok,message):
    if not ok:raise AssertionError(message)
    receipt['checks']+=1
try:
    pulse=output/'original-pulse.wav'
    with wave.open(str(pulse),'wb') as wav:
        wav.setparams((1,2,48000,100,'NONE','not compressed'))
        wav.writeframes(b''.join(struct.pack('<h',int(10000*min(i/8,(99-i)/8,1))) for i in range(100)))
    client=module.VibeRaClient(app);data=client.data
    state=lambda:data('vibera_state')
    command=lambda name,**args:data('vibera_command',dict(command=name,args=args))
    groove=lambda action,**args:data('vibera_groove',dict(action=action,**args))
    pattern=state()['selectedPattern'];channel,other=[c['id'] for c in state()['channels'][:2]]
    notes=lambda:data('vibera_inspect',dict(entity='notes',pattern=pattern))['items']
    for target,pan in [(channel,-1),(other,1)]:
        command('channel.source',channel=target,text=str(pulse));command('channel.pan',channel=target,value=pan)
    for i in range(6):command('notes.add',pattern=pattern,channel=channel,beat=i*.25,length=.25,pitch=60,value=.8)
    command('notes.add',pattern=pattern,channel=other,beat=.1,length=.25,pitch=67,value=.6)
    command('clip.add',pattern=pattern,beat=0,length=4)
    original=notes();ids=[n['id'] for n in original if n['channel']==channel]
    revision=state()['revision']
    captured=groove('capture',pattern=pattern,channel=channel,cycleBeats=2,name='Captured rhythm')['groove']
    check(len(captured['points'])==6 and captured['cycleBeats']==2 and state()['revision']==revision,'Actual capture is read-only and targets current part')
    presets=groove('presets')['presets'];check(len(presets)==3 and all(x['format']=='VibeRa.Groove' for x in presets),'Discover original independent presets')
    template=dict(format='VibeRa.Groove',version=1,name='Reusable test groove',cycleBeats=1,
                  points=[dict(beat=b,length=l,velocity=v) for b,l,v in [(0,.2,.9),(.3125,.1,.5),(.5,.2,.9),(.8125,.1,.5)]])
    check(groove('validate',groove=template)['groove']==template,'Typed template validation/readback')
    contract=data('vibera_commands',dict(command='notes.quantize',includeSchemas=True))['operations'][0]
    check(contract['arguments']['properties']['groove']['properties']['points']['maxItems']==4096 and contract['arguments']['properties']['velocityStrength']['default']==0,'Authoritative timing/level schema')
    file=output/'reusable.vgroove';groove('save',groove=template,path=str(file),overwrite=True)
    check(json.loads(file.read_text())==template,'File exists with exact content before synchronous save completes')
    check(groove('load',path=str(file))['groove']==template,'Actual file reload')
    retained=file.read_bytes()
    failed=client.call('vibera_groove',dict(action='save',path=str(file),groove=captured),allow_failure=True)
    check(failed.get('isError') and failed['structuredContent']['error']['code'] and file.read_bytes()==retained,'Refused overwrite preserves existing template')
    bad=output/'invalid.vgroove';bad.write_text('{"format":"other","version":2}')
    failed=client.call('vibera_groove',dict(action='load',path=str(bad)),allow_failure=True)
    check(failed.get('isError') and notes()==original and state()['revision']==revision,'Malformed file fails atomically without project changes')
    oversized=output/'oversized.vgroove';oversized.write_bytes(b' '*1048577)
    check(client.call('vibera_groove',dict(action='load',path=str(oversized)),allow_failure=True).get('isError'),'Oversized import is refused')
    data('vibera_selection',dict(action='set',target='notes',ids=ids))
    params=dict(pattern=pattern,channel=channel,ids=ids,groove=template,text='duration',velocityStrength=1)
    lease=data('vibera_gesture',dict(action='begin',label='Groove preview',leaseMs=10000))['operation']
    def update(**changes):return data('vibera_gesture',dict(action='update',operation=lease,command='notes.quantize',args=dict(params,**changes)))
    check(update()['completionState']=='pending','Groove preview remains pending')
    played=notes();expected=[0,.3125,.5,.8125,1,1.3125]
    check([n['beat'] for n in played[:6]]==expected and played[-1]==original[-1],'Repeated groove timing and unchanged other Channel')
    check(all(abs(n['velocity']-([.9,.5][i%2]))<1.e-6 for i,n in enumerate(played[:6])),'Groove velocity is real canonical note data')
    check(groove('preview')['groove']==template and state()['quantizerGuide']['cycleBeats']==1,'Read actual active guide template')
    baseline=groove('capture',source='previewBaseline',pattern=pattern,channel=channel,cycleBeats=2,name='Original')['groove']
    check([p['beat'] for p in baseline['points']]==[0,.25,.5,.75,1,1.25],'Modal-equivalent capture reads original pre-preview score')
    resting=json.loads(json.dumps(template));resting['points'][1]['velocity']=0
    update(groove=resting)
    check(len(notes())==5 and state()['selectedNotes']==ids,'Rest preview removes repeated notes while retaining source selection')
    update(groove=resting,zeroVelocityDeletes=False,velocityStrength=0)
    check([n['id'] for n in notes()]==[n['id'] for n in original] and all(abs(n['velocity']-.8)<1.e-6 for n in notes()[:6]),'Turning rests off restores exact original IDs/order/levels')
    good=notes();before=state()['revision']
    malformed=dict(template,version=2)
    for invalid in [dict(velocityStrength=1.1),dict(zeroVelocityDeletes='yes'),dict(groove=malformed)]:
        refused=client.call('vibera_gesture',dict(action='update',operation=lease,command='notes.quantize',args=dict(params,**invalid)),allow_failure=True)
        check(refused.get('isError') and refused['structuredContent'].get('error') and notes()==good and state()['revision']==before,'Invalid template/level/flag retains last valid preview')
    data('vibera_gesture',dict(action='cancel',operation=lease))
    check(notes()==original and state()['selectedNotes']==ids and 'quantizerGuide' not in state(),'Cancel restores score/selection and clears guides')
    lease=data('vibera_gesture',dict(action='begin',label='Groove quantize',leaseMs=10000))['operation'];update()
    check(data('vibera_gesture',dict(action='commit',operation=lease))['completionState']=='completed','Real groove commit')
    quantized=notes();command('edit.undo');check(notes()==original,'One exact groove Undo')
    command('edit.redo');check(notes()==quantized,'Exact groove Redo')
    def render(name):
        path=output/name;job=data('vibera_project',dict(action='export',path=str(path),overwrite=True))
        check(job['completionState']=='pending','WAV request does not pretend acceptance is completion');client.wait_job(job)
        with wave.open(str(path),'rb') as wav:
            check((wav.getframerate(),wav.getnchannels(),wav.getsampwidth())==(48000,2,3),'Actual rendered WAV format');pcm=wav.readframes(wav.getnframes())
        left=[abs(int.from_bytes(pcm[i:i+3],'little',signed=True)) for i in range(0,len(pcm),6)]
        return [i for i,v in enumerate(left) if v>10 and (i==0 or left[i-1]<=10)],b''.join(pcm[i:i+3] for i in range(3,len(pcm),6))
    hits,right=render('groove.wav')
    check(len(hits)==6 and all(abs(x-y)<=3 for x,y in zip(hits,[0,7500,12000,19500,24000,31500])),'Actual PCM verifies repeated groove onsets')
    command('edit.undo');straight,right_original=render('original.wav')
    check(len(straight)==6 and all(abs(x-y)<=3 for x,y in zip(straight,[0,6000,12000,18000,24000,30000])) and right==right_original,'Original onsets and byte-identical unaffected Channel PCM')
    command('edit.redo')
    saved=output/'groove-project.vibra';client.wait_job(data('vibera_project',dict(action='save',path=str(saved),overwrite=True)))
    file.replace(output/'template-moved.vgroove')
    client.wait_job(data('vibera_project',dict(action='open',path=str(saved),discardChanges=True)))
    check(notes()==quantized,'Saved source score does not depend on original template file')
    reopened,reopened_right=render('reopened.wav');check(reopened==hits and reopened_right==right,'Reopened project exact rendered timing')
    command('notes.quantize',**dict(params,groove=resting))
    rest_hits,rest_right=render('rests.wav')
    check(len(rest_hits)==4 and all(abs(x-y)<=3 for x,y in zip(rest_hits,[0,12000,19500,24000])) and rest_right==right,'Actual WAV verifies zero-velocity rests remove repeated hits')
    command('edit.undo');check(notes()==quantized,'Rest application exact Undo')
    lease=data('vibera_gesture',dict(action='begin',leaseMs=250))['operation'];update(groove=resting);time.sleep(.45)
    check(data('vibera_gesture',dict(action='state',operation=lease))['completionState']=='cancelled' and notes()==quantized,'Real expired rest preview restores starting score')
    receipt.update(project=str(saved),straightFrames=straight,grooveFrames=hits,unaffectedRightExact=True)
    client.close();receipt.update(passed=True,exitCode=client.process.returncode)
except Exception as failure:
    receipt['error']=str(failure);raise
finally:
    if client and client.process.poll() is None:client.close()
    receipt.update(finished=True,elapsedSeconds=time.monotonic()-started)
    (output/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt))
