"""Multi-output instrument routing acceptance (owned --mcp-test instance, offline render).

The original fixture writes a constant (bus + 1) * 0.1 on each active stereo output bus. Routing
outputs 2-4 to Mixer inserts 5, 6 and 7 must add exactly those levels to the mix; muting insert 6
must remove only bus 3; clearing the routes must leave the main output alone; save/open must keep
the routes. Usage: verify-multi-output-mcp.py <VibeRa exe> <work folder> <fixture .vst3>
"""
import importlib.util, shutil, sys, time, wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    print("PASS", message)


def level(path):
    with wave.open(str(path)) as w:
        frames, width, channels = w.getnframes(), w.getsampwidth(), w.getnchannels()
        data = w.readframes(frames)
    step = width * channels * 97
    values = [int.from_bytes(data[i:i + width], "little", signed=True) / (2 ** (8 * width - 1))
              for i in range(0, len(data) - width, step)]
    middle = values[len(values) // 4: 3 * len(values) // 4]
    return max(abs(v) for v in middle)


def main(app, work, fixture):
    work = Path(work)
    if work.exists():
        shutil.rmtree(work)
    (work / "scan").mkdir(parents=True)
    shutil.copy2(fixture, work / "scan" / Path(fixture).name)
    spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.VibeRaClient(Path(app))

    def call(tool, args=None, allow_failure=False):
        response = client.call(tool, args or {}, allow_failure=True)
        if response.get("isError") and not allow_failure:
            raise AssertionError(str(response.get("structuredContent", response))[:400])
        return response.get("structuredContent", response)

    def terminal(tool, admission):
        current = admission
        for _ in range(800):
            if current.get("completionState") != "pending":
                return current
            time.sleep(.05)
            current = call(tool, {"action": "job", "operation": admission["operation"]}, True)
        return current

    def command(name, args):
        admission = call("vibera_command", {"command": name, "args": args, "revision": call("vibera_state")["revision"]})
        return terminal("vibera_mixerchain", admission) if admission.get("completionState") == "pending" else admission

    def render(name):
        out = work / (name + ".wav")
        done = terminal("vibera_project", call("vibera_project", {"action": "export", "path": str(out), "overwrite": True}))
        check(done.get("completionState") == "completed", name + " render completes")
        return level(out)

    def routes():
        return [p.get("extraOutputMixers") for p in call("vibera_plugin", {"action": "list", "limit": 64})["items"]
                if p.get("instrument")]

    try:
        call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
        scan = terminal("vibera_catalogue", call("vibera_catalogue", {"action": "scan", "customFolders": [str(work / "scan")],
                                                                      "standardLocations": False}))
        check(scan.get("completionState") == "completed", "Fixture scan completes")
        row = next(r for r in call("vibera_catalogue", {"action": "list", "kind": "instruments", "limit": 64})["items"]
                   if r["name"] == "VibeRa Multi-output Instrument Fixture")
        descriptor = call("vibera_catalogue", {"action": "descriptor", "id": row["id"]})["descriptor"]
        before = {r["id"] for r in call("vibera_state")["rack"]["rows"]}
        command("plugin.instrument.create", {"text": descriptor})
        channel = next(r["id"] for r in call("vibera_state")["rack"]["rows"] if r["id"] not in before)
        pattern = call("vibera_state")["selectedPattern"]
        command("clip.add", {"pattern": pattern, "lane": 0, "beat": 0, "length": 8})
        main_only = render("main-only")
        command("channel.outputRoutes", {"channel": channel, "ids": [5, 6, 7]})
        check(routes() == [[5, 6, 7]], "Routes read back as inserts 5, 6, 7")
        routed = render("routed")
        # Extra buses 0.2 + 0.3 + 0.4 pass the Master fader g; the main bus is main_only.
        master = (routed - main_only) / .9
        check(.5 < master < 1.01, "Extra outputs add the expected Master-scaled level (g=%.3f)" % master)
        command("mixer.mute", {"mixer": 6, "enabled": True})
        muted = render("insert6-muted")
        check(abs((routed - muted) - .3 * master) < .01, "Muting insert 6 removes exactly output bus 3")
        command("mixer.mute", {"mixer": 6, "enabled": False})
        project = work / "multi-output.vibra"
        terminal("vibera_project", call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
        call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
        terminal("vibera_project", call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
        check(routes() == [[5, 6, 7]], "Save/open keeps the multi-output routes")
        command("channel.outputRoutes", {"channel": channel, "ids": []})
        check(abs(render("cleared") - main_only) < .002, "Clearing the routes restores the main output only")
    finally:
        client.close()


if __name__ == "__main__":
    main(*sys.argv[1:4])
