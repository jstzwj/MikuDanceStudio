#include "mikudancestudio/shader_compiler.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (0)
int main() try {
    auto compiler = DefaultSpirvCompiler();
    CHECK(compiler->Available() && !compiler->Identity().empty());
    ShaderModule source;
    source.stage = ShaderStage::Vertex;
    source.entry = "vertex";
    source.hlsl = "float4 vertex(float3 p:POSITION):SV_POSITION{return float4(p,1);}";
    SnapshotBytes output;
    std::string error;
    const auto first = compiler->Stats();
    CHECK(compiler->Compile(source, output, error) && output.size() >= 20 && error.empty());
    const auto expected = output;
    CHECK(compiler->Compile(source, output, error) && output == expected);
    CHECK(compiler->Stats().compilations == first.compilations + 1 &&
          compiler->Stats().cacheHits == first.cacheHits + 1);
    source.hlsl += " invalid shader";
    CHECK(!compiler->Compile(source, output, error) && !error.empty() && output == expected);
    source.hlsl = "#include \"unexpanded.hlsl\"\n";
    CHECK(!compiler->Compile(source, output, error) &&
          error == "shader_includes_must_be_expanded" && output == expected);
    auto missing = CreateDxcSpirvCompiler({});
    CHECK(!missing->Available() && !missing->Compile(source, output, error) && output == expected);
    source.spirv = expected;
    CHECK(missing->Compile(source, output, error) && output == expected && error.empty());
    std::puts("DXC runtime: compile/cache/diagnostic/atomic failure/precompiled passed");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
