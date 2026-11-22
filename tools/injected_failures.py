#!/usr/bin/env python3
"""Injected failures: each must fail at the intended point and say so (docs/GROUND_UP_WORKFLOW_DESIGN.md "Rollout and
measures of success", step 3).

  injected_failures.py compile        a compile error in tests/fixtures/injected_failures/compile fails dev.cmd at
                                      the build step, with the compiler error as the first diagnostic
  injected_failures.py wrong-result   a test with a wrong result in tests/fixtures/injected_failures/wrong_result fails
                                      the suite run (tools/test_suite.py), which names it
  injected_failures.py quarantine     the same wrong result, quarantined with a current entry, is reported and does not
                                      fail the run; with an expired entry it fails again and the entry is named

CTest runs them (alamo_injected_compile_failure, alamo_injected_wrong_result, alamo_injected_quarantine; synthetic), so
landing runs them. The fixtures are separate CMake projects, configured into a temporary folder: never part of the
engine build or a suite.
dev.cmd's logs for the injected run go to that folder too, so build\\logs\\dev-last.json keeps the real run's result.
Standard library only.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIXTURES = os.path.join(ROOT, "tests", "fixtures", "injected_failures")
sys.path.insert(0, HERE)  # python -I leaves the script's folder off sys.path

import dev  # noqa: E402


class _InjectedCase(unittest.TestCase):
    def setUp(self):
        tmp = os.path.join(ROOT, "build", "tmp")  # where tests write (dev.test_env)
        os.makedirs(tmp, exist_ok=True)
        self.work = tempfile.mkdtemp(prefix="alamo_injected_", dir=tmp)
        self.addCleanup(shutil.rmtree, self.work, True)
        self.env = dev.msvc_env()
        self.bdir = os.path.join(self.work, "build")


class CompileFailure(_InjectedCase):
    def test_compile_error_fails_at_the_build_step_with_the_compiler_diagnostic(self):
        logs = os.path.join(self.work, "logs")
        out = io.StringIO()
        # The injected run's dev.cmd logs and dev-last.json go to the work folder, not build\logs.
        with mock.patch.object(dev, "LOG_DIR", logs), contextlib.redirect_stdout(out):
            r = dev.Runner("check", ["--injected-compile-failure"])
            r.env = self.env
            failure = None
            try:
                r.run("configure (injected)", ["cmake", "-G", "Ninja", "-S", os.path.join(FIXTURES, "compile"), "-B",
                                               self.bdir], r.env)
                dev.build(r, self.bdir, None)
            except dev.StepFailed as e:
                failure = e
            code = r.finish(failure)  # closes the log even when configure failed
        text = out.getvalue()
        self.assertIsNotNone(failure, "the broken fixture built:\n" + text)
        self.assertNotEqual(code, 0)
        self.assertIn("FAILED at step 'build all'", text)
        first = next(line for line in text.splitlines() if "first diagnostic:" in line)
        self.assertRegex(first, r"error C\d{4}")
        self.assertIn("injected_compile_failure", first)
        with open(os.path.join(logs, "dev-last.json"), encoding="utf-8") as f:
            last = json.load(f)
        self.assertEqual((last["ok"], last["failed_step"]), (False, "build all"))
        self.assertRegex(last["first_diagnostic"], r"error C\d{4}")


class WrongResult(_InjectedCase):
    def test_wrong_result_fails_the_suite_and_names_the_test(self):
        subprocess.run(dev.resolve(["cmake", "-G", "Ninja", "-S", os.path.join(FIXTURES, "wrong_result"), "-B",
                                    self.bdir], self.env), env=self.env, check=True, capture_output=True)
        p = subprocess.run([sys.executable, "-I", os.path.join(HERE, "test_suite.py"), "--build", self.bdir,
                            "--suite", "synthetic", "--", "-R", "^alamo_injected_"],
                           env=self.env, capture_output=True, text=True)
        self.assertEqual(p.returncode, 1, p.stdout + p.stderr)
        with open(os.path.join(self.bdir, "test-results", "report-synthetic.md"), encoding="utf-8") as f:
            report = f.read()
        # The report file and the console summary both carry the counts and name the failed test, and only it.
        for text in (report, p.stdout):
            self.assertIn("| synthetic | 1 | 1 |", text)
            failed = text.split("## Failed", 1)[1].split("##", 1)[0]
            self.assertIn("alamo_injected_wrong_result", failed)
            self.assertNotIn("alamo_injected_right_result", failed)


class Quarantine(_InjectedCase):
    """The flaky-check exercise of #31 as a permanent check (tests/quarantine.txt; README.md "Test suites")."""

    def run_suite(self, expiry: str) -> tuple[int, str]:
        listing = os.path.join(self.work, "quarantine.txt")
        with open(listing, "w", encoding="utf-8") as f:
            f.write(f"alamo_injected_wrong_result\t#28\tGabriel\t{expiry}\n")
        p = subprocess.run([sys.executable, "-I", os.path.join(HERE, "test_suite.py"), "--build", self.bdir,
                            "--suite", "synthetic", "--", "-R", "^alamo_injected_"],
                           env=dict(self.env, ALAMO_QUARANTINE_FILE=listing), capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr

    def test_current_entry_reports_and_expired_entry_fails(self):
        subprocess.run(dev.resolve(["cmake", "-G", "Ninja", "-S", os.path.join(FIXTURES, "wrong_result"), "-B",
                                    self.bdir], self.env), env=self.env, check=True, capture_output=True)
        code, out = self.run_suite("2999-12-31")
        self.assertEqual(code, 0, out)
        self.assertIn("## Quarantined", out)
        self.assertIn("alamo_injected_wrong_result (#28, Gabriel, until 2999-12-31)", out)
        self.assertIn("1 quarantined", out)
        code, out = self.run_suite("2000-01-01")
        self.assertEqual(code, 1, out)
        self.assertIn("alamo_injected_wrong_result: its quarantine (#28, Gabriel) expired on 2000-01-01", out)
        self.assertIn("alamo_injected_wrong_result", out.split("## Failed", 1)[1])


def main(argv: list[str]) -> int:
    cases = {"compile": CompileFailure, "wrong-result": WrongResult, "quarantine": Quarantine}
    if len(argv) != 1 or argv[0] not in cases:
        print(f"usage: injected_failures.py {'|'.join(cases)}")
        return 2
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(cases[argv[0]])
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
