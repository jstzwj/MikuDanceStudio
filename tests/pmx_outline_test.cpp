// Exercise the production PMX worker, including its architecture-specific tail.
#include "../src/model/model_skinning.cpp"
#include <omp.h>
#include <float.h>
#include <memory>
#include <vector>

int main() {
    using namespace mikudancestudio;
    constexpr unsigned count = 4100;
    // IEEE inputs expose early float stores, including cancellation. Expected
    // results follow the verified x86 FMUL/FMUL/FMUL/FADD/FSTP sequence.
    constexpr std::uint32_t positions[] = {0x37FBA882, 0xBC8B4396, 0x3C3923A3};
    constexpr std::uint32_t normals[] = {0xBF4CCCCD, 0x3F666666, 0xBF333333};
    constexpr std::uint32_t vertexScales[] = {0x3E051EB8, 0x3EFAE148, 0x3F558106};
    constexpr std::uint32_t materialSizes[] = {0x3F19999A, 0x3F804189, 0x3F70A3D7};
    constexpr std::uint32_t extended[] = {0xBB1B6018, 0x35BD010F, 0xBC20CAE8};
    constexpr std::uint32_t single[] = {0xBB1B6019, 0x35BCC000, 0xBC20CAE7};
    constexpr std::uint32_t shrinkExtended[] = {0x3B840E17, 0xBCB020C5, 0x3C727BB3};
    constexpr std::uint32_t shrinkSingle[] = {0x3B840E18, 0xBCB020C5, 0x3C727BB3};
    const auto value = [](std::uint32_t bits) {
        float result;
        std::memcpy(&result, &bits, sizeof result);
        return result;
    };
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    for (unsigned i = 0; i < 4; ++i)
        bone.matInit[i * 5] = 1;
    mdl::ModelMaterialRecord materials[3]{};
    std::vector<mdl::PmxVertex> vertices(count);
    std::vector<mdl::SkinnedVertex<0>> main(count);
    std::vector<mdl::EdgeVertex> edge(count);
    for (unsigned i = 0; i < 3; ++i) {
        materials[i].edgeSize = value(materialSizes[i]);
        materials[i].edgeColor[3] = 1;
    }
    for (unsigned i = 0; i < count; ++i) {
        const unsigned example = i % 3;
        auto& vertex = vertices[i];
        vertex.weightType = mdl::PmxWeightType::bdef1;
        vertex.bone[0] = 0;
        vertex.materialIndex = example;
        vertex.hasPmxEdgeData = i % 2;
        vertex.edgeScale = value(vertexScales[example]);
        for (unsigned c = 0; c < 3; ++c) {
            vertex.position[c] = value(positions[example]);
            vertex.normal[c] = value(normals[example]);
        }
    }
    model->vertexCount = count;
    model->pmxVertices = vertices.data();
    model->boneTable = &bone;
    model->boneCount = 1;
    model->materials = materials;
    omp_set_dynamic(0);
    for (int threads : {1, 4}) {
        omp_set_num_threads(threads);
        int teamSize = 0;
#pragma omp parallel
        {
#pragma omp single
            teamSize = omp_get_num_threads();
        }
        if (teamSize != threads)
            return 2;
        for (bool precision53 : {false, true}) {
#if defined(_M_IX86)
            // Set and restore each participating thread explicitly. This is
            // mode coverage, not a claim that native workers copy caller state.
            unsigned saved[4]{};
#pragma omp parallel
            {
                const int index = omp_get_thread_num();
                saved[index] = _controlfp(0, 0);
                _controlfp(precision53 ? _PC_53 : _PC_24, _MCW_PC);
            }
#endif
            SkinPmx<0>(model.get(), value(0x3D1D97F6), true, edge.data(), main.data());
#if defined(_M_IX86)
#pragma omp parallel
            _controlfp(saved[omp_get_thread_num()], _MCW_PC);
            const auto* expected = precision53 ? extended : single;
            const auto* shrink = precision53 ? shrinkExtended : shrinkSingle;
#else
            const auto* expected = single;
            const auto* shrink = shrinkSingle;
#endif
            for (unsigned i = 0; i < count; ++i) {
                std::uint32_t bits;
                std::memcpy(&bits, &edge[i].position[0], sizeof bits);
                const auto wanted = vertices[i].hasPmxEdgeData ? expected[i % 3] : shrink[i % 3];
                if (bits != wanted) {
                    std::fprintf(
                        stderr,
                        "PMX outline: threads=%d precision53=%d vertex=%u got=%08X expected=%08X\n",
                        threads, precision53, i, bits, wanted);
                    return 1;
                }
            }
        }
    }
    std::puts("PMX outline: 4100 vertices, 1/4 workers, architecture-specific rounding verified");
    return 0;
}
