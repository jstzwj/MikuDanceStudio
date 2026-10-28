#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "../src/render/model_materials.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
int ExpGetCurrentMaterial();
int ExpGetCurrentObject();
}
using namespace mikudancestudio;
namespace {
int failures;
template <std::size_t Slot> void UnexpectedCall() {
    std::fprintf(stderr, "Unexpected COM slot: %zu\n", Slot);
    std::abort();
}
template <std::size_t... Slot> auto TrapTable(std::index_sequence<Slot...>) {
    return std::array<void*, sizeof...(Slot)>{reinterpret_cast<void*>(&UnexpectedCall<Slot>)...};
}
void Check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
struct Draw {
    UINT start, count, stride;
    DWORD fvf, cull, dest;
    int material, object;
    float alpha;
};
// Only the external COM boundary is replaced. All material walks, texture
// selection, D3DX transforms and Exp* observations use the production code.
struct Device {
    void** vtable;
    std::array<DWORD, 256> states{};
    DWORD stages[3][40]{};
    DWORD samplers[3][16]{};
    std::map<unsigned, D3DMATRIX> transforms;
    std::array<IDirect3DBaseTexture9*, 3> textures{};
    D3DMATERIAL9 material{};
    DWORD fvf = 0;
    UINT stride = 0;
    std::vector<Draw> draws;
};
Device& Dev(void* p) {
    return *static_cast<Device*>(p);
}
HRESULT WINAPI SetState(IDirect3DDevice9* p, D3DRENDERSTATETYPE t, DWORD v) {
    Dev(p).states[t] = v;
    return S_OK;
}
HRESULT WINAPI SetStage(IDirect3DDevice9* p, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) {
    Dev(p).stages[s][t] = v;
    return S_OK;
}
HRESULT WINAPI SetSampler(IDirect3DDevice9* p, DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) {
    Dev(p).samplers[s][t] = v;
    return S_OK;
}
HRESULT WINAPI SetTransform(IDirect3DDevice9* p, D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) {
    Dev(p).transforms[t] = *m;
    return S_OK;
}
HRESULT WINAPI GetTransform(IDirect3DDevice9* p, D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) {
    *m = Dev(p).transforms[t];
    return S_OK;
}
HRESULT WINAPI SetTexture(IDirect3DDevice9* p, DWORD s, IDirect3DBaseTexture9* t) {
    Dev(p).textures[s] = t;
    return S_OK;
}
HRESULT WINAPI SetMaterial(IDirect3DDevice9* p, const D3DMATERIAL9* m) {
    Dev(p).material = *m;
    return S_OK;
}
HRESULT WINAPI SetLight(IDirect3DDevice9*, DWORD, const D3DLIGHT9*) {
    return S_OK;
}
HRESULT WINAPI SetFVF(IDirect3DDevice9* p, DWORD fvf) {
    Dev(p).fvf = fvf;
    return S_OK;
}
HRESULT WINAPI SetStream(IDirect3DDevice9* p, UINT stream, IDirect3DVertexBuffer9*, UINT offset,
                         UINT stride) {
    Check(stream == 0 && offset == 0, "model stream uses stream zero without an offset");
    Dev(p).stride = stride;
    return S_OK;
}
HRESULT WINAPI SetIndices(IDirect3DDevice9*, IDirect3DIndexBuffer9*) {
    return S_OK;
}
HRESULT WINAPI DrawIndexed(IDirect3DDevice9* p, D3DPRIMITIVETYPE type, INT base, UINT minimum,
                           UINT vertices, UINT start, UINT count) {
    auto& d = Dev(p);
    Check(type == D3DPT_TRIANGLELIST && base == 0 && minimum == 0 && vertices == 6,
          "material draw retains triangle topology and the complete model vertex range");
    d.draws.push_back({start, count, d.stride, d.fvf, d.states[D3DRS_CULLMODE],
                       d.states[D3DRS_DESTBLEND], ExpGetCurrentMaterial(), ExpGetCurrentObject(),
                       d.material.Diffuse.a});
    return S_OK;
}
struct Effect {
    void** vtable;
    std::string technique;
    std::map<std::string, std::array<float, 4>> colors;
    std::map<std::string, D3DMATRIX> matrices;
    bool additive = false;
    int begins = 0, passes = 0, ends = 0, passEnds = 0;
};
Effect& Fx(void* p);
HRESULT WINAPI Matrix(void* p, const char* name, const D3DMATRIX* matrix) {
    Fx(p).matrices[name] = *matrix;
    return S_OK;
}
HRESULT WINAPI ReadMatrix(void* p, const char* name, D3DMATRIX* matrix) {
    *matrix = Fx(p).matrices[name];
    return S_OK;
}
Effect& Fx(void* p) {
    return *static_cast<Effect*>(p);
}
HRESULT WINAPI Technique(void* p, const char* name) {
    Fx(p).technique = name;
    return S_OK;
}
HRESULT WINAPI Bool(void* p, const char* name, BOOL value) {
    if (std::strcmp(name, "spadd") == 0)
        Fx(p).additive = value != 0;
    return S_OK;
}
HRESULT WINAPI Floats(void* p, const char* name, const float* v, UINT count) {
    Check(count == 4, "effect material parameters keep float4 lengths");
    std::memcpy(Fx(p).colors[name].data(), v, 4 * sizeof(float));
    return S_OK;
}
HRESULT WINAPI Begin(void* p, UINT* count, DWORD flags) {
    Check(flags == 0, "effect Begin flags match original");
    *count = 1;
    ++Fx(p).begins;
    return S_OK;
}
HRESULT WINAPI Pass(void* p, UINT pass) {
    Check(pass == 0, "host material uses effect pass zero");
    ++Fx(p).passes;
    return S_OK;
}
HRESULT WINAPI EndPass(void* p) {
    ++Fx(p).passEnds;
    return S_OK;
}
HRESULT WINAPI End(void* p) {
    ++Fx(p).ends;
    return S_OK;
}
HRESULT WINAPI Shader(void*, void*) {
    return S_OK;
}
struct AccessoryMesh {
    void** vtable;
    unsigned draws = 0;
};
HRESULT WINAPI Subset(void* p, DWORD index) {
    Check(index == 0, "accessory material subset");
    ++static_cast<AccessoryMesh*>(p)->draws;
    return S_OK;
}
struct Buffer {
    void** vtable;
    std::array<mdl::EdgeVertex, 6> vertices{};
    unsigned locks = 0, unlocks = 0;
    UINT bytes = 0;
};
HRESULT WINAPI Lock(void* p, UINT offset, UINT bytes, void** out, DWORD flags) {
    auto& b = *static_cast<Buffer*>(p);
    Check(offset == 0 && flags == 0, "model color locks the whole auxiliary range");
    b.bytes = bytes;
    ++b.locks;
    *out = b.vertices.data();
    return S_OK;
}
HRESULT WINAPI Unlock(void* p) {
    ++static_cast<Buffer*>(p)->unlocks;
    return S_OK;
}
struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::ModelMaterialRecord, 4> materials{};
    std::array<mdl::MaterialMorphPool, 4> add{}, mul{};
    std::array<void*, 119> deviceTable = TrapTable(std::make_index_sequence<119>{});
    std::array<void*, 80> effectTable = TrapTable(std::make_index_sequence<80>{});
    std::array<void*, 14> bufferTable{};
    Device device{deviceTable.data()};
    Effect effect{effectTable.data()};
    Buffer buffer{bufferTable.data()};
    wchar_t mainPath[16] = L"main.png", spherePath[16] = L"sphere.sph";
    Fixture() {
        std::memset(&app->state, 0, sizeof app->state);
        app->Renderer() = renderer.get();
        renderer->device = reinterpret_cast<IDirect3DDevice9*>(&device);
        renderer->effect = reinterpret_cast<ID3DXEffect*>(&effect);
        app->ModelSlot(kModelSlotCount - 1) = model.get();
        app->ActiveRenderObject() = model.get();
        g_Block = app.get();
        model->loadComplete = 1;
        model->vertexCount = 6;
        model->materialCount = 1;
        model->materials = materials.data();
        model->vertexBuffer = &buffer;
        model->vertexBuffer2 = &buffer;
        model->indexBuffer = &buffer;
        model->materialMorphPools.additive = add.data();
        model->materialMorphPools.multiplicative = mul.data();
        model->comboSelIndex = 1;
        for (unsigned i = 0; i < materials.size(); ++i) {
            materials[i].faceVertexCount = 3;
            materials[i].diffuse[3] = 1;
            materials[i].toonReference = 255;
            materials[i].edgeColor[3] = 1;
            materials[i].flags = 0xE;
        }
        app->SceneLight().Direction = {0, -1, 0};
        device.states[D3DRS_CULLMODE] = D3DCULL_CCW;
        deviceTable[44] = reinterpret_cast<void*>(&SetTransform);
        deviceTable[45] = reinterpret_cast<void*>(&GetTransform);
        deviceTable[49] = reinterpret_cast<void*>(&SetMaterial);
        deviceTable[51] = reinterpret_cast<void*>(&SetLight);
        deviceTable[57] = reinterpret_cast<void*>(&SetState);
        deviceTable[65] = reinterpret_cast<void*>(&SetTexture);
        deviceTable[67] = reinterpret_cast<void*>(&SetStage);
        deviceTable[69] = reinterpret_cast<void*>(&SetSampler);
        deviceTable[82] = reinterpret_cast<void*>(&DrawIndexed);
        deviceTable[89] = reinterpret_cast<void*>(&SetFVF);
        deviceTable[100] = reinterpret_cast<void*>(&SetStream);
        deviceTable[104] = reinterpret_cast<void*>(&SetIndices);
        effectTable[22] = reinterpret_cast<void*>(&Bool);
        effectTable[32] = reinterpret_cast<void*>(&Floats);
        effectTable[58] = reinterpret_cast<void*>(&Technique);
        effectTable[63] = reinterpret_cast<void*>(&Begin);
        effectTable[64] = reinterpret_cast<void*>(&Pass);
        effectTable[66] = reinterpret_cast<void*>(&EndPass);
        effectTable[67] = reinterpret_cast<void*>(&End);
        bufferTable[11] = reinterpret_cast<void*>(&Lock);
        bufferTable[12] = reinterpret_cast<void*>(&Unlock);
        renderer->resourcePool[0].heapBuffer = mainPath;
        renderer->resourcePool[0].comObject = reinterpret_cast<IUnknown*>(&buffer);
        renderer->resourcePool[1].heapBuffer = spherePath;
        renderer->resourcePool[1].comObject = reinterpret_cast<IUnknown*>(&effect);
    }
    ~Fixture() { g_Block = nullptr; }
    void Body(bool effectPass = false, bool shadow = false) {
        device.draws.clear();
        DrawModelMaterials(app.get(), model.get(), effectPass, shadow);
    }
};
void TestDraws() {
    Fixture f;
    f.model->materialCount = 4;
    f.materials[0].faceVertexCount = 0;
    f.materials[1].faceVertexCount = 6;
    f.materials[2].faceVertexCount = 3;
    f.materials[3].faceVertexCount = 9;
    f.materials[2].diffuse[3] = 0.5f;
    f.Body();
    Check(f.device.draws.size() == 3 && f.device.draws[0].start == 0 &&
              f.device.draws[1].start == 6 && f.device.draws[2].start == 9 &&
              f.device.draws[2].count == 3 && f.device.draws[0].material == 1 &&
              f.device.draws[2].material == 3 && f.model->currentMaterial == UINT32_MAX,
          "zero-length materials still advance cursor; index ranges accumulate across draws");
    Check(f.device.draws[1].cull == D3DCULL_NONE &&
              f.device.states[D3DRS_CULLMODE] == D3DCULL_CCW &&
              f.device.stages[1][D3DTSS_COLOROP] == D3DTOP_DISABLE,
          "transparent fixed material restores culling and the loop closes stage one");
    f.model->materialCount = 1;
    f.materials[0].faceVertexCount = 3;
    f.materials[0].diffuse[3] = 0.5f;
    f.model->postLoadFlag2 = 1;
    f.Body();
    Check(f.device.draws.size() == 2 && f.device.draws[0].cull == D3DCULL_CW &&
              f.device.draws[1].cull == D3DCULL_CCW && f.device.draws[0].dest == D3DBLEND_ONE &&
              f.device.states[D3DRS_DESTBLEND] == D3DBLEND_INVSRCALPHA,
          "additive transparent material draws back then front faces and restores blending");
    const DWORD fvf[] = {274, 524818, 2622226, 11011090, 44565778};
    for (unsigned i = 0; i < 5; ++i) {
        f.model->pmxAdditionalUvCount = static_cast<unsigned char>(i);
        f.Body();
        Check(f.device.draws[0].fvf == fvf[i] && f.device.draws[0].stride == 32 + 16 * i,
              "zero through four additional float4 UVs retain original FVF and stride");
    }
}
void TestTextureStages() {
    Fixture f;
    auto& m = f.materials[0];
    f.model->physicsMode = 2;
    wcscpy_s(m.texturePath, f.mainPath);
    wcscpy_s(m.spherePath, f.spherePath);
    for (unsigned mode = 0; mode < 4; ++mode) {
        m.sphereMode = static_cast<unsigned char>(mode);
        f.Body();
        Check(f.device.textures[1] == reinterpret_cast<IDirect3DBaseTexture9*>(&f.buffer) &&
                  f.device.stages[2][D3DTSS_COLOROP] == (mode == 0   ? D3DTOP_DISABLE
                                                         : mode == 2 ? D3DTOP_ADD
                                                                     : D3DTOP_MODULATE),
              "PMX sphere modes preserve disabled, multiply, add and additional-UV stages");
        if (mode)
            Check(f.device.stages[2][D3DTSS_TEXCOORDINDEX] == (mode == 3 ? 1u : 0x10000u),
                  "PMX sphere mode three uses additional UV; others use camera normals");
    }
    m.texturePath[0] = 0;
    m.sphereMode = 3;
    f.Body();
    Check(f.device.transforms[D3DTS_TEXTURE1]._11 == 1 &&
              f.device.transforms[D3DTS_TEXTURE1]._22 == 1,
          "sphere-only additional UV uses identity on stage one");
    f.model->physicsMode = 0;
    wcscpy_s(m.texturePath, L"main.sph");
    f.Body();
    Check(f.device.transforms[D3DTS_TEXTURE1]._11 == 0.5f &&
              f.device.stages[1][D3DTSS_TEXTURETRANSFORMFLAGS] == 0 &&
              f.device.stages[1][D3DTSS_TEXCOORDINDEX] == 0,
          "next PMD restores half-texel transform and closes the sphere-main stage");
    Check(f.device.stages[2][D3DTSS_COLOROP] == D3DTOP_MODULATE,
          "PMD sph secondary texture multiplies");
    wcscpy_s(m.spherePath, L"sphere.spa");
    f.Body();
    Check(f.device.stages[2][D3DTSS_COLOROP] == D3DTOP_ADD, "PMD spa secondary texture adds");
    strcpy_s(f.model->pmdToonFileNames[2], "toon03.bmp");
    m.toonReference = 2;
    f.app->ToonTexture(3) = reinterpret_cast<IDirect3DTexture9*>(&f.effect);
    f.Body();
    Check(f.device.textures[0] == reinterpret_cast<IDirect3DBaseTexture9*>(&f.effect) &&
              f.device.samplers[0][D3DSAMP_ADDRESSU] == D3DTADDRESS_CLAMP,
          "standard toon names use the shared table's one-based texture slot");
}
void TestEffectAndShadows() {
    Fixture f;
    auto& m = f.materials[0];
    f.model->physicsMode = 2;
    wcscpy_s(m.texturePath, f.mainPath);
    wcscpy_s(m.spherePath, f.spherePath);
    m.sphereMode = 2;
    f.Body(true);
    Check(f.effect.technique == "BShadowSphiaTextureTec" && f.effect.additive &&
              f.effect.begins == 1 && f.effect.ends == 1 && f.effect.passes == 1 &&
              f.effect.passEnds == 1 && f.device.draws.size() == 1,
          "effect material selects combined sphere technique with paired pass boundaries");
    m.sphereMode = 3;
    f.Body(true);
    Check(f.effect.technique == "BShadowTextureTexCd2Tec",
          "PMX additional-UV sphere uses its distinct effect technique");
    f.model->materialCount = 4;
    f.materials[0].flags = 0;
    f.materials[1].diffuse[3] = 0.9800000190734863f;
    f.materials[2].edgeColor[3] = 0;
    f.materials[3].doubleSided = 1;
    f.Body(true, true);
    Check(f.device.draws.size() == 1 && f.device.draws[0].start == 9 &&
              f.device.draws[0].material == 3 && f.device.draws[0].cull == D3DCULL_NONE,
          "shadow map gates flags, special alpha and edge alpha without losing index ranges");
    f.renderer->postProcessEnabled = 0;
    f.device.draws.clear();
    DrawModelEdgeGeometry(f.app.get(), f.model.get(), false);
    Check(f.device.draws.size() == 1 && f.device.draws[0].start == 9 &&
              f.device.draws[0].stride == 16 && f.device.draws[0].fvf == 66,
          "outline uses the auxiliary vertex layout and advances over skipped materials");
    f.device.draws.clear();
    DrawModelEdgeGeometry(f.app.get(), f.model.get(), true);
    Check(f.device.draws.size() == 3 && f.device.draws[0].start == 3,
          "projected shadow uses its separate PMX flag and includes non-outline materials");
}
void TestColorAndOrder() {
    Fixture f;
    f.buffer.vertices[1].position[0] = 7;
    SetModelColor(f.model.get(), 0x12, 0x134, 0x156);
    Check(f.buffer.locks == 1 && f.buffer.unlocks == 1 && f.buffer.bytes == 96 &&
              f.buffer.vertices[0].diffuse == 0xFF123456 &&
              f.buffer.vertices[5].diffuse == 0xFF123456 && f.buffer.vertices[1].position[0] == 7,
          "typed model color updates every auxiliary color and preserves positions");
    f.model->physicsMode = 2;
    SetModelColor(f.model.get(), 1, 2, 3);
    Check(f.buffer.locks == 1, "PMX color sweep leaves its per-material edge colors intact");
    f.model->physicsMode = 0;
    auto other = std::make_unique<mdl::ModelRecord>(*f.model);
    other->comboSelIndex = 0;
    f.app->ModelSlot(1) = other.get();
    f.app->WireframeRenderingEnabled() = 1;
    f.device.draws.clear();
    RenderModelsFixed(f.app.get());
    Check(f.device.draws.size() == 2 && f.device.draws[0].object == 0 &&
              f.device.draws[1].object == 1 && f.app->ActiveRenderObject() == nullptr &&
              f.device.states[D3DRS_FILLMODE] == D3DFILL_SOLID &&
              f.device.states[D3DRS_ZFUNC] == D3DCMP_LESSEQUAL,
          "frame sorts sparse slots by draw order and publishes/restores object and states");
}
void TestAccessoryEffectInputs() {
    Fixture f;
    std::array<void*, 4> meshTable{};
    meshTable[3] = reinterpret_cast<void*>(&Subset);
    AccessoryMesh mesh{meshTable.data()};
    mdl::AccessoryRecord accessory{};
    InitAccessoryRecord(&accessory);
    accessory.mesh = &mesh;
    accessory.scale = .5f;
    accessory.position[0] = 3;
    accessory.position[1] = 4;
    accessory.position[2] = 5;
    accessory.opacity = .25f;
    accessory.shadowEnabled = 1;
    D3DMATERIAL9 material{};
    material.Diffuse = {.5f, .25f, .125f, .8f};
    material.Ambient = {.2f, .4f, .6f, 1};
    material.Emissive = {.1f, .2f, .3f, 0};
    accessory.materials = &material;
    accessory.materialCount = 1;
    f.app->AccessorySlot(0) = &accessory;
    f.app->SceneLight().Ambient = {.7f, .6f, .5f, 1};
    f.app->SceneLight().Specular = {.2f, .3f, .4f, 1};
    f.app->ProjectedShadowAmbientIntensity() = .5f;
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    f.app->LightViewProjection() = f.app->WorldViewProjection() = identity;
    f.effect.matrices["matRotate"] = identity;
    f.effectTable[38] = reinterpret_cast<void*>(&Matrix);
    f.effectTable[39] = reinterpret_cast<void*>(&ReadMatrix);
    f.deviceTable[92] = f.deviceTable[107] = reinterpret_cast<void*>(&Shader);
    RenderAccessoriesEffectRange(f.app.get(), 0, 1);
    const auto& color = f.effect.colors.at("DifColor");
    Check(color == std::array<float, 4>{5, 2.5f, 1.25f, 1},
          "original accessory DifColor uses the directional light's diffuse RGB");
    Check(std::abs(f.effect.colors.at("EgColor")[0] - .09f) < .000001f &&
              f.effect.colors.at("EgColor")[3] == .2f,
          "accessory ambient/emissive color and opacity follow separate shader inputs");
    Check(f.device.material.Diffuse.a == .8f && material.Diffuse.a == .8f,
          "MME observes the original material alpha without a second opacity factor");
    const auto& world = f.effect.matrices.at("matWorld");
    const auto& projection = f.effect.matrices.at("matWorldViewProj");
    const auto& rotation = f.effect.matrices.at("matWRotate");
    Check(world._41 == 0 && world._42 == 0 && world._43 == 0 && world._11 == 5 &&
              projection._41 == 3 && projection._42 == 4 && projection._43 == 5 &&
              rotation._41 == 0 && rotation._42 == 0 && rotation._43 == 0,
          "lighting drops translation after preserving it in both projection matrices");
    Check(mesh.draws == 1 && accessory.currentMaterial == -1 &&
              f.app->ActiveRenderObject() == nullptr,
          "accessory submission restores its cursor and active identity");
}
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestDraws();
    TestTextureStages();
    TestEffectAndShadows();
    TestColorAndOrder();
    TestAccessoryEffectInputs();
    return failures ? 1 : 0;
}
