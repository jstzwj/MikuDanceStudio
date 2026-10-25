#include "scene_fixture.hpp"

namespace {
struct FrameState {
    std::vector<float> values;
    std::vector<int> flags;
};
std::vector<FrameState> EvaluateFrames(Fixture& f) {
    auto& app = *f.app;
    const int savedFrame = app.CurrentFrame();
    std::vector<FrameState> result;
    for (int frame : {0, 6, 12, 24, 48, savedFrame}) {
        app.CurrentFrame() = frame;
        FrameState state;
        const auto values = [&](const float* first, std::size_t count) {
            state.values.insert(state.values.end(), first, first + count);
        };
        for (int slot = 0; slot < kModelSlotCount; ++slot) {
            auto* m = app.ModelSlot(slot);
            if (!m)
                continue;
            SeekModelFrame(*m, frame, app.PlaybackPhysicsMode());
            if (complexScene) {
                ModelApplyMorphs(m);
                SetPhysicsMode(m, 0, app.ModelSlots(), 0);
                SetPhysicsMode(m, 1, app.ModelSlots(), 0);
            }
            for (unsigned i = 0; i < m->boneCount; ++i) {
                values(m->boneTable[i].trans, 3);
                values(m->boneTable[i].rotQuat, 4);
                state.flags.push_back(m->boneTable[i].physicsDisabled);
                if (complexScene) {
                    values(m->boneTable[i].matInit, 16);
                    values(m->boneTable[i].matWorld, 16);
                }
            }
            for (unsigned i = 0; i < m->morphCount; ++i)
                values(&m->morphs[i].value, 1);
        }
        ReloadModels(&app);
        values(app.CameraPosition(), 3);
        values(app.CameraRotation(), 3);
        values(&app.CameraDistance(), 1);
        values(&app.CameraFov(), 1);
        state.flags.insert(state.flags.end(), {app.CameraParentModel(), app.CameraParentBone(),
                                               app.CameraPerspective()});
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            auto* a = app.AccessorySlot(slot);
            if (!a)
                continue;
            ApplyAccessoryTrack(&app, slot);
            values(a->position, 3);
            values(a->rotation, 3);
            values(&a->scale, 1);
            values(&a->opacity, 1);
            state.flags.insert(state.flags.end(),
                               {a->parentModel, a->parentBone, a->visible, a->shadowEnabled});
        }
        result.push_back(std::move(state));
    }
    result.pop_back();
    return result;
}
void CompareFrames(const std::vector<FrameState>& a, const std::vector<FrameState>& b) {
    Check(a.size() == b.size(), "fixed-frame snapshot count");
    for (std::size_t frame = 0; frame < a.size(); ++frame) {
        Check(a[frame].flags == b[frame].flags, "fixed-frame state flags and attachments");
        Check(a[frame].values.size() == b[frame].values.size(), "fixed-frame value count");
        for (std::size_t i = 0; i < a[frame].values.size(); ++i)
            Near(a[frame].values[i], b[frame].values[i], "fixed-frame scene state");
    }
}

std::vector<unsigned char> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    Check(in.good(), "read exported file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void CompareVmd(const std::filesystem::path& before, const std::filesystem::path& after) {
    auto a = ReadFile(before), b = ReadFile(after);
    const auto normalize = [](std::vector<unsigned char>& data) {
        Check(data.size() >= 54, "complete VMD header");
        // Only fixed-width header/name string tails are unspecified padding.
        for (const auto& field : {std::pair<std::size_t, std::size_t>{0, 30}, {30, 50}}) {
            bool padding = false;
            for (std::size_t i = field.first; i < field.second; ++i) {
                if (padding)
                    data[i] = 0;
                else if (data[i] == 0)
                    padding = true;
            }
        }
    };
    normalize(a);
    normalize(b);
    Check(a == b, "VMD records survive save/load/save");
}

// Optional local oracle input: native pointer addresses plus the exact records
// consumed by the original writer, seek and undo functions. No original code
// is included in the dump.
void DumpReference(Fixture& f) {
    if (referenceDirectory.empty())
        return;
    std::filesystem::create_directories(referenceDirectory);
    std::ofstream out(referenceDirectory / "memory.bin", std::ios::binary);
    const auto region = [&](const void* ptr, std::size_t size) {
        if (!ptr || !size)
            return;
        const std::uint64_t address = reinterpret_cast<std::uintptr_t>(ptr), length = size;
        out.write(reinterpret_cast<const char*>(&address), 8);
        out.write(reinterpret_cast<const char*>(&length), 8);
        out.write(static_cast<const char*>(ptr), size);
    };
    auto& app = *f.app;
    region(&app.state, sizeof app.state);
    region(f.renderer.get(), sizeof(D3DRenderer));
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        auto* m = app.ModelSlot(slot);
        if (!m)
            continue;
        region(m, sizeof(*m));
        region(m->boneTable, sizeof(mdl::BoneRecord) * m->boneCount);
        region(m->bonePhysicsState, m->boneCount);
        region(m->boneSelection, m->boneCount);
        region(m->morphs, sizeof(mdl::MorphRecord) * m->morphCount);
        region(m->ikChains, sizeof(mdl::IkChain) * m->ikChainCount);
        region(m->displayGroups, sizeof(mdl::DisplayGroup) * m->groupCount);
        region(m->boneOrderTable, sizeof(mdl::BoneOrderEntry) * m->boneOrderCount);
        region(m->boneKeys, sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity);
        region(m->morphKeys, sizeof(mdl::MorphKey) * mdl::kMorphKeyCapacity);
        region(m->displayKeys, sizeof(mdl::DisplayKey) * mdl::kDisplayKeyCapacity);
        for (const auto& ring : m->undoRings)
            for (const auto& record : ring.slots) {
                const bool keys = record.operation == mdl::UndoOperation::BoneKeys ||
                                  record.operation == mdl::UndoOperation::ChainedBoneKeys;
                region(record.bonePose,
                       sizeof(mdl::BonePoseSnapshot) * (keys ? m->boneCount : record.recordCount));
                region(record.keySnapshots, sizeof(mdl::BoneKeyUndoEntry) * record.recordCount);
            }
        for (std::size_t key = 0; key < mdl::kDisplayKeyCapacity; ++key) {
            region(mdl::IkStates(m->displayKeys[key]), m->ikChainCount);
            region(mdl::SelectorStates(m->displayKeys[key]),
                   sizeof(mdl::BoneReference) * m->boneOrderCount);
        }
    }
    region(app.CameraKeys(), sizeof(mdl::CameraKey) * mdl::kTimelineKeyCapacity);
    region(app.LightKeys(), sizeof(mdl::LightKey) * mdl::kTimelineKeyCapacity);
    region(app.ShadowKeys(), sizeof(mdl::SelfShadowKey) * mdl::kTimelineKeyCapacity);
    region(app.GravityKeys(), sizeof(mdl::GravityKey) * mdl::kTimelineKeyCapacity);
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
        if (!app.AccessorySlot(slot))
            continue;
        region(app.AccessorySlot(slot), sizeof(mdl::AccessoryRecord));
        region(app.AccessoryKeys(slot), sizeof(mdl::AccessoryKey) * mdl::kTimelineKeyCapacity);
    }
    Check(out.good(), "write original comparison input");
    auto& m = *app.ModelSlot(0);
    std::size_t keyCount = m.boneCount;
    for (std::size_t i = keyCount; i < mdl::kBoneKeyCapacity; ++i)
        if (m.boneKeys[i].frame != 0)
            keyCount = i + 1;
    const auto hex = [](const void* source, std::size_t length) {
        static constexpr char digits[] = "0123456789abcdef";
        const auto* bytes = static_cast<const unsigned char*>(source);
        std::string result;
        for (std::size_t i = 0; i < length; ++i) {
            result += digits[bytes[i] >> 4];
            result += digits[bytes[i] & 15];
        }
        return result;
    };
    const auto pose = [&]() {
        std::string result;
        for (unsigned i = 0; i < m.boneCount; ++i)
            result += hex(m.boneTable[i].trans, 28);
        for (unsigned i = 0; i < m.morphCount; ++i)
            result += hex(&m.morphs[i].value, 4);
        return result;
    };
    std::ofstream metadata(referenceDirectory / "input.json");
    metadata << "{\"app\":" << reinterpret_cast<std::uintptr_t>(&app.state)
             << ",\"complex\":" << (complexScene ? "true" : "false")
             << ",\"pointer_width\":" << sizeof(void*)
             << ",\"model\":" << reinterpret_cast<std::uintptr_t>(&m)
             << ",\"bone_table\":" << reinterpret_cast<std::uintptr_t>(m.boneTable)
             << ",\"bone_count\":" << m.boneCount << ",\"bone_stride\":" << sizeof(mdl::BoneRecord)
             << ",\"pose_offset\":" << offsetof(mdl::BoneRecord, trans)
             << ",\"morph_table\":" << reinterpret_cast<std::uintptr_t>(m.morphs)
             << ",\"morph_count\":" << m.morphCount
             << ",\"morph_stride\":" << sizeof(mdl::MorphRecord)
             << ",\"morph_value_offset\":" << offsetof(mdl::MorphRecord, value)
             << ",\"renderer\":" << reinterpret_cast<std::uintptr_t>(f.renderer.get())
             << ",\"device_offset\":" << offsetof(D3DRenderer, device)
             << ",\"key_pointer_address\":" << reinterpret_cast<std::uintptr_t>(&m.boneKeys)
             << ",\"key_bytes\":" << sizeof(mdl::BoneKey) * keyCount
             << ",\"undo_cursor_address\":" << reinterpret_cast<std::uintptr_t>(&m.UndoCursor())
             << ",\"model_slots_address\":" << reinterpret_cast<std::uintptr_t>(&app.ModelSlot(0))
             << ",\"accessory_slots_address\":"
             << reinterpret_cast<std::uintptr_t>(&app.AccessorySlot(0))
             << ",\"physics_address\":" << reinterpret_cast<std::uintptr_t>(&app.Physics())
             << ",\"physics_reset_address\":"
             << reinterpret_cast<std::uintptr_t>(&app.PhysicsResetPending())
             << ",\"frame_address\":" << reinterpret_cast<std::uintptr_t>(&app.CurrentFrame())
             << ",\"camera_fields\":[";
    bool first = true;
    for (const void* field :
         {static_cast<void*>(app.CameraPosition()), static_cast<void*>(app.CameraRotation()),
          static_cast<void*>(&app.CameraDistance()), static_cast<void*>(&app.CameraFov()),
          static_cast<void*>(&app.CameraParentModel()), static_cast<void*>(&app.CameraParentBone()),
          static_cast<void*>(&app.CameraPerspective())}) {
        if (!first)
            metadata << ',';
        first = false;
        metadata << reinterpret_cast<std::uintptr_t>(field);
    }
    auto& accessory = *app.AccessorySlot(3);
    metadata << "],\"accessory_fields\":[";
    first = true;
    for (const void* field :
         {static_cast<void*>(accessory.position), static_cast<void*>(accessory.rotation),
          static_cast<void*>(&accessory.scale), static_cast<void*>(&accessory.opacity),
          static_cast<void*>(&accessory.parentModel), static_cast<void*>(&accessory.parentBone),
          static_cast<void*>(&accessory.visible), static_cast<void*>(&accessory.shadowEnabled)}) {
        if (!first)
            metadata << ',';
        first = false;
        metadata << reinterpret_cast<std::uintptr_t>(field);
    }
    metadata << "],\"fixed_frames\":[";
    first = true;
    for (int frame : {0, 6, 12, 24, 48}) {
        SeekModelFrame(m, frame, app.PlaybackPhysicsMode());
        app.CurrentFrame() = frame;
        ReloadModels(&app);
        ApplyAccessoryTrack(&app, 3);
        if (complexScene) {
            ModelApplyMorphs(&m);
            SetPhysicsMode(&m, 0, app.ModelSlots(), 0);
            SetPhysicsMode(&m, 1, app.ModelSlots(), 0);
        }
        if (!first)
            metadata << ',';
        first = false;
        metadata << "{\"frame\":" << frame << ",\"pose\":\"" << pose() << "\",\"camera\":\""
                 << hex(app.CameraPosition(), 12) << hex(app.CameraRotation(), 12)
                 << hex(&app.CameraDistance(), 4) << hex(&app.CameraFov(), 4)
                 << hex(&app.CameraParentModel(), 4) << hex(&app.CameraParentBone(), 4)
                 << hex(&app.CameraPerspective(), 1) << "\",\"accessory\":\""
                 << hex(accessory.position, 12) << hex(accessory.rotation, 12)
                 << hex(&accessory.scale, 4) << hex(&accessory.opacity, 4)
                 << hex(&accessory.parentModel, 4) << hex(&accessory.parentBone, 4)
                 << hex(&accessory.visible, 1) << hex(&accessory.shadowEnabled, 1) << '"';
        if (complexScene) {
            metadata << ",\"matrices\":\"";
            for (unsigned i = 0; i < m.boneCount; ++i)
                metadata << hex(m.boneTable[i].matInit, 64) << hex(m.boneTable[i].matWorld, 64);
            metadata << '"';
        }
        metadata << '}';
    }
    app.CurrentFrame() = 24;
    ReloadModels(&app);
    ApplyAccessoryTrack(&app, 3);
    SeekModelFrame(m, 24, app.PlaybackPhysicsMode());
    metadata << "],\"undo\":";
    std::int32_t frame = 24;
    UndoModelEdit(&m, frame);
    const auto undo = [&](std::int32_t currentFrame) {
        metadata << "{\"frame\":" << currentFrame << ",\"cursor\":" << m.UndoCursor()
                 << ",\"keys\":\"" << hex(m.boneKeys, sizeof(mdl::BoneKey) * keyCount)
                 << "\",\"pose\":\"" << pose() << "\"}";
    };
    undo(frame);
    RedoModelEdit(&m, frame);
    metadata << ",\"redo\":";
    undo(frame);
    metadata << '}';
}

void Exercise(Fixture& f) {
    auto& app = *f.app;
    auto& model = f.LoadModel(0, 1);
    auto& second = f.LoadModel(7, 2);
    app.SetSelectedModelSlot(0);
    app.CameraMode() = 0;
    f.Pick(panel::kMainComboModel, 1);
    app.state.mainModelComboSelection = 1;
    RebuildTimelineTrackPanel(&app);
    model.selectedBone = 1;
    model.boneSelection[1] = 1;
    model.boneTable[1].trans[0] = 3.25f;
    model.boneTable[1].rotQuat[2] = .6f;
    model.boneTable[1].rotQuat[3] = .8f;
    model.boneTable[1].physicsDisabled = 1;
    if (complexScene) {
        Check(model.boneCount == 7 && model.ikChainCount == 1 && model.morphCount == 3,
              "complex PMX fixture topology");
        for (int i : {4, 5, 6})
            model.boneSelection[i] = 1;
        model.boneTable[4].trans[0] = .75f;
        model.boneTable[4].trans[1] = .5f;
        model.boneTable[4].trans[2] = 1;
        model.boneTable[5].trans[0] = .5f;
        model.boneTable[5].rotQuat[1] = .3f;
        model.boneTable[5].rotQuat[3] = std::sqrt(.91f);
    }
    app.CurrentFrame() = 24;
    RegisterSelectedBoneKeys(&model, 24, 0);
    const auto key = model.boneKeys[1].next;
    Check(key >= model.boneCount && model.boneKeys[key].frame == 24, "registered sparse bone key");
    model.boneKeys[key].interpolation[0] = 33;
    model.boneKeys[key].physicsDisabled = 1;
    std::int32_t frame = 24;
    UndoModelEdit(&model, frame);
    Check(model.boneKeys[1].next == 0, "undo removes registered bone key");
    RedoModelEdit(&model, frame);
    Check(model.boneKeys[1].next == key, "redo restores registered bone key");
    Near(model.boneKeys[key].position[0], 3.25f, "redo bone translation");
    // Change the curve after redo so this also tests serialization of edited curves.
    model.boneKeys[key].interpolation[0] = 33;
    model.morphs[1].value = .75f;
    RegisterMorphKeyCurrent(&model, 1, 24);
    if (complexScene) {
        model.morphs[2].value = .5f;
        RegisterMorphKeyCurrent(&model, 2, 24);
    }
    model.comboSelIndex2 = 2;
    second.comboSelIndex2 = 1;
    model.edgeScale = 1.37f;
    second.edgeScale = .63f;
    model.frameRegistrationSelection = 4;
    app.PlaybackPhysicsMode() = complexScene ? 0 : 2;
    app.state.gravityMagnitude = 7.5f;
    app.state.gravityX = .25f;
    app.state.gravityY = -1;
    app.state.gravityZ = .5f;
    app.state.gravityNoise = 17;
    app.state.gravityNoiseEnabled = 1;
    app.state.floorVisible = 0;
    app.CameraKeys()[0].next = 24;
    auto& camera = app.CameraKeys()[24];
    camera.frame = 24;
    camera.distance = -31.5f;
    camera.eye[0] = 2;
    camera.eye[1] = 7;
    camera.target[0] = .2f;
    const int parentSlot = complexScene ? 0 : 7;
    const int parentBone = complexScene ? 6 : 1;
    camera.parentModel = parentSlot;
    camera.parentBone = parentBone;
    camera.fov = 42;
    camera.perspective = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 6; ++c)
            camera.interpolation[r][c] = 10 + r * 20 + c;
    const auto expectedCamera = camera;
    const auto accessoryPath = f.directory / L"triangle.x";
    WriteAccessory(accessoryPath);
    auto* accessory =
        static_cast<mdl::AccessoryRecord*>(::operator new(sizeof(mdl::AccessoryRecord)));
    std::memset(accessory, 0, sizeof(*accessory));
    InitAccessoryRecord(accessory);
    app.AccessorySlot(3) = accessory;
    Check(LoadAccessoryObject(&app, accessory, accessoryPath.c_str()), "load fixture accessory");
    strcpy_s(accessory->name, "triangle");
    accessory->order = 0;
    accessory->parentModel = parentSlot;
    accessory->parentBone = parentBone;
    accessory->position[0] = 4;
    accessory->scale = 1.25f;
    accessory->opacity = .75f;
    accessory->visible = 1;
    accessory->shadowEnabled = 1;
    app.SelectedAccessorySlot() = 3;
    f.Add(panel::kAccessoryCombo, "triangle");
    f.Pick(panel::kAccessoryCombo, 0);
    RegisterAccessoryKey(&app, 24, 3);
    const auto expectedFrames = EvaluateFrames(f);
    wcscpy_s(app.EnvFileName(), 256, (f.directory / L"edited.pmm").c_str());
    DumpReference(f);
    f.Save(L"edited.pmm");
    if (!referenceDirectory.empty())
        std::filesystem::copy_file(f.directory / L"edited.pmm", referenceDirectory / "port.pmm",
                                   std::filesystem::copy_options::overwrite_existing);
    f.Reload(L"edited.pmm");
    auto* loaded = app.ModelSlot(0);
    Check(loaded && app.ModelSlot(7), "reload preserves sparse model slots");
    Check(loaded->undoRings[0].slots[loaded->UndoCursor()].operation == mdl::UndoOperation::Empty,
          "project reload starts with empty undo history");
    CompareFrames(expectedFrames, EvaluateFrames(f));
    Check(loaded->comboSelIndex == 1 && loaded->comboSelIndex2 == 2 &&
              app.ModelSlot(7)->comboSelIndex2 == 1,
          "reload model calculation and display order");
    Near(loaded->edgeScale, 1.37f, "reload edge thickness");
    Check(loaded->frameRegistrationSelection == 4, "reload model tail selection");
    const auto loadedKey = loaded->boneKeys[1].next;
    Check(loadedKey == key && loaded->boneKeys[loadedKey].frame == 24, "reload bone chain");
    Near(loaded->boneKeys[loadedKey].position[0], 3.25f, "reload bone key translation");
    Check(loaded->boneKeys[loadedKey].interpolation[0] == 33, "reload bone interpolation");
    Check(loaded->boneKeys[loadedKey].physicsDisabled == 1, "reload bone physics flag");
    const auto morphKey = loaded->morphKeys[1].next;
    Check(morphKey && loaded->morphKeys[morphKey].frame == 24, "reload morph chain");
    Near(loaded->morphKeys[morphKey].value, .75f, "reload morph weight");
    Check(std::memcmp(expectedCamera.interpolation, app.CameraKeys()[24].interpolation, 24) == 0,
          "reload camera interpolation");
    Check(app.CameraKeys()[24].parentModel == parentSlot &&
              app.CameraKeys()[24].parentBone == parentBone,
          "reload camera parent");
    Near(app.CameraKeys()[24].distance, -31.5f, "reload camera distance");
    Check(app.PlaybackPhysicsMode() == (complexScene ? 0 : 2) && app.state.gravityNoise == 17 &&
              app.state.gravityNoiseEnabled == 1 && app.state.floorVisible == 0,
          "reload physics settings");
    Near(app.state.gravityMagnitude, 7.5f, "reload gravity magnitude");
    Near(app.state.gravityX, .25f, "reload gravity X");
    Near(app.state.gravityY, -1, "reload gravity Y");
    Near(app.state.gravityZ, .5f, "reload gravity Z");
    auto* acc = app.AccessorySlot(3);
    Check(acc && acc->parentModel == parentSlot && acc->parentBone == parentBone,
          "reload accessory attachment");
    Near(acc->opacity, .75f, "reload accessory opacity");
    Near(acc->scale, 1.25f, "reload accessory scale");
    Check(app.AccessoryKeys(3)[app.AccessoryKeys(3)[0].next].frame == 24, "reload accessory key");
    SeekModelFrame(*loaded, 24, app.PlaybackPhysicsMode());
    Near(loaded->boneTable[1].trans[0], 3.25f, "fixed-frame reloaded bone pose");
    Near(loaded->morphs[1].value, .75f, "fixed-frame reloaded morph pose");
    f.Save(L"reloaded.pmm");
    f.Reload(L"reloaded.pmm");
    CompareFrames(expectedFrames, EvaluateFrames(f));
    Near(app.ModelSlot(0)->boneKeys[key].position[0], 3.25f, "second PMM generation");
}

void ExerciseMotionAndPose(Fixture& f) {
    auto& app = *f.app;
    app.SetSelectedModelSlot(0);
    app.CameraMode() = 0;
    app.CurrentFrame() = 0;
    auto& source = *app.SelectedModel();
    for (unsigned i = 0; i < source.boneCount; ++i)
        source.boneKeys[i].allocated = 1;
    for (unsigned i = 0; i < source.morphCount; ++i)
        source.morphKeys[i].allocated = 1;
    SaveVmdFile((f.directory / L"motion.vmd").c_str());
    auto& target = f.LoadModel(8, 3);
    app.SetSelectedModelSlot(8);
    f.Pick(panel::kMainComboModel, 3);
    app.state.mainModelComboSelection = 3;
    RebuildTimelineTrackPanel(&app);
    LoadVmdMotion(&app, (f.directory / L"motion.vmd").c_str());
    SeekModelFrame(target, 24, 0);
    Near(target.boneTable[1].trans[0], 3.25f, "VMD fixed-frame translation");
    Near(target.boneTable[1].rotQuat[2], .6f, "VMD fixed-frame quaternion");
    Near(target.morphs[1].value, .75f, "VMD fixed-frame morph");
    Check(target.boneKeys[target.boneKeys[1].next].interpolation[0] == 33,
          "VMD interpolation survives import");
    target.boneSelection[1] = 1;
    SaveVpdFile((f.directory / L"pose.vpd").c_str());
    target.boneTable[1].trans[0] = -7;
    LoadVpdFile((f.directory / L"pose.vpd").c_str());
    Near(target.boneTable[1].trans[0], 3.25f, "VPD pose import");
    std::int32_t frame = 0;
    UndoModelEdit(&target, frame);
    Near(target.boneTable[1].trans[0], -7, "VPD undo restores displaced pose");
    RedoModelEdit(&target, frame);
    Near(target.boneTable[1].trans[0], 3.25f, "VPD redo restores imported pose");
    SaveVpdFile((f.directory / L"pose-again.vpd").c_str());
    Check(ReadFile(f.directory / L"pose.vpd") == ReadFile(f.directory / L"pose-again.vpd"),
          "VPD text survives save/load/save");
    SaveVmdFile((f.directory / L"motion-again.vmd").c_str());
    CompareVmd(f.directory / L"motion.vmd", f.directory / L"motion-again.vmd");
    app.CameraMode() = 1;
    app.CurrentFrame() = 0;
    app.CameraKeys()[0].selected = app.CameraKeys()[24].selected = 1;
    SaveVmdFile((f.directory / L"camera.vmd").c_str());
    const auto expected = app.CameraKeys()[24];
    std::memset(app.CameraKeys(), 0, sizeof(mdl::CameraKey) * mdl::kTimelineKeyCapacity);
    LoadVmdMotion(&app, (f.directory / L"camera.vmd").c_str());
    const auto& camera = app.CameraKeys()[app.CameraKeys()[0].next];
    Check(camera.frame == 24 && camera.fov == 42, "VMD camera frame and FOV");
    Near(camera.distance, expected.distance, "VMD camera distance");
    Check(std::memcmp(camera.interpolation, expected.interpolation, 24) == 0,
          "VMD camera interpolation");
    SaveVmdFile((f.directory / L"camera-again.vmd").c_str());
    CompareVmd(f.directory / L"camera.vmd", f.directory / L"camera-again.vmd");
}

void ExerciseCrossLoading(Fixture& f) {
    const auto expected = EvaluateFrames(f);
    for (const auto* name : {L"original.pmm", L"original-reloaded.pmm"}) {
        const auto source = referenceDirectory / name;
        Check(std::filesystem::is_regular_file(source), "original oracle PMM exists");
        f.Reload(source.c_str());
        CompareFrames(expected, EvaluateFrames(f));
        auto& app = *f.app;
        auto* model = app.ModelSlot(0);
        Check(model && app.ModelSlot(7), "cross-load sparse model slots");
        Check(model->comboSelIndex == 1 && model->comboSelIndex2 == 2 &&
                  app.ModelSlot(7)->comboSelIndex2 == 1,
              "cross-load independent model orders");
        const auto key = model->boneKeys[1].next;
        Check(key && model->boneKeys[key].previous == 1 && model->boneKeys[key].next == 0 &&
                  model->boneKeys[key].frame == 24,
              "cross-load complete bone key chain");
        Check(model->boneKeys[key].interpolation[0] == 33 &&
                  model->boneKeys[key].physicsDisabled == 1,
              "cross-load curve and per-bone physics flag");
        const int parentSlot = complexScene ? 0 : 7;
        const int parentBone = complexScene ? 6 : 1;
        Check(app.CameraKeys()[24].parentModel == parentSlot &&
                  app.CameraKeys()[24].parentBone == parentBone &&
                  app.AccessorySlot(3)->parentModel == parentSlot &&
                  app.AccessorySlot(3)->parentBone == parentBone,
              "cross-load camera and accessory parents");
        Check(app.PlaybackPhysicsMode() == (complexScene ? 0 : 2) && app.state.floorVisible == 0 &&
                  app.state.gravityNoise == 17 && app.state.gravityNoiseEnabled == 1,
              "cross-load physics configuration");
        Near(app.state.gravityMagnitude, 7.5f, "cross-load gravity");
        const auto output = std::filesystem::path(name).stem().wstring() + L"-port-resaved.pmm";
        f.Save(output.c_str());
    }
    std::puts("original-to-port cross-loading passed");
}

LRESULT CALLBACK SceneCommandProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR id, DWORD_PTR) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        CommandDispatch(reinterpret_cast<HWND>(lParam), wParam);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, SceneCommandProc, id);
    return DefSubclassProc(window, message, wParam, lParam);
}

void ExerciseUiCommands(Fixture& f) {
    f.Reload(L"reloaded.pmm");
    auto& app = *f.app;
    auto& model = *app.ModelSlot(0);
    const unsigned selectedBone = complexScene ? 5 : 1;
    app.SetSelectedModelSlot(0);
    app.CameraMode() = 0;
    f.Pick(panel::kMainComboModel, 1);
    app.state.mainModelComboSelection = 1;
    model.selectedBone = selectedBone;
    std::memset(model.boneSelection, 0, model.boneCount);
    model.boneSelection[selectedBone] = 1;
    RebuildTimelineTrackPanel(&app);
    Check(SetWindowSubclass(f.window, SceneCommandProc, 1, 0) != FALSE,
          "install production command fixture");
    std::ofstream snapshots;
    if (!referenceDirectory.empty()) {
        snapshots.open(referenceDirectory / "ui-commands.json");
        snapshots << '[';
    }
    bool firstSnapshot = true;
    const auto command = [&](int id) {
        SendMessageA(GetDlgItem(f.window, id), BM_CLICK, 0, 0);
        Check(app.SceneModified() != 0, "UI command marks scene modified");
        Check(model.selectedBone == selectedBone && model.boneSelection[selectedBone] == 1,
              "UI command preserves bone selection");
        if (snapshots.is_open()) {
            if (!firstSnapshot)
                snapshots << ',';
            firstSnapshot = false;
            snapshots << "{\"id\":" << id << ",\"bone\":" << selectedBone
                      << ",\"frame\":" << app.CurrentFrame() << ",\"cursor\":" << model.UndoCursor()
                      << ",\"latest\":" << model.LatestUndoCursor() << ",\"undo_enabled\":"
                      << (IsWindowEnabled(GetDlgItem(f.window, panel::kUndoButton)) ? 1 : 0)
                      << ",\"redo_enabled\":"
                      << (IsWindowEnabled(GetDlgItem(f.window, panel::kRedoButton)) ? 1 : 0)
                      << ",\"physics_reset\":" << (app.PhysicsResetPending() ? 1 : 0)
                      << ",\"selection\":[";
            for (unsigned i = 0; i < model.boneCount; ++i) {
                if (i)
                    snapshots << ',';
                snapshots << static_cast<unsigned>(model.boneSelection[i]);
            }
            snapshots << ']' << ",\"keys\":\"";
            const auto* data = reinterpret_cast<const unsigned char*>(model.boneKeys);
            constexpr char digits[] = "0123456789abcdef";
            for (std::size_t i = 0; i < sizeof(mdl::BoneKey) * model.boneCount * 4; ++i)
                snapshots << digits[data[i] >> 4] << digits[data[i] & 15];
            snapshots << "\"}";
        }
    };
    app.CurrentFrame() = 36;
    model.boneTable[selectedBone].trans[0] = 4.5f;
    if (complexScene) {
        auto& axis = model.boneTable[selectedBone];
        axis.rotQuat[0] = .2f;
        axis.rotQuat[1] = .3f;
        axis.rotQuat[2] = .1f;
        axis.rotQuat[3] = std::sqrt(.86f);
    }
    const int startCursor = model.UndoCursor();
    command(panel::kRegisterFrameButton);
    const auto previous = model.boneKeys[selectedBone].next;
    const auto added = model.boneKeys[previous].next;
    Check(added && model.boneKeys[added].frame == 36 && model.UndoCursor() != startCursor,
          "register UI command adds a key and undo record");
    if (complexScene) {
        Near(model.boneKeys[added].rotation[0], .2f,
             "button registration retains edited quaternion X");
        Near(model.boneKeys[added].rotation[2], .1f,
             "button registration retains edited quaternion Z");
        Check((model.boneTable[selectedBone].flags & mdl::kBoneFlagLocalAxes) != 0,
              "generated PMX local-axis metadata survives reload");
    }
    model.boneTable[selectedBone].trans[0] = 6.25f;
    command(panel::kRegisterFrameButton);
    Check(model.boneKeys[previous].next == added && model.boneKeys[added].next == 0,
          "overwrite UI command preserves key-chain shape");
    Near(model.boneKeys[added].position[0], 6.25f, "overwrite UI value");
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
        model.boneKeys[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
        model.morphKeys[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
        model.displayKeys[i].allocated = 0;
    model.boneKeys[added].allocated = 1;
    command(panel::kDeleteFramesButton);
    Check(model.boneKeys[previous].next == 0, "delete UI command unlinks the key");
    command(panel::kUndoButton);
    Check(model.boneKeys[previous].next == added, "undo UI command restores deleted key");
    Near(model.boneKeys[added].position[0], 6.25f, "undo delete restores overwritten value");
    Check(IsWindowEnabled(GetDlgItem(f.window, panel::kRedoButton)), "undo enables redo button");
    command(panel::kUndoButton);
    Near(model.boneKeys[added].position[0], 4.5f, "undo overwrite restores first value");
    command(panel::kUndoButton);
    Check(model.boneKeys[previous].next == 0 && model.UndoCursor() == startCursor,
          "continuous undo reaches the initial state");
    for (int i = 0; i < 3; ++i)
        command(panel::kRedoButton);
    Check(model.boneKeys[previous].next == 0 &&
              !IsWindowEnabled(GetDlgItem(f.window, panel::kRedoButton)) &&
              IsWindowEnabled(GetDlgItem(f.window, panel::kUndoButton)),
          "continuous redo restores deletion and button states");
    Check(app.PhysicsResetPending() != 0, "UI undo/redo requests pose refresh");
    if (snapshots.is_open())
        snapshots << ']';
    RemoveWindowSubclass(f.window, SceneCommandProc, 1);
    const auto expected = EvaluateFrames(f);
    f.Save(L"ui-edited.pmm");
    f.Reload(L"ui-edited.pmm");
    CompareFrames(expected, EvaluateFrames(f));
    std::puts("UI command edit/undo/redo/save/reload passed");
}

void ExerciseLegacy(Fixture& f) {
    const auto modelPath = f.directory / L"legacy.pmd";
    WriteModel(modelPath);
    WriteLegacyScene(f.directory / L"legacy.pmm", modelPath);
    f.Reload(L"legacy.pmm");
    auto& app = *f.app;
    auto* model = app.ModelSlot(0);
    Check(model && !app.ModelSlot(7) && !app.ModelSlot(8), "v1 releases old model slots");
    Check(model->boneKeys[1].next == 2 && model->boneKeys[2].frame == 24,
          "v1 bone chain and pool index");
    Near(model->boneKeys[2].position[0], 2.5f, "v1 bone translation");
    Near(model->morphKeys[2].value, .625f, "v1 morph value");
    f.Save(L"upgraded.pmm");
    f.Reload(L"upgraded.pmm");
    model = app.ModelSlot(0);
    Check(model && model->boneKeys[1].next == 2, "v1 to v2 preserves bone chain");
    SeekModelFrame(*model, 24, 0);
    Near(model->boneTable[1].trans[0], 2.5f, "v1 to v2 fixed-frame pose");
    Near(model->morphs[1].value, .625f, "v1 to v2 fixed-frame morph");
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 &&
        (std::strcmp(argv[1], "--reference") == 0 || std::strcmp(argv[1], "--cross-check") == 0)) {
        referenceDirectory = std::filesystem::absolute(argv[2]);
        crossCheck = std::strcmp(argv[1], "--cross-check") == 0;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetWindowsHookExA(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    try {
        const auto rootReference = referenceDirectory;
        for (bool complex : {false, true}) {
            complexScene = complex;
            if (!rootReference.empty())
                referenceDirectory = complex ? rootReference / "complex" : rootReference;
            Fixture fixture;
            if (!fixture.ready) {
                std::puts("SKIP: D3D9 devices unavailable");
                return 77;
            }
            Exercise(fixture);
            if (crossCheck)
                ExerciseCrossLoading(fixture);
            if (!complex) {
                ExerciseMotionAndPose(fixture);
                ExerciseLegacy(fixture);
            }
            ExerciseUiCommands(fixture);
        }
        Check(dialogs == 0, "no unexpected dialogs");
        std::puts("scene roundtrip passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene roundtrip: %s\n", e.what());
        return 1;
    }
}
