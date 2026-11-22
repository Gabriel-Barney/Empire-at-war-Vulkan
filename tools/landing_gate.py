#!/usr/bin/env python3
"""The landing gate's logic (docs/GROUND_UP_WORKFLOW_DESIGN.md "Evidence and integration").

  landing_gate.py classify --event E [--base SHA --head SHA]
                      which jobs a change needs: prints each path's class and writes code=true|false to GITHUB_OUTPUT
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


def _append(env_var: str, text: str) -> None:
    path = os.environ.get(env_var)
    if path:
        with open(path, "a", encoding="utf-8", newline="\n") as f:
            f.write(text)


def _changed_files(base: str, head: str) -> list[str]:
    out = subprocess.run(["git", "-c", "core.quotepath=off", "diff", "--name-only", f"{base}...{head}"],
                         capture_output=True, encoding="utf-8", check=True).stdout
    return [f for f in out.splitlines() if f]


def cmd_classify(a: argparse.Namespace, changed_files=_changed_files) -> int:
    if a.event != "pull_request":
        code = True
        print("not a pull request: full check")
    else:
        files = changed_files(a.base, a.head)
        code = needs_code(files)
        for f in files:
            print(f"{path_class(f)}: {f}")
    value = "true" if code else "false"
    _append("GITHUB_OUTPUT", f"code={value}\n")
    print(f"code={value}")
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

class _Tests(unittest.TestCase):
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
        self.assertEqual((code, output), (0, "code=true\n"))
        self.assertIn("code: tools/dev.py", out)
        self.assertEqual(self.run_main(["classify", "--event", "pull_request"], ["docs/SIMULATION.md"])[2], "code=false\n")
        self.assertEqual(self.run_main(["classify", "--event", "push"])[2], "code=true\n")

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


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
