#pragma once
#include "render_model_inputs.hpp"
namespace mikudancestudio {
class MMDApp;
namespace mdl {
struct ModelRecord;
}
// Called at CPU producers, before installation on the native device.
void UpdateCpuRenderFrame(MMDApp*, const float* world, const float* lightRotation);
void SetCpuRenderViewport(MMDApp*, const rhi::ModelViewport&);
void SetCpuProjectionPolicy(MMDApp*, render::FovConversion);
bool BuildBoneOperationAxisTransform(MMDApp*, const mdl::ModelRecord*, const float* frameWorld,
                                     float* output);
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
bool BuildLiveCpuModelInputs(MMDApp*, mdl::ModelRecord*, unsigned material, rhi::ModelPipeline&,
                             std::array<rhi::MaterialTextureAsset, 3>&, const char*& reason,
                             bool finalSolvedGeometry = false);
void StageCpuAccessoryDraw(MMDApp*, void*, unsigned material, const float* placement,
                           bool projected);
void StageCpuModelOutline(MMDApp*, mdl::ModelRecord*, unsigned material, unsigned first,
                          unsigned count);
void StageCpuGroundDraw(MMDApp*, bool plane);
void StageCpuProjectedModelShadow(MMDApp*, mdl::ModelRecord*, unsigned material, unsigned first,
                                  unsigned count);
void SubmitLiveCpuModelInputs(MMDApp*, mdl::ModelRecord*, unsigned material, unsigned first,
                              unsigned count, unsigned stride);
#endif
}
