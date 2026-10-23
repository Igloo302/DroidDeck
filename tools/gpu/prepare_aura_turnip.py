"""Add an exact-chip experimental A830-profile entry to the pinned DD Mesa source."""
import argparse
import hashlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mesa", type=Path)
    args = parser.parse_args()
    mesa = args.mesa.resolve()
    if (mesa / "VERSION").read_text().strip() != "27.0.0-devel":
        raise SystemExit("Expected DD-Turnip v0.3.1 Mesa 27.0.0-devel source")
    target = mesa / "src/freedreno/common/freedreno_devices.py"
    text = target.read_text()
    anchor = '        GPUId(chip_id=0x44050001, name="Adreno (TM) 830"), # KGSL\n'
    entry = '        GPUId(chip_id=0x44050100, name="Adreno (TM) 831 (experimental)"),\n'
    if entry in text:
        print("831 experimental entry already present")
        return
    expected = {
        "f3193b5d27796a84daf9cc040bfdec2049397101ad4856c84754e9f8952e6b91",
        "77838b33747847788f52f9db467e5b9f0071ffff1d10ee9b4fbe184e52d697bc",
    }
    if hashlib.sha256(target.read_bytes()).hexdigest() not in expected:
        raise SystemExit("Expected the pinned DD-Turnip v0.3.1 Linux- or Android-patched GPU table")
    if text.count(anchor) != 1 or "44050100" in text:
        raise SystemExit("Unexpected GPU table; refusing to guess the insertion point")
    print("before sha256", hashlib.sha256(target.read_bytes()).hexdigest())
    target.write_text(text.replace(anchor, anchor + entry))
    print("after sha256", hashlib.sha256(target.read_bytes()).hexdigest())
    print("EXPERIMENTAL: A830 device profile is an unverified hardware hypothesis; not production support")


if __name__ == "__main__":
    main()
