#!/usr/bin/env python3
"""The roadmap flowchart's file, docs/roadmap/flow.json (README.md "Roadmap").

flow.json holds the stages, top to bottom, and in each stage its boxes; a box's "to" names the boxes it leads to
("solid": needed first, "dashed": feeds into). The work itself is the GitHub issues: an issue belongs to a box
through the label roadmap:<box id>, and the page (docs/roadmap/index.html) reads both live.

  roadmap_flow.py check     the file is well formed (ids, edges, fields); run by CTest
  roadmap_flow.py labels    create the roadmap:<box id> label of every box that lacks one (gh)
  roadmap_flow.py selftest

Standard library only.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLOW = os.path.join(ROOT, "docs", "roadmap", "flow.json")
LABEL_PREFIX = "roadmap:"
BOX_ID = re.compile(r"^[A-Z][A-Z0-9]*$")
STAGE_KEYS = {"id", "title", "goal", "exit", "boxes"}
BOX_KEYS = {"id", "label", "note", "size", "done", "to"}
EDGE_KINDS = {"solid", "dashed"}


def problems(flow: dict) -> list[str]:
    """Everything wrong with a flow.json document; empty when it is well formed."""
    out = []
    if not isinstance(flow.get("title"), str) or not re.fullmatch(r"[\w.-]+/[\w.-]+", str(flow.get("repo", ""))):
        out.append("the file needs a title and a repo (owner/name)")
    stages = flow.get("stages")
    if not isinstance(stages, list) or not stages:
        return out + ["the file needs a list of stages"]
    boxes, stage_ids = {}, set()
    for s in stages:
        sid = s.get("id")
        if not sid or sid in stage_ids:
            out.append(f"stage {sid!r}: missing or repeated id")
        stage_ids.add(sid)
        if not s.get("title"):
            out.append(f"stage {sid}: no title")
        out += [f"stage {sid}: unknown field {k!r}" for k in sorted(set(s) - STAGE_KEYS)]
        for b in s.get("boxes", []):
            bid = b.get("id", "")
            if not BOX_ID.match(bid):
                out.append(f"box {bid!r} in stage {sid}: an id is upper-case letters and digits")
            elif bid in boxes:
                out.append(f"box {bid}: repeated (also in stage {boxes[bid]})")
            boxes[bid] = sid
            if not b.get("label"):
                out.append(f"box {bid}: no label")
            if b.get("size", "wide") != "wide":
                out.append(f"box {bid}: size is \"wide\" or left out")
            if not isinstance(b.get("done", False), bool):
                out.append(f"box {bid}: done is true or left out")
            out += [f"box {bid}: unknown field {k!r}" for k in sorted(set(b) - BOX_KEYS)]
    for s in stages:
        for b in s.get("boxes", []):
            for target, kind in b.get("to", {}).items():
                if target not in boxes:
                    out.append(f"box {b.get('id')}: leads to {target}, which is not a box")
                if kind not in EDGE_KINDS:
                    out.append(f"box {b.get('id')}: the edge to {target} is {kind!r}, not solid or dashed")
                if target == b.get("id"):
                    out.append(f"box {target}: leads to itself")
    return out


def box_ids(flow: dict) -> list[str]:
    return [b["id"] for s in flow["stages"] for b in s.get("boxes", [])]


def load(path: str = FLOW) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main(argv: list[str]) -> int:
    if argv[:1] == ["selftest"]:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
        return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1
    if argv[:1] == ["check"]:
        found = problems(load())
        for p in found:
            print(f"docs/roadmap/flow.json: {p}")
        if not found:
            print(f"docs/roadmap/flow.json: {len(box_ids(load()))} boxes, well formed")
        return 1 if found else 0
    if argv[:1] == ["labels"]:
        flow = load()
        if problems(flow):
            print("roadmap_flow.py: fix flow.json first (roadmap_flow.py check)")
            return 1
        existing = set(json.loads(subprocess.run(
            ["gh", "label", "list", "--repo", flow["repo"], "--limit", "1000", "--json", "name", "-q", "[.[].name]"],
            capture_output=True, text=True, check=True).stdout))
        labels = {b["id"]: b["label"] for s in flow["stages"] for b in s["boxes"]}
        for bid, text in labels.items():
            if LABEL_PREFIX + bid not in existing:
                subprocess.run(["gh", "label", "create", LABEL_PREFIX + bid, "--repo", flow["repo"], "--color", "5319e7",
                                "--description", f"Roadmap box: {text}"[:100]], check=True)
                print(f"created {LABEL_PREFIX}{bid}")
        return 0
    print(__doc__)
    return 2


class _Tests(unittest.TestCase):
    def flow(self, **box) -> dict:
        b = {"id": "A1", "label": "A", "to": {"B": "solid"}, **box}
        return {"title": "t", "repo": "o/r", "stages": [{"id": "s0", "title": "S", "boxes": [b, {"id": "B", "label": "b"}]}]}

    def test_a_good_flow_has_no_problems(self):
        self.assertEqual(problems(self.flow()), [])
        self.assertEqual(problems(self.flow(done=True, size="wide", note="n")), [])

    def test_an_edge_to_a_missing_box_is_named(self):
        self.assertEqual(problems(self.flow(to={"C": "solid"})), ["box A1: leads to C, which is not a box"])

    def test_bad_ids_kinds_and_fields_are_named(self):
        self.assertIn("box 'a-1' in stage s0: an id is upper-case letters and digits", problems(self.flow(id="a-1")))
        self.assertIn("box A1: the edge to B is 'dash', not solid or dashed", problems(self.flow(to={"B": "dash"})))
        self.assertIn("box A1: unknown field 'colour'", problems(self.flow(colour="red")))
        self.assertIn("box B: repeated (also in stage s0)", problems(self.flow(id="B", to={})))

    def test_the_checked_in_flow_is_well_formed(self):
        flow = load()
        self.assertEqual(problems(flow), [])
        self.assertGreater(len(box_ids(flow)), 0)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
