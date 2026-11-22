#!/usr/bin/env python3
"""Machine-wide test slots: at most N dev.cmd test runs at once on this machine (README.md "Test slots").

  test_slots.py hold SECONDS      take a slot, hold it, release it (used by the self-test)
  test_slots.py status            list the slots and who holds them
  test_slots.py selftest          this script's own tests (CTest alamo_test_slots_selftest)
  test_slots.py concurrent        two real dev.cmd suite runs started at once (CTest alamo_test_slots_concurrent)

dev.cmd check, test and suite take a slot around the CTest run, so a second job on this machine waits for the first
instead of running beside it and timing out. ALAMO_TEST_SLOTS sets the number of slots (default 1); the slots live
in %ProgramData%\\alamo-dev\\test-slots, shared by every user, or ALAMO_TEST_SLOT_DIR. A slot is a Windows byte-range
lock on a file, which the system releases when the process exits, however it exits, so a crashed job never keeps one.
The holder's command and process id sit next to it for the waiting job to print. Standard library only.
"""

from __future__ import annotations

import contextlib
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

import msvcrt  # Windows only, like the project

REPORT_EVERY_S = 60.0  # a waiting job repeats its message, so a long wait does not look like a hang


def slot_dir() -> str:
    d = os.environ.get("ALAMO_TEST_SLOT_DIR") or os.path.join(
        os.environ.get("ProgramData") or os.environ.get("LOCALAPPDATA") or tempfile.gettempdir(), "alamo-dev",
        "test-slots")
    os.makedirs(d, exist_ok=True)
    return d


def slot_count() -> int:
    try:
        return max(1, int(os.environ.get("ALAMO_TEST_SLOTS", "1")))
    except ValueError:
        return 1


def _info_path(d: str, i: int) -> str:
    return os.path.join(d, f"slot-{i}.json")


def _lock_path(d: str, i: int) -> str:
    return os.path.join(d, f"slot-{i}.lock")


def _record(d: str, i: int) -> dict:
    try:
        with open(_info_path(d, i), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def is_free(d: str, i: int) -> bool:
    """True when no process holds slot i (probes the lock itself; a crashed holder's record may still be there)."""
    fd = os.open(_lock_path(d, i), os.O_RDWR | os.O_CREAT)
    try:
        msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
    except OSError:
        return False
    else:
        msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
        return True
    finally:
        os.close(fd)


def holder(d: str, i: int) -> dict:
    """Who holds slot i, or {} when it is free."""
    return {} if is_free(d, i) else _record(d, i)


def describe(h: dict) -> str:
    return (f"pid {h.get('pid', '?')}: {h.get('command', '?')} in {h.get('cwd', '?')} since {h.get('since', '?')}"
            if h else "free")


class Slot:
    """A held slot; release() or process exit frees it."""

    def __init__(self, fd: int, index: int, d: str):
        self.fd, self.index, self.dir = fd, index, d

    def release(self) -> None:
        if self.fd is None:
            return
        with contextlib.suppress(OSError):
            os.remove(_info_path(self.dir, self.index))  # before unlocking, so it never removes the next holder's
        with contextlib.suppress(OSError):
            msvcrt.locking(self.fd, msvcrt.LK_UNLCK, 1)
        os.close(self.fd)
        self.fd = None


def try_acquire(command: str, d: str, count: int) -> Slot | None:
    for i in range(count):
        fd = os.open(_lock_path(d, i), os.O_RDWR | os.O_CREAT)
        try:
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        except OSError:
            os.close(fd)
            continue
        slot = Slot(fd, i, d)
        try:
            info = {"pid": os.getpid(), "command": command, "cwd": os.getcwd(),
                    "since": datetime.datetime.now().isoformat(timespec="seconds")}
            tmp = _info_path(d, i) + f".{os.getpid()}"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(info, f)
            os.replace(tmp, _info_path(d, i))  # a reader sees the old record or the new one, never half
        except BaseException:
            slot.release()
            raise
        return slot
    return None


def acquire(command: str, d: str | None = None, count: int | None = None, poll: float = 1.0, say=print) -> Slot:
    """Waits for a slot. It says which jobs hold the slots, once their records are written, and repeats that every
    REPORT_EVERY_S seconds while it waits."""
    d = d or slot_dir()
    count = count if count is not None else slot_count()
    start = time.monotonic()
    said_at = None
    while True:
        slot = try_acquire(command, d, count)
        if slot:
            if said_at is not None:
                say(f"dev.cmd: got a test slot after {time.monotonic() - start:.0f} s")
            return slot
        now = time.monotonic()
        held = [h for h in (holder(d, i) for i in range(count)) if h]
        # The holder writes its record just after taking the lock: wait a moment for it rather than print "pid ?".
        ready = held and all("pid" in h for h in held)
        if (said_at is None and (ready or now - start > 2.0)) or (said_at is not None and now - said_at >= REPORT_EVERY_S):
            say(f"dev.cmd: waiting for a test slot ({count} on this machine, ALAMO_TEST_SLOTS) "
                f"for {now - start:.0f} s: " + "; ".join(describe(h) for h in held))
            said_at = now
        time.sleep(poll)


def main(argv: list[str]) -> int:
    if argv == ["selftest"]:
        return selftest()
    if argv == ["status"]:
        d, n = slot_dir(), slot_count()
        for i in range(n):
            print(f"slot {i}: {describe(holder(d, i))}")
        return 0
    if argv == ["concurrent"]:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Concurrent)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if len(argv) == 3 and argv[0] == "_suite":
        return _dev_suite(argv[1], argv[2])
    if len(argv) == 2 and argv[0] == "hold":
        slot = acquire(f"test_slots.py hold {argv[1]}", poll=0.2)
        print(f"holding slot {slot.index}", flush=True)
        time.sleep(float(argv[1]))
        slot.release()
        print("released", flush=True)
        return 0
    print(__doc__)
    return 2


FIXTURE_TEST = "alamo_slot_fixture"


def _dev_suite(bdir: str, logs: str) -> int:
    """One 'dev.cmd suite' run on bdir, through dev.py's own code, with its logs in logs (the concurrent test)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dev
    dev.LOG_DIR = logs
    args = ["synthetic", "--", "-R", f"^{FIXTURE_TEST}$"]
    r = dev.Runner("suite", args)
    r.env = dev.msvc_env()
    failure = None
    try:
        dev.test_suite(r, bdir, args)
    except dev.StepFailed as e:
        failure = e
    code = r.finish(failure)
    print(f"run_id={r.run_id} pid={os.getpid()}", flush=True)
    return code


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


class _Concurrent(unittest.TestCase):
    """Two real dev.cmd suite runs started at the same moment on one build tree, one slot (README.md "Test slots")."""

    def test_two_jobs_started_at_once(self):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import dev
        root = dev.ROOT
        os.makedirs(os.path.join(root, "build", "tmp"), exist_ok=True)
        work = tempfile.mkdtemp(prefix="alamo_slots_", dir=os.path.join(root, "build", "tmp"))
        self.addCleanup(shutil.rmtree, work, True)
        env = dict(dev.msvc_env(), ALAMO_TEST_SLOT_DIR=os.path.join(work, "slots"), ALAMO_TEST_SLOTS="1")
        bdir = os.path.join(work, "build")
        subprocess.run(dev.resolve(["cmake", "-G", "Ninja", "-S", os.path.join(root, "tests", "fixtures", "test_slots"),
                                    "-B", bdir], env), env=env, check=True, capture_output=True)
        python = getattr(sys, "_base_executable", None) or sys.executable  # see _Tests.hold
        jobs = [subprocess.Popen([python, "-I", os.path.abspath(__file__), "_suite", bdir, os.path.join(work, name)],
                                 env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                for name in ("logs-a", "logs-b")]  # started back to back, neither waits for the other
        outs = [j.communicate(timeout=240)[0] for j in jobs]
        both = "\n---\n".join(outs)
        self.assertEqual([j.returncode for j in jobs], [0, 0], both)
        runs = [dict(kv.split("=") for kv in o.strip().splitlines()[-1].split()) for o in outs]
        waited = [i for i, o in enumerate(outs) if "waiting for a test slot" in o]
        self.assertEqual(len(waited), 1, "exactly one job waits:\n" + both)
        other = runs[1 - waited[0]]
        self.assertIn(f"pid {other['pid']}: dev.cmd suite synthetic", outs[waited[0]])  # names the holder
        self.assertIn("got a test slot", outs[waited[0]])
        ids = [r["run_id"] for r in runs]
        self.assertNotEqual(ids[0], ids[1])
        for run_id, name in zip(ids, ("logs-a", "logs-b")):
            self.assertTrue(os.path.isdir(os.path.join(root, "build", "tmp", run_id)), "own temp folder")
            with open(os.path.join(dev.run_results_dir(bdir, run_id), "report-synthetic.md"), encoding="utf-8") as f:
                self.assertIn("Total: 1 passed, 0 failed", f.read(), "own report survives")
            self.assertEqual(len([n for n in os.listdir(os.path.join(work, name)) if n.endswith("-suite.log")]), 1)
            shutil.rmtree(os.path.join(root, "build", "tmp", run_id), ignore_errors=True)


def selftest() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(_Tests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
