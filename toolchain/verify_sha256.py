"""Verify a downloaded dependency against its SHA-256 pin."""
import hashlib
import sys
from pathlib import Path


def main():
    path = Path(sys.argv[1])
    expected = sys.argv[2].lower()
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        print(error, file=sys.stderr)
        return 1
    if digest.hexdigest() != expected:
        print(f"SHA-256 mismatch for {path}: {digest.hexdigest()}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
