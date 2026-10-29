// ===========================================================================
// VA 0x00423420 / 0x004757C3 - text and selection-line overlay producers
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "vb_dump.hpp"

namespace mikudancestudio {
namespace {

struct TextVertex {
    float x, y, z;
    std::uint32_t rhw;
    D3DCOLOR color;
    float u, v;
};
static_assert(sizeof(TextVertex) == 28);

struct LineVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};
static_assert(sizeof(LineVertex) == 20);

void PutQuad(TextVertex*& out, float left, float top, float right, float bottom, float u0, float v0,
             float u1, float v1, D3DCOLOR color) {
    const TextVertex vertices[6] = {
        {right, top, 0.0f, 0x3F800000u, color, u1, v0},
        {right, bottom, 0.0f, 0x3F800000u, color, u1, v1},
        {left, top, 0.0f, 0x3F800000u, color, u0, v0},
        {left, top, 0.0f, 0x3F800000u, color, u0, v0},
        {right, bottom, 0.0f, 0x3F800000u, color, u1, v1},
        {left, bottom, 0.0f, 0x3F800000u, color, u0, v1},
    };
    for (const TextVertex& vertex : vertices)
        *out++ = vertex;
}

float OverlayScale(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    return sub != nullptr ? sub->viewScale : 1.0f;
}

} // namespace

void PrepareFrameTextOverlay(MMDApp* app) { // 0x423420
    app->ViewportRenderer().ClearOverlay(EditorOverlay::Text);
    auto* vb = app->OverlayVertices();
    app->TextOverlayPrimitiveCount() = 0;
    if (vb == nullptr)
        return;

    TextVertex* out = nullptr;
    if (FAILED(vb->Lock(0, 0x6D60, reinterpret_cast<void**>(&out), 0)) || out == nullptr)
        return;
    TextVertex* const begin = out;

    const float scale = OverlayScale(app);
    const RECT view = app->ViewportRect();
    auto append = [&](float left, float top, float right, float bottom, float u0, float v0,
                      float u1, float v1, D3DCOLOR color) {
        PutQuad(out, left, top, right, bottom, u0, v0, u1, v1, color);
        app->TextOverlayPrimitiveCount() += 2;
    };
    auto pair = [&](float left, float top, float right, float bottom, float u0, float v0, float u1,
                    float v1, D3DCOLOR foreground, D3DCOLOR shifted) {
        append(left, top, right, bottom, u0, v0, u1, v1, foreground);
        append(left - 2.0f, top - 2.0f, right - 2.0f, bottom - 2.0f, u0, v0, u1, v1, shifted);
    };
    auto digits = [&](const char* text, float& xOffset, float top, float atlasV0, float atlasV1,
                      D3DCOLOR foreground, D3DCOLOR shifted) {
        for (const char* p = text; *p != '\0'; ++p) {
            if (*p == ' ') {
                xOffset += 12.0f;
                continue;
            }
            const int digit = *p - '0';
            if (digit < 0 || digit > 9)
                continue;
            const float u0 = static_cast<float>(16 * digit) / 512.0f;
            const float left = static_cast<float>(view.left + static_cast<int>(xOffset * scale));
            pair(left, top, left + 12.5f * scale, top + 25.0f * scale, u0, atlasV0,
                 u0 + 15.0f / 512.0f, atlasV1, foreground, shifted);
            xOffset += 12.0f;
        }
    };

    // 0x423591..0x423EBF: information/FPS. All positions first truncate
    // the scaled offset to int, matching the original x87-to-int sequence.
    if (app->FpsOverlayEnabled() != 0) {
        const int fps = app->FramesPerSecond();
        const float fpsTop = static_cast<float>(view.bottom - static_cast<int>(30.0f * scale));
        if (fps > 0) {
            float xOffset = 15.0f;
            const bool numeric =
                app->PlaybackActive() != 0 || app->state.frameVolumeControlEnabled == 0;
            if (numeric) {
                char value[256]{};
                std::snprintf(value, sizeof(value), "%3d", fps);
                digits(value, xOffset, fpsTop, 0.375f, 0.43554688f, 0xFFE0E0E0u, 0xFF0000FFu);
            } else {
                // 0x42382F: the A4420 path substitutes two fixed glyphs.
                xOffset = 27.0f;
                for (int glyph = 0; glyph < 2; ++glyph) {
                    const float left =
                        static_cast<float>(view.left + static_cast<int>(xOffset * scale));
                    pair(left, fpsTop, left + 12.5f * scale, fpsTop + 25.0f * scale, 0.4375f,
                         0.375f, 0.46679688f, 0.43554688f, 0xFFE0E0E0u, 0xFF0000FFu);
                    xOffset += 12.0f;
                }
            }
            xOffset += 4.0f;
            const float left = static_cast<float>(view.left + static_cast<int>(xOffset * scale));
            pair(left, fpsTop, left + 37.5f * scale, fpsTop + 25.0f * scale, 0.3125f, 0.375f,
                 0.40429688f, 0.43554688f, 0xFFE0E0E0u, 0xFF0000FFu);
        } else {
            const bool english = app->EnglishUI() != 0;
            const float left = static_cast<float>(view.left - static_cast<int>(-30.5f * scale));
            const float width = (english ? 150.0f : 125.0f) * scale;
            pair(left, fpsTop, left + width, fpsTop + 25.0f * scale, english ? 0.0f : 0.375f,
                 0.4375f, english ? 0.37304688f : 0.68554688f, 0.49804688f, 0xFFE0E0E0u,
                 0xFF0000FFu);
        }
    }

    // 0x423EC5..0x4249F7: playback frame, bar and HH:MM:SS cells.
    if (app->PlaybackActive() != 0 && app->FrameStepPlayback() == 0) {
        char frameText[256]{};
        const int frame = static_cast<int>(30.0f * app->PlaybackCursorSeconds());
        std::snprintf(frameText, sizeof(frameText), "%3d", frame);
        float xOffset = 15.0f;
        const float top = static_cast<float>(view.top - static_cast<int>(-5.0f * scale));
        digits(frameText, xOffset, top, 0.5f, 0.56054688f, 0xFF000000u, 0xFFFFFFFFu);
        xOffset += 4.0f;
        float left = static_cast<float>(view.left + static_cast<int>(xOffset * scale));
        pair(left, top, left + 200.0f * scale, top + 25.0f * scale, 0.3125f, 0.50390625f,
             0.81054688f, 0.56054688f, 0xFF000000u, 0xFFFFFFFFu);

        const float seconds = app->PlaybackCursorSeconds();
        xOffset += 75.0f;
        char timePart[256]{};
        std::snprintf(timePart, sizeof(timePart), "%02d", static_cast<int>(seconds / 3600.0f));
        digits(timePart, xOffset, top, 0.5f, 0.56054688f, 0xFF000000u, 0xFFFFFFFFu);
        xOffset += 12.5f; // the colon itself is baked into the bar texture
        std::snprintf(timePart, sizeof(timePart), "%02d", static_cast<int>(seconds / 60.0f));
        digits(timePart, xOffset, top, 0.5f, 0.56054688f, 0xFF000000u, 0xFFFFFFFFu);
        xOffset += 12.5f;
        std::snprintf(timePart, sizeof(timePart), "%02d", static_cast<int>(seconds) % 60);
        digits(timePart, xOffset, top, 0.5f, 0.56054688f, 0xFF000000u, 0xFFFFFFFFu);
    }

    // 0x424A1F..0x424D8F: transient renderer-state indication. These
    // overlays are deliberately unscaled and use alpha-byte color patterns.
    const std::uint8_t status = app->state.autoRepeat;
    if (status > 0 && status < 4) {
        const float left = static_cast<float>(view.right) - 160.0f;
        const float top = static_cast<float>(view.top) + 30.0f;
        const float u0 = static_cast<float>(16 * (4 - status)) / 512.0f;
        pair(left, top, left + 40.0f, top + 60.0f, u0, 96.0f / 512.0f, u0 + 15.0f / 512.0f,
             127.0f / 512.0f, 0xBB000000u, 0xBBFFFFFFu);
    } else if (status == 4) {
        const float left = static_cast<float>(view.right) - 200.0f;
        const float top = static_cast<float>(view.top) + 120.0f;
        append(left, top, left + 120.0f, top + 20.0f, 176.0f / 512.0f, 130.0f / 512.0f,
               319.0f / 512.0f, 159.0f / 512.0f, 0xFFFFFFFFu);
    }

    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Text, begin,
                                           app->TextOverlayPrimitiveCount() * 3u, sizeof(TextVertex));
    DumpVertexBatch("text", begin, app->TextOverlayPrimitiveCount(), 84);
    vb->Unlock();
}

void PrepareFrameLineOverlay(MMDApp* app) { // 0x4757C3..0x4759D8
    app->ViewportRenderer().ClearOverlay(EditorOverlay::Lines);
    app->LineOverlayPrimitiveCount() = 0;
    if (!app->LeftMouseButtonHeld() || app->state.viewportInputActive == 0 ||
        app->BoneBoxSelectionActive() == 0)
        return;

    D3DRenderer* sub = app->Renderer();
    if (sub == nullptr)
        return;
    // The renderer owns the selection and guide-line vertex buffer.
    auto* vb = sub->lineVertexBuffer;
    if (vb == nullptr)
        return;

    LineVertex* out = nullptr;
    if (FAILED(vb->Lock(0, 8 * sizeof(LineVertex), reinterpret_cast<void**>(&out), 0)) ||
        out == nullptr)
        return;

    const float left = static_cast<float>(std::min(app->BoneBoxStartX(), app->MouseX()));
    const float right = static_cast<float>(std::max(app->BoneBoxStartX(), app->MouseX()));
    const float top = static_cast<float>(std::min(app->BoneBoxStartY(), app->MouseY()));
    const float bottom = static_cast<float>(std::max(app->BoneBoxStartY(), app->MouseY()));
    const LineVertex vertices[8] = {
        {left, top, 0.0f, 1.0f, 0xFFFFFFFFu},     {left, bottom, 0.0f, 1.0f, 0xFFFFFFFFu},
        {left, bottom, 0.0f, 1.0f, 0xFFFFFFFFu},  {right, bottom, 0.0f, 1.0f, 0xFFFFFFFFu},
        {right, bottom, 0.0f, 1.0f, 0xFFFFFFFFu}, {right, top, 0.0f, 1.0f, 0xFFFFFFFFu},
        {right, top, 0.0f, 1.0f, 0xFFFFFFFFu},    {left, top, 0.0f, 1.0f, 0xFFFFFFFFu},
    };
    std::copy(std::begin(vertices), std::end(vertices), out);
    app->LineOverlayPrimitiveCount() = 4;
    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Lines, out, 8, sizeof(LineVertex));
    if (right - left >= 20.0f && bottom - top >= 20.0f)
        DumpVertexBatch("line_selection", out, 4, 40);
    vb->Unlock();
}

} // namespace mikudancestudio
