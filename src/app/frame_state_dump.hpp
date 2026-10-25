// ===========================================================================
// Porting-era diagnostics: whole-app state snapshots under
// MIKUDANCESTUDIO_STATE_DUMP_DIR, used to diff the port against the original
// binaries.  Compiled only with -DMIKUDANCESTUDIO_DIAG (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stubs below keep the call
// sites valid and inline away to nothing.
// ===========================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {

#ifdef MIKUDANCESTUDIO_DIAG

inline void DumpFrameEntryState(MMDApp* app, const char* fileName = "frame_state.json",
                                bool oneShot = true) {
    static LONG entryDumped = 0;
    static LONG lineDumped = 0;
    LONG* dumped = std::strcmp(fileName, "frame_state_line.json") == 0 ? &lineDumped : &entryDumped;
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH ||
        (oneShot && InterlockedCompareExchange(dumped, 1, 0) != 0))
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\%s", directory, fileName);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;

    // Diagnostic-only legacy-offset dump: JSON keys stay frozen to the
    // original x86 offsets.  Reads are arch-native now (the xlate table is
    // gone) - on x86 they hit the pinned members; on x64 the numbered keys
    // report that architecture's blob at the same address, exactly as the
    // old identity fallback did for unmapped offsets.
    const unsigned char* blob = reinterpret_cast<const unsigned char*>(&app->state);
    const auto u8 = [blob](std::size_t offset) { return static_cast<unsigned>(blob[offset]); };
    const auto i32 = [blob](std::size_t offset) {
        return *reinterpret_cast<const std::int32_t*>(blob + offset);
    };
    const auto bits = [blob](std::size_t offset) {
        return *reinterpret_cast<const std::uint32_t*>(blob + offset);
    };
    const std::size_t byteOffsets[] = {
        0xC8,    0x2F8,   0x2F9,   0x2FA,   0x2FB,   0x2FC,   0x2FD,   0x2FE,   0x31C,   0x31D,
        0x31E,   0x330,   0x340,   0x341,   0x342,   0x910,   0x918,   0x9ED90, 0x9ED98, 0x9ED99,
        0x9ED9A, 0x9EDB5, 0x9EDB6, 0x9EDD1, 0xA0194, 0xA0CC8, 0xA0D68, 0xA4420};
    const std::size_t intOffsets[] = {
        0x4,     0x8,     0xC,     0x10,    0x84,    0x324,   0x328,   0x32C,   0x344,
        0x34C,   0x914,   0x91C,   0x920,   0x924,   0x92C,   0x97C,   0x980,   0x9E1CC,
        0x9DA24, 0x9DA28, 0x9DA2C, 0x9DA30, 0x9DA34, 0x9DA38, 0x9DA3C, 0x9DA40, 0x9DA44,
        0x9EB80, 0x9EB84, 0x9F124, 0xA0430, 0xA0434, 0xA06C8, 0xA0B74, 0xA0CC4, 0xA0D6C};
    const std::size_t floatOffsets[] = {0x308, 0x30C, 0x310, 0x314,   0x318,   0x320,
                                        0x334, 0x338, 0x33C, 0x9E1E8, 0xA08DC, 0xA4428};

    std::fputs("{\n  \"schema\": 1,\n  \"app\": {\n", stream);
    bool first = true;
    const auto comma = [&]() {
        if (!first)
            std::fputs(",\n", stream);
        first = false;
    };
    for (std::size_t offset : byteOffsets) {
        comma();
        std::fprintf(stream, "    \"%06X.u8\": %u", unsigned(offset), u8(offset));
    }
    for (std::size_t offset : intOffsets) {
        comma();
        std::fprintf(stream, "    \"%06X.i32\": %d", unsigned(offset), i32(offset));
    }
    for (std::size_t offset : floatOffsets) {
        comma();
        std::fprintf(stream, "    \"%06X.f32_bits\": \"%08X\"", unsigned(offset), bits(offset));
    }
    const std::int32_t* rect = reinterpret_cast<const std::int32_t*>(&app->ViewportRect());
    comma();
    std::fprintf(stream, "    \"0A0D40.rect\": [%d, %d, %d, %d]", rect[0], rect[1], rect[2],
                 rect[3]);
    comma();
    std::fprintf(stream, "    \"0A0D38.hwnd\": \"%08X\"",
                 static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(app->FloatingWindow())));
    comma();
    std::fprintf(stream, "    \"0A06B8.hwnd\": \"%08X\"",
                 static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(app->Hwnd())));
    // Keep the JSON keys, but read native records (24/40 bytes on x86/x64)
    // and print complete pointers so x64 snapshots remain usable.
    const auto* modelDisplayClipboard = app->DisplayClipboard();
    const int modelDisplayClipboardCount = static_cast<int>(app->ClipboardCounts().displays);
    comma();
    std::fprintf(stream, "    \"clipboard.model_display.ptr\": \"%p\"",
                 static_cast<void*>(app->DisplayClipboard()));
    comma();
    std::fputs("    \"clipboard.model_display.records\": [", stream);
    if (modelDisplayClipboard != nullptr && modelDisplayClipboardCount > 0 &&
        modelDisplayClipboardCount <= 10000) {
        for (int recordIndex = 0; recordIndex < modelDisplayClipboardCount; ++recordIndex) {
            if (recordIndex != 0)
                std::fputs(", ", stream);
            const auto& record = modelDisplayClipboard[recordIndex];
            std::fprintf(stream,
                         "{\"frame\":%u,\"view\":%u,\"ik_count\":%d,"
                         "\"ik_ptr\":\"%p\",\"relation_count\":%d,"
                         "\"relation_ptr\":\"%p\"}",
                         record.frame, unsigned(record.visible), record.ikCount,
                         static_cast<void*>(record.ikStates), record.selectorCount,
                         static_cast<void*>(record.selectorStates));
        }
    }
    std::fputc(']', stream);
    RECT mainClient{};
    RECT mainWindow{};
    const HWND mainHwnd = static_cast<HWND>(app->Hwnd());
    GetClientRect(mainHwnd, &mainClient);
    GetWindowRect(mainHwnd, &mainWindow);
    comma();
    std::fprintf(stream, "    \"main.client_rect\": [%ld, %ld, %ld, %ld]", mainClient.left,
                 mainClient.top, mainClient.right, mainClient.bottom);
    comma();
    std::fprintf(stream, "    \"main.window_rect\": [%ld, %ld, %ld, %ld]", mainWindow.left,
                 mainWindow.top, mainWindow.right, mainWindow.bottom);
    D3DRenderer* render = app->Renderer(); // app+0xA06C4
    std::uint32_t overlayScaleBits = 0;
    float overlayScale = 0.0f;
    std::int32_t backbufferWidth = 0;
    std::int32_t backbufferHeight = 0;
    if (render != nullptr) {
        // dump byte layout is frozen: read the members, memcpy to the
        // (bit-preserving) local snapshot below exactly as before
        std::memcpy(&backbufferWidth, &render->screenWidth,
                    sizeof(backbufferWidth)); // wrapper+120036 (0x1D4E4)
        std::memcpy(&backbufferHeight, &render->screenHeight,
                    sizeof(backbufferHeight)); // wrapper+120040 (0x1D4E8)
        std::memcpy(&overlayScaleBits, &render->viewScale,
                    sizeof(overlayScaleBits)); // wrapper+120048 (0x1D4F0)
        std::memcpy(&overlayScale, &render->viewScale,
                    sizeof(overlayScale)); // wrapper+120048 (0x1D4F0)
    }
    comma();
    std::fprintf(stream, "    \"render.01D4E4.i32\": %d", backbufferWidth);
    comma();
    std::fprintf(stream, "    \"render.01D4E8.i32\": %d", backbufferHeight);
    comma();
    std::fprintf(stream, "    \"render.01D4F0.f32_bits\": \"%08X\"", overlayScaleBits);
    comma();
    std::fprintf(stream, "    \"render.01D4F0.f32\": %.17g", static_cast<double>(overlayScale));
    const std::size_t matrixOffsets[] = {0xA0438, 0xA0674};
    for (std::size_t offset : matrixOffsets) {
        comma();
        std::fprintf(stream, "    \"%06X.matrix_bits\": [", unsigned(offset));
        for (int i = 0; i < 16; ++i) {
            if (i != 0)
                std::fputs(", ", stream);
            std::fprintf(stream, "\"%08X\"", bits(offset + 4 * i));
        }
        std::fputc(']', stream);
    }
    std::fputs("\n  },\n  \"models\": [", stream);

    bool firstModel = true;
    for (unsigned slot = 0; slot < static_cast<unsigned>(kModelSlotCount); ++slot) {
        unsigned char* model = app->ModelSlot(slot);
        if (model == nullptr)
            continue;
        if (!firstModel)
            std::fputc(',', stream);
        firstModel = false;
        const mdl::ModelRecord& state = *mdl::Mdl(model);
        const int boneCount = static_cast<int>(state.boneCount);
        const auto* bones = mdl::Bones(model);
        auto* active = mikudancestudio::mdl::Mdl(model)->boneSelection;
        auto* secondary = mikudancestudio::mdl::Mdl(model)->bonePhysicsState;
        int activeCount = 0;
        int secondaryCount = 0;
        if (boneCount >= 0 && boneCount <= 100000) {
            for (int i = 0; i < boneCount; ++i) {
                if (active != nullptr && active[i] != 0)
                    ++activeCount;
                if (secondary != nullptr && secondary[i] != 0)
                    ++secondaryCount;
            }
        }
        const int undoCursor = mdl::Mdl(model)->UndoCursor();
        const int undoCurrent = mdl::Mdl(model)->LatestUndoCursor();
        const mdl::UndoRecord* undo = undoCursor >= 0 && undoCursor < 30
                                          ? &mdl::Mdl(model)->undoRings[0].slots[undoCursor]
                                          : nullptr;
        std::fprintf(stream,
                     "\n    {\"slot\": %u, \"order\": %u, \"order_copy\": %u, "
                     "\"visible\": %u, \"morph_count\": %d, \"bone_count\": %d, "
                     "\"relationship_count\": %d, \"selector_count\": %d, "
                     "\"selected_bone\": %d, "
                     "\"active_nonzero\": %d, \"secondary_nonzero\": %d, "
                     "\"max_frame\": %d, \"undo_cursor\": %d, "
                     "\"undo_current\": %d, \"undo_dirty\": %u, "
                     "\"undo_redo\": %u, \"undo_type\": %d, "
                     "\"undo_count\": %d, \"undo_frame\": %d",
                     slot, unsigned(state.comboSelIndex), unsigned(state.comboSelIndex2),
                     unsigned(state.loadComplete), static_cast<int>(state.morphCount), boneCount,
                     static_cast<int>(state.ikChainCount), static_cast<int>(state.boneOrderCount),
                     static_cast<int>(state.selectedBone), activeCount, secondaryCount,
                     static_cast<int>(state.maxFrame), undoCursor, undoCurrent,
                     unsigned(state.undoDirty), unsigned(state.redoDirty),
                     undo != nullptr ? static_cast<int>(undo->operation) : -1,
                     undo != nullptr ? undo->recordCount : -1,
                     undo != nullptr ? static_cast<int>(undo->frame) : -1);
        std::fputs(", \"undo_ring\": [", stream);
        for (int undoIndex = 0; undoIndex < 30; ++undoIndex) {
            if (undoIndex != 0)
                std::fputs(", ", stream);
            const auto& entry = mdl::Mdl(model)->undoRings[0].slots[undoIndex];
            std::fprintf(stream,
                         "{\"index\":%d,\"type\":%d,\"count\":%d,"
                         "\"frame\":%d}",
                         undoIndex, static_cast<int>(entry.operation), entry.recordCount,
                         static_cast<int>(entry.frame));
        }
        std::fputc(']', stream);
        const int selected = static_cast<int>(state.selectedBone);
        std::fputs(", \"selected_bone_matrix_bits\": [", stream);
        if (bones != nullptr && selected >= 0 && selected < boneCount) {
            const auto* bone = &bones[selected];
            for (int i = 0; i < 16; ++i) {
                if (i != 0)
                    std::fputs(", ", stream);
                std::uint32_t value = 0;
                std::memcpy(&value, bone->matInit + i, sizeof(value));
                std::fprintf(stream, "\"%08X\"", value);
            }
        }
        std::fputs("], \"selected_bone_point_bits\": [", stream);
        if (bones != nullptr && selected >= 0 && selected < boneCount) {
            const auto* bone = &bones[selected];
            for (int i = 0; i < 3; ++i) {
                if (i != 0)
                    std::fputs(", ", stream);
                std::uint32_t value = 0;
                std::memcpy(&value, bone->position + i, sizeof(value));
                std::fprintf(stream, "\"%08X\"", value);
            }
        }
        std::fputs("], \"selected_key_bits\": [", stream);
        const mdl::BoneKey* boneKeys = mdl::BoneKeys(model);
        if (boneKeys != nullptr && selected >= 0 && selected < boneCount) {
            const auto* key = reinterpret_cast<const unsigned char*>(&boneKeys[selected]);
            for (int i = 0; i < 15; ++i) {
                if (i != 0)
                    std::fputs(", ", stream);
                std::uint32_t value = 0;
                std::memcpy(&value, key + 4 * i, 4);
                std::fprintf(stream, "\"%08X\"", value);
            }
        }
        std::fputs("], \"selected_key_chain\": [", stream);
        if (boneKeys != nullptr && selected >= 0 && selected < boneCount) {
            int keyIndex = selected;
            for (int chainIndex = 0; chainIndex < 64; ++chainIndex) {
                if (chainIndex != 0)
                    std::fputs(", ", stream);
                const auto* typedKey = &boneKeys[keyIndex];
                const auto* key = reinterpret_cast<const unsigned char*>(typedKey);
                std::fprintf(stream, "{\"index\":%d,\"bits\":[", keyIndex);
                for (int i = 0; i < 15; ++i) {
                    if (i != 0)
                        std::fputs(",", stream);
                    std::uint32_t value = 0;
                    std::memcpy(&value, key + 4 * i, 4);
                    std::fprintf(stream, "\"%08X\"", value);
                }
                std::fputs("]}", stream);
                const int next = static_cast<int>(typedKey->next);
                if (next == 0 || next < 0 || next >= static_cast<int>(mdl::kBoneKeyCapacity) ||
                    next == keyIndex)
                    break;
                keyIndex = next;
            }
        }
        std::fputs("], \"selected_morph_indices\": [", stream);
        for (int i = 0; i < 4; ++i) {
            if (i != 0)
                std::fputs(", ", stream);
            std::fprintf(stream, "%d", state.selectedMorphs[i]);
        }
        std::fputs("], \"selected_morph_key_chains\": [", stream);
        const mdl::MorphKey* morphKeysForChains = mdl::MorphKeys(model);
        const int morphCountForChains = static_cast<int>(state.morphCount);
        bool firstMorphChain = true;
        if (morphKeysForChains != nullptr) {
            for (int lane = 0; lane < 4; ++lane) {
                const int root = state.selectedMorphs[lane];
                if (root < 0 || root >= morphCountForChains)
                    continue;
                if (!firstMorphChain)
                    std::fputs(", ", stream);
                firstMorphChain = false;
                std::fprintf(stream, "{\"lane\":%d,\"root\":%d,\"keys\":[", lane, root);
                int keyIndex = root;
                for (int chainIndex = 0; chainIndex < 64; ++chainIndex) {
                    if (chainIndex != 0)
                        std::fputs(", ", stream);
                    const auto& key = morphKeysForChains[keyIndex];
                    std::uint32_t valueBits = 0;
                    std::memcpy(&valueBits, &key.value, sizeof valueBits);
                    std::fprintf(stream,
                                 "{\"index\":%d,\"frame\":%u,"
                                 "\"prev\":%d,\"next\":%d,"
                                 "\"value_bits\":\"%08X\",\"mark\":%u}",
                                 keyIndex, key.frame, static_cast<int>(key.previous),
                                 static_cast<int>(key.next), valueBits, unsigned(key.allocated));
                    const int next = static_cast<int>(key.next);
                    if (next == 0 || next < 0 || next >= static_cast<int>(mdl::kMorphKeyCapacity) ||
                        next == keyIndex)
                        break;
                    keyIndex = next;
                }
                std::fputs("]}", stream);
            }
        }
        std::fputs("], \"display_key_chain\": [", stream);
        const mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);
        if (displayKeys != nullptr) {
            int keyIndex = 0;
            for (int chainIndex = 0; chainIndex < 64; ++chainIndex) {
                if (chainIndex != 0)
                    std::fputs(", ", stream);
                const auto& key = displayKeys[keyIndex];
                std::fprintf(stream,
                             "{\"index\":%d,\"frame\":%u,\"prev\":%d,"
                             "\"next\":%d,\"view\":%u,\"mark\":%u,"
                             "\"ik_hex\":\"",
                             keyIndex, key.frame, static_cast<int>(key.previous),
                             static_cast<int>(key.next), unsigned(key.visible),
                             unsigned(key.allocated));
                const auto* ik = mdl::IkStates(key);
                const int ikCount = static_cast<int>(state.ikChainCount);
                if (ik != nullptr && ikCount >= 0 && ikCount <= 10000) {
                    for (int i = 0; i < ikCount; ++i)
                        std::fprintf(stream, "%02X", unsigned(ik[i]));
                }
                std::fputs("\",\"relation_bits\":[", stream);
                const auto* relations = mdl::SelectorStates(key);
                const int relationCount = static_cast<int>(mdl::Mdl(model)->boneOrderCount);
                if (relations != nullptr && relationCount >= 0 && relationCount <= 10000) {
                    for (int i = 0; i < relationCount; ++i) {
                        if (i != 0)
                            std::fputs(",", stream);
                        std::fprintf(stream, "\"%08X\",\"%08X\"",
                                     static_cast<std::uint32_t>(relations[i].modelIndex),
                                     static_cast<std::uint32_t>(relations[i].boneIndex));
                    }
                }
                std::fputs("]}", stream);
                const int next = static_cast<int>(key.next);
                if (next == 0 || next < 0 || next >= static_cast<int>(mdl::kDisplayKeyCapacity) ||
                    next == keyIndex)
                    break;
                keyIndex = next;
            }
        }
        std::fputs("], \"marked_morph_keys\": [", stream);
        const mdl::MorphKey* morphKeys = mdl::MorphKeys(model);
        bool firstMorphKey = true;
        if (morphKeys != nullptr) {
            for (int keyIndex = 0; keyIndex < static_cast<int>(mdl::kMorphKeyCapacity);
                 ++keyIndex) {
                const auto& key = morphKeys[keyIndex];
                if (key.allocated == 0 && key.frame != 10)
                    continue;
                if (!firstMorphKey)
                    std::fputs(", ", stream);
                firstMorphKey = false;
                std::fprintf(
                    stream,
                    "{\"index\":%d,\"frame\":%u,\"prev\":%d,"
                    "\"next\":%d,\"value_bits\":\"%08X\","
                    "\"mark\":%u}",
                    keyIndex, key.frame, static_cast<int>(key.previous), static_cast<int>(key.next),
                    [&key] {
                        std::uint32_t bits;
                        std::memcpy(&bits, &key.value, sizeof bits);
                        return bits;
                    }(),
                    unsigned(key.allocated));
            }
        }
        std::fputs("], \"bones\": [", stream);
        if (bones != nullptr && boneCount >= 0 && boneCount <= 100000) {
            for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                if (boneIndex != 0)
                    std::fputs(", ", stream);
                const mdl::BoneRecord& bone = bones[boneIndex];
                std::fprintf(stream, "{\"index\":%d,\"name_hex\":\"", boneIndex);
                for (int i = 0; i < 20; ++i)
                    std::fprintf(stream, "%02X",
                                 unsigned(static_cast<unsigned char>(bone.name[i])));
                std::fprintf(stream,
                             "\",\"parent\":%d,\"linked\":%d,\"source\":%d,"
                             "\"layer\":%d,\"type\":%u,"
                             "\"filter_a\":%u,\"filter_b\":%u,\"flags\":%u,"
                             "\"swept\":%u,\"external_parent\":%d,"
                             "\"matrix_bits\":[",
                             bone.parent, bone.tailBone, bone.tailIdx, bone.layer,
                             unsigned(bone.type), unsigned(bone.hasRigidBody),
                             unsigned(bone.physicsDisabled), unsigned(bone.flags),
                             unsigned(bone.hasFlag), bone.slotIndex);
                for (int i = 0; i < 16; ++i) {
                    if (i != 0)
                        std::fputs(",", stream);
                    std::uint32_t value = 0;
                    std::memcpy(&value, bone.matInit + i, sizeof(value));
                    std::fprintf(stream, "\"%08X\"", value);
                }
                const float* matrixFields[] = {bone.matLocal, bone.matWorld, bone.matExtra};
                const char* matrixNames[] = {"local_matrix_bits", "parent_matrix_bits",
                                             "copy_matrix_bits"};
                for (int field = 0; field < 3; ++field) {
                    std::fprintf(stream, "],\"%s\":[", matrixNames[field]);
                    for (int i = 0; i < 16; ++i) {
                        if (i != 0)
                            std::fputs(",", stream);
                        std::uint32_t value = 0;
                        std::memcpy(&value, matrixFields[field] + i, sizeof(value));
                        std::fprintf(stream, "\"%08X\"", value);
                    }
                }
                std::fputs("],\"point_bits\":[", stream);
                for (int i = 0; i < 3; ++i) {
                    if (i != 0)
                        std::fputs(",", stream);
                    std::uint32_t value = 0;
                    std::memcpy(&value, bone.position + i, sizeof(value));
                    std::fprintf(stream, "\"%08X\"", value);
                }
                std::fputs("],\"secondary_point_bits\":[", stream);
                for (int i = 0; i < 3; ++i) {
                    if (i != 0)
                        std::fputs(",", stream);
                    std::uint32_t value = 0;
                    std::memcpy(&value, bone.tailOffset + i, sizeof(value));
                    std::fprintf(stream, "\"%08X\"", value);
                }
                const float* poseFields[] = {bone.trans,         bone.rotQuat,     bone.rotQuat2,
                                             bone.physicsOffset, bone.physicsQuat, bone.ikBackup,
                                             bone.ikBackup + 3};
                const int poseCounts[] = {3, 4, 4, 3, 4, 3, 4};
                const char* poseNames[] = {"pose_position_bits",       "pose_quaternion_bits",
                                           "ik_quaternion_bits",       "selected_position_bits",
                                           "selected_quaternion_bits", "physics_position_bits",
                                           "physics_quaternion_bits"};
                for (int field = 0; field < 7; ++field) {
                    std::fprintf(stream, "],\"%s\":[", poseNames[field]);
                    for (int i = 0; i < poseCounts[field]; ++i) {
                        if (i != 0)
                            std::fputs(",", stream);
                        std::uint32_t value = 0;
                        std::memcpy(&value, poseFields[field] + i, sizeof(value));
                        std::fprintf(stream, "\"%08X\"", value);
                    }
                }
                std::fputs("],\"base_key_bits\":[", stream);
                if (boneKeys != nullptr) {
                    const auto* key = reinterpret_cast<const unsigned char*>(&boneKeys[boneIndex]);
                    for (int i = 0; i < 15; ++i) {
                        if (i != 0)
                            std::fputs(",", stream);
                        std::uint32_t value = 0;
                        std::memcpy(&value, key + 4 * i, 4);
                        std::fprintf(stream, "\"%08X\"", value);
                    }
                }
                std::fputs("]}", stream);
            }
        }
        std::fputs("]}", stream);
    }
    if (!firstModel)
        std::fputc('\n', stream);
    std::fputs("  ]\n}\n", stream);
    std::fclose(stream);
}

inline void DumpRequestedFrameState(MMDApp* app) {
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    static constexpr const char* kNames[] = {"operation.before", "operation.after"};
    for (const char* name : kNames) {
        char request[MAX_PATH]{};
        char output[MAX_PATH]{};
        std::snprintf(request, sizeof(request), "%s\\%s.request", directory, name);
        if (GetFileAttributesA(request) == INVALID_FILE_ATTRIBUTES)
            continue;
        std::snprintf(output, sizeof(output), "%s.json", name);
        DeleteFileA(request);
        DumpFrameEntryState(app, output, false);
    }
}

#else // !MIKUDANCESTUDIO_DIAG

inline void DumpFrameEntryState(MMDApp*, const char* = "frame_state.json", bool = true) {
}

inline void DumpRequestedFrameState(MMDApp*) {
}

#endif // MIKUDANCESTUDIO_DIAG

} // namespace mikudancestudio
