#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace mikudancestudio;
namespace {
int failures;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
struct File {
    wchar_t path[MAX_PATH]{};
    File() {
        wchar_t directory[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"pmd", 0, path))
            std::abort();
    }
    ~File() { DeleteFileW(path); }
    std::vector<unsigned char> Read() const {
        FILE* stream = nullptr;
        _wfopen_s(&stream, path, L"rb");
        if (!stream)
            std::abort();
        std::fseek(stream, 0, SEEK_END);
        std::vector<unsigned char> bytes(std::ftell(stream));
        std::rewind(stream);
        Check(std::fread(bytes.data(), 1, bytes.size(), stream) == bytes.size(), "read PMD output");
        std::fclose(stream);
        return bytes;
    }
};
// Expected bytes describe PMD fields explicitly; runtime record padding and
// pointer width never determine the expected stream.
struct Bytes {
    std::vector<unsigned char> data;
    void U(unsigned value, int width) {
        for (int i = 0; i < width; ++i)
            data.push_back(static_cast<unsigned char>(value >> (8 * i)));
    }
    void Float(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        U(bits, 4);
    }
    void Text(const char* text, std::size_t width) {
        const std::size_t size = std::strlen(text);
        for (std::size_t i = 0; i < width; ++i)
            data.push_back(i < size ? static_cast<unsigned char>(text[i]) : 0);
    }
    void Floats(std::initializer_list<float> values) {
        for (float value : values)
            Float(value);
    }
};
void Test(bool pmx, bool facial) {
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    auto unselected = std::make_unique<mdl::ModelRecord>();
    auto renderer = std::make_unique<D3DRenderer>();
    std::array<mdl::PmdVertex, 4> vertices{};
    std::array<std::int32_t, 3> indices{3, 1, 2};
    mdl::ModelMaterialRecord material{};
    mdl::BoneRecord bone{};
    mdl::IkChain ik{};
    std::array<std::uint16_t, 2> links{0, 0};
    std::array<mdl::MorphRecord, 2> morphs{};
    std::array<mdl::PmdVertexMorphEntry, 2> base{{{3, {30, 31, 32}}, {1, {10, 11, 12}}}};
    mdl::PmdVertexMorphEntry smile{3, {1, 2, 3}};
    mdl::FrameGroup face{}, displayBone{};
    std::array<mdl::DisplayGroup, 4> groups{};
    mdl::RigidRecord rigid{};
    mdl::JointRecord joint{};
    model->physicsMode = pmx ? 2 : 0;
    strcpy_s(model->name, "typed model");
    strcpy_s(model->nameEn, "english model");
    strcpy_s(model->comment, "comment");
    strcpy_s(model->commentEn, "english comment");
    model->vertexCount = 4;
    model->rawVertices = vertices.data();
    for (int i = 0; i < 4; ++i) {
        vertices[i].position[0] = static_cast<float>(i);
        vertices[i].normal[1] = 1;
        vertices[i].uv[0] = .25f;
        vertices[i].bone[0] = 0;
        vertices[i].bone[1] = 0x1234;
        vertices[i].weightPercent = 75;
        vertices[i].edgeDisabled = i == 2 ? 7 : 0;
    }
    model->indexCount = 3;
    model->indices = indices.data();
    model->materialCount = 1;
    model->materials = &material;
    for (int i = 0; i < 4; ++i)
        material.diffuse[i] = static_cast<float>(i + 1);
    material.specularPower = 5;
    material.specular[0] = 6;
    material.ambient[1] = 7;
    material.toonReference = 8;
    material.doubleSided = 9;
    material.faceVertexCount = 3;
    wcscpy_s(model->modelDirectory, L"root\\");
    wcscpy_s(material.texturePath, L"root\\skin.png");
    wcscpy_s(material.spherePath, L"root\\shine.sph");
    model->boneCount = 1;
    model->boneTable = &bone;
    strcpy_s(bone.name, "root");
    strcpy_s(bone.nameEn, "english root");
    bone.parent = -1;
    bone.tailIdx = -1;
    bone.type = mdl::BoneType::FixedAxis;
    bone.position[1] = 2;
    model->ikChainCount = 1;
    model->ikChains = &ik;
    ik.linkCount = 2;
    ik.links = links.data();
    ik.iterations = 7;
    ik.maxAngle = .5f;
    model->morphCount = 2;
    model->morphs = morphs.data();
    model->morph0Count = 2;
    model->morph0Table = base.data();
    strcpy_s(morphs[0].name, "base");
    morphs[0].offsetCount = 2;
    morphs[0].vertexEntries = base.data();
    strcpy_s(morphs[1].name, "smile");
    strcpy_s(morphs[1].nameEn, "english smile");
    morphs[1].panel = mdl::MorphPanel::eyebrow;
    morphs[1].offsetCount = 1;
    morphs[1].vertexEntries = &smile;
    model->facialFrameCount = facial ? 1 : 0;
    model->displayFrames = &face;
    face.targetIndex = 1;
    model->groupCount = facial ? 4 : 3;
    model->displayGroups = groups.data();
    strcpy_s(groups[model->groupCount - 1].name, "custom group");
    strcpy_s(groups[model->groupCount - 1].nameEn, "english group");
    model->displayBoneCount = 1;
    model->displayBones = &displayBone;
    displayBone.groupIndex = model->groupCount - 1;
    for (int i = 0; i < 10; ++i)
        sprintf_s(model->pmdToonFileNames[i], "toon%02d.bmp", i + 1);
    model->rigidCount = 1;
    model->rigidTable = &rigid;
    strcpy_s(rigid.name, "rigid");
    rigid.boneIndex = -1;
    rigid.group = 3;
    rigid.noCollapse = 0x1234;
    rigid.shape = 2;
    rigid.size[0] = 4;
    rigid.position[1] = 5;
    rigid.rotation[2] = 6;
    rigid.mass = 7;
    rigid.linearDamping = 8;
    rigid.angularDamping = 9;
    rigid.restitution = 10;
    rigid.friction = 11;
    rigid.mode = 2;
    model->jointCount = 1;
    model->jointTable = &joint;
    strcpy_s(joint.name, "joint");
    joint.rigidB = 0x12345678;
    for (int i = 0; i < 12; ++i)
        joint.limits[i] = static_cast<float>(i + 1);
    for (int i = 0; i < 6; ++i)
        joint.springs[i] = static_cast<float>(i + 20);
    app->ModelSlot(0) = unselected.get();
    app->SetSelectedModelSlot(kModelSlotCount - 1);
    app->SelectedModel() = model.get();
    app->Renderer() = renderer.get();
    File file;
    SaveEnhancedModel(app.get(), file.path);
    Check(wcscmp(model->path, file.path) == 0 && unselected->path[0] == 0,
          "menu wrapper saves the selected sparse model slot and updates only its path");
    Check(vertices[1].position[0] == 10 && vertices[3].position[0] == 30,
          "PMD save overwrites base-morph positions before writing vertices");
    Bytes expected;
    expected.Text("Pmd", 3);
    expected.Float(1);
    expected.Text("typed model", 20);
    expected.Text("comment", 256);
    expected.U(4, 4);
    for (int i = 0; i < 4; ++i) {
        if (i == 1)
            expected.Floats({10, 11, 12});
        else if (i == 3)
            expected.Floats({30, 31, 32});
        else
            expected.Floats({static_cast<float>(i), 0, 0});
        expected.Floats({0, 1, 0, .25f, 0});
        expected.U(0, 2);
        expected.U(0x1234, 2);
        expected.U(75, 1);
        expected.U(i == 2, 1);
    }
    expected.U(3, 4);
    for (unsigned index : {3u, 1u, 2u})
        expected.U(index, 2);
    expected.U(1, 4);
    expected.Floats({1, 2, 3, 4, 5, 6, 0, 0, 0, 7, 0});
    expected.U(8, 1);
    expected.U(1, 1);
    expected.U(3, 4);
    expected.Text("skin.png*shine.sph", 20);
    expected.U(1, 2);
    expected.Text("root", 20);
    expected.U(0xFFFF, 2);
    expected.U(0, 2);
    expected.U(8, 1);
    expected.U(0xFFFF, 2);
    expected.Floats({0, 2, 0});
    expected.U(1, 2);
    expected.U(0, 2);
    expected.U(0, 2);
    expected.U(2, 1);
    expected.U(7, 2);
    expected.Float(.5f);
    expected.U(0, 2);
    expected.U(0, 2);
    expected.U(2, 2);
    expected.Text("base", 20);
    expected.U(2, 4);
    expected.U(0, 1);
    expected.U(3, 4);
    expected.Floats({30, 31, 32});
    expected.U(1, 4);
    expected.Floats({10, 11, 12});
    expected.Text("smile", 20);
    expected.U(1, 4);
    expected.U(1, 1);
    expected.U(0, 4); // Raw vertex 3 maps to entry 0 of the base morph.
    expected.Floats({1, 2, 3});
    expected.U(facial, 1);
    if (facial)
        expected.U(1, 2);
    expected.U(1, 1);
    expected.Text("custom group", 50);
    expected.U(1, 4);
    expected.U(0, 2);
    expected.U(1, 1);
    expected.U(1, 1);
    expected.Text("english model", 20);
    expected.Text("english comment", 256);
    expected.Text("english root", 20);
    expected.Text("english smile", 20);
    expected.Text("english group", 50);
    for (int i = 1; i <= 10; ++i) {
        char toon[100];
        sprintf_s(toon, "toon%02d.bmp", i);
        expected.Text(toon, 100);
    }
    expected.U(1, 4);
    expected.Text("rigid", 20);
    expected.U(0xFFFF, 2);
    expected.U(3, 1);
    expected.U(0x1234, 2);
    expected.U(2, 1);
    expected.Floats({4, 0, 0, 0, 5, 0, 0, 0, 6, 7, 8, 9, 10, 11});
    expected.U(2, 1);
    expected.U(1, 4);
    expected.Text("joint", 20);
    expected.U(0, 4);
    expected.U(0x12345678, 4);
    expected.Floats({0, 0, 0, 0, 0, 0, 4, 5, 6, 1, 2, 3, 10, 11, 12, 7, 8, 9});
    expected.Floats({20, 21, 22, 23, 24, 25});
    const auto actual = file.Read();
    if (actual != expected.data) {
        std::size_t offset = 0;
        while (offset < actual.size() && offset < expected.data.size() &&
               actual[offset] == expected.data[offset])
            ++offset;
        std::fprintf(stderr, "PMX=%d facial=%d: sizes %zu/%zu, first mismatch at %zu\n", pmx,
                     facial, actual.size(), expected.data.size(), offset);
    }
    Check(
        actual == expected.data,
        "PMD stream preserves field widths, index stride, morph remap, display groups and physics");
}
} // namespace
int main() {
    for (bool pmx : {false, true})
        for (bool facial : {false, true})
            Test(pmx, facial);
    return failures ? 1 : 0;
}
