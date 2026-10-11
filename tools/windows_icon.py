"""Embed a locally generated ICO in a staged executable using Windows resource APIs."""
import ctypes
from pathlib import Path
import struct
import sys


def icon_resources(data):
    if len(data) < 6:
        raise ValueError("Truncated ICO header")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved or kind != 1 or not 1 <= count <= 32 or len(data) < 6 + count * 16:
        raise ValueError("Invalid ICO directory")
    group = bytearray(struct.pack("<HHH", 0, 1, count))
    images = []
    for i in range(count):
        width, height, colors, zero, planes, bits, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
        if zero or not size or offset < 6 + count * 16 or offset + size > len(data):
            raise ValueError("Invalid ICO image bounds")
        resource_id = 400 + i
        group.extend(struct.pack("<BBBBHHIH", width, height, colors, zero, planes, bits, size, resource_id))
        images.append((resource_id, data[offset:offset+size]))
    return bytes(group), images


def embed_icon(executable, ico):
    if sys.platform != "win32":
        raise RuntimeError("EXE icon embedding must run on Windows")
    from ctypes import wintypes as wt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wt.WORD, wt.LPARAM)
    kernel.LoadLibraryExW.argtypes = [wt.LPCWSTR, wt.HANDLE, wt.DWORD]
    kernel.LoadLibraryExW.restype = wt.HMODULE
    kernel.FreeLibrary.argtypes = [wt.HMODULE]
    kernel.FreeLibrary.restype = wt.BOOL
    kernel.EnumResourceLanguagesW.argtypes = [wt.HMODULE, ctypes.c_void_p, ctypes.c_void_p, callback_type, wt.LPARAM]
    kernel.EnumResourceLanguagesW.restype = wt.BOOL
    kernel.BeginUpdateResourceW.argtypes = [wt.LPCWSTR, wt.BOOL]
    kernel.BeginUpdateResourceW.restype = wt.HANDLE
    kernel.UpdateResourceW.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wt.WORD, ctypes.c_void_p, wt.DWORD]
    kernel.UpdateResourceW.restype = wt.BOOL
    kernel.EndUpdateResourceW.argtypes = [wt.HANDLE, wt.BOOL]
    kernel.EndUpdateResourceW.restype = wt.BOOL
    executable = str(Path(executable).resolve())
    group, images = icon_resources(Path(ico).read_bytes())
    languages = set()
    module = kernel.LoadLibraryExW(executable, None, 2)  # LOAD_LIBRARY_AS_DATAFILE: do not execute code
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    @callback_type
    def collect(_module, _kind, _name, language, _parameter):
        languages.add(language)
        return True
    try:
        if not kernel.EnumResourceLanguagesW(module, ctypes.c_void_p(14), ctypes.c_void_p(1), collect, 0):
            error = ctypes.get_last_error()
            if error not in (1813, 1814, 1815):  # resource type/name/language absent
                raise ctypes.WinError(error)
    finally:
        kernel.FreeLibrary(module)
    languages.add(0)
    handle = kernel.BeginUpdateResourceW(executable, False)  # Preserve manifest/version resources.
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    commit = False
    try:
        for language in languages:
            for kind, resource_id, payload in [(3, ident, image) for ident, image in images] + [(14, 1, group)]:
                buffer = ctypes.create_string_buffer(payload)
                if not kernel.UpdateResourceW(handle, ctypes.c_void_p(kind), ctypes.c_void_p(resource_id), language, buffer, len(payload)):
                    raise ctypes.WinError(ctypes.get_last_error())
        commit = True
    finally:
        if not kernel.EndUpdateResourceW(handle, not commit) and commit:
            raise ctypes.WinError(ctypes.get_last_error())
    print(f"Embedded disc-derived icon: {executable}")
