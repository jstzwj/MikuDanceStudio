#pragma once

#include <d3dx9.h>
#include <new>
#include <stdexcept>
#include <vector>

namespace mikudancestudio::mme {

struct MeshSubsetPlan {
    bool attributeTable = false;
    std::vector<D3DXATTRIBUTERANGE> ranges;
};

// D3DX9_43 DrawSubset has two paths: one selected optimized table entry,
// or every consecutive matching run in the per-face attribute buffer.
// It prefers table[attribute] when that entry has the requested id, then
// scans for the first match. Do not merge or draw duplicate table entries.
// Evidence: GXTri3Mesh<ushort>::DrawSubset 18006AB3C and <uint> 18006C854.
template <typename Mesh>
HRESULT PlanMeshSubset(Mesh& mesh, DWORD attribute, MeshSubsetPlan& plan) {
    plan = {};
    DWORD count = 0;
    HRESULT hr = mesh.GetAttributeTable(nullptr, &count);
    if (FAILED(hr))
        return hr;
    try {
        if (count != 0) {
            plan.attributeTable = true;
            std::vector<D3DXATTRIBUTERANGE> table(count);
            hr = mesh.GetAttributeTable(table.data(), &count);
            if (FAILED(hr))
                return hr;
            if (count > table.size())
                return D3DERR_INVALIDCALL;
            const D3DXATTRIBUTERANGE* selected = nullptr;
            if (attribute < count && table[attribute].AttribId == attribute) {
                selected = &table[attribute];
            } else {
                for (DWORD i = 0; i < count; ++i) {
                    if (table[i].AttribId == attribute) {
                        selected = &table[i];
                        break;
                    }
                }
            }
            if (selected != nullptr && selected->FaceCount != 0)
                plan.ranges.push_back(*selected);
            return S_OK;
        }

        const DWORD faces = mesh.GetNumFaces();
        if (faces == 0)
            return S_OK;
        DWORD* attributes = nullptr;
        hr = mesh.LockAttributeBuffer(D3DLOCK_READONLY, &attributes);
        if (FAILED(hr))
            return hr;
        struct Unlock {
            Mesh& mesh;
            ~Unlock() { mesh.UnlockAttributeBuffer(); }
        } unlock{mesh};
        if (attributes == nullptr)
            return D3DERR_INVALIDCALL;
        const DWORD vertices = mesh.GetNumVertices();
        DWORD face = 0;
        while (face < faces) {
            if (attributes[face] != attribute) {
                ++face;
                continue;
            }
            const DWORD start = face++;
            while (face < faces && attributes[face] == attribute)
                ++face;
            plan.ranges.push_back({attribute, start, face - start, 0, vertices});
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        plan.ranges.clear();
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        plan.ranges.clear();
        return E_OUTOFMEMORY;
    }
}

template <typename Draw>
HRESULT DrawMeshSubsetPlan(const MeshSubsetPlan& plan, DWORD totalFaces,
                          bool indices32Bit, Draw draw) {
    HRESULT hr = S_OK;
    for (const auto& range : plan.ranges) {
        const HRESULT drawn = draw(range);
        // The 16-bit unoptimized implementation ignores intermediate run
        // HRESULTs; only a matching run at the final face returns its HRESULT.
        // The 32-bit implementation stops on any draw failure.
        if (plan.attributeTable || indices32Bit ||
            range.FaceStart + range.FaceCount == totalFaces)
            hr = drawn;
        if (indices32Bit && FAILED(drawn))
            break;
    }
    return hr;
}

} // namespace mikudancestudio::mme
