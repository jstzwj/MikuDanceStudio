#pragma once
#include "rhi_shader.hpp"
#include <filesystem>
#include <memory>

namespace mikudancestudio::rhi {
struct ShaderCompilerOptions {
    std::filesystem::path executable;
    unsigned timeoutMilliseconds = 30000;
    std::size_t cacheBytes = 32 * 1024 * 1024;
};
struct ShaderCompilerStats {
    std::uint64_t compilations = 0, cacheHits = 0;
};
class SpirvCompiler {
public:
    virtual ~SpirvCompiler() = default;
    virtual bool Available() const = 0;
    virtual const std::string& Identity() const = 0;
    // Expanded modern HLSL, frozen per module. Includes must be resolved by
    // the source owner before this boundary. Failure preserves output.
    virtual bool Compile(const ShaderModule&, SnapshotBytes&, std::string& diagnostic) = 0;
    virtual ShaderCompilerStats Stats() const = 0;
};
std::shared_ptr<SpirvCompiler> CreateDxcSpirvCompiler(ShaderCompilerOptions);
std::shared_ptr<SpirvCompiler> DefaultSpirvCompiler();
} // namespace mikudancestudio::rhi
