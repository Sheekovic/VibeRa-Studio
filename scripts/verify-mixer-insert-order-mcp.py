"""Actual semantic insert reorder/history/cancel/batch/arm/save/open regression.

ABS_EXECUTABLE ABS_OWNED_OUTPUT. No OS input, hardware, listening or native
plugin compatibility claim. Two deliberately missing descriptors retain original
unequal opaque states. Reuses the existing strict disposable-output helper.
"""
from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
import xml.etree.ElementTree as ET

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result

def main():
    if len(sys.argv)!=3:return 2
    if not Path(sys.argv[1]).is_absolute():return 2
    app=Path(sys.argv[1]).resolve(strict=True)
    helper=load('insert_order_owned_output',ROOT/'scripts/verify-mixer-effects-mcp.py')
    helper.OWNER={'schemaVersion':1,'owner':'VibeRa verify-mixer-insert-order-mcp'}
    helper.OWNER_FILE='.vibera-mixer-insert-order-mcp-owner.json'
    output,attempt=helper.prepare_output(sys.argv[2])
    checks=[];client=None;started=time.monotonic()
    report=dict(passed=False,finished=False,checks=checks,attemptDirectory=str(attempt),
        application=str(app),applicationSha256=helper.digest(app),
        scriptSha256=helper.digest(Path(__file__)),nativeInput=False,listening=False,hardware=False,
        audio=False,pluginCompatibility=False,fixture='Two original deliberately absent VST3 states')
    def check(ok,message):
        if not ok:raise AssertionError(message)
        checks.append(message)
    try:
        worker=app.parent/'VibeRa Plugin Worker.exe'
        check(app.is_file() and worker.is_file(),'Exact application and relative worker inputs exist')
        report['workerSha256']=helper.digest(worker)
        path=app.parent/'skills/vibera-studio/scripts/stdio_client.py'
        if not path.is_file():path=ROOT/'skills/vibera-studio/scripts/stdio_client.py'
        report['clientSha256']=helper.digest(path)
        client=load('insert_order_neutral_client',path).VibeRaClient(app)
        def call(tool,args=None,fail=False):
            result=client.call(tool,args,allow_failure=True)
            with (attempt/'rpc.jsonl').open('a',encoding='utf-8') as stream:
                stream.write(json.dumps(dict(tool=tool,args=args or {},result=result))+'\n')
            if result.get('isError') and not fail:raise AssertionError(result)
            return result
        data=lambda tool,args=None:call(tool,args)['structuredContent']
        def cmd(name,**args):
            result=data('vibera_command',dict(command=name,args=args))
            # Routing a chain can be a real asynchronous structural operation.
            # Observe its exact owner; never retry a mutation or equate admission
            # with completion. The neutral client's existing job budget is40s.
            deadline=time.monotonic()+40
            while result.get('completionState')=='pending' and time.monotonic()<deadline:
                time.sleep(.03)
                result=data('vibera_mixerchain',dict(action='job',operation=result['operation']))
            if result.get('completionState') in ('pending','failed','cancelled'):
                raise AssertionError('Actual command did not complete: '+str(result))
            return result
        rows=lambda:data('vibera_inspect',dict(entity='mixer',limit=129))['items']
        state=lambda:data('vibera_state')
        order=lambda:[r['index']for r in rows()]
        def musical():
            names=('index','name','gain','pan','muted','colour','effectsBypassed','effects','routes')
            return {r['index']:{key:r[key]for key in names}for r in rows()}
        for name in ('mixer.move','mixer.moveLeft','mixer.moveRight'):
            c=data('vibera_commands',dict(command=name,includeSchemas=True))['operations'][0]
            required={'mixer','mixerPosition'}if name=='mixer.move'else{'mixer'}
            check(c['supported']and c['macroAvailable']and set(c['arguments']['required'])==required and
                set(c['arguments']['properties'])==required and 'resultSchema'in c,'Typed command/result discovery '+name)
        base=attempt/'server-base.vibra'
        client.wait_job(data('vibera_project',dict(action='save',path=str(base),overwrite=False)))
        from dev_support import project_schema
        version=project_schema(ROOT)
        report['projectHeaderSha256']=helper.digest(ROOT/'src/Project.h')
        tree=ET.parse(base);document=tree.getroot()
        check(document.tag=='VIBERA'and int(document.attrib['version'])==version,'Genuine current-source saved schema')
        absent=attempt/'deliberately-absent.vst3';check(not absent.exists(),'Genuine absent plugin target')
        first=int(document.attrib['nextId']);document.set('nextId',str(first+2))
        for slot,target in enumerate((1,5)):
            insert=next(i for i in document.findall('./MIXER/INSERT')if int(i.attrib['index'])==target)
            descriptor=ET.Element('PLUGIN',dict(name='Original reorder missing '+str(slot),format='VST3',category='Fx',
                manufacturer='Original VibeRa fixture',version='1',file=str(absent),uniqueId=format(first+slot,'x'),
                isInstrument='0',fileTime='0',infoUpdateTime='0',numInputs='2',numOutputs='2',isShell='0',hasARAExtension='0',uid='0'))
            plugin=ET.SubElement(insert,'PLUGIN',dict(id=str(first+slot),name='Original missing order state '+str(slot),
                descriptor=ET.tostring(descriptor,encoding='unicode'),state='1.A.'if slot else'1...',instrument='0',
                bypass=str(slot),wet='0.75'if slot else'0.25'))
            ET.SubElement(plugin,'AUTOMATION_BINDINGS')
        fixture=attempt/'original-two-missing.vibra';tree.write(fixture,encoding='utf-8',xml_declaration=True)
        client.wait_job(data('vibera_project',dict(action='open',path=str(fixture),discardChanges=True)))
        # Open reports the document/file result; deferred audio projection has
        # its own public state. Wait before the ONE capture/routing admission.
        projection_started=time.monotonic();projection=state()
        while projection['pendingAudioUpdate'] and time.monotonic()-projection_started<40:
            time.sleep(.03);projection=state()
        check(not projection['pendingAudioUpdate'],'Actual reopened projection settled before one routing admission')
        report['projectionPreparationSeconds']=time.monotonic()-projection_started
        data('vibera_mixer',dict(action='select',insert=1))
        cmd('mixer.routeOnly',mixer=1,target=5)
        cmd('mixer.rename',mixer=1,text='Original named source')
        baseline,contents,selected=order(),musical(),state()['selectedInsert']
        moved=cmd('mixer.move',mixer=1,mixerPosition=4)
        expected=baseline.copy();expected.remove(1);expected.insert(4,1)
        check(moved['completionState']=='completed'and moved['completionScope']=='canonicalProjectOrder'and
            moved['changed']and order()==expected and musical()==contents and state()['selectedInsert']==selected,
            'Actual stable-ID move retains full exposed state and selected source')
        cmd('edit.undo');check(order()==baseline and musical()==contents,'One actual Undo restores exact original order/content')
        cmd('edit.redo');check(order()==expected and musical()==contents,'Actual Redo restores order/content')
        before=state();result=cmd('mixer.move',mixer=1,mixerPosition=4);after=state()
        check(not result['changed']and all(before[k]==after[k]for k in('revision','dirty','undoAvailable','redoAvailable')),
            'Same-row no-op preserves revision/dirty/history availability')
        for args in ({'mixer':1},{'mixer':0,'mixerPosition':2},{'mixer':999,'mixerPosition':2},
                     *({'mixer':1,'mixerPosition':x}for x in(-1,0,128,129,1.5,True)),
                     {'mixer':1,'mixerPosition':2,'lane':3}):
            prior=state();result=call('vibera_command',dict(command='mixer.move',args=args),True)
            check(result.get('isError')and result.get('structuredContent',{}).get('error',{}).get('code')and
                order()==expected and musical()==contents and state()['revision']==prior['revision'],
                'Invalid semantic order input is structured and atomic '+str(args))
        lease=data('vibera_gesture',dict(action='begin',domain='document',label='Original order cancellation'))
        data('vibera_gesture',dict(action='update',operation=lease['operation'],command='mixer.moveLeft',args=dict(mixer=1)))
        check(order()!=expected,'Owned semantic gesture actually changes order')
        cancelled=data('vibera_gesture',dict(action='cancel',operation=lease['operation']))
        check(cancelled['completionState']=='cancelled'and order()==expected and musical()==contents,'Actual gesture cancel restores complete baseline')
        steps=[dict(command='mixer.moveRight',args=dict(mixer=1)),dict(command='mixer.moveRight',args=dict(mixer=1))]
        data('vibera_batch',dict(commands=steps));check(order().index(1)==6,'Actual owned batch applies both order steps')
        cmd('edit.undo');check(order()==expected and musical()==contents,'One actual batch Undo restores original state')
        failed=call('vibera_batch',dict(commands=[steps[0],dict(command='mixer.move',args=dict(mixer=1,mixerPosition=77))]),True)
        check(failed.get('isError')and order()==expected and musical()==contents and not state()['gestureActive'],
            'Invalid later batch step rolls back earlier move and releases exact batch owner')
        cmd('transport.record');check(state()['recordArmed']and not state()['recordingTake'],'Actual software arm, no capture/hardware claim')
        cmd('mixer.moveLeft',mixer=1);check(state()['recordArmed']and not state()['recordingTake'],'Discrete order keeps recording arm intact')
        cmd('transport.record');cmd('edit.undo');check(order()==expected,'Disarm then exact order Undo')
        saved=attempt/'ordered-two-missing.vibra'
        client.wait_job(data('vibera_project',dict(action='save',path=str(saved),overwrite=False)))
        plugins=lambda path:{int(p.attrib['id']):dict(p.attrib)for p in ET.parse(path).getroot().iter('PLUGIN')}
        check(plugins(saved)==plugins(fixture),'Actual save preserves both unequal opaque descriptors/state/wet/bypass exactly')
        client.wait_job(data('vibera_project',dict(action='new',discardChanges=True)))
        client.wait_job(data('vibera_project',dict(action='open',path=str(saved),discardChanges=True)))
        check(order()==expected and musical()==contents,'Actual async New/Open restores order and linked state')
        report['passed']=True
    except Exception as error:report['error']=str(error)
    finally:
        if client:
            try:client.close();report['exitCode']=client.process.returncode
            except Exception as error:report['passed']=False;report['closeError']=str(error)
            (attempt/'stderr.txt').write_text(''.join(client.stderr),encoding='utf-8')
        report.update(finished=True,seconds=time.monotonic()-started)
        payload=json.dumps(report,indent=2)+'\n'
        (attempt/'verification.json').write_text(payload,encoding='utf-8')
        (output/'verification.json').write_text(payload,encoding='utf-8')
    print(json.dumps({k:report.get(k)for k in('passed','finished','seconds','error','attemptDirectory')}))
    return 0 if report['passed']and report.get('exitCode')==0 else 1

if __name__=='__main__':raise SystemExit(main())
