#pragma once

#include <cstddef>
#include <d3d9.h>

namespace mikudancestudio {

struct TexturedScreenVertex {
    float x, y, z, reciprocalW;
    D3DCOLOR color;
    float u, v;
};

struct ColoredScreenVertex {
    float x, y, z, reciprocalW;
    D3DCOLOR color;
};

inline constexpr DWORD kTexturedScreenVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
inline constexpr DWORD kColoredScreenVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
inline constexpr std::size_t kTextOverlayVertexCapacity = 1000;
inline constexpr std::size_t kSpriteOverlayVertexCapacity = 10000;
inline constexpr std::size_t kLineOverlayVertexCapacity = 10000;
inline constexpr std::size_t kBackgroundQuadVertexCount = 6;
inline constexpr std::size_t kBackgroundQuadBytes =
    kBackgroundQuadVertexCount * sizeof(TexturedScreenVertex);

static_assert(sizeof(TexturedScreenVertex) == 28);
static_assert(offsetof(TexturedScreenVertex, reciprocalW) == 12);
static_assert(offsetof(TexturedScreenVertex, color) == 16);
static_assert(offsetof(TexturedScreenVertex, u) == 20);
static_assert(offsetof(TexturedScreenVertex, v) == 24);
static_assert(sizeof(ColoredScreenVertex) == 20);
static_assert(offsetof(ColoredScreenVertex, color) == 16);

} // namespace mikudancestudio
