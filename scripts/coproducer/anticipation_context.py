# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pure quarter-note/token boundary. No model, filesystem or application access."""
import math
import re

DURATION = 10000
NOTE = 11000
CONTROL = 27513
REST = 27512
ORIGIN = 1000  # Avoid upstream's removal of controls earlier than five seconds.


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def identity(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9]{1,20}", value) is not None


def prepare(request):
    """Validate explicit capabilities and retain a ledger for every context event."""
    r = request
    if not isinstance(r, dict) or type(r.get("schema")) is not int or r["schema"] != 1 or r.get("model") != "anticipation-small":
        raise ValueError("Unsupported anticipation request schema/model")
    if not isinstance(r.get("requestId"), str) or not 1 <= len(r["requestId"]) <= 128 or not isinstance(r.get("fingerprint"), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", r["fingerprint"]):
        raise ValueError("Invalid request identity")
    if not identity(r.get("destination")) or type(r.get("count")) is not int or not 1 <= r["count"] <= 3 or type(r.get("seed")) is not int or not 0 <= r["seed"] <= 0xffffffff:
        raise ValueError("Invalid destination/count/seed")
    if r.get("style") != "" or r.get("role") != "native part" or r.get("variation") != "new idea" or r.get("chords") != []:
        raise ValueError("This adapter supports native new parts only; text style, roles, variation and supplied chord overrides are not qualified")
    if not number(r.get("tempo")) or not 20 <= r["tempo"] <= 400 or not number(r.get("length")) or r["length"] <= 0:
        raise ValueError("Invalid fixed tempo/quarter-note length")
    if type(r.get("numerator")) is not int or not 1 <= r["numerator"] <= 32 or type(r.get("denominator")) is not int or r["denominator"] not in (1, 2, 4, 8, 16, 32):
        raise ValueError("Invalid meter")
    scale = 6000 / r["tempo"]  # 100 ten-ms ticks per second.
    ticks = r["length"] * scale
    if not 1 <= ticks <= 1500 or abs(ticks - round(ticks)) > 1e-7:
        raise ValueError("Target must end exactly on a 10 ms boundary and be at most 15 seconds")
    profiles = r.get("profiles")
    if not isinstance(profiles, dict) or len(profiles) > 256 or r["destination"] not in profiles:
        raise ValueError("Explicit destination GM profile required")
    for channel, profile in profiles.items():
        if not identity(channel) or not isinstance(profile, dict) or profile.get("source") != "user-specified" or type(profile.get("program")) is not int or not 0 <= profile["program"] <= 128:
            raise ValueError("Profiles require user-specified zero-based GM program (128 percussion)")
        pitches = profile.get("pitches")
        if not isinstance(pitches, list) or not pitches or len(pitches) > 128 or len(set(pitches)) != len(pitches) or any(type(p) is not int or not 0 <= p <= 127 for p in pitches):
            raise ValueError("Explicit permitted pitches/drum note map required")
    events = r.get("events")
    if r.get("tempoMap") or r.get("controllers") or r.get("tuning"):
        raise ValueError("Tempo changes, controllers and tuning are unsupported by this fixed-tempo adapter")
    if not isinstance(events, list) or len(events) > 8192 or type(r.get("allowUnmappedContext", False)) is not bool:
        raise ValueError("Invalid event/omission budget")
    inputs, controls, ledger = [], [], []
    for index, event in enumerate(events):
        e = event
        if not isinstance(e, dict) or not identity(e.get("channel")) or not identity(e.get("note")) or not number(e.get("start")) or not number(e.get("length")) or e["length"] <= 0 or type(e.get("pitch")) is not int or not 0 <= e["pitch"] <= 127 or not number(e.get("velocity")) or not 0 < e["velocity"] <= 1 or type(e.get("audible")) is not bool or type(e.get("locked")) is not bool:
            raise ValueError("Invalid context event")
        if e.get("cents", 0) != 0 or e.get("expression"):
            raise ValueError("Per-note tuning/expression cannot be represented by this model")
        row = {"index": index, "channel": e["channel"], "note": e["note"]}
        in_target = e["start"] < r["length"] and e["start"] + e["length"] > 0
        if e["channel"] == r["destination"] and e["locked"] and in_target:
            raise ValueError("Exact target locks are unsupported by this adapter")
        reason = None
        if not e["audible"]:
            reason = "not included in audible context"
        elif e["start"] * scale < -500 or e["start"] * scale > ticks + 500:
            reason = "outside five-second local context window"
        elif e["channel"] == r["destination"] and in_target:
            reason = "destination material in generated region; new idea replaces model input, not source"
        elif e["channel"] not in profiles:
            if not r.get("allowUnmappedContext", False):
                raise ValueError("Included context Channel has no explicit GM profile")
            reason = "unmapped context explicitly allowed"
        if reason:
            row["omitted"] = reason
        else:
            profile = profiles[e["channel"]]
            if e["pitch"] not in profile["pitches"]:
                raise ValueError("Context pitch is outside its explicit profile")
            onset = ORIGIN + round(e["start"] * scale)
            duration = round(e["length"] * scale)
            if not 1 <= duration <= 999 or not 500 <= onset < 10000:
                raise ValueError("Context duration/time cannot be represented without truncation")
            token = [onset, DURATION + duration, NOTE + profile["program"] * 128 + e["pitch"]]
            # Source context in/after target is a control; earlier notes form history.
            control = e["start"] >= 0
            (controls if control else inputs).append([v + CONTROL for v in token] if control else token)
            row.update(kind="control" if control else "history", tokens=token,
                       onsetRoundingSeconds=(onset - ORIGIN - e["start"] * scale) / 100,
                       durationRoundingSeconds=(duration - e["length"] * scale) / 100,
                       velocityConsumed=False)
        ledger.append(row)
    if len(inputs) + len(controls) > 240:
        raise ValueError("Context exceeds conservative 240-event budget; select fewer Channels/shorter region")
    flatten = lambda rows: [v for row in sorted(rows, key=lambda row: (row[0], row[2], row[1])) for v in row]
    return {"inputs": flatten(inputs), "controls": flatten(controls), "ledger": ledger,
            "startTick": ORIGIN, "endTick": ORIGIN + round(ticks), "ticksPerQuarter": scale,
            "program": profiles[r["destination"]]["program"], "pitches": profiles[r["destination"]]["pitches"]}


def decode(tokens, prepared):
    if len(tokens) % 3:
        raise ValueError("Incomplete generated token tuple")
    events = []
    for onset, duration, note in zip(tokens[::3], tokens[1::3], tokens[2::3]):
        if any(type(v) is not int for v in (onset, duration, note)):
            raise ValueError("Invalid model token type")
        if note == REST or onset < prepared["startTick"] or onset >= prepared["endTick"]:
            continue  # Upstream includes source history/future in returned sequence.
        program, pitch = divmod(note - NOTE, 128)
        length = duration - DURATION
        if program != prepared["program"] or pitch not in prepared["pitches"] or length <= 0 or onset + length > prepared["endTick"]:
            raise ValueError("Generated event violates explicit destination/time capability")
        events.append({"start": (onset - prepared["startTick"]) / prepared["ticksPerQuarter"],
                       "length": length / prepared["ticksPerQuarter"], "pitch": pitch,
                       "velocity": 72 / 127, "cents": 0})
    if not events:
        raise ValueError("Model generated no target events; no substitute inserted")
    return events
