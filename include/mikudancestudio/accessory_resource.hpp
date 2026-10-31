#pragma once
#include "mikudancestudio/cpu_math.hpp"
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
namespace mikudancestudio::runtime {
struct AccessoryVertex {
    cpu_math::Vec3 position{}, normal{};
    std::array<float, 2> uv{};
};
struct AccessoryMaterial {
    std::array<float, 4> diffuse{}, ambient{};
    cpu_math::Vec3 specular{}, emissive{};
    float shininess = 0;
    std::string texture, originalTextureBytes;
};
struct AccessorySubset {
    std::uint32_t material = 0, firstIndex = 0, indexCount = 0;
};
// Immutable CPU import; no device, COM object, texture decoder or filesystem policy.
class AccessoryResource final {
public:
    static bool Parse(const void*, std::size_t, std::shared_ptr<const AccessoryResource>&,
                      std::string& reason);
    const std::vector<AccessoryVertex>& Vertices() const { return vertices_; }
    const std::vector<std::uint32_t>& Indices() const { return indices_; }
    const std::vector<AccessoryMaterial>& Materials() const { return materials_; }
    const std::vector<AccessorySubset>& Subsets() const { return subsets_; }

private:
    std::vector<AccessoryVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<AccessoryMaterial> materials_;
    std::vector<AccessorySubset> subsets_;
};
}
