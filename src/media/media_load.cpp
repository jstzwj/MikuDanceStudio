// ===========================================================================
// VA 0x0042AE20 - CopyDirPathW         (original: sub_42AE20, 16 bytes)
// VA 0x0042AE40 - CopyPathW         (original: sub_42AE40, 23 bytes)
// VA 0x00433250 - LoadAviFile       (original: sub_433250, 0x53F bytes)
// VA 0x004337A0 - LoadBackgroundPicture (original: sub_4337A0, 0x293 bytes)
// ===========================================================================
// The background-media loaders behind menu 0xD5 ("load background AVI
// file") and menu 0xE8 (case 232, "load background picture file"), plus
// the two path-copy thunks both flows use (wcscpy_s with a fixed count:
// 0x3E8 for the UserFile dir buffers, 0x100 for the media path buffers).
//
// 0x433250/0x4337A0 are __thiscall(app); like 0x418500 the file path is
// read from app storage (app+0x9E1EC for AVI, app+0x9E448 for pictures),
// so these app-taking overloads supersede the old path-taking stub
// declarations (those twins have since been deleted from stubs.cpp).
//
// AVI uses the raw VFW API (AVIFIL32): open -> AVIFileInfoA -> pick the
// 'vids' stream with the LOWEST wPriority (initial best 0xFFFF) ->
// AVIStreamGetFrameOpen -> AVIStreamGetFrame(0) for the BITMAPINFOHEADER.
// The picture loader goes through D3DXCreateTextureFromFileExW (1024x1024,
// D3DPOOL_MANAGED; retry 512x512/1 mip level), storing the texture at
// app+0x9E42C like the original.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <vfw.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// 0x52BA94: "AVIデータ読込"
static const char kMsgAviCaptionJp[] =
    "AVI\x83\x66\x81\x5B\x83\x5E\x93\xC7\x8D\x9E";
// 0x52CA3C: "AVIファイルが読み込めません"
static const char kMsgAviOpenFailJp[] =
    "AVI\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xAA\x93\xC7\x82\xDD\x8D\x9E"
    "\x82\xDF\x82\xDC\x82\xB9\x82\xF1";
// 0x52BA28: "AVIファイルを読み込めません"
static const char kMsgAviOpenFail2Jp[] =
    "AVI\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xF0\x93\xC7\x82\xDD\x8D\x9E"
    "\x82\xDF\x82\xDC\x82\xB9\x82\xF1";
// 0x52CA04: "AVIFILEINFOを読み込めません"
static const char kMsgAviInfoFailJp[] =
    "AVIFILEINFO\x82\xF0\x93\xC7\x82\xDD\x8D\x9E\x82\xDF\x82\xDC\x82\xB9"
    "\x82\xF1";
// 0x52C9D4: "AVIFileGetStream失敗"
static const char kMsgAviGetStreamFailJp[] =
    "AVIFileGetStream\x8E\xB8\x94\x73";
// 0x52C9A8: "AVIStreamInfo失敗"
static const char kMsgAviStreamInfoFailJp[] =
    "AVIStreamInfo\x8E\xB8\x94\x73";
// 0x52CA70: "BMPファイルを読み込めませんでした"
static const char kMsgBmpFailJp[] =
    "BMP\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xF0\x93\xC7\x82\xDD\x8D\x9E"
    "\x82\xDF\x82\xDC\x82\xB9\x82\xF1\x82\xC5\x82\xB5\x82\xBD";
// 0x52CA94: "BMPファイル読込"
static const char kMsgBmpCaptionJp[] = "BMP\x83\x74\x83\x40\x83\x43\x83\x8B"
                                       "\x93\xC7\x8D\x9E";

// minimal D3DXIMAGE_INFO head (no SDK d3dx header, cf. path_resolve.cpp)
struct ImgInfo {                    // first fields of D3DXIMAGE_INFO
    UINT Width, Height, Depth, MipLevels;
};

// x64 端纵横比数学用的 .rdata float 常量（SSE 单精度乘除，
// sub_7FF7CB487A20 @0x7ff7cb487ed3/0x7ff7cb487f03 引用）。
constexpr float kOverlaySpan = 1.2f;   // float @0x7FF7CB552B3C (3F 99 99 9A)
constexpr float kOverlayHalf = 0.5f;   // float @0x7FF7CB55298C (3F 00 00 00)

// 0x50747B: CRT _wsopen_s(&fh, path, _O_BINARY, _SH_DENYNO, _S_IREAD);
// used as an existence probe (returns errno, 0 = exists).
bool WsOpenOk(const wchar_t* path) {
    int fh = -1;
    if (_wsopen_s(&fh, path, _O_BINARY, _SH_DENYNO, _S_IREAD) == 0) {
        _close(fh);
        return true;
    }
    return false;
}

// Shared tail of the 0x433250 failure paths (0x433332..0x43334A):
// clear the stored AVI path, drop the display flag.
void AviFailTail(MMDApp* app) {
    swprintf_s(app->AviBackgroundPath(), 0x100, L"");              // 0x507499
    app->AviBackgroundEnabled() = 0;                                // 0x43333F
}

// 背景媒体（AVI/图片）的纵横比参数，AVI 路径与图片路径共用。
// x64 定谳（全 SSE 单精度，求值序即下式书写序）：
//   AVI   sub_7FF7CB487A20 @0x7ff7cb487ec5..0x7ff7cb487f1b
//   图片  sub_7FF7CB488970 @0x7ff7cb488bf2..0x7ff7cb488c55（同式）
//   zoom = ((hideRectW / viewScale) * 1.2f) / mediaW     -> 写 app+652036
//   posY = h2 - ((mediaH * zoom * viewScale) / 1.2f) * 0.5f  -> 写 app+652032
//   h2 = (hideBottom - hideTop) / 2（整数除法）
// 物理含义：媒体恰好铺满视口宽并垂直居中——消费端 ComputeOverlayRect
// (src/render/bg_overlay.cpp) 的 W*zoom*ratio/1.2 恰好还原出 hideRectW，
// 即本 zoom 正是该公式所需。
// 历史注记：旧版写成 scale/offsetY 双倒数+反号，根因是 x86 x87 的
// fdivrp/fsubrp 反操作数伪影误导；x64 SSE 版（divss/mulss/subss 操作
// 数顺序直观可见）证明上述才是原意。
// 另：hideLeft 实为右缘、hideRight 实为左缘（见 mmd_app.hpp 的 RECT
// 视图），故 hideLeft - hideRight 是正的视口宽；h2 同理取正半高。
void MediaAspect(MMDApp* app, std::int32_t width, std::int32_t height,
                 float& scaleOut, std::int32_t& posOut) {
    auto& s = *app;
    const std::int32_t w = s.state.hideLeft - s.state.hideRight;   // 0x4336B7
    const std::int32_t h2 =
        (s.state.hideBottom - s.state.hideTop) / 2;                // 0x4336E9
    const float ratio = s.Renderer()->viewScale;                   // 0x4336E0
    const float spanW = static_cast<float>(w) / ratio;             // 0x4336F5..
    const float zoom = (spanW * kOverlaySpan) / static_cast<float>(width);
    const float drawnH =
        (static_cast<float>(height) * zoom * ratio) / kOverlaySpan;
    scaleOut = zoom;
    posOut = static_cast<std::int32_t>(
        static_cast<float>(h2) - drawnH * kOverlayHalf);           // 0x43370D..
}

}  // namespace

// ---------------------------------------------------------------------------
// VA 0x0042AE20 - CopyDirPathW(dest, src): wcscpy_s(dest, 0x3E8, src) thunk
// for the wchar_t[1000] UserFile directory buffers.  (Body moved here from
// the command_view_menu.cpp placeholder per its TODO; real port.)
// ---------------------------------------------------------------------------
void CopyDirPathW(wchar_t* dest, const wchar_t* src) {
    wcscpy_s(dest, 0x3E8, src);                                     // 0x506292
}

// ---------------------------------------------------------------------------
// VA 0x0042AE40 - CopyPathW(dest, src): wcscpy_s(dest, 0x100, src) thunk
// for the wchar_t[256] media path buffers.  (Supersedes the was-0x42AE40
// stub, since deleted from stubs.cpp.)
// ---------------------------------------------------------------------------
void CopyPathW(wchar_t* dest, const wchar_t* src) {
    wcscpy_s(dest, 0x100, src);                                     // 0x506292
}

// ---------------------------------------------------------------------------
// VA 0x00433250 - LoadAviFile(this=app).  Reads the path from app+0x9E1EC
// after resolving it under the UserFile tree (0x4089F0).  Handles stored at
// app+0x9E3FC (PAVIFILE), +0x9E400 (PAVISTREAM), +0x9E404 (GETFRAME).
// ---------------------------------------------------------------------------
void LoadAviFile(MMDApp* app) {
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd());

    // release any previously open AVI 0x433269..0x4332B6
    if (s.AviFrameReader() != nullptr) {
        AVIStreamGetFrameClose(static_cast<PGETFRAME>(s.AviFrameReader()));
        s.AviFrameReader() = nullptr;
    }
    if (s.AviStream() != nullptr) {
        AVIStreamRelease(static_cast<PAVISTREAM>(s.AviStream()));
        s.AviStream() = nullptr;
    }
    if (s.AviFile() != nullptr) {
        AVIFileRelease(static_cast<PAVIFILE>(s.AviFile()));
        s.AviFile() = nullptr;
    }

    PathResolutionWorkspace& paths = app->PathWorkspace();
    ResolveUserFilePath(paths, s.AviBackgroundPath());            // 0x4089F0
    wchar_t path[0x100];
    wcscpy_s(path, 0x100,
             paths.resolvedPath);                                  // 0x506292

    PAVIFILE file = nullptr;
    if (AVIFileOpenW(&file, path, 0x40 /* OF_SHARE_DENY_NONE */,
                     nullptr) != 0) {                               // 0x522E80
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open AVI file!!"
                                       : kMsgAviOpenFailJp,
                    s.EnglishUI() != 0 ? "open AVI file" : kMsgAviCaptionJp,
                    0);                                             // 0x4332FB
        s.AviFile() = nullptr;                                      // 0x43333D
        AviFailTail(app);
        return;
    }
    s.AviFile() = file;

    // x64 在 AVIFileOpenW 成功后立即把解析后的绝对路径回写存储槽
    // (wcscpy_s app+651448, @0x7ff7cb487b62)——PMM 保存/重开时原版
    // 保留的正是这条解析后路径；图片侧同款回写见 0x43386D（本文件
    // LoadBackgroundPicture 内 wcscpy_s(stored, 0x100, path)）。
    wcscpy_s(s.AviBackgroundPath(), 0x100, path);

    AVIFILEINFOA info{};
    if (AVIFileInfoA(file, &info, 0x6C) != 0) {                     // 0x522E7A
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot read AVIFILEINFO!!"
                                       : kMsgAviInfoFailJp,
                    s.EnglishUI() != 0 ? "open AVI file" : kMsgAviCaptionJp,
                    0);                                             // 0x43337C
        AVIFileRelease(file);                                       // 0x4333A1
        s.AviFile() = nullptr;
        AviFailTail(app);
        return;
    }

    // stream scan 0x4333CC..0x43346E: keep the 'vids' stream with the
    // LOWEST wPriority (initial best 0xFFFF); non-video streams are
    // released immediately.
    PAVISTREAM best = nullptr;
    unsigned short bestPrio = 0xFFFF;
    for (DWORD i = 0; i < info.dwStreams; ++i) {
        PAVISTREAM stream = nullptr;
        if (AVIFileGetStream(file, &stream, 0, i) != 0) {           // 0x522E74
            MessageBoxA(hwnd,
                        s.EnglishUI() != 0 ? "Failed AVIFileGetStream"
                                           : kMsgAviGetStreamFailJp,
                        s.EnglishUI() != 0 ? "open AVI file"
                                           : kMsgAviCaptionJp,
                        0);                                         // 0x4333F3
            AVIFileRelease(file);
            s.AviFile() = nullptr;
            AviFailTail(app);
            return;
        }
        AVISTREAMINFOA si{};
        if (AVIStreamInfoA(stream, &si, 0x8C) != 0) {               // 0x522E6E
            MessageBoxA(hwnd,
                        s.EnglishUI() != 0 ? "Failed AVIStreamInfo"
                                           : kMsgAviStreamInfoFailJp,
                        s.EnglishUI() != 0 ? "open AVI file"
                                           : kMsgAviCaptionJp,
                        0);                                         // 0x43340F
            AVIFileRelease(file);
            s.AviFile() = nullptr;
            AviFailTail(app);
            return;
        }
        if (si.fccType == 0x73646976 /* 'vids' */) {                 // 0x433417
            if (best == nullptr || si.wPriority < bestPrio) {       // 0x43343C
                if (best != nullptr)
                    AVIStreamRelease(best);                         // 0x433444
                best = stream;
                bestPrio = si.wPriority;                            // 0x433458
            } else {
                AVIStreamRelease(stream);
            }
        } else {
            AVIStreamRelease(stream);                               // 0x433425
        }
    }

    if (best == nullptr) {                                          // 0x433474
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open AVI file"
                                       : kMsgAviOpenFail2Jp,
                    s.EnglishUI() != 0 ? "open AVI file" : kMsgAviCaptionJp,
                    0);                                             // 0x433483
        AVIFileRelease(file);
        s.AviFile() = nullptr;
        AviFailTail(app);
        return;
    }
    s.AviStream() = best;

    PGETFRAME pgf = AVIStreamGetFrameOpen(best, nullptr);  // 0x522E68 (2 args; the
                                              // VC9 header carried a 3rd, ignored)
    s.AviFrameReader() = pgf;                                      // 0x433540
    if (pgf == nullptr) {
        AVIStreamRelease(best);                                     // 0x433551
        s.AviStream() = nullptr;
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open AVI file"
                                       : kMsgAviOpenFail2Jp,
                    s.EnglishUI() != 0 ? "open AVI file" : kMsgAviCaptionJp,
                    0);                                             // 0x43354A
        AVIFileRelease(file);
        s.AviFile() = nullptr;
        AviFailTail(app);
        return;
    }

    s.AviStreamStartFrame() = AVIStreamStart(best);                  // 0x522E62
    s.AviStreamEndFrame() =
        s.AviStreamStartFrame() + AVIStreamLength(best);             // 0x522E5C
    const LONG frameMs = AVIStreamSampleToTime(best, 1);            // 0x522E56
    s.AviUsesThirtyFpsTiming() =
        (frameMs > 0x20 && frameMs < 0x22) ? 1 : 0;                 // 0x4335E7

    LPBITMAPINFOHEADER header =
        static_cast<LPBITMAPINFOHEADER>(AVIStreamGetFrame(pgf, 0)); // 0x522E38
    if (header == nullptr) {                                        // 0x433613
        AVIStreamGetFrameClose(pgf);                                // 0x433624
        s.AviFrameReader() = nullptr;
        AVIStreamRelease(best);
        s.AviStream() = nullptr;
        AVIFileRelease(file);
        s.AviFile() = nullptr;
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open AVI file"
                                       : kMsgAviOpenFail2Jp,
                    s.EnglishUI() != 0 ? "open AVI file" : kMsgAviCaptionJp,
                    0);                                             // 0x433653
        s.AviBackgroundEnabled() = 0;                               // 0x433695
        swprintf_s(s.AviBackgroundPath(), 0x100, L"");
        return;
    }

    // success: record frame geometry + aspect fields 0x4336A8..0x433733
    s.AviFrameWidth() = header->biWidth;                            // 0x4336B1
    s.AviFrameHeight() = header->biHeight;                          // 0x4336C6
    s.AviOffsetX() = 0;                                             // 0x4336EF
    float scale = 0.0f;
    std::int32_t pos = 0;
    MediaAspect(app, header->biWidth, header->biHeight, scale, pos);
    s.AviScale() = scale;
    s.AviOffsetY() = pos;
    CheckMenuItem(GetMenu(hwnd), 0xD8, MF_CHECKED);                 // 0x433731

    if (s.AviBackgroundSurface() != nullptr) {                       // 0x433752
        s.AviBackgroundSurface()->Release();
        s.AviBackgroundSurface() = nullptr;
    }
    s.AviBackgroundEnabled() = 1;                                   // 0x43376C
    AviBgOverlayRefresh(app);                                                 // 0x433776
}

// ---------------------------------------------------------------------------
// VA 0x004337A0 - LoadBackgroundPicture(this=app).  Reads the path from
// app+0x9E448 after UserFile resolution; D3DX texture at app+0x9E42C.
// (Supersedes the was-0x4337A0 stub, since deleted from stubs.cpp.)
// ---------------------------------------------------------------------------
void LoadBackgroundPicture(MMDApp* app) {
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd());
    wchar_t* stored = s.PictureBackgroundPath();

    PathResolutionWorkspace& paths = app->PathWorkspace();
    ResolveUserFilePath(paths, stored);                             // 0x4089F0
    wchar_t path[0x100];
    wcscpy_s(path, 0x100,
             paths.resolvedPath);                                  // 0x506292
    if (!WsOpenOk(path)) {                                          // 0x50747B
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open BMP file"
                                       : kMsgBmpFailJp,
                    s.EnglishUI() != 0 ? "open BMP file" : kMsgBmpCaptionJp,
                    0);                                             // 0x4337FE
        swprintf_s(stored, 0x100, L"");                             // 0x433842
        return;
    }
    wcscpy_s(stored, 0x100, path);                                  // 0x43386D

    if (s.PictureBackgroundTexture() != nullptr) {                   // 0x433872
        s.PictureBackgroundTexture()->Release();
        s.PictureBackgroundTexture() = nullptr;
    }

    IDirect3DDevice9* device = s.Renderer()->device;                // 0x43389C
    auto& api = d3dx::Get();
    IDirect3DTexture9** slot = &s.PictureBackgroundTexture();
    ImgInfo info = {};
    constexpr UINT kD3dxDefault = 0xFFFFFFFFu;
    HRESULT hr = E_FAIL;
    hr = api.fromFileExW(device, stored, 0x400, 0x400, 0, 0,
                         D3DFMT_UNKNOWN, D3DPOOL_MANAGED, kD3dxDefault,
                         kD3dxDefault, 0, &info, nullptr, slot);  // 0x4C68AC
    if (hr != 0) {
        hr = api.fromFileExW(device, stored, 0x200, 0x200, 1, 0,
                             D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                             kD3dxDefault, kD3dxDefault, 0, &info,
                             nullptr, slot);                    // 0x4338FC
    }
    if (hr != 0) {                                                  // 0x433903
        MessageBoxA(hwnd,
                    s.EnglishUI() != 0 ? "Cannot open BMP file"
                                       : kMsgBmpFailJp,
                    s.EnglishUI() != 0 ? "open BMP file" : kMsgBmpCaptionJp,
                    0);                                             // 0x43393A
        swprintf_s(stored, 0x100, L"");                             // 0x43394A
        return;
    }

    // success 0x433965..0x433A22
    s.PictureWidth() = static_cast<std::int32_t>(info.Width);
    s.PictureHeight() = static_cast<std::int32_t>(info.Height);
    s.PictureOffsetX() = 0;                                         // 0x4339AE
    float scale = 0.0f;
    std::int32_t pos = 0;
    MediaAspect(app, static_cast<std::int32_t>(info.Width),
                static_cast<std::int32_t>(info.Height), scale, pos);
    s.PictureScale() = scale;
    s.PictureOffsetY() = pos;
    s.PictureBackgroundEnabled() = 1;                               // 0x433A07
    CheckMenuItem(GetMenu(hwnd), 0xE9, MF_CHECKED);                 // 0x4339F3..
    PicBgOverlayRefresh(app);                                                 // 0x433A22
}

}  // namespace mikudancestudio
