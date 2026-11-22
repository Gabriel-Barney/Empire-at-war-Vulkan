#!/usr/bin/env python3
"""Machine-wide test slots: at most N dev.cmd test runs at once on this machine (README.md "Test slots").

  test_slots.py hold SECONDS      take a slot, hold it, release it (used by the self-test)
  test_slots.py status            list the slots and who holds them
  test_slots.py selftest          this script's own tests (CTest alamo_test_slots_selftest)

dev.cmd check, test and suite take a slot around the CTest run, so a second job on this machine waits for the first
instead of running beside it and timing out. ALAMO_TEST_SLOTS sets the number of slots (default 1); the slots live
in %LOCALAPPDATA%\\alamo-dev\\test-slots, or ALAMO_TEST_SLOT_DIR. A slot is a Windows byte-range lock on a file, which
the system releases when the process exits, however it exits, so a crashed job never keeps one. The holder's command
and process id sit next to it for the waiting job to print. Standard library only.
"""

from __future__ import annotations

import contextlib
import datetime
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

try:
    import msvcrt
except ImportError:  # not Windows: no slots (the project builds only on Windows)
    msvcrt = None


def slot_dir() -> str:
    d = os.environ.get("ALAMO_TEST_SLOT_DIR") or os.path.join(
        os.environ.get("LOCALAPPDATA") or tempfile.gettempdir(), "alamo-dev", "test-slots")
    os.makedirs(d, exist_ok=True)
    return d


def slot_count() -> int:
    try:
        return max(1, int(os.environ.get("ALAMO_TEST_SLOTS", "1")))
    except ValueError:
        return 1


def _info_path(d: str, i: int) -> str:
    return os.path.join(d, f"slot-{i}.json")


def holder(d: str, i: int) -> dict:
    try:
        with open(_info_path(d, i), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


class Slot:
    """A held slot; release() or process exit frees it."""

    def __init__(self, fd: int, index: int, d: str):
        self.fd, self.index, self.dir = fd, index, d

    def release(self) -> None:
        if self.fd is None:
            return
        with contextlib.suppress(OSError):
            os.remove(_info_path(self.dir, self.index))
        with contextlib.suppress(OSError):
            msvcrt.locking(self.fd, msvcrt.LK_UNLCK, 1)
        os.close(self.fd)
        self.fd = None


def try_acquire(command: str, d: str | None = None, count: int | None = None) -> Slot | None:
    if msvcrt is None:
        return Slot(None, -1, "")
    d, count = d or slot_dir(), count or slot_count()
    for i in range(count):
        fd = os.open(os.path.join(d, f"slot-{i}.lock"), os.O_RDWR | os.O_CREAT)
        try:
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        except OSError:
            os.close(fd)
            continue
        info = {"pid": os.getpid(), "command": command, "cwd": os.getcwd(),
                "since": datetime.datetime.now().isoformat(timespec="seconds")}
        with open(_info_path(d, i), "w", encoding="utf-8") as f:
            json.dump(info, f)
        return Slot(fd, i, d)
    return None


def acquire(command: str, d: str | None = None, count: int | None = None, poll: float = 1.0, say=print) -> Slot:
    """Waits for a slot, saying once which job holds the slots."""
    d, count = d or slot_dir(), count or slot_count()
    said = False
    start = time.monotonic()
    while True:
        slot = try_acquire(command, d, count)
        if slot:
            if said:
                say(f"dev.cmd: got a test slot after {time.monotonic() - start:.0f} s")
            return slot
        if not said:
            held = [holder(d, i) for i in range(count)]
            who = "; ".join(f"pid {h.get('pid', '?')}: {h.get('command', '?')} in {h.get('cwd', '?')} since "
                            f"{h.get('since', '?')}" for h in held)
            say(f"dev.cmd: waiting for a test slot ({count} on this machine, ALAMO_TEST_SLOTS): {who}")
            said = True
        time.sleep(poll)


def main(argv: list[str]) -> int:
    if argv == ["selftest"]:
        return selftest()
    if argv == ["status"]:
        d, n = slot_dir(), slot_count()
        for i in range(n):
            h = holder(d, i)
            print(f"slot {i}: " + (f"pid {h.get('pid')}: {h.get('command')} in {h.get('cwd')} since {h.get('since')}"
                                   if h else "free"))
        return 0
    if len(argv) == 2 and argv[0] == "hold":
        slot = acquire(f"test_slots.py hold {argv[1]}", poll=0.2)
        print(f"holding slot {slot.index}", flush=True)
        time.sleep(float(argv[1]))
        slot.release()
        print("released", flush=True)
        return 0
    print(__doc__)
    return 2


# ------------------------------------------------------------------------------------------------- self-test

class _Tests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="alamo_slots_")
        self.env = dict(os.environ, ALAMO_TEST_SLOT_DIR=self.dir, ALAMO_TEST_SLOTS="1")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.dir, ignore_errors=True)

    def hold(self, seconds: float) -> subprocess.Popen:
        # The base interpreter, not a venv's python.exe: that one is a launcher whose child Python holds the lock, so
        # its pid would not be the holder's and killing it would leave the child running.
        python = getattr(sys, "_base_executable", None) or sys.executable
        return subprocess.Popen([python, "-I", os.path.abspath(__file__), "hold", str(seconds)], env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def wait_for_holder(self, timeout: float = 20.0) -> None:
        end = time.monotonic() + timeout
        while not holder(self.dir, 0) and time.monotonic() < end:
            time.sleep(0.05)
        self.assertTrue(holder(self.dir, 0), "the first job never took the slot")

    @unittest.skipIf(msvcrt is None, "Windows only")
    def test_second_job_waits_names_the_holder_and_then_runs(self):
        first = self.hold(2)
        self.wait_for_holder()
        start = time.monotonic()
        second = self.hold(0)
        out2, _ = second.communicate(timeout=60)
        out1, _ = first.communicate(timeout=60)
        self.assertEqual((first.returncode, second.returncode), (0, 0), out1 + out2)
        self.assertIn("waiting for a test slot", out2)
        self.assertIn(f"pid {first.pid}: test_slots.py hold 2", out2)
        self.assertIn("got a test slot", out2)
        self.assertGreater(time.monotonic() - start, 1.0)  # it waited for the first job rather than running beside it

    @unittest.skipIf(msvcrt is None, "Windows only")
    def test_slot_of_a_killed_job_is_free_at_once(self):
        crashed = self.hold(60)
        self.wait_for_holder()
        self.assertIsNone(try_acquire("probe", self.dir, 1))
        crashed.kill()  # no release(): the system drops the lock with the process
        crashed.wait(timeout=30)
        slot = try_acquire("probe", self.dir, 1)
        self.assertIsNotNone(slot)
        self.assertEqual(holder(self.dir, 0)["pid"], os.getpid())
        slot.release()
        self.assertEqual(holder(self.dir, 0), {})

    @unittest.skipIf(msvcrt is None, "Windows only")
    def test_two_slots_run_two_jobs_side_by_side(self):
        a = try_acquire("a", self.dir, 2)
        b = try_acquire("b", self.dir, 2)
        self.assertEqual({a.index, b.index}, {0, 1})
        self.assertIsNone(try_acquire("c", self.dir, 2))
        a.release(), b.release()

    def test_slot_count_from_the_environment(self):
        old = os.environ.get("ALAMO_TEST_SLOTS")
        try:
            for value, want in (("3", 3), ("0", 1), ("x", 1)):
                os.environ["ALAMO_TEST_SLOTS"] = value
                self.assertEqual(slot_count(), want)
        finally:
            os.environ.pop("ALAMO_TEST_SLOTS", None) if old is None else os.environ.__setitem__("ALAMO_TEST_SLOTS", old)


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
