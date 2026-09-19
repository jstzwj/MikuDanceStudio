// ===========================================================================
// VA 0x004175A0 - timeline advance (PlaybackPoseAdvance) + VA 0x00410140
// bezier easing evaluator  (original: sub_4175A0 / sub_410140)
// ===========================================================================
// Called from the FrameDriver catch-up loops (playback_catchup.cpp) once
// per 1/60 tick with advance = 1 and once after the loop with advance = 0.
//
// Flow (verified from the binary):
//   1. per-model pass (0x4175A8): sub_4A31D0(model, cursor 0x9E64C,
//      count 0xA0CC4) for every non-null slot - the bone/morph keyframe
//      interpolator (ported in model_keyframe_advance.cpp).
//   2. advance != 0 -> stop here (0x4175DB skips everything below; the
//      globals are rebuilt once by the final settle call).
//   3. frame rounding (0x4175ED): f = cursor*30 (double); if
//      f*1000 - (int)(f*1000) >= 0.5 -> f = ((int)(f*1000)+1)/1000.
//   4. FOUR global keyframe tracks, all keyed on (0x2F8 || 0x9ED98) plus a
//      per-track active byte.  The key lists are the arrays allocated by
//      the 0x466D20 initializer at app+0x374..0x380 (record/stride):
//      A camera  keys*[0x374]  84B rec  cursor 0x9E65C active 0x9E660
//      B light   keys*[0x378]  40B rec  cursor 0x9E664 active 0x9E668
//      C shadow  keys*[0x37C]  24B rec  cursor 0x9E66C active 0x9E670
//      D physics keys*[0x380]  36B rec  cursor 0x9E674 active 0x9E678
//      (B terminates in device vtable 0xCC = SetLight(0, D3DLIGHT9
//      @0x9E180); D drives the gravity slots 0x9ED80/0x9ED8C.)
//      Records: {+0 frame(uint), +4 prevIdx, +8 nextIdx, ...payload};
//      index 0 is the list sentinel - "next == 0" parks the cursor and
//      clears the active flag, copying the final key's payload verbatim.
//      When parked on a key with frame == f: exact copy; else
//      prev = keys[rec.prevIdx]: prev.frame == cur.frame-1 -> prev copy
//      verbatim (the original's adjacent-frame snap), otherwise
//      t = (f - prev.frame) / (cur.frame - prev.frame) and the camera
//      channels go through sub_410140 bezier easing (six channels,
//      control bytes at rec+40..63 as 4x6: +40 x1 / +46 y1 / +52 x2 /
//      +58 y2, one byte per channel, /127 normalized; linear early-out
//      when x1==y1 && x2==y2; 12-step binary inversion of the X curve).
//   5. after track A: projection rebuild - fov 0x9E1E8 * pi/180 (dbl
//      0.01745329238474369), D3DXMatrixPerspectiveFovLH(mat, fov, aspect
//      = sub1d574+0x1D4EC, 1.0f, 100000.0f), device (sub1d574+0x1D4E0)
//      vtable slot 0xB0 = SetTransform(D3DTS_PROJECTION, mat).
//   6. accessory tracks (0x418093): 256 slots, active flags 0x9E6B8,
//      per-slot key arrays (app+0x384)[i], key cursors 0x9E67C[i],
//      accessory objects 0x9DD70[i]; 60B records - on the advancing tail
//      only the two floats (+52 -> obj+0x22C, +56 -> obj+0x4A0) lerp
//      linearly, everything else snaps from the previous key.
//
// Port notes (docs/ARCHITECTURE.md section 8):
//   - sub_4A31D0 (per-model bone/morph keyframe application) is implemented
//     in model_keyframe_advance.cpp; this call site preserves its slot order.
//   - The 0x466D20 initializer now allocates the four key arrays and the
//     255 accessory slots during WM_CREATE (ui_init.cpp), before the
//     FrameDriver can ever run, so the walks below dereference the key
//     pointers directly like the original - the phase-15 null guards
//     were reclaimed.  The phase-15 SetClipPlane calls on the light
//     track were vtable slot 0xDC; the original's 0x467780-series and
//     the consumer both use slot 0xCC = SetLight - fixed in phase 19.
//   - D3DXMatrixPerspectiveFovLH is resolved through d3dx_dyn.hpp (load-
//     time import in the original; same convention as ui_hscroll.cpp).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

constexpr double kPiOver180 = 0.01745329238474369;   // dbl_52BB20 region

// VA 0x00410140 / x64 sub_7FF7CB47A8B0 - camera-channel bezier easing.
// ch 0..5 selects the control-byte column; the X curve 3(1-u)^2 u x1 +
// 3(1-u) u^2 x2 + u^3 is inverted by 12 halving steps starting at u = 0.5,
// then the Y curve 3(1-u) u^2 y2 + 3(1-u)^2 u y1 + u^3 gives the eased
// fraction.
//
// Precision (x64 0x7FF7CB47A8B0, instruction-pinned): the whole evaluator
// runs single-precision SSE - cvtdq2ps + divss 127.0f for the X control
// scales, mulss/addss chains for gx, ucomiss float equality for the
// early exit, and the Y terms multiply the raw byte FIRST and divide by
// 127.0f afterwards (0x7FF7CB47AC19/0x7FF7CB47AC1E).  The x86 original
// (0x410140) instead keeps the control scales in double locals, runs the
// polynomial on the x87 stack and round-trips u/step/gx through float
// stack slots; that shape stays in the _M_IX86 branch.
float CameraEase(const mdl::CameraKey* keys, int ch, int idx, float t) {
    const auto& interpolation = keys[idx].interpolation;
    if (interpolation[0][ch] == interpolation[1][ch] &&
        interpolation[2][ch] == interpolation[3][ch])
        return t;                                        // linear 0x410180
#if defined(_M_IX86)
    // x86 0x410140: v47/v46 are double locals ((double)byte / 127.0), the
    // polynomial evaluates on the x87 stack, and u/step/gx live in float
    // stack slots (fstp/fld around the compare and the update).
    const double x1 = static_cast<double>(
                          static_cast<signed char>(interpolation[0][ch])) / 127.0;
    const double x2 = static_cast<double>(
                          static_cast<signed char>(interpolation[2][ch])) / 127.0;
    float u = 0.5f, half = 0.25f;
    for (int i = 0; i < 12; ++i) {                       // 0x4101E5..4E5
        const float om = 1.0f - u;
        const double gx = om * 3.0 * u * u * x2 + om * (om * 3.0) * u * x1 +
                          u * u * u;
        if (static_cast<float>(gx) == t)                 // float-slot compare
            break;
        u = static_cast<float>(gx >= static_cast<double>(t) ? u - half
                                                            : u + half);
        half *= 0.5f;
    }
    const float om = 1.0f - u;                           // 0x4104EF
    return static_cast<float>(
        3.0 * om * u * u *
            static_cast<double>(
                static_cast<signed char>(interpolation[3][ch])) / 127.0 +
        om * (om * 3.0) * u *
            static_cast<double>(
                static_cast<signed char>(interpolation[1][ch])) / 127.0 +
        u * u * u);
#else
    // x64: X scales pre-divided (divss 127.0f at 0x7FF7CB47A970/A979); the
    // loop compare is ucomiss on float gx; the Y tail multiplies the raw
    // bytes first and divides each product by 127.0f.
    const float x1 = static_cast<float>(
                         static_cast<signed char>(interpolation[0][ch])) / 127.0f;
    const float x2 = static_cast<float>(
                         static_cast<signed char>(interpolation[2][ch])) / 127.0f;
    const float y1b = static_cast<float>(
        static_cast<signed char>(interpolation[1][ch]));
    const float y2b = static_cast<float>(
        static_cast<signed char>(interpolation[3][ch]));
    float u = 0.5f, half = 0.25f;
    for (int i = 0; i < 12; ++i) {                       // 0x7FF7CB47A980..B95
        const float v = 1.0f - u;
        const float v3 = v * 3.0f;
        const float gx = (v3 * v * u) * x1 + (v3 * u * u) * x2 + u * u * u;
        if (gx == t)                                     // ucomiss 0x7FF7CB47A9C3
            break;
        u = gx >= t ? u - half : u + half;               // comiss/addss/subss
        half *= 0.5f;
    }
    const float v = 1.0f - u;                            // 0x7FF7CB47ABC2
    const float v3 = v * 3.0f;
    return (v3 * v * u) * y1b / 127.0f + (v3 * u * u) * y2b / 127.0f +
           u * u * u;
#endif
}

// Camera-key payload copy (fields shared by the exact/terminal branches;
// 0x4176C5 / 0x41777B / 0x4177F1).
void CopyCameraKey(MMDApp* app, const mdl::CameraKey& key) {
    auto& s = *app;
    std::memcpy(s.CameraPosition(), key.eye, sizeof(key.eye));
    std::memcpy(s.CameraRotation(), key.target, sizeof(key.target));
    s.CameraDistance() = key.distance;
    s.CameraPerspective() = key.perspective;
    s.CameraFov() = static_cast<float>(key.fov);
}

// Light-key payload copy (0x417AC9 / 0x417BC5): the vec3 mirrors plus the
// SetLight refresh through device vtable slot 0xCC.
void CopyLightKey(MMDApp* app, const mdl::LightKey& key, bool apply) {
    auto& s = *app;
    std::memcpy(s.LightDirection(), key.direction, sizeof(key.direction));
    std::memcpy(s.LightColor(), key.color, sizeof(key.color));
    s.ApplyTimelineLightState();
    if (!apply)
        return;
    D3DRenderer* wrapper = s.Renderer();
    IDirect3DDevice9* dev = wrapper->device;               // +0x1D4E0
    dev->SetLight(0, &s.SceneLight());                       // slot 0xCC
}

}  // namespace

// VA 0x0042E640 - seek/evaluate the global camera key list.
// This is the
// state-producing core shared by frame stepping, delete/paste refresh and the
// camera register button.  The original function also echoes every value to
// the camera controls; those controls are already refreshed by the panel/UI
// paths that call this function.  The camera-PARENT switch refresh
// (x64 0x479B00 = RefillBoneRegisterCombo) is NOT deferred - all three of
// the original's branches run it before storing the new parent pair, so it
// is inlined at both assignment sites below.
void ReloadModels(MMDApp* app) {
    if (app == nullptr)
        return;
    auto& s = *app;
    mdl::CameraKey* const keys = s.CameraKeys();
    if (keys == nullptr)
        return;
    const std::uint32_t frame =
        s.state.currentFrame;
    std::uint32_t index = 0;
    while (keys[index].frame < frame) {
        const std::uint32_t next = keys[index].next;
        if (next == 0)
            break;
        index = next;
    }

    const mdl::CameraKey& current = keys[index];
    const std::uint32_t currentFrame = current.frame;
    if (currentFrame <= frame) {
        CopyCameraKey(app, current);
        // Exact-hit and terminal-key copies both clear the view offset
        // mid-payload (x64 *(_QWORD*)(app+0x340) = 0 at 0x7FF7CB479CFC and
        // 0x7FF7CB479EF9; x86 0x308/0x30C).
        s.state.viewOffsetX = 0.0f;
        s.state.viewOffsetY = 0.0f;
        // Camera-parent switch refresh (x64 0x7FF7CB479D2F / terminal
        // 0x7FF7CB479F2C; x86 0x42E710 / 0x42E8EC): when the incoming
        // parent model differs from the live camera parent, the original
        // rebuilds the bone-register combobox (0x479B00 = x86 sub_410040)
        // BEFORE overwriting the pair.
        if (current.parentModel != s.CameraParentModel())
            RefillBoneRegisterCombo(app, current.parentModel);
        s.CameraParentModel() = current.parentModel;
        s.CameraParentBone() = current.parentBone;
    } else {
        const mdl::CameraKey& previous = keys[current.previous];
        const std::uint32_t previousFrame = previous.frame;
        // No adjacent-frame snap here: the between branch of the original
        // (x64 0x7FF7CB47A3CE / x86 0x42ED24) goes straight to the eased
        // fraction - unlike the playback path below, whose double-valued
        // frame can land between consecutive integer keys (snap at
        // 0x4177F1).  With ReloadModels' integer app frame the walk
        // guarantees previousFrame < frame < currentFrame, i.e. a span of
        // at least 2, so cur-1 == prev is unreachable and the branch that
        // used to sit here was dead code copied from the playback path.
        const float t = static_cast<float>(
            static_cast<double>(frame - previousFrame) /
            static_cast<double>(currentFrame - previousFrame));
        s.CameraPerspective() = previous.perspective;
        float* output[6] = {
            &s.CameraPosition()[0], &s.CameraPosition()[1],
            &s.CameraPosition()[2], &s.CameraRotation()[0],
            &s.CameraDistance(), &s.CameraFov()};
        for (int channel = 0; channel < 6; ++channel) {
            const float eased = CameraEase(keys, channel,
                                            static_cast<int>(index), t);
            const double a = channel < 3 ? previous.eye[channel]
                : channel == 3 ? previous.target[0]
                : channel == 4 ? previous.distance
                               : static_cast<double>(previous.fov);
            const double b = channel < 3 ? current.eye[channel]
                : channel == 3 ? current.target[0]
                : channel == 4 ? current.distance
                               : static_cast<double>(current.fov);
            *output[channel] = static_cast<float>(eased * (b - a) + a);
        }
        // Camera channel 3 is one easing curve shared by all three
        // Euler components.  Hex-Rays exposes only the first store when
        // the x87 value is kept live; the following Y/Z stores are part
        // of the same original branch.
        const float rotationEase = CameraEase(
            keys, 3, static_cast<int>(index), t);
        for (int axis = 0; axis < 3; ++axis) {
            const float a = previous.target[axis];
            const float b = current.target[axis];
            s.CameraRotation()[axis] = static_cast<float>(
                static_cast<double>(rotationEase) *
                    (static_cast<double>(b) - static_cast<double>(a)) +
                static_cast<double>(a));
        }
        // View offset clear inside the interpolation stores
        // (x64 0x7FF7CB47A537; x86 0x308/0x30C).
        s.state.viewOffsetX = 0.0f;
        s.state.viewOffsetY = 0.0f;
        // Same refresh gate as the exact branch, against the PREVIOUS
        // record's parent (x64 0x7FF7CB47A401; x86 0x42ED62) - the
        // between branch inherits the parent pair from the earlier key.
        if (previous.parentModel != s.CameraParentModel())
            RefillBoneRegisterCombo(app, previous.parentModel);
        s.CameraParentModel() = previous.parentModel;
        s.CameraParentBone() = previous.parentBone;
    }

    D3DRenderer* wrapper = s.Renderer();
    if (wrapper != nullptr) {
        IDirect3DDevice9* device = wrapper->device;    // +120032
        if (device != nullptr) {
            d3dx::D3DXMATRIXF projection{};
            auto& api = d3dx::Get();
            if (api.Load()) {
                api.perspectiveFovLH(
                    &projection,
                    static_cast<float>(
                        static_cast<double>(s.CameraFov()) * kPiOver180),
                    wrapper->aspectRatio,  // +0x1D4EC
                    1.0f, 100000.0f);
                device->SetTransform(
                    D3DTS_PROJECTION,
                    reinterpret_cast<const D3DMATRIX*>(&projection));
            }
        }
    }
}

// VA 0x004175A0 - see file header.
void PlaybackPoseAdvance(MMDApp* app, int advance) {
    auto& s = *app;

    // ---- 1. per-model keyframe application (0x4175A8) --------------------
    unsigned char** models = s.ModelSlots();
    for (int j = 0; j < kModelSlotCount; ++j)
        if (models[j] != nullptr)
            AdvanceModelKeyframes(models[j], s.PlaybackCursorSeconds(),
                      s.PlaybackPhysicsMode());

    if (advance != 0)
        return;                                          // 0x4175DB

    // ---- 3. frame rounding (0x4175ED) -------------------------------------
    double frame = static_cast<double>(s.PlaybackCursorSeconds() *
                                       30.0f);
    {
        const double scaled = frame * 1000.0;
        if (scaled - static_cast<double>(static_cast<int>(scaled)) >= 0.5)
            frame = static_cast<double>(static_cast<int>(scaled) + 1) / 1000.0;
    }

    const bool editGate = s.state.optflag[0] != 0 ||
                          s.state.followCameraEnabled != 0;

    // ---- 4A. camera track (0x417656..0x417A48) ----------------------------
    if (editGate && s.CameraTrackActive() != 0) {
        mdl::CameraKey* keys = s.CameraKeys();
        s.state.viewOffsetX = 0.0f;                        // 0x308/0x30C
        s.state.viewOffsetY = 0.0f;
        while (true) {
            const mdl::CameraKey& key = keys[s.CameraTrackCursor()];
            if (!(static_cast<double>(key.frame) < frame &&
                  s.CameraTrackActive() != 0))
                break;                                    // 0x4176AF
            const std::uint32_t next = key.next;
            if (next != 0) {
                s.CameraTrackCursor() = next;
            } else {
                s.CameraTrackActive() = 0;
                CopyCameraKey(app, key);
                s.CameraParentModel() = key.parentModel;
                s.CameraParentBone() = key.parentBone;
                break;
            }
        }
        if (s.CameraTrackActive() != 0) {
            const int cur = static_cast<int>(s.CameraTrackCursor());
            const mdl::CameraKey& current = keys[cur];
            const std::uint32_t frameCur = current.frame;
            if (frame == static_cast<double>(frameCur)) {
                CopyCameraKey(app, current);              // 0x41777B
                s.CameraParentModel() = current.parentModel;
                s.CameraParentBone() = current.parentBone;
            } else {
                const mdl::CameraKey& previous = keys[current.previous];
                if (frameCur - 1 == previous.frame) {
                    CopyCameraKey(app, previous);         // 0x4177F1
                } else {
                    const std::uint32_t framePrev = previous.frame;
                    // x64 0x7FF7CB489D2A..0x7FF7CB489D4D: the fraction is
                    // single-precision throughout - (float)frame minus
                    // cvtsi2ss(prev.frame), divided by cvtsi2ss(cur-prev).
                    const float t =
                        (static_cast<float>(frame) -
                         static_cast<float>(framePrev)) /
                        static_cast<float>(frameCur - framePrev);
                    s.CameraPerspective() = previous.perspective;
                    // Per-channel eased lerp (0x41789E..0x4179CA; x64
                    // 0x7FF7CB489D59..0x7FF7CB489E60 keeps every channel
                    // in mulss/addss floats).  The eased fraction from
                    // sub_410140 stays in st0 and multiplies the delta -
                    // the decompiler drops this (labels it the raw t); the
                    // disasm is authoritative.
                    float* output[6] = {
                        &s.CameraPosition()[0], &s.CameraPosition()[1],
                        &s.CameraPosition()[2], &s.CameraRotation()[0],
                        &s.CameraDistance(), &s.CameraFov()};
                    for (int ch = 0; ch < 6; ++ch) {
                        const float e = CameraEase(keys, ch, cur, t);
                        const float curV = ch < 3 ? current.eye[ch]
                            : ch == 3 ? current.target[0]
                            : ch == 4 ? current.distance
                                      : static_cast<float>(current.fov);
                        const float prevV = ch < 3 ? previous.eye[ch]
                            : ch == 3 ? previous.target[0]
                            : ch == 4 ? previous.distance
                                      : static_cast<float>(previous.fov);
                        *output[ch] = e * (curV - prevV) + prevV;
                    }
                    const float rotationEase = CameraEase(keys, 3, cur, t);
                    for (int axis = 0; axis < 3; ++axis) {
                        const float curV = current.target[axis];
                        const float prevV = previous.target[axis];
                        s.CameraRotation()[axis] =
                            rotationEase * (curV - prevV) + prevV;
                    }
                }
                s.CameraParentModel() = previous.parentModel;
                s.CameraParentBone() = previous.parentBone;
            }
        }
        // projection rebuild (0x417A19..0x417A46)
        D3DRenderer* wrapper = s.Renderer();
        const float fovRad = static_cast<float>(
            static_cast<double>(s.CameraFov()) * kPiOver180);
        d3dx::D3DXMATRIXF mat{};
        auto* d3dx = &d3dx::Get();
        if (d3dx->Load())
            d3dx->perspectiveFovLH(&mat, fovRad,
                                   wrapper->aspectRatio,  // +0x1D4EC
                                   1.0f, 100000.0f);
        IDirect3DDevice9* dev = wrapper->device;   // +120032
        dev->SetTransform(D3DTS_PROJECTION,
                          reinterpret_cast<const D3DMATRIX*>(&mat));
    }

    // ---- 4B. light track (0x417A62..0x417DB5) ------------------------------
    // Gate check: x64 sub_7FF7CB489A20 re-tests the same (0x2F8 || 0x9ED98)
    // edit gate as the camera/shadow/gravity sections before the light
    // active flag (0x7FF7CB489F71..0x7FF7CB489F7C).  An earlier note here
    // claimed an active-only gate - that was the x86 reading, wrong for the
    // x64 baseline: during plain playback the light track stays parked.
    if (editGate && s.LightTrackActive() != 0) {
        mdl::LightKey* keys = s.LightKeys();
        while (true) {
            const mdl::LightKey& key = keys[s.LightTrackCursor()];
            if (!(static_cast<double>(key.frame) < frame &&
                  s.LightTrackActive() != 0))
                break;
            const std::uint32_t next = key.next;
            if (next != 0) {
                s.LightTrackCursor() = next;
            } else {
                s.LightTrackActive() = 0;
                CopyLightKey(app, key, true);
                break;
            }
        }
        if (s.LightTrackActive() != 0) {
            const mdl::LightKey& current = keys[s.LightTrackCursor()];
            const std::uint32_t frameCur = current.frame;
            if (frame == static_cast<double>(frameCur)) {
                CopyLightKey(app, current, true);          // 0x417BC5
            } else {
                const mdl::LightKey& previous = keys[current.previous];
                const std::uint32_t framePrev = previous.frame;
                // Single-precision fraction, x64 0x7FF7CB48A126..0x7FF7CB48A14D
                // (cvtsi2ss/subss/divss); the lerps below are mulss/addss.
                const float t =
                    (static_cast<float>(frame) -
                     static_cast<float>(framePrev)) /
                    static_cast<float>(frameCur - framePrev);
                for (int c = 0; c < 3; ++c) {             // 0x417CC0..F2
                    const float curV = current.direction[c];
                    const float prevV = previous.direction[c];
                    s.LightDirection()[c] =
                        t * (curV - prevV) + prevV;
                }
                for (int c = 0; c < 3; ++c) {             // 0x417D2F..5F
                    const float curV = current.color[c];
                    const float prevV = previous.color[c];
                    s.LightColor()[c] =
                        t * (curV - prevV) + prevV;
                }
                // mirrors + SetLight (0x417CFE..0x417DB3)
                s.ApplyTimelineLightState();
                D3DRenderer* wrapper = s.Renderer();
                IDirect3DDevice9* dev = wrapper->device;  // +120032
                dev->SetLight(0, &s.SceneLight());
            }
        }
    }

    // ---- 4C. self-shadow track (0x417DB9..0x417E9B) ------------------------
    if (editGate && s.ShadowTrackActive() != 0) {
        mdl::SelfShadowKey* keys = s.ShadowKeys();
        while (true) {
            const mdl::SelfShadowKey& key = keys[s.ShadowTrackCursor()];
            if (!(static_cast<double>(key.frame) < frame &&
                  s.ShadowTrackActive() != 0))
                break;
            const std::uint32_t next = key.next;
            if (next != 0) {
                s.ShadowTrackCursor() = next;
            } else {
                s.ShadowTrackActive() = 0;
                s.ShadowMode() =
                    static_cast<signed char>(key.mode);
                s.ShadowDistance() = key.distance;
                break;
            }
        }
        if (s.ShadowTrackActive() != 0) {
            const mdl::SelfShadowKey& current = keys[s.ShadowTrackCursor()];
            const std::uint32_t frameCur = current.frame;
            if (frame == static_cast<double>(frameCur)) {
                s.ShadowMode() =
                    static_cast<signed char>(current.mode);
                s.ShadowDistance() = current.distance;
            } else {
                const mdl::SelfShadowKey& previous = keys[current.previous];
                s.ShadowMode() =
                    static_cast<signed char>(previous.mode);
                s.ShadowDistance() = previous.distance;
            }
        }
    }

    // ---- 4D. physics-gravity track (0x417EB1..0x41808D) -------------------
    if (editGate && s.GravityTrackActive() != 0) {
        mdl::GravityKey* keys = s.GravityKeys();
        while (true) {
            const mdl::GravityKey& key = keys[s.GravityTrackCursor()];
            if (!(static_cast<double>(key.frame) < frame &&
                  s.GravityTrackActive() != 0))
                break;
            const std::uint32_t next = key.next;
            if (next != 0) {
                s.GravityTrackCursor() = next;
            } else {
                s.GravityTrackActive() = 0;
                s.GravityNoiseEnabled() = key.noiseEnabled;
                s.GravityNoise() = key.noise;
                s.GravityMagnitude() = key.acceleration;
                std::memcpy(s.GravityDirection(), key.direction,
                            sizeof(key.direction));
                break;
            }
        }
        if (s.GravityTrackActive() != 0) {
            const mdl::GravityKey& current = keys[s.GravityTrackCursor()];
            const std::uint32_t frameCur = current.frame;
            if (frame == static_cast<double>(frameCur)) {
                s.GravityNoiseEnabled() = current.noiseEnabled;
                s.GravityNoise() = current.noise;
                s.GravityMagnitude() = current.acceleration;
                std::memcpy(s.GravityDirection(), current.direction,
                            sizeof(current.direction));
            } else {
                const mdl::GravityKey& previous = keys[current.previous];
                const std::uint32_t framePrev = previous.frame;
                // Single-precision fraction, x64 0x7FF7CB48A4A9..0x7FF7CB48A4C9.
                const float t =
                    (static_cast<float>(frame) -
                     static_cast<float>(framePrev)) /
                    static_cast<float>(frameCur - framePrev);
                s.GravityNoiseEnabled() = previous.noiseEnabled;
                // Noise iteration count (x64 0x7FF7CB48A4CD..0x7FF7CB48A4E4):
                // cvtdq2ps turns (cur - prev) into a FLOAT, mulss applies t,
                // cvttss2si truncates, then prev.noise is added in integer.
                // A double product can cross the integer boundary one frame
                // later and shift the Bullet solver iteration count, so the
                // float domain here is behavior-critical.
                s.GravityNoise() = previous.noise +
                    static_cast<int>(static_cast<float>(
                        current.noise - previous.noise) * t);
                s.GravityMagnitude() =
                    (current.acceleration - previous.acceleration) * t +
                    previous.acceleration;
                for (int axis = 0; axis < 3; ++axis) {
                    s.GravityDirection()[axis] =
                        (current.direction[axis] -
                         previous.direction[axis]) * t +
                        previous.direction[axis];
                }
            }
        }
    }

    // ---- 6. accessory tracks (0x418093..0x4184E9) ---------------------------
    for (int i = 0; i < 0xFF; ++i) {
        if (s.AccessoryTrackActive(i) == 0)
            continue;
        auto& cursor = s.AccessoryTrackCursor(i);
        mdl::AccessoryKey* keys = s.AccessoryKeys(i);
        // The original dereferences the slot pointer directly; a null slot
        // means the track is orphaned (object never restored), so the plain
        // null check below retires it exactly like the reachable original
        // behaviour (same form Wave1-C left in accessory_paste.cpp).
        mdl::AccessoryRecord* const accessorySlot = s.AccessorySlot(i);
        if (keys == nullptr || accessorySlot == nullptr) {
            s.AccessoryTrackActive(i) = 0;
            cursor = 0;
            continue;
        }
        mdl::AccessoryRecord& accessory = *accessorySlot;
        const auto applyDiscreteState = [&](const mdl::AccessoryKey& key) {
            accessory.visible = key.visible;
            accessory.shadowEnabled = key.shadowEnabled;
            accessory.parentModel = key.parentModel;
            accessory.parentBone = key.parentBone;
            std::memcpy(accessory.rotation, key.rotation,
                        sizeof accessory.rotation);
            std::memcpy(accessory.position, key.position,
                        sizeof accessory.position);
        };
        while (true) {
            const mdl::AccessoryKey& rec = keys[cursor];
            if (!(static_cast<double>(
                      rec.frame) < frame &&
                  s.AccessoryTrackActive(i) != 0))
                break;
            const std::uint32_t next = rec.next;
            if (next != 0) {
                cursor = next;
            } else {
                s.AccessoryTrackActive(i) = 0;
                applyDiscreteState(rec);
                accessory.scale = rec.scale;
                accessory.opacity = rec.opacity;
                break;
            }
        }
        if (s.AccessoryTrackActive(i) == 0)
            continue;
        const mdl::AccessoryKey& recCur = keys[cursor];
        const std::uint32_t frameCur = recCur.frame;
        if (frame == static_cast<double>(frameCur)) {
            applyDiscreteState(recCur);
            accessory.scale = recCur.scale;
            accessory.opacity = recCur.opacity;
        } else {
            const mdl::AccessoryKey& recPrev = keys[recCur.previous];
            const std::uint32_t framePrev = recPrev.frame;
            const float t = static_cast<float>(
                (frame - static_cast<double>(framePrev)) /
                static_cast<double>(frameCur - framePrev));
            accessory.scale =
                t * (recCur.scale - recPrev.scale) + recPrev.scale;
            accessory.opacity =
                t * (recCur.opacity - recPrev.opacity) + recPrev.opacity;
            applyDiscreteState(recPrev);
        }
    }
}

}  // namespace mikudancestudio
