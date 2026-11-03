# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verify exported event streams against retained tokens, including GM percussion routing."""
import collections,json,pathlib
import mido
root=pathlib.Path(__file__).resolve().parents[2]/"build/coproducer-research/anticipation"
reports=list((root/"benchmark-original-20261012").glob("case-*/report.json")) + [root/"probe-cache-repeat-20261012/report.json"]
checked=[]
for report_path in reports:
    report=json.loads(report_path.read_text(encoding="utf-8"))
    for arm in report["arms"]:
        if not arm["valid"]:continue
        expected=collections.Counter()
        tokens=arm["generatedTokens"]+[token-27513 for token in arm["controls"]]
        for time,duration,note in zip(tokens[0::3],tokens[1::3],tokens[2::3]):
            if note==27512:continue
            program,pitch=divmod(note-11000,128)
            expected[("on",time,program,pitch,72)]+=1
            expected[("off",time+duration-10000,program,pitch,0)]+=1
        path=report_path.parent/("arm-"+str(arm["arm"])+".mid")
        mid=mido.MidiFile(path);actual=collections.Counter()
        if mid.ticks_per_beat!=50:raise AssertionError("Unexpected export PPQ")
        for track in mid.tracks:
            time=0;programs={}
            for message in track:
                time+=message.time
                if message.type=="program_change":programs[message.channel]=message.program
                elif message.type in ("note_on","note_off"):
                    program=128 if message.channel==9 else programs.get(message.channel,0)
                    kind="on" if message.type=="note_on" and message.velocity>0 else "off"
                    actual[(kind,time,program,message.note,message.velocity if kind=="on" else 0)]+=1
        if actual!=expected:raise AssertionError("MIDI stream mismatch: "+str(path))
        checked.append({"report":str(report_path.relative_to(root)),"arm":arm["arm"],"events":sum(actual.values())})
(root/"midi-verification.json").write_text(json.dumps({"passed":True,"streams":len(checked),"scope":"Raw MIDI on/off/program/timing/velocity events preserved; same-pitch voice ownership and actual instrument playback/listening remain unqualified","checks":checked},indent=2),encoding="utf-8")
print("PASS",len(checked),"real-output MIDI streams, including percussion channel 10; no listening/voice-ownership qualification")
