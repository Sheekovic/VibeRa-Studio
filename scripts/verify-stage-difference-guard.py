"""Disposable exact stage-difference successor source matrix; no runtime."""
from pathlib import Path
import importlib.util
import sys


def main():
    # Reuse the existing bounded source-mirror matrix rather than introduce
    # another applier/framework. Both scripts are recorded as matrix inputs.
    sys.dont_write_bytecode = True
    path = Path(__file__).with_name("verify-summing-properties-guard.py")
    spec = importlib.util.spec_from_file_location("original_stage_difference_matrix", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.FAMILY = "patches/tracktion-stage-difference"
    module.TARGET = "modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h"
    module.EXPECTED_NUMSTAT = (96, 6)  # Independently reviewed frozen diagnostic delta.
    module.EXTRA_INPUTS = {"scripts/verify-stage-difference-guard.py"}
    return module.main()


if __name__ == "__main__":
    raise SystemExit(main())
