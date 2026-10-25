#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "../src/render/model_materials.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace mikudancestudio;
namespace {
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
    T* operator->() const { return p; }
};
struct Window {
    HWND hwnd = CreateWindowW(L"STATIC", L"Model renderer regression", WS_OVERLAPPED, 0, 0, 64, 64,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (hwnd)
            DestroyWindow(hwnd);
    }
};
bool Good(HRESULT hr, const char* call) {
    if (FAILED(hr))
        std::fprintf(stderr, "%s: 0x%08lX\n", call, static_cast<unsigned long>(hr));
    return SUCCEEDED(hr);
}
#define REQUIRE(call)                                                                              \
    if (!Good((call), #call))                                                                      \
    return 1
}
int main() {
    Window window;
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window.hwnd || !d3d.p)
        return 77;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window.hwnd;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    Com<IDirect3DDevice9> device;
    const HRESULT created = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.hwnd,
                                              D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p);
    if (FAILED(created)) {
        std::printf("SKIP: HAL device unavailable: 0x%08lX\n", static_cast<unsigned long>(created));
        return 77;
    }
    auto app = std::make_unique<MMDApp>();
    auto renderer = std::make_unique<D3DRenderer>();
    auto model = std::make_unique<mdl::ModelRecord>();
    std::memset(&app->state, 0, sizeof app->state);
    app->Renderer() = renderer.get();
    renderer->device = device.p;
    app->ModelSlot(kModelSlotCount - 1) = model.get();
    app->ActiveRenderObject() = model.get();
    g_Block = app.get();
    Com<IDirect3DVertexBuffer9> vertices, edges;
    Com<IDirect3DIndexBuffer9> indices;
    Com<IDirect3DTexture9> toon;
    REQUIRE(device->CreateVertexBuffer(3 * sizeof(mdl::SkinnedVertex<4>), 0, 0, D3DPOOL_MANAGED,
                                       &vertices.p, nullptr));
    REQUIRE(device->CreateVertexBuffer(3 * sizeof(mdl::EdgeVertex), 0, 0, D3DPOOL_MANAGED, &edges.p,
                                       nullptr));
    REQUIRE(device->CreateIndexBuffer(6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices.p, nullptr));
    REQUIRE(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &toon.p, nullptr));
    D3DLOCKED_RECT locked{};
    REQUIRE(toon->LockRect(0, &locked, nullptr, 0));
    *static_cast<DWORD*>(locked.pBits) = 0xFFFFFFFF;
    REQUIRE(toon->UnlockRect(0));
    app->ToonTexture(0) = toon.p;
    void* data = nullptr;
    REQUIRE(indices->Lock(0, 0, &data, 0));
    const std::uint16_t triangle[] = {0, 1, 2};
    std::memcpy(data, triangle, sizeof triangle);
    REQUIRE(indices->Unlock());
    const float positions[3][3] = {{-0.9f, -0.9f, 0.2f}, {0, 0.9f, 0.2f}, {0.9f, -0.9f, 0.2f}};
    REQUIRE(edges->Lock(0, 0, &data, 0));
    for (int i = 0; i < 3; ++i)
        std::memcpy(static_cast<mdl::EdgeVertex*>(data)[i].position, positions[i],
                    sizeof positions[i]);
    REQUIRE(edges->Unlock());
    model->vertexBuffer = vertices.p;
    model->vertexBuffer2 = edges.p;
    model->indexBuffer = indices.p;
    model->vertexCount = 3;
    model->loadComplete = 1;
    model->materialCount = 1;
    mdl::ModelMaterialRecord material{};
    material.faceVertexCount = 3;
    material.diffuse[3] = 1;
    material.ambient[0] = 1; // D3DMATERIAL9::Emissive red, independent of lights.
    material.toonReference = 255;
    material.doubleSided = 1;
    model->materials = &material;
    app->SceneLight().Direction = {0, -1, 0};
    InitRenderStates(app.get());
    REQUIRE(device->LightEnable(0, FALSE));
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    for (auto transform : {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION})
        REQUIRE(device->SetTransform(transform, &identity));
    Com<IDirect3DSurface9> target, readback;
    REQUIRE(device->GetRenderTarget(0, &target.p));
    REQUIRE(device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                &readback.p, nullptr));
    int failures = 0;
    const auto pixel = [&](DWORD expected) {
        if (!Good(device->GetRenderTargetData(target.p, readback.p), "readback") ||
            !Good(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "readback lock")) {
            ++failures;
            return;
        }
        const auto actual = *reinterpret_cast<const DWORD*>(static_cast<const char*>(locked.pBits) +
                                                            32 * locked.Pitch + 32 * sizeof(DWORD));
        if ((actual & 0xFFFFFF) != expected) {
            std::fprintf(stderr, "center pixel: %08lX, expected RGB %06lX\n", actual, expected);
            ++failures;
        }
        readback->UnlockRect();
    };
    // Drive the production material walk with all valid runtime GPU layouts,
    // and verify pixels, not merely successful state setters/draw calls.
    for (unsigned uv = 0; uv < 5; ++uv) {
        const unsigned stride = 32 + 16 * uv;
        model->physicsMode = uv ? 2 : 0;
        model->pmxAdditionalUvCount = static_cast<unsigned char>(uv);
        REQUIRE(vertices->Lock(0, 0, &data, 0));
        std::memset(data, 0, 3 * sizeof(mdl::SkinnedVertex<4>));
        for (unsigned i = 0; i < 3; ++i) {
            auto* vertex =
                reinterpret_cast<mdl::SkinnedVertexBase*>(static_cast<char*>(data) + i * stride);
            std::memcpy(vertex->position, positions[i], sizeof positions[i]);
            vertex->normal[2] = -1;
        }
        REQUIRE(vertices->Unlock());
        REQUIRE(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        REQUIRE(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1, 0));
        REQUIRE(device->BeginScene());
        DrawModelMaterials(app.get(), model.get(), false, false);
        REQUIRE(device->EndScene());
        pixel(0xFF0000);
    }
    model->physicsMode = 0;
    SetModelColor(model.get(), 0, 255, 0);
    REQUIRE(device->SetRenderState(D3DRS_LIGHTING, FALSE));
    REQUIRE(device->SetTexture(0, nullptr));
    REQUIRE(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2));
    REQUIRE(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2));
    REQUIRE(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    REQUIRE(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1, 0));
    REQUIRE(device->BeginScene());
    DrawModelEdgeGeometry(app.get(), model.get(), false);
    REQUIRE(device->EndScene());
    pixel(0x00FF00);

    Com<IDirect3DVertexBuffer9> sprites;
    REQUIRE(device->CreateVertexBuffer(0x445C0, 0, 0, D3DPOOL_MANAGED, &sprites.p, nullptr));
    app->SpriteOverlayVertices() = sprites.p;
    app->SetSelectedModelSlot(kModelSlotCount - 1);
    app->ViewportRect() = {0, 0, 64, 64};
    app->EditMode() = ViewportEditMode::Rotate;
    renderer->viewScale = 1;
    mdl::BoneRecord bone{};
    unsigned char selected = 1, edited = 0;
    model->boneTable = &bone;
    model->boneSelection = &selected;
    model->bonePhysicsState = &edited;
    model->boneCount = 1;
    model->selectedBone = 0;
    model->physicsMode = 2;
    bone.type = mdl::BoneType::Move;
    bone.flags = mdl::kBoneFlagVisible;
    std::memcpy(bone.matInit, &identity, sizeof identity);
    bone.position[0] = 0.25f;
    bone.position[1] = 0.5f;
    bone.tailOffset[0] = 0.5f;
    bone.tailOffset[1] = 0.25f;
    for (int translated : {0, 1}) {
        bone.matInit[12] = bone.matInit[13] = translated * 0.25f;
        PrepareFrameSpriteOverlay(app.get());
        const int x = 40 + 8 * translated, y = 16 - 8 * translated;
        if (bone.screenX != x || bone.screenY != y || bone.tailScreenX != 48 + 8 * translated ||
            bone.tailScreenY != 24 - 8 * translated || app->ViewportToolCenterX() != x ||
            app->ViewportToolCenterY() != y) {
            std::fprintf(stderr, "bone/tail projection does not share the operation center\n");
            ++failures;
        }
        // The final six vertices are the rotation HUD quad, centered on the
        // same projected point as the selected marker, with +/-45 extents.
        const UINT end = app->SpriteOverlayPrimitiveCount() * 3 * 28;
        REQUIRE(sprites->Lock(end - 6 * 28, 6 * 28, &data, D3DLOCK_READONLY));
        const auto* quad = static_cast<const float*>(data);
        if (quad[0] != x + 45 || quad[1] != y - 45 || quad[14] != x - 45 || quad[15] != y - 45) {
            std::fprintf(stderr, "rotation HUD quad shifted from selected bone\n");
            ++failures;
        }
        REQUIRE(sprites->Unlock());
    }
    g_Block = nullptr;
    return failures ? 1 : 0;
}
