#!/usr/bin/env python3
"""The development command behind dev.cmd (docs/DECISIONS.md "One development entry point").

  dev.cmd doctor [--engine-only]          toolchain, SDK, dependencies and game data, with remediation;
                                          warns when the shell is elevated
  dev.cmd configure [--preset P]          configure the preset's build tree (CMakePresets.json; default: fast)
  dev.cmd build [TARGET] [--preset P]     configure, check TARGET exists, build it (default: everything)
  dev.cmd check [--preset P] [--target T] [--quick]
                                          build, then run the preset's tests:
                                            fast  build\\       suite synthetic (--quick: without slow tests;
                                                               --target T: only T and the tests named T*)
                                            full  build-full\\  fresh configure, clean build, suite all
                                            analyze  build-analyze\\  build with MSVC /analyze, then fail on a
                                                     finding not in tests\\analyze_baseline.txt (no suite)
                                            asan  build-asan\\  build with MSVC AddressSanitizer and PDBs, suite
                                                  synthetic without the tests labelled no_asan
  dev.cmd test [--preset P] [CTEST ARGS]  run CTest on the preset's tree with any CTest options
  dev.cmd suite [NAME] [--preset P] [tools/test_suite.py options] [-- CTEST ARGS]
  dev.cmd package --commit SHA [--out DIR]
                                          build HEAD (it must be SHA, with a clean tree) in build-package\\ with
                                          PDBs and stage an internal package outside the checkout (default
                                          %LOCALAPPDATA%\\alamo-dev\\packages): alamo-<sha>\\ with MANIFEST.json and
                                          SHA256SUMS, alamo-<sha>.zip, and the PDBs in alamo-<sha>-pdb.zip
  dev.cmd launcher | launcher-test        build or test the Rust Command Center launcher
  dev.cmd selftest                        this script's own unit tests

check, test and suite run their tests inside one of the machine's test slots (tools\\test_slots.py,
ALAMO_TEST_SLOTS; README.md "Test slots"): a second job waits and says which job it waits for.

dev.cmd loads dev.local.cmd (machine settings such as ALAMO_FOC_DATA) and passes every argument here unchanged.
Arguments reach CMake, CTest and tools/test_suite.py as exact argument lists. cmd reads | & < > before this script
does, so the double quotes around such an argument must reach cmd: in cmd, dev.cmd test -R "^(a|b)$"; in PowerShell,
.\\dev.cmd test -R '"^(a|b)$"'.

Every step's output goes to the console and to build\\logs\\dev-<time>-<pid>-<command>.log. A failed step prints the step,
the first meaningful diagnostic, the toolchain and source identity, a reproduction command, the elapsed time and the
log path; build\\logs\\dev-last.json holds the same for CI and agents. Standard library only.
"""

from __future__ import annotations

import contextlib
import datetime
import difflib
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I leaves the script's folder off sys.path
import test_slots  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLCHAIN = os.path.join(ROOT, "toolchain")
PRESETS_FILE = os.path.join(ROOT, "CMakePresets.json")
DEFAULT_PRESET = "fast"
LOG_DIR = os.path.join(ROOT, "build", "logs")

# First meaningful diagnostic, most specific first.
_DIAG = [
    re.compile(r"\berror (C|LNK|RC|D)\d{4}\b"),
    re.compile(r"\bfatal error\b", re.I),
    re.compile(r"^CMake Error\b"),
    re.compile(r"^\s*(FAIL|FAILED)\b[: ]"),
    re.compile(r"\*\*\*(Failed|Timeout|Exception)"),
    re.compile(r"^Total: .*(\b[1-9]\d* failed|required entries missing)"),
    re.compile(r"^Traceback \(most recent call last\)"),
    re.compile(r"(^|\s)error[: ]", re.I),
]


class StepFailed(Exception):
    def __init__(self, step: str, code: int, lines: list[str], remediation: str = ""):
        super().__init__(step)
        self.step, self.code, self.lines, self.remediation = step, code, lines, remediation


def first_diagnostic(lines: list[str]) -> str:
    for pat in _DIAG:
        for line in lines:
            if pat.search(line):
                return line.strip()
    tail = [line.strip() for line in lines if line.strip()]
    return tail[-1] if tail else "(no output)"


# ------------------------------------------------------------------------------------------------- presets

def load_presets(path: str = PRESETS_FILE) -> dict[str, str]:
    """Visible configure preset name -> absolute build directory."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    by_name = {p["name"]: p for p in data.get("configurePresets", [])}

    def binary_dir(p: dict) -> str | None:
        if "binaryDir" in p:
            return p["binaryDir"]
        for parent in ([p["inherits"]] if isinstance(p.get("inherits"), str) else p.get("inherits", [])):
            d = binary_dir(by_name[parent])
            if d:
                return d
        return None

    out = {}
    for name, p in by_name.items():
        d = binary_dir(p)
        if p.get("hidden") or not d:
            continue
        out[name] = os.path.normpath(d.replace("${sourceDir}", ROOT))
    return out


def take_preset(args: list[str]) -> tuple[str, list[str]]:
    """Removes a leading --preset P (or --preset=P) and returns (preset, remaining args)."""
    if args and args[0] == "--preset":
        if len(args) < 2:
            raise SystemExit("dev.cmd: --preset needs a name")
        return args[1], args[2:]
    if args and args[0].startswith("--preset="):
        return args[0].split("=", 1)[1], args[1:]
    return DEFAULT_PRESET, args


def build_dir(preset: str) -> str:
    presets = load_presets()
    if preset not in presets:
        raise SystemExit(f"dev.cmd: unknown preset '{preset}' (CMakePresets.json has: {', '.join(sorted(presets))})")
    return presets[preset]


# ------------------------------------------------------------------------------------------------- environment

def _find_key(env: dict, name: str) -> str:
    return next((k for k in env if k.upper() == name.upper()), name)


def msvc_env() -> dict[str, str]:
    """The current environment plus toolchain\\msvc\\setup_x64.bat, the local CMake / Ninja and the venv."""
    bat = os.path.join(TOOLCHAIN, "msvc", "setup_x64.bat")
    if not os.path.isfile(bat):
        raise SystemExit(f"dev.cmd: {bat} not found. Run toolchain\\2_setup.cmd (dev.cmd doctor lists what is missing).")
    p = subprocess.run(f'"{bat}" >nul && set', shell=True, capture_output=True)
    if p.returncode != 0:
        raise SystemExit(f"dev.cmd: {bat} failed:\n{p.stdout.decode('oem', 'replace')}{p.stderr.decode('oem', 'replace')}")
    env: dict[str, str] = {}
    for line in p.stdout.decode("oem", "replace").splitlines():
        k, sep, v = line.partition("=")
        if sep and k:
            env[k] = v
    path_key = _find_key(env, "PATH")
    extra = [os.path.join(TOOLCHAIN, "cmake", "bin"), os.path.join(TOOLCHAIN, "venv", "Scripts")]
    env[path_key] = os.pathsep.join([d for d in extra if os.path.isdir(d)] + [env.get(path_key, "")])
    env["VSLANG"] = "1033"  # English compiler messages, so diagnostics match the patterns above
    return env


def resolve(cmd: list[str], env: dict[str, str] | None) -> list[str]:
    """Windows looks a bare program name up in this process's PATH, not the child's; look it up in the child's."""
    if not env or not cmd or os.path.dirname(cmd[0]):
        return cmd
    found = shutil.which(cmd[0], path=env.get(_find_key(env, "PATH"), ""))
    return [found or cmd[0], *cmd[1:]]


def test_env(env: dict[str, str], run_id: str) -> dict[str, str]:
    """Tests write temporary files under build\\tmp\\<run>, never the user's TEMP, and never into another run's."""
    tmp = os.path.join(ROOT, "build", "tmp", run_id)
    os.makedirs(tmp, exist_ok=True)
    env = dict(env)
    env[_find_key(env, "TEMP")] = tmp
    env[_find_key(env, "TMP")] = tmp
    return env


def tool_identity(env: dict[str, str]) -> dict[str, str]:
    def first_line(cmd: list[str]) -> str:
        try:
            p = subprocess.run(resolve(cmd, env), capture_output=True, text=True, env=env, cwd=ROOT)
            return (p.stdout or p.stderr).strip().splitlines()[0]
        except (OSError, IndexError):
            return "unavailable"

    sha = first_line(["git", "rev-parse", "--short=12", "HEAD"])
    dirty = subprocess.run(["git", "status", "--porcelain"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    return {
        "msvc": env.get(_find_key(env, "VCToolsVersion"), "unknown"),
        "windows_sdk": env.get(_find_key(env, "WindowsSDKVersion"), "unknown").rstrip("\\"),
        "cmake": first_line(["cmake", "--version"]).replace("cmake version ", ""),
        "source": sha + (" (uncommitted changes)" if dirty else ""),
    }


# ------------------------------------------------------------------------------------------------- running steps

class Runner:
    def __init__(self, command: str, argv: list[str]):
        self.command, self.argv = command, argv
        self.start = time.monotonic()
        os.makedirs(LOG_DIR, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.run_id = f"{stamp}-{os.getpid()}"  # this run's log, temp and result folders: unique per process
        self.log_path = os.path.join(LOG_DIR, f"dev-{self.run_id}-{command}.log")
        self.log = open(self.log_path, "w", encoding="utf-8", newline="\n")
        self.env: dict[str, str] | None = None

    def run(self, step: str, cmd: list[str], env: dict[str, str] | None = None, cwd: str = ROOT) -> None:
        self.log.write(f"\n=== {step}: {subprocess.list2cmdline(cmd)}\n")
        self.log.flush()
        print(f"dev.cmd: {step}", flush=True)
        lines: list[str] = []
        cmd = resolve(cmd, env)
        try:
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=cwd)
        except OSError as e:
            raise StepFailed(step, 127, [f"cannot run {cmd[0]}: {e}"]) from None
        assert p.stdout is not None
        for raw in p.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            lines.append(line)
            print(line, flush=True)
            self.log.write(line + "\n")
        code = p.wait()
        if code != 0:
            raise StepFailed(step, code, lines)

    def finish(self, failure: StepFailed | None) -> int:
        elapsed = round(time.monotonic() - self.start, 1)
        ident = tool_identity(self.env) if self.env else {}
        rel_log = os.path.relpath(self.log_path, ROOT)
        result = {
            "command": self.command, "args": self.argv, "ok": failure is None, "elapsed_s": elapsed,
            "log": rel_log, "toolchain": ident,
            "reproduce": "dev.cmd " + subprocess.list2cmdline([self.command, *self.argv]),
        }
        if failure:
            result.update({"failed_step": failure.step, "exit_code": failure.code,
                           "first_diagnostic": first_diagnostic(failure.lines), "remediation": failure.remediation})
        self.log.write("\n" + json.dumps(result, indent=1) + "\n")
        self.log.close()
        with open(os.path.join(LOG_DIR, "dev-last.json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=1)
        if failure:
            print(f"\ndev.cmd: FAILED at step '{failure.step}' (exit {failure.code}) after {elapsed} s")
            print(f"  first diagnostic: {result['first_diagnostic']}")
            if failure.remediation:
                print(f"  remediation: {failure.remediation}")
            if ident:
                print(f"  toolchain: MSVC {ident['msvc']}, Windows SDK {ident['windows_sdk']}, CMake {ident['cmake']}")
                print(f"  source: {ident['source']}")
            print(f"  reproduce: {result['reproduce']}")
            print(f"  log: {rel_log}")
            return failure.code or 1
        print(f"dev.cmd: {self.command} passed in {elapsed} s (log: {rel_log})")
        return 0


def configure(r: Runner, preset: str, fresh: bool = False) -> str:
    bdir = build_dir(preset)
    r.run(f"configure ({preset})", ["cmake", "--preset", preset, *(["--fresh"] if fresh else [])], r.env)
    return bdir


def ninja_targets(r: Runner, bdir: str) -> set[str]:
    p = subprocess.run(resolve(["cmake", "--build", bdir, "--", "-t", "targets", "all"], r.env), capture_output=True,
                       text=True, env=r.env, cwd=ROOT)
    return {line.split(":", 1)[0].strip() for line in p.stdout.splitlines() if ":" in line}


def build(r: Runner, bdir: str, target: str | None, clean: bool = False) -> None:
    if target:
        known = ninja_targets(r, bdir)
        if known and target not in known:
            near = difflib.get_close_matches(target, sorted(known), n=5)
            raise StepFailed("check target", 2, [f"error: no build target '{target}'"
                                                 + (f"; close matches: {', '.join(near)}" if near else "")])
    cmd = ["cmake", "--build", bdir, *(["--clean-first"] if clean else []), *(["--target", target] if target else [])]
    r.run(f"build {target or 'all'}", cmd, r.env)


def run_results_dir(bdir: str, run_id: str) -> str:
    """Where a run's own copy of its reports goes; <build>\\test-results keeps the latest run's."""
    return os.path.join(bdir, "test-results", "runs", run_id)


_RUN_ID = re.compile(r"^\d{8}-\d{6}-\d+$")


def prune_runs(parent: str, keep: int = 20) -> None:
    """Removes all but the newest `keep` run folders (named <date>-<time>-<pid>) under parent."""
    try:
        runs = sorted(n for n in os.listdir(parent) if _RUN_ID.match(n) and os.path.isdir(os.path.join(parent, n)))
    except OSError:
        return
    for n in runs[:-keep] if len(runs) > keep else []:
        shutil.rmtree(os.path.join(parent, n), ignore_errors=True)


@contextlib.contextmanager
def test_slot(r: Runner, bdir: str):
    """Holds one of the machine's test slots while the tests run, so concurrent jobs wait instead of timing each other
    out (dev.cmd check, test and suite; tools/test_slots.py; README.md "Test slots"). Prunes old run folders."""
    slot = test_slots.acquire(f"dev.cmd {subprocess.list2cmdline([r.command, *r.argv])}")
    prune_runs(os.path.join(ROOT, "build", "tmp"))
    prune_runs(os.path.join(bdir, "test-results", "runs"))
    try:
        yield
    finally:
        slot.release()


def test_suite(r: Runner, bdir: str, args: list[str]) -> None:
    py = os.path.join(TOOLCHAIN, "venv", "Scripts", "python.exe")
    suite = args[0] if args and not args[0].startswith("-") else "synthetic"
    rest = args[1:] if args and not args[0].startswith("-") else args
    with test_slot(r, bdir):
        r.run(f"suite {suite}", [py, "-I", os.path.join(ROOT, "tools", "test_suite.py"), "--build", bdir,
                                 "--suite", suite, "--archive", run_results_dir(bdir, r.run_id), *rest],
              test_env(r.env or {}, r.run_id))


# ------------------------------------------------------------------------------------------------- commands

def is_elevated() -> bool:
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except (AttributeError, OSError):
        return False


def elevation_warning(elevated: bool) -> str:
    """What doctor prints about an elevated shell, given whether it runs elevated."""
    if not elevated:
        return ""
    return ("warning: this shell is elevated (Administrator). The Vulkan loader ignores VK_LAYER_PATH in an elevated\n"
            "  process, so the pinned validation layer in toolchain\\vvl\\bin cannot be enabled and the GPU tests'\n"
            "  validation-layer check fails (alamo_rhi_gpu_tests, alamo_shaderxlat_gpu_tests).\n"
            "  Fix: run dev.cmd from an unelevated shell, or register toolchain\\vvl\\bin\\VkLayer_khronos_validation.json\n"
            "  under HKLM\\SOFTWARE\\Khronos\\Vulkan\\ExplicitLayers (as .github/workflows/landing.yml does).")


def cmd_doctor(r: Runner, args: list[str]) -> None:
    engine_only = "--engine-only" in args
    print(f"python: {sys.version.split()[0]} ({sys.executable})")
    warning = elevation_warning(is_elevated())
    if warning:
        print(warning)
    check = [os.path.join(TOOLCHAIN, "2_setup.cmd"), "--check", *(["--engine-only"] if engine_only else [])]
    try:
        r.run("toolchain check", check)
    except StepFailed as e:
        e.remediation = "run toolchain\\2_setup.cmd" + (" --engine-only" if engine_only else "")
        raise
    r.env = msvc_env()
    for k, v in tool_identity(r.env).items():
        print(f"{k}: {v}")
    cl = shutil.which("cl", path=r.env.get(_find_key(r.env, "PATH")))
    if not cl:
        raise StepFailed("compiler", 1, ["error: cl.exe is not on PATH after toolchain\\msvc\\setup_x64.bat"],
                         "delete toolchain\\msvc and run toolchain\\2_setup.cmd")
    print(f"cl: {cl}")
    for var, what in (("ALAMO_FOC_DATA", "retail suite (Forces of Corruption)"), ("ALAMO_EAW_DATA", "retail suite (base game)"),
                      ("ALAMO_Q2_TRACES", "traces suite")):
        v = os.environ.get(var, "")
        state = "not set" if not v else ("ok" if os.path.isdir(v) else f"MISSING folder {v}")
        print(f"{var}: {state} ({what})")
    for rel, what in (("extracted/foc_data_text.zip", "XML conformance checks"),
                      ("extracted/format_examples", "glTF reference checks")):
        print(f"{rel}: {'ok' if os.path.exists(os.path.join(ROOT, rel)) else 'absent'} ({what})")
    print("vulkan: checked per test by alamo_test_gate (vulkan suite)")


def cmd_configure(r: Runner, args: list[str]) -> None:
    preset, rest = take_preset(args)
    if rest:
        raise SystemExit(f"dev.cmd configure: unexpected arguments {rest}")
    r.env = msvc_env()
    configure(r, preset)


def cmd_build(r: Runner, args: list[str]) -> None:
    preset, rest = take_preset(args)
    if rest and rest[0] == "--preset":
        raise SystemExit("dev.cmd build: put --preset before the target")
    target = rest[0] if rest else None
    if len(rest) > 1:
        raise SystemExit(f"dev.cmd build: one target at most, got {rest}")
    r.env = msvc_env()
    build(r, configure(r, preset), target)


def cmd_check(r: Runner, args: list[str]) -> None:
    preset, target, quick = DEFAULT_PRESET, None, False
    it = iter(args)
    for a in it:
        if a == "--preset":
            preset = next(it, "")
        elif a == "--target":
            target = next(it, "")
        elif a == "--quick":
            quick = True
        else:
            raise SystemExit(f"dev.cmd check: unknown argument '{a}'")
    plan = check_plan(preset, target, quick)
    r.env = msvc_env()
    bdir = configure(r, preset, fresh=plan["fresh"])
    build(r, bdir, target, clean=plan["clean"])
    if plan["analyze"]:
        py = os.path.join(TOOLCHAIN, "venv", "Scripts", "python.exe")
        try:
            r.run("analyze findings", [py, "-I", os.path.join(ROOT, "tools", "analyze_report.py"), "check", "--build",
                                       bdir], r.env)
        except StepFailed as e:
            e.remediation = ("fix the new /analyze findings (or, for a finding accepted on purpose, add it to "
                             "tests/analyze_baseline.txt)")
            raise
    if plan["suite"] is not None:
        test_suite(r, bdir, plan["suite"])


def check_plan(preset: str, target: str | None, quick: bool) -> dict:
    """What dev.cmd check does for a preset: fresh configure, clean build, the suite arguments, the /analyze report."""
    if preset == "analyze":
        if target or quick:
            raise SystemExit("dev.cmd check: --target and --quick do not apply to --preset analyze")
        return {"fresh": False, "clean": False, "suite": None, "analyze": True}
    if preset not in ("fast", "full", "asan"):
        raise SystemExit(f"dev.cmd check: preset '{preset}' has no check yet (fast, full, analyze, asan)")
    full = preset == "full"
    suite = ["all"] if full else ["synthetic"]
    if quick:
        suite.append("--fast")
    ctest = ["-LE", "^no_asan$"] if preset == "asan" else []  # deliberate crashes ASan would intercept
    if target:
        ctest += ["-R", "^" + re.escape(target)]
    if ctest:
        suite += ["--", *ctest]
    return {"fresh": full, "clean": full, "suite": suite, "analyze": False}


def cmd_test(r: Runner, args: list[str]) -> None:
    preset, rest = take_preset(args)
    bdir = build_dir(preset)
    if not os.path.isfile(os.path.join(bdir, "CTestTestfile.cmake")):
        raise SystemExit(f"dev.cmd: {bdir} is not configured. Run dev.cmd build first.")
    r.env = msvc_env()
    with test_slot(r, bdir):
        r.run("ctest", ["ctest", "--test-dir", bdir, "--output-on-failure", "--no-tests=error", *rest],
              test_env(r.env, r.run_id))


def cmd_suite(r: Runner, args: list[str]) -> None:
    # The suite name may come before or after --preset.
    name: list[str] = []
    if args and not args[0].startswith("-"):
        name, args = [args[0]], args[1:]
    preset, rest = take_preset(args)
    bdir = build_dir(preset)
    if not os.path.isfile(os.path.join(bdir, "CTestTestfile.cmake")):
        raise SystemExit(f"dev.cmd: {bdir} is not configured. Run dev.cmd build first.")
    r.env = msvc_env()
    test_suite(r, bdir, name + rest)


def cmd_launcher(r: Runner, args: list[str], verb: str) -> None:
    r.run(f"cargo {verb}", [os.path.join(ROOT, "launcher", "cargo.cmd"), verb, "--release", "--locked", *args])


# ------------------------------------------------------------------------------------------------- packaging (#29)

PACKAGE_PRESET = "package"
PACKAGE_TARGETS = ("alamo_game", "alamo_headless")
# (path in the package, source): "bin:<name>" is the build tree's bin folder; anything else is relative to the
# checkout, and one "*" matches a versioned toolchain folder. #27 decision 3 lists what ships.
PACKAGE_FILES = (
    ("alamo_game.exe", "bin:alamo_game.exe"),
    ("alamo_headless.exe", "bin:alamo_headless.exe"),
    ("dxcompiler.dll", "bin:dxcompiler.dll"),
    ("dxil.dll", "bin:dxil.dll"),
    ("avcodec-62.dll", "bin:avcodec-62.dll"),
    ("avformat-62.dll", "bin:avformat-62.dll"),
    ("avutil-60.dll", "bin:avutil-60.dll"),
    ("amd_fidelityfx_loader_vk.dll", "bin:amd_fidelityfx_loader_vk.dll"),
    ("amd_fidelityfx_upscaler_vk.dll", "bin:amd_fidelityfx_upscaler_vk.dll"),
    ("libxess.dll", "bin:libxess.dll"),
    ("licenses/LICENSE.txt", "LICENSE"),
    ("licenses/lua-COPYRIGHT.txt", "toolchain/lua-*/COPYRIGHT"),
    ("licenses/miniaudio-LICENSE.txt", "toolchain/miniaudio-*/LICENSE"),
    ("licenses/smaa-LICENSE.txt", "engine/render/third_party/smaa/LICENSE.txt"),
    ("licenses/ffmpeg-COPYING.LGPLv2.1.txt", "bin:COPYING.LGPLv2.1"),
    ("licenses/amd_fidelityfx_license.md", "bin:amd_fidelityfx_license.md"),
    ("licenses/intel_xess_license.txt", "bin:intel_xess_license.txt"),
    ("licenses/intel_xess_third-party-programs.txt", "bin:intel_xess_third-party-programs.txt"),
    ("licenses/dxc-LICENSE-LLVM.txt", "toolchain/dxc/LICENSE-LLVM.txt"),
    ("licenses/dxc-LICENSE-MIT.txt", "toolchain/dxc/LICENSE-MIT.txt"),
    ("licenses/dxc-LICENSE-MS.txt", "toolchain/dxc/LICENSE-MS.txt"),
)
# The symbols go in their own archive, never the package (docs/DECISIONS.md "Release symbols").
PACKAGE_SYMBOLS = (("alamo_game.pdb", "bin:alamo_game.pdb"), ("alamo_headless.pdb", "bin:alamo_headless.pdb"))
# Never in the package: fxc and its DLL (docs/DECISIONS.md "fxc ships in test builds"), symbols, Command Center.
NEVER_PACKAGED = re.compile(r"(^|/)(fxc\.exe|d3dcompiler_47\.dll|command-center\.exe)$|\.pdb$", re.I)


class PackageError(Exception):
    pass


def resolve_package_files(entries, bin_dir: str, root: str, symbols: bool = False) -> list[tuple[str, str]]:
    """(package path, source file) for every entry; every missing source is named at once. Outside the symbols
    archive, a never-packaged name is refused."""
    out, missing = [], []
    for dest, src in entries:
        if not symbols and NEVER_PACKAGED.search(dest):
            raise PackageError(f"{dest} must never be in the package")
        if src.startswith("bin:"):
            found = [os.path.join(bin_dir, src[4:])]
        else:
            found = sorted(glob.glob(os.path.join(root, src.replace("/", os.sep))))
        if len(found) != 1 or not os.path.isfile(found[0]):
            missing.append(f"{dest} ({src}{': ' + str(len(found)) + ' matches' if len(found) > 1 else ''})")
            continue
        out.append((dest, found[0]))
    if missing:
        raise PackageError("missing from the build: " + ", ".join(missing))
    return out


def file_sha256(path: str) -> tuple[str, int]:
    h, size = hashlib.sha256(), 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def package_manifest(commit: str, toolchain: dict, deps: dict, files, symbols) -> dict:
    def digests(items):
        return {dest: dict(zip(("sha256", "bytes"), file_sha256(src))) for dest, src in items}

    return {"commit": commit, "toolchain": toolchain, "dependencies": deps, "files": digests(files),
            "symbols": digests(symbols)}


def sha256sums(digests: dict[str, str]) -> str:
    """sha256sum's format (`sha256sum -c`, PowerShell Get-FileHash), sorted by path."""
    return "".join(f"{digests[p]}  {p}\n" for p in sorted(digests))


def dependency_versions(toolchain: str) -> dict[str, str]:
    """Versions from the toolchain's versioned folders (name-1.2.3) and the pinned DXC release."""
    out = {}
    for name in os.listdir(toolchain):
        m = re.fullmatch(r"([a-z]+)-(\d[\w.]*)", name)
        if m and os.path.isdir(os.path.join(toolchain, name)):
            out[m.group(1)] = m.group(2)
    try:
        with open(os.path.join(toolchain, "3_renderer_deps.cmd"), encoding="utf-8") as f:
            m = re.search(r"DirectXShaderCompiler/releases/download/v([\w.]+)/", f.read())
        if m:
            out["dxc"] = m.group(1)
    except OSError:
        pass
    return dict(sorted(out.items()))


def package_refusal(commit: str, head: str, status: str, out_dir: str, root: str) -> str | None:
    """Why dev.cmd package must not run, or None. #27 decisions 2 and 3: the exact, clean HEAD, staged outside."""
    if commit != head:
        return f"--commit {commit[:12]} is not HEAD ({head[:12]}): check that commit out first"
    if status.strip():
        return "the tree has uncommitted changes; a package is built from a clean commit"
    inside = os.path.normcase(os.path.abspath(out_dir) + os.sep).startswith(os.path.normcase(os.path.abspath(root))
                                                                             + os.sep)
    if inside:
        return f"--out {out_dir} is inside the checkout; a package is staged outside the checkout"
    return None


def git_out(args: list[str]) -> str:
    p = subprocess.run(["git", *args], capture_output=True, text=True, cwd=ROOT)
    if p.returncode:
        raise SystemExit(f"dev.cmd package: git {' '.join(args)} failed: {p.stderr.strip()}")
    return p.stdout.strip()


def cmd_package(r: Runner, args: list[str]) -> None:
    commit = out = None
    it = iter(args)
    for a in it:
        if a == "--commit":
            commit = next(it, "")
        elif a == "--out":
            out = next(it, "")
        else:
            raise SystemExit(f"dev.cmd package: unknown argument '{a}'")
    if not commit:
        raise SystemExit("dev.cmd package: --commit <sha> is required (the commit checked out, HEAD)")
    commit = git_out(["rev-parse", "--verify", commit + "^{commit}"])
    out = os.path.abspath(out or os.path.join(os.environ.get("LOCALAPPDATA", os.path.dirname(ROOT)), "alamo-dev",
                                              "packages"))
    refusal = package_refusal(commit, git_out(["rev-parse", "HEAD"]), git_out(["status", "--porcelain"]), out, ROOT)
    if refusal:
        raise SystemExit(f"dev.cmd package: {refusal}")
    r.env = msvc_env()
    bdir = configure(r, PACKAGE_PRESET)
    for target in PACKAGE_TARGETS:
        build(r, bdir, target)
    bin_dir = os.path.join(bdir, "bin")
    try:
        files = resolve_package_files(PACKAGE_FILES, bin_dir, ROOT)
        symbols = resolve_package_files(PACKAGE_SYMBOLS, bin_dir, ROOT, symbols=True)
    except PackageError as e:
        raise StepFailed("stage package", 1, [f"error: {e}"],
                         "a listed file is missing: check the toolchain (dev.cmd doctor) and the build") from None
    name = f"alamo-{commit[:12]}"
    stage = os.path.join(out, name)
    shutil.rmtree(stage, ignore_errors=True)
    for dest, src in files:
        os.makedirs(os.path.dirname(os.path.join(stage, dest)), exist_ok=True)
        shutil.copy2(src, os.path.join(stage, dest))
    manifest = package_manifest(commit, tool_identity(r.env), dependency_versions(TOOLCHAIN), files, symbols)
    manifest["toolchain"]["source"] = commit
    with open(os.path.join(stage, "MANIFEST.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    digests = {dest: d["sha256"] for dest, d in manifest["files"].items()}
    digests["MANIFEST.json"] = file_sha256(os.path.join(stage, "MANIFEST.json"))[0]
    with open(os.path.join(stage, "SHA256SUMS"), "w", encoding="utf-8", newline="\n") as f:
        f.write(sha256sums(digests))
    package_zip, symbols_zip = os.path.join(out, name + ".zip"), os.path.join(out, name + "-pdb.zip")
    with zipfile.ZipFile(package_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for dest in sorted([*digests, "SHA256SUMS"]):
            z.write(os.path.join(stage, dest), f"{name}/{dest}")
    with zipfile.ZipFile(symbols_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for dest, src in symbols:
            z.write(src, f"{name}-pdb/{dest}")
    text = f"dev.cmd: package staged in {stage}\n  package: {package_zip}\n  symbols: {symbols_zip}"
    print(text)
    r.log.write(text + "\n")


COMMANDS = {
    "package": cmd_package,
    "doctor": cmd_doctor,
    "configure": cmd_configure,
    "build": cmd_build,
    "check": cmd_check,
    "test": cmd_test,
    "suite": cmd_suite,
    "launcher": lambda r, a: cmd_launcher(r, a, "build"),
    "launcher-test": lambda r, a: cmd_launcher(r, a, "test"),
}


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0 if argv else 2
    command, args = argv[0], argv[1:]
    if command == "selftest":
        return selftest()
    if command not in COMMANDS:
        print(f"dev.cmd: unknown command '{command}' ({', '.join([*COMMANDS, 'selftest'])})")
        return 2
    r = Runner(command, args)
    try:
        COMMANDS[command](r, args)
    except StepFailed as e:
        return r.finish(e)
    return r.finish(None)


# ------------------------------------------------------------------------------------------------- self-test

class _Tests(unittest.TestCase):
    def test_first_diagnostic_prefers_compiler_error(self):
        lines = ["[3/9] Building CXX object a.obj", "FAILED: a.obj", "a.cpp(3): error C2065: 'x': undeclared identifier",
                 "ninja: build stopped: subcommand failed."]
        self.assertIn("error C2065", first_diagnostic(lines))

    def test_first_diagnostic_linker_and_cmake(self):
        self.assertIn("LNK2019", first_diagnostic(["x", "b.obj : error LNK2019: unresolved external symbol f"]))
        self.assertTrue(first_diagnostic(["-- ok", "CMake Error at x.cmake:3 (message):"]).startswith("CMake Error"))

    def test_first_diagnostic_test_failure_and_fallback(self):
        self.assertIn("Failed", first_diagnostic(["1/2 Test #1: a ....***Failed  0.1 sec"]))
        self.assertEqual(first_diagnostic(["", "last words", ""]), "last words")
        self.assertEqual(first_diagnostic([]), "(no output)")

    def test_first_diagnostic_suite_summary_not_command_line(self):
        lines = ["+ ctest.EXE --test-dir build --output-on-failure --no-tests=error -L ^synthetic$",
                 "Total: 204 passed, 0 failed, 0 skipped, 3 required entries missing"]
        self.assertTrue(first_diagnostic(lines).startswith("Total: 204 passed"))

    def test_check_plan_per_preset(self):
        self.assertEqual(check_plan("fast", None, False),
                         {"fresh": False, "clean": False, "suite": ["synthetic"], "analyze": False})
        self.assertEqual(check_plan("fast", "alamo_x_tests", True)["suite"],
                         ["synthetic", "--fast", "--", "-R", "^alamo_x_tests"])
        self.assertEqual(check_plan("full", None, False),
                         {"fresh": True, "clean": True, "suite": ["all"], "analyze": False})
        # analyze: an incremental /analyze build compared with the baseline; no suite (the binaries are fast's).
        self.assertEqual(check_plan("analyze", None, False),
                         {"fresh": False, "clean": False, "suite": None, "analyze": True})
        # asan: an incremental AddressSanitizer build running the synthetic suite (--quick and --target apply).
        # Tests labelled no_asan (deliberate crashes that ASan intercepts) are left out.
        self.assertEqual(check_plan("asan", None, False),
                         {"fresh": False, "clean": False, "suite": ["synthetic", "--", "-LE", "^no_asan$"],
                          "analyze": False})
        self.assertEqual(check_plan("asan", "alamo_x_tests", True)["suite"],
                         ["synthetic", "--fast", "--", "-LE", "^no_asan$", "-R", "^alamo_x_tests"])
        for bad in (("nope", None, False), ("analyze", "alamo_x_tests", False), ("analyze", None, True)):
            with self.assertRaises(SystemExit):
                check_plan(*bad)

    def test_analyze_preset_has_its_own_build_folder(self):
        presets = load_presets()
        self.assertEqual(presets["analyze"], os.path.join(ROOT, "build-analyze"))
        self.assertEqual(presets["asan"], os.path.join(ROOT, "build-asan"))
        self.assertEqual(len(set(presets.values())), len(presets))

    def test_each_run_has_its_own_temp_and_result_folders(self):
        a, b = test_env({"PATH": "x"}, "20261122-010203-111"), test_env({"PATH": "x"}, "20261122-010203-222")
        self.assertEqual(a["TEMP"], os.path.join(ROOT, "build", "tmp", "20261122-010203-111"))
        self.assertEqual(a["TEMP"], a["TMP"])
        self.assertNotEqual(a["TEMP"], b["TEMP"])
        self.assertEqual(run_results_dir("E:\\b", "20261122-010203-111"),
                         os.path.join("E:\\b", "test-results", "runs", "20261122-010203-111"))

    def test_old_run_folders_are_pruned(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            names = [f"20261122-0000{i:02d}-{100 + i}" for i in range(25)] + ["keep-me"]
            for n in names:
                os.makedirs(os.path.join(d, n))
            prune_runs(d, keep=20)
            left = sorted(os.listdir(d))
            self.assertEqual(left, sorted(names[5:]))  # the 5 oldest runs go; a folder not named like a run stays

    def test_take_preset(self):
        self.assertEqual(take_preset(["--preset", "full", "-R", "x"]), ("full", ["-R", "x"]))
        self.assertEqual(take_preset(["--preset=full"]), ("full", []))
        self.assertEqual(take_preset(["-R", "^(a|b)$"]), (DEFAULT_PRESET, ["-R", "^(a|b)$"]))

    def test_presets_file(self):
        presets = load_presets()
        self.assertEqual(presets["fast"], os.path.join(ROOT, "build"))
        self.assertIn("full", presets)
        self.assertNotIn("base", presets)

    def test_resolve_uses_child_path(self):
        env = {"Path": os.path.dirname(sys.executable)}
        exe = os.path.splitext(os.path.basename(sys.executable))[0]
        self.assertEqual(os.path.normcase(resolve([exe, "-V"], env)[0]), os.path.normcase(sys.executable))
        self.assertEqual(resolve(["C:\\x\\y.exe"], env), ["C:\\x\\y.exe"])

    def test_elevation_warning_names_cause_and_both_fixes(self):
        text = elevation_warning(True)
        self.assertIn("warning", text.lower())
        self.assertIn("VK_LAYER_PATH", text)
        self.assertIn("unelevated", text)
        self.assertIn("HKLM\\SOFTWARE\\Khronos\\Vulkan\\ExplicitLayers", text)

    def test_no_elevation_warning_in_a_normal_shell(self):
        self.assertEqual(elevation_warning(False), "")

    def test_unknown_command_and_usage(self):
        self.assertEqual(main(["no-such-command"]), 2)

    # -------------------------------------------------------------------------------- dev.cmd package (#29)

    def _fake_tree(self, d: str, skip: tuple[str, ...] = ()) -> tuple[str, str]:
        """A source root and bin folder holding every file the package lists, except `skip`."""
        root, bdir = os.path.join(d, "src"), os.path.join(d, "bin")
        for dest, src in (*PACKAGE_FILES, *PACKAGE_SYMBOLS):
            if dest in skip:
                continue
            path = os.path.join(bdir, src[4:]) if src.startswith("bin:") else os.path.join(
                root, src.replace("*", "1.0").replace("/", os.sep))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(dest)
        return root, bdir

    def test_package_lists_the_runtime_and_licence_notices(self):
        dests = {d for d, _ in PACKAGE_FILES}
        for name in ("alamo_game.exe", "alamo_headless.exe", "dxcompiler.dll", "dxil.dll", "avcodec-62.dll",
                     "avformat-62.dll", "avutil-60.dll", "amd_fidelityfx_loader_vk.dll",
                     "amd_fidelityfx_upscaler_vk.dll", "libxess.dll"):
            self.assertIn(name, dests)
        notices = " ".join(d for d in dests if d.startswith("licenses/"))
        self.assertIn("licenses/LICENSE.txt", dests)  # the project's own
        for part in ("lua", "miniaudio", "smaa", "ffmpeg", "fidelityfx", "xess", "dxc"):
            self.assertIn(part, notices.lower())
        self.assertEqual({d for d, _ in PACKAGE_SYMBOLS}, {"alamo_game.pdb", "alamo_headless.pdb"})

    def test_package_never_holds_fxc_d3dcompiler_pdbs_or_command_center(self):
        for d, _ in PACKAGE_FILES:
            self.assertIsNone(NEVER_PACKAGED.search(d), d)
        for bad in ("fxc.exe", "D3DCompiler_47.dll", "alamo_game.pdb", "x/command-center.exe"):
            self.assertIsNotNone(NEVER_PACKAGED.search(bad), bad)
        with self.assertRaises(PackageError):
            resolve_package_files((("fxc.exe", "bin:fxc.exe"),), "E:\\nowhere", "E:\\nowhere")

    def test_package_resolves_every_file_and_names_all_missing(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            root, bdir = self._fake_tree(d)
            files = resolve_package_files(PACKAGE_FILES, bdir, root)
            self.assertEqual([dest for dest, _ in files], [dest for dest, _ in PACKAGE_FILES])
            self.assertTrue(all(os.path.isfile(src) for _, src in files))
        with tempfile.TemporaryDirectory() as d:
            root, bdir = self._fake_tree(d, skip=("libxess.dll", "licenses/lua-COPYRIGHT.txt"))
            with self.assertRaises(PackageError) as e:
                resolve_package_files(PACKAGE_FILES, bdir, root)
            self.assertIn("libxess.dll", str(e.exception))
            self.assertIn("lua-COPYRIGHT.txt", str(e.exception))

    def test_package_manifest_and_checksums(self):
        import hashlib
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.exe"), os.path.join(d, "b.txt")
            with open(a, "wb") as f:
                f.write(b"abc")
            with open(b, "wb") as f:
                f.write(b"")
            m = package_manifest("0123456789abcdef0123456789abcdef01234567", {"msvc": "14.44"}, {"lua": "5.0.2"},
                                 [("alamo.exe", a), ("licenses/b.txt", b)], [("alamo.pdb", b)])
            self.assertEqual(m["commit"], "0123456789abcdef0123456789abcdef01234567")
            self.assertEqual(m["toolchain"], {"msvc": "14.44"})
            self.assertEqual(m["dependencies"], {"lua": "5.0.2"})
            # Expected digests: SHA-256 of "abc" and of the empty string (FIPS 180-2 examples).
            abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
            empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            self.assertEqual(m["files"]["alamo.exe"], {"sha256": abc, "bytes": 3})
            self.assertEqual(m["files"]["licenses/b.txt"], {"sha256": empty, "bytes": 0})
            self.assertEqual(m["symbols"]["alamo.pdb"]["sha256"], empty)
            self.assertEqual(hashlib.sha256(b"abc").hexdigest(), abc)
            sums = sha256sums({"alamo.exe": abc, "MANIFEST.json": empty})
            self.assertEqual(sums, f"{empty}  MANIFEST.json\n{abc}  alamo.exe\n")

    def test_package_dependency_versions_from_the_toolchain(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            for name in ("lua-5.0.2", "miniaudio-0.11.25", "ffmpeg-8.1.3", "fidelityfx-2.4.0", "xess-3.0.2", "dxc",
                         "venv", "msvc"):
                os.makedirs(os.path.join(d, name))
            with open(os.path.join(d, "3_renderer_deps.cmd"), "w", encoding="utf-8") as f:
                f.write("set DXC_URL=https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
                        "v1.8.2407/dxc_2024_07_31.zip\n")
            self.assertEqual(dependency_versions(d), {"dxc": "1.8.2407", "ffmpeg": "8.1.3", "fidelityfx": "2.4.0",
                                                      "lua": "5.0.2", "miniaudio": "0.11.25", "xess": "3.0.2"})

    def test_package_refuses_another_commit_a_dirty_tree_or_a_folder_inside_the_checkout(self):
        head = "0123456789abcdef0123456789abcdef01234567"
        self.assertIsNone(package_refusal(head, head, "", "E:\\packages", "E:\\repo"))
        self.assertIn("is not HEAD", package_refusal("fedcba9876543210fedcba9876543210fedcba98", head, "", "E:\\p",
                                                     "E:\\repo"))
        self.assertIn("uncommitted", package_refusal(head, head, " M tools/dev.py", "E:\\p", "E:\\repo"))
        self.assertIn("outside the checkout", package_refusal(head, head, "", "E:\\repo\\build\\pkg", "E:\\repo"))
        self.assertIsNone(package_refusal(head, head, "", "E:\\repo-packages", "E:\\repo"))


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
