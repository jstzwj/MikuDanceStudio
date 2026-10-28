// ===========================================================================
// VA 0x00466D20 - CreateUIControls  (original: sub_466D20, 0x436B bytes)
// ===========================================================================
// Called from WM_CREATE in the main WndProc (0x004C3A10); returning false
// terminates the process (original: `if (!sub_466D20(hWnd)) exit(1);`).
//
// PHASE A (this file): drag-and-drop acceptance + the full 168-control
// creation sequence (recovered table in ui_controls.inc - ids,
// classes, texts, styles, geometry and
// sidebar-relative positions are the original's, in original order).
//
// TODO(port) remaining phases of the original (tracked in docs/PORTING_STATUS.md):
//   - splite.tga / auxiliary D3D texture load
//   - font system init tail (0x00424DC0 / 0x0042AE80 beyond CreateUiFont)
//
// Deviation note (resolved): per-control ANSI/wide variants are now issued
// through CreateWindowExA/W exactly like the original call sites; ANSI
// captions (".<", ">|", "X", SJIS texts) reach the A entry points as the
// original .rdata bytes, wide texts as \xXXXX escapes from .rdata.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commctrl.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <dsound.h>
#include <shellapi.h> // DragAcceptFiles
#include <objbase.h>  // CoInitialize (0x408EF0 DirectShowInit)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/screen_vertex.hpp"
#include "mikudancestudio/image_resources.hpp"
#include <cstdio>
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/rhi_assets.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "ui_controls.inc"

namespace mikudancestudio {

namespace {
struct HudAtlasPixels {
    rhi::AtlasAsset atlas;
};
std::unordered_map<MMDApp*, HudAtlasPixels> g_hudAtlasPixels;
}

bool LoadHudSpriteRgba(MMDApp* app, const std::uint8_t*& pixels, std::size_t& bytes,
                       std::uint32_t& width, std::uint32_t& height, std::uint64_t& revision) {
    const auto found = g_hudAtlasPixels.find(app);
    if (found == g_hudAtlasPixels.end())
        return false;
    pixels = found->second.atlas.image.pixels.data();
    bytes = found->second.atlas.image.pixels.size();
    width = found->second.atlas.image.width;
    height = found->second.atlas.image.height;
    revision = found->second.atlas.revision;
    return true;
}

void ForgetHudSpriteRgba(MMDApp* app) {
    g_hudAtlasPixels.erase(app);
}

static int EvalPos(const ui::PosExpr& e, int sidebar) {
    switch (e.kind) {
    case 1:
        return sidebar + e.off; // sidebar + off
    case 2:
        return sidebar / 2 + e.off; // sidebar/2 + off
    default:
        return e.off; // fixed literal
    }
}

// j_??2@YAPAXI@Z - the VC9 throwing operator new(uint) used for the track
// key arrays, followed by the original's immediate memset-zero.
template <typename Key> static Key* NewZeroedTrack() {
    constexpr std::size_t bytes = mdl::kTimelineKeyCapacity * sizeof(Key);
    auto* p = static_cast<Key*>(::operator new(bytes));
    std::memset(p, 0, bytes);
    return p;
}

// Porting-era trace under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call site
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
static void TraceInitialAccessoryTrack(const MMDApp& app) {
    const char* directory = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (directory == nullptr || directory[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream, "stage=ui-initial-accessory-track-174 image=%p track=%p\r\n",
            GetModuleHandleW(nullptr), app.AccessoryKeyTracks()[174]);
    fclose(stream);
}
#else
static inline void TraceInitialAccessoryTrack(const MMDApp&) {
}
#endif

static bool InitTimelineAudio(MMDApp* app, HWND hwnd, HDC timeline, bool english) {
    WaveAudioContext* audio = app->Audio();
    audio->englishUI = english ? 1 : 0;
    audio->mainWindow = hwnd;

#ifdef MIKUDANCESTUDIO_DIAG
    // Debug escape hatch: MSVC ASAN's CreateThread wrapper crashes the
    // DSOUND worker thread that DirectSoundCreate spawns (null read inside
    // GetDeviceID), so sanitizer builds can run without timeline audio.
    if (std::getenv("MIKUDANCESTUDIO_SKIP_DSOUND") != nullptr)
        return false;
#endif

    IDirectSound*& directSound = audio->directSound;
    if (FAILED(DirectSoundCreate(nullptr, &directSound, nullptr))) {
        // x64 sub_7FF7CB4FA070 @ 0x7FF7CB4FA0C9: JP text is the full blob at
        // 0x7FF7CB552568 =
        // "Cannot create DirectSound.\nDirectSound is unavailable".
        // Caption stays the empty string: the original passes a pointer at
        // a NUL byte (IDA name "Locale" resolves to zeroed data).
        static const char kJpCannotMakeDirectSound[] =
            "DirectSound\x82\xAA\x90\xB6\x90\xAC\x82\xC5\x82\xAB\x82\xDC"
            "\x82\xB9\x82\xF1\nDirectSound\x82\xCC\x8E\x67\x97\x70\x82\xAA"
            "\x95\x73\x89\xC2\x94\x5C\x82\xC5\x82\xB7";
        MessageBoxA(hwnd, english ? "Cannot make DirectSound!!" : kJpCannotMakeDirectSound, "",
                    MB_OK);
        return false;
    }
    if (FAILED(directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY))) {
        // JP text at x64 0x7FF7CB5525C8:
        // "Failed to set DirectSound cooperative level".
        static const char kJpFailedCooperateDirectSound[] =
            "DirectSound\x82\xCC\x8B\xA2\x92\xB2\x82\xC9\x8E\xB8\x94\x73\x82\xB5\x82\xDC\x82\xB5\x82\xBD";
        MessageBoxA(hwnd,
                    english ? "Failed cooperate DirectSound!!" : kJpFailedCooperateDirectSound, "",
                    MB_OK);
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 44100;
    format.nAvgBytesPerSec = 176400;
    format.nBlockAlign = 4;
    format.wBitsPerSample = 16;

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = 0x81E0;
    desc.dwBufferBytes = 352800;
    desc.lpwfxFormat = &format;
    audio->bufferBytes = static_cast<std::int32_t>(desc.dwBufferBytes);

    IDirectSoundBuffer*& buffer = audio->streamingBuffer;
    if (FAILED(directSound->CreateSoundBuffer(&desc, &buffer, nullptr)))
        buffer = nullptr;
    if (buffer != nullptr) {
        buffer->SetCurrentPosition(0);
        buffer->Play(0, 0, DSBPLAY_LOOPING);
        Sleep(1);
        buffer->Stop();
        buffer->Release();
        buffer = nullptr;
    }

    audio->timelineDC = timeline;
    return true;
}

// VA 0x0040AE00 - .  Blank the timeline strip and the
// interpolation-curve box: white rectangles over both GDI surfaces (the
// timeline spans the backbuffer width at 49 px, the curve is 127x127) plus
// the black centre line at timeline y=25.
void ClearTimelineAndCurveDCs(MMDApp* app) {
    HDC timeline = app->TimelineDC();
    HDC curve = app->CurveDC();
    D3DRenderer* r = app->Renderer();
    const int backbufferWidth = r->screenWidth;

    HPEN whitePen = CreatePen(PS_SOLID, 1, 0xFFFFFF);
    HBRUSH whiteBrush = CreateSolidBrush(0xFFFFFF);
    HGDIOBJ oldPen = SelectObject(timeline, whitePen);
    HGDIOBJ oldBrush = SelectObject(timeline, whiteBrush);
    SelectObject(curve, whitePen);
    SelectObject(curve, whiteBrush);
    Rectangle(timeline, 0, 0, backbufferWidth, 49);
    Rectangle(curve, 0, 0, 127, 127);
    SelectObject(timeline, oldPen);
    SelectObject(curve, oldPen);
    DeleteObject(whitePen);
    SelectObject(timeline, oldBrush);
    SelectObject(curve, oldBrush);
    DeleteObject(whiteBrush);

    HPEN blackPen = CreatePen(PS_SOLID, 1, 0);
    SelectObject(timeline, blackPen);
    MoveToEx(timeline, 0, 25, nullptr);
    LineTo(timeline, backbufferWidth, 25);
    SelectObject(timeline, oldPen);
    DeleteObject(blackPen);
}

static bool LoadPngTexture(MMDApp* app, IDirect3DDevice9* device, WORD resourceId,
                           IDirect3DTexture9** output) {
    if (resourceId == 0x66)
        ForgetHudSpriteRgba(app);
    const rhi::CallbackAssetSource source([app](rhi::AssetId id, std::vector<std::uint8_t>& bytes) {
        const HMODULE module = static_cast<HMODULE>(app->HInstance());
        const HRSRC info = FindResourceA(module, MAKEINTRESOURCEA(static_cast<WORD>(id)), "PNG");
        if (!info)
            return false;
        const HGLOBAL resource = LoadResource(module, info);
        const void* data = resource ? LockResource(resource) : nullptr;
        const DWORD length = SizeofResource(module, info);
        if (!data || !length || length > 16u * 1024u * 1024u)
            return false;
        const auto* begin = static_cast<const std::uint8_t*>(data);
        bytes.assign(begin, begin + length);
        return true;
    });
    std::vector<std::uint8_t> bytes;
    if (!source.Read(static_cast<rhi::AssetId>(resourceId), bytes))
        return false;
    auto& api = d3dx::Get();
    const bool loaded = SUCCEEDED(
        api.fromMemEx(device, bytes.data(), static_cast<UINT>(bytes.size()), static_cast<UINT>(-1),
                      static_cast<UINT>(-1), 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                      static_cast<DWORD>(-1), static_cast<DWORD>(-1), 0, nullptr, nullptr, output));
    if (loaded && resourceId == 0x66) {
        HudAtlasPixels atlas;
        if (rhi::LoadAtlas(source, rhi::AssetId::HudSprites, atlas.atlas)) {
            g_hudAtlasPixels[app] = std::move(atlas);
        }
    }
    return loaded;
}

bool CreateUIControls(MMDApp* app, HWND hwnd) {
    auto& s = *app;

    // D3D bootstrap first (original call site 0x00466D5B, top of UI init).
    if (!InitD3D(app, hwnd, app->EnglishUI() != 0, static_cast<HMODULE>(app->HInstance())))
        return false;

    // Physics scene construction (original call site 0x00466D78, directly
    // after the D3D bootstrap): gizmo buffers + the Bullet world and its
    // static ground body inside the 0x048 wrapper.
    if (!SceneConstruct(app->Physics(), app->Renderer()))
        return false;

    // Phase 1 of the original: enable drag-and-drop of model/scene files.
    DragAcceptFiles(hwnd, TRUE);

    // ---- 0x466D8A..0x466E69: off-screen GDI surfaces ------------------------
    // Four DCs track the D3D back-buffer size (wrapper at this+0xA06C4,
    // dims +0x1D4E4/+0x1D4E8): the horizontal frame ruler strip (bbW x 50,
    // this+0x2E0), the main panel double buffer (bbW x bbH, this+0x2D4, with
    // a spare bitmap at this+0x2D8), the 128x128 interpolation-curve cache
    // (this+0x2E8) and a temporary scratch DC the pre-draw fills below use
    // to paint the spare bitmap.  WM_PAINT blits these DCs
    // (wm_paint.cpp:396) - without them the timeline area stays black.
    HDC hdc = GetDC(hwnd);
    D3DRenderer* wrap = app->Renderer();
    const int bbW = wrap->screenWidth;
    const int bbH = wrap->screenHeight;

    s.TimelineDC() = CreateCompatibleDC(hdc);               // 0x466D97
    HBITMAP ruler = CreateCompatibleBitmap(hdc, bbW, 0x32); // 0x466DB3
    s.TimelineBitmap() = ruler;
    SelectObject(s.TimelineDC(), ruler);

    s.PanelDC() = CreateCompatibleDC(hdc);                 // 0x466DD0
    HBITMAP panel = CreateCompatibleBitmap(hdc, bbW, bbH); // 0x466DF1
    s.PanelBitmap() = panel;
    HBITMAP spare = CreateCompatibleBitmap(hdc, bbW, bbH); // 0x466E12
    s.PanelSpareBitmap() = spare;
    SelectObject(s.PanelDC(), panel);

    s.CurveDC() = CreateCompatibleDC(hdc);                   // 0x466E2F
    HBITMAP curve = CreateCompatibleBitmap(hdc, 0x80, 0x80); // 0x466E46
    s.CurveBitmap() = curve;
    SelectObject(s.CurveDC(), curve);

    HDC scratch = CreateCompatibleDC(hdc); // 0x466E5D
    SelectObject(scratch, spare);

    // ---- 0x466E7A..0x467023: pre-draw fills on the spare bitmap -------------
    // Pen+brush of the same COLORREF, Rectangle, restore, delete - three
    // passes with the color.txt palette fields.
    auto fill = [&](COLORREF color, void (*rects)(HDC, int, int)) {
        HPEN pen = CreatePen(PS_SOLID, 1, color);        // 0x466E7A
        HBRUSH brush = CreateSolidBrush(color);          // 0x466E8B
        HGDIOBJ oldPen = SelectObject(scratch, pen);     // 0x466E97
        HGDIOBJ oldBrush = SelectObject(scratch, brush); // 0x466EA3
        rects(scratch, bbW, bbH);
        SelectObject(scratch, oldBrush); // 0x466ECE
        SelectObject(scratch, oldPen);   // 0x466ED6
        DeleteObject(pen);               // 0x466EE1
        DeleteObject(brush);             // 0x466EEC
    };
    fill(s.ThemeColor(UiThemeColor::TimelineBase), // base fill
         [](HDC dc, int w, int h) { Rectangle(dc, 0, 0, w, h); });
    fill(s.ThemeColor(UiThemeColor::HeaderBand), // header bands
         [](HDC dc, int w, int h) {
             Rectangle(dc, 1, 0xF, 0x5A, h); // 0x466F3C
             Rectangle(dc, 0x5A, 1, w, 0xF); // 0x466F56
         });
    fill(s.ThemeColor(UiThemeColor::RowBand), // row bands
         [](HDC dc, int w, int h) {
             for (int i = 0xF; i + 0xF < h; i += 0x1C) // 0x466FE4
                 Rectangle(dc, 1, i, 0x5A, i + 0xF);
         });

    // ---- 0x467023..0x46725F: remaining palette fills and grid lines -------
    fill(ColorLerp(s.ThemeColor(UiThemeColor::ControlLight),
                   s.ThemeColor(UiThemeColor::ControlDark), 0.08281938f),
         [](HDC dc, int, int) { Rectangle(dc, 0, 0, 0x59, 0xF); });
    fill(s.ThemeColor(UiThemeColor::TimelineRows), [](HDC dc, int w, int h) {
        for (int y = 0xF; y < h; y += 0x1C)
            Rectangle(dc, 0x5B, y, w, y + 0xF);
    });

    auto drawRows = [&](COLORREF color, int x0, int x1) {
        HPEN rowPen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ previous = SelectObject(scratch, rowPen);
        for (int y = 0xF; y < bbH; y += 0xE) {
            MoveToEx(scratch, x0, y, nullptr);
            LineTo(scratch, x1, y);
        }
        SelectObject(scratch, previous);
        DeleteObject(rowPen);
    };
    drawRows(s.ThemeColor(UiThemeColor::LabelGrid), 1, 0x5A);
    drawRows(s.ThemeColor(UiThemeColor::TimelineGrid), 0x5B, bbW);

    // The original releases the window DC and destroys the temporary DC,
    // then copies the completed spare bitmap into the persistent panel DC.
    ReleaseDC(hwnd, hdc);                     // 0x46726B
    DeleteDC(scratch);                        // 0x467272
    HDC copyDc = CreateCompatibleDC(nullptr); // 0x46727A
    SelectObject(copyDc, spare);
    BitBlt(s.PanelDC(), 0, 0, bbW, bbH, copyDc, 0, 0, SRCCOPY); // 0x4672B5
    DeleteDC(copyDc);

    // 0x4672C2..0x4672E4: initialize the ruler/curve caches, then bind the
    // ruler HDC to the wave/timeline object while DirectSound is initialized.
    ClearTimelineAndCurveDCs(app); // 0x40AE00
    s.DirectSoundAvailable() = static_cast<std::uint8_t>(
        InitTimelineAudio(app, hwnd, s.TimelineDC(), app->EnglishUI() != 0));

    SetCurrentDirectoryW(app->ExeDir()); // 0x4672F1
    s.state.bmpRes101 =
        LoadBitmapA(static_cast<HMODULE>(s.HInstance()), MAKEINTRESOURCEA(0x65)); // 0x467303
    s.state.bmpRes119 =
        LoadBitmapA(static_cast<HMODULE>(s.HInstance()), MAKEINTRESOURCEA(0x77)); // 0x467311

    // 0x467320: DirectShowInit (sub_408EF0, this = the 0x6C AVI-codec
    // subsystem object at app+0xA06C0 which only supplies the failure
    // MessageBox parent).  Failure terminates CreateUIControls.
    if (CoInitialize(nullptr) < 0) {
        MessageBoxA(hwnd, "Failed CoInitialize!", "DirectShowInit", 0);
        return false;
    }

    // 0x46732D..0x467336: the recording step-flag byte.  operator new(1)
    // with NO initialization in the original; the wait loops at 0x46E8F5 /
    // 0x46F08C and FinishAviRecord:0x464A28 all dereference app+0x9EDD4
    // unconditionally, so this allocation is what keeps those paths alive.
    // The fresh-heap byte reads as 0 in practice; we zero it explicitly so
    // the port stays deterministic.
    s.RecordingCompletionFlag() = new unsigned char(0);

    // 0x46733C..0x467375: material used for projected accessory shadows.
    // The original memsets the whole D3DMATERIAL9 at 0xA0CE0 and then
    // seeds a half-transparent diffuse term, white ambient term, and a
    // specular alpha; only those promoted fields are ever read back
    // (ProjectedShadowMaterial() rebuilds the struct from them), so the
    // setters alone are observably identical to the memset + setters.
    s.SetProjectedShadowAmbient(1.0f);
    s.ProjectedShadowDiffuseAlpha() = 0.5f;
    s.ProjectedShadowSpecularAlpha() = 1.0f;

    // 0x46737B: colored world axes and 5-unit ground grid.
    if (!MakeLineGeometry(app))
        return false;

    // 0x467384..0x4674A6: dynamic text/sprite/line batches plus the two
    // post-process quads. RefreshMainWindowViewport rewrites the latter after every resize.
    IDirect3DDevice9* device = wrap->device;
    if (device != nullptr) {
        if (FAILED(device->CreateVertexBuffer(
                kTextOverlayVertexCapacity * sizeof(TexturedScreenVertex), D3DUSAGE_WRITEONLY,
                kTexturedScreenVertexFvf, D3DPOOL_MANAGED, &s.OverlayVertices(), nullptr)))
            return false;
        device->CreateVertexBuffer(kBackgroundQuadBytes, D3DUSAGE_WRITEONLY,
                                   kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                   &s.LeftViewportVertices(), nullptr);
        device->CreateVertexBuffer(kBackgroundQuadBytes, D3DUSAGE_WRITEONLY,
                                   kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                   &s.RightViewportVertices(), nullptr);
        // PMM picture/AVI background refreshes write one 6-vertex textured
        // quad per layer.  These are distinct scene resources (and are
        // released independently at shutdown), not aliases of the viewport
        // post-process quads.
        device->CreateVertexBuffer(kBackgroundQuadBytes, D3DUSAGE_WRITEONLY,
                                   kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                   &s.AviOverlayVertices(), nullptr);
        device->CreateVertexBuffer(kBackgroundQuadBytes, D3DUSAGE_WRITEONLY,
                                   kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                   &s.PictureOverlayVertices(), nullptr);
        device->CreateVertexBuffer(kSpriteOverlayVertexCapacity * sizeof(TexturedScreenVertex),
                                   D3DUSAGE_WRITEONLY, kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                   &s.SpriteOverlayVertices(), nullptr);
        device->CreateVertexBuffer(kLineOverlayVertexCapacity * sizeof(ColoredScreenVertex),
                                   D3DUSAGE_WRITEONLY, kColoredScreenVertexFvf, D3DPOOL_MANAGED,
                                   &wrap->lineVertexBuffer, nullptr);

        // 0x467509 and 0x46759E: the HUD sprite sheet and accessory helper
        // texture are PNG resources 102 and 114 in the original image.
        // PNG 102 load failure gets the original's error box (x64
        // 0x7FF7CB4328EE, caption "InitFont"): EN "cannot load splite.tga"
        // / JP "Failed to load splite.tga" (0x7FF7CB54B4C0/B4D9). The original
        // asset was named splite.tga; preserve the original message even
        // though the embedded sheet is maintained as hud_sprites.png.
        // Non-fatal: the original shows the box and falls through to
        // PNG 114 (only 102 has a failure box).
        if (!LoadPngTexture(app, device, image::kHudSprites, &s.OverlayTexture())) {
            static const char kJpHudSheetLoadFailed[] =
                "splite.tga\x93\xC7\x8D\x9E\x8E\xB8\x94\x73";
            MessageBoxA(hwnd,
                        app->EnglishUI() != 0 ? "cannot load splite.tga" : kJpHudSheetLoadFailed,
                        "InitFont", MB_OK);
        }
        LoadPngTexture(app, device, image::kAccessoryHelper, &s.ProjectedShadowRestoreTexture());
    }

    // ---- 0x467637..0x467674: default-checked menu items -------------------
    HMENU menu = GetMenu(hwnd);
    CheckMenuItem(menu, MDS_CMD_TOGGLE_COORDINATE_AXIS, MF_CHECKED);
    CheckMenuItem(menu, MDS_CMD_TOGGLE_GROUND_SHADOW, MF_CHECKED);
    CheckMenuItem(menu, MDS_CMD_FPS_60, MF_CHECKED);
    CheckMenuItem(menu, MDS_CMD_SCREEN_CAPTURE_OFF, MF_CHECKED);
    CheckMenuItem(menu, MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY, MF_CHECKED);

    // ---- 0x46767B..0x467747: the four global track key arrays -------------
    // 10000-key arrays consumed by PlaybackPoseAdvance (0x4175A0): camera
    // 84B rec, light 40B rec, self-shadow 24B rec, physics-gravity 36B rec,
    // plus 255 accessory slots of 10000 x 60B records (the 256th consumer
    // slot at app+0x780 is deliberately left unallocated by the original -
    // it reads the first model-slot pointer there, gated off by its zero
    // active flag in a fresh app).
    auto* cam = NewZeroedTrack<mdl::CameraKey>();
    s.CameraKeys() = cam;
    auto* trkLight = NewZeroedTrack<mdl::LightKey>();
    s.LightKeys() = trkLight;
    auto* trkShadow = NewZeroedTrack<mdl::SelfShadowKey>();
    s.ShadowKeys() = trkShadow;
    auto* trkPhys = NewZeroedTrack<mdl::GravityKey>();
    s.GravityKeys() = trkPhys;
    for (int i = 0; i < mdl::kAccessorySlotCount; ++i) { // 0x467705
        auto* acc = NewZeroedTrack<mdl::AccessoryKey>();
        s.AccessoryKeys(i) = acc;
        acc[0].visible = 1;
        acc[0].parentModel = -1;
        acc[0].scale = 1.0f;
        acc[0].opacity = 1.0f;
        s.AccessorySlot(i) = nullptr;
    }
    TraceInitialAccessoryTrack(s);
#ifdef MIKUDANCESTUDIO_DIAG
    if (std::getenv("MIKUDANCESTUDIO_PMM_GUARD_ACCESSORY_TRACKS") != nullptr) {
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        const std::uintptr_t address =
            reinterpret_cast<std::uintptr_t>(&s.AccessoryKeyTracks()[174]);
        const std::uintptr_t page =
            address & ~(static_cast<std::uintptr_t>(systemInfo.dwPageSize) - 1);
        DWORD previousProtection = 0;
        VirtualProtect(reinterpret_cast<void*>(page), systemInfo.dwPageSize, PAGE_READONLY,
                       &previousProtection);
    }
#endif

    // ---- 0x467749..0x46787B: camera work record + per-key defaults --------
    cam[0].distance = -45.0f; // flt_52A1E8
    cam[0].eye[1] = 10.0f;    // flt_52A1E4
    cam[0].perspective = 0;
    cam[0].fov = 30;
    for (std::size_t k = 0; k < mdl::kTimelineKeyCapacity; ++k) { // 0x467780
        cam[k].parentModel = -1;
        for (int j = 0; j < 6; ++j) {
            cam[k].interpolation[0][j] = 20; // x1 column, 6 channels
            cam[k].interpolation[1][j] = 20; // y1 column
            cam[k].interpolation[2][j] = 107;
            cam[k].interpolation[3][j] = 107;
        }
    }

    // ---- 0x46787E..0x467967: light cluster + device light 0 ---------------
    s.CameraFov() = 30.0f;
    s.ResetSceneLight();
    float* direction = s.LightDirection();
    D3DLIGHT9& light = s.SceneLight();
    D3DRenderer* w = app->Renderer();
    IDirect3DDevice9* dev = w->device; // +0x1D4E0
    dev->SetLight(0, &light);
    dev->LightEnable(0, TRUE); // vtable 0xD4

    // ---- 0x467984..0x467A2E: track work records ---------------------------
    trkLight[0].color[0] = 0.602f;
    trkLight[0].color[1] = 0.602f;
    trkLight[0].color[2] = 0.602f;
    trkLight[0].direction[0] = -0.5f;
    trkLight[0].direction[1] = -1.0f;
    trkLight[0].direction[2] = 0.5f;
    trkShadow[0].mode = w->postProcessEnabled != 0 ? 1 : 0;
    trkShadow[0].distance = 0.01125f; // flt_52A1D8
    trkPhys[0].noise = 10;
    trkPhys[0].acceleration = 9.8000002f; // flt_52A1DC
    trkPhys[0].direction[1] = -1.0f;

    const int sidebar = app->SidebarWidth();                    // 657096
    HINSTANCE hInst = static_cast<HINSTANCE>(app->HInstance()); // this+0

    // The font object is created before the first UI child in the original.
    // Every child then receives its font/range/subclass messages immediately
    // at its own creation site.
    CreateUiFont(app, hwnd);
    const auto initializeTrackRange = [](HWND control, int id) {
        int lo = 0, hi = 0;
        switch (id) {
        case panel::kFovSlider:
            lo = 1;
            hi = 125;
            break;
        case panel::kLightColorSliderR:
        case panel::kLightColorSliderG:
        case panel::kLightColorSliderB:
            lo = 0;
            hi = 255;
            break;
        case panel::kLightDirSliderX:
        case panel::kLightDirSliderY:
        case panel::kLightDirSliderZ:
            lo = -100;
            hi = 100;
            break;
        case panel::kMorphSlider0:
        case panel::kMorphSlider1:
        case panel::kMorphSlider2:
        case panel::kMorphSlider3:
        case panel::kFrameVolumeSlider:
            lo = 0;
            hi = 100;
            break;
        case panel::kSelfShadowRangeSlider:
            lo = 0;
            hi = 9999;
            break;
        default:
            return;
        }
        SendMessageA(control, TBM_SETRANGEMIN, 0, lo);
        SendMessageA(control, TBM_SETRANGEMAX, 0, hi);

        // The tick frequency is part of the original creation sequence, not
        // cosmetic post-processing.  Without it, comctl32 uses its default
        // one-unit interval; the 201/10000 densely packed tick marks then
        // merge into two horizontal rules.  Original sites:
        //   Light X/Y/Z  0x468B34 / 0x468BF9 / 0x468CBE
        //   Shadow range 0x46A2F2
        if (id == panel::kLightDirSliderX || id == panel::kLightDirSliderY ||
            id == panel::kLightDirSliderZ || id == panel::kSelfShadowRangeSlider)
            SendMessageA(control, TBM_SETTICFREQ, 1000, 0);
    };

    for (const ui::ControlSpec& c : ui::kControls) {
        // Original mixes CreateWindowExW/A per control; the table carries the
        // exact wire bytes for both variants (wide flag from the original
        // call site), so ANSI captions reach the A entry points verbatim.
        HWND control = c.wide
                           ? CreateWindowExW(c.ex, c.wcls, c.wtext, c.style, EvalPos(c.x, sidebar),
                                             EvalPos(c.y, sidebar), c.w, c.h, hwnd,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.id)),
                                             hInst, nullptr)
                           : CreateWindowExA(c.ex, c.acls, c.atext, c.style, EvalPos(c.x, sidebar),
                                             EvalPos(c.y, sidebar), c.w, c.h, hwnd,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.id)),
                                             hInst, nullptr);
        if (control == nullptr)
            return false; // original exits the process from WM_CREATE
        ApplyUiFontToControl(app, control, c.id);
        initializeTrackRange(control, c.id);
        InstallControlSubclass(app, control, c.id);
    }

    // 0x46A8ED..0x46A8F7: the current-frame edit is initialized only
    // after its numeric-edit subclass has been installed.  Leaving the
    // CreateWindow caption empty made the top overlay differ by one glyph.
    SetWindowTextA(GetDlgItem(hwnd, panel::kGotoFrameEdit), "0");

    // UI-init tail (original 0x004675FB / 0x00467602, adjacent calls):
    // toon gradient textures, then the in-scene HUD font atlas.  Both are
    // device-guarded no-ops if the device could not be created.
    InitToonTextures(app);
    InitSceneFontTexture(app);
    if (!InitAxisMesh(app))
        return false;

    // ---- 0x46A99F..0x46AC5E: edit/trackbar mirrors of the work records ---
    char text[0x100];
    auto setEdit = [&](int id, const char* t) {
        SendMessageA(GetDlgItem(hwnd, id), EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(t));
    };
    auto setTrack = [&](int id, int pos) {
        SendMessageA(GetDlgItem(hwnd, id), TBM_SETPOS, 1, pos);
    };
    sprintf_s(text, 0x100, "%3d", static_cast<int>(static_cast<double>(s.LightColor()[0]) * 256.0));
    setEdit(panel::kLightColorEditR, text);
    sprintf_s(text, 0x100, "%3d", static_cast<int>(static_cast<double>(s.LightColor()[1]) * 256.0));
    setEdit(panel::kLightColorEditG, text);
    sprintf_s(text, 0x100, "%3d", static_cast<int>(static_cast<double>(s.LightColor()[2]) * 256.0));
    setEdit(panel::kLightColorEditB, text);
    sprintf_s(text, 0x100, "%+3.1f", direction[0]);
    setEdit(panel::kLightDirEditX, text);
    sprintf_s(text, 0x100, "%+3.1f", direction[1]);
    setEdit(panel::kLightDirEditY, text);
    sprintf_s(text, 0x100, "%+3.1f", direction[2]);
    setEdit(panel::kLightDirEditZ, text);
    sprintf_s(text, 0x100, "%3d", static_cast<int>(s.CameraFov()));
    setEdit(panel::kFovEdit, text);
    setTrack(panel::kFovSlider, static_cast<int>(s.CameraFov()));
    setEdit(panel::kCurrentFrameEdit, "0");
    setTrack(panel::kLightColorSliderR,
             static_cast<int>(static_cast<double>(s.LightColor()[0]) * 256.0));
    setTrack(panel::kLightColorSliderG,
             static_cast<int>(static_cast<double>(s.LightColor()[1]) * 256.0));
    setTrack(panel::kLightColorSliderB,
             static_cast<int>(static_cast<double>(s.LightColor()[2]) * 256.0));
    setTrack(panel::kLightDirSliderX, static_cast<int>(static_cast<double>(direction[0]) * 100.0));
    setTrack(panel::kLightDirSliderY, static_cast<int>(static_cast<double>(direction[1]) * 100.0));
    setTrack(panel::kLightDirSliderZ, static_cast<int>(static_cast<double>(direction[2]) * 100.0));

    // ---- 0x46AC60..0x46AF22: initial command availability/state ----------
    // MF_GRAYED is passed by command ID in the original.  These commands
    // become available later when a model, accessory, motion or media file
    // establishes the corresponding editing context.
    static constexpr UINT kInitiallyDisabled[] = {
        MDS_CMD_EDGE_THICKNESS,
        MDS_CMD_SELECT_ALL_BONE_FRAMES,
        MDS_CMD_SELECT_ALL_MORPH_FRAMES,
        MDS_CMD_SELECT_ALL_DISPLAY_FRAMES,
        MDS_CMD_LOAD_POSE,
        MDS_CMD_SAVE_POSE,
        MDS_CMD_CENTER_POSITION_BIAS,
        MDS_CMD_DELETE_UNUSED_FRAMES,
        MDS_CMD_ACCESSORY_SETTINGS,
        MDS_CMD_PASTE_AT_DIFFERENT_FRAME,
        MDS_CMD_MULTIPLY_BONE_FRAMES,
        MDS_CMD_MULTIPLY_MORPH_FRAMES,
        MDS_CMD_MODEL_DRAW_ORDER,
        MDS_CMD_MODEL_CALCULATION_ORDER,
        MDS_CMD_TOGGLE_MOTION_CAPTURE,
    };
    for (UINT id : kInitiallyDisabled)
        EnableMenuItem(menu, id, MF_BYCOMMAND | MF_GRAYED);
    for (UINT id = MDS_CMD_LOAD_LIP_SYNC; id <= MDS_CMD_DELETE_EYEBROW_FRAMES; ++id)
        EnableMenuItem(menu, id, MF_BYCOMMAND | MF_GRAYED);
    for (UINT id = MDS_CMD_SELECT_PHYSICS_BONES; id <= MDS_CMD_TOGGLE_FRAME_PHYSICS; ++id)
        EnableMenuItem(menu, id, MF_BYCOMMAND | MF_GRAYED);

    if (w->multisampleAvailable == 0)
        EnableMenuItem(menu, MDS_CMD_TOGGLE_ANTIALIASING, MF_BYCOMMAND | MF_GRAYED);
    else
        CheckMenuItem(menu, MDS_CMD_TOGGLE_ANTIALIASING, MF_BYCOMMAND | MF_CHECKED);

    if (w->postProcessEnabled == 0) {
        EnableMenuItem(menu, MDS_CMD_TOGGLE_SELF_SHADOW, MF_BYCOMMAND | MF_GRAYED);
        s.SelfShadowMode() = 0;
        s.state.selfShadowEnabled = 0;
    } else {
        CheckMenuItem(menu, MDS_CMD_TOGGLE_SELF_SHADOW, MF_BYCOMMAND | MF_CHECKED);
        s.SelfShadowMode() = 1;
        s.state.selfShadowEnabled = 1;
    }

    if (s.FrameVolumeControlEnabled() != 0) {
        CheckMenuItem(menu, MDS_CMD_TOGGLE_LOW_POWER, MF_BYCOMMAND | MF_CHECKED);
        SendMessageA(GetDlgItem(hwnd, panel::kFrameVolumeCheckbox), BM_SETCHECK, BST_CHECKED, 0);
    }
    static constexpr UINT kInitiallyChecked[] = {
        MDS_CMD_TOGGLE_PHYSICS_FLOOR,    MDS_CMD_TOGGLE_CAPTURE_MIRROR,
        MDS_CMD_TOGGLE_CAPTURE_SKELETON, MDS_CMD_TOGGLE_MIPMAP,
        MDS_CMD_PHYSICS_ON_OFF,
    };
    for (UINT id : kInitiallyChecked)
        CheckMenuItem(menu, id, MF_BYCOMMAND | MF_CHECKED);

    setTrack(panel::kFrameVolumeSlider, 100 - s.FrameNormalization());
    SendMessageA(GetDlgItem(hwnd, panel::kCoordAxisCheckbox), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kPhysicsCheckbox), BM_SETCHECK, BST_CHECKED, 0);

    // ---- 0x46AF24..0x46AFCE: submenu state and dynamic full-screen label --
    MENUITEMINFOA mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    mii.fState = MFS_DISABLED | MFS_GRAYED;
    SetMenuItemInfoA(GetSubMenu(menu, 7), 2, TRUE, &mii);
    DrawMenuBar(hwnd);

    static const char kJpFullScreen[] = "\x83\x74\x83\x8B\x83\x58\x83\x4E\x83\x8A\x81\x5B"
                                        "\x83\x93\x95\x5C\x8E\xA6(Alt+Enter)";
    static const char kJpNvidia3D[] = "NVIDIA 3D Vision\x95\x5C\x8E\xA6(Alt+Enter)";
    const bool stereoCapable = w->stereoEnabled != 0;
    const char* fullScreenText;
    if (app->EnglishUI() != 0)
        fullScreenText = stereoCapable ? "NDIVIA 3D Vision(Alt+Enter)" : "full screen(Alt+Enter)";
    else
        fullScreenText = stereoCapable ? kJpNvidia3D : kJpFullScreen;
    mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = const_cast<char*>(fullScreenText);
    SetMenuItemInfoA(GetSubMenu(menu, 2), 0x1D, TRUE, &mii);

    // ---- 0x46AFD4..0x46B06E: self-shadow mode/range mirrors ---------------
    SendMessageA(GetDlgItem(hwnd, panel::kEditMode1Checkbox), BM_SETCHECK, BST_CHECKED, 0);
    const double rangeValue = 10000.0 - static_cast<double>(s.state.selfShadowDistance) * 100000.0;
    const int rangePos = static_cast<int>(rangeValue + 0.5);
    setTrack(panel::kSelfShadowRangeSlider, rangePos);
    sprintf_s(text, 0x100, "%d", rangePos);
    SetWindowTextA(GetDlgItem(hwnd, panel::kSelfShadowRangeEdit), text);
    return true;
}

// ---------------------------------------------------------------------------
// x86 0x00410040 / x64 0x7FF7CB479B00 - RefillBoneRegisterCombo.
// Clear the combo, append registerable bones in model order using the
// selected UI language, then invalidate selection readouts. Negative or
// empty slots leave the combo empty. ModelRecord/BoneRecord provide the
// native layout (boneCount: x86 +11652 / x64 +12560).
// ---------------------------------------------------------------------------
void RefillBoneRegisterCombo(MMDApp* app, int slot) {
    HWND combo = GetDlgItem(static_cast<HWND>(app->Hwnd()), panel::kBoneRegisterCombo);
    SendMessageA(combo, CB_RESETCONTENT, 0, 0); // 0x41006D
    if (slot >= 0) {
        const mdl::ModelRecord* model = app->ModelSlot(slot);
        if (model != nullptr) {
            const auto* record = model;
            const auto boneCount = static_cast<std::int32_t>(record->boneCount);
            const mdl::BoneRecord* bones = record->boneTable;
            for (std::int32_t i = 0; i < boneCount; ++i) { // 0x4100B1
                const auto& bone = bones[i];
                // The original compares the type byte as signed (x64 setl).
                const auto type = static_cast<std::int8_t>(bone.type);
                if (type < static_cast<std::int8_t>(mdl::BoneType::InertTip) ||
                    type == static_cast<std::int8_t>(mdl::BoneType::FixedAxis)) {
                    const char* name = app->EnglishUI() != 0 ? bone.nameEn : bone.name;
                    SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                }
            }
        }
    }
    InvalidateSelectionReadouts(app); // 0x410132
}

} // namespace mikudancestudio
