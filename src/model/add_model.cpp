// ===========================================================================
// VA 0x00460430 - LoadModelFile / AddModel  (original: sub_460430)
// ===========================================================================
// Model-slot orchestrator: finds a free slot among the kModelSlotCount at
// this+1920,
// allocates the 0x4CCF4 block (operator new + memset + default init
// 0x4A8DC0), runs the loader 0x4BF3E0, and on success registers the model
// name in the three comboboxes (436/474/449 via CB_ADDSTRING), selects it
// (CB_SETCURSEL), refreshes the bone/morph selectors (0x42F1E0 chain),
// resets the manipulation radios (BM_SETCHECK), enables the model-dependent
// menu items, and toggles the physics menu-item state (SetMenuItemInfoA).
// On failure the model is disposed (0x48F830) and the slot cleared.
// When all kModelSlotCount slots are taken the original shows
// "you cannot add models over %d!" (JP: 0x52E198/0x52E18C).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

#include "model_labels.inc"  // GENERATED (kMsgModelLimitJp / kTitleAddModelJp)


}  // namespace

void LoadModelFile(MMDApp* app, const wchar_t* path) {      // 0x460430
    HWND hwnd = static_cast<HWND>(app->Hwnd());
    int slot = 0;
    while (app->ModelSlot(slot) != nullptr) {
        ++slot;
        if (slot >= kModelSlotCount) {  // x64 槽扫描界 255 @0x7FF7CB4B6705
            app->state.enterKeyState = 1;
            char text[256];
            if (app->EnglishUI() == 0) {
                sprintf_s(text, 0x100, kMsgModelLimitJp, kModelSlotCount);
                MessageBoxA(hwnd, text, kTitleAddModelJp, 0);
            } else {
                sprintf_s(text, 0x100, "you cannot add models over %d!",
                          kModelSlotCount);  // 255 @0x7FF7CB4B6733
                MessageBoxA(hwnd, text, "add model", 0);
            }
            return;
        }
    }

    void* block = operator new(mdl::kSize);   // architecture-correct model ABI
    if (block == nullptr)                     //  thunk 0x4C46F0 in original)
        return;
    unsigned char* model = static_cast<unsigned char*>(block);
    app->ModelSlot(slot) = model;
    std::memset(model, 0, mdl::kSize);
    ModelInitDefaults(model);                                 // 0x4A8DC0

    if (ModelLoadPMD(model, hwnd, path,
                     app->Renderer(), 1,
                     1,                                    // a6: box gate
                     app->EnglishUI(),                     // a7: -> m+12740
                     app->Physics(),                      // a8: -> m+60 scene
                     app->PathWorkspace())) {       // 0x4BF3E0
        app->SceneModified() = 1;
        const char* name =
            app->EnglishUI() == 0 ? mdl::Mdl(model)->name
                                  : mdl::Mdl(model)->nameEn;
        mdl::Mdl(model)->comboSelIndex = static_cast<std::uint8_t>(SendMessageA(
            GetDlgItem(hwnd, panel::kMainComboModel), CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(name)));
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name));
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name));
        mdl::Mdl(model)->comboSelIndex2 = mdl::Mdl(model)->comboSelIndex;
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL,
                     mdl::Mdl(model)->comboSelIndex, 0);
        app->state.mainModelComboSelection =
            mdl::Mdl(model)->comboSelIndex;
        app->SetSelectedModelSlot(static_cast<std::uint8_t>(slot));
        mikudancestudio::mdl::Mdl(model)->displayState = app->state.characterTransparentMode;

        if (app->state.optflag[0] != 0) {
            RebuildModelModePanel(app);                                   // 0x44D610
            app->state.optflag[0] = 0;
            PostLanguageSweep(app);                           // 0x42F1E0
            PostModelReload2(app);                            // 0x40D940
            HandleWindowSize(app);                            // 0x443300
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            PostLanguageSweep(app);                           // 0x42F1E0
        }

        SendMessageA(GetDlgItem(hwnd, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kShadowCheckbox), BM_SETCHECK, 1, 0);
        app->EditMode() = ViewportEditMode::Bone;

        HMENU menu = GetMenu(hwnd);
        static const UINT kEnable[] = {0xFD, 0xD9, 0xDC, 0xDA, 0xCA, 0xCB,
                                       0xDB, 0xDE, 0xFB, 0xFC, 0x120, 0x121};
        for (UINT item : kEnable)
            EnableMenuItem(menu, item, 0);
        for (UINT j = 224; j <= 231; ++j)
            EnableMenuItem(GetMenu(hwnd), j, 0);
        for (UINT k = 273; k <= 275; ++k)
            EnableMenuItem(GetMenu(hwnd), k, 0);
        // x64 在 273..275 启用循环后从 0xED 起依次禁用 237..242
        // (@0x7FF7CB4B6CBC..0x4B6D75)，无 EnableMenuItem(menu, 1, 0) 调用。
        static const UINT kDisable[] = {0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2};
        for (UINT item : kDisable)
            EnableMenuItem(GetMenu(hwnd), item, 1);
        EnableWindow(GetDlgItem(hwnd, panel::kExpandShrinkButton), 1);

        // x64 @0x7FF7CB4B6DBD..0x4B6DFC：cbSize=80(=sizeof on x64)、fMask=MIIM_STATE，
        // fState 按 physicsMode==2 置 MFS_GRAYED|MFS_DISABLED，按位置 2 寻址。
        MENUITEMINFOA mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STATE;
        unsigned char* curModel = app->SelectedModel();
        mii.fState = mdl::Mdl(curModel)->physicsMode == 2
                         ? (MFS_GRAYED | MFS_DISABLED)
                         : 0;
        SetMenuItemInfoA(GetSubMenu(GetMenu(hwnd), 7), 2, TRUE, &mii);
        DrawMenuBar(hwnd);

        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), 0);
        if (app->ClipboardCounts().morphs != 0 ||
            app->ClipboardCounts().displays != 0) {
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), 1);
            if (app->ClipboardCounts().bones != 0) {
                EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), 1);
                PostLanguageSweep2(app);                      // 0x40D070
                return;
            }
        } else if (app->ClipboardCounts().bones != 0) {
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), 1);
            EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), 1);
            PostLanguageSweep2(app);                          // 0x40D070
            return;
        }
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), 0);
        PostLanguageSweep2(app);                          // 0x40D070
    } else {
        void* oldModel = app->ModelSlot(slot);
        if (oldModel != nullptr) {
            ModelDispose(static_cast<unsigned char*>(oldModel));    // 0x48F830
            ::operator delete(oldModel);
            app->ModelSlot(slot) = nullptr;
        }
    }
}

}  // namespace mikudancestudio
