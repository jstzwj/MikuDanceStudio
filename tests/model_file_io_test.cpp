#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "pmm_io_common.hpp"
#include "pmm_model_save.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <memory>
#include <string>
#include <vector>

using namespace mikudancestudio;
namespace {
int failures;
int paints, selections, seeks;
bool expectVmdRefresh;
mdl::ModelRecord* soughtModel;
int soughtFrame, soughtPhysics;
void Check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
struct TempFile {
    wchar_t path[MAX_PATH]{};
    explicit TempFile(const wchar_t* suffix = nullptr) {
        wchar_t directory[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"mio", 0, path))
            std::abort();
        if (suffix) {
            const std::wstring oldPath = path;
            wcscat_s(path, suffix);
            if (!MoveFileW(oldPath.c_str(), path))
                std::abort();
        }
    }
    ~TempFile() { DeleteFileW(path); }
    std::vector<unsigned char> Read() const {
        FILE* file = nullptr;
        _wfopen_s(&file, path, L"rb");
        if (!file)
            std::abort();
        std::fseek(file, 0, SEEK_END);
        std::vector<unsigned char> bytes(std::ftell(file));
        std::rewind(file);
        Check(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read saved file");
        std::fclose(file);
        return bytes;
    }
    void Write(const char* text) const {
        FILE* file = nullptr;
        _wfopen_s(&file, path, L"wt");
        if (!file)
            std::abort();
        std::fputs(text, file);
        std::fclose(file);
    }
    void Write(const std::vector<unsigned char>& bytes) const {
        FILE* file = nullptr;
        _wfopen_s(&file, path, L"wb");
        if (!file)
            std::abort();
        Check(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size(), "write fixture");
        std::fclose(file);
    }
};
struct Bytes {
    std::vector<unsigned char> data;
    void Byte(unsigned value) { data.push_back(static_cast<unsigned char>(value)); }
    void U32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            Byte(value >> (8 * i));
    }
    void Float(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        U32(bits);
    }
    void Text(const char* text, std::size_t width) {
        const auto length = std::strlen(text);
        for (std::size_t i = 0; i < width; ++i)
            Byte(i < length ? text[i] : 0);
    }
};
struct Model {
    std::unique_ptr<mdl::ModelRecord> record = std::make_unique<mdl::ModelRecord>();
    std::unique_ptr<mdl::BoneKey[]> bones = std::make_unique<mdl::BoneKey[]>(mdl::kBoneKeyCapacity);
    std::unique_ptr<mdl::MorphKey[]> morphs =
        std::make_unique<mdl::MorphKey[]>(mdl::kMorphKeyCapacity);
    std::unique_ptr<mdl::DisplayKey[]> display =
        std::make_unique<mdl::DisplayKey[]>(mdl::kDisplayKeyCapacity);
    std::array<mdl::BoneRecord, 3> boneTable{};
    std::array<mdl::MorphRecord, 2> morphTable{};
    std::array<unsigned char, 3> selected{}, edited{};
    std::array<unsigned char, mdl::kDisplayKeyCapacity> ikStates{};
    mdl::IkChain ik{};
    Model() {
        strcpy_s(record->name, "fixture");
        record->boneCount = 2;
        record->morphCount = 2;
        record->ikChainCount = 1;
        record->boneKeys = bones.get();
        record->morphKeys = morphs.get();
        record->displayKeys = display.get();
        record->boneTable = boneTable.data();
        record->morphs = morphTable.data();
        record->boneSelection = selected.data();
        record->bonePhysicsState = edited.data();
        record->ikChains = &ik;
        for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
            display[i].ikStates = &ikStates[i];
        ik.boneIndex = 1;
        for (int i = 0; i < 3; ++i) {
            strcpy_s(boneTable[i].name, i ? "target" : "base");
            boneTable[i].type = mdl::BoneType::RotateMove;
            boneTable[i].rotQuat[3] = 1;
        }
        strcpy_s(morphTable[0].name, "unused");
        strcpy_s(morphTable[1].name, "smile");
    }
    ~Model() {
        for (auto& ring : record->undoRings)
            for (auto& entry : ring.slots) {
                ::operator delete(entry.bonePose);
                ::operator delete(entry.keySnapshots);
            }
    }
};
struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    HWND window = CreateWindowW(L"STATIC", L"file I/O regression", WS_OVERLAPPED, 0, 0, 100, 100,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Fixture() {
        std::memset(&app->state, 0, sizeof app->state);
        app->Hwnd() = window;
        app->SetSelectedModelSlot(kModelSlotCount - 1);
        app->state.englishUI = 1;
        g_Block = app.get();
        paints = selections = seeks = 0;
        expectVmdRefresh = true;
    }
    ~Fixture() {
        g_Block = nullptr;
        if (window)
            DestroyWindow(window);
    }
    void Select(Model& model) { app->ModelSlot(kModelSlotCount - 1) = model.record.get(); }
};
void AppendBone(Bytes& out, const char* name, unsigned frame, const mdl::BoneKey& key) {
    out.Text(name, 15);
    out.U32(frame);
    for (float value : key.position)
        out.Float(value);
    for (float value : key.rotation)
        out.Float(value);
    // Original VMD redundancy includes three bytes from position.x after
    // the 16 control bytes. The fixture uses zero there deliberately.
    std::array<unsigned char, 19> controls{};
    std::memcpy(controls.data(), key.interpolation, 16);
    for (int channel = 0; channel < 4; ++channel) {
        for (int column = 0; column < 4; ++column) {
            if (channel == 0 && column == 0) {
                out.Byte(controls[0]);
                out.Byte(controls[1]);
                out.Byte(key.physicsDisabled ? 0x63 : 0);
                out.Byte(key.physicsDisabled ? 0x0F : 0);
            } else {
                for (int byte = 0; byte < 4; ++byte)
                    out.Byte(controls[column * 4 + channel + byte]);
            }
        }
    }
}
void TestVmdModel() {
    Fixture f;
    Model source;
    f.Select(source);
    source.bones[0].allocated = 1;
    source.bones[0].frame = 10;
    source.bones[0].rotation[3] = 1;
    auto& lastBone = source.bones[mdl::kBoneKeyCapacity - 1];
    lastBone.allocated = 2;
    lastBone.previous = 1;
    lastBone.frame = 25;
    lastBone.physicsDisabled = 2;
    lastBone.position[1] = 3;
    lastBone.rotation[3] = 1;
    for (int i = 0; i < 16; ++i)
        lastBone.interpolation[i] = static_cast<unsigned char>(10 + i);
    auto& lastMorph = source.morphs[mdl::kMorphKeyCapacity - 1];
    lastMorph.allocated = 2;
    lastMorph.previous = 1;
    lastMorph.frame = 30;
    lastMorph.value = 0.75f;
    auto& lastDisplay = source.display[mdl::kDisplayKeyCapacity - 1];
    unsigned char ikState = 2;
    lastDisplay.frame = 35;
    lastDisplay.allocated = 2;
    lastDisplay.visible = 2;
    lastDisplay.ikStates = &ikState;
    TempFile file;
    SaveVmdFile(file.path);
    const auto bytes = file.Read();
    Bytes expected;
    expected.Text("fixture", 20);
    expected.U32(2);
    AppendBone(expected, "base", 0, source.bones[0]);
    AppendBone(expected, "target", 15, lastBone);
    expected.U32(1);
    expected.Text("smile", 15);
    expected.U32(20);
    expected.Float(.75f);
    expected.U32(0);
    expected.U32(0);
    expected.U32(0);
    expected.U32(1);
    expected.U32(25);
    expected.Byte(1);
    expected.U32(1);
    expected.Text("target", 20);
    expected.Byte(1);
    Check(
        bytes.size() == 30 + expected.data.size() &&
            std::memcmp(bytes.data(), "Vocaloid Motion Data 0002", 25) == 0 &&
            std::memcmp(bytes.data() + 30, expected.data.data(), expected.data.size()) == 0,
        "VMD model stream preserves disk widths, last slots, names, relative frames and IK flags");
    Model target;
    target.boneTable[1].trans[0] = 9;
    target.edited[1] = 7;
    target.selected[1] = 3;
    target.record->UndoCursor() = mdl::kUndoHistoryDepth - 1;
    f.Select(target);
    f.app->state.currentFrame = 100;
    Check(LoadVmdMotion(f.app.get(), file.path) == 42 && soughtModel == target.record.get() &&
              soughtFrame == 100 && soughtPhysics == 0 && paints == 1 && selections == 1 &&
              seeks == 1,
          "VMD import seeks the selected real slot after repaint and curve refresh");
    const auto& undo = target.record->undoRings[0].slots[0];
    Check(target.record->UndoCursor() == 0 && target.record->LatestUndoCursor() == 0 &&
              undo.operation == mdl::UndoOperation::BoneKeys && undo.frame == 100 &&
              undo.bonePose[1].boneIndex == 1 && undo.bonePose[1].position[0] == 9 &&
              undo.bonePose[1].poseEdited == 7 && target.selected[1] == 3,
          "VMD import wraps undo and snapshots pose edit flags independently of selection");
    const auto& bone = target.bones[target.bones[1].next];
    const auto& morph = target.morphs[target.morphs[1].next];
    const auto& display = target.display[target.display[0].next];
    Check(
        bone.frame == 115 && bone.position[1] == 3 && bone.physicsDisabled == 1 &&
            std::memcmp(bone.interpolation, lastBone.interpolation, 16) == 0 &&
            morph.frame == 120 && morph.value == .75f && display.frame == 125 &&
            display.visible == 1 && display.ikStates[0] == 1,
        "VMD file reaches real registrars with the original offset, interpolation and display data");
}
void TestVmdCamera() {
    Fixture f;
    f.app->CameraMode() = 1;
    auto cameras = std::make_unique<mdl::CameraKey[]>(mdl::kTimelineKeyCapacity);
    auto lights = std::make_unique<mdl::LightKey[]>(mdl::kTimelineKeyCapacity);
    auto shadows = std::make_unique<mdl::SelfShadowKey[]>(mdl::kTimelineKeyCapacity);
    f.app->CameraKeys() = cameras.get();
    f.app->LightKeys() = lights.get();
    f.app->ShadowKeys() = shadows.get();
    auto& camera = cameras[mdl::kTimelineKeyCapacity - 1];
    camera.selected = 2;
    camera.frame = 20;
    camera.distance = -10;
    camera.fov = 37;
    camera.perspective = 2;
    for (int column = 0; column < 4; ++column)
        for (int channel = 0; channel < 6; ++channel)
            camera.interpolation[column][channel] =
                static_cast<unsigned char>(column * 10 + channel);
    auto& light = lights[mdl::kTimelineKeyCapacity - 1];
    light.selected = 2;
    light.frame = 22;
    light.color[0] = .5f;
    light.direction[1] = -1;
    auto& shadow = shadows[mdl::kTimelineKeyCapacity - 1];
    shadow.selected = 2;
    shadow.frame = 25;
    shadow.mode = 2;
    shadow.distance = .0125f;
    TempFile file;
    SaveVmdFile(file.path);
    const auto bytes = file.Read();
    Bytes expected;
    expected.U32(0);
    expected.U32(0);
    expected.U32(1);
    expected.U32(0);
    expected.Float(-10);
    for (int i = 0; i < 6; ++i)
        expected.Float(0);
    for (int channel = 0; channel < 6; ++channel)
        for (int column : {0, 2, 1, 3})
            expected.Byte(column * 10 + channel);
    expected.U32(37);
    expected.Byte(1);
    expected.U32(1);
    expected.U32(2);
    for (float value : {.5f, 0.f, 0.f, 0.f, -1.f, 0.f})
        expected.Float(value);
    expected.U32(1);
    expected.U32(5);
    expected.Byte(2);
    expected.Float(.0125f);
    expected.U32(0);
    Check(bytes.size() == 50 + expected.data.size() &&
              std::memcmp(bytes.data() + 50, expected.data.data(), expected.data.size()) == 0,
          "camera VMD writes packed global sections and last slots with no selected model");
}
void TestVmdFixedAxis() {
    for (bool pmx : {false, true}) {
        for (int mode = 0; mode < 3; ++mode) {
            Fixture f;
            Model source;
            f.Select(source);
            source.bones[1].allocated = 1;
            source.bones[1].rotation[0] = .36f;
            source.bones[1].rotation[1] = -.48f;
            source.bones[1].rotation[3] = .8f;
            TempFile file;
            SaveVmdFile(file.path);
            Model target;
            target.record->physicsMode = pmx ? 2 : 0;
            auto& bone = target.boneTable[1];
            bone.type = mode == 0 ? mdl::BoneType::FixedAxis : mdl::BoneType::UnderIk;
            bone.flags = mode == 2 ? 0 : mdl::kBoneFlagFixedAxis;
            bone.axis[1] = 1;
            bone.tailBone = 0;
            target.boneTable[0].position[1] = 1;
            f.Select(target);
            Check(LoadVmdMotion(f.app.get(), file.path) == 42, "load fixed-axis VMD");
            const auto& key = target.bones[1];
            Check(std::fabs(key.rotation[0] - (mode == 2 ? .36f : 0.f)) < .000001f &&
                      std::fabs(key.rotation[1] - (mode == 2 ? -.48f : -.6f)) < .000001f &&
                      key.rotation[2] == 0 && std::fabs(key.rotation[3] - .8f) < .000001f,
                  "VMD registrar uses the PMX axis or PMD rest delta only for fixed-axis bones");
        }
    }
}
void TestPmmModel() {
    Model model;
    model.record->boneCount = model.record->morphCount = 1;
    mdl::BoneOrderEntry selector{};
    selector.boneIndex = 7;
    selector.windowStart = 10;
    selector.windowEnd = 20;
    selector.linkedModel = kModelSlotCount - 1;
    selector.linkedBone = 42;
    model.record->boneOrderCount = 1;
    model.record->boneOrderTable = &selector;
    mdl::BoneReference reference{selector.linkedModel, selector.linkedBone};
    for (auto& key : {&model.display[0], &model.display[mdl::kDisplayKeyCapacity - 1]}) {
        key->selectorStates = &reference;
        key->visible = 2;
        key->allocated = 2;
        key->ikStates[0] = 2;
    }
    auto& bone = model.bones[mdl::kBoneKeyCapacity - 1];
    bone.frame = 123;
    bone.previous = 3;
    bone.position[1] = 2;
    bone.rotation[3] = 1;
    bone.allocated = bone.physicsDisabled = 2;
    for (int i = 0; i < 16; ++i)
        bone.interpolation[i] = static_cast<unsigned char>(i + 1);
    auto& morph = model.morphs[mdl::kMorphKeyCapacity - 1];
    morph.frame = 124;
    morph.value = .75f;
    morph.allocated = 2;
    model.display[mdl::kDisplayKeyCapacity - 1].frame = 125;
    FILE* raw = nullptr;
    if (tmpfile_s(&raw) != 0 || !raw)
        std::abort();
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, &std::fclose);
    const int fd = _fileno(raw);
    std::array<mdl::ModelRecord*, kModelSlotCount> slots{};
    slots.back() = model.record.get();
    char text[256]{};
    pmm_io::WritePmmModelBlocks(fd, nullptr, slots.data(), text);
    const auto size = _tell(fd);
    std::vector<unsigned char> actual(size);
    _lseek(fd, 0, SEEK_SET);
    Check(_read(fd, actual.data(), size) == size, "read PMM model block");
    Bytes expected;
    expected.Byte(kModelSlotCount - 1);
    expected.Byte(7);
    expected.Text("fixture", 7);
    expected.Byte(0);
    expected.Text("", 256);
    expected.Byte(0);
    expected.U32(1);
    expected.Byte(4);
    expected.Text("base", 4);
    expected.U32(1);
    expected.Byte(6);
    expected.Text("unused", 6);
    expected.U32(1);
    expected.U32(1);
    expected.U32(1);
    expected.U32(7);
    expected.Byte(0);
    expected.Byte(0);
    for (int i = 0; i < 5; ++i)
        expected.U32(0);
    expected.Byte(0);
    expected.U32(0);
    expected.U32(0);
    const auto denseBoneOffset = expected.data.size();
    // PMM 0002 bone keys occupy 58 disk bytes (runtime keys occupy 60).
    expected.Text("", 58);
    expected.U32(1);
    expected.U32(mdl::kBoneKeyCapacity - 1);
    Bytes sparseBone;
    sparseBone.U32(123);
    sparseBone.U32(3);
    sparseBone.U32(0);
    for (int lane = 0; lane < 4; ++lane)
        for (int column = 0; column < 4; ++column)
            sparseBone.Byte(1 + lane + column * 4);
    for (float value : {0.f, 2.f, 0.f, 0.f, 0.f, 0.f, 1.f})
        sparseBone.Float(value);
    sparseBone.Byte(1);
    sparseBone.Byte(1);
    const auto sparseBoneOffset = expected.data.size();
    expected.data.insert(expected.data.end(), sparseBone.data.begin(), sparseBone.data.end());
    expected.Text("", 17);
    expected.U32(1);
    expected.U32(mdl::kMorphKeyCapacity - 1);
    expected.U32(124);
    expected.U32(0);
    expected.U32(0);
    expected.Float(.75f);
    expected.Byte(1);
    // Display records serialize flags and references, never runtime pointers.
    for (unsigned frame : {0u, 125u}) {
        if (frame) {
            expected.U32(1);
            expected.U32(mdl::kDisplayKeyCapacity - 1);
        }
        expected.U32(frame);
        expected.U32(0);
        expected.U32(0);
        expected.Byte(1);
        expected.Byte(1);
        expected.U32(kModelSlotCount - 1);
        expected.U32(42);
        expected.Byte(1);
    }
    for (float value : {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f})
        expected.Float(value);
    expected.Byte(0);
    expected.Byte(0);
    expected.Byte(0);
    expected.Float(0);
    expected.Byte(0);
    expected.U32(10);
    expected.U32(20);
    expected.U32(kModelSlotCount - 1);
    expected.U32(42);
    expected.Byte(0);
    expected.Float(0);
    expected.Byte(0);
    expected.Byte(0);
    Check(
        actual == expected.data,
        "PMM model stream preserves packed keys, full sparse capacities, selectors and pose tail");
    mdl::BoneKey decoded{};
    _lseek(fd, static_cast<long>(sparseBoneOffset), SEEK_SET);
    pmm_io::ReadPmmBoneKey<pmm_io::PmmStream::V2>(fd, decoded);
    Check(_tell(fd) == sparseBoneOffset + 58 && decoded.frame == 123 && decoded.position[1] == 2 &&
              decoded.physicsDisabled == 1 &&
              std::memcmp(decoded.interpolation, bone.interpolation, 16) == 0,
          "PMM v2 reader consumes 58 bytes and restores transposed interpolation");
    decoded.physicsDisabled = 7;
    _lseek(fd, static_cast<long>(sparseBoneOffset), SEEK_SET);
    pmm_io::ReadPmmBoneKey<pmm_io::PmmStream::V1>(fd, decoded);
    Check(_tell(fd) == sparseBoneOffset + 57 && decoded.physicsDisabled == 7 &&
              decoded.allocated == 1 && denseBoneOffset < sparseBoneOffset,
          "PMM v1 reader consumes 57 bytes and leaves the absent physics byte untouched");
}
void TestVsq(bool pmx) {
    Fixture f;
    Model model;
    std::array<mdl::MorphRecord, 5> morphs{};
    const int firstMorph = pmx ? 0 : 1;
    model.record->physicsMode = pmx ? 2 : 0;
    const char* names[] = {"\x82\xa0", "\x82\xa2", "\x82\xa4", "\x82\xa8"};
    for (int i = 0; i < 4; ++i)
        strcpy_s(morphs[firstMorph + i].name, names[i]);
    model.record->morphs = morphs.data();
    model.record->morphCount = firstMorph + 4;
    model.bones[mdl::kBoneKeyCapacity - 1].allocated = 2;
    model.morphs[mdl::kMorphKeyCapacity - 1].allocated = 2;
    model.display[mdl::kDisplayKeyCapacity - 1].allocated = 2;
    f.Select(model);
    Bytes track;
    for (unsigned byte : {0u, 0xFFu, 0x51u, 3u, 7u, 0xA1u, 0x20u})
        track.Byte(byte);
    std::string text = "[EventList]\n0=ID#0000\n";
    char line[128];
    for (int i = 0; i < 5; ++i) {
        sprintf_s(line, "%d=ID#%04d\n", 480 + i * 960, i + 1);
        text += line;
    }
    for (int i = 0; i < 5; ++i) {
        sprintf_s(line, "[ID#%04d]\nLength=480\nLyricHandle=h#%04d\n", i + 1, i);
        text += line;
    }
    for (int i = 0; i < 5; ++i) {
        sprintf_s(line, "[h#%04d]\nL0=\"%c\",\"%c\"\n", i, "aiueo"[i], "aiueo"[i]);
        text += line;
    }
    // Each VSQ text meta-event carries an eight-byte DM prefix. Keep the
    // encoded length below 128, matching the original one-byte reader.
    for (std::size_t offset = 0; offset < text.size(); offset += 100) {
        const auto length = (std::min)(std::size_t{100}, text.size() - offset);
        for (unsigned byte : {0u, 0xFFu, 1u})
            track.Byte(byte);
        track.Byte(static_cast<unsigned>(length + 8));
        track.Text("DM:0000:", 8);
        for (std::size_t i = 0; i < length; ++i)
            track.Byte(text[offset + i]);
    }
    for (unsigned byte : {0u, 0xFFu, 0x2Fu, 0u})
        track.Byte(byte);
    Bytes midi;
    midi.Text("MThd", 4);
    for (unsigned byte : {0u, 0u, 0u, 6u, 0u, 0u, 0u, 1u, 1u, 0xE0u})
        midi.Byte(byte);
    midi.Text("MTrk", 4);
    for (int shift : {24, 16, 8, 0})
        midi.Byte(static_cast<unsigned>(track.data.size()) >> shift);
    midi.data.insert(midi.data.end(), track.data.begin(), track.data.end());
    TempFile file(L".vsq");
    file.Write(midi.data);
    const std::wstring originalPath = file.path;
    expectVmdRefresh = false;
    LoadVsqFile(f.app.get(), file.path);
    DeleteFileW(originalPath.c_str());
    Check(paints == 1 && seeks == 1 && selections == 0 && soughtModel == model.record.get() &&
              soughtFrame == 150 && f.app->state.currentFrame == 150 &&
              f.app->state.timelineStartFrame == 144 && f.app->LastRegisteredFrame() == 150,
          "VSQ parses SMF, selects the real model slot and seeks before refreshing the panel");
    bool correct = model.bones[mdl::kBoneKeyCapacity - 1].allocated == 0 &&
                   model.morphs[mdl::kMorphKeyCapacity - 1].allocated == 0 &&
                   model.display[mdl::kDisplayKeyCapacity - 1].allocated == 0;
    const int face[] = {firstMorph, firstMorph + 1, firstMorph + 2, firstMorph, firstMorph + 3};
    const float weight[] = {.7f, .6f, 1.f, .5f, 1.f};
    const auto hasKey = [&](int index, int frame, float value) {
        for (unsigned key = static_cast<unsigned>(index), visited = 0;
             visited < mdl::kMorphKeyCapacity; ++visited) {
            const auto& record = model.morphs[key];
            if (record.frame == frame)
                return record.value == value;
            if (!record.next)
                return false;
            key = record.next;
        }
        return false;
    };
    for (int i = 0; i < 5; ++i) {
        const int start = 15 + 30 * i;
        correct &= hasKey(face[i], start - 2, 0) && hasKey(face[i], start, weight[i]) &&
                   hasKey(face[i], start + 13, weight[i]) && hasKey(face[i], start + 15, 0);
    }
    correct &= hasKey(firstMorph + 1, 103, 0) && hasKey(firstMorph + 1, 105, .3f) &&
               hasKey(firstMorph + 1, 118, .3f) && hasKey(firstMorph + 1, 120, 0);
    Check(correct,
          "VSQ clears full-capacity marks and registers all five vowel envelopes, including e=A+I");
}
void TestVpd() {
    Fixture f;
    Model model;
    f.Select(model);
    model.selected[1] = 2;
    model.boneTable[1].trans[0] = 3;
    model.boneTable[1].ikBackup[0] = 9;
    model.boneTable[1].ikBackup[6] = 1;
    f.app->PlaybackPhysicsMode() = 2;
    TempFile file;
    for (unsigned flag : {0u, 1u, 2u, 3u}) {
        model.boneTable[1].hasRigidBody = static_cast<unsigned char>(flag);
        SaveVpdFile(file.path);
        const auto bytes = file.Read();
        const std::string text(bytes.begin(), bytes.end());
        Check(
            text.find("Bone0{target\r\n") != std::string::npos &&
                text.find(flag & 1 ? "  9.000000,0.000000,0.000000;"
                                   : "  3.000000,0.000000,0.000000;") != std::string::npos &&
                text.find("Bone1{") == std::string::npos,
            "VPD saves selected bones and uses the reference's raw rigid-body bit for pose source");
    }
    model.record->physicsMode = 2;
    model.record->boneCount = 3;
    for (int i : {1, 2}) {
        model.boneTable[i].type = i == 1 ? mdl::BoneType::FixedAxis : mdl::BoneType::UnderIk;
        model.boneTable[i].flags = mdl::kBoneFlagFixedAxis;
        model.boneTable[i].axis[1] = 1;
        model.boneTable[i].trans[0] = 10.f + i;
        model.edited[i] = static_cast<unsigned char>(4 + i);
    }
    model.record->UndoCursor() = mdl::kUndoHistoryDepth - 1;
    file.Write("Vocaloid Pose Data file\n\nfixture.osm;\n2;\n\n"
               "Bone0{target\n  1,2,3;\n  0.36,-0.48,0,0.8;\n}\n\n"
               "Bone1{missing\n  4,5,6;\n  0,0,0,1;\n}\n\n");
    LoadVpdFile(file.path);
    for (int i : {1, 2}) {
        const auto& bone = model.boneTable[i];
        Check(
            bone.trans[0] == 1 && bone.trans[1] == 2 && bone.rotQuat[0] == 0 &&
                std::fabs(bone.rotQuat[1] + .6f) < .000001f && bone.rotQuat[2] == 0 &&
                std::fabs(bone.rotQuat[3] - .8f) < .000001f && model.selected[i] == 1 &&
                model.edited[i] == 1,
            "VPD matches every duplicate name and constrains rotation to the bone's own fixed axis");
    }
    const auto& undo = model.record->undoRings[0].slots[0];
    Check(
        model.record->UndoCursor() == 0 && model.record->LatestUndoCursor() == 0 &&
            undo.operation == mdl::UndoOperation::BonePose && undo.recordCount == 2 &&
            undo.bonePose[0].boneIndex == 2 && undo.bonePose[0].position[0] == 12 &&
            undo.bonePose[0].poseEdited == 6 && undo.bonePose[1].boneIndex == 0 &&
            undo.bonePose[1].position[0] == 0,
        "VPD preserves file-record snapshots, last duplicate match, unmatched zeros and undo wrap");
}
}
namespace mikudancestudio {
// Keep parsing, registration, files, undo and D3DX real; observe only the
// application repaint and final scene-evaluation boundaries.
void PanelPaint(MMDApp*) {
    ++paints;
}
void SelectionReeval(MMDApp*) {
    ++selections;
}
int SeekModelFrame(mdl::ModelRecord& model, int frame, int physics) {
    Check(expectVmdRefresh ? paints == 1 && selections == 1 : paints == 0 && selections == 0,
          "file import preserves the original seek and refresh order");
    ++seeks;
    soughtModel = &model;
    soughtFrame = frame;
    soughtPhysics = physics;
    return 42;
}
void ReloadModels(MMDApp*) {
}
void ApplyGravityTrack(MMDApp*) {
}
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestVmdModel();
    TestVmdCamera();
    TestVmdFixedAxis();
    TestPmmModel();
    TestVsq(false);
    TestVsq(true);
    TestVpd();
    return failures ? 1 : 0;
}
