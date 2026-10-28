#pragma once
#include <cstddef>
#include <cstdint>
namespace mikudancestudio::render {
struct ModelVertex {
    float position[3], normal[3], uv[2];
};
template <std::size_t N> struct ExtendedModelVertex {
    ModelVertex base;
    float additionalUv[N][4];
};
template <> struct ExtendedModelVertex<0> {
    ModelVertex base;
};
struct ModelEdgeVertex {
    float position[3];
    std::uint32_t diffuse;
}; // ARGB
static_assert(sizeof(ModelVertex) == 32 && sizeof(ModelEdgeVertex) == 16);
static_assert(sizeof(ExtendedModelVertex<0>) == 32 && sizeof(ExtendedModelVertex<4>) == 96);
}
