#pragma once
#include "mikudancestudio/rhi_shader.hpp"
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#include <unknwn.h>
#include <oaidl.h>
#include <dxc/dxcapi.h>
#include <wrl/client.h>
#endif
namespace mikudancestudio::rhi::detail {
#ifdef _WIN32
inline DxcCreateInstanceProc DxcFactory() {
    static const auto factory = []() -> DxcCreateInstanceProc {
        DWORD previousMode = 0;
        const bool changed = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX,
                                                &previousMode) != FALSE;
        struct ErrorMode {
            bool changed;
            DWORD previous;
            ~ErrorMode() {
                if (changed)
                    SetThreadErrorMode(previous, nullptr);
            }
        } restore{changed, previousMode};
        auto module = LoadLibraryExW(L"dxcompiler.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#ifdef MDS_DXC_LIBRARY_PATH
        if (!module) {
            const std::string path = MDS_DXC_LIBRARY_PATH;
            const auto size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
            std::wstring wide(size, 0);
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), size);
            module =
                LoadLibraryExW(wide.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
#endif
        return module ? reinterpret_cast<DxcCreateInstanceProc>(
                            GetProcAddress(module, "DxcCreateInstance"))
                      : nullptr;
    }();
    return factory;
}
#endif
inline bool RuntimeSpirvCompilerAvailable() {
#ifdef _WIN32
    return DxcFactory() != nullptr;
#else
    return false;
#endif
}
inline bool CompileSpirv(const ShaderModule& source, SnapshotBytes& output, std::string& error) {
    if (!source.spirv.empty()) {
        output = source.spirv;
        return true;
    }
#ifdef _WIN32
    const auto factory = DxcFactory();
    if (!factory) {
        error = "runtime_spirv_compiler_unavailable";
        return false;
    }
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
    if (FAILED(factory(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        error = "spirv_compiler_creation_failed";
        return false;
    }
    const std::wstring entry(source.entry.begin(), source.entry.end());
    const wchar_t* args[] = {L"-spirv",
                             L"-fspv-target-env=vulkan1.3",
                             L"-fvk-use-dx-layout",
                             L"-fvk-stage-io-order=decl",
                             L"-fvk-b-shift",
                             L"0",
                             L"0",
                             L"-fvk-t-shift",
                             L"4",
                             L"0",
                             L"-fvk-s-shift",
                             L"20",
                             L"0",
                             L"-Zpr",
                             L"-Ges",
                             L"-Gis",
                             L"-T",
                             source.stage == ShaderStage::Vertex ? L"vs_6_0" : L"ps_6_0",
                             L"-E",
                             entry.c_str()};
    DxcBuffer buffer{source.hlsl.data(), source.hlsl.size(), DXC_CP_UTF8};
    Microsoft::WRL::ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&buffer, args, UINT32(std::size(args)), nullptr,
                                 IID_PPV_ARGS(&result)))) {
        error = "spirv_compile_failed";
        return false;
    }
    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status)) {
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        error = errors ? std::string(errors->GetStringPointer(), errors->GetStringLength())
                       : "spirv_compile_failed";
        return false;
    }
    Microsoft::WRL::ComPtr<IDxcBlob> binary;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&binary), nullptr)) || !binary) {
        error = "missing_spirv_object";
        return false;
    }
    output.resize(binary->GetBufferSize());
    std::memcpy(output.data(), binary->GetBufferPointer(), output.size());
    output.Seal();
    return true;
#else
    error = "runtime_spirv_compiler_unavailable";
    return false;
#endif
}
// Reflect the bounded graphics ABI before passing modules to the native driver.
// Arbitrary storage resources, descriptor arrays/sets and MRT are not in this profile.
inline bool ReflectSpirv(const ShaderProgramDesc& desc, const ShaderModule& source,
                         const SnapshotBytes& binary, std::string& error,
                         std::map<unsigned, std::vector<unsigned>>* stageInterface = nullptr) try {
    const auto fail = [&](const char* why) {
        error = why;
        return false;
    };
    if (binary.size() < 20 || binary.size() % 4)
        return fail("invalid_spirv_size");
    std::vector<std::uint32_t> words(binary.size() / 4);
    std::memcpy(words.data(), binary.data(), binary.size());
    if (words[0] != 0x07230203 || words[1] > 0x00010600 || words[4])
        return fail("invalid_spirv_header");
    struct Type {
        unsigned op = 0;
        std::vector<unsigned> data;
    };
    struct Variable {
        unsigned type = 0, storage = 0;
    };
    std::map<unsigned, Type> types;
    std::map<unsigned, Variable> variables;
    std::map<std::pair<unsigned, unsigned>, unsigned> decorations, offsets;
    std::map<std::pair<unsigned, unsigned>, std::string> memberNames;
    std::map<std::tuple<unsigned, unsigned, unsigned>, unsigned> memberDecorations;
    std::map<unsigned, unsigned> constants;
    bool entry = false;
    for (std::size_t n = 5; n < words.size();) {
        const auto count = words[n] >> 16, op = words[n] & 65535u;
        if (!count || count > words.size() - n)
            return fail("invalid_spirv_instruction");
        const auto* w = words.data() + n;
        if (op == 89 || op == 90 || op == 93 || op == 94 || op == 97)
            return fail("comparison_sampling_not_supported");
        if (op == 17) {
            // Float32 signed-zero/Inf/NaN preservation is required when the
            // Vulkan device is selected, and emitted by DXC's strict mode.
            if (count != 2 || (w[1] != 0 && w[1] != 1 && w[1] != 50 && w[1] != 51 && w[1] != 4466 &&
                               w[1] != 5379))
                return fail("shader_feature_not_supported");
        } else if (op == 15) {
            if (count < 4)
                return fail("invalid_spirv_entrypoint");
            const auto* name = reinterpret_cast<const char*>(w + 3);
            const auto* end = static_cast<const char*>(std::memchr(name, 0, (count - 3) * 4));
            if (!end)
                return fail("invalid_spirv_entrypoint");
            if (std::string(name, end) == source.entry) {
                if (w[1] != (source.stage == ShaderStage::Vertex ? 0u : 4u))
                    return fail("shader_stage_mismatch");
                entry = true;
            }
        } else if (op == 6 && count >= 4) {
            const auto* name = reinterpret_cast<const char*>(w + 3);
            const auto* end = static_cast<const char*>(std::memchr(name, 0, (count - 3) * 4));
            if (!end)
                return fail("invalid_spirv_member_name");
            memberNames[{w[1], w[2]}] = std::string(name, end);
        } else if (op == 71 && count >= 4)
            decorations[{w[1], w[2]}] = w[3];
        else if (op == 72 && count >= 5 && w[3] == 35)
            offsets[{w[1], w[2]}] = w[4];
        else if (op == 72 && count >= 4)
            memberDecorations[{w[1], w[2], w[3]}] = count >= 5 ? w[4] : 1;
        else if (op >= 19 && op <= 33 && count >= 2)
            types[w[1]] = {op, std::vector<unsigned>(w + 2, w + count)};
        else if (op == 43 && count == 4)
            constants[w[2]] = w[3];
        else if (op == 59 && count >= 4)
            variables[w[2]] = {w[1], w[3]};
        n += count;
    }
    if (!entry)
        return fail("missing_spirv_entrypoint");
    std::function<unsigned(unsigned, unsigned)> size = [&](unsigned id,
                                                           unsigned depth) -> unsigned {
        if (depth > 16)
            return 0;
        const auto t = types.find(id);
        if (t == types.end())
            return 0;
        const auto& a = t->second.data;
        if ((t->second.op == 21 || t->second.op == 22) && !a.empty())
            return a[0] / 8;
        if ((t->second.op == 23 || t->second.op == 24) && a.size() == 2)
            return size(a[0], depth + 1) * a[1];
        if (t->second.op == 28 && a.size() == 2) {
            const auto step = decorations.find({id, 6});
            const auto count = constants.find(a[1]);
            if (step == decorations.end() || count == constants.end() || !count->second ||
                step->second > 65536 / count->second)
                return 0;
            return step->second * count->second;
        }
        if (t->second.op == 30) {
            unsigned bytes = 0;
            for (unsigned n = 0; n < a.size(); ++n) {
                const auto offset = offsets.find({id, n});
                if (offset == offsets.end())
                    return 0;
                const auto member = size(a[n], depth + 1);
                if (!member || offset->second > 65536 || member > 65536 - offset->second)
                    return 0;
                bytes = (std::max)(bytes, offset->second + member);
            }
            return (bytes + 15) / 16 * 16;
        }
        return 0;
    };
    bool vertexPosition = false;
    std::set<unsigned> pixelOutputs;
    for (const auto& v : variables) {
        const auto pointer = types.find(v.second.type);
        if (pointer == types.end() || pointer->second.op != 32 || pointer->second.data.size() != 2)
            return fail("invalid_spirv_pointer");
        const auto pointee = pointer->second.data[1];
        const auto type = types.find(pointee);
        if (type == types.end())
            return fail("invalid_spirv_type");
        if (source.stage == ShaderStage::Vertex && v.second.storage == 3) {
            const auto builtin = decorations.find({v.first, 11});
            if (builtin != decorations.end() && builtin->second == 0)
                vertexPosition = type->second.op == 23 && type->second.data.size() == 2 &&
                                 type->second.data[1] == 4 &&
                                 types.at(type->second.data[0]).op == 22 &&
                                 types.at(type->second.data[0]).data == std::vector<unsigned>{32};
        }
        if (stageInterface && ((source.stage == ShaderStage::Vertex && v.second.storage == 3) ||
                               (source.stage == ShaderStage::Pixel && v.second.storage == 1))) {
            const auto location = decorations.find({v.first, 30});
            if (location != decorations.end()) {
                auto t = type->second;
                unsigned components = 1;
                if (t.op == 23 && t.data.size() == 2) {
                    components = t.data[1];
                    t = types.at(t.data[0]);
                }
                if ((t.op != 21 && t.op != 22) || t.data.empty() || t.data[0] != 32 ||
                    !components || components > 4)
                    return fail("shader_stage_interface_not_supported");
                auto shape = t.data;
                shape.insert(shape.begin(), {t.op, components});
                if (!stageInterface->emplace(location->second, std::move(shape)).second)
                    return fail("shader_stage_interface_mismatch");
            }
        }
        if (v.second.storage == 0 || v.second.storage == 2 || v.second.storage == 12) {
            const auto set = decorations.find({v.first, 34}),
                       binding = decorations.find({v.first, 33});
            if (set == decorations.end() || set->second || binding == decorations.end())
                return fail("shader_descriptor_set_not_supported");
            ShaderBindingKind kind;
            if (v.second.storage == 2 && type->second.op == 30)
                kind = ShaderBindingKind::Constants;
            else if (v.second.storage == 0 && type->second.op == 25 &&
                     type->second.data.size() >= 7 && type->second.data[1] == 1 &&
                     type->second.data[2] != 1 && !type->second.data[3] && !type->second.data[4] &&
                     type->second.data[5] == 1 && types.at(type->second.data[0]).op == 22 &&
                     types.at(type->second.data[0]).data == std::vector<unsigned>{32})
                kind = ShaderBindingKind::Texture;
            else if (v.second.storage == 0 && type->second.op == 26)
                kind = ShaderBindingKind::Sampler;
            else
                return fail("shader_resource_type_not_supported");
            const unsigned shift = kind == ShaderBindingKind::Constants ? 0
                                   : kind == ShaderBindingKind::Texture ? 4
                                                                        : 4 + GraphicsTextureSlots;
            const ShaderBinding* found = nullptr;
            for (const auto& b : desc.bindings)
                if (b.kind == kind && b.slot + shift == binding->second &&
                    (b.stages & unsigned(source.stage)))
                    found = &b;
            if (!found)
                return fail("shader_reflection_binding_mismatch");
            if (kind == ShaderBindingKind::Constants && size(pointee, 0) != found->bytes)
                return fail("shader_reflection_constant_size_mismatch");
            if (kind == ShaderBindingKind::Constants)
                for (const auto& member : found->members) {
                    unsigned index = unsigned(type->second.data.size());
                    for (unsigned n = 0; n < type->second.data.size(); ++n)
                        if (memberNames[{pointee, n}] == member.name)
                            index = n;
                    if (index == type->second.data.size() ||
                        offsets[{pointee, index}] != member.offset)
                        return fail("shader_reflection_member_mismatch");
                    auto value = types.at(type->second.data[index]);
                    unsigned elements = 1;
                    if (value.op == 28 && value.data.size() == 2) {
                        elements = constants[value.data[1]];
                        value = types.at(value.data[0]);
                    }
                    const bool matrix = member.type == ShaderValueType::Matrix4x4;
                    // DXC represents an HLSL row-major matrix as a transposed
                    // SPIR-V matrix with ColMajor storage, stride 16.
                    if (matrix && (memberDecorations[{pointee, index, 5}] != 1 ||
                                   memberDecorations[{pointee, index, 7}] != 16))
                        return fail("shader_reflection_matrix_layout_mismatch");
                    if (elements != member.count ||
                        (matrix &&
                         (value.op != 24 || value.data.size() != 2 || value.data[1] != 4)))
                        return fail("shader_reflection_member_type_mismatch");
                    if (matrix)
                        value = types.at(value.data[0]);
                    const unsigned columns = member.type == ShaderValueType::Float2   ? 2
                                             : member.type == ShaderValueType::Float3 ? 3
                                             : (member.type == ShaderValueType::Float4 || matrix)
                                                 ? 4
                                                 : 1;
                    if (columns > 1) {
                        if (value.op != 23 || value.data.size() != 2 || value.data[1] != columns)
                            return fail("shader_reflection_member_type_mismatch");
                        value = types.at(value.data[0]);
                    }
                    const bool integer =
                        member.type == ShaderValueType::Int || member.type == ShaderValueType::UInt;
                    if (value.op != (integer ? 21u : 22u) || value.data.empty() ||
                        value.data[0] != 32 ||
                        (integer &&
                         (value.data.size() != 2 ||
                          value.data[1] != (member.type == ShaderValueType::Int ? 1u : 0u))))
                        return fail("shader_reflection_member_type_mismatch");
                }
        } else if (v.second.storage == 1 && source.stage == ShaderStage::Vertex) {
            const auto location = decorations.find({v.first, 30});
            if (location == decorations.end())
                continue;
            const ShaderVertexInput* found = nullptr;
            for (const auto& i : desc.inputs)
                if (i.location == location->second)
                    found = &i;
            const auto components = found ? (found->format == ShaderVertexFormat::Float2   ? 2u
                                             : found->format == ShaderVertexFormat::Float3 ? 3u
                                                                                           : 4u)
                                          : 0;
            if (!found || type->second.op != 23 || type->second.data.size() != 2 ||
                type->second.data[1] != components || types.at(type->second.data[0]).op != 22 ||
                types.at(type->second.data[0]).data != std::vector<unsigned>{32})
                return fail("shader_reflection_vertex_input_mismatch");
        } else if (v.second.storage == 3 && source.stage == ShaderStage::Pixel) {
            const auto location = decorations.find({v.first, 30});
            if (location == decorations.end() || location->second >= desc.colorOutputs.size() ||
                !pixelOutputs.insert(location->second).second)
                return fail("shader_output_layout_mismatch");
            auto t = type->second;
            unsigned components = 1;
            if (t.op == 23 && t.data.size() == 2) {
                components = t.data[1];
                t = types.at(t.data[0]);
            }
            if (t.op != 22 || t.data != std::vector<unsigned>{32} ||
                components != desc.colorOutputs[location->second])
                return fail("shader_output_layout_mismatch");
        }
    }
    if (source.stage == ShaderStage::Pixel && pixelOutputs.size() != desc.colorOutputs.size())
        return fail("shader_output_layout_mismatch");
    if (source.stage == ShaderStage::Vertex && !vertexPosition)
        return fail("shader_vertex_position_missing");
    return true;
} catch (const std::out_of_range&) {
    error = "invalid_spirv_type_reference";
    return false;
}
}
