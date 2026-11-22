#!/usr/bin/env python3
"""Run one test suite (or all) and report passed / failed / skipped counts by suite, with the reason for every skip.

  python -I tools/test_suite.py [--suite synthetic|retail|vulkan|traces|all] [--build <dir>] [--fast] [--no-retry]
                                [--require <suite>,...] [-- <extra ctest args>]
  python -I tools/test_suite.py --write-required [--build <dir>]   regenerate tests/required_synthetic.txt
  python -I tools/test_suite.py --selftest

dev.cmd suite <name> [...] calls this. Suites are CTest labels (cmake/AlamoTestSuites.cmake, README.md "Test suites").

What makes a run fail (exit 1):
  - a failed test (after the one retry below);
  - --suite retail / vulkan / traces: a missing prerequisite or a skipped part. The suite named on the command line is
    required (ALAMO_TEST_REQUIRE, read by alamo_test_gate), so it cannot pass by skipping. --require adds others,
    e.g. --suite all --require retail on a machine that must have the game installed;
  - --suite synthetic or all without a test filter: an entry listed in tests/required_synthetic.txt that this build
    tree does not have (a module left out of ALAMO_ENGINE_MODULES, python missing for the lints, a renamed test);
  - no tests selected.

Retry: a failed test is run once more on its own unless it is a determinism check (label "determinism", or a name that
says so: worker-count entries _w<N>, goldens, CRC, replay, selfcheck, parity harness): rerunning those proves nothing.
A test that passes on the retry counts as passed but is listed as flaky in the report, and its first failing output
is appended to <build>/test-results/flaky.jsonl, so the evidence of the first failure is kept.

Outputs: <build>/test-results/<suite>.xml (CTest JUnit), <build>/test-results/report-<suite>.md (the report printed at
the end), flaky.jsonl. Standard library only.
"""

from __future__ import annotations

import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITES = ("synthetic", "retail", "vulkan", "traces")
REQUIRED_FILE = os.path.join(ROOT, "tests", "required_synthetic.txt")
DETERMINISM = re.compile(r"(_w\d+|_wmax|_k\d+_w\d+)$|golden|_crc|crc_|replay|selfcheck|determinism|^parity_harness", re.I)
FILTER_ARGS = {"-R", "--tests-regex", "-E", "--exclude-regex", "-L", "--label-regex", "-LE", "--label-exclude", "-I",
               "--tests-information", "-U", "--union", "--rerun-failed"}


def ctest_exe() -> str:
    exe = shutil.which("ctest")
    if not exe:
        raise SystemExit("test_suite.py: ctest not found on PATH (run through dev.cmd)")
    return exe


def show_tests(build: str, filters: list[str]) -> dict[str, list[str]]:
    """name -> labels of the tests the filters select."""
    p = subprocess.run([ctest_exe(), "--test-dir", build, "--show-only=json-v1", *filters], capture_output=True,
                       text=True, errors="replace")
    try:
        tests = json.loads(p.stdout).get("tests", [])
    except ValueError:
        raise SystemExit(f"test_suite.py: ctest --show-only failed:\n{p.stdout}\n{p.stderr}")
    out = {}
    for t in tests:
        labels = next((x.get("value") or [] for x in t.get("properties", []) if x.get("name") == "LABELS"), [])
        out[t["name"]] = list(labels)
    return out


def suite_of(labels: list[str]) -> str:
    """The entry's suite label ("retail+vulkan" for a combined companion); "unlabelled" if it has none."""
    s = [x for x in SUITES if x in labels]
    return "+".join(s) if s else "unlabelled"


def parse_junit(path: str) -> dict[str, dict]:
    """name -> {status: passed|failed|skipped, out: text, skip: ctest skip message}."""
    out = {}
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return out
    for c in root.iter("testcase"):
        st = c.get("status", "")
        skipped = c.find("skipped")
        text = c.findtext("system-out") or ""
        if skipped is not None:
            status = "skipped"
        elif st == "run":
            status = "passed"
        else:
            status = "failed"  # fail, notrun without a skip (executable missing), disabled
        out[c.get("name")] = {"status": status, "out": text,
                              "skip": skipped.get("message", "") if skipped is not None else ""}
    return out


def skip_reasons(case: dict) -> list[str]:
    """The reasons a skipped entry gives: the gate's SKIP-PREREQ / SKIP-PART lines, else the test's own SKIP lines."""
    lines = case["out"].splitlines()
    r = [re.sub(r"^SKIP-(PREREQ|PART) \S+ ?", "", l).strip() for l in lines if l.startswith(("SKIP-PREREQ", "SKIP-PART"))]
    r = [x for x in r if "exited with the skip code" not in x] or r
    if not r:
        r = [l.strip() for l in lines if re.match(r"^\s*(--\s*)?SKIP\b", l)]
    return r or [case["skip"] or "skipped (no reason printed)"]


def retryable(name: str, labels: list[str]) -> bool:
    return "determinism" not in labels and not DETERMINISM.search(name)


def required_tests() -> dict[str, bool] | None:
    """name -> slow, from tests/required_synthetic.txt; None when the file is missing (the run then fails)."""
    out = {}
    try:
        with open(REQUIRED_FILE, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].split()
                if line:
                    out[line[0]] = "slow" in line[1:]
    except OSError:
        return None
    return out


def write_required(build: str) -> int:
    tests = show_tests(build, ["-L", "^synthetic$"])
    with open(REQUIRED_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("# The synthetic CTest entries a full configure must produce (every engine module, python found).\n"
                "# tools/test_suite.py fails a synthetic run that is missing one. Regenerate after adding or renaming a\n"
                "# synthetic test: dev.cmd suite --write-required (then review the diff).\n")
        for name in sorted(tests):
            f.write(name + (" slow" if "slow" in tests[name] else "") + "\n")
    print(f"wrote {len(tests)} entries to {os.path.relpath(REQUIRED_FILE, ROOT)}")
    return 0


def run_ctest(args: list[str], env: dict) -> int:
    print("+ " + " ".join(args), flush=True)
    return subprocess.call(args, env=env)


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    extra = argv[argv.index("--") + 1:] if "--" in argv else []
    opts = argv[:argv.index("--")] if "--" in argv else argv

    def opt(name, default=None):
        return opts[opts.index(name) + 1] if name in opts else default

    build = os.path.abspath(opt("--build", os.path.join(ROOT, "build")))
    if "--write-required" in opts:
        return write_required(build)
    suite = opt("--suite", "synthetic")
    if suite not in SUITES + ("all",):
        print(f"test_suite.py: unknown suite '{suite}' (synthetic, retail, vulkan, traces, all)")
        return 2
    require = [s for s in (opt("--require", "") or "").split(",") if s]
    if suite != "all" and suite != "synthetic":
        require.append(suite)
    fast = "--fast" in opts
    filtered = any(a in FILTER_ARGS for a in extra)

    filters = ([] if suite == "all" else ["-L", f"^{suite}$"]) + (["-LE", "^slow$"] if fast else []) + extra
    selected = show_tests(build, filters)
    if not selected:
        print(f"test_suite.py: no tests selected ({' '.join(filters) or 'no filter'}): configure and build first")
        return 1

    results_dir = os.path.join(build, "test-results")
    os.makedirs(results_dir, exist_ok=True)
    junit = os.path.join(results_dir, f"{suite}.xml")
    env = dict(os.environ)
    if require:
        env["ALAMO_TEST_REQUIRE"] = ",".join(sorted(set(require)))
    elif "ALAMO_TEST_REQUIRE" in env and suite == "synthetic":
        del env["ALAMO_TEST_REQUIRE"]
    rc = run_ctest([ctest_exe(), "--test-dir", build, "--output-on-failure", "--no-tests=error",
                    "--output-junit", junit, *filters], env)
    results = parse_junit(junit)
    if not results:
        print(f"test_suite.py: ctest wrote no results (exit {rc})")
        return 1

    flaky = []
    failed = [n for n, r in results.items() if r["status"] == "failed"]
    if failed and "--no-retry" not in opts:
        retry = [n for n in failed if retryable(n, selected.get(n, []))]
        if retry:
            print(f"\nretry: running {len(retry)} failed test(s) once more: {', '.join(retry)}", flush=True)
            junit2 = os.path.join(results_dir, f"{suite}-retry.xml")
            run_ctest([ctest_exe(), "--test-dir", build, "--output-on-failure", "--output-junit", junit2, "-R",
                       "^(" + "|".join(re.escape(n) for n in retry) + ")$"], env)
            again = parse_junit(junit2)
            now = datetime.datetime.now().isoformat(timespec="seconds")
            with open(os.path.join(results_dir, "flaky.jsonl"), "a", encoding="utf-8") as f:
                for n in retry:
                    if again.get(n, {}).get("status") == "passed":
                        flaky.append(n)
                        f.write(json.dumps({"at": now, "test": n, "suite": suite,
                                            "first_output": results[n]["out"][-6000:]}) + "\n")
                        results[n] = dict(again[n], flaky=True)

    missing = []
    if suite in ("synthetic", "all") and not filtered:
        required = required_tests()
        if required is None:  # without the list the check would pass vacuously
            missing = [f"(tests/required_synthetic.txt is missing: regenerate it with dev.cmd suite --write-required)"]
        else:
            missing = sorted(n for n, slow in required.items() if n not in results and not (fast and slow))

    lines = report(suite, results, selected, flaky, missing, require, fast)
    path = os.path.join(results_dir, f"report-{suite}.md")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("\n" + "\n".join(lines))
    print(f"\n(report: {path})")
    bad = any(r["status"] == "failed" for r in results.values()) or missing
    return 1 if bad else 0


def report(suite, results, selected, flaky, missing, require, fast) -> list[str]:
    by_suite: dict[str, dict[str, list[str]]] = {}
    for n, r in results.items():
        s = suite_of(selected.get(n, []))
        by_suite.setdefault(s, {"passed": [], "failed": [], "skipped": []})[r["status"]].append(n)
    lines = [f"# Test report: suite {suite}" + (" (fast tier: slow tests excluded)" if fast else ""), ""]
    if require:
        lines.append(f"Required in this run: {', '.join(sorted(set(require)))} (a missing prerequisite fails).")
    lines += ["", "| Suite | Passed | Failed | Skipped | Flaky (passed on retry) |", "| --- | --- | --- | --- | --- |"]
    order = list(SUITES) + sorted(s for s in by_suite if s not in SUITES)
    for s in order:
        if s not in by_suite:
            continue
        c = by_suite[s]
        nf = sum(1 for n in c["passed"] if n in flaky)
        lines.append(f"| {s} | {len(c['passed'])} | {len(c['failed'])} | {len(c['skipped'])} | {nf} |")
    for s in order:
        if s not in by_suite or not by_suite[s]["skipped"]:
            continue
        reasons: dict[str, list[str]] = {}
        for n in by_suite[s]["skipped"]:
            for why in sorted(set(skip_reasons(results[n]))):
                reasons.setdefault(why, []).append(n)
        lines += ["", f"## Skipped in {s} (outside this run)"]
        for why, names in sorted(reasons.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            shown = ", ".join(sorted(names)[:8]) + (f", ... ({len(names)} entries)" if len(names) > 8 else "")
            lines.append(f"- {why}: {shown}")
    if suite in ("synthetic", "all"):
        configured = {suite_of(l) for n, l in show_all_labels().items()}
        outside = sorted(configured - set(by_suite) - {"unlabelled"})
        empty = [s for s in SUITES if not any(s in c.split("+") for c in configured)]
        if suite == "synthetic":
            lines += ["", "Not selected by this run: the retail, vulkan and traces suites (dev.cmd suite all shows them,"
                      " skipped with their reasons where the machine lacks them)."]
        elif outside:
            lines += ["", f"Suites with no entries in this run: {', '.join(outside)}"]
        if empty:
            lines += ["", f"Suites with no configured entries at all: {', '.join(empty)}"]
    failed = sorted(n for c in by_suite.values() for n in c["failed"])
    if failed:
        lines += ["", "## Failed", *[f"- {n}" for n in failed]]
    if flaky:
        lines += ["", "## Flaky (failed, then passed on one retry; first output in test-results/flaky.jsonl)",
                  *[f"- {n}" for n in sorted(flaky)]]
    if missing:
        lines += ["", "## Required synthetic entries missing from this build tree (fails the run)",
                  *[f"- {n}" for n in missing],
                  "", "Configure every module (ALAMO_ENGINE_MODULES), make sure python was found, or regenerate "
                      "tests/required_synthetic.txt if a test was renamed on purpose."]
    unl = by_suite.get("unlabelled")
    if unl:
        lines += ["", "Unlabelled entries (not created through cmake/AlamoTestSuites.cmake): " +
                  ", ".join(sorted(n for v in unl.values() for n in v))]
    total = {k: sum(len(c[k]) for c in by_suite.values()) for k in ("passed", "failed", "skipped")}
    lines += ["", f"Total: {total['passed']} passed, {total['failed']} failed, {total['skipped']} skipped"
              + (f", {len(missing)} required entries missing" if missing else "")]
    return lines


_ALL = None


def show_all_labels():
    global _ALL
    if _ALL is None:
        _ALL = show_tests(_BUILD[0], [])
    return _ALL


_BUILD = [os.path.join(ROOT, "build")]


def selftest() -> int:
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)

    ok(suite_of(["slow", "retail", "vulkan"]) == "retail+vulkan", "combined suite label")
    ok(suite_of(["synthetic"]) == "synthetic", "synthetic label")
    ok(not retryable("alamo_sim_save_continue_w4", []), "worker-count entries are never retried")
    ok(not retryable("alamo_x_tests", ["determinism"]), "the determinism label is never retried")
    ok(retryable("alamo_jobs_tests", ["synthetic"]), "a plain test is retried")
    case = {"out": "SKIP-PREREQ retail foc: ALAMO_FOC_DATA not set\n", "skip": "SKIP_RETURN_CODE=77"}
    ok(skip_reasons(case) == ["foc: ALAMO_FOC_DATA not set"], f"gate reason: {skip_reasons(case)}")
    case = {"out": "SKIP-PART vulkan: SKIP gpu: no suitable Vulkan 1.3 device\n"
                   "SKIP-PART vulkan: the test exited with the skip code\n", "skip": ""}
    ok(skip_reasons(case) == ["SKIP gpu: no suitable Vulkan 1.3 device"], f"part reason: {skip_reasons(case)}")
    case = {"out": "-- SKIP: ALAMO_FOC_DATA is not set\n", "skip": "SKIP_REGULAR_EXPRESSION_MATCHED"}
    ok(skip_reasons(case) == ["-- SKIP: ALAMO_FOC_DATA is not set"], f"own reason: {skip_reasons(case)}")
    sel = {"a": ["synthetic"], "b": ["retail"], "c": ["retail", "vulkan"]}
    res = {"a": {"status": "passed", "out": ""}, "b": {"status": "skipped", "out": "SKIP-PREREQ retail foc: x\n", "skip": ""},
           "c": {"status": "failed", "out": ""}}
    _BUILD[0] = "__none__"
    global _ALL
    _ALL = sel
    rep = "\n".join(report("all", res, sel, [], ["gone_test"], [], False))
    ok("| retail | 0 | 0 | 1 | 0 |" in rep and "| retail+vulkan | 0 | 1 | 0 | 0 |" in rep, "per-suite table")
    ok("- foc: x: b" in rep, "skip reason listed")
    ok("gone_test" in rep, "missing required entry listed")
    for b in bad:
        print("test_suite selftest FAIL: " + b)
    if bad:
        return 1
    print("test_suite selftest: ok")
    return 0


if __name__ == "__main__":
    if "--build" in sys.argv:
        _BUILD[0] = os.path.abspath(sys.argv[sys.argv.index("--build") + 1])
    sys.exit(main(sys.argv[1:]))
