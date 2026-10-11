#!/usr/bin/env python3
"""Install a user desktop-menu shortcut for a locally personalized Linux package."""
import argparse
import os
from pathlib import Path
import sys


def value(text):
    return str(text).replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def argument(text):
    # Exec quoting is undone after desktop-string escaping. Escape both layers.
    quoted = "".join("\\" + c if c in '\\"`$' else c for c in str(text))
    return '"' + value(quoted).replace("%", "%%") + '"'


def desktop_entry(package):
    package = Path(package).resolve()
    script, icon = package / "ridge-racer-6.sh", package / "icons/rr6.png"
    if not script.is_file() or not icon.is_file():
        raise ValueError("Run this from a Linux package containing the locally generated icons")
    return ("[Desktop Entry]\nType=Application\nName=Ridge Racer 6\n"
            f"Exec=/bin/bash {argument(script)}\nPath={value(package)}\n"
            f"Icon={value(icon)}\nTerminal=false\nCategories=Game;\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, help="Write a desktop entry to this path instead of installing it")
    args = parser.parse_args()
    if not args.output and sys.platform != "linux":
        parser.error("Shortcut installation is for Linux; --output can generate a preview elsewhere")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    if not data_home.is_absolute():
        data_home = Path.home() / ".local/share"
    destination = args.output or data_home / "applications/rr6-recomp.desktop"
    contents = desktop_entry(args.package)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(contents, encoding="utf-8")
    print(f"Installed desktop-menu shortcut: {destination}")


if __name__ == "__main__":
    main()
