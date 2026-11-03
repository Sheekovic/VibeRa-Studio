from pathlib import Path
import argparse,hashlib,json,stat,subprocess,time,uuid

def main():
 ap=argparse.ArgumentParser(description='Disposable source-only physical callback patch guard checks; never builds or edits the dependency cache.')
 ap.add_argument('--repository',type=Path,default=Path(__file__).resolve().parent.parent)
 ap.add_argument('--bundle',type=Path)
 ap.add_argument('--original-root',type=Path,help='Optional raw original JUCE tree for isolated draft qualification; otherwise derive from recognized repository inputs.')
 ap.add_argument('--output-root',type=Path,required=True)
 a=ap.parse_args(); repo=a.repository.resolve(); bundle=(a.bundle or repo).resolve(); output=a.output_root.absolute()
 if any(p.is_symlink() or (getattr(p.lstat(),'st_file_attributes',0) & getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0)) for p in [output,*output.parents] if p.exists()):raise SystemExit('Output crosses a reparse point')
 output=output.resolve()
 if not output.is_relative_to(repo/'build'):raise SystemExit('Output must be a non-reparse owned path under repository/build')
 output.mkdir(parents=True,exist_ok=True); attempt=output/('attempt-'+uuid.uuid4().hex);attempt.mkdir()
 family='patches/juce-physical-callback'; script='scripts/apply-physical-callback-patch.ps1'; manifest=json.loads((bundle/family/'manifest.json').read_text()); dirname=manifest['sourceDirectory']; relbase='vendor/source/'+dirname
 copied=[script,family+'/manifest.json']+[e['patch'] for e in manifest['files']];targets=[relbase+'/'+e['path'] for e in manifest['files']]; reads=[relbase+'/'+e['path'] for e in manifest['vendorReadDependencies']]
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 def q(v):return "'"+str(v).replace("'","''")+"'"
 cases=['fresh','idempotent','mixed','verify-original','verify-postimage','verify-mixed','unknown-first','unknown-second','manifest-postimage','payload','lock-revision','raw-eol','target-path','payload-path','read-dependency','read-path','reparse-target','owned-lock','held-second-target']
 results=[]; started=time.perf_counter()
 for case in cases:
  d=attempt/case;d.mkdir()
  for f in copied:
   p=d/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((bundle/f).read_bytes())
  (d/'dependencies.lock.json').write_bytes((repo/'dependencies.lock.json').read_bytes())
  (d/'.gitattributes').write_text('patches/juce-physical-callback/*.patch -text\n',encoding='ascii')
  for f in targets+reads:
   p=d/f;p.parent.mkdir(parents=True,exist_ok=True)
   original=(a.original_root/f.removeprefix(relbase+'/')) if a.original_root and f in targets else repo/f
   p.write_bytes(original.read_bytes())
  subprocess.run(['git','init','-q',str(d)],check=True,capture_output=True)
  for entry,f in zip(manifest['files'],targets):
   p=d/f
   if sha(p)==entry['patchedSha256']:
    r=subprocess.run(['git','-c','core.longpaths=true','-c','core.autocrlf=false','apply','--reverse','--directory='+relbase,str(d/entry['patch'])],cwd=d,capture_output=True);assert r.returncode==0,r.stderr
   assert sha(p)==entry['originalSha256'],f
  if case in ['idempotent','mixed','verify-postimage','verify-mixed']:
   selected=targets if case in ['idempotent','verify-postimage'] else targets[:1]
   for entry,f in zip(manifest['files'],targets):
    if f in selected:
     r=subprocess.run(['git','-c','core.longpaths=true','-c','core.autocrlf=false','apply','--directory='+relbase,str(d/entry['patch'])],cwd=d,capture_output=True);assert r.returncode==0,r.stderr
     assert sha(d/f)==entry['patchedSha256']
  if case.startswith('unknown-'):
   p=d/targets[case=='unknown-second'];p.write_bytes(p.read_bytes()+b'// unknown input\r\n')
  if case=='manifest-postimage':
   p=d/family/'manifest.json';m=json.loads(p.read_text());m['files'][1]['patchedSha256']='0'*64;p.write_text(json.dumps(m))
  if case=='payload':
   p=d/manifest['files'][1]['patch'];p.write_bytes(p.read_bytes()+b'//tampered\r\n')
  if case=='lock-revision':
   p=d/'dependencies.lock.json';m=json.loads(p.read_text());next(x for x in m['dependencies'] if x['name']=='JUCE')['revision']='0'*40;p.write_text(json.dumps(m))
  if case=='raw-eol':
   p=d/targets[0];b=p.read_bytes();assert b'\r\n' in b;p.write_bytes(b.replace(b'\r\n',b'\n'));assert p.read_bytes()!=b
  if case in ['target-path','payload-path','read-path']:
   p=d/family/'manifest.json';m=json.loads(p.read_text());m['vendorReadDependencies' if case=='read-path' else 'files'][0]['path' if case!='payload-path' else 'patch']='../unapproved-file';p.write_text(json.dumps(m))
  if case=='read-dependency':
   p=d/reads[0];p.write_bytes(p.read_bytes()+b'// unknown native input\r\n')
  if case=='reparse-target':
   p=(d/targets[0]).parent;to=d/'junction-owned-target';p.rename(to)
   r=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command','New-Item -ItemType Junction -Path '+q(p)+' -Target '+q(to)+' | Out-Null'],capture_output=True,text=True);assert r.returncode==0,r.stderr
  if case=='owned-lock':
   p=d/'build/physical-callback-check/apply.lock';p.parent.mkdir(parents=True);p.write_bytes(b'other owner fixture')
  before={f:sha(d/f) for f in targets+reads}; args=['powershell','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(d/script)]
  if case.startswith('verify-'):args+=['-VerifyOnly']
  if case=='held-second-target':
   wrapper=d/'held-target.ps1';wrapper.write_text("$ErrorActionPreference='Stop'\n$held=[IO.FileStream]::new("+q(d/targets[1])+",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)\ntry {& "+q(d/script)+"; if ($?) {exit 0} else {exit 1}} finally {$held.Dispose()}\n",encoding='utf-8');args[-1]=str(wrapper)
  began=time.perf_counter();r=subprocess.run(args,cwd=d,capture_output=True,text=True,timeout=30);elapsed=time.perf_counter()-began
  after={f:sha(d/f) if (d/f).exists() else 'MISSING' for f in targets+reads};good=case in ['fresh','idempotent','mixed','verify-original','verify-postimage','verify-mixed'];expected=dict(before)
  if good and not case.startswith('verify-'):expected.update({f:e['patchedSha256'] for f,e in zip(targets,manifest['files'])})
  passed=(r.returncode==0)==good and after==expected
  if case=='owned-lock':passed=passed and (d/'build/physical-callback-check/apply.lock').read_bytes()==b'other owner fixture'
  else:passed=passed and not (d/'build/physical-callback-check/apply.lock').exists()
  (d/'guard.log').write_text(r.stdout+'\n'+r.stderr,encoding='utf-8');results.append({'case':case,'pass':passed,'exit':r.returncode,'elapsedSeconds':elapsed,'before':before,'after':after,'expected':expected})
  receipt={'kind':'isolated source guard matrix','passed':all(z['pass'] for z in results),'cases':results,'elapsedSeconds':time.perf_counter()-started,'liveVendorChanged':False,'buildOrRuntimeRun':False,'inputs':{f:sha(bundle/f) for f in copied}}
  (attempt/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(case,'PASS' if passed else 'FAIL',r.returncode,flush=True)
 print('RECEIPT',str(attempt/'receipt.json'),flush=True)
 return 0 if receipt['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
