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
  dev.cmd launcher | launcher-test        build or test the Rust Command Center launcher
  dev.cmd selftest                        this script's own unit tests

dev.cmd loads dev.local.cmd (machine settings such as ALAMO_FOC_DATA) and passes every argument here unchanged.
Arguments reach CMake, CTest and tools/test_suite.py as exact argument lists. cmd reads | & < > before this script
does, so the double quotes around such an argument must reach cmd: in cmd, dev.cmd test -R "^(a|b)$"; in PowerShell,
.\\dev.cmd test -R '"^(a|b)$"'.

Every step's output goes to the console and to build\\logs\\dev-<time>-<command>.log. A failed step prints the step,
the first meaningful diagnostic, the toolchain and source identity, a reproduction command, the elapsed time and the
log path; build\\logs\\dev-last.json holds the same for CI and agents. Standard library only.
"""

from __future__ import annotations

import datetime
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unittest

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


def test_env(env: dict[str, str]) -> dict[str, str]:
    """Tests write temporary files under build\\tmp, never the user's TEMP."""
    tmp = os.path.join(ROOT, "build", "tmp")
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
        self.log_path = os.path.join(LOG_DIR, f"dev-{stamp}-{command}.log")
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


def test_suite(r: Runner, bdir: str, args: list[str]) -> None:
    py = os.path.join(TOOLCHAIN, "venv", "Scripts", "python.exe")
    suite = args[0] if args and not args[0].startswith("-") else "synthetic"
    r.run(f"suite {suite}", [py, "-I", os.path.join(ROOT, "tools", "test_suite.py"), "--build", bdir,
                             "--suite", suite, *(args[1:] if args and not args[0].startswith("-") else args)],
          test_env(r.env or {}))


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
    r.run("ctest", ["ctest", "--test-dir", bdir, "--output-on-failure", "--no-tests=error", *rest], test_env(r.env))


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


COMMANDS = {
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


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
