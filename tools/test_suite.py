#!/usr/bin/env python3
"""Run one test suite (or all) and report passed / failed / skipped counts by suite, with the reason for every skip.

  python -I tools/test_suite.py [--suite synthetic|retail|vulkan|traces|all] [--build <dir>] [--fast] [--no-retry]
                                [--require <suite>,...] [--archive <dir>] [-- <extra ctest args>]
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

Quarantine: tests/quarantine.txt lists known flaky, non-critical tests with a tracking issue, an owner and an expiry
date. A listed test that still fails after the retry is reported as quarantined and does not fail the run until the
entry expires; then it fails again, and the run says which entry expired. A determinism check cannot be listed, and a
malformed list fails the run.

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

    try:  # checked before the run, so a bad list fails at once
        quarantine = load_quarantine()
        if quarantine:
            check_quarantine(quarantine, show_tests(build, []))
    except QuarantineError as e:
        print(f"test_suite.py: error: {e}")
        return 1

    latest_dir = os.path.join(build, "test-results")
    # With --archive (dev.cmd: <build>\test-results\runs\<run>) this run writes its JUnit and report there, then
    # copies them up as the latest, so concurrent runs on one tree never read each other's files.
    results_dir = opt("--archive") or latest_dir
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
            with open(os.path.join(latest_dir, "flaky.jsonl"), "a", encoding="utf-8") as f:
                for n in retry:
                    if again.get(n, {}).get("status") == "passed":
                        flaky.append(n)
                        f.write(json.dumps({"at": now, "test": n, "suite": suite,
                                            "first_output": results[n]["out"][-6000:]}) + "\n")
                        results[n] = dict(again[n], flaky=True)

    results, expired = apply_quarantine(results, quarantine, datetime.date.today())
    for note in expired:
        print(f"test_suite.py: {note}; the failure counts again (renew or remove the entry)")

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
    if results_dir != latest_dir:
        publish_latest(results_dir, latest_dir, suite)
        print(f"(this run's own copy: {results_dir})")
    bad = any(r["status"] == "failed" for r in results.values()) or missing
    return 1 if bad else 0


def report(suite, results, selected, flaky, missing, require, fast) -> list[str]:
    by_suite: dict[str, dict[str, list[str]]] = {}
    for n, r in results.items():
        s = suite_of(selected.get(n, []))
        by_suite.setdefault(s, {"passed": [], "failed": [], "skipped": [], "quarantined": []})[r["status"]].append(n)
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
    quarantined = sorted(n for c in by_suite.values() for n in c["quarantined"])
    if quarantined:
        lines += ["", "## Quarantined (failed; reported, not failing the run, until the entry expires: tests/quarantine.txt)",
                  *[f"- {n} ({results[n]['quarantine']['issue']}, {results[n]['quarantine']['owner']}, until "
                    f"{results[n]['quarantine']['expires']})" for n in quarantined]]
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
              + (f", {len(quarantined)} quarantined" if quarantined else "")
              + (f", {len(missing)} required entries missing" if missing else "")]
    return lines


QUARANTINE_FILE = os.path.join(ROOT, "tests", "quarantine.txt")


class QuarantineError(Exception):
    pass


def parse_quarantine(lines: list[str], source: str = "tests/quarantine.txt") -> dict[str, dict]:
    """One tab-separated entry per line: test, #issue, owner, expiry (YYYY-MM-DD). Errors name `source`."""
    entries: dict[str, dict] = {}
    for no, line in enumerate(lines, 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        where = f"{source}:{no}"
        parts = [p.strip() for p in line.rstrip("\r\n").split("\t")]
        if len(parts) != 4 or not all(parts):
            raise QuarantineError(f"{where}: need test, #issue, owner and expiry, tab-separated: {line!r}")
        name, issue, owner, expires = parts
        if not re.fullmatch(r"#\d+", issue):
            raise QuarantineError(f"{where}: the issue is written #<number>, got {issue!r}")
        try:
            day = datetime.date.fromisoformat(expires)
        except ValueError:
            raise QuarantineError(f"{where}: the expiry is a date YYYY-MM-DD, got {expires!r}") from None
        if name in entries:
            raise QuarantineError(f"{where}: {name} is already listed")
        entries[name] = {"issue": issue, "owner": owner, "expires": day.isoformat(), "day": day, "source": source}
    return entries


def load_quarantine() -> dict[str, dict]:
    """The quarantine list; ALAMO_QUARANTINE_FILE points elsewhere (tools/injected_failures.py quarantine)."""
    path = os.environ.get("ALAMO_QUARANTINE_FILE") or QUARANTINE_FILE
    if not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return parse_quarantine(f.read().splitlines(), shown_path(path))


def shown_path(path: str) -> str:
    """`path` as messages show it: relative to the checkout when inside it."""
    path = os.path.abspath(path)
    inside = os.path.normcase(path).startswith(os.path.normcase(ROOT) + os.sep)
    return os.path.relpath(path, ROOT).replace(os.sep, "/") if inside else path


def check_quarantine(entries: dict, labels: dict) -> None:
    """A determinism check cannot be quarantined: rerunning or excusing those proves nothing. `labels` holds every
    configured test, not only this run's, so a listed determinism check is refused whatever the run selects."""
    for name, entry in entries.items():
        if not retryable(name, labels.get(name, [])):
            raise QuarantineError(f"{entry['source']}: {name} is a determinism check and cannot be quarantined")


def apply_quarantine(results: dict, entries: dict, today: datetime.date) -> tuple[dict, list[str]]:
    """Failed tests under a current quarantine (through its expiry date, local time) become 'quarantined'; an expired
    entry leaves the failure standing and says so."""
    out, notes = dict(results), []
    for name, entry in entries.items():
        r = out.get(name)
        if not r or r["status"] != "failed":
            continue
        if entry["day"] >= today:
            out[name] = dict(r, status="quarantined", quarantine=entry)
        else:
            notes.append(f"{name}: its quarantine ({entry['issue']}, {entry['owner']}) expired on {entry['expires']}")
    return out, notes


_ALL = None


def show_all_labels():
    global _ALL
    if _ALL is None:
        _ALL = show_tests(_BUILD[0], [])
    return _ALL


_BUILD = [os.path.join(ROOT, "build")]


def publish_latest(run_dir: str, latest_dir: str, suite: str) -> None:
    """Copies a run's report and JUnit files from its own folder to <build>\test-results, the latest run's place."""
    for name in (f"report-{suite}.md", f"{suite}.xml", f"{suite}-retry.xml"):
        src = os.path.join(run_dir, name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(latest_dir, name))


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
    # Quarantine (tests/quarantine.txt): test, issue, owner, expiry.
    import tempfile
    from unittest import mock
    today = datetime.date(2026, 11, 22)
    entries = parse_quarantine(["# comment", "", "alamo_flaky_tests\t#40\tGabriel\t2026-12-01",
                                "alamo_old_tests\t#41\tGabriel\t2026-11-21"])
    ok(set(entries) == {"alamo_flaky_tests", "alamo_old_tests"}, "quarantine entries parsed")
    for bad_lines in (["alamo_x_tests\t#40\t\t2026-12-01"], ["alamo_x_tests\t40\tGabriel\t2026-12-01"],
                      ["alamo_x_tests\t#40\tGabriel\t2026-13-01"], ["alamo_x_tests\t#40\tGabriel"],
                      ["alamo_x_tests\t#40\tGabriel\t2026-12-01", "alamo_x_tests\t#41\tGabriel\t2026-12-01"]):
        try:
            parse_quarantine(bad_lines)
            ok(False, f"malformed quarantine list accepted: {bad_lines!r}")
        except QuarantineError:
            pass
    res = {"alamo_flaky_tests": {"status": "failed", "out": ""}, "alamo_old_tests": {"status": "failed", "out": ""},
           "alamo_other_tests": {"status": "failed", "out": ""}, "alamo_ok_tests": {"status": "passed", "out": ""}}
    labels = {n: ["synthetic"] for n in res}
    res2, notes = apply_quarantine(res, entries, today)
    ok(res2["alamo_flaky_tests"]["status"] == "quarantined", "a quarantined failure is reported, not failed")
    ok(res2["alamo_old_tests"]["status"] == "failed", "an expired quarantine fails again")
    ok(any("alamo_old_tests" in n and "expired" in n for n in notes), "the expired entry is named")
    ok(res2["alamo_other_tests"]["status"] == "failed" and res2["alamo_ok_tests"]["status"] == "passed",
       "other results unchanged")
    check_quarantine(entries, labels)
    for name, test_labels in (("alamo_x_w4", []), ("alamo_x_tests", ["synthetic", "determinism"])):
        try:  # by name, and by label (main passes every configured test's labels, not only this run's)
            check_quarantine(parse_quarantine([f"{name}\t#40\tGabriel\t2026-12-01"]), {name: test_labels})
            ok(False, f"a determinism check was quarantined: {name}")
        except QuarantineError as e:
            ok(str(e).startswith("tests/quarantine.txt:") and name in str(e), f"determinism refusal message: {e}")
    with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ):
        listing = os.path.join(d, "q.txt")
        with open(listing, "w", encoding="utf-8") as f:
            f.write("alamo_x_tests\t#40\tGabriel\t2026-12-01\nalamo_y_tests\t#40\tGabriel\t2026-12-01\n")
        os.environ["ALAMO_QUARANTINE_FILE"] = listing
        ok(set(load_quarantine()) == {"alamo_x_tests", "alamo_y_tests"}, "ALAMO_QUARANTINE_FILE is read")
        with open(listing, "a", encoding="utf-8") as f:
            f.write("alamo_z_tests\t41\tGabriel\t2026-12-01\n")
        try:
            load_quarantine()
            ok(False, "a malformed quarantine file was accepted")
        except QuarantineError as e:
            ok(str(e).startswith(shown_path(listing) + ":3:"),
               f"errors name the file actually read: {e}")
    rep = "\n".join(report("synthetic", res2, labels, [], [], [], False))
    ok("## Quarantined" in rep and "alamo_flaky_tests (#40, Gabriel, until 2026-12-01)" in rep, "quarantine listed")
    ok("1 quarantined" in rep, "quarantined count in the total")
    with tempfile.TemporaryDirectory() as d:
        latest = os.path.join(d, "test-results")
        for run, text in (("r1", "first"), ("r2", "second")):
            run_dir = os.path.join(latest, "runs", run)
            os.makedirs(run_dir)
            for name in ("report-synthetic.md", "synthetic.xml"):
                with open(os.path.join(run_dir, name), "w", encoding="utf-8") as f:
                    f.write(text)
            publish_latest(run_dir, latest, "synthetic")
        with open(os.path.join(latest, "runs", "r1", "report-synthetic.md"), encoding="utf-8") as f:
            ok(f.read() == "first", "the first run's own report survives the second run")
        with open(os.path.join(latest, "synthetic.xml"), encoding="utf-8") as f:
            ok(f.read() == "second", "the latest run's JUnit is published")
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
