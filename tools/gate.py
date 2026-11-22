#!/usr/bin/env python3
"""The landing gate's logic (docs/GROUND_UP_WORKFLOW_DESIGN.md "Evidence and integration").

  gate.py classify --event E [--base SHA --head SHA]   which jobs a change needs; writes code=true|false
  gate.py aggregate --classify R --code C --docs R --engine R
                                                       the one "landing" verdict from the job results
  gate.py selftest                                     this script's own unit tests

.github/workflows/landing.yml calls it; the self-test runs as CTest alamo_gate_selftest. Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import unittest

# Prose only: top-level README/AGENTS/CLAUDE, docs/*.md, docs/agents/*.md. Everything else is code; docs/re/*.md feeds
# the tag-ledger lint, so it is code too.
_PROSE = [re.compile(p) for p in (r"^(README|AGENTS|CLAUDE)\.md$", r"^docs/[^/]+\.md$", r"^docs/agents/[^/]+\.md$")]


class GateError(Exception):
    pass


def is_prose(path: str) -> bool:
    return any(p.match(path) for p in _PROSE)


def needs_code(files: list[str]) -> bool:
    """True when the change needs the engine job, False when the docs job is enough."""
    if not files:
        raise GateError("no changed files: nothing to classify")
    return not all(is_prose(f) for f in files)


def verdict(classify: str, code: str, docs: str, engine: str) -> str | None:
    """None when landing passes, else the reason it fails. Results are GitHub's job results."""
    if classify != "success":
        return f"path classification is '{classify or 'missing'}'"
    needed = {"true": ("engine", engine), "false": ("docs", docs)}.get(code)
    if needed is None:
        return f"unknown path classification '{code}'"
    job, result = needed
    if result != "success":
        return f"the {job} check is '{result or 'missing'}', and this change needs it"
    return None


def _append(env_var: str, text: str) -> None:
    path = os.environ.get(env_var)
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(text)


def cmd_classify(a: argparse.Namespace) -> int:
    rows = [("event", a.event), ("head", a.head or "-"), ("base", a.base or "-"), ("tested candidate", a.candidate or "-")]
    _append("GITHUB_STEP_SUMMARY", "### landing candidate\n\n| | |\n| --- | --- |\n"
            + "".join(f"| {k} | `{v}` |\n" for k, v in rows) + "\n")
    if a.event != "pull_request":
        code = True
        print("not a pull request: full check")
    else:
        out = subprocess.run(["git", "diff", "--name-only", f"{a.base}...{a.head}"], capture_output=True, text=True,
                             check=True).stdout
        files = [f for f in out.splitlines() if f]
        code = needs_code(files)
        for f in files:
            print(f"{'docs' if is_prose(f) else 'code'}: {f}")
    _append("GITHUB_OUTPUT", f"code={'true' if code else 'false'}\n")
    print(f"code={'true' if code else 'false'}")
    return 0


def cmd_aggregate(a: argparse.Namespace) -> int:
    print(f"classify={a.classify} code={a.code} docs={a.docs} engine={a.engine}")
    reason = verdict(a.classify, a.code, a.docs, a.engine)
    if reason:
        print(f"::error::{reason}")
        return 1
    print("landing: pass")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["selftest"]:
        return selftest()
    p = argparse.ArgumentParser(prog="gate.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("classify")
    c.add_argument("--event", required=True)
    c.add_argument("--base", default="")
    c.add_argument("--head", default="")
    c.add_argument("--candidate", default="")
    g = sub.add_parser("aggregate")
    for name in ("classify", "code", "docs", "engine"):
        g.add_argument(f"--{name}", default="")
    a = p.parse_args(argv)
    try:
        return {"classify": cmd_classify, "aggregate": cmd_aggregate}[a.command](a)
    except GateError as e:
        print(f"::error::{e}")
        return 1


# ------------------------------------------------------------------------------------------------- self-test

class _Tests(unittest.TestCase):
    def test_docs_only_change_needs_docs_job(self):
        self.assertFalse(needs_code(["README.md", "docs/DECISIONS.md", "docs/agents/preplanning.md"]))

    def test_any_other_path_needs_engine_job(self):
        self.assertTrue(needs_code(["docs/DECISIONS.md", "tools/dev.py"]))
        self.assertTrue(needs_code(["docs/re/xml_tags.md"]))  # feeds the tag-ledger lint
        self.assertTrue(needs_code(["docs/shaders/catalog.json"]))

    def test_empty_change_list_fails(self):
        with self.assertRaises(GateError):
            needs_code([])

    def test_code_change_passes_only_on_engine_success(self):
        self.assertIsNone(verdict("success", "true", "skipped", "success"))
        for result in ("failure", "cancelled", "skipped", ""):
            reason = verdict("success", "true", "skipped", result)
            self.assertIsNotNone(reason)
            self.assertIn("engine", reason)

    def test_docs_change_passes_only_on_docs_success(self):
        self.assertIsNone(verdict("success", "false", "success", "skipped"))
        for result in ("failure", "cancelled", "skipped", ""):
            reason = verdict("success", "false", result, "skipped")
            self.assertIsNotNone(reason)
            self.assertIn("docs", reason)

    def test_failed_or_unknown_classification_fails(self):
        self.assertIn("classification", verdict("failure", "true", "skipped", "success"))
        self.assertIn("classification", verdict("success", "", "success", "success"))
        self.assertIn("classification", verdict("success", "maybe", "success", "success"))


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
