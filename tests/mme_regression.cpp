#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <vector>

#include "../third_party/mmeffect/src/mmeffect/model_name_registry.h"
#include "../third_party/mmeffect/src/mmeffect/object_plan_state.h"
#include "../src/render/mme/mesh_subset.hpp"
#include "../src/render/fx_slots.hpp"

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #expr); std::exit(1); } } while (0)

using mikudancestudio::mme::MeshSubsetPlan;
using mikudancestudio::mme::PlanMeshSubset;
using mikudancestudio::mme::DrawMeshSubsetPlan;

void TestControlObjectSelection() {
    struct Model { int identity; } first{1}, second{2}, collision{3}, other{4};
    mme::ModelNameRegistry<Model> names;
    // Loaded first/second, but reordered second/owner/first for rendering.
    names.Add("same.pmx", 30, &first);
    names.Add("same.pmx", 10, &second);
    names.Add("same.pmx", 10, &collision);
    names.Add("different.pmx", 15, &other);
    CHECK(names.Find("same.pmx", 20) == &second);
    CHECK(names.Find("same.pmx", 10) == &second); // first insertion wins
    CHECK(names.Find("same.pmx", 9) == &first);  // wrap, not not-found
    CHECK(names.Find("same.pmx", 40) == &first);
    CHECK(names.First("same.pmx") == &second);
    CHECK(names.Find("Same.pmx", 40) == nullptr); // case-sensitive registry
    CHECK(names.Find("missing.pmx", 40) == nullptr);
    names.Remove(&second);
    CHECK(names.First("same.pmx") == &first);
    // No stale registry entries after a new frame / order change.
    names.Clear();
    names.Add("same.pmx", 5, &first);
    names.Add("same.pmx", 25, &second);
    CHECK(names.Find("same.pmx", 20) == &first);
    CHECK(names.Find("different.pmx", 20) == nullptr);
    // Unsigned tree ordering but signed comparison, including abs(INT_MIN).
    names.Add("same.pmx", INT_MIN, &other);
    CHECK(names.Find("same.pmx", 40) == &other);
    CHECK(names.Find("same.pmx", 0) == &other);
}

void TestPlanMatrix() {
    mme::ObjectPlanState state;
    for (int r = 0; r != 4; ++r)
        for (int c = 0; c != 4; ++c)
            CHECK(state.world.m[r][c] == (r == c ? 1.0f : 0.0f));
    state.world.m[3][0] = 17.0f;
    state.renderOrder = 42;
    state.passKey = 3;
    state.flag = 1;
    state = mme::ObjectPlanState{};
    CHECK(state.renderOrder == 0 && state.passKey == -1 && state.flag == 0);
    // Identity must preserve a general homogeneous position, not collapse it
    // as the old four colorN[0] stores did.
    const float point[4] = {2, 3, 7, 1};
    for (int c = 0; c != 4; ++c) {
        float value = 0;
        for (int r = 0; r != 4; ++r) value += point[r] * state.world.m[r][c];
        CHECK(value == point[c]);
    }
}

// Deliberately uses only the mesh's public query surface; no real COM vtable
// is fabricated, patched, or intercepted by these failure-injection tests.
struct MeshQueries {
    std::vector<D3DXATTRIBUTERANGE> table;
    std::vector<DWORD> attributes;
    HRESULT countResult = S_OK, tableResult = S_OK, lockResult = S_OK;
    unsigned locks = 0, unlocks = 0;
    DWORD vertices = 600;
    HRESULT GetAttributeTable(D3DXATTRIBUTERANGE* out, DWORD* count) {
        if (out == nullptr) { *count = static_cast<DWORD>(table.size()); return countResult; }
        if (FAILED(tableResult)) return tableResult;
        std::memcpy(out, table.data(), table.size() * sizeof(*out));
        *count = static_cast<DWORD>(table.size());
        return S_OK;
    }
    DWORD GetNumFaces() { return static_cast<DWORD>(attributes.size()); }
    DWORD GetNumVertices() { return vertices; }
    HRESULT LockAttributeBuffer(DWORD flags, DWORD** out) {
        CHECK(flags == D3DLOCK_READONLY);
        ++locks;
        *out = attributes.data();
        return lockResult;
    }
    HRESULT UnlockAttributeBuffer() { ++unlocks; return S_OK; }
};

void TestSubsetPlanning() {
    MeshQueries mesh;
    MeshSubsetPlan plan;
    for (DWORD i = 0; i != 129; ++i)
        mesh.table.push_back({i, i * 2, 2, i * 3, 3});
    CHECK(PlanMeshSubset(mesh, 128, plan) == S_OK);
    CHECK(plan.attributeTable && plan.ranges.size() == 1);
    CHECK(plan.ranges[0].FaceStart == 256 && plan.ranges[0].FaceCount == 2);
    CHECK(plan.ranges[0].VertexStart == 384 && plan.ranges[0].VertexCount == 3);
    CHECK(mesh.locks == 0);
    CHECK(PlanMeshSubset(mesh, 500, plan) == S_OK && plan.ranges.empty());
    // D3DX's table-index fast path takes precedence even over an earlier
    // duplicate id; otherwise only the first matching entry is selected.
    mesh.table = {{2, 10, 1, 0, 3}, {9, 20, 1, 0, 3}, {2, 30, 1, 0, 3}};
    CHECK(PlanMeshSubset(mesh, 2, plan) == S_OK && plan.ranges[0].FaceStart == 30);
    mesh.table[2].AttribId = 7;
    CHECK(PlanMeshSubset(mesh, 2, plan) == S_OK && plan.ranges[0].FaceStart == 10);
    mesh.table[0].FaceCount = 0;
    CHECK(PlanMeshSubset(mesh, 2, plan) == S_OK && plan.ranges.empty());
    mesh.countResult = E_FAIL;
    CHECK(PlanMeshSubset(mesh, 2, plan) == E_FAIL && plan.ranges.empty());
    mesh.countResult = S_OK;
    mesh.tableResult = E_FAIL;
    CHECK(PlanMeshSubset(mesh, 2, plan) == E_FAIL && plan.ranges.empty());
    mesh.table.clear();
    mesh.attributes = {2, 2, 9, 2, 9, 2, 2};
    CHECK(PlanMeshSubset(mesh, 2, plan) == S_OK);
    CHECK(!plan.attributeTable && plan.ranges.size() == 3);
    CHECK(plan.ranges[0].FaceStart == 0 && plan.ranges[0].FaceCount == 2);
    CHECK(plan.ranges[1].FaceStart == 3 && plan.ranges[1].FaceCount == 1);
    CHECK(plan.ranges[2].FaceStart == 5 && plan.ranges[2].FaceCount == 2);
    CHECK(plan.ranges[2].VertexStart == 0 && plan.ranges[2].VertexCount == 600);
    CHECK(mesh.locks == 1 && mesh.unlocks == 1);
    unsigned draws = 0;
    CHECK(DrawMeshSubsetPlan(plan, 7, true, [&](const D3DXATTRIBUTERANGE&) {
        ++draws; return E_FAIL;
    }) == E_FAIL && draws == 1);
    draws = 0;
    CHECK(DrawMeshSubsetPlan(plan, 7, false, [&](const D3DXATTRIBUTERANGE&) {
        return ++draws == 1 ? E_FAIL : S_OK;
    }) == S_OK && draws == 3);
    CHECK(PlanMeshSubset(mesh, 9, plan) == S_OK && plan.ranges.size() == 2);
    CHECK(DrawMeshSubsetPlan(plan, 7, false, [](const D3DXATTRIBUTERANGE&) {
        return E_FAIL; // no matching final face: original ushort returns S_OK
    }) == S_OK);
    mesh.lockResult = E_FAIL;
    CHECK(PlanMeshSubset(mesh, 2, plan) == E_FAIL && plan.ranges.empty());
    CHECK(mesh.locks == 3 && mesh.unlocks == 2);
    mesh.attributes.clear();
    CHECK(PlanMeshSubset(mesh, 2, plan) == S_OK && plan.ranges.empty());
}

void TestRuntimeMeshAbi() {
    // Verify the typed mirror against the installed Microsoft runtime, not
    // against another hand-built fake interface. NULLREF needs no visible UI.
#ifdef _WIN64
    HMODULE runtime = LoadLibraryW(L"d3dx9_43.dll");
#else
    HMODULE runtime = LoadLibraryW(L"d3dx9_32.dll");
#endif
    CHECK(runtime != nullptr);
    using CreateMesh = HRESULT(WINAPI*)(DWORD, DWORD, DWORD, DWORD,
        IDirect3DDevice9*, ID3DXMesh**);
    auto createMesh = reinterpret_cast<CreateMesh>(GetProcAddress(runtime, "D3DXCreateMeshFVF"));
    CHECK(createMesh != nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    CHECK(d3d != nullptr);
    HWND window = CreateWindowW(L"STATIC", L"MME regression", WS_OVERLAPPED,
        0, 0, 32, 32, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(window != nullptr);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    IDirect3DDevice9* device = nullptr;
    CHECK(SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
        window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device)));
    using CreateEffect = HRESULT(WINAPI*)(IDirect3DDevice9*, const void*, UINT,
        const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    auto createEffect = reinterpret_cast<CreateEffect>(GetProcAddress(runtime, "D3DXCreateEffect"));
    CHECK(createEffect != nullptr);
    const char source[] = "float4x4 TestMatrix; float4 Values; bool Toggle; technique Test { pass P {} }";
    ID3DXEffect* effect = nullptr;
    ID3DXBuffer* errors = nullptr;
    CHECK(SUCCEEDED(createEffect(device, source, sizeof(source) - 1, nullptr,
        nullptr, 0, nullptr, &effect, &errors)));
    namespace fx = mikudancestudio::fx;
    mikudancestudio::d3dx::D3DXMATRIXF written = {}, read = {};
    for (int r = 0; r != 4; ++r)
        for (int c = 0; c != 4; ++c) written.m[r][c] = static_cast<float>(r * 4 + c);
    CHECK(SUCCEEDED(fx::SetMatrix(effect, "TestMatrix", &written)));
    CHECK(SUCCEEDED(fx::GetMatrix(effect, "TestMatrix", &read)));
    CHECK(std::memcmp(&written, &read, sizeof(read)) == 0);
    CHECK(SUCCEEDED(fx::SetBool(effect, "Toggle", TRUE)));
    BOOL toggle = FALSE;
    CHECK(SUCCEEDED(effect->GetBool("Toggle", &toggle)) && toggle);
    const float values[4] = {1, 2, 3, 4};
    CHECK(SUCCEEDED(fx::SetFloatArray(effect, "Values", values, 4)));
    CHECK(SUCCEEDED(fx::SetTechnique(effect, "Test")));
    UINT passes = 0;
    CHECK(SUCCEEDED(fx::Begin(effect, &passes)) && passes == 1);
    CHECK(SUCCEEDED(fx::BeginPass(effect)));
    CHECK(SUCCEEDED(fx::EndPass(effect)));
    CHECK(SUCCEEDED(fx::End(effect)));
    CHECK(SUCCEEDED(fx::OnLostDevice(effect)));
    CHECK(SUCCEEDED(fx::OnResetDevice(effect)));
    effect->Release();
    if (errors != nullptr) errors->Release();
    ID3DXMesh* mesh = nullptr;
    CHECK(SUCCEEDED(createMesh(129, 387, 0x220, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1,
        device, &mesh)));
    CHECK(mesh->GetNumFaces() == 129 && mesh->GetNumVertices() == 387);
    CHECK(mesh->GetFVF() == 274 && mesh->GetNumBytesPerVertex() == 32);
    DWORD* attributes = nullptr;
    CHECK(SUCCEEDED(mesh->LockAttributeBuffer(0, &attributes)));
    for (DWORD i = 0; i != 129; ++i) attributes[i] = i;
    CHECK(SUCCEEDED(mesh->UnlockAttributeBuffer()));
    MeshSubsetPlan plan;
    CHECK(PlanMeshSubset(*mesh, 128, plan) == S_OK);
    CHECK(plan.ranges.size() == 1 && plan.ranges[0].FaceStart == 128);
    std::vector<D3DXATTRIBUTERANGE> table;
    for (DWORD i = 0; i != 129; ++i) table.push_back({i, i, 1, i * 3, 3});
    CHECK(SUCCEEDED(mesh->SetAttributeTable(table.data(), 129)));
    CHECK(PlanMeshSubset(*mesh, 128, plan) == S_OK && plan.attributeTable);
    CHECK(plan.ranges.size() == 1 && plan.ranges[0].VertexStart == 384);
    mesh->Release();
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    FreeLibrary(runtime);
}

int main(int argc, char** argv) {
    TestControlObjectSelection();
    TestPlanMatrix();
    TestSubsetPlanning();
    if (argc > 1) TestRuntimeMeshAbi();
    std::puts("MME regression tests passed");
}
