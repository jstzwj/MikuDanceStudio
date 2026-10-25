#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;
namespace {
int failures = 0;
std::vector<mdl::CameraClipboardRecord> cameras;
mdl::LightClipboardRecord light{};
mdl::ShadowClipboardRecord shadow{};
mdl::BoneClipboardRecord bone{};
mdl::MorphClipboardRecord morph{};
mdl::IkClipboardState ik{};
int displayVisible = -1;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
struct FileBytes {
    std::vector<unsigned char> data;
    void Byte(unsigned value) { data.push_back(static_cast<unsigned char>(value)); }
    void Word(unsigned value) {
        Byte(value);
        Byte(value >> 8);
    }
    void U32(std::uint32_t value) {
        Word(value);
        Word(value >> 16);
    }
    void Float(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        U32(bits);
    }
    void Text(const char* text, int count) {
        const auto length = std::strlen(text);
        for (int i = 0; i < count; ++i)
            Byte(i < length ? text[i] : 0);
    }
    void Save(const wchar_t* path) {
        FILE* file = nullptr;
        _wfopen_s(&file, path, L"wb");
        if (!file) {
            ++failures;
            return;
        }
        Check(std::fwrite(data.data(), 1, data.size(), file) == data.size(), "write VMD fixture");
        std::fclose(file);
    }
};
}
namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
MMDApp* g_Block = nullptr;
int LoadVmdMotion(MMDApp*, const wchar_t*);
int RegisterCameraKey(MMDApp*, const void* record, int) {
    cameras.push_back(*static_cast<const mdl::CameraClipboardRecord*>(record));
    return 1;
}
int RegisterLightKey(MMDApp*, const void* record) {
    light = *static_cast<const mdl::LightClipboardRecord*>(record);
    return 1;
}
int RegisterSelfShadowKey(MMDApp*, const void* record) {
    shadow = *static_cast<const mdl::ShadowClipboardRecord*>(record);
    return 1;
}
bool RegisterBoneKey(unsigned char*, mdl::BoneClipboardRecord& record, int, unsigned char) {
    bone = record;
    return true;
}
bool RegisterMorphKeyFromRecord(unsigned char*, const mdl::MorphClipboardRecord& record, int) {
    morph = record;
    return true;
}
bool RegisterDisplayKeyFromRecord(unsigned char*, int, unsigned char visible, int count,
                                  const mdl::IkClipboardState* states, int,
                                  const mdl::SelectorClipboardState*, int) {
    displayVisible = visible;
    if (count == 1)
        ik = states[0];
    return count == 1;
}
void ResetBoneKeyCursor(unsigned char*) {
}
int ResetMorphKeyCursor(unsigned char*) {
    return 0;
}
void ResetDisplayKeyCursor(unsigned char*) {
}
int SeekModelFrame(mdl::ModelRecord&, int, int) {
    return 42;
}
void PanelPaint(MMDApp*) {
}
void SelectionReeval(MMDApp*) {
}
void ReloadModels(MMDApp*) {
}
void RefreshLightPanel(MMDApp*) {
}
void RefreshSelfShadowPanel(MMDApp*) {
}
void ApplyGravityTrack(MMDApp*) {
}
}
int main() {
    auto app = std::make_unique<mds::MMDApp>();
    wchar_t temp[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"vmd", 0, path))
        return 1;
    app->CameraMode() = 1;
    for (int version : {1, 2}) {
        cameras.clear();
        FileBytes file;
        file.Text(version == 1 ? "Vocaloid Motion Data file" : "Vocaloid Motion Data 0002", 30);
        file.Text("\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe", version == 1 ? 10 : 20);
        file.U32(0);
        file.U32(0);
        file.U32(2);
        for (int i = 0; i < 2; ++i) {
            file.U32(11 + i);
            file.Float(-42.5f);
            for (int j = 0; j < 6; ++j)
                file.Float(1.25f + j);
            for (int j = 0; j < (version == 1 ? 1 : 6); ++j)
                for (int column = 0; column < 4; ++column)
                    file.Byte(10 * column + j);
            if (version == 2) {
                file.U32(37);
                file.Byte(i == 0 ? 0 : 2);
            }
        }
        file.U32(1);
        file.U32(31);
        for (int j = 0; j < 6; ++j)
            file.Float(.125f + j);
        if (version == 2) {
            file.U32(1);
            file.U32(41);
            file.Byte(2);
            file.Float(.0125f);
        }
        file.Save(path);
        mds::LoadVmdMotion(app.get(), path);
        Check(cameras.size() == 2, "decode both camera records");
        if (cameras.size() != 2)
            continue;
        const auto& camera = cameras[0];
        Check(camera.frame == 11 && camera.distance == -42.5f && camera.eye[0] == 1.25f &&
                  camera.target[2] == 6.25f && camera.parentModel == -1 && camera.parentBone == 0,
              "disk camera field order maps to native members");
        Check(camera.fov == (version == 1 ? 45 : 37) && camera.perspective == 0 &&
                  cameras[1].perspective == (version == 1 ? 0 : 1),
              "camera perspective normalizes only its input byte");
        for (int j = 0; j < 6; ++j) {
            const int increment = version == 1 ? 0 : j;
            Check(camera.interpolation[j] == increment &&
                      camera.interpolation[12 + j] == 10 + increment &&
                      camera.interpolation[6 + j] == 20 + increment &&
                      camera.interpolation[18 + j] == 30 + increment,
                  "VMD interpolation columns retain the original transposition");
        }
        Check(light.frame == 31 && light.color[0] == .125f && light.direction[0] == 3.125f,
              "light disk colors precede direction");
        if (version == 2)
            Check(shadow.frame == 41 && shadow.mode == 2 && shadow.distance == .0125f,
                  "shadow's packed nine disk bytes decode independently of native padding");
    }
    auto model = std::make_unique<mdl::ModelRecord>();
    auto boneKeys = std::make_unique<mdl::BoneKey[]>(mdl::kBoneKeyCapacity);
    auto morphKeys = std::make_unique<mdl::MorphKey[]>(mdl::kMorphKeyCapacity);
    auto displayKeys = std::make_unique<mdl::DisplayKey[]>(mdl::kDisplayKeyCapacity);
    model->boneKeys = boneKeys.get();
    model->morphKeys = morphKeys.get();
    model->displayKeys = displayKeys.get();
    strcpy_s(model->name, "fixture");
    app->CameraMode() = 0;
    app->ModelSlot(0) = reinterpret_cast<unsigned char*>(model.get());
    FileBytes file;
    file.Text("Vocaloid Motion Data 0002", 30);
    file.Text("fixture", 20);
    file.U32(1);
    file.Text("bone", 15);
    file.U32(5);
    for (int j = 0; j < 7; ++j)
        file.Float(.5f + j);
    for (int channel = 0; channel < 4; ++channel) {
        if (channel == 0) {
            file.Word(20);
            file.Word(3939);
        } else
            file.U32(20 + channel);
        for (int column = 1; column < 4; ++column)
            file.U32(20 + channel + column * 10);
    }
    file.U32(1);
    file.Text("smile", 15);
    file.U32(7);
    file.Float(.75f);
    file.U32(0);
    file.U32(0);
    file.U32(0); // camera/light/shadow counts
    file.U32(1);
    file.U32(9);
    file.Byte(0);
    file.U32(1);
    file.Text("legIK", 20);
    file.Byte(0);
    file.Save(path);
    Check(mds::LoadVmdMotion(app.get(), path) == 42, "model load reaches frame evaluation");
    Check(bone.frame == 5 && bone.position[0] == .5f && bone.rotation[0] == 3.5f &&
              bone.physicsDisabled == 1 && bone.interpolation[0] == 20 &&
              bone.interpolation[15] == 53,
          "bone disk layout and physics marker decode correctly");
    Check(std::strcmp(morph.name, "smile") == 0 && morph.frame == 7 && morph.value == .75f,
          "decode morph record");
    Check(displayVisible == 0 && std::strcmp(ik.boneName, "legIK") == 0 && ik.enabled == 0,
          "zero display/IK bytes stay zero after the 3939 physics marker");
    for (auto& ring : model->undoRings)
        for (auto& slot : ring.slots) {
            ::operator delete(slot.bonePose);
            ::operator delete(slot.auxiliaryPose);
        }
    DeleteFileW(path);
    return failures ? 1 : 0;
}
