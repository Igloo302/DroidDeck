"""Package the experimental Android/Linux pair for DroidDeck's existing importer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

SOURCE_SHA = "d094072e89d7810537c624b163b3ad40ad0d74f7514a1dd4beaea4b38b25b07a"
COMMIT = "44fd0fde8187b5bb1e919625f1ba24cb36c18cbc"
LIB = "libvulkan_freedreno.so"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--android", type=Path, required=True)
    parser.add_argument("--linux", type=Path, required=True)
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True, help="Extracted DD-Turnip source root")
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", type=int, choices=(1, 2, 3), default=1)
    args = parser.parse_args()
    if hashlib.sha256(args.source_archive.read_bytes()).hexdigest() != SOURCE_SHA:
        raise SystemExit("Unexpected source archive")
    files = {}
    tag = f"DD-Turnip-AURA831-prototype{args.revision}"
    from patch_wsi_signaled_fd import ORIGINAL_SHA256, PATCHED_SHA256
    wsi = args.source / "mesa/src/vulkan/wsi/wsi_common_drm.c"
    wsi_sha = hashlib.sha256(wsi.read_bytes()).hexdigest()
    if wsi_sha != (PATCHED_SHA256 if args.revision >= 2 else ORIGINAL_SHA256):
        raise SystemExit("WSI source does not match the requested prototype revision")
    manifest = {
        "schemaVersion": 1, "package": "DD Turnip AURA 831 (experimental)",
        "version": f"prototype{args.revision}", "tag": tag,
        "mesa": {"version": "27.0.0-devel", "commit": COMMIT,
                 "source": "https://github.com/Droid-Deck/Drivers-CI/releases/tag/DD-Turnip-v0.3.1",
                 "sourceArchiveSha256": SOURCE_SHA},
        "experimental": {"chipId": "0x44050100", "profile": "A830; unverified hardware hypothesis"},
        "drivers": {},
    }
    if args.revision >= 2:
        manifest["localPatches"] = [{
            "path": "src/vulkan/wsi/wsi_common_drm.c",
            "purpose": "Do not import an already-signaled sync FD (-1) into a dma-buf",
            "beforeSha256": ORIGINAL_SHA256, "afterSha256": PATCHED_SHA256,
        }]
    from patch_aura_compositor_cache import ORIGINAL_SHA256 as CACHE_ORIGINAL, PATCHED_SHA256 as CACHE_PATCHED
    cache_path = args.source / "mesa/src/freedreno/vulkan/tu_cmd_buffer.cc"
    if hashlib.sha256(cache_path.read_bytes()).hexdigest() != (CACHE_PATCHED if args.revision >= 3 else CACHE_ORIGINAL):
        raise SystemExit("Command-buffer source does not match the requested prototype revision")
    if args.revision >= 3:
        manifest["localPatches"].append({
            "path": "src/freedreno/vulkan/tu_cmd_buffer.cc",
            "purpose": "Cache maintenance only for vrcompositor on exact chip 0x44050100",
            "beforeSha256": CACHE_ORIGINAL, "afterSha256": CACHE_PATCHED,
        })
    for platform, path, libc in [("android", args.android, "bionic"), ("linux", args.linux, "glibc")]:
        data = path.read_bytes()
        if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[16:18], "little") != 3 or int.from_bytes(data[18:20], "little") != 183:
            raise SystemExit(f"{platform}: expected an AArch64 ELF64 shared library")
        dynamic = subprocess.check_output([args.readelf, "-d", str(path)], text=True)
        needed = set(re.findall(r'Shared library: \[(.*?)\]', dynamic))
        expected = "libc.so" if platform == "android" else "libc.so.6"
        if expected not in needed or ({"libc.so", "libc.so.6"} - {expected}) & needed:
            raise SystemExit(f"{platform}: wrong libc dependencies {needed}")
        for marker in [b"Adreno (TM) 831 (experimental)", b"Failed to set initial PWR_MAX constraint"]:
            if marker not in data:
                raise SystemExit(f"{platform}: missing prototype/DD marker {marker!r}")
        digest = hashlib.sha256(data).hexdigest()
        meta = dict(schemaVersion=1, name=f"DD Turnip AURA 831 prototype {args.revision} (experimental)",
                    description="Exact chip 0x44050100 only; A830 profile hypothesis. Not full 831 support.",
                    author="Mesa / DroidDeck; local experimental chip entry", packageVersion=str(args.revision),
                    vendor="Mesa", driverVersion=tag, libraryName=LIB, librarySha256=digest)
        if platform == "android":
            meta["minApi"] = 34
        else:
            meta.update(platform="linux", architecture="aarch64", libc="glibc", minGlibc="2.38",
                        wsi=["wayland", "x11"], mesaVersion="27.0.0-devel", mesaCommit=COMMIT)
        files[f"{platform}/{LIB}"] = data
        files[f"{platform}/meta.json"] = (json.dumps(meta, indent=2) + "\n").encode()
        manifest["drivers"][platform] = dict(path=platform, library=f"{platform}/{LIB}",
            meta=f"{platform}/meta.json", architecture="aarch64", libc=libc, sha256=digest)
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    files["NOTICE"] = (f"{tag}\nExperimental local pair, not an official DD release.\n"
        f"Mesa {COMMIT}; original source archive SHA256 {SOURCE_SHA}.\n"
        "Original source and DD patches: https://github.com/Droid-Deck/Drivers-CI/releases/tag/DD-Turnip-v0.3.1\n"
        "Local exact-chip modification and build instructions: tools/gpu/ in this DroidDeck branch.\n"
        "Mesa licenses: LICENSES/mesa; DD patch set and derived driver: GPL-3.0.\n"
        "Import this ZIP in DroidDeck GPU drivers and select the pair; restart the app.\n"
        "Existing drivers remain available for rollback. No system driver is replaced.\n").encode()
    files["LICENSES/GPL-3.0.txt"] = (args.source / "drivers/LICENSE").read_bytes()
    files["LICENSES/mesa/license.rst"] = (args.source / "mesa/docs/license.rst").read_bytes()
    for path in sorted((args.source / "mesa/licenses").rglob("*")):
        if path.is_file():
            files[f"LICENSES/mesa/{path.relative_to(args.source / 'mesa/licenses').as_posix()}"] = path.read_bytes()
    for name in ["prepare_aura_turnip.py", "patch_wsi_signaled_fd.py", "patch_aura_compositor_cache.py", "Dockerfile.turnip", "README.md", "android-cross.ini"]:
        files[f"recipe/{name}"] = Path(__file__).with_name(name).read_bytes()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            archive.writestr(zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)), files[name], zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(args.output) as archive:
        if sorted(archive.namelist()) != sorted(files) or archive.testzip() is not None:
            raise SystemExit("ZIP readback failed")
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix + ".sha256").write_text(f"{digest}  {args.output.name}\n")
    print(json.dumps(dict(zip=str(args.output), sha256=digest, drivers=manifest["drivers"]), indent=2))


if __name__ == "__main__":
    main()
