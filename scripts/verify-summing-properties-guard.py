"""Disposable full strict-guard source matrix; no compile, provider or DSP run."""
from pathlib import Path
import argparse
import ast
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import stat
import subprocess
import sys
import time
import uuid

FAMILY='patches/tracktion-summing-properties-reuse'
TARGET='modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h'
# Defaults remain the independent reviewed Summing2-add/1-delete assertion.
EXPECTED_NUMSTAT=(2,1)
EXTRA_INPUTS=set()
sha=lambda data:hashlib.sha256(data).hexdigest()

def ordinary_owned(path,root):
    path=Path(os.path.abspath(path));root=Path(os.path.abspath(root))
    if path!=root and root not in path.parents:raise ValueError('Evidence must remain inside project build')
    for current in [path,*path.parents]:
        try:info=current.lstat()
        except FileNotFoundError:continue
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise ValueError('Evidence path crosses reparse point')
    return path

def parse_args(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle',type=Path,
                        help='Optional reviewed metadata bundle; defaults to this repository')
    parser.add_argument('--output-root',type=Path,required=True)
    return parser.parse_args(argv)

def _plan_source_stage(root,module,entry,target_relative,postimage,protected_root,validated_changes):
    """Reconstruct one reviewed stage ONLY in a disposable full guard mirror.

    Never apply a historical patch straight to an aggregate successor. Every
    known actual endpoint is reversed to the pinned earliest image, checking
    every intermediate hash, then replayed to the requested original/final.
    Actual vendor admission still belongs exclusively to the normal guard.
    """
    root=Path(root).absolute()
    protected=Path(protected_root or Path(__file__).absolute().parents[1]).absolute()
    if root.resolve()==protected.resolve() or Path(module.ROOT).absolute()!=root:
        raise ValueError('Source reconstruction requires a disposable guard mirror')
    changes=validated_changes
    items=[item for item in changes if item['rel']==target_relative]
    if len(items)!=1:
        raise ValueError('Disposable source reconstruction lacks exact target authority')
    item=items[0];chain=item['chain']
    requested=[index for index,row in enumerate(chain) if row['entry']==entry]
    if len(requested)!=1:
        raise ValueError('Disposable source reconstruction lacks exact reviewed stage authority')
    target=module.ordinary(root/target_relative)
    current=target.read_bytes();current_hash=sha(current)
    endpoints=[chain[0]['entry']['originalSha256']]+[row['entry']['patchedSha256'] for row in chain]
    if endpoints.count(current_hash)!=1:
        raise ValueError('Disposable source reconstruction refuses unknown/ambiguous vendor bytes')
    trace=[];reconstructed=current
    def transition(row,reverse):
        nonlocal reconstructed
        step=row['entry'];before=sha(reconstructed)
        expected_before=step['patchedSha256' if reverse else 'originalSha256']
        expected_after=step['originalSha256' if reverse else 'patchedSha256']
        if before!=expected_before:
            raise ValueError('Disposable source reconstruction stage preimage mismatch')
        payload=module.read(step['patch'])
        if sha(payload)!=step['patchSha256']:
            raise ValueError('Disposable source reconstruction payload identity mismatch')
        hunks=module.parse(payload,step['path'],step['addedLines'],step['deletedLines'])
        reconstructed=module.apply(hunks,reconstructed,reverse)
        if sha(reconstructed)!=expected_after:
            raise ValueError('Disposable source reconstruction stage postimage mismatch')
        trace.append(dict(patch=step['patch'],reverse=reverse,beforeSha256=before,
                          afterSha256=expected_after,payloadSha256=step['patchSha256']))
    for row in reversed(chain[:endpoints.index(current_hash)]):transition(row,True)
    stop=requested[0]+int(postimage)
    for row in chain[:stop]:transition(row,False)
    expected=entry['patchedSha256' if postimage else 'originalSha256']
    if sha(reconstructed)!=expected or target.read_bytes()!=current:
        raise ValueError('Disposable source reconstruction destination/source changed')
    receipt=dict(inputSha256=current_hash,predecessorSha256=sha(reconstructed),
                requestedImage='final' if postimage else 'original',
                aggregateFinalSha256=item['entry']['patchedSha256'],
                reconstructed=current!=reconstructed,fullPinnedPreflight=True,stages=trace)

    return target,current,reconstructed,receipt


def prepare_source_stages(root,module,requests,protected_root=None):
    """One disposable transaction: validate all inputs, plan all stages, then write.

    A batch does not cache validation across fixture mutations. Every call uses
    a fresh complete native/source/payload preflight, and every reverse/forward
    stage still checks exact raw identities. Never a production applier API.
    """
    root=Path(root).absolute()
    protected=Path(protected_root or Path(__file__).absolute().parents[1]).absolute()
    if root.resolve()==protected.resolve() or Path(module.ROOT).absolute()!=root:
        raise ValueError('Source reconstruction requires a disposable guard mirror')
    requests=list(requests)
    if not requests or len({request[1] for request in requests})!=len(requests):
        raise ValueError('Disposable stage transaction requires unique requested targets')
    changes=module.preflight()
    plans=[_plan_source_stage(root,module,entry,target,bool(postimage),protected,changes)
           for entry,target,postimage in requests]
    # Check EVERY actual vendor input before FIRST mirror write. The existing
    # full preflight already validated source/manifest/payload identities; sole
    # fixture ownership forbids concurrent metadata mutations throughout call.
    for item in changes:
        if module.read_target(item['rel'],item['chain'][0]['entry'].get('originalAbsent',False))!=item['before']:
            raise ValueError('Disposable source reconstruction input changed before batch write')
    for target,current,reconstructed,_ in plans:
        module.ordinary(target)
        if target.read_bytes()!=current:
            raise ValueError('Disposable source reconstruction target changed before batch write')
    for target,current,reconstructed,_ in plans:
        if reconstructed!=current:target.write_bytes(reconstructed)
    return [receipt for _,_,_,receipt in plans]


def prepare_source_stage(root,module,entry,target_relative,postimage=False,protected_root=None):
    return prepare_source_stages(root,module,[(entry,target_relative,postimage)],protected_root)[0]


def prepare_source_predecessor(root,module,entry,target_relative):
    return prepare_source_stage(root,module,entry,target_relative)


def prepare_source_seed(root,module,entry,target_relative):
    """Normalize OTHER known chains in the disposable seed, never the checkout.

    A later unrelated family can be pending when --bundle qualifies a successor.
    Target preparation keeps its exact independent predecessor. Only full pinned
    preflight-derived postimages may normalize peers before per-case snapshots.
    """
    preparation=prepare_source_predecessor(root,module,entry,target_relative)
    peers=[]
    for item in module.preflight():
        if item['rel']!=target_relative and item['pending']:
            module.ordinary(root/item['rel'],required=item['before'] is not None).write_bytes(item['final'])
            peers.append(item['rel'])
    preparation['otherReviewedTargetsFinalized']=peers
    return preparation


def main():
    args=parse_args()
    repo=Path(__file__).absolute().parents[1]
    # A frozen draft may live under build; the real integration script lives at
    # scripts/. Caller supplies actual repository explicitly in this case.
    if not (repo/'dependencies.lock.json').is_file():
        repo=Path.cwd().absolute()
    if not (repo/'dependencies.lock.json').is_file():raise ValueError('Run from actual project root')
    if not args.output_root.is_absolute():raise ValueError('Absolute owned output required')
    output=ordinary_owned(args.output_root,repo/'build')
    family_key=FAMILY.rsplit('/',1)[1]
    if FAMILY=='patches/tracktion-summing-properties-reuse':
        marker=output/'.vibera-summing-properties-guard-owner.json'
        owner=dict(schemaVersion=1,owner='VibeRa Summing property-reuse source guard matrix')
    else:
        marker=output/('.vibera-'+family_key+'-guard-owner.json')
        owner=dict(schemaVersion=1,owner='VibeRa '+family_key+' source guard matrix')
    if output.exists() and any(output.iterdir()):
        if not marker.is_file() or json.loads(marker.read_text())!=owner:
            raise ValueError('Populated unowned output preserved')
    output.mkdir(parents=True,exist_ok=True);marker.write_text(json.dumps(owner)+'\n')
    attempt=output/('attempt-'+uuid.uuid4().hex);attempt.mkdir()
    bundle=(args.bundle or repo).absolute()
    def input_file(rel):
        candidate=bundle/rel
        return candidate if candidate.is_file() else repo/rel
    guard_rel='scripts/guard-structural-graph.py'
    guard_raw=input_file(guard_rel).read_bytes()
    tree=ast.parse(guard_raw.decode())
    approved=ast.literal_eval(next(n.value for n in tree.body if isinstance(n,ast.Assign) and
        any(isinstance(t,ast.Name) and t.id=='APPROVED' for t in n.targets)))
    contracts=set(approved['requiredContractSha256'])|{f['path'] for f in approved['families']}|{approved['sourceBurstJuce']['path']}
    inputs={guard_rel,'dependencies.lock.json','scripts/dev_support.py',
            'scripts/verify-summing-properties-guard.py'}|contracts|EXTRA_INPUTS
    for rel in contracts:
        m=json.loads(input_file(rel).read_text())
        inputs.update(e['patch'] for e in m.get('files',[]) if 'patch' in e)
        dependencies=list(m.get('sourceDependencies',[]))
        if 'sourceDependency' in m:dependencies.append(m['sourceDependency'])
        inputs.update(e['path'] for e in dependencies)
    directory='vendor/source/'+approved['sourceDirectory']+'/'
    paths={directory+e['path'] for family in approved['families'] for e in family['files']}
    paths.update(directory+e['path'] for e in approved['sourceBurstFence']['files'])
    original_inputs={p:input_file(p).read_bytes() for p in sorted(inputs)}
    # Only the exact pinned new header may be absent; permanent preflight
    # below validates that authority before any disposable reconstruction.
    absent_paths={directory+e['path'] for family in approved['families'] for e in family['files'] if e.get('originalAbsent',False)}
    def native_read(path):
        try:return path.read_bytes()
        except FileNotFoundError:
            if path.relative_to(repo).as_posix() in absent_paths:return None
            raise
    native_inputs={p:native_read(repo/p) for p in sorted(paths)}
    live_before={p:sha(data) if data is not None else None for p,data in native_inputs.items()}
    manifest=json.loads(input_file(FAMILY+'/manifest.json').read_text())
    matching=[e for e in manifest['files'] if e['path']==TARGET]
    if len(matching)!=1:raise ValueError('Exactly one requested family target required')
    entry=matching[0];target=directory+TARGET
    if (entry['addedLines'],entry['deletedLines'])!=EXPECTED_NUMSTAT:
        raise ValueError('Independent reviewed family numstat changed')
    results=[];started=time.monotonic()
    report=dict(kind='isolated full strict structural guard source matrix',passed=False,cases=results,
        liveVendorChanged=False,buildOrRuntimeRun=False,provider=False,
        inputs={rel:sha(data) for rel,data in original_inputs.items()})
    def receipt():
        report['elapsedSeconds']=time.monotonic()-started
        report['liveVendorChanged']=any((sha(data) if (data:=native_read(repo/p)) is not None else None)!=value for p,value in live_before.items())
        (attempt/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    def mirror(case):
        root=attempt/case;root.mkdir()
        for rel,data in {**original_inputs,**native_inputs}.items():
            path=root/rel;path.parent.mkdir(parents=True,exist_ok=True)
            if data is not None:path.write_bytes(data)
        (root/'build').mkdir(exist_ok=True)
        spec=importlib.util.spec_from_file_location('vibera_summing_guard_'+case.replace('-','_'),root/guard_rel)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return root,module
    def run(module,verify):
        saved=sys.argv
        stream=io.StringIO()
        try:
            sys.argv=[guard_rel]+(['--verify-only'] if verify else [])
            with contextlib.redirect_stdout(stream):module.main()
            return True,stream.getvalue()
        except Exception as error:return False,stream.getvalue()+type(error).__name__+': '+str(error)
        finally:sys.argv=saved
    try:
        # Reconstruct once in an owned disposable seed. Current final and exact
        # predecessor checkouts are both supported; no authoring bundle needed.
        seed,seed_module=mirror('source-predecessor')
        preparation=prepare_source_seed(seed,seed_module,entry,target)
        native_inputs={p:(seed/p).read_bytes() for p in paths}
        report['sourcePreparation']=preparation
        results.append(dict(case='pinned-disposable-source-predecessor',passed=True,**preparation))
        receipt()
        cases=('fresh','idempotent','verify-predecessor','verify-final','unknown-target','unknown-peer',
               'payload','manifest-postimage','lock-revision','vendor-eol','reparse-target','reparse-payload')
        for case in cases:
            root,module=mirror(case);target_path=root/target
            junction=None
            if case in ('idempotent','verify-final'):
                prepared=module.preflight()
                item=next(i for i in prepared if i['entry']['path']==TARGET)
                target_path.write_bytes(item['final'])
            if case=='unknown-target':target_path.write_bytes(target_path.read_bytes()+b'// unknown\n')
            if case=='unknown-peer':
                peer=root/next(p for p in paths if p.endswith('/tracktion_Plugin.h'))
                peer.write_bytes(peer.read_bytes()+b'// unknown\n')
            if case=='payload':
                p=root/entry['patch'];p.write_bytes(p.read_bytes()+b'// tampered\n')
            if case=='manifest-postimage':
                p=root/FAMILY/'manifest.json';m=json.loads(p.read_text());m['files'][0]['patchedSha256']='0'*64
                p.write_text(json.dumps(m))
            if case=='lock-revision':
                p=root/'dependencies.lock.json';m=json.loads(p.read_text())
                next(d for d in m['dependencies'] if d['name']=='tracktion_engine')['revision']='0'*40
                p.write_text(json.dumps(m))
            if case=='vendor-eol':target_path.write_bytes(target_path.read_bytes().replace(b'\n',b'\r\n'))
            if case.startswith('reparse-'):
                p=target_path.parent if case=='reparse-target' else (root/entry['patch']).parent
                moved=root/('owned-junction-'+case);p.rename(moved)
                if os.name=='nt':
                    q=lambda value:"'"+str(value).replace("'","''")+"'"
                    result=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command',
                        'New-Item -ItemType Junction -Path '+q(p)+' -Target '+q(moved)+' | Out-Null'],
                        check=True,capture_output=True,text=True,timeout=10)
                else:p.symlink_to(moved,target_is_directory=True)
                junction=(p,moved)
            try:
                before={p:sha((root/p).read_bytes()) for p in paths}
                ok,log=run(module,case.startswith('verify-'))
                after={p:sha((root/p).read_bytes()) for p in paths}
            finally:
                if junction:
                    link,destination=junction
                    if (root not in link.absolute().parents or root not in destination.absolute().parents or
                            link.resolve()!=destination.resolve()):
                        raise RuntimeError('Owned junction cleanup identity refused')
                    info=link.lstat()
                    if not (stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400):
                        raise RuntimeError('Owned junction cleanup requires actual link')
                    # Remove ONLY the verified link, never traverse/delete its
                    # retained destination files or any ancestor recursively.
                    if os.name=='nt':os.rmdir(link)
                    else:link.unlink()
                    if link.exists() or not destination.is_dir():
                        raise RuntimeError('Owned junction cleanup did not preserve destination')
            good=case in ('fresh','idempotent','verify-predecessor','verify-final')
            expected=dict(before)
            if good and not case.startswith('verify-'):expected[target]=preparation['aggregateFinalSha256']
            passed=ok==good and after==expected
            (root/'guard.log').write_text(log)
            results.append(dict(case=case,passed=passed,accepted=ok,before=before,after=after,expected=expected,
                                ownedJunctionRemoved=bool(junction)))
            receipt()
            if not passed:raise AssertionError('Guard matrix case failed: '+case+' '+log)
        root,module=mirror('parser-contracts');raw=(root/entry['patch']).read_bytes()
        for case,payload,added,deleted in [
            ('wrong-header',raw.replace(('+++ b/'+TARGET).encode(),b'+++ b/other',1),*EXPECTED_NUMSTAT),
            ('wrong-numstat',raw,EXPECTED_NUMSTAT[0]+1,EXPECTED_NUMSTAT[1]),
            ('payload-crlf',raw.replace(b'\n',b'\r\n'),*EXPECTED_NUMSTAT)]:
            try:module.parse(payload,TARGET,added,deleted)
            except ValueError:passed=True
            else:passed=False
            results.append(dict(case=case,passed=passed));receipt()
            if not passed:raise AssertionError('Parser admitted '+case)
        hunks=module.parse(raw,TARGET,*EXPECTED_NUMSTAT)
        post=module.apply(hunks,native_inputs[target])
        reverse=module.apply(hunks,post,True)
        results.append(dict(case='independent-forward-reverse-postimage',passed=
            sha(post)==entry['patchedSha256'] and reverse==native_inputs[target]))
        receipt()
        report['passed']=all(c['passed'] for c in results) and not report['liveVendorChanged']
        receipt();print(json.dumps(dict(receipt=str(attempt/'receipt.json'),passed=report['passed'],cases=len(results))))
        return 0 if report['passed'] else 1
    except Exception as error:
        report['error']=str(error);receipt();raise

if __name__=='__main__':
    sys.dont_write_bytecode=True
    raise SystemExit(main())
