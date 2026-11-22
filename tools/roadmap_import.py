#!/usr/bin/env python3
"""Imports the open tier-1 and tier-2 work of the old roadmap store as issues (#32; #27 decision 7).

The store (the private repository Gabriel-Barney/Empire-Vulkan-Brenach, checked out locally) holds groups in
data/items/<id>.json (each with tasks t01..., every task with a status s and often a tier) and findings in
data/backlog/<id>.json. What is imported:

  group     status "partly" with at least one open (todo) tier-1 or tier-2 task: one issue for the group, listing
            every open task with its tier. The group's tier is its lowest open task tier.
  backlog   status "open", tier 1 or 2: one issue each. (A promoted entry became a group's task.)

Everything else open stays in the store, counted with the reason: parked or waiting-for-Gabriel groups, groups whose
open tasks are all tier 3 or untiered, tier-3 and untiered backlog entries ("Current focus: parity tiers 1 and 2
only" in docs/DECISIONS.md).

Each issue is labelled `imported` and `needs-triage` and carries a hidden marker with the old id, so running the import
again creates no duplicates. Its body names the old id, the store commit and the old repository's source commit.

  roadmap_import.py plan  --store DIR    what would be imported and left behind (no network)
  roadmap_import.py apply --store DIR    create the missing issues with gh (needs the store at a clean commit)
  roadmap_import.py selftest

Standard library only.
"""

from __future__ import annotations

import glob
import json
import os
import re
import subprocess
import sys
import unittest

STORE_REPO = "Gabriel-Barney/Empire-Vulkan-Brenach"
OLD_REPO = "Gabriel-Barney/Empire-at-war-vulkan-old"
OLD_COMMIT = "7f911b51f0d12d0d10e8218e726af3d7ace5d9c5"  # the old repository's last commit (docs/LEGACY_DOCS.md)
LABELS = ("imported", "needs-triage")
MARKER = re.compile(r"<!-- imported: ([\w./-]+) -->")


def tier_of(x: dict) -> int | None:
    t = str(x.get("tier", "")).strip()
    return int(t) if t in ("1", "2", "3") else None


def plan(groups: dict[str, dict], backlog: dict[str, dict]) -> tuple[list[dict], list[tuple[str, str]]]:
    """(imports, left behind as (old id, reason)). Imports are sorted by tier, then group order or backlog id."""
    imports, left = [], []
    for gid, g in groups.items():
        open_tasks = {tid: t for tid, t in g.get("tasks", {}).items() if t.get("s") == "todo"}
        if not open_tasks:
            continue
        tiers = {tier_of(t) for t in open_tasks.values()}
        if g.get("status") != "partly":
            left.append((gid, f"group {g.get('status')}"))
        elif not tiers & {1, 2}:
            left.append((gid, "no open tier-1 or tier-2 task"))
        else:
            imports.append({"id": gid, "kind": "group", "tier": min(tiers & {1, 2}), "order": g.get("order", 0),
                            "data": g, "tasks": open_tasks})
    for bid, b in backlog.items():
        if b.get("s") != "open":
            continue
        tier = tier_of(b)
        if tier in (1, 2):
            imports.append({"id": bid, "kind": "backlog", "tier": tier, "order": 0, "data": b})
        else:
            left.append((bid, f"backlog tier {tier or 'none'}"))
    imports.sort(key=lambda i: (i["tier"], i["kind"] != "group", i["order"], i["id"]))
    left.sort()
    return imports, left


def title(i: dict) -> str:
    d = i["data"]
    if i["kind"] == "group":
        return f"[T{i['tier']}] {d.get('title', i['id'])}"
    text = re.sub(r"\s+", " ", d.get("t", "")).strip()
    return f"[T{i['tier']}] {i['id']}: " + (text if len(text) <= 90 else text[:87].rstrip() + "...")


def body(i: dict, store_commit: str) -> str:
    d = i["data"]
    path = f"data/{'items' if i['kind'] == 'group' else 'backlog'}/{i['id']}.json"
    lines = [
        f"Imported from the old roadmap (#32): {i['kind']} `{i['id']}`, tier {i['tier']}.",
        "",
        f"- Old id: `{i['id']}` ({path} in the private roadmap store, commit `{store_commit[:12]}`)",
        f"- Old repository: [`{OLD_COMMIT[:8]}`](https://github.com/{OLD_REPO}/tree/{OLD_COMMIT})",
    ]
    for key, label in (("node", "Node"), ("mod", "Modules"), ("cat", "Category"), ("sev", "Severity"),
                       ("kind", "Kind")):
        if d.get(key) and not (i["kind"] == "group" and key == "kind"):
            lines.append(f"- {label}: {d[key]}")
    if i["kind"] == "group":
        for key, label in (("why", "Why"), ("exit", "Done when")):
            if d.get(key):
                lines += ["", f"**{label}:** {d[key]}"]
        lines += ["", "**Open tasks:**", ""]
        for tid, t in sorted(i["tasks"].items(), key=lambda kv: (kv[1].get("o", 0), kv[0])):
            tier = tier_of(t)
            meta = ", ".join(x for x in (f"tier {tier}" if tier else "no tier", t.get("sev"), t.get("sub")) if x)
            lines.append(f"- [ ] `{i['id']}/{tid}` ({meta}): {t.get('t', '').strip()}")
    else:
        lines += ["", d.get("t", "").strip()]
        if d.get("why"):
            lines += ["", f"**Why:** {d['why']}"]
    lines += ["", f"<!-- imported: {i['id']} -->"]
    return "\n".join(lines) + "\n"


def imported_ids(issue_bodies: list[str]) -> set[str]:
    return {m.group(1) for b in issue_bodies for m in MARKER.finditer(b or "")}


def load_store(store: str) -> tuple[dict, dict]:
    def read(kind: str) -> dict:
        out = {}
        for f in sorted(glob.glob(os.path.join(store, "data", kind, "*.json"))):
            with open(f, encoding="utf-8") as fh:
                out[os.path.splitext(os.path.basename(f))[0]] = json.load(fh)
        return out

    groups, backlog = read("items"), read("backlog")
    if not groups and not backlog:
        raise SystemExit(f"roadmap_import.py: no data/items or data/backlog under {store}")
    return groups, backlog


def gh(args: list[str], stdin: str | None = None) -> str:
    p = subprocess.run(["gh", *args], input=stdin, capture_output=True, text=True, encoding="utf-8")
    if p.returncode:
        raise SystemExit(f"roadmap_import.py: gh {' '.join(args[:3])} failed: {p.stderr.strip()}")
    return p.stdout


def summary(imports: list[dict], left: list[tuple[str, str]]) -> list[str]:
    def count(items, key):
        out = {}
        for x in items:
            out[key(x)] = out.get(key(x), 0) + 1
        return ", ".join(f"{v} {k}" for k, v in sorted(out.items()))

    return [f"import: {len(imports)} ({count(imports, lambda i: 'tier-%d %s' % (i['tier'], i['kind']))})",
            f"left in the store: {len(left)} ({count(left, lambda x: x[1])})"]


def main(argv: list[str]) -> int:
    if argv[:1] == ["selftest"]:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
        return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1
    if len(argv) != 3 or argv[0] not in ("plan", "apply") or argv[1] != "--store":
        print(__doc__)
        return 2
    store = argv[2]
    commit = subprocess.run(["git", "-C", store, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if not commit:
        raise SystemExit(f"roadmap_import.py: {store} is not a git checkout of {STORE_REPO}")
    imports, left = plan(*load_store(store))
    if argv[0] == "plan":
        for i in imports:
            print(f"T{i['tier']} {i['kind']:7} {i['id']:8} {title(i)}")
        for old, why in left:
            print(f"left    {old:8} {why}")
        print("\n".join(summary(imports, left)))
        return 0
    if subprocess.run(["git", "-C", store, "status", "--porcelain"], capture_output=True, text=True).stdout.strip():
        raise SystemExit("roadmap_import.py: the store checkout has local changes; import from a clean commit")
    bodies = json.loads(gh(["issue", "list", "--label", "imported", "--state", "all", "--limit", "1000", "--json",
                            "body"]) or "[]")
    done = imported_ids([b["body"] for b in bodies])
    for label in LABELS:
        gh(["label", "create", label, "--force", "--description",
            "Imported from the old roadmap store (#32)" if label == "imported" else "Needs triage"])
    created = 0
    for i in imports:
        if i["id"] in done:
            continue
        url = gh(["issue", "create", "--title", title(i), "--body-file", "-", *sum((["--label", x] for x in LABELS),
                                                                                    [])], body(i, commit)).strip()
        print(f"{i['id']}: {url}", flush=True)
        created += 1
    print(f"created {created}, already imported {len(imports) - created}")
    print("\n".join(summary(imports, left)))
    return 0


class _Tests(unittest.TestCase):
    GROUPS = {
        "a1": {"status": "partly", "order": 5, "title": "A1 Thing", "node": "AI", "mod": "ai",
               "tasks": {"t01": {"s": "done", "tier": "1", "t": "x"}, "t02": {"s": "todo", "tier": "2", "t": "y"},
                         "t03": {"s": "todo", "tier": "1", "t": "z", "sev": "S2", "sub": "Parity", "o": 3}}},
        "b2": {"status": "partly", "order": 1, "title": "B2", "tasks": {"t01": {"s": "todo", "tier": "2", "t": "w"}}},
        "c3": {"status": "partly", "order": 2, "title": "C3", "tasks": {"t01": {"s": "todo", "tier": "3", "t": "v"},
                                                                         "t02": {"s": "todo", "t": "u"}}},
        "d4": {"status": "parked", "order": 3, "title": "D4", "tasks": {"t01": {"s": "todo", "tier": "1", "t": "q"}}},
        "e5": {"status": "done", "order": 4, "title": "E5", "tasks": {"t01": {"s": "done", "tier": "1", "t": "p"}}},
    }
    BACKLOG = {
        "b0001": {"s": "open", "tier": "1", "t": "Open finding", "sev": "S2", "cat": "audio"},
        "b0002": {"s": "promoted", "tier": "1", "t": "Became a task"},
        "b0003": {"s": "open", "tier": "3", "t": "Tier three"},
        "b0004": {"s": "open", "t": "No tier"},
        "b0005": {"s": "dropped", "tier": "2", "t": "Dropped"},
    }

    def test_open_tier_1_and_2_work_is_imported_in_order(self):
        imports, _ = plan(self.GROUPS, self.BACKLOG)
        self.assertEqual([(i["id"], i["tier"]) for i in imports], [("a1", 1), ("b0001", 1), ("b2", 2)])
        self.assertEqual(sorted(imports[0]["tasks"]), ["t02", "t03"])  # every open task, done ones left out

    def test_the_rest_is_left_behind_with_a_reason(self):
        _, left = plan(self.GROUPS, self.BACKLOG)
        self.assertEqual(left, [("b0003", "backlog tier 3"), ("b0004", "backlog tier none"),
                                ("c3", "no open tier-1 or tier-2 task"), ("d4", "group parked")])

    def test_body_carries_the_old_id_both_commits_and_the_tier(self):
        imports, _ = plan(self.GROUPS, self.BACKLOG)
        b = body(imports[0], "b094710a56f3036007f87a353a77de642d2ff729")
        self.assertIn("group `a1`, tier 1", b)
        self.assertIn("`b094710a56f3`", b)
        self.assertIn(OLD_COMMIT, b)
        self.assertIn("- [ ] `a1/t03` (tier 1, S2, Parity): z", b)
        self.assertTrue(b.rstrip().endswith("<!-- imported: a1 -->"))
        self.assertEqual(title(imports[0]), "[T1] A1 Thing")
        self.assertEqual(title(imports[1]), "[T1] b0001: Open finding")

    def test_a_second_run_finds_what_was_imported(self):
        imports, _ = plan(self.GROUPS, self.BACKLOG)
        bodies = [body(i, "0" * 40) for i in imports] + ["unrelated", None]
        self.assertEqual(imported_ids(bodies), {"a1", "b0001", "b2"})

    def test_long_backlog_titles_are_cut(self):
        i = {"id": "b9", "kind": "backlog", "tier": 2, "data": {"t": "word " * 40}}
        self.assertLessEqual(len(title(i)), 110)
        self.assertTrue(title(i).endswith("..."))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
