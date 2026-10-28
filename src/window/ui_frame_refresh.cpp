// ===========================================================================
// Timeline-driven Light / Self Shadow panel refresh.
//   0x00411070  RefreshLightPanel: evaluate the light key
//               list and synchronize controls 455..466
//   0x00411B90  RefreshSelfShadowPanel: evaluate the shadow
//               key list and synchronize controls 560..564
//   0x004134E0  SyncAccessoryEditPanel: reload the accessory
//               edit controls 474..486 from the selected accessory
//   0x00411DF0  RegisterSelfShadowState: insert/overwrite the
//               self-shadow key at a frame with the current mode + interval
//               (the SelfShadow sibling of RegisterCameraState/RegisterLightState)
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"

namespace mikudancestudio {
namespace {

// Porting-era trace under MIKUDANCESTUDIO_LIGHT_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// (including the cross-TU TraceSceneLightState) valid and inlines away to
// nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void WriteLightTrace(MMDApp* app, const char* stage) {
    const char* directory = std::getenv("MIKUDANCESTUDIO_LIGHT_TRACE_DIR");
    if (directory == nullptr || directory[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\light_state.jsonl", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    const D3DLIGHT9& light = app->SceneLight();
    std::fprintf(stream,
                 "{\"stage\":\"%s\",\"type\":%u,\"direction\":[%.9g,%.9g,%.9g],"
                 "\"specular\":[%.9g,%.9g,%.9g],\"ambient\":[%.9g,%.9g,%.9g]}\\n",
                 stage, static_cast<unsigned>(light.Type), light.Direction.x, light.Direction.y,
                 light.Direction.z, light.Specular.r, light.Specular.g, light.Specular.b,
                 light.Ambient.r, light.Ambient.g, light.Ambient.b);
    fclose(stream);
}
#else
inline void WriteLightTrace(MMDApp*, const char*) {
}
#endif

void ReplaceEditText(HWND main, int id, const char* text) {
    HWND edit = GetDlgItem(main, id);
    const LRESULT length = GetWindowTextLengthA(edit);
    SendMessageA(edit, EM_SETSEL, 0, length);
    SendMessageA(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
}

void FormatLegacyOneDecimal(char* text, std::size_t size, float value) {
    const double magnitude = std::floor(std::fabs(static_cast<double>(value)) * 10.0 + 0.5) / 10.0;
    const double rounded = std::signbit(value) ? -magnitude : magnitude;
    sprintf_s(text, size, "%+3.1f", rounded);
}

std::uint32_t FindKeyAtOrAfter(const std::uint32_t* keys, std::uint32_t strideDwords,
                               std::uint32_t frame) {
    std::uint32_t index = 0;
    if (keys[0] < frame) {
        for (;;) {
            const std::uint32_t next = keys[index * strideDwords + 2];
            if (next == 0)
                break;
            index = next;
            if (keys[index * strideDwords] >= frame)
                break;
        }
    }
    return index;
}

} // namespace

void TraceSceneLightState(MMDApp* app, const char* stage) {
    if (app != nullptr)
        WriteLightTrace(app, stage);
}

// VA 0x00411070
void RefreshLightPanel(MMDApp* app) {
    if (app == nullptr)
        return;
    std::uint32_t* keys = reinterpret_cast<std::uint32_t*>(app->LightKeys());
    if (keys == nullptr)
        return;

    const std::uint32_t frame = app->state.currentFrame;
    const std::uint32_t index = FindKeyAtOrAfter(keys, 10, frame);
    const std::uint32_t* rightRecord = keys + index * 10;
    float* right = reinterpret_cast<float*>(keys + index * 10);

    float direction[3];
    float color[3];
    // When the walk reaches the end of the chain, the original copies the
    // terminal key verbatim.  Interpolating it against its own prev index
    // produces a zero denominator (and poisoned the light register payload
    // with NaNs in the previous approximation).
    if (rightRecord[0] <= frame) {
        direction[0] = right[3];
        direction[1] = right[4];
        direction[2] = right[5];
        color[0] = right[6];
        color[1] = right[7];
        color[2] = right[8];
    } else {
        const std::uint32_t previous = keys[index * 10 + 1];
        const std::uint32_t* leftRecord = keys + previous * 10;
        float* left = reinterpret_cast<float*>(keys + previous * 10);
        format::PmmLightKey prior{}, following{};
        prior.frame = leftRecord[0];
        following.frame = rightRecord[0];
        for (int lane = 0; lane < 3; ++lane) {
            prior.direction[lane] = left[3 + lane];
            prior.color[lane] = left[6 + lane];
            following.direction[lane] = right[3 + lane];
            following.color[lane] = right[6 + lane];
        }
        std::array<float, 6> interpolated{};
        if (format::InterpolatePmmLightSegment(
                prior, following, frame, format::PmmLightInterpolation::FrameSeek, interpolated)) {
            for (int lane = 0; lane < 3; ++lane) {
                color[lane] = interpolated[lane];
                direction[lane] = interpolated[lane + 3];
            }
        } else {
            const double amount = static_cast<double>(frame - leftRecord[0]) /
                                  static_cast<double>(rightRecord[0] - leftRecord[0]);
            for (int lane = 0; lane < 3; ++lane) {
                direction[lane] = static_cast<float>((right[3 + lane] - left[3 + lane]) * amount +
                                                     left[3 + lane]);
                color[lane] = static_cast<float>((right[6 + lane] - left[6 + lane]) * amount +
                                                 left[6 + lane]);
            }
        }
    }

    std::memcpy(app->LightDirection(), direction, sizeof(direction));
    std::memcpy(app->LightColor(), color, sizeof(color));
    app->ApplyTimelineLightState();

    // Original 0x411201..0x41121F: IDirect3DDevice9::SetLight(0,
    // reinterpret_cast<D3DLIGHT9*>(app+0x9E180)).
    D3DRenderer* wrapper = app->Renderer();
    if (wrapper != nullptr) {
        IDirect3DDevice9* device = wrapper->device;
        if (device != nullptr)
            device->SetLight(0, &app->SceneLight());
    }
    TraceSceneLightState(app, "timeline-light-refresh");

    const HWND main = app->state.hwnd;
    char text[0x34];
    for (int lane = 0; lane < 3; ++lane) {
        SendMessageA(GetDlgItem(main, panel::kLightColorSliderR + lane), TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(static_cast<int>(color[lane] * 256.0f)));
        SendMessageA(GetDlgItem(main, panel::kLightDirSliderX + lane), TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(static_cast<int>(direction[lane] * 100.0f)));

        sprintf_s(text, sizeof(text), "%3d", static_cast<int>(color[lane] * 256.0f));
        ReplaceEditText(main, panel::kLightColorEditR + lane, text);
        FormatLegacyOneDecimal(text, sizeof(text), direction[lane]);
        ReplaceEditText(main, panel::kLightDirEditX + lane, text);
    }
}

// VA 0x00411B90
void RefreshSelfShadowPanel(MMDApp* app) {
    if (app == nullptr)
        return;
    std::uint32_t* keys = reinterpret_cast<std::uint32_t*>(app->ShadowKeys());
    if (keys == nullptr)
        return;

    const std::uint32_t frame = app->state.currentFrame;
    const std::uint32_t index = FindKeyAtOrAfter(keys, 6, frame);
    const std::uint32_t* record = keys + index * 6;
    if (record[0] != frame)
        record = keys + record[1] * 6;

    app->SelfShadowMode() =
        static_cast<std::int8_t>(reinterpret_cast<const std::uint8_t*>(record)[12]);
    app->state.selfShadowDistance = *reinterpret_cast<const float*>(record + 4);
    SeekPortableSceneSelfShadow(app, static_cast<int>(frame));

    const double rawRange = 10000.0 - static_cast<double>(app->state.selfShadowDistance) * 100000.0;
    int range = static_cast<int>(rawRange);
    if (rawRange - static_cast<double>(range) >= 0.5)
        ++range;

    const HWND main = app->state.hwnd;
    SendMessageA(GetDlgItem(main, panel::kSelfShadowRangeSlider), TBM_SETPOS, TRUE, range);
    char text[0x34];
    sprintf_s(text, sizeof(text), "%d", range);
    SetWindowTextA(GetDlgItem(main, panel::kSelfShadowRangeEdit), text);

    const int mode = app->SelfShadowMode();
    SendMessageA(GetDlgItem(main, panel::kEditOffCheckbox), BM_SETCHECK,
                 mode == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(main, panel::kEditMode1Checkbox), BM_SETCHECK,
                 mode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(main, panel::kEditMode2Checkbox), BM_SETCHECK,
                 mode != 0 && mode != 1 ? BST_CHECKED : BST_UNCHECKED, 0);
}

// VA 0x004134E0 - reload the accessory edit panel
// (combos 474/475 + checkboxes 476/477/486 + pos/rot/scale/opacity edits)
// from the selected accessory slot.
void SyncAccessoryEditPanel(MMDApp* app) {
    if (app == nullptr)
        return;
    const std::uint8_t selected = app->state.selectedObjectSlot;
    mdl::AccessoryRecord* accessory = app->AccessorySlot(selected);
    if (accessory == nullptr)
        return;

    const HWND main = app->state.hwnd;
    HWND modelCombo = GetDlgItem(main, panel::kMainComboGround);
    const LRESULT oldModel = SendMessageA(modelCombo, CB_GETCURSEL, 0, 0);
    const std::int32_t parentSlot = accessory->parentModel;
    const mdl::ModelRecord* parentModel = parentSlot >= 0 ? app->ModelSlot(parentSlot) : nullptr;
    // The stored parent is a model slot; the control contains model order.
    const int parentComboIndex = parentModel != nullptr ? parentModel->comboSelIndex : 0;
    if (oldModel != parentComboIndex) {
        HWND boneCombo = GetDlgItem(main, panel::kAttachBoneCombo);
        SendMessageA(boneCombo, CB_RESETCONTENT, 0, 0);
        if (parentSlot >= 0) {
            if (parentModel != nullptr) {
                const mdl::ModelRecord& record = *parentModel;
                const std::int32_t boneCount = record.boneCount;
                mdl::BoneRecord* bones = record.boneTable;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    mdl::BoneRecord* bone = &bones[i];
                    const mdl::BoneType type = bone->type;
                    if (type < mdl::BoneType::InertTip || type == mdl::BoneType::FixedAxis)
                        SendMessageA(boneCombo, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(bone->name));
                }
                SendMessageA(modelCombo, CB_SETCURSEL, static_cast<WPARAM>(record.comboSelIndex),
                             0);
            }
        } else {
            SendMessageA(modelCombo, CB_SETCURSEL, 0, 0);
        }
    }

    if (parentSlot >= 0) {
        if (parentModel != nullptr) {
            const mdl::BoneRecord* bones = parentModel->boneTable;
            const std::int32_t boneIndex = accessory->parentBone;
            if (bones != nullptr && boneIndex >= 0) {
                const char* wanted = bones[boneIndex].name;
                HWND boneCombo = GetDlgItem(main, panel::kAttachBoneCombo);
                char actual[108]{};
                const LRESULT current = SendMessageA(boneCombo, CB_GETCURSEL, 0, 0);
                if (current >= 0)
                    SendMessageA(boneCombo, CB_GETLBTEXT, current,
                                 reinterpret_cast<LPARAM>(actual));
                if (current < 0 || std::strcmp(wanted, actual) != 0) {
                    const LRESULT count = SendMessageA(boneCombo, CB_GETCOUNT, 0, 0);
                    for (LRESULT i = 0; i < count; ++i) {
                        SendMessageA(boneCombo, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(actual));
                        if (std::strcmp(wanted, actual) == 0) {
                            SendMessageA(boneCombo, CB_SETCURSEL, i, 0);
                            break;
                        }
                    }
                }
            }
        }
    }

    SendMessageA(GetDlgItem(main, panel::kAccessoryVisibleCheckbox), BM_SETCHECK,
                 accessory->visible != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(main, panel::kAccessoryAddBlendCheckbox), BM_SETCHECK,
                 accessory->additiveBlend != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(main, panel::kAccessoryShadowCheckbox), BM_SETCHECK,
                 accessory->shadowEnabled != 0 ? BST_CHECKED : BST_UNCHECKED, 0);

    char text[104];
    for (int lane = 0; lane < 3; ++lane) {
        sprintf_s(text, sizeof(text), "%3.4f", static_cast<double>(accessory->position[lane]));
        ReplaceEditText(main, panel::kAccPosXEdit + lane, text);
#if defined(_M_X64)
        // Original x64: divss, mulss, then conversion for the printf argument.
        const float degrees = accessory->rotation[lane] / 3.141592025756836f * 180.0f;
#else
        const double degrees =
            static_cast<double>(accessory->rotation[lane]) / 3.141592025756836 * 180.0;
#endif
        sprintf_s(text, sizeof(text), "%3.4f", degrees);
        ReplaceEditText(main, panel::kAccRotXEdit + lane, text);
    }
    sprintf_s(text, sizeof(text), "%3.4f", static_cast<double>(accessory->scale));
    ReplaceEditText(main, panel::kAccScaleEdit, text);
    sprintf_s(text, sizeof(text), "%3.2f", static_cast<double>(accessory->opacity));
    ReplaceEditText(main, panel::kAccOpacityEdit, text);
}

// VA 0x00411DF0 - self-shadow track's "register" backend:
// exact-hit overwrite or free-slot splice of the 24-byte shadow key carrying
// the current shadow mode (this+0xA0B10) and interval (this+0xA0B0C).
void RegisterSelfShadowState(MMDApp* app, int frameValue) {
    if (app == nullptr || frameValue < 0)
        return;
    std::uint32_t* keys = reinterpret_cast<std::uint32_t*>(app->ShadowKeys());
    if (keys == nullptr)
        return;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameValue);
    const std::uint32_t current = FindKeyAtOrAfter(keys, 6, frame);
    std::uint32_t* currentRecord = keys + current * 6;

    if (currentRecord[0] == frame) {
        reinterpret_cast<std::uint8_t*>(currentRecord)[12] =
            static_cast<std::uint8_t>(app->SelfShadowMode());
        *reinterpret_cast<float*>(currentRecord + 4) = app->state.selfShadowDistance;
        reinterpret_cast<std::uint8_t*>(currentRecord)[20] = 1;
        return;
    }

    std::uint32_t freeIndex = 1;
    while (freeIndex < 10000 && keys[freeIndex * 6] != 0)
        ++freeIndex;
    if (freeIndex >= 10000) {
        char message[0x100];
        sprintf_s(message, sizeof(message),
                  "You cannot regist over %dpoint.\n"
                  "Please execute 'delete unused frame'",
                  10000);
        MessageBoxA(app->state.hwnd, message, "register frame", 0);
        return;
    }

    std::uint32_t* added = keys + freeIndex * 6;
    if (currentRecord[0] < frame) {
        currentRecord[2] = freeIndex;
        added[1] = current;
    } else {
        const std::uint32_t previous = currentRecord[1];
        keys[previous * 6 + 2] = freeIndex;
        added[1] = previous;
        currentRecord[1] = freeIndex;
        added[2] = current;
    }
    added[0] = frame;
    reinterpret_cast<std::uint8_t*>(added)[12] = static_cast<std::uint8_t>(app->SelfShadowMode());
    *reinterpret_cast<float*>(added + 4) = app->state.selfShadowDistance;
    reinterpret_cast<std::uint8_t*>(added)[20] = 1;
    if (frame > app->state.lastRegisteredFrame)
        app->state.lastRegisteredFrame = frame;
}

} // namespace mikudancestudio
