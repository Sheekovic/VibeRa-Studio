# SPDX-License-Identifier: AGPL-3.0-or-later
"""Optional offline one-request worker. Native process containment remains required."""
import os
os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1", USE_TF="0", USE_FLAX="0", HF_HUB_DISABLE_TELEMETRY="1")
import hashlib
import json
import pathlib
import sys
import time
from anticipation_context import prepare, decode, NOTE, DURATION, REST


def atomic(path, value):
    data = json.dumps(value, allow_nan=False).encode("utf-8")
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Response exceeds byte budget")
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    for attempt in range(5):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 4:
                raise
            time.sleep(.01)  # Isolated process; never on application/UI/audio thread.


def main(installation, input_path, output_path):
    request = {}
    def cancelled():
        if (output_path.parent / "cancel").exists():
            raise RuntimeError("Generation cancelled")
    def progress(amount, stage="initializing", calls=0):
        cancelled()
        atomic(output_path.parent / "progress.json", {"requestId": request["requestId"], "progress": amount, "stage": stage, "calls": calls})
    try:
        if input_path.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("Request exceeds byte budget")
        request = json.loads(input_path.read_text(encoding="utf-8"))
        prepared = prepare(request)
        progress(0)
        begun = time.perf_counter()
        provenance = json.loads((installation / "provenance.json").read_text(encoding="utf-8"))
        if provenance["sourceRevision"] != "af37397922665a0fb8d474d7988b0f3755a38d45" or provenance["modelRevision"] != "fa800530aa1126dd6b58b38f3bdb8fcec9c9d4f5":
            raise ValueError("Unsupported provider revision")
        records = {item["path"]: item for item in provenance["files"]}
        if records.get("model/pytorch_model.bin", {}).get("sha256") != "45af5006d529a19af3654c729192c072f3aeb9e7e13e9eaf92c19bfd7c87e4d8":
            raise ValueError("Unpinned model checkpoint")
        required = ["model/config.json", "model/pytorch_model.bin"] + ["source/anticipation/" + name + ".py" for name in ("__init__", "config", "vocab", "ops", "sample")]
        for name in required:
            cancelled()
            record = records[name]
            path = installation / name
            h = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    cancelled()
                    h.update(block)
            if h.hexdigest() != record["sha256"]:
                raise ValueError("Pinned provider input changed: " + name)
        sys.path.insert(0, str(installation / "source"))
        import torch
        import transformers
        from transformers import AutoModelForCausalLM
        from anticipation import sample, ops
        torch.set_num_threads(2)
        torch.set_num_interop_threads(1)
        cancelled()
        model = AutoModelForCausalLM.from_pretrained(str(installation / "model"), local_files_only=True,
                    trust_remote_code=False, weights_only=True, torch_dtype=torch.float32, attn_implementation="eager").eval()
        initialized = time.perf_counter()
        cancelled()

        class Bounded:
            device = model.device
            offset = 0
            def reset(self):
                self.calls = 0
                self.cache_hits = 0
                self.tokens = None
                self.past = None
                self.deadline = time.perf_counter() + 90
            def __call__(self, tokens):
                cancelled()
                self.calls += 1
                if self.calls > 1536 or time.perf_counter() > self.deadline:
                    raise RuntimeError("Bounded inference budget exhausted")
                full = tokens[0].tolist()
                if self.tokens is not None and len(full) > len(self.tokens) and full[:len(self.tokens)] == self.tokens:
                    result = model(tokens[:, len(self.tokens):], past_key_values=self.past, use_cache=True)
                    self.cache_hits += 1
                else:
                    result = model(tokens, use_cache=True)
                self.tokens = full
                self.past = result.past_key_values
                if self.calls % 3 == 0:
                    progress(self.progress_base, "generating", self.calls)
                logits = result.logits[0, -1]
                phase = (tokens.shape[1] - 1) % 3
                if phase == 0:
                    upper = prepared["endTick"] - self.offset
                    if upper < 0:
                        raise ValueError("Invalid generation time origin")
                    logits[upper + 1:DURATION] = -float("inf")
                elif phase == 1:
                    maximum = max(1, min(999, prepared["endTick"] - int(tokens[0, -1]) - self.offset))
                    logits[DURATION + maximum + 1:NOTE] = -float("inf")
                else:
                    first = NOTE + prepared["program"] * 128
                    logits[NOTE:first] = -float("inf")
                    logits[first + 128:REST] = -float("inf")
                    permitted = set(prepared["pitches"])
                    for pitch in range(128):
                        if pitch not in permitted:
                            logits[first + pitch] = -float("inf")
                    if int(tokens[0, -1]) == DURATION:
                        logits[NOTE:REST] = -float("inf")
                    else:
                        logits[REST] = -float("inf")
                return result

        bounded = Bounded()
        original = sample.add_token
        def add_token(adapter, z, tokens, *args, **kwargs):
            offset = ops.min_time(tokens[max(len(tokens) - 1017, 0):], seconds=False)
            if offset != adapter.offset:
                adapter.tokens = None
                adapter.past = None
            adapter.offset = offset
            return original(adapter, z, tokens, *args, **kwargs)
        sample.add_token = add_token
        candidates, trials = [], []
        for index in range(request["count"]):
            bounded.progress_base = .05 + .9 * index / request["count"]
            progress(bounded.progress_base, "generating")
            bounded.reset()
            seed = (request["seed"] + index) & 0xffffffff
            torch.manual_seed(seed)
            tokens = sample.generate(bounded, prepared["startTick"] / 100, prepared["endTick"] / 100,
                                    inputs=prepared["inputs"], controls=prepared["controls"], top_p=.98)
            events = decode(tokens, prepared)
            candidates.append({"provider": "Anticipatory Music Transformer local CPU", "modelVersion": provenance["modelRevision"],
                               "testFixture": False, "seed": seed, "events": events})
            trials.append({"seed": seed, "calls": bounded.calls, "cacheHits": bounded.cache_hits,
                           "generatedTokens": tokens})
        progress(1, "complete")
        warnings = ["Experimental fixed-tempo 10 ms adapter; context onset/duration rounding recorded in ledger.",
                    "Velocity/expression, text styles, exact target locks, calibrated roles, song summary and performance variation are unsupported.",
                    "Local context is limited to five seconds each side; upstream rolling history can forget older tokens.",
                    "Channels sharing a GM program share a model voice identity; native source Channels remain unchanged.",
                    "Generated velocity is fixed at MIDI 72; no instrument articulation/playability or musical-quality qualification."]
        atomic(output_path, {"schema": 1, "status": "ok", "requestId": request["requestId"], "fingerprint": request["fingerprint"],
                            "candidates": candidates, "warnings": warnings,
                            "context": prepared, "trials": trials,
                            "measurements": {"initializationMs": (initialized - begun) * 1000,
                                             "inferenceMs": (time.perf_counter() - initialized) * 1000,
                                             "backend": "cpu", "torch": torch.__version__, "transformers": transformers.__version__,
                                             "networkInference": False}})
    except Exception as error:
        atomic(output_path, {"schema": 1, "status": "error", "requestId": request.get("requestId", "") if isinstance(request, dict) else "",
                            "fingerprint": request.get("fingerprint", "") if isinstance(request, dict) else "", "error": str(error)[:1024]})


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: anticipation-worker.py installation request.json response.json")
    main(*(pathlib.Path(arg).resolve() for arg in sys.argv[1:]))
