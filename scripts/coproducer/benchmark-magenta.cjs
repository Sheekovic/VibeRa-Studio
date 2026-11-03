// SPDX-License-Identifier: AGPL-3.0-or-later
// Original synthetic excerpts, not private/user projects or a musical-quality claim.
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { createHash } = require('node:crypto');
const installation = path.resolve(process.argv[2] ?? 'build/coproducer-research/magenta');
const output = path.resolve(process.argv[3] ?? path.join(installation, 'benchmark-' + Date.now()));
fs.mkdirSync(output, { recursive: true });
if (fs.existsSync(path.join(output, 'excerpts.json'))) throw new Error('Choose a fresh benchmark directory; existing evidence is preserved');
// Explicitly authored phrases: distinct registers, rests, rhythms, triads and tempi.
const excerpts = [
  ['rising-keys', 'keys', 96, [0,4,7], [[0,.5,60],[.75,.25,64],[1,.5,67],[2,1,72],[3.25,.5,67]]],
  ['minor-keys', 'keys', 72, [9,0,4], [[0,1,57],[1.5,.5,60],[2,.75,64],[3,.5,60]]],
  ['spaced-guitar', 'guitar', 108, [2,6,9], [[0,.5,62],[1,.25,66],[1.75,.5,69],[3,.75,66]]],
  ['answer-guitar', 'guitar', 132, [7,11,2], [[.5,.5,67],[1.25,.25,71],[2,.25,74],[2.5,.5,71],[3.25,.25,67]]],
  ['pulse-bass', 'bass', 120, [0,4,7], [[0,.75,36],[1,.5,43],[2,.75,36],[3,.5,40]]],
  ['resting-bass', 'bass', 84, [2,5,9], [[.25,1,38],[2,.5,45],[3,.5,41]]],
  ['held-strings', 'strings', 64, [5,9,0], [[0,1.5,65],[2,1.75,69]]],
  ['syncopated-synth', 'synth', 144, [4,7,11], [[.25,.25,64],[.75,.5,71],[1.5,.25,67],[2.25,.5,76],[3.5,.25,71]]],
  ['falling-keys', 'keys', 112, [10,2,5], [[0,.5,77],[.5,.5,74],[1.5,.75,70],[2.5,.5,65],[3.25,.5,62]]],
  ['minor-guitar', 'guitar', 88, [7,10,2], [[0,.75,55],[1,.5,62],[2.25,.5,58],[3,.75,62]]]
].map(([id,family,tempo,pitches,phrase]) => ({ id,family,tempo,pitches,phrase,
  provenance: 'Authored in this repository as a synthetic evaluation excerpt; no training or user material',
  summary: { section: 'four-quarter-note test phrase', repeatedMotif: id, energy: 'unknown' } }));
fs.writeFileSync(path.join(output, 'excerpts.json'), JSON.stringify(excerpts, null, 2));
const hash = value => createHash('sha256').update(JSON.stringify(value)).digest('hex');
const manifest = { schema:1, scope:'Magenta MusicVAE input-consumption and symbolic smoke only',
  sourceSha256:createHash('sha256').update(fs.readFileSync(__filename)).digest('hex'),
  modelWorkerSha256:createHash('sha256').update(fs.readFileSync(path.join(__dirname,'model-worker.cjs'))).digest('hex'),
  excerptsSha256:hash(excerpts), candidatesPerArm:2, seed:17,
  musicalReview:'pending', userAcceptance:'not measured', style:'unsupported', drums:'unsupported',
  arms:['instrument only (style unsupported)','harmony plus target notes','same plus surroundings and song summary'],
  consumption:'This adapter consumes supplied whole-bar harmony, but new-idea mode ignores target notes, surroundings and song summary. An identical second/third arm exposes this limitation; it is not full context qualification.',
  results:[], comparisons:[] };
fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2));
async function run(excerpt, arm) {
  const directory=path.join(output,excerpt.id,'arm-'+arm);fs.mkdirSync(directory,{recursive:true});
  const note=(channel, index, [start,length,pitch])=>({channel:String(channel),note:String(index+1),start,length,pitch,velocity:.75,audible:true,locked:false});
  const target=excerpt.phrase.map((event,index)=>note(1,index,event));
  const surroundings=[note(1,100,[-1, .5, excerpt.phrase[0][2]]),note(1,101,[4.5,.5,excerpt.phrase.at(-1)[2]]),note(2,102,[0,1,excerpt.phrase[0][2]-12])];
  const request={schema:1,requestId:excerpt.id+'-'+arm,model:'multitrack_chords',seed:17,count:2,
    role:'native part',style:'',variation:'new idea',variationAmount:.5,family:excerpt.family,
    length:4,origin:4,tempo:excerpt.tempo,numerator:4,denominator:4,destination:'1',
    events:arm===1?[]:arm===2?target:[...target,...surroundings],
    chords:arm===1?[]:[{start:0,length:4,pitches:excerpt.pitches}],
    songSummary:arm===3?excerpt.summary:null};
  request.fingerprint=hash(request);
  const input=path.join(directory,'request.json'),response=path.join(directory,'response.json');
  fs.writeFileSync(input,JSON.stringify(request,null,2));
  const log=fs.openSync(path.join(directory,'worker.log'),'wx');
  const begun=Date.now();
  let processError='';
  await new Promise(resolve=>{
    const child=spawn(process.execPath,['--max-old-space-size=512',path.join(__dirname,'model-worker.cjs'),installation,input,response],{stdio:['ignore',log,log],windowsHide:true});
    const timer=setTimeout(()=>{processError='Owned worker exceeded 120 seconds';child.kill();},120000);
    child.on('error',error=>{processError=error.message;clearTimeout(timer);resolve();});
    child.on('exit',code=>{if(code && !processError)processError='Worker exit '+code;clearTimeout(timer);resolve();});
  });
  fs.closeSync(log);
  let result;
  try {result=JSON.parse(fs.readFileSync(response,'utf8'));} catch(error) {processError ||= error.message;}
  const rows={excerpt:excerpt.id,family:excerpt.family,arm,milliseconds:Date.now()-begun,valid:false,
    error:processError || result?.error || '',warnings:result?.warnings ?? [],measurements:result?.measurements ?? {}};
  if(!rows.error && result?.status==='ok' && result.requestId===request.requestId && result.fingerprint===request.fingerprint && result.candidates?.length===2) {
    rows.valid=result.candidates.every(candidate=>candidate.testFixture===false && candidate.events.length>0 && candidate.events.length<=65536 && candidate.events.every(event=>Number.isFinite(event.start)&&event.start>=0&&Number.isFinite(event.length)&&event.length>0&&event.start+event.length<=4&&Number.isInteger(event.pitch)&&event.pitch>=0&&event.pitch<=127&&Number.isFinite(event.velocity)&&event.velocity>0&&event.velocity<=1&&event.cents===0));
    rows.candidateHashes=result.candidates.map(candidate=>hash(candidate.events));
    rows.noteCounts=result.candidates.map(candidate=>candidate.events.length);
    rows.modelVersions=[...new Set(result.candidates.map(candidate=>candidate.modelVersion))];
  }
  if(!rows.valid && !rows.error) rows.error='Response/candidate validity contract failed';
  manifest.results.push(rows);
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2));
  console.log(JSON.stringify({excerpt:excerpt.id,arm,valid:rows.valid,error:rows.error,notes:rows.noteCounts,milliseconds:rows.milliseconds}));
  return rows;
}
(async()=>{
  for(const excerpt of excerpts) {
    const arms=[];for(let arm=1;arm<=3;++arm)arms.push(await run(excerpt,arm));
    manifest.comparisons.push({excerpt:excerpt.id,allArmsValid:arms.every(row=>row.valid),
      harmonyChangedOutput:arms[0].valid&&arms[1].valid?JSON.stringify(arms[0].candidateHashes)!==JSON.stringify(arms[1].candidateHashes):null,
      surroundingChangedOutput:arms[1].valid&&arms[2].valid?JSON.stringify(arms[1].candidateHashes)!==JSON.stringify(arms[2].candidateHashes):null});
    fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2));
  }
  manifest.terminal=true;manifest.validArms=manifest.results.filter(row=>row.valid).length;
  manifest.failedArms=manifest.results.length-manifest.validArms;
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2));
  console.log(JSON.stringify({output,excerpts:excerpts.length,validArms:manifest.validArms,failedArms:manifest.failedArms,review:'pending'}));
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
