"""Actual semantic bus/send/history/lease/project/WAV/error workflows.

No pointer simulation, native-input, listening or hardware claim.
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
app,output=[Path(p).resolve()for p in sys.argv[1:3]]
output.mkdir(parents=True,exist_ok=True)
module_path=app.parent/'skills/vibera-studio/scripts/stdio_client.py'
if not module_path.exists():module_path=root/'skills/vibera-studio/scripts/stdio_client.py'
spec=importlib.util.spec_from_file_location('neutral_mixer',module_path)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
receipt=dict(passed=False,finished=False,checks=0,applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),nativeInput=False,listening=False,hardware=False)
client=None;started=time.monotonic()
def check(value,message):
    if not value:raise AssertionError(message)
    receipt['checks']+=1
try:
    client=module.VibeRaClient(app);d=client.data
    cmd=lambda k,**a:d('vibera_command',dict(command=k,args=a))
    state=lambda:d('vibera_state')
    inserts=lambda:d('vibera_inspect',dict(entity='mixer'))['items']
    routes=lambda:{m['index']:m['routes']for m in inserts()}
    gain=lambda source,target:next(r['gain']for r in routes()[source]if r['target']==target)
    error=lambda tool,a:client.call(tool,a,allow_failure=True).get('isError')is True
    tools=client.request('tools/list',{})['tools']
    check('vibera_mixer'in[t['name']for t in tools],'Discover Mixer selection/state service')
    for command,fields in [('mixer.route',{'mixer','target','enabled'}),('mixer.routeOnly',{'mixer','target'}),('mixer.send',{'mixer','target','value'})]:
        contract=d('vibera_commands',dict(command=command,includeSchemas=True))['operations'][0]
        check(contract['supported']and set(contract['arguments']['properties'])==fields,'Meaningful typed '+command)
    before=state();selected=d('vibera_mixer',dict(action='select',insert=1))
    check(selected['selectedInsert']==1 and state()['selectedInsert']==1,'Shared source selection readback')
    check(selected['revision']==before['revision']and selected['projectIdentity']==before['projectIdentity'],'Selection does not edit document')
    check(error('vibera_mixer',dict(action='select',insert=999999))and state()['selectedInsert']==1,'Invalid source preserves selection')
    check(error('vibera_mixer',dict(action='state',insert=1)),'Reject inapplicable state fields')
    pattern=state()['selectedPattern'];channel=state()['channels'][0]['id']
    contract=d('vibera_commands',dict(command='channel.mixer',includeSchemas=True))['operations'][0]
    routing_schema=next(t for t in tools if t['name']=='vibera_mixer')['outputSchema']['properties']['routing']
    check(set(contract['arguments']['properties'])=={'channel','mixer'} and
          contract['arguments']['properties']['mixer']['maximum']==2147483647 and
          len(contract['resultSchema']['anyOf'])==3 and
          contract['resultSchema']['anyOf'][0]==routing_schema and
          contract['resultSchema']['anyOf'][1]==next(t for t in tools if t['name']=='vibera_mixerchain')['outputSchema'] and
          contract['boundedStructuralPolling']['tool']=='vibera_mixerchain' and
          contract['boundedStructuralPolling']['operationType']=='string' and contract['asynchronous'],
          'Actual stdio discovery retains numeric assignment receipt plus bounded structural and direct outcomes')
    original_channel=state()['channels'][0]['mixer']
    assigned=cmd('channel.mixer',channel=channel,mixer=2)
    check(assigned['completionState']=='completed' and assigned['documentCommitted'] and
          assigned['audioDisposition']=='deferredNoDevice' and assigned['completionScope']=='document' and
          not assigned['nativeCompletionKnown'] and not assigned['audioPosted'],
          'No-device assignment is actual document/history completion, never native audio proof')
    operation=assigned['operation']
    # These fields describe current Studio state, not the retained operation.
    # A timer may legitimately advance them between separate stdio requests.
    current_fields={'revision','currentProjectIdentity','currentRevision','pendingAudioUpdate','audioProjectionDeferred'}
    retained=lambda value:{k:v for k,v in value.items() if k not in current_fields}
    check(retained(d('vibera_mixer',dict(action='routingJob',operation=operation))['routing'])==retained(assigned),
          'Read the exact completed operation through the actual stdio transport')
    cancellation=client.call('vibera_mixer',dict(action='cancelRouting',operation=operation),allow_failure=True)
    check(cancellation.get('isError') is True and
          cancellation['structuredContent']['error']['code']=='routing.tooLate' and
          retained(d('vibera_mixer',dict(action='routingJob',operation=operation))['routing'])==retained(assigned),
          'Too-late cancellation is a failed request and preserves the original operation')
    cmd('edit.undo');check(state()['channels'][0]['mixer']==original_channel,'Canonical routing Undo through stdio')
    cmd('edit.redo');check(state()['channels'][0]['mixer']==2,'Canonical routing Redo through stdio')
    cmd('edit.undo')
    original=state()
    for invalid in (-1,.5,2147483648):
        rejected=client.call('vibera_command',dict(command='channel.mixer',args=dict(channel=channel,mixer=invalid)),allow_failure=True)
        check(rejected.get('isError') is True and
              rejected['structuredContent']['error']['code']=='invalid_arguments' and
              state()['revision']==original['revision'] and state()['channels']==original['channels'],
              'Typed invalid assignment preserves actual document/source/history revision')
    cmd('channel.mixer',channel=channel,mixer=1)
    cmd('channel.source',channel=channel,text='builtin:bass')
    cmd('channel.gain',channel=channel,value=.25)
    cmd('channel.sampler.loop',channel=channel,enabled=True)
    cmd('notes.add',pattern=pattern,channel=channel,beat=0,length=2,pitch=60,value=.7)
    for beat in (0,4):cmd('clip.add',pattern=pattern,beat=beat,length=4)
    notes=d('vibera_inspect',dict(entity='notes',pattern=pattern))['items']
    def render(name):
        path=output/name;job=d('vibera_project',dict(action='export',path=str(path),overwrite=True))
        check(job['completionState']=='pending','WAV is pending until the worker finishes');client.wait_job(job)
        with wave.open(str(path),'rb')as wav:
            check(wav.getnchannels()==2 and wav.getsampwidth()==3 and wav.getnframes()>0,'Actual stereo24 PCM')
            pcm=wav.readframes(wav.getnframes())
        return [int.from_bytes(pcm[i:i+3],'little',signed=True)for i in range(0,len(pcm),3)]
    direct=render('direct.wav')
    cmd('mixer.route',mixer=1,target=3)
    check({r['target']for r in routes()[1]}=={0,3},'New send preserves default Master path')
    baseline=routes();revision=state()['revision'];cmd('mixer.route',mixer=1,target=3)
    check(routes()==baseline and state()['revision']==revision,'Redundant enable is exact no-op')
    lease=d('vibera_gesture',dict(action='begin',domain='mixerSend',sourceInsert=1,targetInsert=3))
    token=lease['operation'];check(lease['completionState']=='pending','Real continuous level lease')
    check(error('vibera_mixer',dict(action='select',insert=2)),'Selection cannot steal a level gesture')
    for value in (.2,.3,.4):d('vibera_gesture',dict(action='update',operation=token,command='mixer.send',args=dict(mixer=1,target=3,value=value)))
    check(abs(gain(1,3)-.4)<1e-6,'Continuous changes actually applied')
    check(error('vibera_gesture',dict(action='update',operation=token,command='mixer.routeOnly',args=dict(mixer=1,target=2))),'Lease rejects topology command')
    check(error('vibera_gesture',dict(action='update',operation=token,command='mixer.send',args=dict(mixer=1,target=0,value=.5))),'Lease rejects another destination')
    check(error('vibera_gesture',dict(action='commit',operation='foreign')),'Foreign token rejected')
    done=d('vibera_gesture',dict(action='commit',operation=token));check(done['completionState']=='completed','Commit reports actual completion')
    changed=routes();cmd('edit.undo');check(routes()==baseline,'One Undo restores the whole level stroke')
    cmd('edit.redo');check(routes()==changed,'Exact level Redo')
    token=d('vibera_gesture',dict(action='begin',domain='mixerSend',sourceInsert=1,targetInsert=3))['operation']
    d('vibera_gesture',dict(action='update',operation=token,command='mixer.send',args=dict(mixer=1,target=3,value=.8)))
    check(d('vibera_gesture',dict(action='cancel',operation=token))['completionState']=='cancelled'and routes()==changed,'Explicit cancel restores baseline')
    token=d('vibera_gesture',dict(action='begin',domain='mixerSend',sourceInsert=1,targetInsert=3,leaseMs=250))['operation']
    d('vibera_gesture',dict(action='update',operation=token,command='mixer.send',args=dict(mixer=1,target=3,value=.8)))
    time.sleep(.45)
    check(d('vibera_gesture',dict(action='state'))['completionState']=='cancelled'and routes()==changed,'Actual lease expiry restores source')
    for command,args in [('mixer.route',dict(mixer=3,target=1)),('mixer.route',dict(mixer=1,target=1)),('mixer.route',dict(mixer=0,target=1)),('mixer.route',dict(mixer=1,target=999999)),('mixer.send',dict(mixer=1,target=4,value=.5)),('mixer.send',dict(mixer=1,target=3,value=2.01)),('mixer.send',dict(mixer=1,target=3,value=-.1)),('mixer.routeOnly',dict(mixer=1,target=3,enabled=True))]:
        check(error('vibera_command',dict(command=command,args=args))and routes()==changed,'Atomic routing/meaningful-input rejection')
    cmd('mixer.send',mixer=1,target=3,value=.5);cmd('mixer.routeOnly',mixer=1,target=3)
    check([r['target']for r in routes()[1]]==[3],'Route only removes Master')
    bus=render('half-bus.wav');cmd('mixer.route',mixer=1,target=0);parallel=render('parallel.wav')
    check(len(direct)==len(bus)==len(parallel)and max(map(abs,direct))>0,'Routing retains actual musical length/content')
    check(max(abs(a/2-b)for a,b in zip(direct,bus))<=6,'Actual half-level bus PCM')
    check(max(abs(a+b-c)for a,b,c in zip(direct,bus,parallel))<=6,'Actual parallel bus sum alignment')
    check(d('vibera_inspect',dict(entity='notes',pattern=pattern))['items']==notes,'Routing never edits Pattern notes')
    saved=routes();path=output/'shared-bus.vibra'
    job=d('vibera_project',dict(action='save',path=str(path),overwrite=True));check(job['completionState']=='pending','Save admission distinct from completion');client.wait_job(job)
    cmd('mixer.routeOnly',mixer=1,target=2)
    check(error('vibera_project',dict(action='open',path=str(path))),'Unsaved route edits are protected before open')
    client.wait_job(d('vibera_project',dict(action='open',path=str(path),discardChanges=True)))
    check(routes()==saved and path.exists(),'Actual save/reopen restores routes and levels')
    commands=[dict(command='mixer.routeOnly',args=dict(mixer=1,target=2)),dict(command='mixer.route',args=dict(mixer=2,target=3)),dict(command='mixer.send',args=dict(mixer=2,target=3,value=.25))]
    d('vibera_batch',dict(commands=commands));batched=routes();cmd('edit.undo');check(routes()==saved,'Routing batch is one exact Undo');cmd('edit.redo');check(routes()==batched,'Routing batch exact Redo');cmd('edit.undo')
    failed=commands[:1]+[dict(command='mixer.routeOnly',args=dict(mixer=2,target=1))]
    check(error('vibera_batch',dict(commands=failed))and routes()==saved,'Cycle in second batch command rolls back the whole batch')
    blocked=output/'blocked-parent';blocked.write_text('Owned failure fixture')
    terminal=client.call('vibera_project',dict(action='exportMidi',path=str(blocked/'score.mid'),overwrite=True),allow_failure=True)['structuredContent']
    check(terminal['completionState']=='pending','Real failed MIDI job is accepted as pending first')
    if terminal['completionState']=='pending':
        deadline=time.monotonic()+30
        while terminal['completionState']=='pending'and time.monotonic()<deadline:
            time.sleep(.03);terminal=client.call('vibera_project',dict(action='job',operation=terminal['operation']),allow_failure=True)['structuredContent']
    check(terminal['completionState']=='failed','Actual asynchronous MIDI export failure remains failed')
    receipt['passed']=True
except Exception as failure:receipt['error']=str(failure)
finally:
    if client:
        try:client.close();receipt['exitCode']=client.process.returncode
        except Exception as failure:receipt['passed']=False;receipt['closeError']=str(failure)
    receipt['finished']=True;receipt['seconds']=time.monotonic()-started
    (output/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt))
sys.exit(0 if receipt['passed']and receipt.get('exitCode')==0 else 1)
