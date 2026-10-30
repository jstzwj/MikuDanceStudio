#pragma once
#include "sas_exec.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

// A deterministic native inventory: identities are local ordinal numbers,
// never addresses. Read live FX pool values, not each SAS record's old texture.
class MmeRequirementsReport {
    std::map<IUnknown*, unsigned> identities;
    std::ostringstream rows;
    unsigned maxSamplers = 0;
    unsigned Identity(IUnknown* object) {
        if (!object) return 0;
        IUnknown* canonical = nullptr;
        if (FAILED(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical))))
            throw std::runtime_error("native inventory identity");
        const auto found = identities.emplace(canonical, unsigned(identities.size()) + 1);
        canonical->Release();
        return found.first->second;
    }
public:
    void Effect(mme::SasEffect& sas) {
        ID3DXEffectPool* pool = nullptr;
        sas.effect->GetPool(&pool);
        rows << "effect pool=" << Identity(pool) << " class=" << int(sas.scriptClass)
             << " order=" << int(sas.scriptOrder) << '\n';
        if (pool) pool->Release();
        D3DXEFFECT_DESC effect{};
        sas.effect->GetDesc(&effect);
        for (unsigned n = 0; n < effect.Parameters; ++n) {
            const auto handle = sas.effect->GetParameter(nullptr, n);
            D3DXPARAMETER_DESC parameter{};
            sas.effect->GetParameterDesc(handle, &parameter);
            if (parameter.Type < D3DXPT_TEXTURE || parameter.Type > D3DXPT_TEXTURECUBE)
                continue;
            IDirect3DBaseTexture9* texture = nullptr;
            sas.effect->GetTexture(handle, &texture);
            rows << "texture " << parameter.Name << " shared="
                 << bool(parameter.Flags & D3DX_PARAMETER_SHARED) << " identity="
                 << Identity(texture) << '\n';
            if (texture) texture->Release();
        }
        for (const auto& resource : sas.resources) {
            D3DXPARAMETER_DESC parameter{};
            sas.effect->GetParameterDesc(resource.param, &parameter);
            IDirect3DBaseTexture9* texture = nullptr;
            sas.effect->GetTexture(resource.param, &texture);
            rows << "resource " << resource.name << " semantic=" << resource.semanticId
                 << " shared=" << bool(parameter.Flags & D3DX_PARAMETER_SHARED)
                 << " identity=" << Identity(texture) << " requested=" << resource.reqWidth
                 << 'x' << resource.reqHeight << " format=" << unsigned(resource.format)
                 << " levels=" << resource.texMips << " ratio=" << resource.hasViewportRatio;
            if (texture && texture->GetType() == D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc{};
                static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &desc);
                rows << " actual=" << desc.Width << 'x' << desc.Height
                     << " actualFormat=" << unsigned(desc.Format)
                     << " actualLevels=" << texture->GetLevelCount();
            }
            rows << '\n';
            if (texture) texture->Release();
            for (unsigned row = 0; row < resource.defaultEffectMap.size(); ++row)
                rows << "child " << resource.name << " row=" << row << " selector="
                     << resource.defaultEffectMap[row].first << '\n';
        }
        for (auto handle : sas.techniqueOrder) {
            const auto index = mme::SasFindTechniqueIndex(&sas, handle);
            if (index < 0) throw std::runtime_error("native inventory selection");
            const auto& technique = sas.techniques[index];
            rows << "selected " << technique.name << " hardware=" << technique.hardwareOk
                 << " shaderMix=" << technique.shaderMixOk << '\n';
            for (unsigned pc = 0; pc < technique.cmds.size(); ++pc) {
                const auto& command = technique.cmds[pc];
                D3DXPARAMETER_DESC desc{};
                if (command.param && command.id != mme::kSasCmdPass)
                    sas.effect->GetParameterDesc(command.param, &desc);
                rows << "command " << pc << " id=" << command.id << " value=" << command.value
                     << " parameter=" << (desc.Name ? desc.Name : "") << '\n';
            }
            for (const auto& pass : technique.passes) {
                D3DXPASS_DESC desc{};
                if (FAILED(sas.effect->GetPassDesc(pass.handle, &desc)))
                    throw std::runtime_error("native inventory pass");
                rows << "pass " << technique.name << '/' << pass.name << '\n';
                for (bool pixel : {false, true}) {
                    const auto* shader = pixel ? desc.pPixelShaderFunction : desc.pVertexShaderFunction;
                    if (!shader) continue;
                    using ShaderSize = UINT (WINAPI*)(const DWORD*);
                    const auto shaderSize = reinterpret_cast<ShaderSize>(GetProcAddress(
#ifdef _WIN64
                        GetModuleHandleW(L"d3dx9_43.dll"), "D3DXGetShaderSize"));
#else
                        GetModuleHandleW(L"d3dx9_32.dll"), "D3DXGetShaderSize"));
#endif
                    if (!shaderSize) throw std::runtime_error("native inventory shader size");
                    const auto bytes = shaderSize(shader);
                    unsigned count = 0;
                    for (unsigned token = 1; token < bytes / 4;) {
                        const auto op = shader[token];
                        if ((op & 65535) == D3DSIO_END) break;
                        if ((op & 65535) != D3DSIO_COMMENT) {
                            token += 1 + ((op >> 24) & 15);
                            continue;
                        }
                        const auto words = (op >> 16) & 32767;
                        if (words > bytes / 4 - token - 1)
                            throw std::runtime_error("native inventory shader comment");
                        if (words > 8 && shader[token + 1] == 0x42415443) {
                            const auto* base = reinterpret_cast<const unsigned char*>(shader + token + 2);
                            const unsigned length = (words - 1) * 4;
                            const auto u32 = [&](unsigned offset) {
                                if (offset > length || length - offset < 4)
                                    throw std::runtime_error("native inventory CTAB offset");
                                std::uint32_t value; std::memcpy(&value, base + offset, 4); return value;
                            };
                            const auto constants = u32(12), entries = u32(16);
                            if (constants > length / 20) throw std::runtime_error("native inventory CTAB count");
                            for (unsigned n = 0; n < constants; ++n) {
                                const auto entry = entries + n * 20, name = u32(entry), reg = u32(entry + 4);
                                if ((reg & 65535) != 3) continue; // D3DXRS_SAMPLER
                                const auto slot = reg >> 16, slots = u32(entry + 8) & 65535;
                                if (name >= length || !std::memchr(base + name, 0, length - name))
                                    throw std::runtime_error("native inventory CTAB name");
                                ++count;
                                rows << "binding " << (pixel ? "ps" : "vs") << " sampler="
                                     << reinterpret_cast<const char*>(base + name) << " slot=" << slot
                                     << " count=" << slots << '\n';
                                if (pixel) maxSamplers = (std::max)(maxSamplers, slot + slots);
                            }
                        }
                        token += words + 1;
                    }
                    rows << "samplers " << (pixel ? "ps" : "vs") << " active=" << count << '\n';
                }
            }
        }
    }
    void Executed(const mme::SasEffect& sas, int pass) {
        rows << "executed pass=" << pass << " resources=" << sas.resources.size() << '\n';
    }
    void Write(const char* path) {
        std::ofstream file(path, std::ios::binary);
        file << "MME native requirements v1\nmaxPixelSamplerSlots=" << maxSamplers << '\n'
             << rows.str();
        if (!file) throw std::runtime_error("native inventory write");
    }
};

