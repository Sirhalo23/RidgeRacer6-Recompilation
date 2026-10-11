#!/usr/bin/env python3
"""Make a local app icon from the player's ISO-extracted executable, never repo artwork."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from disc_icon import extract_title_icon


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def make_icon(xex, rexglue, output, arch):
    xex, rexglue, output = xex.resolve(), rexglue.resolve(), output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rr6-local-icon-") as directory:
        temporary = Path(directory)
        extracted = temporary / "extracted"
        artwork = extract_title_icon(xex, rexglue, extracted)
        renderer = temporary / "render_icon"
        run("xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fobjc-arc", "-arch", arch, "-framework", "AppKit",
            Path(__file__).with_name("render_icon.mm"), "-o", renderer)
        master = temporary / "master.png"
        run(renderer, artwork, master)
        iconset = temporary / "RR6.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = "@2x" if scale == 2 else ""
                pixels = size * scale
                run("sips", "-z", pixels, pixels, master, "--out",
                    iconset / f"icon_{size}x{size}{suffix}.png")
        run("iconutil", "-c", "icns", iconset, "-o", output)
    print(f"Created local ISO-derived app icon: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xex", type=Path)
    parser.add_argument("rexglue", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--arch", choices=("arm64", "x86_64"), required=True)
    args = parser.parse_args()
    make_icon(args.xex, args.rexglue, args.output, args.arch)
