#!/usr/bin/env python3
"""The two-PR race demonstration's invariant (tests/fixtures/race_demo/README.md): at most one *.txt file in
tests/fixtures/race_demo. CTest alamo_race_demo. Temporary: removed with the demonstration. Standard library only."""

import os
import sys

FOLDER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests", "fixtures", "race_demo")

files = sorted(f for f in os.listdir(FOLDER) if f.endswith(".txt"))
print(f"race_demo: {len(files)} file(s): {', '.join(files) or 'none'}")
if len(files) > 1:
    print("error: race_demo: more than one file; two pull requests that each passed alone landed together")
    sys.exit(1)
