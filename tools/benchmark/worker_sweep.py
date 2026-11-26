#!/usr/bin/env python3
"""Worker-count sweep (#629): bench scenes sim-only in alamo_headless at several worker counts, interleaved rounds.

    python tools/benchmark/worker_sweep.py --scenes land_battle,land_battle_lane [--workers 0,1,4,auto] [--rounds 2]
                                           [--build build] [--scenes-dir DIR] [--out DIR] [--zones tac.ai,tac.cap.*]
                                           [--max-busy P] [--retries N] [-- MORE ALAMO_HEADLESS ARGS]

Each scene is first made from its recipe (tests/benchmark/<scene>.abench) with this build, as the CTest fixtures do
(tools/goldens/bench_scenarios.py); --scenes-dir DIR uses scenes already made there instead. Then, for each round, for each
scene, for each worker count: one process `alamo_headless --bench SCENE --runs 1 --warmup 0 [--workers W] --quiet
--bench-json OUT/<scene>_w<W>_r<round>.json [MORE ARGS]` ("auto" passes no --workers: the default count). Interleaving keeps
machine-load drift out of the worker-count comparison. The whole-machine CPU use is sampled (typeperf) before each run and kept
with its result, since other checkouts may be building; --max-busy P retries a run (after a 30 s pause) that was busier than P
percent before it, or whose other processes took more than P percent during it.

Writes OUT/sweep.json (every number) and OUT/sweep.md (one Markdown table per scene: tick median / p99 / worst per worker count,
mean over rounds, the busy CPU, the ratio to the first count, and the --zones table: p50 / mean ms and share per count). A
recipe with the AI lane on (land_battle_lane) has its own fingerprint, so lane-on runs are timed as comparable runs.
Exit 0; 1 when a run failed or did not match its fingerprint; 2 usage or a scene could not be made. Game data: ALAMO_FOC_DATA.
"""
from __future__ import annotations

import argparse
import datetime
import fnmatch
import json
import os
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RECIPES = os.path.join(ROOT, "tests", "benchmark")
DEFAULT_ZONES = "tac.ai,tac.ai.publish,tac.ai.capture,tac.cap.*,crc.battle.ai,T7.*,ai.goals,T5.battles"


def cpu_busy_pct() -> float | None:
    """The whole machine's CPU use over about 2 s (typeperf; None when unavailable)."""
    try:
        r = subprocess.run(["typeperf", r"\Processor(_Total)\% Processor Time", "-sc", "2"], capture_output=True, text=True,
                           timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    vals = []
    for line in r.stdout.splitlines():
        parts = [p.strip().strip('"') for p in line.split('","')]
        if len(parts) == 2:
            try:
                vals.append(float(parts[1]))
            except ValueError:
                pass
    return round(sum(vals) / len(vals), 1) if vals else None


def parse_counts(text: str) -> list[str]:
    """'0,1,4,auto' -> ['0', '1', '4', 'auto'] (integers sorted first, 'auto' last); ValueError on anything else."""
    items = [x.strip() for x in text.split(",") if x.strip()]
    nums = sorted({int(x) for x in items if x != "auto"})
    if any(n < 0 for n in nums):
        raise ValueError("negative worker count")
    return [str(n) for n in nums] + (["auto"] if "auto" in items else [])


def run_cmd(cmd: list[str]) -> tuple[int, str]:
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def make_scenes(names: list[str], build: str, out: str) -> dict[str, str]:
    """Materialize each recipe with this build (tools/goldens/bench_scenarios.py); scene name -> made .abench path."""
    made = {}
    for n in names:
        r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "goldens", "bench_scenarios.py"), "materialize", n,
                            "--build", build, "--out", out], cwd=ROOT, capture_output=True, text=True)
        path = os.path.join(out, n + ".abench")
        if r.returncode != 0 or not os.path.isfile(path):
            raise RuntimeError(f"scene {n} could not be made (exit {r.returncode}): {(r.stdout + r.stderr)[-400:]}")
        made[n] = path
    return made


def mean(vals: list) -> float | None:
    v = [x for x in vals if isinstance(x, (int, float))]
    return statistics.fmean(v) if v else None


def fmt(v, digits: int = 3) -> str:
    return f"{v:.{digits}f}" if isinstance(v, (int, float)) else "-"


def zone_names(runs: dict[str, list[dict]], patterns: list[str]) -> list[str]:
    names: list[str] = []
    for ds in runs.values():
        for d in ds:
            for z in d.get("zones", []):
                n = z.get("name", "")
                if n not in names and any(fnmatch.fnmatchcase(n, p) for p in patterns):
                    names.append(n)
    return names


def zone_value(ds: list[dict], name: str, key: str) -> float | None:
    return mean([z.get(key) for d in ds for z in d.get("zones", []) if z.get("name") == name])


def summarize(results: dict[str, dict[str, list[dict]]], counts: list[str], patterns: list[str]) -> dict:
    """results[scene][count] = the result dicts of the rounds (each with "busy_before_pct" added). Means over rounds."""
    out: dict = {"workers": counts, "scenes": {}}
    for scene, runs in results.items():
        sc: dict = {"counts": {}, "zones": {}}
        base = None
        for c in counts:
            ds = runs.get(c, [])
            row = {k: mean([d.get("summary", {}).get(k) for d in ds]) for k in ("median_ms", "p99_ms", "worst_ms")}
            row["medians_ms"] = [d.get("summary", {}).get("median_ms") for d in ds]
            row["busy_before_pct"] = [d.get("busy_before_pct") for d in ds]
            row["other_cpu_pct"] = [d.get("summary", {}).get("max_other_cpu_pct") for d in ds]
            row["workers_used"] = sorted({d.get("settings", {}).get("workers") for d in ds if "settings" in d} - {None})
            row["comparable"] = bool(ds) and all(d.get("comparable", False) for d in ds)
            if base is None:
                base = row["median_ms"]
            row["ratio_to_first"] = row["median_ms"] / base if base and row["median_ms"] is not None else None
            sc["counts"][c] = row
        for n in sorted(zone_names(runs, patterns), key=lambda n: -(zone_value(runs.get(counts[0], []), n, "mean_ms") or 0.0)):
            sc["zones"][n] = {c: {k: zone_value(runs.get(c, []), n, k) for k in ("p50_ms", "mean_ms", "share")} for c in counts}
        out["scenes"][scene] = sc
    return out


def markdown(summary: dict) -> str:
    counts = summary["workers"]
    head = "| | " + " | ".join(f"W{c}" for c in counts) + " |\n|---|" + "---|" * len(counts) + "\n"
    parts = []
    for scene, sc in summary["scenes"].items():
        rows = sc["counts"]
        lines = [f"### {scene}\n", head.rstrip("\n")]

        def line(label, fn):
            lines.append(f"| {label} | " + " | ".join(fn(rows[c]) for c in counts) + " |")

        line("tick median ms (mean of the rounds)", lambda r: fmt(r["median_ms"]))
        line("tick p99 ms (mean of the rounds)", lambda r: fmt(r["p99_ms"]))
        line("worst tick ms (mean of the rounds)", lambda r: fmt(r["worst_ms"]))
        line("median per round", lambda r: " / ".join(fmt(v) for v in r["medians_ms"]))
        line(f"time / W{counts[0]} (below 1 = faster)", lambda r: fmt(r["ratio_to_first"]))
        line("workers used", lambda r: ", ".join(str(w) for w in r["workers_used"]) or "-")
        line("busy before %", lambda r: " / ".join(fmt(v, 0) for v in r["busy_before_pct"]))
        line("other CPU % during", lambda r: " / ".join(fmt(v, 0) for v in r["other_cpu_pct"]))
        line("fingerprint", lambda r: "ok" if r["comparable"] else "**not comparable**")
        if sc["zones"]:
            lines.append("")
            lines.append("| zone | " + " | ".join(f"W{c} p50 / mean ms (share), mean of the rounds" for c in counts) + " |")
            lines.append("|---|" + "---|" * len(counts))
            for n, by in sc["zones"].items():
                lines.append(f"| `{n}` | " + " | ".join(
                    f"{fmt(by[c]['p50_ms'])} / {fmt(by[c]['mean_ms'])} ({fmt((by[c]['share'] or 0) * 100, 1)} %)"
                    if by[c]["mean_ms"] is not None else "-" for c in counts) + " |")
        parts.append("\n".join(lines))
    return "\n\n".join(parts) + "\n"


def main(argv: list[str], runner=run_cmd, busy=cpu_busy_pct, sleep=time.sleep) -> int:
    extra: list[str] = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenes", required=True, help="comma list of recipe names (tests/benchmark/<name>.abench)")
    ap.add_argument("--workers", default="0,1,4,auto", help="comma list of worker counts; auto = the default count")
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"), help="build tree with bin/alamo_headless.exe")
    ap.add_argument("--scenes-dir", default="", help="use the scenes already made in this folder (no materialize)")
    ap.add_argument("--out", default="")
    ap.add_argument("--zones", default=DEFAULT_ZONES, help="comma list of zone names or prefix* patterns ('' for none)")
    ap.add_argument("--max-busy", type=float, default=0.0)
    ap.add_argument("--retries", type=int, default=3)
    a = ap.parse_args(argv)
    try:
        counts = parse_counts(a.workers)
    except ValueError:
        print("worker_sweep: --workers wants a comma list of worker counts (integers or auto)", file=sys.stderr)
        return 2
    names = [s for s in a.scenes.split(",") if s]
    if not counts or not names or a.rounds < 1:
        print("worker_sweep: nothing to run (--scenes, --workers, --rounds)", file=sys.stderr)
        return 2
    unknown = [n for n in names if not os.path.isfile(os.path.join(RECIPES, n + ".abench"))]
    if unknown and not a.scenes_dir:
        print(f"worker_sweep: no recipe tests/benchmark/<name>.abench for {', '.join(unknown)}", file=sys.stderr)
        return 2
    exe = os.path.join(a.build, "bin", "alamo_headless.exe")
    if runner is run_cmd and not os.path.isfile(exe):
        print(f"worker_sweep: {exe} not found (dev.cmd build alamo_headless first)", file=sys.stderr)
        return 2
    out = a.out or os.path.join(a.build, "worker_sweep", datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    if a.scenes_dir:
        scenes = {n: os.path.join(a.scenes_dir, n + ".abench") for n in names}
        missing = [n for n, p in scenes.items() if not os.path.isfile(p)]
        if missing:
            print(f"worker_sweep: {', '.join(missing)} not made in {a.scenes_dir}", file=sys.stderr)
            return 2
    else:
        try:
            scenes = make_scenes(names, a.build, os.path.join(out, "scenes"))
        except RuntimeError as e:
            print(f"worker_sweep: {e}", file=sys.stderr)
            return 2
    results: dict = {n: {} for n in names}
    failed = False
    for rnd in range(1, a.rounds + 1):
        for name in names:
            for c in counts:
                res = os.path.join(out, f"{name}_w{c}_r{rnd}.json")
                cmd = [exe, "--bench", scenes[name], "--runs", "1", "--warmup", "0"] + (["--workers", c] if c != "auto" else []) \
                    + ["--quiet", "--bench-json", res] + extra
                d: dict = {}
                rc, b = 1, None
                tries = a.retries + 1 if a.max_busy > 0 else 1
                for attempt in range(tries):
                    if attempt:
                        sleep(30)
                    b = busy()
                    if os.path.isfile(res):
                        os.remove(res)
                    rc, text = runner(cmd)
                    with open(res[:-5] + ".log", "w", encoding="utf-8") as f:
                        f.write(text)
                    try:
                        with open(res, encoding="utf-8") as f:
                            d = json.load(f)
                    except (OSError, ValueError):
                        d = {}
                    other = d.get("summary", {}).get("max_other_cpu_pct")
                    loaded = (b is not None and b > a.max_busy) or (isinstance(other, (int, float)) and other > a.max_busy)
                    if a.max_busy <= 0 or not loaded:
                        break
                    if attempt + 1 < tries:
                        print(f"  round {rnd} {name} W{c}: busy (before {fmt(b, 0)} %, during {fmt(other, 0)} %), retrying",
                              flush=True)
                    else:
                        print(f"  round {rnd} {name} W{c}: still busy after {a.retries} retries, kept", flush=True)
                ok = rc == 0 and d.get("comparable", False)
                failed = failed or not ok
                d["busy_before_pct"] = b
                results[name].setdefault(c, []).append(d)
                print(f"  round {rnd} {name} W{c}: median {fmt(d.get('summary', {}).get('median_ms'))} ms, busy before {fmt(b, 0)} %"
                      + ("" if ok else f"  FAILED (exit {rc}{'' if d.get('comparable') else ', not comparable'})"), flush=True)
    pats = [p for p in a.zones.split(",") if p]
    summary = summarize(results, counts, pats)
    summary.update(rounds=a.rounds, extra_args=extra, date_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"))
    with open(os.path.join(out, "sweep.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    md = markdown(summary)
    with open(os.path.join(out, "sweep.md"), "w", encoding="utf-8") as f:
        f.write(md)
    print("\n" + md)
    print(f"worker_sweep: results in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
