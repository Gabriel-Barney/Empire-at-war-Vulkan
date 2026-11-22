#!/usr/bin/env python3
"""The landing gate's logic (docs/GROUND_UP_WORKFLOW_DESIGN.md "Evidence and integration").

  landing_gate.py classify --event E [--base SHA --head SHA --body-file F]
                      which jobs a change needs: prints each path's class and writes code= and simulation=true|false
                      to GITHUB_OUTPUT; a simulation change (tests/simulation_paths.txt) whose pull request
                      description F has no 'Behaviour:' line fails
  landing_gate.py aggregate --classify R --code C --docs R --engine R [--event E --base SHA --head SHA --candidate SHA]
                      the one "landing" verdict from the job results; the candidate's commits go in the step summary
  landing_gate.py selftest
                      this script's own unit tests (CTest alamo_landing_gate_selftest)

.github/workflows/landing.yml calls it. Every failure prints a GitHub ::error:: line with the reason and exits 1.
Standard library only.
"""

from __future__ import annotations

import argparse
import contextlib
import fnmatch
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

# Prose only: top-level README/AGENTS/CLAUDE, docs/*.md, docs/agents/*.md. Everything else is code; docs/re/*.md feeds
# the tag-ledger lint, so it is code too.
_PROSE = [re.compile(p) for p in (r"^(README|AGENTS|CLAUDE)\.md$", r"^docs/[^/]+\.md$", r"^docs/agents/[^/]+\.md$")]


class GateError(Exception):
    pass


def path_class(path: str) -> str:
    """'docs' or 'code' for a repository-relative path with forward slashes; anything else cannot be classified."""
    if not path.strip() or path != path.strip() or path.startswith("/") or "\\" in path or re.match(r"^[A-Za-z]:", path) \
            or ".." in path.split("/"):
        raise GateError(f"cannot classify path {path!r}: expected a repository-relative path with forward slashes")
    return "docs" if any(p.match(path) for p in _PROSE) else "code"


def needs_code(files: list[str]) -> bool:
    """True when the change needs the engine job, False when the docs job is enough."""
    if not files:
        raise GateError("no changed files: nothing to classify")
    return any(path_class(f) == "code" for f in files)


def verdict(classify: str, code: str, docs: str, engine: str) -> str | None:
    """None when landing passes, else the reason it fails. Results are GitHub's job results."""
    if classify != "success":
        return f"the path classification job is '{classify or 'missing'}'"
    needed = {"true": ("engine", engine), "false": ("docs", docs)}.get(code)
    if needed is None:
        return f"unknown path classification '{code}'"
    job, result = needed
    if result != "success":
        return f"the {job} job is '{result or 'missing'}', and this change needs it"
    return None


SIMULATION_PATHS_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests",
                                     "simulation_paths.txt")


def load_simulation_patterns(path: str = SIMULATION_PATHS_FILE) -> list[str]:
    with open(path, encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip() and not line.lstrip().startswith("#")]


def _matches(path: str, pattern: str) -> bool:
    # A pattern ending in / is a folder prefix; otherwise fnmatch, where * also crosses /.
    return path.startswith(pattern) if pattern.endswith("/") else fnmatch.fnmatchcase(path, pattern)


def simulation_paths(files: list[str], patterns: list[str]) -> list[str]:
    """The changed files that are simulation code: matched by a pattern and by no !pattern (tests/simulation_paths.txt)."""
    keep = [p for p in patterns if not p.startswith("!")]
    drop = [p[1:] for p in patterns if p.startswith("!")]
    return [f for f in files if any(_matches(f, p) for p in keep) and not any(_matches(f, p) for p in drop)]


def behaviour_line(body: str) -> str | None:
    """The value of the PR body's first 'Behaviour: ...' line, or None.

    HTML comments (an unclosed one runs to the end) and fenced code blocks do not count. The line may be a list item
    or bold ('- Behaviour: none', '**Behaviour:** none') and the word is matched in any case. An unfilled placeholder
    ('Behaviour: <...>') is not a declaration."""
    text = re.sub(r"<!--.*?(-->|\Z)", "", body or "", flags=re.S)
    text = re.sub(r"^[ \t]*(```|~~~).*?^[ \t]*\1[^\n]*$", "", text, flags=re.S | re.M)
    m = re.search(r"^[ \t]*(?:[-*+][ \t]+)?(?:\*\*|__)?Behaviour:(?:\*\*|__)?[ \t]*(\S.*?)[ \t\r]*$", text,
                  flags=re.M | re.I)
    if not m or m.group(1).startswith("<"):
        return None
    return m.group(1)


def missing_behaviour_error(sim_files: list[str], body: str) -> str | None:
    """The error for a simulation change without a declaration, or None (docs/DECISIONS.md "Simulation changes are
    declared")."""
    if not sim_files or behaviour_line(body) is not None:
        return None
    shown = ", ".join(sim_files[:5]) + (f" and {len(sim_files) - 5} more" if len(sim_files) > 5 else "")
    return (f"this pull request changes simulation code ({shown}) but its description has no 'Behaviour:' line. "
            "Add a line 'Behaviour: none', or 'Behaviour: ' and the scenarios and systems that change, to the "
            "description, then re-run the failed jobs (docs/SIMULATION.md \"Declared behaviour changes\")")


def _append(env_var: str, text: str) -> None:
    path = os.environ.get(env_var)
    if path:
        with open(path, "a", encoding="utf-8", newline="\n") as f:
            f.write(text)


def _changed_files(base: str, head: str) -> list[str]:
    # --no-renames lists a moved file at both paths, so moving code out of a simulation folder counts as touching it.
    out = subprocess.run(["git", "-c", "core.quotepath=off", "diff", "--name-only", "--no-renames",
                          f"{base}...{head}"], capture_output=True, encoding="utf-8", check=True).stdout
    return [f for f in out.splitlines() if f]


def cmd_classify(a: argparse.Namespace, changed_files=_changed_files) -> int:
    if a.event != "pull_request":
        code, sim = True, []
        print("not a pull request: full check")
    else:
        files = changed_files(a.base, a.head)
        code = needs_code(files)
        sim = set(simulation_paths(files, load_simulation_patterns()))
        for f in files:
            print(f"{path_class(f)}{' (simulation)' if f in sim else ''}: {f}")
        body = ""
        if a.body_file:
            with open(a.body_file, encoding="utf-8") as fh:
                body = fh.read()
        error = missing_behaviour_error(sorted(sim), body)
        if error:
            raise GateError(error)
        if sim:
            declared = behaviour_line(body)
            print(f"Behaviour: {declared}")
            _append("GITHUB_STEP_SUMMARY",
                    f"### Simulation change\n\n`Behaviour: {declared}`\n\nThis run includes the synthetic `determinism` "
                    "entries. The ones that need game data run in the local `dev.cmd suite retail` that the pull "
                    "request records (docs/SIMULATION.md \"Declared behaviour changes\").\n\n")
    out = f"code={'true' if code else 'false'}\nsimulation={'true' if sim else 'false'}\n"
    _append("GITHUB_OUTPUT", out)
    print(out, end="")
    return 0


def cmd_aggregate(a: argparse.Namespace) -> int:
    rows = [("event", a.event), ("head", a.head), ("base", a.base), ("tested candidate", a.candidate)]
    _append("GITHUB_STEP_SUMMARY", "### landing candidate\n\n| | |\n| --- | --- |\n"
            + "".join(f"| {k} | `{v or '-'}` |\n" for k, v in rows) + "\n")
    print(f"classify={a.classify} code={a.code} docs={a.docs} engine={a.engine}")
    reason = verdict(a.classify, a.code, a.docs, a.engine)
    if reason:
        raise GateError(reason)
    print("landing: pass")
    return 0


def main(argv: list[str], changed_files=_changed_files) -> int:
    if argv == ["selftest"]:
        return selftest()
    p = argparse.ArgumentParser(prog="landing_gate.py", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("classify")
    c.add_argument("--event", required=True)
    c.add_argument("--base", default="")
    c.add_argument("--head", default="")
    c.add_argument("--body-file", default="", help="the pull request description, for the Behaviour: line")
    g = sub.add_parser("aggregate")
    for name in ("classify", "code", "docs", "engine", "event", "base", "head", "candidate"):
        g.add_argument(f"--{name}", default="")
    a = p.parse_args(argv)
    try:
        if a.command == "classify":
            return cmd_classify(a, changed_files)
        return cmd_aggregate(a)
    except GateError as e:
        print(f"::error::{e}")
        return 1


# ------------------------------------------------------------------------------------------------- self-test

class _RunsMain:
    def run_main(self, argv: list[str], files: list[str] | None = None) -> tuple[int, str, str, str]:
        """Runs main() as the workflow does; returns exit code, stdout, GITHUB_OUTPUT and GITHUB_STEP_SUMMARY."""
        with tempfile.TemporaryDirectory() as d:
            out_path, summary_path = os.path.join(d, "output"), os.path.join(d, "summary")
            old = {k: os.environ.get(k) for k in ("GITHUB_OUTPUT", "GITHUB_STEP_SUMMARY")}
            os.environ["GITHUB_OUTPUT"], os.environ["GITHUB_STEP_SUMMARY"] = out_path, summary_path
            buf = io.StringIO()
            try:
                with contextlib.redirect_stdout(buf):
                    code = main(argv, changed_files=lambda base, head: list(files or []))
            finally:
                for k, v in old.items():
                    if v is None:
                        os.environ.pop(k, None)
                    else:
                        os.environ[k] = v
            read = lambda p: open(p, encoding="utf-8").read() if os.path.exists(p) else ""
            return code, buf.getvalue(), read(out_path), read(summary_path)


class _Tests(_RunsMain, unittest.TestCase):
    # classify
    def test_docs_only_change_needs_docs_job(self):
        self.assertFalse(needs_code(["README.md", "docs/DECISIONS.md", "docs/agents/preplanning.md"]))

    def test_any_other_path_needs_engine_job(self):
        self.assertTrue(needs_code(["docs/DECISIONS.md", "tools/dev.py"]))
        self.assertTrue(needs_code(["docs/re/xml_tags.md"]))  # feeds the tag-ledger lint
        self.assertTrue(needs_code(["docs/shaders/catalog.json"]))

    def test_empty_change_list_fails(self):
        with self.assertRaises(GateError):
            needs_code([])

    def test_unclassifiable_path_fails(self):
        for bad in ("", " README.md", "/etc/passwd", "C:/x.md", "tools\\dev.py", "tools/../dev.py"):
            with self.assertRaises(GateError, msg=bad):
                needs_code(["README.md", bad])

    def test_classify_writes_the_job_output(self):
        code, out, output, _ = self.run_main(["classify", "--event", "pull_request", "--base", "b", "--head", "h"],
                                             ["README.md", "tools/dev.py"])
        self.assertEqual((code, output), (0, "code=true\nsimulation=false\n"))
        self.assertIn("code: tools/dev.py", out)
        self.assertEqual(self.run_main(["classify", "--event", "pull_request"], ["docs/SIMULATION.md"])[2],
                         "code=false\nsimulation=false\n")
        self.assertEqual(self.run_main(["classify", "--event", "push"])[2], "code=true\nsimulation=false\n")

    def test_classify_fails_closed_without_output(self):
        for files in ([], ["tools\\x.py"]):
            code, out, output, _ = self.run_main(["classify", "--event", "pull_request"], files)
            self.assertEqual((code, output), (1, ""))
            self.assertIn("::error::", out)

    # aggregate
    def test_code_change_passes_only_on_engine_success(self):
        self.assertIsNone(verdict("success", "true", "skipped", "success"))
        for result in ("failure", "cancelled", "skipped", ""):
            self.assertIn("the engine job is", verdict("success", "true", "skipped", result))

    def test_docs_change_passes_only_on_docs_success(self):
        self.assertIsNone(verdict("success", "false", "success", "skipped"))
        for result in ("failure", "cancelled", "skipped", ""):
            self.assertIn("the docs job is", verdict("success", "false", result, "skipped"))

    def test_failed_missing_or_unknown_classification_fails(self):
        for result in ("failure", "cancelled", "skipped", ""):
            self.assertIn("the path classification job is", verdict(result, "true", "skipped", "success"))
        for code in ("", "maybe"):
            self.assertIn("unknown path classification", verdict("success", code, "success", "success"))

    def test_aggregate_exit_code_error_line_and_candidate_summary(self):
        ok = ["aggregate", "--classify", "success", "--code", "true", "--docs", "skipped", "--engine", "success",
              "--event", "pull_request", "--head", "h1", "--base", "b1", "--candidate", "m1"]
        code, out, _, summary = self.run_main(ok)
        self.assertEqual(code, 0)
        for row in ("| head | `h1` |", "| base | `b1` |", "| tested candidate | `m1` |"):
            self.assertIn(row, summary)
        code, out, _, summary = self.run_main(["aggregate", "--classify", "success", "--code", "true",
                                               "--engine", "cancelled"])
        self.assertEqual(code, 1)
        self.assertIn("::error::the engine job is 'cancelled'", out)
        self.assertIn("| head | `-` |", summary)


class _BehaviourTests(_RunsMain, unittest.TestCase):
    PATTERNS = ["engine/tactical/", "engine/core/src/sim_*.cpp", "tests/goldens/", "!*/tests/*"]

    def test_simulation_paths_come_from_the_list_minus_tests(self):
        sim = simulation_paths(["engine/tactical/combat/src/a.cpp", "engine/tactical/combat/tests/test_a.cpp",
                                "engine/core/src/sim_rng.cpp", "engine/core/src/log.cpp", "tests/goldens/x.crc",
                                "tools/dev.py"], self.PATTERNS)
        self.assertEqual(sim, ["engine/tactical/combat/src/a.cpp", "engine/core/src/sim_rng.cpp", "tests/goldens/x.crc"])

    def test_checked_in_list_parses_and_covers_simulation_modules(self):
        patterns = load_simulation_patterns()
        self.assertTrue(simulation_paths(["engine/tactical/combat/src/combat_system_part2.cpp"], patterns))
        self.assertTrue(simulation_paths(["engine/core/src/sim_crc.cpp"], patterns))
        self.assertTrue(simulation_paths(["engine/apps/game/rules/skirmish_rules.cpp",
                                          "engine/core/include/alamo/core/save_file.hpp"], patterns))
        self.assertFalse(simulation_paths(["engine/render/src/jam_fx.cpp", "tools/dev.py",
                                           "engine/galactic/tests/test_economy.cpp", "engine/jobs/bench/b.cpp",
                                           "engine/lua/tools/gen_bindings.py"], patterns))

    def test_behaviour_line(self):
        self.assertEqual(behaviour_line("## Behaviour\n\nBehaviour: none\n"), "none")
        self.assertEqual(behaviour_line("Behaviour:  battle tie-break (combat), skirmish_ai_a1 \r\n"),
                         "battle tie-break (combat), skirmish_ai_a1")
        for written in ("- Behaviour: none", "**Behaviour:** none", "behaviour: none", "* __Behaviour:__ none"):
            self.assertEqual(behaviour_line(written + "\n"), "none", written)
        self.assertIsNone(behaviour_line("## Behaviour\n\nnothing here\n"))
        self.assertIsNone(behaviour_line("Behaviour:\n"))
        self.assertIsNone(behaviour_line("Behaviour: <scenarios and systems>\n"))  # the unfilled placeholder
        # The template's example inside an HTML comment, an unclosed comment or a code block does not count.
        self.assertIsNone(behaviour_line("<!-- e.g.\nBehaviour: none\n-->\n"))
        self.assertIsNone(behaviour_line("text <!-- unclosed\nBehaviour: none\n"))
        self.assertIsNone(behaviour_line("```\nBehaviour: none\n```\n"))
        self.assertEqual(behaviour_line("```\nx\n```\nBehaviour: none\n"), "none")

    def test_simulation_change_needs_a_declaration(self):
        sim = ["engine/tactical/combat/src/a.cpp"]
        reason = missing_behaviour_error(sim, "## Behaviour\n\n<!-- Behaviour: none -->\n")
        self.assertIn("Behaviour:", reason)
        self.assertIn("engine/tactical/combat/src/a.cpp", reason)
        self.assertIsNone(missing_behaviour_error(sim, "Behaviour: none"))
        self.assertIsNone(missing_behaviour_error(sim, "Behaviour: unit pathing (movement), space_ai_b2"))
        self.assertIsNone(missing_behaviour_error([], ""))  # no simulation path: no line needed

    def test_classify_undeclared_declared_and_non_simulation_changes(self):
        sim, other = ["engine/core/src/sim_rng.cpp"], ["engine/render/src/jam_fx.cpp"]
        with tempfile.TemporaryDirectory() as d:
            body = os.path.join(d, "body.md")
            for text, files, want, output_want in (
                    ("## Behaviour\n", sim, 1, ""),
                    ("Behaviour: none\n", sim, 0, "code=true\nsimulation=true\n"),
                    ("Behaviour: rng stream order (combat), skirmish_ai_a1\n", sim, 0, "code=true\nsimulation=true\n"),
                    ("", other, 0, "code=true\nsimulation=false\n")):
                with open(body, "w", encoding="utf-8") as f:
                    f.write(text)
                code, out, output, summary = self.run_main(
                    ["classify", "--event", "pull_request", "--body-file", body], files)
                self.assertEqual((code, output), (want, output_want), out)
                if want:
                    self.assertIn("::error::", out)
                    self.assertIn("sim_rng.cpp", out)
                elif files is sim:
                    self.assertIn("code (simulation): engine/core/src/sim_rng.cpp", out)
                    self.assertIn("Simulation change", summary)
                    self.assertIn("determinism", summary)
                else:
                    self.assertNotIn("Simulation change", summary)


def selftest() -> int:
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (_Tests, _BehaviourTests))
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
