#!/usr/bin/env python3
"""MSVC /analyze findings against a checked-in baseline (dev.cmd check --preset analyze).

  analyze_report.py check --build DIR            fail on a finding the baseline does not have; list fixed ones
  analyze_report.py write-baseline --build DIR   rewrite tests/analyze_baseline.txt from DIR's findings
  analyze_report.py selftest                     this script's own unit tests (CTest alamo_analyze_report_selftest)

MSVC writes one <object>.nativecodeanalysis.xml per compiled file, rewritten when the file recompiles, so the findings
of an incremental build are complete. A finding is keyed by warning code, source path and function (lambda numbers
dropped), not by line, so unrelated edits do not churn the baseline; the price is that fixing one finding while adding
another of the same code in the same function goes unnoticed. Logs of sources no longer in build.ninja are ignored, a
log that does not parse fails, and no logs at all fails (the build did not run /analyze). Standard library only.
"""

from __future__ import annotations

import collections
import contextlib
import io
import os
import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape as xml_escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "tests", "analyze_baseline.txt")

Key = tuple[str, str, str]  # (C<code>, repository-relative path, function)


def _function(name: str) -> str:
    return re.sub(r"<lambda_\d+>", "<lambda>", name or "")


class AnalyzeError(Exception):
    pass


_LOG = ".nativecodeanalysis.xml"


def current_objects(build_dir: str) -> set[str] | None:
    """The object files build.ninja builds now (normalised, relative to build_dir), or None without a build.ninja.
    An analysis log whose object is not among them belongs to a deleted or renamed source and is ignored."""
    path = os.path.join(build_dir, "build.ninja")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as f:
        return {os.path.normcase(os.path.normpath(m.group(1).replace("$ ", " ").replace("$:", ":")))
                for m in re.finditer(r"^build ((?:[^:$\n]|\$.)+\.obj):", f.read(), flags=re.M)}


def collect(build_dir: str, root: str = ROOT) -> tuple[collections.Counter, dict[Key, list[str]], int]:
    """The findings in build_dir's analysis logs: a count per key, 'path(line): description' per key, and the number
    of logs read. A log that is not empty and does not parse raises AnalyzeError."""
    seen: set[tuple] = set()  # a header's finding appears once per file that includes it
    where: dict[Key, list[str]] = collections.defaultdict(list)
    count: collections.Counter = collections.Counter()
    root_n = os.path.normcase(os.path.abspath(root)) + os.sep
    objects = current_objects(build_dir)
    logs = 0
    for dirpath, _, names in os.walk(build_dir):
        for name in names:
            if not name.endswith(_LOG):
                continue
            log = os.path.join(dirpath, name)
            obj = os.path.normcase(os.path.normpath(os.path.relpath(log, build_dir)[:-len(_LOG)] + ".obj"))
            if objects is not None and obj not in objects:
                continue  # stale: its source is no longer built
            logs += 1
            if os.path.getsize(log) == 0:
                continue  # no findings
            try:
                tree = ET.parse(log)
            except ET.ParseError as e:
                raise AnalyzeError(f"cannot read the analysis log {log}: {e}") from None
            for d in tree.getroot().iter("DEFECT"):
                sfa = d.find("SFA")
                if sfa is None or not sfa.findtext("FILENAME", ""):
                    continue
                full = os.path.abspath(os.path.join(sfa.findtext("FILEPATH", ""), sfa.findtext("FILENAME", "")))
                if not os.path.normcase(full).startswith(root_n):
                    continue  # toolchain and SDK headers
                rel = os.path.relpath(full, root).replace("\\", "/")
                code, line = "C" + d.findtext("DEFECTCODE", "?"), sfa.findtext("LINE", "?")
                func = _function(d.findtext("FUNCTION", ""))
                ident = (code, rel, line, func, d.findtext("DESCRIPTION", ""))
                if ident in seen:
                    continue
                seen.add(ident)
                key = (code, rel, func)
                count[key] += 1
                where[key].append(f"{rel}({line}): {code}: {d.findtext('DESCRIPTION', '').strip()}")
    return count, where, logs


def load_baseline(path: str = BASELINE) -> collections.Counter:
    count: collections.Counter = collections.Counter()
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                code, rel, func = line.rstrip("\n").split("\t")
                count[(code, rel, func)] += 1
    return count


def compare(found: collections.Counter, baseline: collections.Counter) -> tuple[collections.Counter, collections.Counter]:
    """(new findings, baseline entries no longer found)."""
    return found - baseline, baseline - found


def write_baseline(found: collections.Counter, path: str = BASELINE) -> int:
    lines = sorted(f"{c}\t{r}\t{fn}" for (c, r, fn), n in found.items() for _ in range(n))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# MSVC /analyze findings accepted on 2026-11-21, one per line: code, path, function (tab-separated).\n"
                "# dev.cmd check --preset analyze fails on a finding not listed here. Fix findings and shrink this list;\n"
                "# regenerate it with: python -I tools/analyze_report.py write-baseline --build build-analyze\n")
        f.write("".join(line + "\n" for line in lines))
    return len(lines)


def main(argv: list[str]) -> int:
    if argv == ["selftest"]:
        return selftest()
    if len(argv) != 3 or argv[0] not in ("check", "write-baseline") or argv[1] != "--build":
        print(__doc__)
        return 2
    try:
        found, where, logs = collect(argv[2])
    except AnalyzeError as e:
        print(f"error: {e}")
        return 1
    if logs == 0:  # /analyze did not run: a pass here would be vacuous
        print(f"error: no {_LOG} analysis logs under {argv[2]}: was it built with the analyze preset (ALAMO_ANALYZE)?")
        return 1
    if argv[0] == "write-baseline":
        print(f"analyze_report: wrote {write_baseline(found)} findings to {os.path.relpath(BASELINE, ROOT)}")
        return 0
    if not os.path.isfile(BASELINE):
        print(f"error: {os.path.relpath(BASELINE, ROOT)} is missing; write it with: "
              "python -I tools/analyze_report.py write-baseline --build build-analyze")
        return 1
    new, gone = compare(found, load_baseline())
    total = sum(found.values())
    print(f"analyze_report: {logs} analysis logs, {total} findings, {sum(new.values())} not in the baseline, "
          f"{sum(gone.values())} fixed")
    for key in sorted(gone):
        print(f"  fixed (remove from tests/analyze_baseline.txt): {' '.join(key)}")
    for key in sorted(new):
        for w in where[key]:
            print(f"error: new /analyze finding: {w} [in {key[2]}]")
    return 1 if new else 0


# ------------------------------------------------------------------------------------------------- self-test

_XML = """<?xml version="1.0" encoding="UTF-8"?>
<DEFECTS>{}</DEFECTS>"""
_DEFECT = """<DEFECT><SFA><FILEPATH>{dir}</FILEPATH><FILENAME>{file}</FILENAME><LINE>{line}</LINE></SFA>
<DEFECTCODE>{code}</DEFECTCODE><DESCRIPTION>{desc}</DESCRIPTION><FUNCTION>{func}</FUNCTION></DEFECT>"""


class _Tests(unittest.TestCase):
    def build(self, root: str, logs: dict[str, list[tuple]]) -> str:
        bdir = os.path.join(root, "build-analyze")
        for name, defects in logs.items():  # None: an empty log file
            os.makedirs(os.path.join(bdir, "obj"), exist_ok=True)
            body = "".join(_DEFECT.format(dir=d, file=f, line=ln, code=c, desc="desc", func=xml_escape(fn))
                           for d, f, ln, c, fn in defects or [])
            with open(os.path.join(bdir, "obj", name + ".nativecodeanalysis.xml"), "w", encoding="utf-8") as fh:
                fh.write(_XML.format(body) if defects is not None else "")
        return bdir

    def test_keys_by_code_path_function_and_drops_outside_and_duplicates(self):
        with tempfile.TemporaryDirectory() as root:
            src = os.path.join(root, "engine", "x") + os.sep
            outside = os.path.join(os.path.dirname(root), "sdk") + os.sep
            bdir = self.build(root, {
                "a.cpp": [(src, "a.cpp", 10, 6011, "f::<lambda_3>::()"), (src, "h.hpp", 5, 6262, "g"),
                          (outside, "winbase.h", 1, 28251, "WinMain")],
                "b.cpp": [(src, "h.hpp", 5, 6262, "g"), (src, "a.cpp", 20, 6011, "f::<lambda_7>::()")],
                "empty.cpp": None,
            })
            found, where, logs = collect(bdir, root)
            self.assertEqual(found, collections.Counter({("C6011", "engine/x/a.cpp", "f::<lambda>::()"): 2,
                                                         ("C6262", "engine/x/h.hpp", "g"): 1}))
            self.assertEqual(logs, 3)
            self.assertIn("engine/x/a.cpp(20): C6011: desc", where[("C6011", "engine/x/a.cpp", "f::<lambda>::()")])

    def test_stale_logs_of_sources_no_longer_built_are_ignored(self):
        with tempfile.TemporaryDirectory() as root:
            src = os.path.join(root, "engine", "x") + os.sep
            bdir = self.build(root, {"a.cpp": [(src, "a.cpp", 1, 6011, "f")], "gone.cpp": [(src, "gone.cpp", 1, 6011, "g")]})
            with open(os.path.join(bdir, "build.ninja"), "w", encoding="utf-8") as f:
                f.write("build obj\\a.cpp.obj: CXX_COMPILER E$:\\x\\a.cpp\n")
            found, _, logs = collect(bdir, root)
            self.assertEqual((list(found), logs), ([("C6011", "engine/x/a.cpp", "f")], 1))

    def test_corrupt_log_fails_and_no_logs_fail_the_check(self):
        with tempfile.TemporaryDirectory() as root:
            bdir = self.build(root, {"a.cpp": []})
            with open(os.path.join(bdir, "obj", "a.cpp" + _LOG), "w", encoding="utf-8") as f:
                f.write("<DEFECTS><DEFECT>")  # truncated
            with self.assertRaises(AnalyzeError):
                collect(bdir, root)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["check", "--build", os.path.join(root, "empty")]), 1)

    def test_new_and_fixed_findings(self):
        a, b, c = ("C6011", "p.cpp", "f"), ("C6262", "q.cpp", "g"), ("C6326", "r.cpp", "h")
        new, gone = compare(collections.Counter({a: 2, c: 1}), collections.Counter({a: 1, b: 1}))
        self.assertEqual(new, collections.Counter({a: 1, c: 1}))
        self.assertEqual(gone, collections.Counter({b: 1}))
        self.assertEqual(compare(collections.Counter({a: 1}), collections.Counter({a: 1})),
                         (collections.Counter(), collections.Counter()))

    def test_baseline_round_trip(self):
        found = collections.Counter({("C6011", "p.cpp", "f::<lambda>::()"): 2, ("C6262", "q.cpp", "g"): 1})
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "baseline.txt")
            self.assertEqual(write_baseline(found, path), 3)
            self.assertEqual(load_baseline(path), found)

    def test_checked_in_baseline_parses(self):
        self.assertTrue(load_baseline())


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
