#pragma once
#include "rhi_frame_passes.hpp"
namespace mikudancestudio::rhi {
// Driver-native anisotropy has no cross-API kernel/LOD guarantee. The portable
// profile explicitly chooses bilinear texels and linear mip interpolation.
// Neither profile changes mip contents, addressing, gamma or alpha-test/blend state.
enum class MaterialSamplingPolicy {
    Native,
    PortableTrilinear
};
inline bool ApplyMaterialSampling(ModelPipeline& pipeline, MaterialSamplingPolicy policy) {
    if (policy != MaterialSamplingPolicy::Native &&
        policy != MaterialSamplingPolicy::PortableTrilinear)
        return false;
    if (policy == MaterialSamplingPolicy::PortableTrilinear)
        if (pipeline.postProcess.mode == PostProcessMode::None)
            for (unsigned stage = 0; stage < 3; ++stage)
                if (pipeline.shader.layout.stages[stage].role != MaterialTextureRole::None) {
                    auto& sampler = pipeline.shader.samplers[stage];
                    sampler.min = sampler.mag = sampler.mip = SamplerFilter::Linear;
                    sampler.maxAnisotropy = 1;
                }
    return true;
}
inline bool ApplyMaterialSampling(RenderFrame& frame, MaterialSamplingPolicy policy) {
    if (policy != MaterialSamplingPolicy::Native &&
        policy != MaterialSamplingPolicy::PortableTrilinear)
        return false;
    for (auto& pass : frame.passes)
        for (auto& draw : pass.draws)
            ApplyMaterialSampling(draw.pipeline, policy);
    return true;
}
}
