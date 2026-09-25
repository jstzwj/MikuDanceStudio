// ===========================================================================
// VA 0x0046E787..0x0046EFB7 - frame-step recording readback / push pass
// (inline section of the original FrameDriver sub_46B090)
// ===========================================================================
// Entered right after the scene render's EndScene when the frame-step mode
// byte app+0x9ED90 is set (AVI recording AND manual frame stepping): the
// non-recording path jumps straight to the catch-up section at 0x46EFBE.
// This block is what actually feeds the DirectShow graph - without it the
// MMDxShow push pin's FillBuffer never sees a frame and the AVI file never
// grows past its headers (the ③-C "stuck at 64KB" root cause).
//
// Per-pass flow (0x46E79A..0x46EFB7, non-stereo addresses):
//   one-time (first pass):
//     [0x9EB88] = CreateRenderTarget(capW, capH, backbuffer fmt, 0, 0,
//                                   Lockable=FALSE, &, 0)          0x46E79A
//       FAILED -> recording epilogue 0x464A00 + return from the driver
//     [0x9EB8C] = CreateOffscreenPlainSurface(capW, capH, fmt,
//                                   D3DPOOL_SYSTEMMEM, &, 0)       0x46E82D
//       FAILED -> epilogue + return
//     [0x9F334] = operator new(capW * capH * 4)                    0x46E88D
//   every pass:
//     ack wait: GetStreamingState(rec+0x68, &flag[0x9EDD4]) once,
//     then while (*flag == 0) { null-interface break; E_FAIL (stream
//     ended) break; Sleep(1) } - paces the push to the pin's FillBuffer
//     consumption (SetBitmapInfo seeds the ack, each push clears it,
//     FillBuffer re-sets it); vtable slot 4, x64 call [vt+0x20] at
//     0x7FF7CB44B1EF / 轮询循环头 0x7FF7CB44B200                 0x46E8D9
//     lazy GetBackBuffer (sub+0x1D538) + Present(NULL, NULL, NULL,
//     NULL) + D3DERR_DEVICELOST recovery (message pump + 0x440DB0)
//                                                                   0x46E92C
//     stereo enable (0x407470 -> nvapi 0x4CC0F0; -3 no-op without
//     nvapi)                                                       0x46EA49
//     3D Vision side-by-side branch (A0D61 && A0D64 == 2):
//       wide = CreateRenderTarget(2*screenW, screenH, ...);
//       StretchRect(bb, {0,0,screenW,screenH}, wide,
//                          {0,0,2*screenW,screenH}, LINEAR|NONE);
//       StretchRect(wide, {0,0,capW,capH},       [9EB88],
//                          {0,0,capW,capH},           LINEAR|NONE);
//       StretchRect(wide, {screenW,0,screenW+capW,capH}, [9EB88],
//                          {capW,0,2*capW,capH},        LINEAR|NONE)
//                                                                   0x46EA93
//     StretchRect(bb, {0,0,capW,capH}, [9EB88], same, LINEAR, then
//     NONE on failure); still FAILED -> epilogue + return          0x46ED5D
//     GetRenderTargetData([0x9EB88] -> [0x9EB8C]); FAILED -> ditto
//                                                                   0x46EDF1
//     LockRect([0x9EB8C], &locked, NULL, D3DLOCK_READONLY); FAILED
//     -> ditto;  bottom-up row copy locked.pBits -> [0x9F334]
//     (dst walks down from (capH-1)*capW*4, src up by Pitch)       0x46EE16
//     push: rec+0x68 vtable slot 5 fn(push, [0x9F334]) - the
//     per-frame bits handoff to MMDxShow (x64 call [vt+0x28] at
//     0x7FF7CB44B836; x86 原版同一槽的字节偏移是 +0x14)         0x46EEB0
//     UnlockRect([0x9EB8C])                                        0x46EECC
//   then falls into the catch-up counter/display (0x46EFBE, already
//   ported as PlaybackCatchup section 1).
//
// capW = 0xA08D4 render width (x 0xA0D64 when 0xA0D61 is set), capH =
// 0xA08D8.  Vtable slots used: device 0x44 Present, 0x48 GetBackBuffer,
// 0x70 CreateRenderTarget, 0x88 StretchRect, 0x80 GetRenderTargetData,
// 0x90 CreateOffscreenPlainSurface, 0x0C TestCooperativeLevel; surface
// 0x34 LockRect / 0x38 UnlockRect (all read off the disassembly; the
// interface pointers travel as explicit cdecl stack arguments in the
// original, e.g. fn(push, flag) at 0x46E8EE).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

// VA 0x00407470 - stereo activation toggle on the 0x1D574 wrapper
// (src/io/io_device_helpers.cpp).
int StereoActivationToggle(unsigned char* wrapper, std::uint8_t enable);

namespace {

constexpr long kEFail = static_cast<long>(0x80004005);

// MIKUDANCESTUDIO_TRACE_REC diagnostic (CMake option MIKUDANCESTUDIO_DIAG,
// default OFF): one line per pass (throttled), plus one line per early-out
// with the failing stage.  The OFF stub keeps the call sites valid and
// inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void TraceRec(const char* fmt, ...) {
    if (getenv("MIKUDANCESTUDIO_TRACE_REC") == nullptr)
        return;
    if (FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_REC"), "a")) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(tf, fmt, ap);
        va_end(ap);
        fclose(tf);
    }
}
#else
inline void TraceRec(const char*, ...) {}
#endif

// 与 dshow_record_graph.cpp / shutdown_cleanup.cpp 相同的 vtable 槽索引
// 惯用法：槽号是 ABI 无关的（x86 槽距 4 字节、x64 槽距 8 字节），按索引
// 取就不会把 x86 的字节偏移带进 x64 构建。
// The push interface (app+0xA06C0 object, +0x68 field).  IPushSource is
// an STDMETHODCALLTYPE (stdcall) interface: `this` travels as the first
// stack argument (the ABI the original binary's push-style call sites
// use).  A cdecl cast here drifts the stack 8 bytes per call and corrupts
// the caller's locals.
long CallStreamingState(IPushSource* push, void* flagPtr) {
    // GetStreamingState = 槽 4：x64 原版 call [vt+0x20] @0x7FF7CB44B1EF
    // （轮询循环头 0x7FF7CB44B217，返回值与 0x80004005 比较即流结束）。
    return push->GetStreamingState(flagPtr);
}

}  // namespace

// Returns false when the original aborts the whole FrameDriver pass
// (failure epilogue at 0x46E80B); true to continue into the catch-up.
bool RecordingReadbackPass(MMDApp* app) {
    auto& s = *app;
    D3DRenderer* wrapper = s.Renderer();
    if (wrapper == nullptr)
        return true;  // nothing the original could act on either
    IDirect3DDevice9* device = wrapper->device;  // +0x1D4E0
    if (device == nullptr)
        return true;

    const bool stereo3d = s.AviStereoOutput() != 0;
    const std::int32_t capW = stereo3d
        ? s.AviStereoWidthMultiplier() * s.RenderWidth()
        : s.RenderWidth();                                      // 0x46E86A
    const std::int32_t capH = s.RenderHeight();
    const D3DFORMAT fmt = wrapper->backbufferFormat;  // +0x1D540

    // ---- one-time surfaces + bits buffer ------------------------------
    IDirect3DSurface9** rtSlot = &s.CaptureRenderTarget();
    if (*rtSlot == nullptr) {                                    // 0x46E79A
        IDirect3DSurface9* surf = nullptr;
        const HRESULT hr = device->CreateRenderTarget(
            capW, capH, fmt, D3DMULTISAMPLE_NONE, 0, FALSE, &surf,
            nullptr);
        *rtSlot = surf;
        if (FAILED(hr) || surf == nullptr) {
            TraceRec("readback: CreateRenderTarget hr=%ld\n", (long)hr);
            FinishAviRecord(app);                                      // 0x46E80B
            return false;
        }
    }
    IDirect3DSurface9** sysSlot = &s.CaptureSystemSurface();
    if (*sysSlot == nullptr) {                                   // 0x46E82D
        IDirect3DSurface9* surf = nullptr;
        const HRESULT hr = device->CreateOffscreenPlainSurface(
            capW, capH, fmt, D3DPOOL_SYSTEMMEM, &surf, nullptr);
        *sysSlot = surf;
        if (FAILED(hr)) {
            TraceRec("readback: sysmem hr=%ld\n", (long)hr);
            FinishAviRecord(app);
            return false;
        }
    }
    if (s.CaptureReadbackPixels() == nullptr) {                  // 0x46E88D
        const std::size_t bytes = static_cast<std::size_t>(capW) *
                                  static_cast<std::size_t>(capH) * 4;
        s.CaptureReadbackPixels() = ::operator new(bytes);
    }

    // ---- previous-frame ack wait ---------------------------------------
    // DIAGNOSTIC ONLY: pace the recording passes to a wall-clock rate
    // (the original's pin-consumption ack paces it to ~60/s; the port runs
    // at ~200/s) to test for residual wall-clock coupling in the physics.
#ifdef MIKUDANCESTUDIO_DIAG
    if (const char* pace = getenv("MIKUDANCESTUDIO_REC_PACE_MS"))
        Sleep(atoi(pace));
#endif
    DShowRecorder* recorder = s.Recorder();
    IPushSource* push = recorder != nullptr
        ? static_cast<IPushSource*>(recorder->framePush)
        : nullptr;
    unsigned char* flagPtr = s.RecordingCompletionFlag();
    if (push != nullptr)
        CallStreamingState(push, flagPtr);                       // 0x46E8D9
    while (flagPtr != nullptr && *flagPtr == 0) {                // 0x46E8F5
        if (push == nullptr)
            break;
        if (CallStreamingState(push, flagPtr) == kEFail)
            break;  // streaming ended
        Sleep(1);
    }

    // ---- Present + device-lost recovery (0x46E92C) ---------------------
    IDirect3DSurface9** backBuffer = &wrapper->backbufferSurface;  // +0x1D538
    if (*backBuffer == nullptr)
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                              backBuffer);
    if (mme::Present(app, device, nullptr, nullptr, nullptr, nullptr) ==
        D3DERR_DEVICELOST) {
        while (device->TestCooperativeLevel() !=
               D3DERR_DEVICENOTRESET) {
            Sleep(1);
            MSG msg;
            if (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
        PostDeviceReset(app);                                    // 0x440DB0
    }
    // StereoActivationToggle (io_device_helpers.cpp) still works on the raw wrapper
    // bytes - hand it the object address unchanged.
    StereoActivationToggle(
        reinterpret_cast<unsigned char*>(wrapper), 1);           // 0x46EA49

    HRESULT stretch = 0;                                         // 0x46ED5D
    if (stereo3d && s.AviStereoWidthMultiplier() == 2) {
        // 3D Vision side-by-side packing (0x46EA93..0x46ED5C): stretch
        // the back buffer into a double-width staging target, then copy
        // the two eye regions down into the capture target.
        const std::int32_t screenW = GetSystemMetrics(SM_CXSCREEN);
        const std::int32_t screenH = GetSystemMetrics(SM_CYSCREEN);
        IDirect3DSurface9* wide = nullptr;
        device->CreateRenderTarget(screenW * 2, screenH, fmt,
                                   D3DMULTISAMPLE_NONE, 0, FALSE, &wide,
                                   nullptr);
        if (wide != nullptr) {
            RECT srcR{0, 0, screenW, screenH};
            RECT dstR{0, 0, screenW * 2, screenH};
            stretch = device->StretchRect(*backBuffer, &srcR, wide, &dstR,
                                          D3DTEXF_LINEAR);
            if (stretch != 0)
                stretch = device->StretchRect(*backBuffer, &srcR, wide,
                                              &dstR, D3DTEXF_NONE);
            RECT eyeL{0, 0, capW, capH};
            HRESULT eye = device->StretchRect(wide, &eyeL, *rtSlot, &eyeL,
                                              D3DTEXF_LINEAR);
            if (eye != 0)
                device->StretchRect(wide, &eyeL, *rtSlot, &eyeL,
                                    D3DTEXF_NONE);
            RECT eyeRsrc{screenW, 0, screenW + capW, capH};
            RECT eyeRdst{capW, 0, capW * 2, capH};
            eye = device->StretchRect(wide, &eyeRsrc, *rtSlot, &eyeRdst,
                                      D3DTEXF_LINEAR);
            if (eye != 0)
                device->StretchRect(wide, &eyeRsrc, *rtSlot, &eyeRdst,
                                    D3DTEXF_NONE);
            wide->Release();
        }
    } else {
        RECT rect{0, 0, capW, capH};
        stretch = device->StretchRect(*backBuffer, &rect, *rtSlot, &rect,
                                      D3DTEXF_LINEAR);
        if (stretch != 0)
            stretch = device->StretchRect(*backBuffer, &rect, *rtSlot,
                                          &rect, D3DTEXF_NONE);
    }
    if (stereo3d)
        StereoActivationToggle(
            reinterpret_cast<unsigned char*>(wrapper), 0);       // 0x46EDE4
    if (stretch != 0) {
        TraceRec("readback: StretchRect hr=%ld\n", (long)stretch);
        FinishAviRecord(app);                                          // 0x46EDEB
        return false;
    }

    // ---- GPU -> system memory -> push buffer -> MMDxShow ----------------
    const HRESULT grtdHr = device->GetRenderTargetData(*rtSlot, *sysSlot);
    if (FAILED(grtdHr)) {
        TraceRec("readback: GetRenderTargetData hr=%ld\n",
                 (long)grtdHr);
        FinishAviRecord(app);                                          // 0x46EE2F
        return false;
    }
    D3DLOCKED_RECT locked;
    if (FAILED((*sysSlot)->LockRect(&locked, nullptr,
                                    D3DLOCK_READONLY))) {        // 0x46EE16
        TraceRec("readback: LockRect failed\n");
        FinishAviRecord(app);
        return false;
    }
    {
        const std::size_t rowBytes =
            static_cast<std::size_t>(capW) * 4;
        unsigned char* dst = static_cast<unsigned char*>(
                                 s.CaptureReadbackPixels()) +
                             (static_cast<std::size_t>(capH) - 1) *
                                 rowBytes;
        const unsigned char* src =
            static_cast<const unsigned char*>(locked.pBits);
        for (std::int32_t y = 0; y < capH; ++y) {                // 0x46EE81
            std::memcpy(dst, src, rowBytes);
            src += locked.Pitch;
            dst -= rowBytes;                                     // flip
        }
    }
    static LONG passCount = 0;
    if (++passCount <= 5 || passCount % 60 == 0)
        TraceRec("readback pass#%ld push=%p capW=%d capH=%d" "\n",
                 passCount, (void*)push, (int)capW, (int)capH);
    if (push != nullptr) {                                       // 0x46EEB0
        // 帧推送 = 槽 5：x64 原版 call [vt+0x28] @0x7FF7CB44B836，
        // rdx = 像素缓冲（[r12+0xA0298]），返回值不检查。
        push->StartStreaming(
            reinterpret_cast<DWORD_PTR>(s.CaptureReadbackPixels()));
    }
    (*sysSlot)->UnlockRect();                                    // 0x46EECC
    return true;
}

}  // namespace mikudancestudio
