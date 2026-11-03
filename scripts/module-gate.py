"""Check the active module ledger before an explicit release transaction.

Software checklist completeness is necessary, never proof of FL parity or heard
audio. Exact build/source/package checks remain separate mandatory gates.

Passed receipts bind nonempty dependencyHashes and candidateHashes lists. Each
entry names a workspace-relative path and SHA256. Dependency mode is "exact"
(default) or "content" for supported source-text suffixes, using dev_support LF
normalization. Candidate binary identities are always exact bytes. Declared
dependencies must cover actual test inputs; this gate cannot infer test coverage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

from dev_support import TEXT, content_identity

ROOT=Path(__file__).resolve().parents[1]

def workspace_file(root, value):
    if not isinstance(value, str) or not value or Path(value).is_absolute():
        raise ValueError('requires a workspace-relative file path')
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError('file outside workspace or absent')
    return path


def binding_failures(receipt, root):
    errors = []
    for group in ('dependencyHashes', 'candidateHashes'):
        bindings = receipt.get(group)
        if not isinstance(bindings, list) or not bindings:
            errors.append(group + ': nonempty identity bindings required')
            continue
        seen = set()
        for binding in bindings:
            if not isinstance(binding, dict):
                errors.append(group + ': malformed identity binding')
                continue
            value = binding.get('path')
            try:
                path = workspace_file(root, value)
                canonical = str(path).casefold()
                if canonical in seen:
                    raise ValueError('duplicate identity path')
                seen.add(canonical)
                expected = binding.get('sha256')
                if not isinstance(expected, str) or not re.fullmatch('[0-9a-fA-F]{64}', expected):
                    raise ValueError('valid SHA256 required')
                mode = binding.get('mode', 'exact')
                if group == 'candidateHashes' and (mode != 'exact' or 'contentSha256' in binding):
                    raise ValueError('candidate binary identity must be exact')
                if group == 'candidateHashes' and path.suffix.lower() not in ('.exe', '.dll'):
                    raise ValueError('candidate identity requires an executable or runtime binary')
                if mode not in ('exact', 'content'):
                    raise ValueError('identity mode must be exact or content')
                if mode == 'content' and path.suffix.lower() not in TEXT:
                    raise ValueError('content normalization requires a supported source-text file')
                actual = content_identity(path)['contentSha256' if mode == 'content' else 'sha256']
                if actual != expected.lower():
                    raise ValueError('stale ' + mode + ' identity')
            except (OSError, ValueError, TypeError) as error:
                errors.append(group + ' ' + str(value) + ': ' + str(error))
    return errors


def failures(ledger,root,phase='candidate'):
    errors=[]
    if ledger.get('format')!=1 or not ledger.get('requirements'):return ['Missing versioned module requirements']
    ids=set()
    for item in ledger['requirements']:
        identity=item.get('id','')
        if not identity or identity in ids:errors.append('Missing/duplicate requirement ID')
        ids.add(identity)
        if not item.get('releaseRequired',True):continue
        if item.get('phase')=='release'and phase in('candidate','patch'):continue
        if item.get('status')!='verified':errors.append(identity+': '+item.get('status','missing'));continue
        evidence=item.get('evidence',[])
        if not evidence:errors.append(identity+': no evidence');continue
        for proof in evidence:
            if not isinstance(proof,dict):errors.append(identity+': malformed evidence');continue
            try:
                path=workspace_file(root,proof.get('path'))
                data=path.read_bytes()
            except (OSError,ValueError,TypeError) as error:
                errors.append(identity+': evidence '+str(error));continue
            if hashlib.sha256(data).hexdigest()!=proof.get('sha256'):errors.append(identity+': stale evidence hash');continue
            try:receipt=json.loads(data)
            except (ValueError,UnicodeDecodeError):errors.append(identity+': evidence must be a structured passed receipt');continue
            if not isinstance(receipt,dict) or receipt.get('passed')is not True:
                errors.append(identity+': evidence did not pass');continue
            # A patch release keeps the rows proven by the last complete regression; it does not
            # re-bind them to the changed sources (milestone releases do).
            if phase!='patch':errors.extend(identity+': '+error for error in binding_failures(receipt,root))
    return errors

def self_test():
    with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='module-gate-test-')as folder:
        root=Path(folder);receipt=root/'proof.json'
        backend=root/'Mixer.cpp';backend.write_bytes(b'float gain = 0.8f;\n')
        app=root/'VibeRa Studio.exe';app.write_bytes(b'MZ original synthetic gate fixture\r\n')
        worker=root/'VibeRa Plugin Worker.exe';worker.write_bytes(b'MZ original worker fixture\r\n')
        body=dict(passed=True,
                  dependencyHashes=[dict(path='Mixer.cpp',mode='content',sha256=content_identity(backend)['contentSha256'])],
                  candidateHashes=[dict(path=p.name,sha256=content_identity(p)['sha256'])for p in (app,worker)])
        item=dict(id='test',releaseRequired=True,status='verified',evidence=[])
        ledger=dict(format=1,requirements=[item])
        def write_proof():
            receipt.write_text(json.dumps(body),encoding='utf-8')
            item['evidence']=[dict(path='proof.json',sha256=hashlib.sha256(receipt.read_bytes()).hexdigest())]
        write_proof();assert not failures(ledger,root)
        backend.write_bytes(b'float gain = 0.8f;\r\n');assert not failures(ledger,root)
        backend.write_bytes(b'float gain = 0.9f;\r\n');assert failures(ledger,root)
        backend.write_bytes(b'float gain = 0.8f;\n');assert not failures(ledger,root)
        app_original=app.read_bytes();app.write_bytes(app_original.replace(b'\r\n',b'\n'));assert failures(ledger,root)
        app.write_bytes(app_original);assert not failures(ledger,root)
        worker_original=worker.read_bytes();worker.write_bytes(worker_original+b'changed');assert failures(ledger,root)
        worker.write_bytes(worker_original);assert not failures(ledger,root)
        dependency=body['dependencyHashes'][0]
        dependency['mode']='exact';dependency['sha256']=content_identity(backend)['sha256'];write_proof()
        backend.write_bytes(b'float gain = 0.8f;\r\n');assert failures(ledger,root)
        backend.write_bytes(b'float gain = 0.8f;\n');assert not failures(ledger,root)
        for group in ('dependencyHashes','candidateHashes'):
            previous=body.pop(group);write_proof();assert failures(ledger,root)
            body[group]=[];write_proof();assert failures(ledger,root)
            body[group]=previous;write_proof();assert not failures(ledger,root)
            body[group].append(dict(previous[0]));write_proof();assert failures(ledger,root)
            body[group].pop();write_proof();assert not failures(ledger,root)
            body[group].append('malformed');write_proof();assert failures(ledger,root)
            body[group].pop();write_proof();assert not failures(ledger,root)
        candidate=body['candidateHashes'][0]
        candidate['mode']='content';write_proof();assert failures(ledger,root)
        candidate.pop('mode');write_proof()
        candidate_path=candidate['path'];candidate_hash=candidate['sha256']
        candidate.update(path='Mixer.cpp',sha256=content_identity(backend)['sha256'])
        write_proof();assert failures(ledger,root)
        candidate.update(path=candidate_path,sha256=candidate_hash);write_proof();assert not failures(ledger,root)
        dependency.update(path=app.name,mode='content',sha256=content_identity(app)['sha256'])
        write_proof();assert failures(ledger,root)
        dependency.update(path='../outside.cpp',mode='exact');write_proof();assert failures(ledger,root)
        dependency.update(path='Mixer.cpp',sha256='invalid');write_proof();assert failures(ledger,root)
        dependency['sha256']=content_identity(backend)['sha256'];write_proof();assert not failures(ledger,root)
        body['passed']=False;write_proof();assert failures(ledger,root)
        body['passed']=True;write_proof()
        item['status']='in_progress';assert failures(ledger,root)
        item['status']='verified';item['evidence']=[];assert failures(ledger,root)
        item['evidence']=[dict(path='proof.json',sha256='stale')];assert failures(ledger,root)
        item.update(phase='release',status='open');assert not failures(ledger,root,'candidate')and failures(ledger,root,'complete')
    print('PASS module gate: declared dependencies, explicit LF normalization, exact app/worker bytes, missing/duplicate/malformed/outside identities, failed/stale/open evidence and release phase')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--module',default='rack-pattern-sampler');p.add_argument('--phase',choices=['candidate','patch','complete'],default='candidate');p.add_argument('--self-test',action='store_true');args=p.parse_args()
    if args.self_test:self_test();return 0
    if not re.fullmatch('[a-z][a-z0-9-]*',args.module):p.error('Invalid module ID')
    ledger=json.loads((ROOT/'docs/modules'/(args.module.upper()+'.json')).read_text(encoding='utf-8'))
    errors=failures(ledger,ROOT,args.phase)
    print(json.dumps(dict(module=ledger['id'],passed=not errors,phase=args.phase,errors=errors),indent=2))
    return 1 if errors else 0

if __name__=='__main__':sys.exit(main())
