// Windows/application adapter for VMD loading. The binary grammar and bounds
// checks live in vmd_format.cpp; this file applies a parsed motion to the
// original app/model records and keeps the UI and undo behavior.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <sys/stat.h>
#include <vector>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/vmd_format.hpp"
#include "mikudancestudio/clipboard_layout.hpp"
#include "mikudancestudio/vmd_track_apply.hpp"

namespace mikudancestudio {
void SelectionReeval(MMDApp* app);
void RefreshLightPanel(MMDApp* app);
void RefreshSelfShadowPanel(MMDApp* app);
void ApplyGravityTrack(MMDApp* app);

namespace {

const char kCameraLightName[] = "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe";
const char kJpTitleMotionLoad[] = "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b\x83\x5e"
                                  "\x93\xc7\x8d\x9e";
const char kJpNotVmd[] = "\x82\xb1\x82\xcc\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd\x20Vocaloid "
                         "Motion Data file \x82\xc5\x82\xcd\x82\xa0\x82\xe8\x82\xdc\x82\xb9"
                         "\x82\xf1";
const char kJpCannotOpen[] = "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x93\xc7\x82\xdd\x8d\x9e"
                             "\x82\xdf\x82\xdc\x82\xb9\x82\xf1:%d";
const char kJpNoCamLight[] = "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
                             "\x81\x5b\x83\x5e\x82\xc9\x82\xcd\x83\x4a\x83\x81\x83\x89\x81\x45"
                             "\x8f\xc6\x96\xbe\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xcd\x82\xa0"
                             "\x82\xe8\x82\xdc\x82\xb9\x82\xf1";
const char kJpNoCamLightShadow[] =
    "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
    "\x81\x5b\x83\x5e\x82\xc9\x82\xcd\x81\x41\x83\x4a\x83\x81\x83\x89"
    "\x81\x45\x8f\xc6\x96\xbe\x81\x45\x83\x5a\x83\x8b\x83\x74\x89\x65"
    "\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xcd\x8a\xdc\x82\xdc\x82\xea"
    "\x82\xdc\x82\xb9\x82\xf1";
const char kJpIsCamLight[] = "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
                             "\x81\x5b\x83\x5e\x82\xcd\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6"
                             "\x96\xbe\x97p\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xc5\x82\xb7\n\n"
                             "\x83\x4a\x83\x81\x83\x89\x95\xd2\x8f\x57\x83\x82\x81\x5b\x83\x68"
                             "\x82\xc5\x93\xc7\x82\xdd\x8d\x9e\x82\xf1\x82\xc5\x89\xba\x82\xb3"
                             "\x82\xa2";
const char kJpModelMismatch[] = "\x82\xb1\x82\xcc\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd\x20%s\x97"
                                "p\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b"
                                "\x83\x5e\x82\xc5\x82\xb7\n\x93\xaf\x82\xb6\x83\x7b\x81\x5b\x83\x93"
                                "\x96\xbc\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x82\xcc"
                                "\x82\xdd\x82\xf0\x8e\xe6\x82\xe8\x8d\x9e\x82\xde\x8e\x96\x82\xc9"
                                "\x82\xc8\x82\xe8\x82\xdc\x82\xb7\n\x91\xb1\x8d\x73\x82\xb5\x82\xdc"
                                "\x82\xb7\x82\xa9\x81\x48";

template <std::size_t N> void CopyName(char* destination, const std::array<char, N>& name) {
    std::memset(destination, 0, 30);
    std::memcpy(destination, name.data(), name.size());
    if (auto* newline = std::strchr(reinterpret_cast<char*>(destination), '\n'))
        *newline = 0;
}

} // namespace

int LoadVmdMotion(MMDApp* app, const wchar_t* fileName) {
    auto& s = *app;
    const HWND hwnd = s.state.hwnd;
    const bool english = s.state.englishUI != 0;
    int fd;
    const errno_t openError = _wsopen_s(&fd, fileName, 0x8000 /* _O_BINARY */,
                                        0x40 /* _SH_DENYNO */, 0x80 /* _S_IREAD */);
    if (openError != 0) {
        char text[256];
        if (english)
            sprintf_s(text, sizeof text, "Cannot open file:%d", openError);
        else
            sprintf_s(text, sizeof text, kJpCannotOpen, openError);
        MessageBoxA(hwnd, text, "", 0);
        return 0;
    }
    std::FILE* stream = _fdopen(fd, "rb");
    if (stream == nullptr) {
        _close(fd);
        return 0;
    }
    format::VmdDocument motion;
    const bool valid = format::ReadVmd(stream, motion);
    std::fclose(stream);
    if (!valid) {
        MessageBoxA(hwnd, english ? "This is not Vocaloid Motion Data file !!" : kJpNotVmd,
                    english ? "load motion file" : kJpTitleMotionLoad, 0);
        return 0;
    }
    char name[21]{};
    std::memcpy(name, motion.modelName.data(), motion.modelName.size());
    mdl::ModelRecord* const model = app->SelectedModel();
    if (s.state.optflag[0] != 0) {
        if (!motion.hasCameraSection) {
            MessageBoxA(hwnd,
                        english ? "This motion data has not camera/light data !!" : kJpNoCamLight,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            return 0;
        }
        if (motion.version == format::VmdVersion::V2 &&
            std::memcmp(name, kCameraLightName, 13) != 0) {
            MessageBoxA(hwnd,
                        english ? "This motion file has not cammera,light,shadow data."
                                : kJpNoCamLightShadow,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            return 0;
        }
        ApplyVmdGlobalTracks(app, motion);
        PanelPaint(app);
        SelectionReeval(app);
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        return 0;
    }

    if (model == nullptr)
        return 0;
    if (std::strcmp(name, model->name) != 0) {
        if (std::memcmp(name, kCameraLightName, 13) == 0) {
            MessageBoxA(hwnd, english ? "This motion data if camera/light data !!" : kJpIsCamLight,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            return 0;
        }
        char message[256];
        if (english)
            sprintf_s(message, sizeof message,
                      "This motion file is the data for '%s'.\n"
                      "You can regist the motion only same bone name.\n"
                      "Are you OK?",
                      name);
        else
            sprintf_s(message, sizeof message, kJpModelMismatch, name);
        if (MessageBoxA(hwnd, message, english ? "load motion data" : kJpTitleMotionLoad,
                        MB_OKCANCEL) != IDOK)
            return 0;
    }

    mdl::BoneKey* const boneKeys = (model)->boneKeys;
    mdl::MorphKey* const morphKeys = (model)->morphKeys;
    mdl::DisplayKey* const displayKeys = (model)->displayKeys;
    for (unsigned i = 0; i < mdl::kBoneKeyCapacity; ++i)
        boneKeys[i].allocated = 0;
    for (unsigned i = 0; i < mdl::kMorphKeyCapacity; ++i)
        morphKeys[i].allocated = 0;
    for (unsigned i = 0; i < mdl::kDisplayKeyCapacity; ++i)
        displayKeys[i].allocated = 0;

    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    mdl::ModelRecord& record = *model;
    record.undoDirty = 1;
    record.redoDirty = 0;
    std::uint32_t& ringIdx = record.undoState[0];
    if (++ringIdx >= 30)
        ringIdx = 0;
    record.undoState[1] = ringIdx;
    auto& undo = record.undoRings[0].slots[ringIdx];
    undo.operation = mdl::UndoOperation::BoneKeys;
    undo.recordCount = 0;
    undo.frame = s.state.currentFrame;
    if (undo.bonePose != nullptr)
        operator delete(undo.bonePose);
    const int boneCount = static_cast<int>(record.boneCount);
    auto* const snapshot = static_cast<mdl::BonePoseSnapshot*>(operator new(
        boneCount * sizeof(mdl::BonePoseSnapshot)));
    undo.bonePose = snapshot;
    std::memset(snapshot, 0, static_cast<std::size_t>(boneCount) * sizeof *snapshot);
    if (boneCount > 0) {
        const mdl::BoneRecord* bones = (model)->boneTable;
        const unsigned char* flags = record.bonePhysicsState;
        for (int i = 0; i < boneCount; ++i) {
            snapshot[i].boneIndex = i;
            std::memcpy(snapshot[i].position, bones[i].trans, sizeof snapshot[i].position);
            std::memcpy(snapshot[i].rotation, bones[i].rotQuat, sizeof snapshot[i].rotation);
            snapshot[i].poseEdited = flags[i];
        }
    }
    if (undo.keySnapshots != nullptr)
        operator delete(undo.keySnapshots);
    const std::size_t rawSize = 192 * motion.bones.size();
    unsigned char* rawKeys = static_cast<unsigned char*>(operator new(rawSize));
    undo.keySnapshots = reinterpret_cast<mdl::BoneKeyUndoEntry*>(rawKeys);
    std::memset(rawKeys, 0, rawSize);
    std::memset(record.keyVisitMap, 0, sizeof record.keyVisitMap);
    ResetBoneKeyCursor(model);

    bool boneOk = true;
    for (const format::VmdBoneKey& key : motion.bones) {
        mdl::BoneClipboardRecord rec{};
        CopyName(rec.name, key.name);
        rec.frame = key.frame;
        std::memcpy(rec.rotation, key.rotation, sizeof rec.rotation);
        std::memcpy(rec.position, key.position, sizeof rec.position);
        rec.physicsDisabled = key.interpolation[2] == 0x63 && key.interpolation[3] == 0x0F ? 1 : 0;
        for (int channel = 0; channel < 4; ++channel) {
            rec.interpolation[0 + channel] = key.interpolation[channel * 16];
            rec.interpolation[4 + channel] = key.interpolation[channel * 16 + 4];
            rec.interpolation[8 + channel] = key.interpolation[channel * 16 + 8];
            rec.interpolation[12 + channel] = key.interpolation[channel * 16 + 12];
        }
        if (boneOk && !RegisterBoneKey(model, rec, s.state.currentFrame, 0))
            boneOk = false;
    }
    ResetMorphKeyCursor(model);
    bool morphOk = true;
    for (const format::VmdMorphKey& key : motion.morphs) {
        mdl::MorphClipboardRecord rec{};
        CopyName(rec.name, key.name);
        rec.frame = key.frame;
        const float value = _finite(key.value) ? key.value : 0.0f;
        rec.value = value;
        if (morphOk && !RegisterMorphKeyFromRecord(model, rec, s.state.currentFrame))
            morphOk = false;
    }
    if (s.LastRegisteredFrame() < record.maxFrame)
        s.LastRegisteredFrame() = record.maxFrame;

    if (!motion.ik.empty()) {
        ResetDisplayKeyCursor(model);
        bool ikOk = true;
        for (const format::VmdIkKey& key : motion.ik) {
            std::vector<mdl::IkClipboardState> entries(key.entries.size());
            for (std::size_t i = 0; i < key.entries.size(); ++i) {
                std::memcpy(entries[i].boneName, key.entries[i].name.data(), 20);
                entries[i].enabled = key.entries[i].enabled != 0 ? 1 : 0;
            }
            if (ikOk && !RegisterDisplayKeyFromRecord(model, static_cast<int>(key.frame),
                                                      key.visible != 0 ? 1 : 0,
                                                      static_cast<int>(key.entries.size()),
                                                      entries.empty() ? nullptr : entries.data(), 0,
                                                      nullptr, s.state.currentFrame))
                ikOk = false;
        }
        if (s.LastRegisteredFrame() < record.maxFrame)
            s.LastRegisteredFrame() = record.maxFrame;
    }
    PanelPaint(app);
    SelectionReeval(app);
    return SeekModelFrame(*model, s.state.currentFrame, s.PlaybackPhysicsMode());
}

void LoadVmdFile(const wchar_t* path) {
    if (g_Block != nullptr)
        LoadVmdMotion(g_Block, path);
}

} // namespace mikudancestudio
