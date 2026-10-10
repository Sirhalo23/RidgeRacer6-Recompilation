#!/usr/bin/env python3
"""Finds names in the game's own source files that a Windows header defines as
a macro.

The Windows build cannot be tried on the Linux machines this project is mostly
written on, and windows.h defines ordinary-looking words: `small`, `near`,
`far`, `interface`, `hyper`. A local variable called `small` compiled
everywhere but on Windows (rpcndr.h: #define small char) and broke the
achievements build.

usage: check_windows_names.py [--headers DIR] file...

DIR is a folder of Windows headers; the default is where Debian and Ubuntu put
MinGW-w64's (package mingw-w64-common). Only lower-case and mixed-case names
are reported: upper-case ones are understood to be macros. Exit status 1 if
anything is found.
"""
import glob
import os
import re
import sys

# Defined as macros by MinGW's own C library headers, not by the Windows SDK
# (pthread.h and friends), or switched off for this project (NOMINMAX).
NOT_ON_WINDOWS_SDK = {'min', 'max'}
# A few rarely used headers (sehmap.h, nmsupp.h) turn C++ keywords such as
# try into Microsoft's structured exception handling. Nothing includes them
# unless asked to, and a keyword cannot be renamed anyway.
CXX_KEYWORDS = {
    'alignas', 'alignof', 'and', 'asm', 'auto', 'bool', 'break', 'case', 'catch', 'char', 'class',
    'const', 'consteval', 'constexpr', 'constinit', 'const_cast', 'continue', 'decltype', 'default',
    'delete', 'do', 'double', 'dynamic_cast', 'else', 'enum', 'explicit', 'export', 'extern', 'false',
    'float', 'for', 'friend', 'goto', 'if', 'inline', 'int', 'long', 'mutable', 'namespace', 'new',
    'noexcept', 'not', 'nullptr', 'operator', 'or', 'private', 'protected', 'public', 'register',
    'reinterpret_cast', 'return', 'short', 'signed', 'sizeof', 'static', 'static_assert',
    'static_cast', 'struct', 'switch', 'template', 'this', 'thread_local', 'throw', 'true', 'try',
    'typedef', 'typeid', 'typename', 'union', 'unsigned', 'using', 'virtual', 'void', 'volatile',
    'while', 'xor'}
# Headers nothing includes unless asked to, which redefine ordinary Windows API
# names (mapinls.h maps WideCharToMultiByte and friends for old MAPI code).
RARE_HEADERS = ('mapinls.h',)
SKIP_HEADERS = RARE_HEADERS + ('pthread', 'sched.h', 'semaphore.h', 'unistd.h', 'io.h', 'stdio.h', 'stdlib.h', 'string.h',
                'time.h', 'wchar.h', 'math.h', 'process.h', 'direct.h', 'sys', 'float.h', 'errno.h', 'signal.h',
                'ctype.h', 'malloc.h', 'tgmath.h', 'complex.h', 'inttypes.h', 'conio.h', 'assert.h', 'locale.h',
                'memory.h', 'search.h', 'stddef.h', 'stdint.h', 'tchar.h', 'fcntl.h', 'dirent.h', 'utime.h')


def windows_macros(folder):
    macros = {}
    for path in glob.glob(os.path.join(folder, '*.h')):
        name = os.path.basename(path)
        if name.startswith(SKIP_HEADERS):
            continue
        with open(path, errors='ignore') as f:
            for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)', f.read(), re.M):
                macros.setdefault(m.group(1), name)
    return macros


def names_in(path):
    """(line number, name) for every identifier outside comments and strings."""
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    for number, line in enumerate(text.split('\n'), 1):
        line = re.sub(r'//.*', '', line)
        line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
        line = re.sub(r"'(\\.|[^'\\])'", "''", line)
        if line.lstrip().startswith('#include'):
            continue
        for m in re.finditer(r'\b[A-Za-z_][A-Za-z0-9_]*\b', line):
            yield number, m.group(0)


def main():
    args = sys.argv[1:]
    folder = '/usr/share/mingw-w64/include'
    if args[:1] == ['--headers']:
        folder, args = args[1], args[2:]
    if not args:
        print(__doc__)
        return 2
    macros = windows_macros(folder)
    if len(macros) < 1000:
        print('No Windows headers in %s (install mingw-w64-common, or give --headers).' % folder)
        return 2
    found = 0
    for path in args:
        seen = set()
        for number, name in names_in(path):
            if name in macros and name not in NOT_ON_WINDOWS_SDK and name not in CXX_KEYWORDS \
                    and not name.isupper() \
                    and not name.startswith('_') and (name, number) not in seen:
                seen.add((name, number))
                print('%s:%d: "%s" is a macro in the Windows headers (%s)' % (path, number, name, macros[name]))
                found += 1
    if found:
        print('%d name(s) to rename.' % found)
        return 1
    print('%d files: no names that the Windows headers define.' % len(args))
    return 0


if __name__ == '__main__':
    sys.exit(main())
