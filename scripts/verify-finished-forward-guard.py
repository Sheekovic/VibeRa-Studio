"""Exact finished-forward same-latency native source matrix; not DSP qualification."""
from pathlib import Path
import importlib.util
import sys

def main():
    sys.dont_write_bytecode = True
    path = Path(__file__).with_name("verify-summing-properties-guard.py")
    if not path.is_file():
        path = Path.cwd() / "scripts/verify-summing-properties-guard.py"
    spec = importlib.util.spec_from_file_location("finished_forward_matrix", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.FAMILY = "patches/tracktion-finished-forward-continuation"
    module.TARGET = "modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h"
    module.EXPECTED_NUMSTAT = (288, 2)
    module.EXTRA_INPUTS = {"scripts/verify-finished-forward-guard.py"}
    return module.main()

if __name__ == "__main__":
    raise SystemExit(main())
