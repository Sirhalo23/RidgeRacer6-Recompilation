#!/usr/bin/env python3
"""Assemble only approved release files, audit linkage, sign and zip the app."""
from pathlib import Path
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

from make_icon import make_icon

ROOT = Path(__file__).resolve().parents[1]


def version_key(value):
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


def reject_game_data(app):
    for path in app.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() in {".iso", ".xex", ".sfd", ".dat", ".bin", ".xpso"}:
            raise RuntimeError(f"Refusing to package game data: {path}")
        with path.open("rb") as stream:
            if stream.read(4) in {b"LIVE", b"PIRS", b"CON "}:
                raise RuntimeError(f"Refusing to package a content package: {path}")


def package(build, sdk, arch, output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rr6-release-") as temporary:
        app = Path(temporary) / "Ridge Racer 6.app"
        contents = app / "Contents"
        binary = contents / "MacOS"
        resources = contents / "Resources"
        binary.mkdir(parents=True)
        resources.mkdir()
        source = build / "Contents/MacOS"
        files = ["rr6_recomp", "librexruntime.dylib", "librexgpu-xenos.dylib",
                 "vulkan/lib/libvulkan.1.dylib", "vulkan/lib/libMoltenVK.dylib",
                 "vulkan/share/vulkan/icd.d/MoltenVK_icd.json"]
        code = []
        for name in files:
            destination = (resources if name.startswith("vulkan/") else binary) / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / name, destination)
            if name.endswith(".dylib") or name == "rr6_recomp":
                code.append(destination)
        shutil.copy2(ROOT / "config/rr6_recomp.macos.toml", resources / "rr6_recomp.toml")
        shutil.copy2(ROOT / "gamecontrollerdb.txt", resources / "gamecontrollerdb.txt")
        (resources / "sounds").mkdir()
        shutil.copy2(ROOT / "assets/achievement.wav", resources / "sounds/achievement.wav")

        # Local personalization only: distribution packages need no disc artwork.
        # Generate inside staging so no extracted PNG or ICNS enters the repo.
        icon_xex = os.environ.get("RR6_ICON_XEX")
        if icon_xex:
            platform = "mac-arm64" if arch == "arm64" else "mac-amd64"
            rexglue = Path(os.environ.get("RR6_ICON_REXGLUE", sdk / "out" / platform / "rexglue"))
            make_icon(Path(icon_xex), rexglue, resources / "RR6.icns", arch)

        # Respect the deployment target of the existing build, rather than
        # claiming support for an older macOS than its dependencies require.
        versions = []
        for path in code:
            if arch not in run("lipo", "-archs", path).split():
                raise RuntimeError(f"Wrong architecture: {path}")
            info = run("vtool", "-show-build", path)
            versions += re.findall(r"\bminos\s+([\d.]+)", info)
        if not versions:
            raise RuntimeError("Could not determine the build's minimum macOS version")
        minimum = max(versions, key=version_key)
        cache = build.parent / "CMakeCache.txt"
        if cache.is_file():
            target = re.search(r"^CMAKE_OSX_DEPLOYMENT_TARGET:[^=]+=([\d.]+)$",
                               cache.read_text(), re.MULTILINE)
            if target and version_key(minimum) > version_key(target[1]):
                raise RuntimeError(
                    f"A bundled binary requires macOS {minimum}, above the configured "
                    f"deployment target {target[1]}; rebuild all dependencies")
        launcher = binary / "rr6-launcher"
        run("xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fobjc-arc", "-arch", arch, f"-mmacosx-version-min={minimum}", "-framework", "Cocoa",
            "-framework", "UniformTypeIdentifiers", "-I", sdk / "thirdparty/sdl3/include",
            ROOT / "macos/launcher.mm", ROOT / "macos/settings.cpp",
            ROOT / "launcher/disc_image.cpp", "-L", binary, "-lrexruntime",
            "-Wl,-rpath,@executable_path", "-o", launcher)
        code.append(launcher)

        # No dependency on a developer machine's SDK, Homebrew or build tree.
        available = {path.name for path in code}
        for path in code:
            dependencies = run("otool", "-L", path).splitlines()[1:]
            if path.suffix == ".dylib":
                dependencies = dependencies[1:]  # its own install ID
            for entry in dependencies:
                dependency = entry.strip().split(" (", 1)[0]
                if dependency.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                if dependency.startswith(("@rpath/", "@loader_path/", "@executable_path/")):
                    if Path(dependency).name in available:
                        continue
                raise RuntimeError(f"Unbundled dependency in {path.name}: {dependency}")
            run("strip", "-S", "-x", path)

        with (contents / "Info.plist").open("wb") as stream:
            info = {
                "CFBundleExecutable": "rr6-launcher",
                "CFBundleIdentifier": "org.rr6recomp.game",
                "CFBundleName": "Ridge Racer 6",
                "CFBundleDisplayName": "Ridge Racer 6",
                "CFBundlePackageType": "APPL",
                "CFBundleVersion": "0.1",
                "CFBundleShortVersionString": "0.1",
                "LSMinimumSystemVersion": minimum,
                "NSHighResolutionCapable": True,
            }
            if icon_xex:
                info["CFBundleIconFile"] = "RR6.icns"
            plistlib.dump(info, stream)
        licenses = resources / "licenses"
        shutil.copytree(ROOT / "package/licenses", licenses)
        for base in [sdk, *sorted((sdk / "thirdparty").iterdir())]:
            if not base.is_dir():
                continue
            for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
                for license_file in base.glob(pattern):
                    if license_file.is_file():
                        target = licenses / f"{base.name}-{license_file.name}"
                        shutil.copy2(license_file, target)
        (resources / "README.txt").write_text(
            f"Ridge Racer 6 — experimental macOS {arch} build\n"
            f"Minimum macOS for this particular build: {minimum}\n\n"
            "Put this app beside your Ridge Racer 6 USA ISO and double-click it.\n"
            "The native launcher opens before the game. Choose options, then Play.\n"
            "If the image cannot be located automatically, Play asks you to choose it.\n"
            "First Play verifies the disc executable and copies about 6 GB of files.\n"
            "Later launches reuse the extracted copy; the ISO is no longer needed.\n"
            "Game files, settings, saves and logs are kept in\n"
            "~/Library/Application Support/Ridge Racer 6/.\n\n"
            "Put your own DLC content packages in that folder's DLC subfolder.\n"
            "New or changed packages are checked and installed at game startup.\n"
            "Display offers language, full screen/windowed mode, render resolution,\n"
            "Retina output, optional display VSync, smoothing and texture filtering.\n"
            "Save Settings or Play applies edits; unrelated custom settings are retained.\n"
            "The launcher returns when the game closes and reloads F4 changes.\n\n"
            "Pair a controller over Bluetooth or connect USB. Controls shows connected\n"
            "devices, sticks, triggers, held buttons and an optional vibration test.\n"
            "The controller selector is for testing, not player assignment.\n"
            "Select a keyboard row and Set Key, or edit comma-separated key names.\n"
            "Xbox/PlayStation labels are a reference; in-game prompts stay Xbox.\n"
            "Game Files has ISO import and shortcuts to data, DLC and logs.\n"
            "F4 opens advanced in-game settings; F3 shows statistics.\n"
            "Defaults use 720p guest rendering and a logical-resolution presentation buffer.\n"
            "For sharper output, choose High density in the Retina output option.\n"
            "Higher render resolutions are experimental and use more GPU memory.\n"
            "VSync can add latency and does not eliminate shader compilation stalls.\n"
            "Async shader compilation remains enabled; retain caches between runs.\n"
            "Race rendering, full controller behavior and older OS versions still need testing.\n"
            "This test package is ad-hoc signed, not notarized.\n", encoding="utf-8")
        (resources / "BUILD.txt").write_text(
            f"RR6 commit: {run('git', '-C', ROOT, 'rev-parse', 'HEAD').strip()}\n"
            f"SDK commit: {run('git', '-C', sdk, 'rev-parse', 'HEAD').strip()}\n"
            f"MoltenVK commit: {run('git', '-C', sdk / 'thirdparty/moltenvk', 'rev-parse', 'HEAD').strip()}\n"
            "Local working-tree changes may be included.\n", encoding="utf-8")
        for path in app.rglob("*"):
            if path.is_file():
                path.chmod(0o755 if path in code else 0o644)
        reject_game_data(app)
        for path in code:
            run("codesign", "--force", "--sign", "-", path)
        run("codesign", "--force", "--sign", "-", app)
        run("codesign", "--verify", "--deep", "--strict", app)
        archive = output / f"RidgeRacer6-macOS-{arch}-test.zip"
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive)
        print(f"Created {archive}\nMinimum macOS: {minimum}\nAd-hoc signed test build; not notarized.")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("usage: package.py app-build sdk-source arm64|x86_64 output-folder")
    package(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3],
            Path(sys.argv[4]).resolve())
