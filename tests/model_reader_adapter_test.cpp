#include "model_reader_fixture.hpp"
#include "model_format_win.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <Windows.h>
#include <d3d9.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
void Check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
int OpenFixture(const model_reader_fixture::Bytes& fixture, wchar_t (&path)[MAX_PATH]) {
    wchar_t directory[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, directory) != 0, "temporary directory");
    Check(GetTempFileNameW(directory, L"mdr", 0, path) != 0, "temporary file");
    int file = -1;
    Check(_wsopen_s(&file, path, _O_BINARY | _O_CREAT | _O_TRUNC | _O_RDWR,
                    _SH_DENYNO, _S_IREAD | _S_IWRITE) == 0, "open fixture");
    Check(_write(file, fixture.data.data(),
                 static_cast<unsigned>(fixture.data.size())) ==
              static_cast<int>(fixture.data.size()), "write fixture");
    _lseek(file, 0, SEEK_SET);
    return file;
}
void CompareStream(model_reader_fixture::Bytes fixture) {
    mikudancestudio::format::ModelDocument memory, fileDocument;
    Check(mikudancestudio::format::ReadModelDocument(
              model_reader_fixture::Bytes::read, &fixture, memory),
          "memory document");
    wchar_t path[MAX_PATH]{};
    int file = OpenFixture(fixture, path);
    Check(mikudancestudio::format::ReadModelDocument(
              mikudancestudio::ReadModelFileBytesResult, &file, fileDocument),
          "CRT document");
    Check(_tell(file) == static_cast<long>(fixture.data.size()), "stream offset");
    Check(memory.kind == fileDocument.kind, "model kind");
    if (memory.kind == mikudancestudio::format::ModelKind::Pmd) {
        Check(memory.pmd.vertices.size() == fileDocument.pmd.vertices.size(),
              "PMD vertex count");
        Check(memory.pmd.bones[0].position[0] ==
              fileDocument.pmd.bones[0].position[0], "PMD bone position");
        Check(memory.pmd.englishModel.name[0] ==
              fileDocument.pmd.englishModel.name[0], "PMD English name");
        Check(memory.pmd.joints[0].rigidA == fileDocument.pmd.joints[0].rigidA,
              "PMD joint");
    } else {
        Check(memory.pmx.vertices.size() == fileDocument.pmx.vertices.size(),
              "PMX vertex count");
        Check(memory.pmx.materials[0].fields.faceVertexCount ==
              fileDocument.pmx.materials[0].fields.faceVertexCount,
              "PMX material faces");
        Check(memory.pmx.morphs[0].offsets[0].values[0] ==
              fileDocument.pmx.morphs[0].offsets[0].values[0], "PMX morph");
    }
    _close(file);
    DeleteFileW(path);
}
void ComparePmxRuntime(IDirect3DDevice9* device, HWND window) {
    auto fixture = model_reader_fixture::Pmx();
    mikudancestudio::format::ModelDocument disk;
    Check(mikudancestudio::format::ReadModelDocument(
              model_reader_fixture::Bytes::read, &fixture, disk), "PMX document");
    wchar_t path[MAX_PATH]{};
    int file = OpenFixture(fixture, path);
    Check(_lseek(file, 3, SEEK_SET) == 3, "PMX signature offset");
    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = device;
    auto storage = std::make_unique<mikudancestudio::mdl::ModelRecord>();
    mikudancestudio::ModelInitDefaults(storage.get());
    auto& runtime = *storage.get();
    runtime.hwnd = window;
    runtime.physicsFlags = 1;
    mikudancestudio::PathResolutionWorkspace paths{};
    std::fprintf(stderr, "PMX: runtime load\n");
    const bool loaded = mikudancestudio::LoadPMX(
        storage.get(), renderer.get(), 0, 1, paths, file);
    DeleteFileW(path);
    Check(loaded, "Windows PMX load");
    std::fprintf(stderr, "PMX: compare\n");
    Check(runtime.vertexCount == disk.pmx.vertices.size(), "runtime vertices");
    Check(runtime.indexCount == disk.pmx.indices.size(), "runtime indices");
    Check(runtime.materialCount == disk.pmx.materials.size(), "runtime materials");
    Check(runtime.boneCount == disk.pmx.bones.size(), "runtime bones");
    Check(runtime.morphCount == disk.pmx.morphs.size(), "runtime morphs");
    Check(runtime.materials[0].diffuse[0] == disk.pmx.materials[0].fields.diffuse[0],
          "runtime material diffuse");
    Check(runtime.pmxVertices[0].position[0] == disk.pmx.vertices[0].position[0],
          "runtime vertex position");
    Check(runtime.indices[0] ==
          disk.pmx.indices[0], "runtime face index");
    Check(runtime.materials[0].faceVertexCount ==
          disk.pmx.materials[0].fields.faceVertexCount, "runtime material faces");
    Check(runtime.boneTable[0].position[0] == disk.pmx.bones[0].fields.position[0],
          "runtime bone position");
    Check(runtime.boneTable[0].parent == disk.pmx.bones[0].fields.parent,
          "runtime bone parent");
    Check(runtime.morphs[0].vertexEntries[0].offset[0] ==
          disk.pmx.morphs[0].offsets[0].values[0], "runtime morph offset");
    Check(runtime.displayRootBone == disk.pmx.frames[0].entries[0].index,
          "runtime root display bone");
    std::fprintf(stderr, "PMX: model dispose\n");
    mikudancestudio::ModelDispose(storage.get());
    std::fprintf(stderr, "PMX: done\n");
}
void ComparePmdRuntime(IDirect3DDevice9* device, HWND window) {
    auto fixture = model_reader_fixture::Pmd(true, false);
    mikudancestudio::format::ModelDocument disk;
    Check(mikudancestudio::format::ReadModelDocument(
              model_reader_fixture::Bytes::read, &fixture, disk), "PMD document");
    wchar_t path[MAX_PATH]{};
    int file = OpenFixture(fixture, path);
    _close(file);
    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = device;
    auto storage = std::make_unique<mikudancestudio::mdl::ModelRecord>();
    mikudancestudio::ModelInitDefaults(storage.get());
    mikudancestudio::PathResolutionWorkspace paths{};
    wchar_t originalDirectory[MAX_PATH]{};
    Check(GetCurrentDirectoryW(MAX_PATH, originalDirectory) != 0,
          "current directory");
    std::fprintf(stderr, "PMD: runtime load\n");
    const bool loaded = mikudancestudio::ModelLoadPMD(
        storage.get(), window, path, renderer.get(), 0, 0, 1, nullptr, paths);
    SetCurrentDirectoryW(originalDirectory);
    DeleteFileW(path);
    Check(loaded, "Windows PMD load");
    std::fprintf(stderr, "PMD: compare\n");
    auto& runtime = *storage.get();
    Check(runtime.vertexCount == disk.pmd.vertices.size(), "PMD runtime vertices");
    Check(runtime.indexCount == disk.pmd.indices.size(), "PMD runtime indices");
    Check(runtime.materialCount == disk.pmd.materials.size(), "PMD runtime materials");
    Check(runtime.boneCount == disk.pmd.bones.size(), "PMD runtime bones");
    Check(runtime.morphCount == disk.pmd.morphs.size(), "PMD runtime morphs");
    Check(runtime.materials[0].diffuse[0] == disk.pmd.materials[0].diffuse[0],
          "PMD runtime material");
    Check(runtime.boneTable[0].position[0] == disk.pmd.bones[0].position[0],
          "PMD runtime bone");
    Check(runtime.morphs[0].vertexEntries[0].offset[0] ==
          disk.pmd.morphs[0].offsets[0].offset[0], "PMD runtime morph");
    std::fprintf(stderr, "PMD: model dispose\n");
    mikudancestudio::ModelDispose(storage.get());
    std::fprintf(stderr, "PMD: done\n");
}
void ComparePmxPhysicsRuntime(IDirect3DDevice9* device, HWND window) {
    auto fixture = model_reader_fixture::Pmx(true);
    mikudancestudio::format::ModelDocument disk;
    Check(mikudancestudio::format::ReadModelDocument(
              model_reader_fixture::Bytes::read, &fixture, disk), "PMX physics document");
    wchar_t path[MAX_PATH]{};
    int file = OpenFixture(fixture, path);
    Check(_lseek(file, 3, SEEK_SET) == 3, "PMX physics signature offset");
    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = device;
    mikudancestudio::PhysicsScene scene{};
    Check(mikudancestudio::SceneConstruct(&scene, renderer.get()), "PMX physics scene");
    auto storage = std::make_unique<mikudancestudio::mdl::ModelRecord>();
    mikudancestudio::ModelInitDefaults(storage.get());
    auto& runtime = *storage.get();
    runtime.hwnd = window;
    runtime.scenePtr = &scene;
    runtime.physicsFlags = 1;
    mikudancestudio::PathResolutionWorkspace paths{};
    Check(mikudancestudio::LoadPMX(storage.get(), renderer.get(), 0, 1, paths, file),
          "Windows PMX physics load");
    DeleteFileW(path);
    Check(runtime.rigidCount == disk.pmx.rigids.size() &&
          runtime.jointCount == disk.pmx.joints.size() &&
          runtime.jointTable[0].rigidA == disk.pmx.joints[0].fields.rigidA,
          "PMX physics runtime fields");
    mikudancestudio::ModelDispose(storage.get());
    mikudancestudio::DisposePhysicsWorld(&scene);
}
}
int main(int argc, char** argv) {
    try {
        const bool pmd = argc == 2 && std::strcmp(argv[1], "pmd") == 0;
        const bool pmx = argc == 2 && std::strcmp(argv[1], "pmx") == 0;
        const bool physics = argc == 2 && std::strcmp(argv[1], "pmx_physics") == 0;
        if (!pmd && !pmx && !physics) return 2;
        std::fprintf(stderr, "%s: stream\n", argv[1]);
        if (pmd) CompareStream(model_reader_fixture::Pmd());
        else CompareStream(model_reader_fixture::Pmx());
        HWND window = CreateWindowW(L"STATIC", L"Model reader parity",
                                    WS_OVERLAPPED, 0, 0, 64, 64, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return 77;
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) { DestroyWindow(window); return 77; }
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        IDirect3DDevice9* device = nullptr;
        const HRESULT result = d3d->CreateDevice(
            0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &parameters, &device);
        if (FAILED(result)) { d3d->Release(); DestroyWindow(window); return 77; }
        std::fprintf(stderr, "%s: load and compare\n", argv[1]);
        if (pmd) ComparePmdRuntime(device, window);
        else if (pmx) ComparePmxRuntime(device, window);
        else ComparePmxPhysicsRuntime(device, window);
        std::fprintf(stderr, "%s: device teardown\n", argv[1]);
        device->Release();
        d3d->Release();
        DestroyWindow(window);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
