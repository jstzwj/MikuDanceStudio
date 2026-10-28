#include "model_format_win.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_reader.hpp"
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
#include <cstdlib>
#include <memory>
#include <vector>
namespace {
void Check(bool good, const char* message) {
    if (!good) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
}
int main() {
    const wchar_t* path = _wgetenv(L"MDS_MODEL_FIXTURE");
    if (!path || !*path) return 77;
    const char* x86Diagnostic = std::getenv("MDS_MODEL_RUNTIME_X86_DIAGNOSTIC");
    if (sizeof(void*) != 8 &&
        (!x86Diagnostic || std::strcmp(x86Diagnostic, "1") != 0)) return 77;
    int file = -1;
    Check(_wsopen_s(&file, path, _O_BINARY | _O_RDONLY,
                    _SH_DENYNO, _S_IREAD) == 0, "open real PMX");
    mikudancestudio::format::ModelDocument disk;
    Check(mikudancestudio::format::ReadModelDocument(
              mikudancestudio::ReadModelFileBytesResult, &file, disk), "parse real PMX");
    Check(disk.kind == mikudancestudio::format::ModelKind::Pmx, "real PMX kind");
    Check(_lseek(file, 3, SEEK_SET) == 3, "rewind real PMX");
    std::fprintf(stderr, "real PMX: parsed; creating D3D\n");
    HWND window = CreateWindowW(L"STATIC", L"PMX runtime parity", WS_OVERLAPPED,
                                0, 0, 64, 64, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!window) return 77;
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return 77;
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    IDirect3DDevice9* device = nullptr;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device))) return 77;
    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = device;
    mikudancestudio::PhysicsScene scene{};
    Check(mikudancestudio::SceneConstruct(&scene, renderer.get()), "build physics scene");
    auto storage = std::make_unique<mikudancestudio::mdl::ModelRecord>();
    mikudancestudio::ModelInitDefaults(storage.get());
    auto& runtime = *storage.get();
    runtime.hwnd = window;
    runtime.scenePtr = &scene;
    runtime.physicsFlags = 1;
    const wchar_t* filename = wcsrchr(path, L'\\');
    Check(filename && static_cast<std::size_t>(filename - path + 1) <
          sizeof runtime.modelDirectory / sizeof(wchar_t), "real PMX directory");
    wcsncpy_s(runtime.modelDirectory, path, filename - path + 1);
    mikudancestudio::PathResolutionWorkspace paths{};
    std::fprintf(stderr, "real PMX: loading runtime\n");
    Check(mikudancestudio::LoadPMX(storage.get(), renderer.get(), 0, 1, paths, file),
          "load real PMX runtime");
    std::fprintf(stderr, "real PMX: comparing\n");
    Check(runtime.vertexCount == disk.pmx.vertices.size() &&
          runtime.indexCount == disk.pmx.indices.size() &&
          runtime.materialCount == disk.pmx.materials.size() &&
          runtime.boneCount == disk.pmx.bones.size() &&
          runtime.morphCount == disk.pmx.morphs.size() &&
          runtime.rigidCount == disk.pmx.rigids.size() &&
          runtime.jointCount == disk.pmx.joints.size(), "real PMX section counts");
    if (!disk.pmx.vertices.empty())
        Check(runtime.pmxVertices[0].position[0] == disk.pmx.vertices[0].position[0],
              "real PMX vertex");
    if (!disk.pmx.materials.empty())
        Check(runtime.materials[0].diffuse[0] ==
              disk.pmx.materials[0].fields.diffuse[0], "real PMX material");
    if (!disk.pmx.bones.empty())
        Check(runtime.boneTable[0].position[0] ==
              disk.pmx.bones[0].fields.position[0], "real PMX bone");
    if (!disk.pmx.rigids.empty())
        Check(runtime.rigidTable[0].shape == disk.pmx.rigids[0].fields.shape,
              "real PMX rigid");
    if (!disk.pmx.joints.empty())
        Check(runtime.jointTable[0].rigidA == disk.pmx.joints[0].fields.rigidA,
              "real PMX joint");
    std::fprintf(stderr, "real PMX: disposing model\n");
    mikudancestudio::ModelDispose(storage.get());
    std::fprintf(stderr, "real PMX: disposing scene\n");
    mikudancestudio::DisposePhysicsWorld(&scene);
    std::fprintf(stderr, "real PMX: releasing D3D\n");
    device->Release();
    d3d->Release();
    DestroyWindow(window);
}
