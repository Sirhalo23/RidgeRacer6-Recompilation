#!/usr/bin/env python3
"""Copies the publishable part of the project into a clean folder, laid out as
the public repository.

usage: export_source.py <destination folder>

Run from anywhere; it finds the project next to itself. Only files on the list
below are copied: the project's own source, scripts and notes. Never copied:
anything made from the game (rr6-recomp/generated/default, the unpacked
executable in rr6-recomp/analysis), game files, the SDK, build output, logs
and executables. Files already in the destination are overwritten; nothing is
deleted there.

Layout of the result (the repository's top level is the project folder):

    README.md, BUILDING.md, ...      from rr6-recomp/publish
    src/, launcher/, config/, ...    from rr6-recomp
    docs/                            the working notes (rr6-recomp/README.md, SDK-NOTES.md)
    tools/                           from tools (which sits next to rr6-recomp here)
"""
import fnmatch
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (source folder, destination folder, patterns, recurse into subfolders)
INCLUDE = [
    ('rr6-recomp', '', ['CMakeLists.txt', 'CMakePresets.json', 'rr6_recomp_manifest.toml',
                        'gamecontrollerdb.txt', '*.bat', '*.sh'], False),
    ('rr6-recomp/generated', 'generated', ['rexglue.cmake'], False),
    ('rr6-recomp/src', 'src', ['*.cpp', '*.h'], True),
    ('rr6-recomp/config', 'config', ['*.toml'], True),
    ('rr6-recomp/launcher', 'launcher', ['*.cpp', '*.c', '*.h', '*.rc', '*.manifest', '*.ico', '*.sh',
                                         '*.txt'], True),
    ('rr6-recomp/package', 'package', ['*'], True),
    ('rr6-recomp/linux', 'linux', ['*.sh', '*.txt'], True),
    ('rr6-recomp/analysis', 'analysis', ['*.txt'], False),
    ('rr6-recomp/publish', '', ['*.md', 'LICENSE'], False),
    ('rr6-recomp/publish/images', 'docs/images', ['*.png', '*.py'], False),
    ('tools', 'tools', ['*.py'], False),
    ('tools/linux-rig', 'tools/linux-rig', ['*'], True),
]
# Single files that get another name or place.
RENAME = [
    ('rr6-recomp/README.md', 'docs/DEVELOPMENT.md'),
    ('rr6-recomp/SDK-NOTES.md', 'docs/SDK-NOTES.md'),
    ('rr6-recomp/publish/gitignore.txt', '.gitignore'),
    ('rr6-recomp/publish/gitattributes.txt', '.gitattributes'),
    ('rr6-recomp/publish/github-workflows/launcher.yml', '.github/workflows/launcher.yml'),
]
# Never, whatever the lists above say.
FORBIDDEN = ['*.exe', '*.dll', '*.iso', '*.xex', '*.bin', '*.sfd', '*.dat', '*.map', '*.zip', '*.log',
             '*.xpso', '*.pdb', '*.obj', '*.o', '*.res', '*.pyc', 'iso_listing.txt']
MAX_BYTES = 2 * 1024 * 1024

DEVELOPMENT_NOTE = (
    '> Working notes, kept as they were written. Paths are those of the working\n'
    '> folder, where the project lives in `rr6-recomp/` with `tools/`, `game/`,\n'
    '> `sdk/` and `dist/` next to it; in this repository the project folder is the\n'
    '> top level and `tools/` is inside it.\n\n')


def allowed(name):
    low = name.lower()
    return not any(fnmatch.fnmatch(low, f) for f in FORBIDDEN)


def copy(source, target):
    if os.path.getsize(source) > MAX_BYTES:
        sys.exit('refusing to copy a file this large: ' + source)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    try:
        shutil.copyfile(source, target)
    except OSError as error:
        print('  could not write', target, '-', error)
        return 0
    return 1


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    dest = os.path.abspath(sys.argv[1])
    copied = 0
    for folder, out, patterns, recurse in INCLUDE:
        base = os.path.join(ROOT, *folder.split('/'))
        if not os.path.isdir(base):
            continue
        for current, dirs, files in os.walk(base):
            dirs[:] = sorted(d for d in dirs if d not in ('__pycache__', '.git')) if recurse else []
            for name in sorted(files):
                if not allowed(name) or not any(fnmatch.fnmatch(name, p) for p in patterns):
                    continue
                source = os.path.join(current, name)
                relative = os.path.relpath(source, base)
                copied += copy(source, os.path.join(dest, *out.split('/'), relative) if out
                               else os.path.join(dest, relative))
    for source, target in RENAME:
        source = os.path.join(ROOT, *source.split('/'))
        if os.path.isfile(source) and allowed(os.path.basename(source)):
            copied += copy(source, os.path.join(dest, *target.split('/')))
    notes = os.path.join(dest, 'docs', 'DEVELOPMENT.md')
    if os.path.isfile(notes):
        text = open(notes, encoding='utf-8').read()
        if not text.startswith('> Working notes'):
            first, _, rest = text.partition('\n')
            open(notes, 'w', encoding='utf-8', newline='').write(first + '\n\n' + DEVELOPMENT_NOTE + rest.lstrip('\n'))
    print(copied, 'files copied to', dest)


if __name__ == '__main__':
    main()
