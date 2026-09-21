// Exercise the production worker without exposing a test-only product API.
#include "../src/model/model_skinning.cpp"
#include <omp.h>
#include <memory>
#include <vector>

#if !defined(_OPENMP) || !defined(_M_X64)
#error This regression requires the x64 OpenMP skinning configuration.
#endif

int main() {
    using namespace mikudancestudio;
    constexpr int count = 4099; // Uneven partition boundaries with four workers.
    constexpr float distance = 0.125f;
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bones[2]{};
    std::vector<mdl::PmdVertex> source(count);
    std::vector<mdl::SkinnedVertexBase> serial(count), parallel(count);
    std::vector<mdl::EdgeVertex> serialEdge(count), parallelEdge(count);
    model->vertexCount = count;
    model->rawVertices = source.data();
    model->boneTable = bones;
    model->boneCount = 2;
    for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 3; ++c) {
            bones[b].matInit[c * 5] = b == 0 ? 1.0f : 2.0f;
            bones[b].matInit[12 + c] = (b == 0 ? 4.0f : -8.0f) * (c + 1);
        }
        bones[b].matInit[15] = 1.0f;
    }
    for (int i = 0; i < count; ++i) {
        auto& vertex = source[i];
        for (int c = 0; c < 3; ++c) {
            vertex.position[c] = static_cast<float>((i % 31) + c + 1) * 0.25f;
            vertex.normal[c] = static_cast<float>(c + 1) * 0.125f;
        }
        vertex.uv[0] = static_cast<float>(i) * 0.125f;
        vertex.uv[1] = -static_cast<float>(i) * 0.25f;
        vertex.bone[0] = 0;
        vertex.bone[1] = 1;
        vertex.weightPercent = static_cast<std::int8_t>(i % 101);
        vertex.edgeDisabled = static_cast<std::uint8_t>(i % 2);
        serialEdge[i].diffuse = 0xFF000000u | static_cast<std::uint32_t>(i);
    }
    const auto originalSource = source;
    const auto initialEdge = serialEdge;
    auto* bytes = reinterpret_cast<unsigned char*>(model.get());
    omp_set_dynamic(0);
    omp_set_num_threads(1);
    SkinPmd(bytes, distance, serialEdge.data(), serial.data());
    for (int i = 0; i < count; ++i) {
        const float w = source[i].weightPercent * 0.009999999776482582f;
        const float other = 1.0f - w;
        for (int c = 0; c < 3; ++c) {
            const float p = source[i].position[c];
            const float n = source[i].normal[c];
            const float position = (p * 2.0f - 8.0f * (c + 1)) * other +
                                   (p + 4.0f * (c + 1)) * w;
            const float normal = (n * 2.0f) * other + n * w;
            const float amount = source[i].edgeDisabled ? -0.005f : distance;
            const float edge = position + normal * amount;
            if (serial[i].position[c] != position || serial[i].normal[c] != normal ||
                serialEdge[i].position[c] != edge) {
                std::fprintf(stderr, "PMD transform mismatch at vertex %d component %d\n", i, c);
                return 1;
            }
        }
        if (std::memcmp(serial[i].uv, source[i].uv, sizeof(source[i].uv)) != 0 ||
            serialEdge[i].diffuse != initialEdge[i].diffuse) return 2;
    }
    omp_set_num_threads(4);
    int teamSize = 0;
#pragma omp parallel
    {
#pragma omp single
        teamSize = omp_get_num_threads();
    }
    if (teamSize != 4) {
        std::fprintf(stderr, "Expected four OpenMP workers, got %d\n", teamSize);
        return 3;
    }
    for (int repeat = 0; repeat < 8; ++repeat) {
        std::memset(parallel.data(), 0xCD, parallel.size() * sizeof(parallel[0]));
        parallelEdge = initialEdge;
        SkinPmd(bytes, distance, parallelEdge.data(), parallel.data());
        if (std::memcmp(serial.data(), parallel.data(), serial.size() * sizeof(serial[0])) ||
            std::memcmp(serialEdge.data(), parallelEdge.data(), serialEdge.size() * sizeof(serialEdge[0])))
            return 4;
    }
    if (std::memcmp(source.data(), originalSource.data(), source.size() * sizeof(source[0]))) return 5;
    std::printf("PMD: %d vertices, 1/4 threads, 8 repeated byte-identical runs; UV/diffuse retained\n", count);
    return 0;
}
