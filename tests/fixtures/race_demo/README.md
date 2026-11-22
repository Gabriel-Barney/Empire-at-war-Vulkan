# Two-PR race demonstration

Phase 3's race proof (`docs/GROUND_UP_WORKFLOW_DESIGN.md` "Rollout and measures of success", step 3; issue #17).
CTest `alamo_race_demo` (`tools/race_demo.py`) fails when this folder holds more than one `*.txt` file. Two pull
requests each add one file: each passes `landing` alone, and the run on the push to `main` after both land fails. A
follow-up pull request removes this folder, the script and the entry.
