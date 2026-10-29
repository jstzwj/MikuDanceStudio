#pragma once
#include "rhi_shader.hpp"
namespace mikudancestudio::rhi {
std::shared_ptr<const ShaderProgram> PostProcessProgram();
ShaderResourceBindings PostProcessBindings(const ModelPipeline&, Handle source);
ShaderPipeline PostProcessPipeline(const ModelPipeline&);
}
