#!/usr/bin/env python3
"""The exact-package smoke test (#30; #27 decision 4): one internal package (dev.cmd package; README.md "Internal
packages"), unpacked, every checksum verified, then the data-free checks run from the unpacked folder.

  package_smoke.py check PACKAGE.zip [--sums SHA256SUMS]
        unpack into a temporary folder; SHA256SUMS must list every file in the package, and nothing else, with the
        right SHA-256 (and equal --sums, the copy attached to the release); MANIFEST.json must agree; then
          alamo_game --list-flags --json        equals launcher/src/testdata/game_flags.json
          alamo_headless --selfcheck
          alamo_headless --replay engine/apps/headless/testdata/replay_stream.acmd --check-workers
  package_smoke.py selftest

Runs from a checkout (for the flag list and the replay fixture). Standard library only.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLAGS = os.path.join(ROOT, "launcher", "src", "testdata", "game_flags.json")
REPLAY = os.path.join(ROOT, "engine", "apps", "headless", "testdata", "replay_stream.acmd")


class SmokeError(Exception):
    pass


def unpack(package_zip: str, into: str) -> str:
    """Extracts the zip; returns its one top folder (alamo-<sha>)."""
    with zipfile.ZipFile(package_zip) as z:
        tops = {n.split("/", 1)[0] for n in z.namelist()}
        if len(tops) != 1 or any("/" not in n for n in z.namelist()):
            raise SmokeError(f"{package_zip}: a package zip holds one top folder, found {sorted(tops)}")
        z.extractall(into)
    return os.path.join(into, tops.pop())


def read_sums(path: str) -> dict[str, str]:
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            digest, _, name = line.rstrip("\r\n").partition("  ")
            if len(digest) != 64 or not name:
                raise SmokeError(f"{path}: not a SHA256SUMS line: {line!r}")
            out[name] = digest.lower()
    return out


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(folder: str, release_sums: str | None = None) -> int:
    """Checks SHA256SUMS against the folder and MANIFEST.json; returns the number of files verified."""
    sums = read_sums(os.path.join(folder, "SHA256SUMS"))
    if release_sums is not None and read_sums(release_sums) != sums:
        raise SmokeError("the release's SHA256SUMS differs from the one inside the package")
    present = {os.path.relpath(os.path.join(d, f), folder).replace(os.sep, "/")
               for d, _, names in os.walk(folder) for f in names} - {"SHA256SUMS"}
    problems = [f"{p}: in the package but not listed in SHA256SUMS" for p in sorted(present - set(sums))]
    problems += [f"{p}: listed in SHA256SUMS but missing" for p in sorted(set(sums) - present)]
    problems += [f"{p}: checksum mismatch" for p in sorted(present & set(sums))
                 if sha256(os.path.join(folder, p)) != sums[p]]
    with open(os.path.join(folder, "MANIFEST.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    listed = {p: e["sha256"] for p, e in manifest.get("files", {}).items()}
    expected = {p: d for p, d in sums.items() if p != "MANIFEST.json"}
    if listed != expected:
        problems.append("MANIFEST.json: its files and checksums differ from SHA256SUMS")
    if problems:
        raise SmokeError("; ".join(problems))
    return len(sums)


def run_checks(folder: str) -> list[str]:
    """Runs the data-free checks from the unpacked folder; returns the names of the failed ones."""
    game, headless = os.path.join(folder, "alamo_game.exe"), os.path.join(folder, "alamo_headless.exe")
    failed = []

    def run(name: str, cmd: list[str]) -> subprocess.CompletedProcess:
        print(f"package_smoke: {name}: {subprocess.list2cmdline(cmd)}", flush=True)
        try:
            p = subprocess.run(cmd, cwd=folder, capture_output=True, text=True, errors="replace", timeout=600)
        except subprocess.TimeoutExpired as e:  # a hang is a failed check, not a crash of the smoke test
            failed.append(f"{name} (no exit after {e.timeout:.0f} s)")
            return subprocess.CompletedProcess(cmd, -1, "", "")
        tail = (p.stdout + p.stderr).strip().splitlines()[-5:]
        print("\n".join("  " + line for line in tail), flush=True)
        if p.returncode != 0:
            failed.append(f"{name} (exit {p.returncode})")
        return p

    p = run("flag list", [game, "--list-flags", "--json"])
    with open(FLAGS, encoding="utf-8") as f:
        if p.returncode == 0 and p.stdout.replace("\r\n", "\n").strip() != f.read().strip():
            failed.append(f"flag list (differs from {os.path.relpath(FLAGS, ROOT)})")
    run("selfcheck", [headless, "--selfcheck"])
    run("replay worker-count check", [headless, "--replay", REPLAY, "--check-workers"])
    return failed


def cmd_check(argv: list[str]) -> int:
    if not argv or argv[0].startswith("-"):
        print(__doc__)
        return 2
    package_zip = os.path.abspath(argv[0])
    if argv[-1] == "--sums":
        print("package_smoke: error: --sums needs the release's SHA256SUMS file")
        return 2
    release_sums = argv[argv.index("--sums") + 1] if "--sums" in argv else None
    with tempfile.TemporaryDirectory(prefix="alamo_package_smoke_") as d:
        try:
            folder = unpack(package_zip, d)
            n = verify(folder, release_sums)
        except SmokeError as e:
            print(f"package_smoke: error: {e}")
            return 1
        print(f"package_smoke: {n} checksums verified in {os.path.basename(folder)}", flush=True)
        failed = run_checks(folder)
    if failed:
        print("package_smoke: error: failed: " + ", ".join(failed))
        return 1
    print("package_smoke: the package passed")
    return 0


class _Tests(unittest.TestCase):
    def make_package(self, d: str, files: dict[str, bytes], sums: dict[str, str] | None = None,
                     manifest: dict | None = None) -> str:
        """A package zip as dev.cmd package writes it: alamo-x/<files>, MANIFEST.json and SHA256SUMS."""
        def sha(b: bytes) -> str:
            return hashlib.sha256(b).hexdigest()

        files = dict(files)
        if manifest is None:
            manifest = {"commit": "x", "files": {p: {"sha256": sha(b), "bytes": len(b)} for p, b in files.items()}}
        files["MANIFEST.json"] = json.dumps(manifest).encode()
        if sums is None:
            sums = {p: sha(b) for p, b in files.items()}
        files["SHA256SUMS"] = "".join(f"{sums[p]}  {p}\n" for p in sorted(sums)).encode()
        path = os.path.join(d, "alamo-x.zip")
        with zipfile.ZipFile(path, "w") as z:
            for p, b in files.items():
                z.writestr(f"alamo-x/{p}", b)
        return path

    def unpacked(self, d: str, **kw) -> str:
        return unpack(self.make_package(d, {"a.exe": b"abc", "licenses/l.txt": b"l"}, **kw), os.path.join(d, "u"))

    def test_a_good_package_verifies_every_file(self):
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d)
            self.assertEqual(os.path.basename(folder), "alamo-x")
            self.assertEqual(verify(folder), 3)  # a.exe, licenses/l.txt, MANIFEST.json

    def test_a_changed_byte_fails_and_names_the_file(self):
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d)
            with open(os.path.join(folder, "licenses", "l.txt"), "wb") as f:
                f.write(b"L")
            with self.assertRaisesRegex(SmokeError, r"licenses/l\.txt.*checksum"):
                verify(folder)

    def test_an_unlisted_or_missing_file_fails(self):
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d)
            with open(os.path.join(folder, "extra.dll"), "wb") as f:
                f.write(b"x")
            with self.assertRaisesRegex(SmokeError, r"extra\.dll.*not listed"):
                verify(folder)
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d)
            os.remove(os.path.join(folder, "a.exe"))
            with self.assertRaisesRegex(SmokeError, r"a\.exe.*missing"):
                verify(folder)

    def test_the_manifest_must_agree_with_the_sums(self):
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d, manifest={"commit": "x", "files": {"a.exe": {"sha256": "0" * 64, "bytes": 3}}})
            with self.assertRaisesRegex(SmokeError, r"MANIFEST\.json"):
                verify(folder)

    def test_the_release_copy_of_the_sums_must_match(self):
        with tempfile.TemporaryDirectory() as d:
            folder = self.unpacked(d)
            good = os.path.join(folder, "SHA256SUMS")
            self.assertEqual(verify(folder, good), 3)
            other = os.path.join(d, "SHA256SUMS")
            with open(other, "w", encoding="utf-8") as f:
                f.write("0" * 64 + "  a.exe\n")
            with self.assertRaisesRegex(SmokeError, r"release's SHA256SUMS"):
                verify(folder, other)

    def test_a_zip_without_one_top_folder_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "bad.zip")
            with zipfile.ZipFile(path, "w") as z:
                z.writestr("a.exe", b"abc")
                z.writestr("b/c.txt", b"c")
            with self.assertRaisesRegex(SmokeError, "one top folder"):
                unpack(path, os.path.join(d, "u"))


def main(argv: list[str]) -> int:
    if argv[:1] == ["selftest"]:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
        return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1
    if argv[:1] == ["check"]:
        return cmd_check(argv[1:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
