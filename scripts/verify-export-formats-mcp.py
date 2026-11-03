"""Song export as WAV/FLAC/OGG and per-insert stems through MCP on the real application.

Renders the demo project offline (no audio device): checks container signatures and
sizes, that every stem is a distinct non-silent WAV of the same length as the Song WAV,
the refusals for unsupported formats and existing stems, and actual job outcomes.
No listening is involved; audio content is checked numerically.
"""
import importlib.util
import json
import time
from pathlib import Path
import sys
import uuid
import wave

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stdio_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []
# Fake LAME: copies the WAV it is given behind an ID3 header, so the MP3 route is exercised.
fake_lame = output / "fake-lame.py"
fake_lame.write_text("import sys, shutil\nargs = sys.argv[1:]\nsrc, dst = args[-2], args[-1]\n"
                     "open(dst, 'wb').write(b'ID3' + open(src, 'rb').read())\n", encoding="utf-8")
lame_wrapper = output / "lame.cmd"
lame_wrapper.write_text(f'@"{sys.executable}" -I "{fake_lame}" %*\r\n', encoding="ascii")
import os
os.environ["VIBERA_LAME_EXECUTABLE"] = str(lame_wrapper)


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def energy(path):
    with wave.open(str(path), "rb") as w:
        frames = w.readframes(w.getnframes())
        width = w.getsampwidth()
        total = 0
        for i in range(0, len(frames) - width + 1, width * 97):  # Sparse but deterministic sampling.
            sample = int.from_bytes(frames[i:i + width], "little", signed=True)
            total += abs(sample)
        return w.getnframes(), total


client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    client.wait_job(data("vibera_project", dict(action="new", template="demo", discardChanges=True)))

    def export(path, **extra):
        job = data("vibera_project", dict(action="export", path=str(path), **extra))
        return client.wait_job(job, seconds=120)

    song = output / "song.wav"
    export(song)
    flac = output / "song.flac"
    export(flac)
    ogg = output / "song.ogg"
    export(ogg)
    check(song.read_bytes()[:4] == b"RIFF", "WAV export writes a RIFF file")
    check(flac.read_bytes()[:4] == b"fLaC" and flac.stat().st_size > 10000, "FLAC export writes a FLAC stream")
    check(ogg.read_bytes()[:4] == b"OggS" and ogg.stat().st_size > 5000, "OGG export writes an Ogg Vorbis stream")
    check(flac.stat().st_size < song.stat().st_size and ogg.stat().st_size < flac.stat().st_size,
          "Lossless FLAC is smaller than WAV and Vorbis smaller still")
    check(not (output / "song.render.wav").exists() and not list(output.glob("*.part")),
          "Intermediate render and partial files are removed")

    # Sample rate / bit depth: read the RIFF fmt chunk (format tag, rate, bits).
    def fmt(path):
        raw = path.read_bytes()
        at = raw.index(b"fmt ")
        tag = int.from_bytes(raw[at + 8:at + 10], "little")
        rate = int.from_bytes(raw[at + 12:at + 16], "little")
        bits = int.from_bytes(raw[at + 22:at + 24], "little")
        if tag == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the real tag.
            tag = int.from_bytes(raw[at + 32:at + 34], "little")
        return tag, rate, bits
    check(fmt(song)[1:] == (48000, 24), "The default export is 48 kHz, 24-bit")
    cd = output / "song-44k16.wav"
    export(cd, sampleRate=44100, bitDepth=16)
    check(fmt(cd) == (1, 44100, 16), "sampleRate 44100 / bitDepth 16 writes 44.1 kHz 16-bit PCM")
    cd_frames, cd_total = energy(cd)
    song_frames, _ = energy(song)
    check(abs(cd_frames / 44100 - song_frames / 48000) < .01 and cd_total > 0,
          "The 44.1 kHz export has the same duration and a nonzero signal")
    floating = output / "song-float.wav"
    export(floating, sampleRate=48000, bitDepth=32)
    check(fmt(floating) == (3, 48000, 32), "bitDepth 32 writes 32-bit float")
    export(output / "song-again.wav", sampleRate=48000, bitDepth=24)
    rejected_rate = client.call("vibera_project", dict(action="export", path=str(output / "x.wav"), sampleRate=22050),
                                allow_failure=True)
    check(bool(rejected_rate.get("isError")), "An unsupported sample rate is refused by the contract")

    # Cancel: an export cancelled right after admission ends as cancelled and writes nothing.
    doomed = output / "cancelled.wav"
    admitted = data("vibera_project", dict(action="export", path=str(doomed)))
    cancel = data("vibera_project", dict(action="cancelExport"))
    check(cancel["cancelling"] is True, "cancelExport reports that it is cancelling")
    final = admitted
    for _ in range(400):
        final = data("vibera_project", dict(action="job", operation=admitted["operation"]))
        if final.get("completionState") != "pending":
            break
        time.sleep(.05)
    check(final.get("completionState") == "cancelled" and not doomed.exists() and not list(output.glob("*.part")),
          "A cancelled export ends as cancelled and leaves no file")
    check(data("vibera_project", dict(action="cancelExport"))["cancelling"] is False,
          "cancelExport with nothing running says so")

    mp3 = output / "song.mp3"
    export(mp3)
    check(mp3.read_bytes()[:3] == b"ID3" and not list(output.glob("*.part")), "MP3 export runs through LAME")
    rejected = client.call("vibera_project", dict(action="export", path=str(output / "song.wma")), allow_failure=True)
    check(bool(rejected.get("isError")), "Unsupported export format is rejected")

    stems = output / "stems"
    export(stems, stems=True)
    files = sorted(stems.glob("*.wav"))
    check(len(files) >= 2, "Stems export writes one WAV per fed Mixer insert (" + str(len(files)) + ")")
    song_frames, _ = energy(song)
    measured = [energy(f) for f in files]
    check(all(frames == song_frames for frames, _ in measured), "Every stem has the Song's length")
    check(all(total > 0 for _, total in measured), "Every stem carries signal")
    check(len({total for _, total in measured}) == len(measured), "Stems differ from each other")
    names = [f.name for f in files]
    check(all(name[:2].isdigit() and name[2] == " " for name in names), "Stems are named 'NN Insert.wav'")
    again = client.call("vibera_project", dict(action="export", path=str(stems), stems=True), allow_failure=True)
    check(bool(again.get("isError")) and "output_exists" in json.dumps(again),
          "Existing stems need overwrite=true")
    export(stems, stems=True, overwrite=True)
    check(len(sorted(stems.glob("*.wav"))) == len(files), "Stems overwrite in place")
    # Playlist markers through the canonical commands and inspect readback.
    data("vibera_command", dict(command="marker.add", args=dict(beat=8, text="Drop")))
    data("vibera_command", dict(command="marker.add", args=dict(beat=2)))
    rows = data("vibera_inspect", dict(entity="markers"))
    markers = next(v for v in rows.values() if isinstance(v, list))
    check([(m["beat"], m["name"]) for m in markers] == [(2, "Marker 2"), (8, "Drop")],
          "Markers read back in beat order")
    data("vibera_command", dict(command="marker.delete", args=dict(target=markers[1]["id"])))
    rows = data("vibera_inspect", dict(entity="markers"))
    check(len(next(v for v in rows.values() if isinstance(v, list))) == 1, "marker.delete removes it")
    # Tempo map: add a jump and a ramp, read back seconds, edit, delete; a Song export lengthens.
    base_seconds = None
    data("vibera_command", dict(command="project.tempo", args=dict(value=120)))
    data("vibera_command", dict(command="tempo.change.add", args=dict(beat=8, value=60)))
    data("vibera_command", dict(command="tempo.change.add", args=dict(beat=4, value=90, step=True)))
    rows = next(v for v in data("vibera_inspect", dict(entity="tempo")).values() if isinstance(v, list))
    check([(r["beat"], r["bpm"], r["ramp"]) for r in rows] == [(0, 120, False), (4, 90, True), (8, 60, False)],
          "Tempo map reads back base tempo and sorted changes")
    import math
    ramp = 60 / ((90 - 120) / 4) * math.log(90 / 120)
    check(abs(rows[1]["seconds"] - ramp) < 1e-9 and abs(rows[2]["seconds"] - (ramp + 4 * 60 / 90)) < 1e-9,
          "Change times integrate the ramp exactly")
    bad = client.call("vibera_command", dict(command="tempo.change.add", args=dict(beat=8, value=100)), allow_failure=True)
    check(bool(bad.get("isError")), "A second change at one beat is rejected")
    data("vibera_command", dict(command="tempo.change.delete", args=dict(target=rows[2]["id"])))
    rows = next(v for v in data("vibera_inspect", dict(entity="tempo")).values() if isinstance(v, list))
    check(len(rows) == 2, "tempo.change.delete removes it")
    # Video player: refuses missing/undecodable files truthfully, reports closed state.
    missing = client.call("vibera_project", dict(action="video", path=str(output / "none.mp4")), allow_failure=True)
    check(bool(missing.get("isError")), "A missing video is refused")
    bogus = output / "not-a-video.mp4"
    bogus.write_bytes(b"not a video")
    check(bool(client.call("vibera_project", dict(action="video", path=str(bogus)), allow_failure=True).get("isError")),
          "An undecodable file is refused")
    check(data("vibera_project", dict(action="video", close=True))["open"] is False, "Video player reports closed")
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), listening=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
