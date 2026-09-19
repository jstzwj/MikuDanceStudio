# -*- coding: utf-8 -*-
# Drag-drop crash reproducer with built-in debugger.
#   python crash_repro.py --exe <path> [--cwd <dir>] [--label name]
#        --drop <file> --wait <sec>   (repeatable pairs, in order)
#        --final <sec>  (how long to survive after last drop)
# Runs the exe under DEBUG_ONLY_THIS_PROCESS, finds its main window,
# posts WM_DROPFILES for each --drop after --wait seconds, and records
# first-chance + second-chance exceptions with CONTEXT/stack/module map.
# Exit code: 0 = survived, 2 = crashed (see crashdump/<label>.txt), 3 = no window.
import ctypes, ctypes.wintypes as wt, struct, sys, time, os, argparse

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

DEBUG_ONLY_THIS_PROCESS = 0x02
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_DEBUG_EVENT = 1
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
OUTPUT_DEBUG_STRING_EVENT = 8
WM_DROPFILES = 0x0233
GMEM_MOVEABLE = 0x0002
GMEM_ZEROINIT = 0x0040
CONTEXT_FULL_AMD64 = 0x100003  # CONTROL|INTEGER (avoid 16-alignment-sensitive FltSave)
TH32CS_SNAPMODULE = 0x0008
LIST_MODULES_ALL = 0x03


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD)] + [(n, wt.WCHAR * 320) for n in
        ("lpReserved", "lpDesktop", "lpTitle")] + [
        ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
        ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
        ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
        ("lpReserved2", ctypes.c_void_p), ("hStdInput", ctypes.c_void_p),
        ("hStdOutput", ctypes.c_void_p), ("hStdError", ctypes.c_void_p)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", ctypes.c_void_p), ("hThread", ctypes.c_void_p),
                ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD), ("_pad0", wt.DWORD),
                ("u", ctypes.c_byte * 200)]


class FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [("Control", wt.WORD), ("Status", wt.WORD), ("Tag", wt.WORD),
                ("ErrorOperand", wt.WORD * 4), ("Cr0NpxState", wt.WORD)]


class CONTEXT(ctypes.Structure):
    _fields_ = [
        ("P1Home", ctypes.c_ulonglong), ("P2Home", ctypes.c_ulonglong),
        ("P3Home", ctypes.c_ulonglong), ("P4Home", ctypes.c_ulonglong),
        ("P5Home", ctypes.c_ulonglong), ("P6Home", ctypes.c_ulonglong),
        ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
        ("SegCs", wt.WORD), ("SegDs", wt.WORD), ("SegEs", wt.WORD),
        ("SegFs", wt.WORD), ("SegGs", wt.WORD), ("SegSs", wt.WORD),
        ("EFlags", wt.DWORD),
        ("Dr0", ctypes.c_ulonglong), ("Dr1", ctypes.c_ulonglong),
        ("Dr2", ctypes.c_ulonglong), ("Dr3", ctypes.c_ulonglong),
        ("Dr6", ctypes.c_ulonglong), ("Dr7", ctypes.c_ulonglong),
        ("Rax", ctypes.c_ulonglong), ("Rcx", ctypes.c_ulonglong),
        ("Rdx", ctypes.c_ulonglong), ("Rbx", ctypes.c_ulonglong),
        ("Rsp", ctypes.c_ulonglong), ("Rbp", ctypes.c_ulonglong),
        ("Rsi", ctypes.c_ulonglong), ("Rdi", ctypes.c_ulonglong),
        ("R8", ctypes.c_ulonglong), ("R9", ctypes.c_ulonglong),
        ("R10", ctypes.c_ulonglong), ("R11", ctypes.c_ulonglong),
        ("R12", ctypes.c_ulonglong), ("R13", ctypes.c_ulonglong),
        ("R14", ctypes.c_ulonglong), ("R15", ctypes.c_ulonglong),
        ("Rip", ctypes.c_ulonglong),
        ("_pad_to_1232", ctypes.c_byte * (1232 - 0x10C)),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD),
                ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD),
                ("hModule", ctypes.c_void_p), ("szModule", wt.WCHAR * 256),
                ("szExePath", wt.WCHAR * 260)]


k32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
                               wt.BOOL, wt.DWORD, ctypes.c_void_p, wt.LPCWSTR,
                               ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION)]
k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wt.DWORD]
k32.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
k32.GetThreadContext.argtypes = [ctypes.c_void_p, ctypes.POINTER(CONTEXT)]
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                  ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.GlobalAlloc.restype = ctypes.c_void_p
k32.GlobalAlloc.argtypes = [wt.UINT, ctypes.c_size_t]
k32.GlobalLock.restype = ctypes.c_void_p
k32.GlobalLock.argtypes = [ctypes.c_void_p]
k32.GlobalUnlock.argtypes = [ctypes.c_void_p]
k32.OpenThread.restype = ctypes.c_void_p
k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
u32.PostMessageW.argtypes = [wt.HWND, wt.UINT, ctypes.c_void_p, ctypes.c_void_p]
u32.PostMessageW.restype = wt.BOOL

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_main_window(pid):
    found = []

    @EnumWindowsProc
    def cb(h, lp):
        wpid = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(wpid))
        if wpid.value == pid and u32.IsWindowVisible(h):
            buf = ctypes.create_unicode_buffer(256)
            u32.GetWindowTextW(h, buf, 256)
            if buf.value.startswith("MikuDance") and not found:
                found.append((h, buf.value))
        return True

    u32.EnumWindows(cb, 0)
    return found[0] if found else (None, None)


def post_drop(hwnd, paths):
    payload = "".join(p + "\0" for p in paths) + "\0"
    data = payload.encode("utf-16-le")
    total = 20 + len(data)
    h = k32.GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total)
    if not h:
        return False
    ptr = k32.GlobalLock(h)
    ctypes.memmove(ptr, struct.pack("<5i", 20, 0, 0, 0, 1), 20)
    ctypes.memmove(ctypes.c_void_p(ptr + 20), data, len(data))
    k32.GlobalUnlock(h)
    return bool(u32.PostMessageW(hwnd, WM_DROPFILES, h, 0))


def module_map(hproc):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0)
    k32.CloseHandle(snap)  # not used; psapi below
    arr_type = ctypes.c_void_p * 1024
    arr = arr_type()
    needed = wt.DWORD()
    mods = []
    if psapi.EnumProcessModulesEx(hproc, arr, ctypes.sizeof(arr), ctypes.byref(needed), LIST_MODULES_ALL):
        count = min(needed.value // ctypes.sizeof(ctypes.c_void_p), 1024)
        for i in range(count):
            name = ctypes.create_unicode_buffer(520)
            psapi.GetModuleFileNameExW(hproc, ctypes.c_void_p(arr[i]), name, 520)
            class LPMODULEINFO(ctypes.Structure):
                _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wt.DWORD), ("EntryPoint", ctypes.c_void_p)]
            mi = LPMODULEINFO()
            if psapi.GetModuleInformation(hproc, ctypes.c_void_p(arr[i]), ctypes.byref(mi), ctypes.sizeof(mi)):
                mods.append((mi.lpBaseOfDll or 0, mi.SizeOfImage, name.value))
    return mods


def resolve(addr, mods):
    for base, size, name in mods:
        if base <= addr < base + size:
            return "%s+0x%X" % (os.path.basename(name), addr - base)
    return "0x%016X" % addr


def rpm(hproc, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(hproc, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)):
        return b""
    return buf.raw[:got.value]


def dump_crash(out, hproc, hthread, ev, label, first_chances):
    mods = module_map(hproc)
    u = bytes(ev.u)
    code, = struct.unpack_from("<I", u, 0)
    addr, = struct.unpack_from("<Q", u, 16)
    nparams, = struct.unpack_from("<I", u, 24)
    params = struct.unpack_from("<15Q", u, 32)
    ctx_buf = ctypes.create_string_buffer(4096)
    ctypes.memmove(ctx_buf, b"\0" * 4096, 4096)
    ctx = CONTEXT.from_buffer(ctx_buf)
    ctx.ContextFlags = CONTEXT_FULL_AMD64
    ok = k32.GetThreadContext(hthread, ctypes.byref(ctx))
    out.write("label=%s code=0x%08X addr=%s thread=%d getctx=%d\n" %
              (label, code, resolve(addr, mods), ev.dwThreadId, ok))
    out.write("RIP=%s RSP=%s RBP=%s\n" % (resolve(ctx.Rip, mods), hex(ctx.Rsp), hex(ctx.Rbp)))
    out.write("RAX=%016X RBX=%016X RCX=%016X RDX=%016X\n" % (ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx))
    out.write("RSI=%016X RDI=%016X R8=%016X R9=%016X R10=%016X R11=%016X\n" %
              (ctx.Rsi, ctx.Rdi, ctx.R8, ctx.R9, ctx.R10, ctx.R11))
    out.write("av: op=%s target=%s\n" % (params[0], hex(params[1]) if len(params) > 1 else "?"))
    stack = rpm(hproc, ctx.Rsp, 1024)
    for i in range(0, len(stack) // 8):
        v, = struct.unpack_from("<Q", stack, i * 8)
        if v:
            tag = resolve(v, mods)
            if tag.startswith("MikuMikuDance") or tag.startswith("MMEffect"):
                out.write("stack[%02d]=%s\n" % (i, tag))
    out.write("---- modules (%d) ----\n" % len(mods))
    for base, size, name in sorted(mods):
        out.write("%016X %8X %s\n" % (base, size, name))
    out.write("---- first-chance log ----\n")
    for line in first_chances:
        out.write(line + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--cwd", default=None)
    ap.add_argument("--label", default="crash")
    ap.add_argument("--drop", action="append", default=[])
    ap.add_argument("--wait", action="append", type=float, default=[])
    ap.add_argument("--final", type=float, default=60.0)
    ap.add_argument("--watch-off", type=int, default=-1,
                    help="app-block offset to watch (write, 4 bytes)")
    ap.add_argument("--arm-at", type=float, default=10.0)
    ap.add_argument("--block-rva", type=lambda x: int(x, 0), default=0x100FD8)
    args = ap.parse_args()

    si = STARTUPINFOW(); si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    cmdline = ctypes.create_unicode_buffer('"%s"' % args.exe)
    if not k32.CreateProcessW(args.exe, cmdline, None, None, False,
                              DEBUG_ONLY_THIS_PROCESS, None, args.cwd or os.path.dirname(args.exe),
                              ctypes.byref(si), ctypes.byref(pi)):
        print("CreateProcess failed err=%d" % ctypes.get_last_error()); sys.exit(4)
    print("pid=%d" % pi.dwProcessId)

    os.makedirs("crashdump", exist_ok=True)
    outpath = os.path.join("crashdump", "ray_%s.txt" % args.label)
    drops = args.drop
    waits = [float(w) for w in (args.wait or [])]
    while len(waits) < len(drops):
        waits.append(10.0)
    schedule = list(zip(drops, waits))

    t0 = time.time()
    hwnd = None
    drop_idx = 0
    last_drop_time = None
    first_chances = []
    seen_fc = set()
    attach_bp_seen = False
    main_tid = 0
    watch_armed = False
    watch_addr = 0
    watch_hits = 0
    ev = DEBUG_EVENT()
    status = "survived"

    while True:
        got = k32.WaitForDebugEvent(ctypes.byref(ev), 200)
        now = time.time() - t0
        if hwnd is None and now < 40:
            h, title = find_main_window(pi.dwProcessId)
            if h:
                hwnd = h
                print("window %x %r at %.1fs" % (h, title, now))
        if hwnd and drop_idx < len(schedule):
            path, w = schedule[drop_idx]
            if now >= (sum(x[1] for x in schedule[:drop_idx]) + 12) if drop_idx == 0 else False:
                pass
        # schedule: first drop after 12s from window found, subsequent after their wait
        if hwnd is None and now > 40:
            print("no window found"); k32.TerminateProcess(pi.hProcess, 0); sys.exit(3)
        # arm the hardware write watchpoint on the UI thread
        if (args.watch_off >= 0 and not watch_armed and main_tid
                and now >= args.arm_at):
            mods = module_map(pi.hProcess)
            exe_base = 0
            for base, size, name in mods:
                if name.lower().endswith("mikumikudancee.exe"):
                    exe_base = base
                    break
            blk = 0
            if exe_base:
                d = rpm(pi.hProcess, exe_base + args.block_rva, 8)
                if d and len(d) == 8:
                    blk, = struct.unpack("<Q", d)
            if blk:
                watch_addr = blk + args.watch_off
                TH32CS_SNAPTHREAD = 0x4
                class THREADENTRY32(ctypes.Structure):
                    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD),
                                ("th32ThreadID", wt.DWORD),
                                ("th32OwnerProcessID", wt.DWORD),
                                ("tpBasePri", ctypes.c_long),
                                ("tpDeltaPri", ctypes.c_long),
                                ("dwFlags", wt.DWORD)]
                ts = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
                te = THREADENTRY32()
                te.dwSize = ctypes.sizeof(te)
                armed_count = 0
                if k32.Thread32First(ts, ctypes.byref(te)):
                    while True:
                        if te.th32OwnerProcessID == pi.dwProcessId:
                            hth = k32.OpenThread(0x1FFFFF, False, te.th32ThreadID)
                            if hth:
                                cb = ctypes.create_string_buffer(4096)
                                ctx = CONTEXT.from_buffer(cb)
                                ctx.ContextFlags = 0x00100010
                                k32.GetThreadContext(hth, ctypes.byref(ctx))
                                ctx.Dr0 = watch_addr
                                ctx.Dr7 = 0xD0001
                                if k32.SetThreadContext(hth, ctypes.byref(ctx)):
                                    armed_count += 1
                                k32.CloseHandle(hth)
                        if not k32.Thread32Next(ts, ctypes.byref(te)):
                            break
                k32.CloseHandle(ts)
                watch_armed = True
                print("[%.1fs] watch armed at 0x%X on %d threads (app 0x%X +%d)"
                      % (now, watch_addr, armed_count, blk, args.watch_off))
            else:
                print("watch arm FAILED (exe_base=%X)" % exe_base)
        if hwnd and drop_idx < len(schedule):
            path, w = schedule[drop_idx]
            ready = (now >= 12 + sum(x[1] for x in schedule[:drop_idx]))
            if ready:
                print("[%.1fs] drop %s" % (now, path))
                if not post_drop(hwnd, [os.path.abspath(path)]):
                    print("post failed")
                last_drop_time = time.time()
                drop_idx += 1
        if drop_idx >= len(schedule) and last_drop_time and time.time() - last_drop_time > args.final:
            print("survived %.0fs after last drop" % args.final)
            break

        if not got:
            continue
        cont = DBG_CONTINUE
        if ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
            u = bytes(ev.u)
            code, = struct.unpack_from("<I", u, 0)
            addr, = struct.unpack_from("<Q", u, 16)
            first, = struct.unpack_from("<I", u, 152)
            if code == 0x80000003 and not attach_bp_seen:
                attach_bp_seen = True
                main_tid = ev.dwThreadId
                cont = DBG_CONTINUE
            elif code == 0x80000004 and watch_armed:
                # hardware watchpoint hit: log the writer and resume
                hth = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)
                cb = ctypes.create_string_buffer(4096)
                ctx = CONTEXT.from_buffer(cb)
                ctx.ContextFlags = 0x100017  # CONTROL|INTEGER|DEBUG_REGISTERS
                k32.GetThreadContext(hth, ctypes.byref(ctx))
                if ctx.Dr6 & 1:
                    val = rpm(pi.hProcess, watch_addr, 4)
                    v = struct.unpack("<i", val)[0] if val and len(val) == 4 else -999
                    if v != 0 and watch_hits < 200:
                        watch_hits += 1
                        mods = module_map(pi.hProcess)
                        print("WATCH[%d] tid=%d rip=%s val=%d"
                              % (watch_hits, ev.dwThreadId,
                                 resolve(ctx.Rip, mods), v))
                        first_chances.append("watch rip=%s val=%d"
                                             % (resolve(ctx.Rip, mods), v))
                ctx.Dr6 = 0
                ctx.EFlags |= 0x10000  # RF: execute the write once more
                k32.SetThreadContext(hth, ctypes.byref(ctx))
                k32.CloseHandle(hth)
                cont = DBG_CONTINUE
            elif first == 0:
                # second chance: fatal
                hthread = None
                snap = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)  # THREAD_ALL_ACCESS
                with open(outpath, "w", encoding="utf-8") as f:
                    dump_crash(f, pi.hProcess, snap, ev, args.label, first_chances)
                print("CRASH second-chance 0x%08X at 0x%X -> %s" % (code, addr, outpath))
                k32.TerminateProcess(pi.hProcess, 1)
                time.sleep(0.5)
                sys.exit(2)
            else:
                if code in (0xC0000005, 0xC0000374, 0xC0000409, 0x80000003):
                    key = (code, addr)
                    if key not in seen_fc and len(first_chances) < 200:
                        seen_fc.add(key)
                        mods = module_map(pi.hProcess)
                        first_chances.append("fc code=0x%08X addr=%s t=%d" %
                                             (code, resolve(addr, mods), ev.dwThreadId))
                cont = DBG_EXCEPTION_NOT_HANDLED
        elif ev.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT:
            cont = DBG_CONTINUE
        elif ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT:
            print("process exited on its own at %.1fs" % (time.time() - t0))
            # distinguish clean exit vs crash-exit: exit code in u
            u = bytes(ev.u)
            ec, = struct.unpack_from("<I", u, 0)
            print("exit code 0x%08X" % ec)
            if ec not in (0,):
                with open(outpath, "w", encoding="utf-8") as f:
                    f.write("label=%s process-exit code=0x%08X\n" % (args.label, ec))
                sys.exit(2)
            break
        k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont)

    k32.TerminateProcess(pi.hProcess, 0)
    print("done -> survived")
    sys.exit(0)


if __name__ == "__main__":
    main()
