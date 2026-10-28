"""Run an optional original-MMD render test without a GUI or binary patches.

The x86 reference EXE is fixed at 0x400000. Reserve that region before the
test process initializes its DLLs/CRT; its early fixture initializer releases
the placeholder and asks the Windows loader to map the verified reference.
Pass the test executable followed by its usual --original/--model/--output.
"""

import ctypes
from ctypes import wintypes
import os
import subprocess
import sys


def run(arguments):
    if os.name != "nt":
        raise RuntimeError("The original rendering oracle requires Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    allocate = kernel.VirtualAllocEx
    allocate.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t,
                         wintypes.DWORD, wintypes.DWORD]
    allocate.restype = ctypes.c_void_p
    resume = ctypes.WinDLL("ntdll").NtResumeProcess
    resume.argtypes = [wintypes.HANDLE]
    resume.restype = ctypes.c_long
    environment = dict(os.environ, MDS_ORIGINAL_IMAGE_RESERVED="1")
    process = subprocess.Popen(arguments, env=environment, creationflags=0x00000004)  # CREATE_SUSPENDED
    try:
        if allocate(int(process._handle), 0x400000, 0x1A3000, 0x2000, 1) != 0x400000:
            raise ctypes.WinError(ctypes.get_last_error())
        if resume(int(process._handle)) < 0:
            raise RuntimeError("Cannot resume the original-render test process")
        return process.wait()
    except BaseException:
        process.kill()
        process.wait()
        raise


if __name__ == "__main__":
    if len(sys.argv) < 4 or "--original" not in sys.argv:
        raise SystemExit("Usage: run_native_render.py x86_test.exe --original MikuMikuDance.exe [options]")
    raise SystemExit(run(sys.argv[1:]))
