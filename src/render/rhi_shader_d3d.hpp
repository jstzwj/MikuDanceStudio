#pragma once
#include "mikudancestudio/rhi_shader.hpp"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <map>
#include <tuple>
#include <cctype>

namespace mikudancestudio::rhi::detail {
struct D3DShaderProgram {
    Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel;
};
inline bool CompileD3DProgram(const ShaderProgram& program, D3DShaderProgram& out,
                              std::string& error) {
    const auto& desc = program.description;
    std::map<std::pair<std::string, unsigned>, std::pair<unsigned, unsigned>> outputs;
    for (unsigned stage = 1; stage <= 2; ++stage) {
        const auto& source = stage == 1 ? desc.vertex : desc.pixel;
        auto& binary = stage == 1 ? out.vertex : out.pixel;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const auto hr = D3DCompile(
            source.hlsl.data(), source.hlsl.size(), desc.name.c_str(), nullptr, nullptr,
            source.entry.c_str(), stage == 1 ? "vs_5_0" : "ps_5_0",
            D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0, &binary, &errors);
        if (FAILED(hr)) {
            error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                         errors->GetBufferSize())
                           : "shader_compile_failed";
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(binary->GetBufferPointer(), binary->GetBufferSize(),
                              IID_PPV_ARGS(&reflection)))) {
            error = "shader_reflection_failed";
            return false;
        }
        D3D11_SHADER_DESC shader{};
        reflection->GetDesc(&shader);
        if (reflection->GetRequiresFlags()) {
            error = "shader_feature_not_supported";
            return false;
        }
        if (stage == 1) {
            bool position = false;
            for (unsigned n = 0; n < shader.OutputParameters; ++n) {
                D3D11_SIGNATURE_PARAMETER_DESC output{};
                reflection->GetOutputParameterDesc(n, &output);
                if (output.SystemValueType == D3D_NAME_POSITION)
                    position =
                        output.Mask == 15 && output.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32;
                std::string semantic = output.SemanticName;
                for (auto& c : semantic)
                    c = char(std::toupper(static_cast<unsigned char>(c)));
                outputs[{semantic, output.SemanticIndex}] = {output.ComponentType, output.Mask};
            }
            if (!position) {
                error = "shader_vertex_position_missing";
                return false;
            }
        } else
            for (unsigned n = 0; n < shader.InputParameters; ++n) {
                D3D11_SIGNATURE_PARAMETER_DESC input{};
                reflection->GetInputParameterDesc(n, &input);
                if (input.SystemValueType != D3D_NAME_UNDEFINED)
                    continue;
                std::string semantic = input.SemanticName;
                for (auto& c : semantic)
                    c = char(std::toupper(static_cast<unsigned char>(c)));
                const auto found = outputs.find({semantic, input.SemanticIndex});
                if (found == outputs.end() ||
                    found->second !=
                        std::make_pair(unsigned(input.ComponentType), unsigned(input.Mask))) {
                    error = "shader_stage_interface_mismatch";
                    return false;
                }
            }
        for (unsigned n = 0; n < shader.BoundResources; ++n) {
            D3D11_SHADER_INPUT_BIND_DESC resource{};
            reflection->GetResourceBindingDesc(n, &resource);
            ShaderBindingKind kind;
            if (resource.Type == D3D_SIT_CBUFFER)
                kind = ShaderBindingKind::Constants;
            else if (resource.Type == D3D_SIT_TEXTURE &&
                     resource.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
                     (resource.ReturnType == D3D_RETURN_TYPE_FLOAT ||
                      resource.ReturnType == D3D_RETURN_TYPE_UNORM ||
                      resource.ReturnType == D3D_RETURN_TYPE_SNORM))
                kind = ShaderBindingKind::Texture;
            else if (resource.Type == D3D_SIT_SAMPLER)
                if (resource.uFlags & D3D_SIF_COMPARISON_SAMPLER) {
                    error = "comparison_sampling_not_supported";
                    return false;
                } else
                    kind = ShaderBindingKind::Sampler;
            else {
                error = "shader_resource_type_not_supported";
                return false;
            }
            const ShaderBinding* binding = nullptr;
            for (const auto& b : desc.bindings)
                if (b.kind == kind && b.slot == resource.BindPoint && (b.stages & stage))
                    binding = &b;
            if (!binding || resource.BindCount != 1) {
                error = "shader_reflection_binding_mismatch";
                return false;
            }
            if (kind != ShaderBindingKind::Constants)
                continue;
            auto* cb = reflection->GetConstantBufferByName(resource.Name);
            D3D11_SHADER_BUFFER_DESC block{};
            cb->GetDesc(&block);
            if (block.Size != binding->bytes) {
                error = "shader_reflection_constant_size_mismatch";
                return false;
            }
            for (const auto& member : binding->members) {
                auto* variable = cb->GetVariableByName(member.name.c_str());
                D3D11_SHADER_VARIABLE_DESC v{};
                D3D11_SHADER_TYPE_DESC type{};
                if (FAILED(variable->GetDesc(&v)) || FAILED(variable->GetType()->GetDesc(&type)) ||
                    v.StartOffset != member.offset ||
                    (type.Elements ? type.Elements : 1) != member.count) {
                    error = "shader_reflection_member_mismatch";
                    return false;
                }
                const auto matrix = member.type == ShaderValueType::Matrix4x4;
                const auto columns = member.type == ShaderValueType::Float2               ? 2u
                                     : member.type == ShaderValueType::Float3             ? 3u
                                     : (member.type == ShaderValueType::Float4 || matrix) ? 4u
                                                                                          : 1u;
                const auto scalar = member.type == ShaderValueType::Int    ? D3D_SVT_INT
                                    : member.type == ShaderValueType::UInt ? D3D_SVT_UINT
                                                                           : D3D_SVT_FLOAT;
                if (type.Type != scalar || type.Columns != columns ||
                    type.Rows != (matrix ? 4u : 1u) ||
                    (matrix && type.Class != D3D_SVC_MATRIX_ROWS)) {
                    error = "shader_reflection_member_type_mismatch";
                    return false;
                }
            }
        }
        if (stage == 2 && shader.OutputParameters != desc.colorOutputs.size()) {
            error = "shader_output_layout_mismatch";
            return false;
        }
        if (stage == 2)
            for (unsigned n = 0; n < shader.OutputParameters; ++n) {
                D3D11_SIGNATURE_PARAMETER_DESC output{};
                reflection->GetOutputParameterDesc(n, &output);
                if (output.SystemValueType != D3D_NAME_TARGET ||
                    output.SemanticIndex >= desc.colorOutputs.size() ||
                    output.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32 ||
                    output.Mask != ((1u << desc.colorOutputs[output.SemanticIndex]) - 1)) {
                    error = "shader_output_not_supported";
                    return false;
                }
            }
        if (stage == 1)
            for (unsigned n = 0; n < shader.InputParameters; ++n) {
                D3D11_SIGNATURE_PARAMETER_DESC input{};
                reflection->GetInputParameterDesc(n, &input);
                if (input.SystemValueType != D3D_NAME_UNDEFINED)
                    continue;
                const ShaderVertexInput* found = nullptr;
                for (const auto& i : desc.inputs)
                    if (!_stricmp(i.semantic.c_str(), input.SemanticName) &&
                        i.semanticIndex == input.SemanticIndex)
                        found = &i;
                const auto components = found ? (found->format == ShaderVertexFormat::Float2   ? 2u
                                                 : found->format == ShaderVertexFormat::Float3 ? 3u
                                                                                               : 4u)
                                              : 0;
                if (!found || input.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32 ||
                    input.Mask != ((1u << components) - 1)) {
                    error = "shader_reflection_vertex_input_mismatch";
                    return false;
                }
            }
    }
    error.clear();
    return true;
}
}
