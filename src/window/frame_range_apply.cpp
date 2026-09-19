// ===========================================================================
// VA 0x0043E970 - model frame-range scale/apply
// ===========================================================================
// The dialog supplies an inclusive range [start,end] and a positive scale.
// Every selected family is rebuilt through the original registrar:
//   688 bone keys, 689 morph keys, 690 model display/IK keys.
// Keys inside the range are scaled around start; keys after end are shifted by
// the range's length delta.  Bone edits use MMD's paired type-4/type-2 undo
// transaction, while morph/display follow the original no-extra-ring path.
// Reference: IDA v9.32 disassembly/decompile 0x43E970..0x4403AE.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// Porting-era trace under MIKUDANCESTUDIO_STATE_DUMP_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void TraceRange(const char* stage, int value = -1) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\frame_range_trace.log", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    if (value >= 0)
        std::fprintf(stream, "%s=%d\n", stage, value);
    else
        std::fprintf(stream, "%s\n", stage);
    std::fclose(stream);
}
#else
inline void TraceRange(const char*, int = -1) {}
#endif

template <typename T>
T& At(unsigned char* base, std::size_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

// x64 sub_7FF7CB4BD470 computes both frame maps in SSE single precision:
//   (int)(float)((float)(int)(frame - start) * (float)scale)
// i.e. cvtsi2ss -> mulss (round-to-nearest float product) -> cvttss2si.
// A double product (the x87 shape of the x86 build) diverges by one frame
// at common values (e.g. scale 1.05, diff 20: float rounds 20.9999990 up
// to 21.0f and truncates to 21, double truncates to 20), so the multiply
// and the truncation must both stay in float.
std::uint32_t ScaledRelativeFrame(std::uint32_t frame,
                                  std::uint32_t start,
                                  std::uint32_t end,
                                  double scale) {
    const float f = static_cast<float>(scale);
    if (frame <= end) {
        const float m = static_cast<float>(
            static_cast<std::int32_t>(frame - start)) * f;
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(m));
    }
    const float m = static_cast<float>(
        static_cast<std::int32_t>(end - start)) * f;
    return frame + static_cast<std::uint32_t>(static_cast<std::int32_t>(m)) -
           end;
}

template <typename Key>
int TrackOwner(const Key* keys, int index, int rootCount) {
    while (index >= rootCount)
        index = static_cast<int>(keys[index].previous);
    return index;
}

void ClearBoneMarks(mdl::BoneKey* keys) {
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i)
        keys[i].allocated = 0;
}

void ClearMorphMarks(mdl::MorphKey* keys) {
    for (int i = 0; i < 20000; ++i)
        keys[i].allocated = 0;
}

void ClearDisplayMarks(mdl::DisplayKey* keys) {
    for (int i = 0; i < 1000; ++i)
        keys[i].allocated = 0;
}

struct DisplayCopy {
    std::uint32_t frame{};
    unsigned char view{};
    std::vector<unsigned char> ik;
    std::vector<unsigned char> relations;
};

void ScaleBoneKeys(MMDApp* app, unsigned char* model, HWND hDlg,
                   std::uint32_t start, std::uint32_t end, double scale) {
    if (IsDlgButtonChecked(hDlg, panel::kScaleBoneCheckbox) != BST_CHECKED)
        return;

    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    mdl::BoneRecord* const bones = mdl::Bones(model);
    const int boneCount = static_cast<int>(mdl::Mdl(model)->boneCount);

    int count = 0;
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) {
        mdl::BoneKey& key = keys[i];
        key.allocated = 0;
        const std::uint32_t frame = key.frame;
        if ((frame != 0 || i < boneCount) && frame >= start) {
            key.allocated = 1;
            ++count;
        }
    }
    if (count == 0)
        return;
    TraceRange("bone.count", count);

    ClearMorphMarks(morphKeys);
    ClearDisplayMarks(displayKeys);

    std::vector<std::array<unsigned char, 84>> copies(
        static_cast<std::size_t>(count));
    int outIndex = 0;
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) {
        const mdl::BoneKey& key = keys[i];
        if (key.allocated == 0)
            continue;
        auto& out = copies[static_cast<std::size_t>(outIndex++)];
        const int owner = TrackOwner(keys, i, boneCount);
        strcpy_s(reinterpret_cast<char*>(out.data()), 30,
                 bones[owner].name);
        At<std::uint32_t>(out.data(), 32) = ScaledRelativeFrame(
            key.frame, start, end, scale);
        std::memcpy(out.data() + 36, key.rotation, sizeof(key.rotation));
        std::memcpy(out.data() + 52, key.position, sizeof(key.position));
        out[64] = key.physicsDisabled;
        std::memcpy(out.data() + 65, key.interpolation,
                    sizeof(key.interpolation));
    }

    // Deleting the marked originals creates the first type-2 snapshot.
    // The inline block turns it into type 4 and opens the paired insertion
    // snapshot before the registrar starts touching the new records.
    DeleteMarkedKeyframes(app);
    TraceRange("bone.deleted");
    BeginRangeScaleBoneUndo(model, app->CurrentFrame(), count);
    TraceRange("bone.undo_open");
    ResetBoneKeyCursor(model);
    for (auto& copy : copies) {
        if (!RegisterBoneKey(model, copy.data(), static_cast<int>(start), 0))
            break;
    }
    TraceRange("bone.inserted");
}

void ScaleMorphKeys(MMDApp* app, unsigned char* model, HWND hDlg,
                    std::uint32_t start, std::uint32_t end, double scale) {
    if (IsDlgButtonChecked(hDlg, panel::kScaleMorphCheckbox) != BST_CHECKED)
        return;

    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    mdl::MorphKey* const keys = mdl::MorphKeys(model);
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    mdl::MorphRecord* const morphs = mdl::Morphs(model);
    const int morphCount = static_cast<int>(mdl::Mdl(model)->morphCount);

    int count = 0;
    for (int i = 0; i < 20000; ++i) {
        mdl::MorphKey& key = keys[i];
        key.allocated = 0;
        const std::uint32_t frame = key.frame;
        if ((frame != 0 || i < morphCount) && frame >= start) {
            key.allocated = 1;
            ++count;
        }
    }
    if (count == 0)
        return;
    TraceRange("morph.count", count);

    ClearBoneMarks(boneKeys);
    ClearDisplayMarks(displayKeys);

    std::vector<std::array<unsigned char, 40>> copies(
        static_cast<std::size_t>(count));
    int outIndex = 0;
    for (int i = 0; i < 20000; ++i) {
        const mdl::MorphKey& key = keys[i];
        if (key.allocated == 0)
            continue;
        auto& out = copies[static_cast<std::size_t>(outIndex++)];
        const int owner = TrackOwner(keys, i, morphCount);
        strcpy_s(reinterpret_cast<char*>(out.data()), 30,
                 morphs[owner].name);
        At<std::uint32_t>(out.data(), 32) = ScaledRelativeFrame(
            key.frame, start, end, scale);
        std::memcpy(out.data() + 36, &key.value, sizeof(key.value));
    }

    DeleteMarkedKeyframes(app);
    TraceRange("morph.deleted");
    ResetMorphKeyCursor(model);
    for (const auto& copy : copies) {
        if (!RegisterMorphKeyFromRecord(model, copy.data(), static_cast<int>(start)))
            break;
    }
    TraceRange("morph.inserted");
}

void ScaleDisplayKeys(MMDApp* app, unsigned char* model, HWND hDlg,
                      std::uint32_t start, std::uint32_t end, double scale) {
    if (IsDlgButtonChecked(hDlg, panel::kScaleDispIkCheckbox) != BST_CHECKED)
        return;

    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    mdl::DisplayKey* const keys = mdl::DisplayKeys(model);
    mdl::BoneRecord* const bones = mdl::Bones(model);
    mdl::IkChain* const ikChains = mdl::IkChains(model);
    const mdl::BoneOrderEntry* const selectors = mdl::BoneOrder(model);
    const int ikCount = static_cast<int>(mdl::Mdl(model)->ikChainCount);
    const int relationCount =
        static_cast<int>(mdl::Mdl(model)->boneOrderCount);

    int count = 0;
    for (int i = 0; i < 1000; ++i) {
        mdl::DisplayKey& key = keys[i];
        key.allocated = 0;
        const std::uint32_t frame = key.frame;
        if (frame >= start && frame != 0) {
            key.allocated = 1;
            ++count;
        }
    }
    if (count == 0)
        return;
    TraceRange("display.count", count);

    ClearBoneMarks(boneKeys);
    ClearMorphMarks(morphKeys);

    std::vector<DisplayCopy> copies;
    copies.reserve(static_cast<std::size_t>(count));
    for (int keyIndex = 0; keyIndex < 1000; ++keyIndex) {
        const mdl::DisplayKey& key = keys[keyIndex];
        if (key.allocated == 0)
            continue;

        DisplayCopy copy;
        copy.frame = ScaledRelativeFrame(key.frame, start, end, scale);
        copy.view = key.visible;
        copy.ik.resize(static_cast<std::size_t>(ikCount) * 21);
        const unsigned char* const keyIk = mdl::IkStates(key);
        for (int i = 0; i < ikCount; ++i) {
            unsigned char* const out = copy.ik.data() + 21 * i;
            const int bone = ikChains[i].boneIndex;
            strcpy_s(reinterpret_cast<char*>(out), 20,
                     bones[bone].name);
            out[20] = keyIk[i];
        }

        copy.relations.resize(static_cast<std::size_t>(relationCount) * 28);
        const mdl::BoneReference* const keyRelations =
            mdl::SelectorStates(key);
        for (int i = 0; i < relationCount; ++i) {
            unsigned char* const out = copy.relations.data() + 28 * i;
            if (i != 0) {
                const int bone = selectors[i].boneIndex;
                strcpy_s(reinterpret_cast<char*>(out), 20,
                         bones[bone].name);
            }
            At<std::int32_t>(out, 20) = keyRelations[i].modelIndex;
            At<std::int32_t>(out, 24) = keyRelations[i].boneIndex;
        }
        copies.push_back(std::move(copy));
    }

    DeleteMarkedKeyframes(app);
    TraceRange("display.deleted");
    ResetDisplayKeyCursor(model);
    for (const auto& copy : copies) {
        if (!RegisterDisplayKeyFromRecord(model, static_cast<int>(copy.frame), copy.view,
                       ikCount, copy.ik.data(), relationCount,
                       copy.relations.data(), static_cast<int>(start)))
            break;
    }
    TraceRange("display.inserted");
}

}  // namespace

// VA 0x0043E970 - read the dialog's start/end/scale fields
// and re-register the selected key families through the scaled range.
void ApplyFrameRangeScale(HWND hDlg) {
    TraceRange("entered");
    MMDApp* const app = g_Block;
    if (app == nullptr)
        return;
    unsigned char* const model = app->SelectedModel();
    if (model == nullptr)
        return;

    char text[256]{};
    GetWindowTextA(GetDlgItem(hDlg, panel::kScaleFromEdit), text, 8);
    const std::int32_t startSigned = std::atol(text);
    GetWindowTextA(GetDlgItem(hDlg, panel::kScaleToEdit), text, 8);
    const std::int32_t endSigned = std::atol(text);
    TraceRange("start", startSigned);
    TraceRange("end", endSigned);
    if (endSigned - startSigned <= 0)
        return;

    // (between modelOffsetZ and morphFrameShift), no state member yet.
    app->FrameRangeStartFrame() = startSigned;
    GetWindowTextA(GetDlgItem(hDlg, panel::kScaleRateEdit), text, 8);
    const double scale = std::atof(text);
    TraceRange("scale_x1000", static_cast<int>(scale * 1000.0));
    if (scale < 0.0000099999997 || scale == 1.0)
        return;

    const std::uint32_t start = static_cast<std::uint32_t>(startSigned);
    const std::uint32_t end = static_cast<std::uint32_t>(endSigned);
    TraceRange("begin");
    ScaleBoneKeys(app, model, hDlg, start, end, scale);
    ScaleMorphKeys(app, model, hDlg, start, end, scale);
    ScaleDisplayKeys(app, model, hDlg, start, end, scale);
    TraceRange("families_done");

    const std::uint32_t modelMax = mdl::Mdl(model)->maxFrame;
    if (app->LastRegisteredFrame() < modelMax)
        app->LastRegisteredFrame() = modelMax;
    SeekModelFrame(model, app->CurrentFrame(),
              app->PlaybackPhysicsMode());
    PanelPaint(app);
    SelectionReeval(app);
    app->SceneModified() = 1;
    TraceRange("done");
}

}  // namespace mikudancestudio
