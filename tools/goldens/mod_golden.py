#!/usr/bin/env python3
"""Run one mod scenario (tests/goldens/mods/<name>.json; roadmap mcx7 t04, docs/MODS.md "Determinism with mods").

A mod scenario is a short headless skirmish of a reference mod. Its CRC log used to be stored as tests/goldens/mods/<name>.crc;
since wr1 t07 (2026-11-21) it is a scenario of tools/goldens/scenarios.json, computed on the parent and the commit and
diffed by tools/goldens/behaviour_check.py (nightly tier). The spec file names the mod and the run:
  {"name": "...", "mod": "<workshop id>", "stack": ["STEAMMOD=<id>", ...], "args": [alamo_headless arguments],
   "fingerprint": "<data fingerprint of the mod when the spec was made; informational>"}
  python -I tools/goldens/mod_golden.py --spec S --exe alamo_headless.exe [more alamo_headless arguments, e.g. --crc-out F]
  python -I tools/goldens/mod_golden.py --spec S --fingerprint --mod-check alamo_mod_check.exe
      print the installed mod's fingerprint: the engine's mod content hash (vfs::mod_content_hash: file sizes plus each
      MEG's header and tables, no write times), read from alamo_mod_check --list-mods, so a re-downloaded or copied mod
      with the same bytes keeps it and the two definitions cannot drift (#349)
  python -I tools/goldens/mod_golden.py --selftest                 check the fingerprint lookup on a canned mod list
It skips (prints a "SKIP mod golden ..." line and exits 77, the skip code its retail ctest entry registers) when ALAMO_FOC_DATA is not set or the workshop item
is not installed (the Steam libraries: the one holding the FoC folder, then ALAMO_STEAM_ROOT or the default client's
libraryfolders.vdf, as docs/MODS.md "Command line switches"). Otherwise it runs alamo_headless --game <ALAMO_FOC_DATA> <args> <stack>
[--workers 4] <more arguments> and passes its exit code through (the ctest entry: the mod loads and runs without error).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

FOC_APP = "32470"


def steam_libraries(game: str) -> list[str]:
    libs: list[str] = []
    p = os.path.abspath(game)
    while True:  # the library holding the FoC folder: the parent of its nearest steamapps ancestor
        parent = os.path.dirname(p)
        if os.path.basename(p).lower() == "steamapps":
            libs.append(parent)
            break
        if parent == p:
            break
        p = parent
    roots = [r for r in os.environ.get("ALAMO_STEAM_ROOT", "").split(";") if r] or [r"C:\Program Files (x86)\Steam"]
    for root in roots:
        libs.append(root)
        for vdf in (os.path.join(root, "steamapps", "libraryfolders.vdf"), os.path.join(root, "config", "libraryfolders.vdf")):
            try:
                with open(vdf, encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in re.finditer(r'"(?:path|\d+)"\s+"([^"]+)"', text):
                libs.append(m.group(1).replace("\\\\", "\\"))
    out, seen = [], set()
    for lib in libs:
        k = os.path.normcase(os.path.abspath(lib))
        if k not in seen:
            seen.add(k)
            out.append(lib)
    return out


def find_mod(game: str, mod_id: str) -> str | None:
    for lib in steam_libraries(game):
        d = os.path.join(lib, "steamapps", "workshop", "content", FOC_APP, mod_id)
        if os.path.isdir(os.path.join(d, "Data")):
            return d
    return None


def fingerprint(mod_check: str, game: str, mod_id: str, run=subprocess.run) -> str | None:
    """The workshop item's content_hash (16 hex digits) in alamo_mod_check --list-mods, or None when it is not listed.

    The item's main modinfo entry (variant "") is preferred; its <variant>-modinfo.json entries share the folder and so
    the hash, and stand in when the folder has no main entry."""
    out = run([mod_check, "--game", game, "--list-mods", "--json", "-"], capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        raise RuntimeError(f"alamo_mod_check --list-mods failed ({out.returncode}): {out.stderr.strip()}")
    items = [m for m in json.loads(out.stdout).get("mods", []) if m.get("kind") == "workshop" and m.get("id") == mod_id]
    items.sort(key=lambda m: m.get("variant", "") != "")  # the main entry first (stable)
    return items[0].get("content_hash") if items else None


def _selftest() -> int:
    listing = {"schema": "alamo.mod_list", "version": 1, "game": "G", "mods": [
        {"key": "MODPATH=Mods/X", "kind": "local", "id": "1125571106", "variant": "", "content_hash": "1111111111111111"},
        {"key": "STEAMMOD=1125571106:v2", "kind": "workshop", "id": "1125571106", "variant": "v2", "content_hash": "3333333333333333"},
        {"key": "STEAMMOD=1125571106", "kind": "workshop", "id": "1125571106", "variant": "", "content_hash": "755e1df113e97c7e"},
        {"key": "STEAMMOD=2:only", "kind": "workshop", "id": "2", "variant": "only", "content_hash": "2222222222222222"}]}
    calls = []

    class Done:
        def __init__(self, code: int, text: str) -> None:
            self.returncode, self.stdout, self.stderr = code, text, "boom"

    def ok(cmd, **_):
        calls.append(cmd)
        return Done(0, json.dumps(listing))

    fails = 0
    checks = [
        (fingerprint("mc.exe", "G", "1125571106", ok) == "755e1df113e97c7e", "the workshop item's main entry, not a variant or a local mod"),
        (fingerprint("mc.exe", "G", "2", ok) == "2222222222222222", "a variant when the item has no main entry"),
        (fingerprint("mc.exe", "G", "3", ok) is None, "an item that is not listed"),
        (calls[0] == ["mc.exe", "--game", "G", "--list-mods", "--json", "-"], "the command line"),
    ]
    try:
        fingerprint("mc.exe", "G", "2", lambda cmd, **_: Done(1, ""))
        raised = False
    except RuntimeError:
        raised = True
    checks.append((raised, "a failing tool raises"))
    for good, what in checks:
        if not good:
            print("selftest FAIL:", what)
            fails += 1
    print("mod_golden selftest:", "ok" if fails == 0 else f"{fails} failed")
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--spec")
    ap.add_argument("--exe")
    ap.add_argument("--fingerprint", action="store_true")
    ap.add_argument("--mod-check", help="alamo_mod_check.exe, for --fingerprint")
    ap.add_argument("--selftest", action="store_true")
    a, rest = ap.parse_known_args()
    if a.selftest:
        return _selftest()
    if not a.spec:
        ap.error("--spec is required")
    with open(a.spec, encoding="utf-8") as f:
        spec = json.load(f)
    name = os.path.splitext(os.path.basename(a.spec))[0]
    game = os.environ.get("ALAMO_FOC_DATA", "")
    if not game or not os.path.isdir(game):
        print(f"SKIP mod golden {name}: ALAMO_FOC_DATA not set")
        return 77
    if a.fingerprint:
        if not a.mod_check:
            ap.error("--fingerprint needs --mod-check alamo_mod_check.exe")
        fp = fingerprint(a.mod_check, game, spec["mod"])
        if fp is None:
            print(f"mod golden {name}: alamo_mod_check does not list workshop item {spec['mod']}", file=sys.stderr)
            return 1
        print(fp)
        return 0
    mod_dir = find_mod(game, spec["mod"])
    if mod_dir is None:
        print(f"SKIP mod golden {name}: workshop item {spec['mod']} ({spec.get('name', '')}) not installed next to ALAMO_FOC_DATA")
        return 77
    if not a.exe:
        ap.error("--exe is required")
    cmd = [a.exe, "--game", game] + list(spec["args"]) + list(spec.get("stack") or ["STEAMMOD=" + spec["mod"]])
    if "--workers" not in rest and "--workers" not in spec["args"]:
        cmd += ["--workers", "4"]
    cmd += rest
    print("$ " + subprocess.list2cmdline(cmd), flush=True)
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
