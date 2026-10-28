// ===========================================================================
// VA 0x0042AE80 - InitSceneFontTexture  (original: sub_42AE80, 0x12C0 bytes)
// ===========================================================================
// Builds the in-scene HUD font atlas: a 512x512 A8R8G8B8 texture whose DC is
// filled with the overlay character set (verified draw list):
//   row 0   : axes glyphs + "Model:" "Bone:"
//   row 32  : "camera/light/accessory"
//   row 64  : "center" "X:" "angle" "X:" "distance" "Y:" "Z:"
//   row 96  : digits, "X:", ".", "X:"
//   row 128 : "global" "local" "Recording(Esc:Stop)" "Playing"
//   row 192 : digits "fps" "." "-"
//   row 224 : "1fps or less" "1fps "
//   row 256 : digits "frame ("
// then re-locks the destination texture and converts pixels to white with
// alpha = source blue channel.
//
// Original quirk (not replicated): the pixel loop writes past the declared
// stack frame via the return-address slot - a Ghidra artifact of a 1MB stack
// buffer.  The port uses an explicit heap buffer.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {
struct FontAtlasPixels {
    std::vector<std::uint8_t> rgba;
    std::uint64_t revision = 0;
};
std::unordered_map<MMDApp*, FontAtlasPixels> g_fontAtlasPixels;
std::uint64_t g_nextFontRevision = 0;

struct Glyph {
    const char* s;
    short x, y;
};

// exact draw list from the decompilation (size 30, color 255,255,255, normal);
// SJIS bytes decoded from the original .rdata (row 0 spells
// camera/lighting/accessory glyph-by-glyph for per-char positioning)
constexpr Glyph kAtlas[] = {
    {"\xb6", 0, 0},
    {"\xd2", 16, 0},
    {"\xd7", 32, 0},
    {"\xa5", 48, 0},
    {"\x8f\xc6\x96\xbe", 64, 0}, // lighting
    {"\xa5", 128, 0},
    {"\xb1", 144, 0},
    {"\xb8", 160, 0},
    {"\xbe", 176, 0},
    {"\xbb", 192, 0},
    {"\xd8", 208, 0},
    {"M", 256, 0},
    {"o", 272, 0},
    {"d", 288, 0},
    {"e", 304, 0},
    {"l", 320, 0},
    {":", 336, 0},
    {"B", 352, 0},
    {"o", 368, 0},
    {"n", 384, 0},
    {"e", 400, 0},
    {":", 416, 0},
    {"c", 0, 32},
    {"a", 16, 32},
    {"m", 32, 32},
    {"e", 48, 32},
    {"r", 64, 32},
    {"a", 80, 32},
    {"/", 96, 32},
    {"l", 112, 32},
    {"i", 128, 32},
    {"g", 144, 32},
    {"h", 160, 32},
    {"t", 176, 32},
    {"/", 192, 32},
    {"a", 208, 32},
    {"c", 224, 32},
    {"c", 240, 32},
    {"e", 256, 32},
    {"s", 272, 32},
    {"s", 288, 32},
    {"o", 304, 32},
    {"r", 320, 32},
    {"y", 336, 32},
    {"c", 0, 64},
    {"e", 16, 64},
    {"n", 32, 64},
    {"t", 48, 64},
    {"e", 64, 64},
    {"r", 80, 64},
    {"X", 112, 64},
    {":", 128, 64},
    {"a", 160, 64},
    {"n", 176, 64},
    {"g", 192, 64},
    {"l", 208, 64},
    {"e", 224, 64},
    {"X", 256, 64},
    {":", 272, 64},
    {"d", 288, 64},
    {"i", 304, 64},
    {"s", 320, 64},
    {"t", 336, 64},
    {"a", 352, 64},
    {"n", 368, 64},
    {"c", 384, 64},
    {"e", 400, 64},
    {"Y", 416, 64},
    {":", 432, 64},
    {"Z", 448, 64},
    {":", 464, 64},
    {"0", 0, 96},
    {"1", 16, 96},
    {"2", 32, 96},
    {"3", 48, 96},
    {"4", 64, 96},
    {"5", 80, 96},
    {"6", 96, 96},
    {"7", 112, 96},
    {"8", 128, 96},
    {"9", 144, 96},
    {"\x92\x86\x90\x53", 160, 96}, // center
    {"X", 240, 96},
    {":", 256, 96},
    {"\x8ap\x93x", 288, 96}, // view angle
    {"X", 368, 96},
    {":", 384, 96},
    {"\x8b\x97\x97\xa3", 416, 96}, // distance
    {"global", 0, 128},
    {"local", 96, 128},
    {"Recording(Esc:Stop)", 176, 128},
    {"\x98^\x89\xe6\x92\x86(Esc:\x92\x86\x92"
     "\xf)",
     0, 160}, // recording (Esc: interrupt)
    {"P", 256, 160},
    {"l", 272, 160},
    {"a", 288, 160},
    {"y", 304, 160},
    {"i", 320, 160},
    {"n", 336, 160},
    {"g", 352, 160},
    {"\x8d\xc4\x90\xb6\x92\x86", 384, 160}, // playing
    {"0", 0, 192},
    {"1", 16, 192},
    {"2", 32, 192},
    {"3", 48, 192},
    {"4", 64, 192},
    {"5", 80, 192},
    {"6", 96, 192},
    {"7", 112, 192},
    {"8", 128, 192},
    {"9", 144, 192},
    {"fps", 160, 192},
    {".", 208, 192},
    {"-", 224, 192},
    {"1fps or less", 0, 224},
    {"1fps \x96\xA2\x96\xBA", 208, 224}, // JP "less than" (x64 0x7FF7CB54B430)

    {"0", 0, 256},
    {"1", 16, 256},
    {"2", 32, 256},
    {"3", 48, 256},
    {"4", 64, 256},
    {"5", 80, 256},
    {"6", 96, 256},
    {"7", 112, 256},
    {"8", 128, 256},
    {"9", 144, 256},
    {"frame (", 160, 256},
    {":", 288, 256},
    {":", 336, 256},
    {")", 384, 256},
};

IDirect3DDevice9* DeviceOf(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    if (sub == nullptr)
        return nullptr;
    return sub->device;
}

} // namespace

bool InitSceneFontTexture(MMDApp* app) {
    g_fontAtlasPixels.erase(app);
    auto& s = *app;
    IDirect3DDevice9* device = DeviceOf(app);
    if (device == nullptr)
        return false; // original returns 0 when surface creation fails

    // The original creates a SYSTEMMEM X8R8G8B8 texture, obtains level 0,
    // and lets GDI draw into that surface.  Keeping the texture alive is
    // essential: its pixels are the source of the conversion pass below.
    IDirect3DTexture9* sourceTex = nullptr;
    if (FAILED(device->CreateTexture(512, 512, 1, 0, D3DFMT_X8R8G8B8 /*22*/,
                                     D3DPOOL_SYSTEMMEM /*2*/, &sourceTex, nullptr)))
        return false;

    IDirect3DSurface9* surface = nullptr;
    if (FAILED(sourceTex->GetSurfaceLevel(0, &surface))) {
        sourceTex->Release();
        return false;
    }

    HDC hdc = nullptr;
    if (FAILED(surface->GetDC(&hdc))) {
        surface->Release();
        sourceTex->Release();
        return false;
    }

    HPEN pen = CreatePen(PS_SOLID /*0*/, 1, 0);
    HBRUSH brush = CreateSolidBrush(0);
    HGDIOBJ prevPen = SelectObject(hdc, pen);
    HGDIOBJ prevBrush = SelectObject(hdc, brush);
    Rectangle(hdc, 0, 0, 512, 512);
    SelectObject(hdc, prevBrush);
    DeleteObject(brush);
    SelectObject(hdc, prevPen);
    DeleteObject(pen);

    for (const Glyph& g : kAtlas)
        DrawGlyph(app, g.s, hdc, 30, g.x, g.y, 0, 0, 255, 1);

    surface->ReleaseDC(hdc);
    DeleteDC(hdc);
    surface->Release();

    // destination texture slot (this+650784), recreated as SYSTEMMEM
    if (s.state.sceneFontTexture != nullptr) {
        reinterpret_cast<IDirect3DTexture9*>(s.state.sceneFontTexture)->Release();
        s.state.sceneFontTexture = nullptr;
    }
    IDirect3DTexture9* fontTex = nullptr;
    if (FAILED(device->CreateTexture(512, 512, 1, 0, D3DFMT_A8R8G8B8 /*21*/, D3DPOOL_MANAGED /*1*/,
                                     &fontTex, nullptr))) {
        sourceTex->Release();
        return false;
    }
    s.state.sceneFontTexture = fontTex;

    // 0x42C0B0..0x42C124: destination RGB is white and alpha comes from the
    // pure-blue GDI glyph coverage in byte 0 of X8R8G8B8 pixels.
    D3DLOCKED_RECT srcRect{};
    D3DLOCKED_RECT dstRect{};
    const HRESULT srcHr = sourceTex->LockRect(0, &srcRect, nullptr, D3DLOCK_READONLY);
    const HRESULT dstHr = fontTex->LockRect(0, &dstRect, nullptr, 0);
    if (SUCCEEDED(srcHr) && SUCCEEDED(dstHr)) {
        FontAtlasPixels cpuAtlas;
        cpuAtlas.rgba.resize(512u * 512u * 4u);
        for (int row = 0; row < 512; ++row) {
            auto* src = static_cast<const unsigned char*>(srcRect.pBits) + row * srcRect.Pitch;
            auto* dst = static_cast<unsigned char*>(dstRect.pBits) + row * dstRect.Pitch;
            for (int col = 0; col < 512; ++col) {
                const unsigned char alpha = src[col * 4];
                unsigned char* out = dst + col * 4;
                out[0] = out[1] = out[2] = 0xFF;
                out[3] = alpha;
                auto* portable =
                    cpuAtlas.rgba.data() + (static_cast<std::size_t>(row) * 512u + col) * 4u;
                portable[0] = portable[1] = portable[2] = 0xFF;
                portable[3] = alpha;
            }
        }
        cpuAtlas.revision = ++g_nextFontRevision;
        g_fontAtlasPixels[app] = std::move(cpuAtlas);
    }
    if (SUCCEEDED(dstHr))
        fontTex->UnlockRect(0);
    if (SUCCEEDED(srcHr))
        sourceTex->UnlockRect(0);
    sourceTex->Release();
    return true;
}

FontAtlasRgbaView GetSceneFontRgba(MMDApp* app) {
    const auto found = g_fontAtlasPixels.find(app);
    if (found == g_fontAtlasPixels.end())
        return {};
    return {found->second.rgba.data(), found->second.rgba.size(), found->second.revision};
}

void ForgetSceneFontRgba(MMDApp* app) {
    g_fontAtlasPixels.erase(app);
}

} // namespace mikudancestudio
