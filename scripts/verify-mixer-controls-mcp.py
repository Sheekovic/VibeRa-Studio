"""Actual named Mixer, pan/mute/solo, history, project and WAV workflows.

Semantic/component evidence is separate from native input, listening and hardware.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import wave

sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
app,output=map(lambda p:Path(p).resolve(),sys.argv[1:3]);output.mkdir(parents=True,exist_ok=True)
module_path=app.parent/'skills/vibera-studio/scripts/stdio_client.py'
if not module_path.exists():module_path=root/'skills/vibera-studio/scripts/stdio_client.py'
spec=importlib.util.spec_from_file_location('neutral_mixer_controls',module_path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
receipt=dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
client=None;started=time.monotonic()
def check(value,message):
    if not value:raise AssertionError(message)
    receipt['checks']+=1
try:
    client=module.VibeRaClient(app);d=client.data
    cmd=lambda name,**args:d('vibera_command',dict(command=name,args=args))
    rows=lambda:d('vibera_inspect',dict(entity='mixer'))['items']
    row=lambda index:next(m for m in rows()if m['index']==index)
    mask=lambda:{m['index']:m['muted']for m in rows()}
    state=lambda:d('vibera_state')
    error=lambda name,args:client.call(name,args,allow_failure=True).get('isError')is True
    contract=d('vibera_commands',dict(command='mixer.meterReset',includeSchemas=True))['operations'][0]
    check(contract['supported'] and set(contract['arguments']['properties'])=={'insert'} and contract['arguments']['required']==['insert'],'Typed non-document overload reset discovery')
    def reset_snapshot():
        current=state()
        return {key:current[key] for key in ('revision','dirty') if key in current}, [{key:value for key,value in item.items() if key!='meter'} for item in rows()]
    for args in (dict(action='resetOverload'),dict(action='resetOverload',insert=-1),dict(action='resetOverload',insert=999),dict(action='resetOverload',insert=1.5),dict(action='resetOverload',insert=True),dict(action='resetOverload',insert=0,extra=True)):
        before=reset_snapshot();check(error('vibera_mixer',args) and reset_snapshot()==before,'Invalid overload reset is atomic '+str(args))
    # Headless semantic admission can have a prepared but unpublished meter.
    # Actual overloaded PCM and component clicks are checked by mixer_controls_gui.
    before=reset_snapshot();result=client.call('vibera_mixer',dict(action='resetOverload',insert=1),allow_failure=True)
    if result.get('isError'):
        check(result.get('structuredContent',{}).get('error',{}).get('code')=='unavailable_target','Unprepared reset explicitly unavailable')
    else:
        reset=result['structuredContent']
        check(reset['completionState']=='completed' and reset['undoable'] is False and reset['clearedThroughBlock']>=0 and 'leftOverload' in reset['meter'],'Actual overload fence/result')
    check(reset_snapshot()==before,'Transient reset preserves project revision/dirty/model')
    for name,fields in [('mixer.rename',{'mixer','text'}),('mixer.colour',{'mixer','value'}),('mixer.pan',{'mixer','value'}),('mixer.mute',{'mixer','enabled'}),('mixer.solo',{'mixer','enabled','restore'}),('mixer.soloConnected',{'mixer','enabled','restore'}),('mixer.unsolo',{'mixer','restore'})]:
        contract=d('vibera_commands',dict(command=name,includeSchemas=True))['operations'][0]
        check(contract['supported']and set(contract['arguments']['properties'])==fields,'Meaningful typed '+name)
    contract=d('vibera_commands',dict(command='mixer.unmuteAll',includeSchemas=True))['operations'][0]
    check(contract['supported'] and contract['arguments']['properties']=={} and contract['arguments']['required']==[] and contract['arguments']['additionalProperties'] is False,'Global Unmute All has an explicit no-argument contract')
    for args in (dict(mixer=1),dict(enabled=False),dict(restore=True)):
        before=reset_snapshot();check(error('vibera_command',dict(command='mixer.unmuteAll',args=args)) and reset_snapshot()==before,'Global command rejects unrelated arguments atomically')
    for index in (0,2,7):cmd('mixer.mute',mixer=index,enabled=True)
    muted=mask();global_before=reset_snapshot()
    result=cmd('mixer.unmuteAll')
    check(result['completionState']=='completed' and not any(mask().values()) and d('vibera_mixer',dict(action='state'))['solo'] is None,'Global manual mask actually completes, including Master')
    cmd('edit.undo');check(mask()==muted and reset_snapshot()[1]==global_before[1],'One global Undo restores exact Mixer state')
    cmd('edit.redo');check(not any(mask().values()),'Global Redo restores every enabled track')
    before=state()['revision'];cmd('mixer.unmuteAll');check(state()['revision']==before,'Global no-op does not create document history')
    cmd('mixer.soloConnected',mixer=2);solo_before=d('vibera_mixer',dict(action='state'))['solo'];muted=mask()
    cmd('mixer.unmuteAll');check(not any(mask().values()) and d('vibera_mixer',dict(action='state'))['solo'] is None,'Global action clears active solo restoration')
    cmd('edit.undo');check(mask()==muted and d('vibera_mixer',dict(action='state'))['solo']==solo_before,'Global Undo restores original solo ownership and mask')
    cmd('edit.redo')
    global_path=output/'unmuted-all.vibra';client.wait_job(d('vibera_project',dict(action='save',path=str(global_path),overwrite=True)))
    cmd('mixer.mute',mixer=0,enabled=True)
    client.wait_job(d('vibera_project',dict(action='open',path=str(global_path),discardChanges=True)))
    check(not any(mask().values()) and d('vibera_mixer',dict(action='state'))['solo'] is None,'Global result persists through actual save/reopen')
    cmd('mixer.rename',mixer=3,text=' DRUM BUS ');cmd('mixer.colour',mixer=3,value=0xff32bda2)
    check(row(3)['name']=='DRUM BUS'and row(3)['colour']==0xff32bda2,'Named coloured bus readback')
    for name,args in [('mixer.rename',dict(mixer=1,text=' ')),('mixer.rename',dict(mixer=1,text='bad\nname')),('mixer.rename',dict(mixer=999,text='gone')),('mixer.colour',dict(mixer=1,value=0x80332211)),('mixer.pan',dict(mixer=1,value=1.00000001)),('mixer.pan',dict(mixer=1,value=-1.00000001)),('mixer.solo',dict(mixer=0)),('mixer.mute',dict(mixer=1,value=1))]:
        before=rows();check(error('vibera_command',dict(command=name,args=args))and rows()==before,'Invalid control input atomic '+name)
    cmd('mixer.routeOnly',mixer=1,target=3);cmd('mixer.route',mixer=2,target=3);cmd('mixer.route',mixer=2,target=4)
    cmd('mixer.mute',mixer=7,enabled=True);baseline=mask()
    cmd('mixer.solo',mixer=1)
    check(not row(0)['muted']and not row(1)['muted']and not row(3)['muted']and row(2)['muted'],'Standard solo keeps downstream output only')
    cmd('mixer.soloConnected',mixer=3)
    check(not row(1)['muted']and not row(2)['muted']and row(4)['muted']and row(6)['muted'],'Connected solo adds inputs without parallel branch/Master bridge')
    check(d('vibera_mixer',dict(action='state'))['solo']['mutedBefore']==[7],'Original mask survives target switching')
    saved=rows();path=output/'named-buses.vibra';client.wait_job(d('vibera_project',dict(action='save',path=str(path),overwrite=True)))
    cmd('mixer.unsolo');check(mask()==baseline,'Original mute baseline restored')
    cmd('edit.undo');check(rows()==saved,'Exact solo exit Undo');cmd('edit.redo');check(mask()==baseline,'Exact solo exit Redo')
    client.wait_job(d('vibera_project',dict(action='open',path=str(path),discardChanges=True)))
    check(rows()==saved,'Real project round trip includes solo baseline/name/colour')
    cmd('mixer.unsolo');check(mask()==baseline,'Restoration after reopen')
    cmd('mixer.solo',mixer=1);cmd('mixer.soloConnected',mixer=3);cmd('mixer.unsolo',restore=False)
    check(not any(mask().values()),'Optional unmute-all exit')
    cmd('mixer.solo',mixer=1);cmd('mixer.mute',mixer=8,enabled=True)
    check(d('vibera_mixer',dict(action='state'))['solo']is None,'Manual mute ends restore ownership')
    cmd('edit.undo');cmd('mixer.unsolo',restore=False)
    start=row(1)['pan'];lease=d('vibera_gesture',dict(action='begin',domain='mixerControl',sourceInsert=1));token=lease['operation']
    for pan in (-.6,-.3,.4):d('vibera_gesture',dict(action='update',operation=token,command='mixer.pan',args=dict(mixer=1,value=pan)))
    check(abs(row(1)['pan']-.4)<1e-6,'Live pan gesture applied')
    check(error('vibera_gesture',dict(action='update',operation=token,command='mixer.pan',args=dict(mixer=2,value=.5))),'Lease target isolation')
    check(error('vibera_gesture',dict(action='update',operation=token,command='mixer.mute',args=dict(mixer=1,enabled=True))),'Lease rejects unrelated control')
    check(d('vibera_gesture',dict(action='commit',operation=token))['completionState']=='completed','Actual terminal gesture completion')
    cmd('edit.undo');check(row(1)['pan']==start,'One Undo for full stroke');cmd('edit.redo');check(abs(row(1)['pan']-.4)<1e-6,'Exact stroke Redo')
    token=d('vibera_gesture',dict(action='begin',domain='mixerControl',sourceInsert=1))['operation'];d('vibera_gesture',dict(action='update',operation=token,command='mixer.pan',args=dict(mixer=1,value=-1)))
    check(d('vibera_gesture',dict(action='cancel',operation=token))['completionState']=='cancelled'and abs(row(1)['pan']-.4)<1e-6,'Cancel restores exact pan')
    token=d('vibera_gesture',dict(action='begin',domain='mixerControl',sourceInsert=1,leaseMs=250))['operation'];d('vibera_gesture',dict(action='update',operation=token,command='mixer.gain',args=dict(mixer=1,value=.2)));time.sleep(.45)
    check(d('vibera_gesture',dict(action='state'))['completionState']=='cancelled'and row(1)['gain']!=.2,'Real lease expiry restores gain')
    check('leftPeak'in row(1)['meter']and 'leftRms'in row(1)['meter']and 'stale'in row(1)['meter'],'Actual signal meter contract distinct from gain')
    s=state();pattern=s['selectedPattern'];channel=s['channels'][0]['id']
    cmd('channel.source',channel=channel,text='builtin:bass');cmd('channel.mixer',channel=channel,mixer=1);cmd('notes.add',pattern=pattern,channel=channel,beat=0,length=1,pitch=60,value=.7);cmd('clip.add',pattern=pattern,beat=0,length=4)
    def render(name):
        target=output/name;job=d('vibera_project',dict(action='export',path=str(target),overwrite=True));check(job['completionState']=='pending','Export admission is pending');client.wait_job(job)
        with wave.open(str(target),'rb')as wav:
            check(wav.getnchannels()==2 and wav.getsampwidth()==3,'Real stereo24 WAV');pcm=wav.readframes(wav.getnframes())
        values=[int.from_bytes(pcm[i:i+3],'little',signed=True)for i in range(0,len(pcm),3)];return max(map(abs,values[::2])),max(map(abs,values[1::2]))
    cmd('mixer.pan',mixer=1,value=-1);left,right=render('left.wav');check(left>0 and right<=1,'Real offline pan PCM')
    cmd('mixer.mute',mixer=1,enabled=True);left,right=render('muted.wav');check(left<=1 and right<=1,'Real offline muted bus PCM')
    cmd('edit.undo');left,right=render('restored.wav');check(left>0 and right<=1,'Real Undo restores routed export')
    cmd('mixer.mute',mixer=0,enabled=True);cmd('mixer.unmuteAll');left,right=render('globally-restored.wav');check(left>0 and right<=1,'Global command restores actual routed export after Master mute')
    receipt['passed']=True
except Exception as failure:receipt['error']=str(failure)
finally:
    if client:
        try:client.close();receipt['exitCode']=client.process.returncode
        except Exception as failure:receipt['passed']=False;receipt['closeError']=str(failure)
    receipt['finished']=True;receipt['seconds']=time.monotonic()-started;(output/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt));sys.exit(0 if receipt['passed']and receipt.get('exitCode')==0 else 1)
