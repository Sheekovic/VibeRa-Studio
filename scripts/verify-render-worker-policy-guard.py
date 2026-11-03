"""Exact offline-render worker-order patch matrix; no runtime/audio claim."""
from pathlib import Path
import importlib.util
import sys


def main():
    sys.dont_write_bytecode = True
    path = Path(__file__).with_name("verify-summing-properties-guard.py")
    spec = importlib.util.spec_from_file_location("render_worker_policy_matrix", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.FAMILY = "patches/tracktion-render-worker-policy"
    module.TARGET = "modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp"
    module.EXPECTED_NUMSTAT = (4, 1)  # Independent frozen construction-order delta.
    module.EXTRA_INPUTS = {"scripts/verify-render-worker-policy-guard.py"}
    return module.main()


if __name__ == "__main__":
    raise SystemExit(main())
