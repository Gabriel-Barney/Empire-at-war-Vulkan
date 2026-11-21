"""Materialize one benchmark recipe using this build's engine."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

RECIPES = Path(__file__).resolve().parents[2] / "tests" / "benchmark"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["materialize"])
    parser.add_argument("name", choices=sorted(p.stem for p in RECIPES.glob("*.abench")))
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    if not os.environ.get("ALAMO_FOC_DATA"):
        print("Benchmark fixture skipped because ALAMO_FOC_DATA is unset.")
        return 77

    recipe = RECIPES / (args.name + ".abench")
    rendered = "host game" in recipe.read_text(encoding="utf-8").splitlines()
    executable = args.build.resolve() / "bin" / ("alamo_game.exe" if rendered else "alamo_headless.exe")
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.out) as tmp:
        manifest = Path(tmp).resolve() / recipe.name
        shutil.copyfile(recipe, manifest)
        flags = ["--hidden"] if rendered else []
        subprocess.run([executable, *flags, "--bench-regen", manifest], cwd=tmp, check=True)
        products = [manifest.with_suffix(ext) for ext in (".alsv", ".acmd", ".crc", ".abench")]
        for path in products:
            if not path.is_file():
                raise FileNotFoundError(path)
        for path in products:
            os.replace(path, args.out / path.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
