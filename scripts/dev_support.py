"""Small shared development safeguards. Never participates in real-time audio."""
from __future__ import annotations

import ctypes
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import time
from contextlib import contextmanager

_PROCESS_JOB = None


def owned_job_members():
    """A non-terminating job tracks descendants even if a launcher exits.

    Only this engineering Python process is assigned; all future descendants
    inherit it. No unrelated PID is adopted and KILL_ON_JOB_CLOSE is never set.
    One job per process avoids repeated/nested assignment during unit tests.
    """
    global _PROCESS_JOB
    if os.name != "nt":
        return []
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                               wintypes.DWORD, ctypes.c_void_p]
    if _PROCESS_JOB is None:
        job = kernel.CreateJobObjectW(None, None)
        if not job or not kernel.AssignProcessToJobObject(job, kernel.GetCurrentProcess()):
            raise RuntimeError("Cannot establish owned descendant tracking; no workload launched")
        _PROCESS_JOB = job
    capacity = 256
    while capacity <= 65536:
        class ProcessList(ctypes.Structure):
            _fields_ = [("assigned", wintypes.DWORD), ("count", wintypes.DWORD),
                        ("ids", ctypes.c_size_t * capacity)]
        result = ProcessList()
        if kernel.QueryInformationJobObject(_PROCESS_JOB, 3, ctypes.byref(result), ctypes.sizeof(result), None):
            return [int(result.ids[i]) for i in range(result.count)
                    if result.ids[i] != os.getpid() and process_identity(int(result.ids[i])) is not None]
        if ctypes.get_last_error() != 234:  # ERROR_MORE_DATA
            raise RuntimeError("Cannot reconcile engineering descendant ownership")
        capacity *= 2
    raise RuntimeError("Engineering descendant limit exceeded; reconcile rather than bypass")


def process_identity(pid):
    """PID plus OS creation identity; an inaccessible process is not an owner."""
    if os.name != "nt":
        try:
            return Path(f"/proc/{pid}/stat").read_text().split()[21]
        except OSError:
            return None
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        if ctypes.get_last_error() == 5:
            raise RuntimeError(f"Cannot establish process identity for PID {pid}; preserve uncertain process")
        return None
    try:
        exit_code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
            raise RuntimeError(f"Cannot verify whether PID {pid} is still active")
        if exit_code.value != 259:
            return None
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *(ctypes.byref(x) for x in times)):
            return None
        return str((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime)
    finally:
        kernel.CloseHandle(handle)


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}


def compiler_background_identity(root, pid):
    """Only owned telemetry at the exact configured compiler's pinned path.

    No MSBuild/compiler/test/plugin/input process is classified as background.
    An uncertain path or identity continues blocking. This never stops a process.
    """
    if os.name != "nt":
        return None
    from ctypes import wintypes
    import re
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return None
    try:
        name = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(name))
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(length)):
            return None
        actual = Path(name.value).resolve()
    finally:
        kernel.CloseHandle(handle)
    for definition in (Path(root) / "build/windows/CMakeFiles").glob("*/CMakeCXXCompiler.cmake"):
        match = re.search(r'^set\(CMAKE_CXX_COMPILER "([^"]+)"\)', definition.read_text(), re.M)
        if match:
            compiler = Path(match[1]).resolve()
            telemetry = compiler.with_name("VCTIP.exe")
            if compiler.name.lower() == "cl.exe" and compiler.is_file() and telemetry.is_file() and actual == telemetry:
                return dict(image=str(actual), sha256=content_identity(actual)["sha256"],
                            role="configured compiler telemetry; not an active build or input owner")
    return None


class EngineeringOwner:
    """Kernel lease serializes shared build/package/native/timing operations.

    Verified inherited token permits child scripts, never another independent
    command. Child processes are recorded by creation identity, never by name.
    No process is terminated here. Interrupted live children block takeover.
    """
    def __init__(self, root, purpose):
        self.root, self.purpose = Path(root).resolve(), purpose
        self.path = self.root / "build/engineering-owner.json"
        self.file = None
        self.nested = False

    def __enter__(self):
        self.path.parent.mkdir(exist_ok=True)
        previous = read_json(self.path)
        token = os.environ.get("VIBERA_DEV_OWNER_TOKEN")
        if token:
            if (token == previous.get("token") and
                    not previous.get("finished") and
                    process_identity(previous.get("pid", 0)) == previous.get("creation") and
                    previous.get("creation") is not None):
                self.nested = True
                return self
            raise RuntimeError("Invalid/stale inherited engineering owner; do not bypass the lease")
        self.file = (self.path.parent / ".engineering-owner.lock").open("a+b")
        self.file.seek(0)
        self.file.write(b"\0")
        self.file.flush()
        self.file.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.file.close()
            self.file = None
            raise RuntimeError(f"Engineering owner busy: {previous.get('purpose', 'unknown')} PID {previous.get('pid')}")
        try:
            # Read again under the kernel lock. Old metadata alone is harmless;
            # a surviving verified child is not. Never kill it to gain ownership.
            previous = read_json(self.path)
            background = []
            for child in previous.get("children", []):
                if child.get("creation") and process_identity(child["pid"]) == child["creation"]:
                    verified = compiler_background_identity(self.root, child["pid"])
                    if not verified or (child.get("background") is not None and verified != child["background"]):
                        raise RuntimeError(f"Interrupted owned child still active: PID {child['pid']}; reconcile its log first")
                    child["background"] = verified
                    background.append(child)
            self.token = secrets.token_hex(24)
            self.record = dict(pid=os.getpid(), creation=process_identity(os.getpid()),
                               purpose=self.purpose, token=self.token, children=background, started=time.time())
            if self.record["creation"] is None:
                raise RuntimeError("Cannot verify engineering process identity")
            if set(owned_job_members()) - {x["pid"] for x in background}:
                raise RuntimeError("Existing owned descendants still active; reconcile before a new workload")
            self.save()
            return self
        except BaseException:
            self.file.close()
            self.file = None
            raise

    def save(self):
        self.path.write_text(json.dumps(self.record, indent=2), encoding="utf-8")

    @contextmanager
    def metadata(self):
        # A separate byte serializes brief nested-child registrations. No DSP
        # thread ever uses this mechanism. The root continues holding byte0.
        with (self.path.parent / ".engineering-metadata.lock").open("a+b") as lock:
            lock.write(b"\0")
            lock.flush()
            lock.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX)
            record = read_json(self.path)
            try:
                yield record
            finally:
                self.path.write_text(json.dumps(record, indent=2), encoding="utf-8")

    def run(self, command, cwd=None, log=None):
        env = dict(os.environ)
        # Preserve compiler/object caches while preventing task-owned MSBuild
        # servers from outliving the serialized transaction and serving others.
        env["MSBUILDDISABLENODEREUSE"] = "1"
        if not self.nested:
            env["VIBERA_DEV_OWNER_TOKEN"] = self.token
        started = time.perf_counter()
        stream = Path(log).open("w", encoding="utf-8") if log else None
        try:
            child = subprocess.Popen(command, cwd=cwd or self.root, env=env,
                                     stdout=stream, stderr=subprocess.STDOUT if stream else None)
            identity = dict(pid=child.pid, creation=process_identity(child.pid),
                            command=[str(x) for x in command])
            with self.metadata() as record:
                record["children"].append(identity)
            result = child.wait()
            with self.metadata() as record:
                record["children"] = [x for x in record["children"] if x != identity]
            return dict(command=[str(x) for x in command], exitCode=result,
                        seconds=time.perf_counter() - started, log=str(log) if log else None)
        finally:
            if stream:
                stream.close()

    def __exit__(self, *exception):
        if self.file:
            with self.metadata() as record:
                for pid in owned_job_members():
                    creation = process_identity(pid)
                    if creation and not any(x["pid"] == pid and x["creation"] == creation for x in record["children"]):
                        child = dict(pid=pid, creation=creation, purpose="owned descendant retained after launcher exit")
                        background = compiler_background_identity(self.root, pid)
                        if background:
                            child["background"] = background
                        record["children"].append(child)
                record["finished"] = time.time()
                record["interrupted"] = exception[0] is not None
            self.file.close()  # Kernel releases byte lock even after interruption.


TEXT = {".cpp", ".h", ".py", ".ps1", ".json", ".txt", ".cmake", ".md"}


def content_identity(path, *, normalize_text=False):
    """Source LF normalization is explicit; binary identity is always exact."""
    path = Path(path)
    data = path.read_bytes()
    exact = hashlib.sha256(data).hexdigest()
    normalized = data.replace(b"\r\n", b"\n") if normalize_text or path.suffix.lower() in TEXT else data
    return dict(sha256=exact, contentSha256=hashlib.sha256(normalized).hexdigest())


def compare_compiled_inputs(before, after):
    """Compare authoritative source inventories: text LF content, other bytes exact.

    Unknown/malformed identities cannot establish stability. This is a receipt
    check, not a cache, automatic result reuse or a replacement source inventory.
    """
    def index(rows):
        if not isinstance(rows, list) or not rows:
            raise ValueError("Compiled source inventory is missing/empty")
        result = {}
        for row in rows:
            if not isinstance(row, dict) or not isinstance(row.get("path"), str) or not row["path"]:
                raise ValueError("Compiled source inventory has an invalid path")
            path = row["path"]
            if path in result:
                raise ValueError("Compiled source inventory has duplicate paths")
            key = "contentSha256" if Path(path).suffix.lower() in TEXT else "sha256"
            digest = row.get(key)
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise ValueError("Compiled source inventory has an invalid content identity")
            result[path] = digest
        return result

    previous, current = index(before), index(after)
    added = sorted(current.keys() - previous.keys())
    removed = sorted(previous.keys() - current.keys())
    changed = [dict(path=path, before=previous[path], after=current[path])
               for path in sorted(previous.keys() & current.keys()) if previous[path] != current[path]]
    fingerprint = lambda rows: hashlib.sha256(json.dumps(sorted(rows.items()), separators=(",", ":")).encode()).hexdigest()
    return dict(stable=not (added or removed or changed), added=added, removed=removed, changed=changed,
                beforeContentSha256=fingerprint(previous), afterContentSha256=fingerprint(current))


def record_compiled_input_stability(receipt, before, read_source, binaries):
    """Always record post-test source/binary identity; retain primary test errors."""
    check = dict(source=None, binaries=[])
    try:
        check["source"] = compare_compiled_inputs(before, read_source())
    except Exception as error:
        check["source"] = dict(stable=False, unavailable=type(error).__name__)
    binaries_stable = True
    for path, expected in sorted(binaries.items()):
        item = dict(path=path, before=expected.get("sha256"))
        try:
            digest = item["before"]
            if not isinstance(digest, str) or len(digest) != 64:
                raise ValueError("Recorded binary identity missing")
            item["after"] = content_identity(path)["sha256"]
            item["stable"] = item["before"] == item["after"]
        except Exception as error:
            item.update(stable=False, unavailable=type(error).__name__)
        binaries_stable = binaries_stable and item["stable"]
        check["binaries"].append(item)
    stable = bool(check["source"]["stable"] and binaries_stable)
    receipt["compiledInputsCheck"] = check
    receipt["compiledInputsStable"] = stable
    if not stable:
        receipt["passed"] = False
        receipt.setdefault("failure", "Compiled source/binary inputs changed or could not be verified after tests")
    return stable


def verification_inputs(root, tests, tool_paths=()):
    """Receipt-only verifier provenance, deliberately separate from C++ inputs.

    Conservatively include local script helpers rather than pretend dynamic
    Python/Node imports or PowerShell dot-sourcing are a statically complete graph.
    No environment secrets/file contents, execution, build invalidation or
    result reuse. Before/after snapshots expose changes during a verification.
    """
    root = Path(root).resolve()
    files = {}
    script_suffixes = {".py", ".pyw", ".ps1", ".psm1", ".psd1", ".js", ".mjs", ".cjs", ".bat", ".cmd"}

    def add(path, reason):
        path = Path(path).resolve()
        try:
            name = path.relative_to(root).as_posix()
        except ValueError:
            name = str(path)
        files.setdefault(name, dict(path=path, reasons=set()))["reasons"].add(reason)

    def scripts_below(directory):
        # Never follow reparse/symlink research/output trees. Only the explicit
        # project-owned script roots and actual candidate skill scripts below.
        def safe(path):
            return not (path.is_symlink() or
                        getattr(path.lstat(), "st_file_attributes", 0) & 0x400)
        if not directory.is_dir() or not safe(directory):
            return
        for current, directories, names in os.walk(directory, followlinks=False):
            directories[:] = [name for name in directories if name != "__pycache__" and safe(Path(current) / name)]
            for name in names:
                path = Path(current) / name
                if path.suffix.lower() in script_suffixes and safe(path):
                    add(path, "conservative local verifier/helper")

    for name in ("scripts/dev.py", "scripts/dev-select.py", "scripts/dev_support.py", "scripts/distribution.py"):
        add(root / name, "runner/selection/identity implementation")
    for name in ("scripts", "tests", "skills/vibera-studio/scripts"):
        scripts_below(root / name)
    selected = []
    for test in tests:
        working = next((p["value"] for p in test.get("properties", [])
                        if p.get("name") == "WORKING_DIRECTORY"), str(root / "build/windows"))
        working = Path(working)
        if not working.is_absolute():
            working = root / working
        command = [str(value) for value in test.get("command", [])]
        selected.append(dict(name=test["name"], command=command, workingDirectory=str(working)))
        unittest_discovery = any(command[i:i + 2] == ["-m", "unittest"]
                                 for i in range(len(command) - 1))
        for index, argument in enumerate(command):
            # unittest's -p/--pattern is a discovery expression, not a script
            # executed relative to CTest's build directory. Its actual test and
            # helper sources are already captured from the tests tree above.
            if unittest_discovery and ((index > 0 and command[index - 1] in ("-p", "--pattern")) or
                                       argument.startswith("--pattern=") or
                                       (argument.startswith("-p") and argument != "-p")):
                continue
            suffix = Path(argument).suffix.lower()
            if suffix not in script_suffixes | {".exe"}:
                continue
            path = Path(argument)
            if not path.is_absolute():
                path = working / path
            if suffix in script_suffixes:
                add(path, "selected CTest script: " + test["name"])
                # Custom project verifiers can import adjacent helper modules.
                # Bound this to siblings; no unknown directory-wide tree scan.
                if path.parent.is_relative_to(root) and path.parent.is_dir():
                    for sibling in path.parent.iterdir():
                        if sibling.is_file() and sibling.suffix.lower() in script_suffixes:
                            add(sibling, "selected verifier sibling helper")
            else:
                add(path, "selected CTest executable input: " + test["name"])
                # Current MCP verifiers prefer this client beside the actual
                # selected app before falling back to the project skill source.
                scripts_below(path.parent / "skills/vibera-studio/scripts")
    for path in tool_paths:
        add(path, "verification tool executable")
    snapshot = []
    for name, item in sorted(files.items()):
        entry = dict(path=name, reasons=sorted(item["reasons"]))
        try:
            entry.update(content_identity(item["path"], normalize_text=item["path"].suffix.lower() in script_suffixes))
            entry["missing"] = False
        except OSError as error:
            entry.update(missing=True, error=type(error).__name__)
        snapshot.append(entry)
    return dict(schema=1, files=snapshot, selectedTests=selected,
                complete=not any(row["missing"] for row in snapshot),
                runtime=dict(pythonVersion=list(__import__("sys").version_info[:3]), osName=os.name),
                scope="Conservative local scripts plus selected script paths/candidate skill clients and tools; "
                      "not a complete third-party package, arbitrary data or environment snapshot; no automatic reuse")


def project_schema(root):
    import re
    match = re.search(r"currentVersion\s*=\s*(\d+)\s*;", (Path(root) / "src/Project.h").read_text())
    if not match:
        raise ValueError("Authoritative project schema missing")
    return int(match[1])


def configure_required(root, required_targets=()):
    """Reuse the valid cache; new VS targets need generation before MSBuild."""
    import re
    root = Path(root).resolve()
    preset = next(x for x in read_json(root / "CMakePresets.json")["configurePresets"] if x["name"] == "windows")
    if preset.get("binaryDir") != "${sourceDir}/build/windows":
        raise ValueError("Only the canonical build/windows cache is authorized")
    cache = root / "build/windows/CMakeCache.txt"
    if not cache.is_file():
        return True
    entries = dict(re.findall(r"^([A-Za-z_][A-Za-z0-9_-]*):[^=\r\n]*=([^\r\n]*)", cache.read_text(errors="replace"), re.M))
    if entries.get("CMAKE_HOME_DIRECTORY", "").replace("\\", "/").casefold() != root.as_posix().casefold():
        raise ValueError("CMake cache belongs to another source; preserve and reconcile")
    if entries.get("CMAKE_GENERATOR") != preset["generator"] or entries.get("CMAKE_GENERATOR_PLATFORM") != preset["architecture"]:
        raise ValueError("CMake generator/architecture changed; reconcile this cache explicitly")
    for name, value in preset.get("cacheVariables", {}).items():
        if not isinstance(value, str):
            raise ValueError("Unsupported configure value; inspect before reusing the cache")
        if entries.get(name) != value:
            return True
    for target in required_targets:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", target):
            raise ValueError("Invalid required CMake target")
        if not (root / "build/windows" / (target + ".vcxproj")).is_file():
            return True
    return False
