#pragma once
#include <d3d9.h>
#include "rhi_model_replay.hpp"
namespace mikudancestudio::rhi {
struct D3D9ReplayState {
    unsigned stage, key;
    DWORD value;
};
// CPU-only transport for the DX9 recorder. Numeric enums are decoded at this
// boundary and must never leak into the portable stream as unchecked casts.
struct D3D9ReplaySnapshot {
    std::vector<D3D9ReplayState> renderStates, stageStates, samplerStates;
    std::array<D3DMATRIX, 8> textureMatrices{};
    struct Light {
        DWORD index;
        D3DLIGHT9 value;
        BOOL enabled;
    };
    std::vector<Light> lights;
    RECT scissor{};
    D3DMATRIX world{}, view{}, projection{};
    D3DMATERIAL9 material{};
    D3DVIEWPORT9 viewport{};
    unsigned type = 0, minimum = 0, vertices = 0, start = 0, count = 0, offset = 0, stride = 0;
    DWORD fvf = 0;
    int base = 0;
    bool indexed = false;
    // Supplied by the diagnostic scene walk, never inferred from a sampler
    // slot: accessories can bind Base or Sphere at cascade stage zero.
    bool accessoryMaterial = false;
    D3DFORMAT indexFormat = D3DFMT_UNKNOWN;
    std::vector<unsigned char> vertexBytes, indexBytes;
    std::array<std::vector<unsigned char>, 8> textureBytes;
    struct Texture {
        bool present = false;
        D3DFORMAT format = D3DFMT_UNKNOWN;
        unsigned width = 0, height = 0, levels = 0, lod = 0;
        bool autogen = false;
        DWORD autogenFilter = D3DTEXF_LINEAR;
    };
    std::array<Texture, 8> textureInfo{};
    // Explicit RGBA snapshots of hidden native AUTOGEN levels, including top.
    // Empty means no preparation proof; a top image alone is insufficient.
    std::array<ModelTextureData, 8> preparedMips;
};
// Appends an owned snapshot transactionally. Stable logical slots use strictly
// increasing versions; distinct draws can never overwrite prior bytes.
// PositionColor capture is opt-in until the independent shadow image gate passes.
ModelReplayResult AppendD3D9ReplaySnapshot(ModelReplayStream&, const D3D9ReplaySnapshot&,
                                           bool allowPositionColor = false);
}
