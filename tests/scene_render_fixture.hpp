#pragma once

#define NOMINMAX
#include "scene_fixture.hpp"
#include "original_physics_module.hpp"
#include "../src/render/model_materials.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p)
            p->Release();
    }
    T** Out() {
        Check(p == nullptr, "COM output already occupied");
        return &p;
    }
    T* operator->() const { return p; }
};
void Hr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: %08lX\n", operation, static_cast<unsigned long>(result));
        throw std::runtime_error(operation);
    }
}
constexpr UINT kSize = 512;
constexpr DWORD kBackground = 0xFFB0B8C0;
constexpr UINT kD3dxDefault = static_cast<UINT>(-1);
constexpr int kPngFormat = 3; // D3DXIMAGE_FILEFORMAT::D3DXIFF_PNG in d3dx9_32.
struct SharedToons {
    MMDApp& app;
    std::array<Com<IDirect3DTexture9>, 11> textures;
    explicit SharedToons(Fixture& f) : app(*f.app) {
        for (int i = 0; i < 11; ++i) {
            char name[32]{};
            sprintf_s(name, "toon%02d.png", i);
            const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "res" / "assets" / name;
            Hr(D3DXCreateTextureFromFileExW(f.device, path.c_str(), kD3dxDefault, kD3dxDefault, 1,
                                            0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, kD3dxDefault,
                                            kD3dxDefault, 0, nullptr, nullptr, textures[i].Out()),
               "load shared Toon texture");
            app.ToonTexture(i) = textures[i].p;
        }
    }
    ~SharedToons() {
        for (int i = 0; i < 11; ++i)
            app.ToonTexture(i) = nullptr;
    }
};
struct RenderTarget {
    IDirect3DDevice9* device;
    UINT size;
    Com<IDirect3DSurface9> previous, previousDepth, color, depth, readback;
    explicit RenderTarget(IDirect3DDevice9* value, UINT dimension = kSize)
        : device(value), size(dimension) {
        Hr(device->GetRenderTarget(0, previous.Out()), "previous target");
        device->GetDepthStencilSurface(previousDepth.Out());
        Hr(device->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                      color.Out(), nullptr),
           "image target");
        Hr(device->CreateDepthStencilSurface(size, size, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                             depth.Out(), nullptr),
           "image depth");
        Hr(device->CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                               readback.Out(), nullptr),
           "image readback");
        Hr(device->SetRenderTarget(0, color.p), "bind image target");
        Hr(device->SetDepthStencilSurface(depth.p), "bind image depth");
        const D3DVIEWPORT9 viewport{0, 0, size, size, 0, 1};
        Hr(device->SetViewport(&viewport), "image viewport");
    }
    ~RenderTarget() {
        device->SetRenderTarget(0, previous.p);
        device->SetDepthStencilSurface(previousDepth.p);
    }
    std::vector<DWORD> Pixels(const std::filesystem::path& path = {}) {
        Hr(device->GetRenderTargetData(color.p, readback.p), "read GPU pixels");
        if (!path.empty())
            Hr(D3DXSaveSurfaceToFileW(path.c_str(), kPngFormat, readback.p, nullptr, nullptr),
               "save GPU image");
        D3DLOCKED_RECT lock{};
        Hr(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock pixels");
        std::vector<DWORD> result(size * size);
        for (UINT y = 0; y < size; ++y)
            std::memcpy(result.data() + y * size,
                        static_cast<const char*>(lock.pBits) + y * lock.Pitch, size * 4);
        Hr(readback->UnlockRect(), "unlock pixels");
        return result;
    }
};
struct OriginalModel {
    OriginalPhysicsModule& module;
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    PhysicsScene scene{};
    OriginalModel(OriginalPhysicsModule& binary, Fixture& port, const std::filesystem::path& path,
                  int threads = 4)
        : module(binary) {
        module.RenderWorkers(threads);
        renderer->device = port.device;
#if defined(_M_X64)
        const auto locale = reinterpret_cast<_locale_t (*)(int, const char*)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_create_locale"));
        for (auto& value : renderer->localeTable)
            value = locale(LC_ALL, "Japanese_Japan.932");
        Check(
            module.Function<bool (*)(PhysicsScene*, D3DRenderer*)>(0x27B0)(&scene, renderer.get()),
            "original scene");
        module.Function<void (*)(mdl::ModelRecord*)>(0xA9610)(model.get());
        std::array<wchar_t, 32768> paths{};
        using Load = bool (*)(mdl::ModelRecord*, HWND, const wchar_t*, D3DRenderer*, unsigned char,
                              unsigned char, PhysicsScene*, void*);
        const auto cwd = std::filesystem::current_path();
        const bool loaded = module.Function<Load>(0xB2670)(
            model.get(), port.window, path.c_str(), renderer.get(), 0, 1, &scene, paths.data());
#else
        const auto locale = module.Function<_locale_t (*)(int, const char*)>(0x10602F);
        for (auto& value : renderer->localeTable)
            value = locale(LC_ALL, "JPN");
        using Scene = bool(__thiscall*)(PhysicsScene*, D3DRenderer*);
        Check(module.Function<Scene>(0x32B0)(&scene, renderer.get()), "original x86 scene");
        module.Function<void(__thiscall*)(mdl::ModelRecord*)>(0xA8DC0)(model.get());
        std::array<wchar_t, 32768> paths{};
        using Load = bool(__thiscall*)(mdl::ModelRecord*, HWND, const wchar_t*, D3DRenderer*, int,
                                       int, int, PhysicsScene*, void*);
        const auto cwd = std::filesystem::current_path();
        const bool loaded = module.Function<Load>(0xBF3E0)(
            model.get(), port.window, path.c_str(), renderer.get(), 1, 0, 1, &scene, paths.data());
#endif
        std::filesystem::current_path(cwd);
        Check(loaded, "original model load");
    }
    ~OriginalModel() {
#if defined(_M_X64)
        module.Function<void (*)(mdl::ModelRecord*)>(0xA8D50)(model.get());
        module.Function<void (*)(PhysicsScene*)>(0x2580)(&scene);
        const auto freeLocale = reinterpret_cast<void (*)(_locale_t)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_free_locale"));
        for (auto value : renderer->localeTable)
            if (value)
                freeLocale(value);
#else
        module.Function<void(__thiscall*)(mdl::ModelRecord*)>(0x8F830)(model.get());
        module.Function<void(__thiscall*)(PhysicsScene*)>(0x30F0)(&scene);
        // The original's static CRT owns these locales; its private heap is
        // destroyed after every original object and CRT callback is released.
#endif
    }
    void Pose() {
#if defined(_M_X64)
        module.Function<void (*)(mdl::ModelRecord*, std::uint32_t, int)>(0xCBD90)(model.get(), 0,
                                                                                  0);
        module.Function<void (*)(mdl::ModelRecord*)>(0xBD780)(model.get());
        mdl::ModelRecord* models[kModelSlotCount]{};
        models[0] = model.get();
        using PoseCall = void (*)(mdl::ModelRecord*, int, mdl::ModelRecord**, int);
        constexpr auto poseRva = 0xB9F70;
#else
        module.Function<void(__thiscall*)(mdl::ModelRecord*, std::uint32_t, int)>(0xB4260)(
            model.get(), 0, 0);
        module.Function<void(__thiscall*)(mdl::ModelRecord*)>(0x970B0)(model.get());
        mdl::ModelRecord* models[kModelSlotCount]{};
        models[0] = model.get();
        using PoseCall = void(__thiscall*)(mdl::ModelRecord*, int, mdl::ModelRecord**, int);
        constexpr auto poseRva = 0xA9220;
#endif
        module.Function<PoseCall>(poseRva)(model.get(), 0, models, 0);
        module.Function<PoseCall>(poseRva)(model.get(), 1, models, 0);
    }
    void Skin(MMDApp& app, const D3DXMATRIX& world) {
#if defined(_M_X64)
        // IDA RVA C13C0 and caller 2A1D0: WORLD, camera XYZ, FOV, shadow byte.
        using SkinCall =
            void (*)(mdl::ModelRecord*, const float*, const float*, float, unsigned char);
        module.Function<SkinCall>(0xC13C0)(model.get(), &world._11, app.CameraPosition(),
                                           app.CameraFov(), app.state.selfShadowEnabled);
#else
        // IDA caller 46DBE9: renderer, WORLD by value, camera pointer, FOV,
        // shadow byte. The callee returns with RET 50h (80 stack bytes).
        using SkinCall = void(__thiscall*)(mdl::ModelRecord*, D3DRenderer*, D3DMATRIX, const float*,
                                           float, unsigned char);
        module.Function<SkinCall>(0xB0C50)(model.get(), renderer.get(), world, app.CameraPosition(),
                                           app.CameraFov(), app.state.selfShadowEnabled);
#endif
    }
    void Materials(MMDApp& app) {
        std::array<IDirect3DTexture9*, 11> toons{};
        for (int i = 0; i < 11; ++i)
            toons[i] = app.ToonTexture(i);
#if defined(_M_X64)
        using Draw =
            void (*)(mdl::ModelRecord*, D3DRenderer*, const D3DVECTOR*, IDirect3DTexture9**);
        module.Function<Draw>(0xB6D70)(model.get(), renderer.get(), &app.SceneLight().Direction,
                                       toons.data());
#else
        using Draw =
            void(__thiscall*)(mdl::ModelRecord*, D3DRenderer*, D3DVECTOR, IDirect3DTexture9**);
        module.Function<Draw>(0x912F0)(model.get(), renderer.get(), app.SceneLight().Direction,
                                       toons.data());
#endif
    }
    void Edges(bool projected) {
#if defined(_M_X64)
        module.Function<void (*)(mdl::ModelRecord*, D3DRenderer*, unsigned char)>(0xB82A0)(
            model.get(), renderer.get(), projected ? 1 : 0);
#else
        module.Function<void(__thiscall*)(mdl::ModelRecord*, D3DRenderer*, unsigned char)>(0x92640)(
            model.get(), renderer.get(), projected ? 1 : 0);
#endif
    }
};
std::uint64_t Digest(const std::vector<DWORD>& pixels) {
    std::uint64_t value = 14695981039346656037ull;
    for (DWORD pixel : pixels)
        for (int shift = 0; shift < 32; shift += 8) {
            value ^= (pixel >> shift) & 255;
            value *= 1099511628211ull;
        }
    return value;
}
struct Difference {
    unsigned pixels = 0, maximum = 0;
    Difference(const std::vector<DWORD>& a, const std::vector<DWORD>& b) {
        Check(a.size() == b.size(), "pixel count");
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i])
                ++pixels;
            for (int shift = 0; shift < 32; shift += 8)
                maximum =
                    std::max(maximum, static_cast<unsigned>(std::abs(int((a[i] >> shift) & 255) -
                                                                     int((b[i] >> shift) & 255))));
        }
    }
};
std::vector<unsigned char> Buffer(IDirect3DVertexBuffer9* buffer) {
    D3DVERTEXBUFFER_DESC desc{};
    Hr(buffer->GetDesc(&desc), "vertex description");
    void* bits = nullptr;
    Hr(buffer->Lock(0, 0, &bits, D3DLOCK_READONLY), "vertex read");
    const auto* bytes = static_cast<const unsigned char*>(bits);
    std::vector<unsigned char> result(bytes, bytes + desc.Size);
    Hr(buffer->Unlock(), "vertex unlock");
    return result;
}

void SaveRenderBytes(const std::filesystem::path& directory, const std::string& name,
                     const void* data, std::size_t size) {
    if (directory.empty())
        return;
    std::filesystem::create_directories(directory);
    std::ofstream file(directory / name, std::ios::binary);
    file.write(static_cast<const char*>(data), size);
    Check(file.good(), "write render numeric artifact");
}
std::array<std::vector<float>, 3> RenderModelNumbers(const mdl::ModelRecord& model) {
    // Serialize semantic fields only: pointers and uninitialized padding do
    // not belong in a numerical comparison, including between architectures.
    std::array<std::vector<float>, 3> result;
    auto& bones = result[0];
    auto& vertices = result[1];
    auto& materials = result[2];
    auto append = [](std::vector<float>& out, const float* data, std::size_t count) {
        out.insert(out.end(), data, data + count);
    };
    for (unsigned i = 0; i < model.boneCount; ++i) {
        const auto& b = model.boneTable[i];
        append(bones, b.matInit, 16);
        append(bones, b.matLocal, 16);
        append(bones, b.matWorld, 16);
        append(bones, b.matExtra, 16);
        append(bones, b.position, 3);
        append(bones, b.trans, 3);
        append(bones, b.rotQuat, 4);
    }
    if (model.physicsMode == 2)
        for (unsigned i = 0; i < model.vertexCount; ++i) {
            const auto& v = model.pmxVertices[i];
            append(vertices, v.position, 3);
            append(vertices, v.normal, 3);
            append(vertices, v.uv, 2);
            append(vertices, v.weight, 4);
        }
    for (unsigned i = 0; i < model.materialCount; ++i) {
        const auto& m = model.materials[i];
        append(materials, m.diffuse, 4);
        append(materials, m.specular, 3);
        append(materials, m.ambient, 3);
        append(materials, &m.specularPower, 1);
        append(materials, m.edgeColor, 4);
        append(materials, &m.edgeSize, 1);
    }
    return result;
}
void SameRenderModel(const mdl::ModelRecord& a, const mdl::ModelRecord& b) {
    const auto port = RenderModelNumbers(a), original = RenderModelNumbers(b);
    for (unsigned i = 0; i < port.size(); ++i)
        Check(port[i].size() == original[i].size() &&
                  (port[i].empty() || std::memcmp(port[i].data(), original[i].data(),
                                                  port[i].size() * sizeof(float)) == 0),
              "model numerical fields differ from original");
}
void SaveRenderModel(const std::filesystem::path& directory, const std::string& name,
                     const mdl::ModelRecord& model) {
    const auto data = RenderModelNumbers(model);
    const char* kinds[] = {"bones", "vertices", "materials"};
    for (unsigned i = 0; i < data.size(); ++i)
        SaveRenderBytes(directory, name + "." + kinds[i] + ".f32", data[i].data(),
                        data[i].size() * sizeof(float));
}

} // namespace
