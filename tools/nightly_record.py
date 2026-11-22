#!/usr/bin/env python3
"""The nightly checks' record: the issue whose body holds each preset's dated last-passing commit.

  nightly_record.py update --body-file F --results analyze=success[,asan=failure] --sha SHA --date YYYY-MM-DD --run URL
                     prints F's body with each successful preset's row replaced and each preset's last result set
  nightly_record.py selftest
                     this script's own unit tests (CTest alamo_nightly_record_selftest)

.github/workflows/nightly.yml runs it after the checks and writes the result back to the issue titled
"Nightly checks: last passing" (docs/DECISIONS.md "MSVC is the only compiler"). A failed or cancelled preset keeps its
last-passing row; only its "last run" column changes. Standard library only.
"""

from __future__ import annotations

import re
import sys
import unittest

TITLE = "Nightly checks: last passing"
HEADER = ("Updated by `.github/workflows/nightly.yml` (`tools/nightly_record.py`); do not edit by hand.\n\n"
          "| Preset | Last passing | Commit | Run | Last run |\n| --- | --- | --- | --- | --- |\n")
_ROW = re.compile(r"^\| `(?P<preset>[\w-]+)` \| (?P<date>[^|]*) \| (?P<sha>[^|]*) \| (?P<run>[^|]*) \| (?P<last>[^|]*) \|$")


def parse(body: str) -> dict[str, dict[str, str]]:
    rows = {}
    for line in (body or "").splitlines():
        m = _ROW.match(line.strip())
        if m:
            rows[m["preset"]] = {k: m[k].strip() for k in ("date", "sha", "run", "last")}
    return rows


def update(body: str, results: dict[str, str], sha: str, date: str, run: str) -> str:
    rows = parse(body)
    for preset, result in results.items():
        row = rows.setdefault(preset, {"date": "never", "sha": "-", "run": "-", "last": "-"})
        if result == "success":
            row.update(date=date, sha=f"`{sha[:12]}`", run=f"[run]({run})")
        row["last"] = f"{result} {date} ([run]({run}))"
    return HEADER + "".join(f"| `{p}` | {r['date']} | {r['sha']} | {r['run']} | {r['last']} |\n"
                            for p, r in sorted(rows.items()))


def main(argv: list[str]) -> int:
    if argv == ["selftest"]:
        return selftest()
    opts = dict(zip(argv[1::2], argv[2::2])) if argv[:1] == ["update"] else {}
    need = ("--body-file", "--results", "--sha", "--date", "--run")
    if not opts or any(k not in opts for k in need):
        print(__doc__)
        return 2
    with open(opts["--body-file"], encoding="utf-8") as f:
        body = f.read()
    results = dict(item.split("=", 1) for item in opts["--results"].split(",") if item)
    sys.stdout.write(update(body, results, opts["--sha"], opts["--date"], opts["--run"]))
    return 0


# ------------------------------------------------------------------------------------------------- self-test

class _Tests(unittest.TestCase):
    def test_first_success_creates_the_table(self):
        body = update("", {"analyze": "success"}, "0123456789abcdef", "2026-11-22", "https://x/runs/1")
        self.assertTrue(body.startswith(HEADER))
        self.assertEqual(parse(body)["analyze"], {"date": "2026-11-22", "sha": "`0123456789ab`",
                                                  "run": "[run](https://x/runs/1)",
                                                  "last": "success 2026-11-22 ([run](https://x/runs/1))"})

    def test_failure_keeps_the_last_passing_row(self):
        body = update("", {"analyze": "success", "asan": "success"}, "aaaaaaaaaaaaaaaa", "2026-11-22", "u1")
        body = update(body, {"analyze": "failure", "asan": "success"}, "bbbbbbbbbbbbbbbb", "2026-11-23", "u2")
        rows = parse(body)
        self.assertEqual((rows["analyze"]["date"], rows["analyze"]["sha"]), ("2026-11-22", "`aaaaaaaaaaaa`"))
        self.assertEqual(rows["analyze"]["last"], "failure 2026-11-23 ([run](u2))")
        self.assertEqual((rows["asan"]["date"], rows["asan"]["sha"]), ("2026-11-23", "`bbbbbbbbbbbb`"))

    def test_never_passed(self):
        rows = parse(update("", {"asan": "cancelled"}, "c" * 16, "2026-11-22", "u"))
        self.assertEqual((rows["asan"]["date"], rows["asan"]["sha"]), ("never", "-"))

    def test_cli(self):
        import contextlib, io, os, tempfile
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "body.md")
            open(p, "w", encoding="utf-8").close()
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = main(["update", "--body-file", p, "--results", "analyze=success", "--sha", "f" * 40,
                             "--date", "2026-11-22", "--run", "u"])
            self.assertEqual(code, 0)
            self.assertIn("| `analyze` | 2026-11-22 | `ffffffffffff` |", out.getvalue())
            self.assertEqual(main(["update", "--body-file", p]), 2)


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
