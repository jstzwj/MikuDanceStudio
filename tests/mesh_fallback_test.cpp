#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "../src/render/mme/mesh_subset.hpp"

#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #e); std::exit(1); } } while (0)

struct Mesh {
    DWORD options = 1, stride = 32, attribute = 0;
    unsigned calls = 0;
    HRESULT result = S_OK;
    DWORD GetOptions() const { return options; }
    DWORD GetNumBytesPerVertex() const { return stride; }
    HRESULT DrawSubset(DWORD value) { ++calls; attribute = value; return result; }
};
struct Device {
    D3DCAPS9 caps{};
    unsigned calls = 0;
    HRESULT result = S_OK;
    HRESULT GetDeviceCaps(D3DCAPS9* value) { ++calls; *value = caps; return result; }
};
template <typename Desc> struct Buffer {
    Desc desc{};
    HRESULT result = S_OK;
    HRESULT GetDesc(Desc* value) { *value = desc; return result; }
};

int main() {
    Mesh mesh;
    Device device;
    Buffer<D3DVERTEXBUFFER_DESC> vertices;
    Buffer<D3DINDEXBUFFER_DESC> indices;
    vertices.desc.Size = 70000 * mesh.stride;
    indices.desc.Size = 300 * sizeof(DWORD);
    device.caps.MaxVertexIndex = 70000;
    device.caps.MaxPrimitiveCount = 100;
    unsigned indexedCalls = 0;
    auto draw = [&] {
        return mikudancestudio::mme::DrawMeshSubsetWithFallback(
            mesh, device, vertices, indices, 17,
            [&] { ++indexedCalls; return S_FALSE; });
    };
    // Equality is deliberately allowed (the reference compares capacity,
    // not capacity - 1). Ordinary MME interception must remain selected.
    CHECK(draw() == S_FALSE && indexedCalls == 1 && mesh.calls == 0);
    device.caps.MaxVertexIndex = 69999;
    mesh.result = E_FAIL;
    CHECK(draw() == E_FAIL && mesh.calls == 1 && mesh.attribute == 17);
    CHECK(indexedCalls == 1); // native error must never retry via indexed draw
    device.caps.MaxVertexIndex = 70000;
    device.caps.MaxPrimitiveCount = 99;
    CHECK(draw() == E_FAIL && mesh.calls == 2);
    device.caps.MaxPrimitiveCount = 100;
    vertices.desc.Size = 3 * mesh.stride;
    device.caps.MaxVertexIndex = 65535;
    CHECK(draw() == E_FAIL && mesh.calls == 3); // even a three-vertex mesh
    device.caps.MaxVertexIndex = 65536;
    CHECK(draw() == S_FALSE && indexedCalls == 2);
    device.result = D3DERR_DEVICELOST;
    CHECK(draw() == D3DERR_DEVICELOST && indexedCalls == 2 && mesh.calls == 3);
    device.result = S_OK;
    vertices.result = E_OUTOFMEMORY;
    CHECK(draw() == E_OUTOFMEMORY);
    vertices.result = S_OK;
    indices.result = D3DERR_INVALIDCALL;
    CHECK(draw() == D3DERR_INVALIDCALL);
    // 16-bit meshes do not enter the D3DX 32-bit fallback, even on old caps.
    mesh.options = 0;
    const unsigned queries = device.calls;
    CHECK(draw() == S_FALSE && indexedCalls == 3 && device.calls == queries);
    std::puts("Mesh fallback dispatch tests passed");
}
