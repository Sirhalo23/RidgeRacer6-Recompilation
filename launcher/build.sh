#!/bin/sh
# Cross-compiles the launcher with MinGW-w64 (also works in an MSYS2 MinGW-w64
# shell on Windows, without the "-posix" suffix).
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres launcher.rc -O coff -o launcher.res
# The movie reader is C (it contains the pl_mpeg decoder).
x86_64-w64-mingw32-gcc -std=c11 -O2 -c movie_still.c -o movie_still.o
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static \
    launcher.cpp disc_image.cpp movie_still.o launcher.res -o "RR6 Launcher.exe" \
    -lgdiplus -lcomdlg32 -lcomctl32 -lshell32 -luxtheme -lwinmm -lole32 -luuid -lgdi32
rm -f movie_still.o
ls -l "RR6 Launcher.exe"
