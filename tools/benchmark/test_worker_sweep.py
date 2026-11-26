#!/usr/bin/env python3
"""#629: self-test of worker_sweep.py with a fake alamo_headless (no game data, no build).

    python tools/benchmark/test_worker_sweep.py [WORKDIR]

Covers: the interleaved run order and each run's arguments ("auto" passes no --workers, the arguments after -- reach every run),
the medians / ratio / zone table in sweep.json and sweep.md, a run that does not match its fingerprint (exit 1), the --max-busy
retry, and the usage refusals (exit 2).
"""
import contextlib
import io
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import worker_sweep as ws  # noqa: E402

FAILS = 0


def check(cond, what):
    global FAILS
    if not cond:
        FAILS += 1
        print("FAIL:", what)


class FakeHeadless:
    """Writes a bench result for each --bench command: tick median 4 ms at W0, 4 / (1 + W) otherwise, 1 ms at auto."""

    def __init__(self, comparable=True, other_cpu=2.0):
        self.calls = []
        self.comparable = comparable
        self.other_cpu = other_cpu

    def __call__(self, cmd):
        self.calls.append(cmd)
        w = cmd[cmd.index("--workers") + 1] if "--workers" in cmd else "auto"
        med = 1.0 if w == "auto" else 4.0 / (1 + int(w))
        d = {"format": "alamo-bench-result", "comparable": self.comparable,
             "settings": {"workers": 6 if w == "auto" else int(w)},
             "summary": {"median_ms": med, "p99_ms": med * 2, "worst_ms": med * 5, "max_other_cpu_pct": self.other_cpu},
             "zones": [{"name": "tac.ai", "p50_ms": med / 2, "mean_ms": med / 2, "share": 0.5},
                       {"name": "tac.cap.rest", "p50_ms": med / 4, "mean_ms": med / 4, "share": 0.25},
                       {"name": "tac.movement", "p50_ms": 0.1, "mean_ms": 0.1, "share": 0.1}]}
        with open(cmd[cmd.index("--bench-json") + 1], "w", encoding="utf-8") as f:
            json.dump(d, f)
        return 0, "bench: OK\n"


def run(argv, runner, busy=lambda: 5.0):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = ws.main(argv, runner=runner, busy=busy, sleep=lambda s: None)
    return rc, out.getvalue(), err.getvalue()


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="worker_sweep_")
    scenes = os.path.join(work, "scenes")
    os.makedirs(scenes, exist_ok=True)
    for n in ("alpha", "beta"):
        open(os.path.join(scenes, n + ".abench"), "w").close()

    # a clean sweep: two scenes, three counts, two rounds, interleaved
    fake = FakeHeadless()
    out = os.path.join(work, "clean")
    rc, text, err = run(["--scenes", "alpha,beta", "--workers", "auto,0,3", "--rounds", "2", "--scenes-dir", scenes, "--out", out,
                         "--zones", "tac.ai,tac.cap.*", "--", "--ai-lane-k", "2"], fake)
    check(rc == 0, f"clean sweep exits 0 (got {rc}; {err})")
    check(len(fake.calls) == 12, f"2 scenes x 3 counts x 2 rounds = 12 runs (got {len(fake.calls)})")
    order = [(os.path.basename(c[c.index("--bench") + 1]), c[c.index("--workers") + 1] if "--workers" in c else "auto")
             for c in fake.calls]
    check(order[:6] == [("alpha.abench", "0"), ("alpha.abench", "3"), ("alpha.abench", "auto"),
                        ("beta.abench", "0"), ("beta.abench", "3"), ("beta.abench", "auto")],
          f"round 1 runs every scene at every count, counts ascending, auto last (got {order[:6]})")
    check(order[6:] == order[:6], "round 2 repeats round 1's order")
    check(all(c[-2:] == ["--ai-lane-k", "2"] for c in fake.calls), "the arguments after -- reach every run")
    check(all("--runs" in c and c[c.index("--runs") + 1] == "1" and c[c.index("--warmup") + 1] == "0" for c in fake.calls),
          "each run is one timed run without warm-up")
    with open(os.path.join(out, "sweep.json"), encoding="utf-8") as f:
        s = json.load(f)
    a = s["scenes"]["alpha"]
    check(s["workers"] == ["0", "3", "auto"], f"counts in sweep.json (got {s['workers']})")
    check(a["counts"]["0"]["median_ms"] == 4.0 and a["counts"]["3"]["median_ms"] == 1.0, "mean tick median per count")
    check(abs(a["counts"]["auto"]["ratio_to_first"] - 0.25) < 1e-9, "ratio of the default count to W0 is 1 / 4")
    check(a["counts"]["auto"]["workers_used"] == [6], "the worker count the default resolved to is kept")
    check(a["counts"]["0"]["busy_before_pct"] == [5.0, 5.0], "the busy CPU before every run is kept")
    check(set(a["zones"]) == {"tac.ai", "tac.cap.rest"}, f"zones by name and prefix*, others left out (got {set(a['zones'])})")
    check(a["zones"]["tac.ai"]["0"]["p50_ms"] == 2.0, "zone p50 at W0")
    check(s["extra_args"] == ["--ai-lane-k", "2"], "sweep.json records the extra arguments")
    with open(os.path.join(out, "sweep.md"), encoding="utf-8") as f:
        md = f.read()
    check("### alpha" in md and "| tick median ms | 4.000 | 1.000 | 1.000 |" in md, "sweep.md has the median row")
    check("| `tac.cap.rest` |" in md and "| fingerprint | ok | ok | ok |" in md, "sweep.md has the zone table and fingerprint row")

    # a run that does not match its fingerprint fails the sweep
    rc, text, _ = run(["--scenes", "alpha", "--workers", "0", "--rounds", "1", "--scenes-dir", scenes, "--out",
                       os.path.join(work, "stale")], FakeHeadless(comparable=False))
    check(rc == 1 and "not comparable" in text, f"a stale scene exits 1 and says not comparable (got {rc})")

    # --max-busy: a loaded run is retried, at most --retries times
    fake = FakeHeadless(other_cpu=50.0)
    rc, text, _ = run(["--scenes", "alpha", "--workers", "0", "--rounds", "1", "--scenes-dir", scenes, "--max-busy", "20",
                       "--retries", "2", "--out", os.path.join(work, "busy")], fake)
    check(rc == 0 and len(fake.calls) == 3 and text.count("retrying") == 2, f"busy run retried twice (calls {len(fake.calls)})")

    # usage refusals
    for argv, what in ((["--scenes", "alpha", "--workers", "x", "--scenes-dir", scenes], "a bad worker list"),
                       (["--scenes", "gamma", "--scenes-dir", scenes], "a scene not made in --scenes-dir"),
                       (["--scenes", "no_such_recipe"], "an unknown recipe"),
                       (["--scenes", "alpha", "--rounds", "0", "--scenes-dir", scenes], "zero rounds")):
        rc, _, err = run(argv + ["--out", os.path.join(work, "usage")], FakeHeadless())
        check(rc == 2 and err.startswith("worker_sweep:"), f"{what} exits 2 with a message (got {rc})")

    print("test_worker_sweep: %s" % ("OK" if FAILS == 0 else f"{FAILS} failure(s)"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
