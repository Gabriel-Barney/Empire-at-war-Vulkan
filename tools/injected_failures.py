#!/usr/bin/env python3
"""Injected failures: each must fail at the intended point and say so (docs/GROUND_UP_WORKFLOW_DESIGN.md "Rollout and
measures of success", step 3).

  injected_failures.py compile        a compile error in tests/fixtures/injected_failures/compile fails dev.cmd at
                                      the build step, with the compiler error as the first diagnostic
  injected_failures.py wrong-result   a test with a wrong result in tests/fixtures/injected_failures/wrong_result fails
                                      the suite run (tools/test_suite.py), which names it

CTest runs both (alamo_injected_compile_failure, alamo_injected_wrong_result; synthetic), so landing runs them. The
fixtures are separate CMake projects, configured into a temporary folder: never part of the engine build or a suite.
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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIXTURES = os.path.join(ROOT, "tests", "fixtures", "injected_failures")
sys.path.insert(0, HERE)  # python -I leaves the script's folder off sys.path

import dev  # noqa: E402


def _workdir() -> str:
    return tempfile.mkdtemp(prefix="alamo_injected_", dir=os.path.join(ROOT, "build", "tmp")
                            if os.path.isdir(os.path.join(ROOT, "build")) else None)


class CompileFailure(unittest.TestCase):
    def test_compile_error_fails_at_the_build_step_with_the_compiler_diagnostic(self):
        work = _workdir()
        self.addCleanup(shutil.rmtree, work, True)
        dev.LOG_DIR = os.path.join(work, "logs")
        r = dev.Runner("check", ["--injected-compile-failure"])
        r.env = dev.msvc_env()
        bdir = os.path.join(work, "build")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            r.run("configure (injected)", ["cmake", "-G", "Ninja", "-S", os.path.join(FIXTURES, "compile"), "-B", bdir],
                  r.env)
            try:
                dev.build(r, bdir, None)
                failure = None
            except dev.StepFailed as e:
                failure = e
            code = r.finish(failure)
        text = out.getvalue()
        self.assertIsNotNone(failure, "the broken fixture built:\n" + text)
        self.assertNotEqual(code, 0)
        self.assertIn("FAILED at step 'build all'", text)
        first = next(line for line in text.splitlines() if "first diagnostic:" in line)
        self.assertRegex(first, r"error C\d{4}")
        self.assertIn("injected_compile_failure", first)
        last = json.load(open(os.path.join(dev.LOG_DIR, "dev-last.json"), encoding="utf-8"))
        self.assertEqual((last["ok"], last["failed_step"]), (False, "build all"))
        self.assertRegex(last["first_diagnostic"], r"error C\d{4}")


class WrongResult(unittest.TestCase):
    def test_wrong_result_fails_the_suite_and_names_the_test(self):
        work = _workdir()
        self.addCleanup(shutil.rmtree, work, True)
        env = dev.msvc_env()
        bdir = os.path.join(work, "build")
        subprocess.run(dev.resolve(["cmake", "-G", "Ninja", "-S", os.path.join(FIXTURES, "wrong_result"), "-B", bdir],
                                   env), env=env, check=True, capture_output=True)
        p = subprocess.run([sys.executable, "-I", os.path.join(HERE, "test_suite.py"), "--build", bdir,
                            "--suite", "synthetic", "--", "-R", "^alamo_injected_"],
                           env=env, capture_output=True, text=True)
        self.assertEqual(p.returncode, 1, p.stdout + p.stderr)
        report = open(os.path.join(bdir, "test-results", "report-synthetic.md"), encoding="utf-8").read()
        self.assertIn("| synthetic | 1 | 1 |", report)
        failed = report.split("## Failed", 1)[1].split("##", 1)[0]
        self.assertIn("alamo_injected_wrong_result", failed)
        self.assertNotIn("alamo_injected_right_result", failed)


def main(argv: list[str]) -> int:
    cases = {"compile": CompileFailure, "wrong-result": WrongResult}
    if len(argv) != 1 or argv[0] not in cases:
        print(f"usage: injected_failures.py {'|'.join(cases)}")
        return 2
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(cases[argv[0]])
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
