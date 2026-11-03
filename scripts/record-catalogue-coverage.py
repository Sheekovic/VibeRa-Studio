"""Record three scoped callback reviews from current passing component/MCP facts.

Does not qualify a package, native mouse input or the entire Plugins page.
Run after observing the corresponding owned processes exit successfully.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import sys

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--component-result", type=pathlib.Path, required=True)
    parser.add_argument("--mcp-result", type=pathlib.Path, required=True)
    args = parser.parse_args()
    ui, mcp = read(args.component_result), read(args.mcp_result)
    sha = digest(args.executable)
    if not (ui.get("passed") is True and mcp.get("passed") is True and mcp.get("exitCode") == 0 and
            ui.get("applicationSha256") == sha == mcp.get("applicationSha256")):
        raise ValueError("Current binary and actual successful component/MCP evidence must match")
    if mcp.get("verificationScriptSha256") != digest(ROOT / "scripts/verify-catalogue-mcp.py"):
        raise ValueError("MCP verifier changed; old result cannot qualify its new cases")
    if (mcp.get("clientSha256") != digest(ROOT / "skills/vibera-studio/scripts/stdio_client.py") or
            not ui.get("checks") or not mcp.get("checks") or mcp.get("cancellation") != "cancelled" or
            mcp.get("asyncFailure") != "persistence_failed" or not mcp.get("temporarySettingsRemoved")):
        raise ValueError("Current client and all claimed cancellation/failure/cleanup cases must be qualified")
    inputs = {item["path"]:item["sha256"] for item in read(ROOT / "build/compiled-project-inputs.json")}
    for name, recorded in inputs.items():
        if digest(ROOT / name) != recorded:
            raise ValueError("Native input changed after compilation: " + name)
    for name in ["scripts/verify-catalogue-mcp.py", "scripts/mcp-coverage.py",
                 "skills/vibera-studio/SKILL.md", "skills/vibera-studio/manifest.json",
                 "skills/vibera-studio/scripts/stdio_client.py", "skills/vibera-studio/references/catalogue.md"]:
        inputs[name] = digest(ROOT / name)
    evidence = dict(passed=True, applicationSha256=sha, sourceInputs=inputs,
        scope="Three actual Settings component callbacks plus installed-provider semantic MCP; not complete page or native OS mouse acceptance",
        checks=[dict(kind="actual JUCE component callbacks",receipt=args.component_result.resolve().relative_to(ROOT).as_posix(),cases=ui["checks"]),
                dict(kind="actual installed-provider MCP",receipt=args.mcp_result.resolve().relative_to(ROOT).as_posix(),provider=mcp["provider"],
                     caseCount=len(mcp["checks"]),asyncFailure=mcp["asyncFailure"],cancellation=mcp["cancellation"],temporarySettingsRemoved=mcp["temporarySettingsRemoved"])],
        nativeMouse="pending: targeted messages failed; guarded OS run paused on recent user input", completeFunctionalCoverage=False)
    evidence_path = ROOT / "docs/mcp/PLUGIN_DISCOVERY_EVIDENCE.json"
    evidence_path.write_text(json.dumps(evidence,indent=2)+"\n")
    spec = importlib.util.spec_from_file_location("coverage_gate", ROOT / "scripts/mcp-coverage.py")
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    review_path = ROOT / "docs/mcp/REVIEWED_SURFACES.json"
    reviews = read(review_path) if review_path.exists() else {}
    # Replace only this feature's callback records; preserve other engineers' reviews.
    def owned(value):
        return (isinstance(value, dict) and isinstance(value.get("verification"), dict) and
                value["verification"].get("receipt") == evidence_path.relative_to(ROOT).as_posix())
    reviews = {key:value for key,value in reviews.items() if not owned(value)}
    count = 0
    lines = (ROOT / "src/PluginPage.h").read_text().splitlines()
    for entry in gate.inventory()["customSurfaceCandidates"]:
        if not entry["surface"].startswith("src/PluginPage.h:"):
            continue
        code = lines[int(entry["surface"].split(":")[1])-1].strip()
        if code not in ["scan.onClick = [this]", "retry.onClick = [this]", "cancel.onClick = [this]"]:
            continue
        reviews[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"],
            operations=["plugins.cancelScan" if code.startswith("cancel") else "plugins.scan", "plugins.discover"],
            readback=["vibera_catalogue:state","vibera_catalogue:job","vibera_catalogue:list"],
            skill="skills/vibera-studio/references/catalogue.md", verification=dict(status="passed",receipt=evidence_path.relative_to(ROOT).as_posix(),
                inputs=inputs,cases=["Actual Settings "+code.split('.')[0]+" callback and scope/availability/persistence assertions",
                                    "Installed-provider MCP discovery/retry/cancellation/failure/project/export"],nativeMouse="pending; component/MCP scope only"))
        count += 1
    if count != 3:
        raise ValueError("Callback ownership changed; manually review instead of inventing mappings")
    review_path.write_text(json.dumps(reviews,indent=2)+"\n")
    print(json.dumps(dict(applicationSha256=sha,reviewedCallbacks=count,componentCases=len(ui["checks"]),mcpCases=len(mcp["checks"]))))


if __name__ == "__main__":
    main()
