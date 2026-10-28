#pragma once
#include "rhi_material_textures.hpp"
#include <cstdint>
#include <type_traits>
namespace mikudancestudio::rhi {
// Diagnostic capture only, not a portable file format. Reader checks version
// and native input size. No original model/texture assets are committed.
struct RealBatchCapture {
    std::uint32_t magic = 0x3142444d, version = 5, inputSize = sizeof(MaterialShaderInputs);
    std::uint32_t frame = 0, slot = 0, material = 0, first = 0, count = 0, vertexBytes = 0,
                  indexBytes = 0;
    std::uint32_t width = 0, height = 0, multisample = 0, multisampleQuality = 0;
    std::uint32_t history = 0;
    std::uint32_t colorFormat = 21;     // Diagnostic native DX9 format token.
    std::uint32_t vertexProcessing = 0; // Actual GetSoftwareVertexProcessing result.
    std::array<std::uint32_t, 210> render{};
    std::array<std::array<std::uint32_t, 33>, 3> stages{};
    std::array<std::array<std::uint32_t, 14>, 3> samplers{};
};
struct RealBatchTexture {
    std::uint32_t width = 0, height = 0, levels = 0, autogen = 0;
};
static_assert(std::is_trivially_copyable<MaterialShaderInputs>::value,
              "Diagnostic snapshot must have no owning resources");
}
