#pragma once

#include "d3d9_device_fixture.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Both engines receive this same forwarding device. Only public D3D9 state
// and issued geometry are recorded; no native engine structures are decoded.
struct MmeRecordingDevice : SnapshotDeviceFixture {
    using SnapshotDeviceFixture::SnapshotDeviceFixture;
    // Six geometry fields, viewport, six render states, UP kind/stride, then
    // 32 float4 registers per stage (272 DWORDs per draw). External packages
    // leave those registers zero and record shader-read constants in .passes.
    std::vector<std::array<DWORD, 272>> draws;
    bool capturePasses = false;
    std::vector<std::string> passes;
    struct PassTarget {
        bool present = false;
        D3DSURFACE_DESC desc{};
        std::uint64_t hash = 0;
        bool operator==(const PassTarget& other) const {
            return present == other.present &&
                   (!present ||
                    (desc.Width == other.desc.Width && desc.Height == other.desc.Height &&
                     desc.Format == other.desc.Format &&
                     desc.MultiSampleType == other.desc.MultiSampleType && hash == other.hash));
        }
    };
    using PassTargets = std::array<PassTarget, 4>;
    std::vector<PassTargets> passTargets;
    std::string captureContext;
    unsigned readbacks = 0;
    unsigned screenDraws = 0;
    IDirect3DTexture9* offscreen = nullptr; // borrowed until assignment/reset
    UINT offscreenSize = 0;
    void InitializeTarget(IDirect3DSurface9* surface, UINT width, UINT height) {
        if (!capturePasses)
            return;
        // Fresh D3D9 allocations have unspecified contents. Give both engines
        // the same initial GPU memory, including pixels outside model coverage.
        if (SUCCEEDED(real->ColorFill(surface, nullptr, 0)))
            return;
        std::array<Owned<IDirect3DSurface9>, 4> old;
        Owned<IDirect3DSurface9> depth;
        D3DVIEWPORT9 viewport{};
        real->GetViewport(&viewport);
        real->GetDepthStencilSurface(&depth.p);
        for (DWORD i = 0; i < old.size(); ++i)
            real->GetRenderTarget(i, &old[i].p);
        for (DWORD i = 1; i < old.size(); ++i)
            real->SetRenderTarget(i, nullptr);
        real->SetDepthStencilSurface(nullptr);
        const D3DVIEWPORT9 full{0, 0, width, height, 0, 1};
        HRESULT result = real->SetRenderTarget(0, surface);
        if (SUCCEEDED(result))
            result = real->SetViewport(&full);
        if (SUCCEEDED(result))
            result = real->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
        for (DWORD i = 0; i < old.size(); ++i)
            real->SetRenderTarget(i, old[i].p);
        real->SetDepthStencilSurface(depth.p);
        real->SetViewport(&viewport);
        if (FAILED(result))
            throw std::runtime_error("initialize oracle target contents");
    }
    template <class T> struct Owned {
        T* p = nullptr;
        ~Owned() {
            if (p)
                p->Release();
        }
    };
    static std::uint64_t Hash(const void* data, std::size_t size,
                              std::uint64_t hash = 14695981039346656037ull) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        // Mix complete words for large GPU surfaces, identically on x86/x64.
        std::size_t i = 0;
        for (; i + sizeof(DWORD) <= size; i += sizeof(DWORD)) {
            DWORD word;
            std::memcpy(&word, bytes + i, sizeof word);
            hash = (hash ^ word) * 1099511628211ull;
        }
        for (; i < size; ++i)
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        return hash;
    }
    template <class T> static std::uint64_t ShaderHash(T* shader) {
        if (!shader)
            return 0;
        UINT size = 0;
        if (FAILED(shader->GetFunction(nullptr, &size)))
            throw std::runtime_error("MME shader bytecode size");
        std::vector<unsigned char> code(size);
        if (FAILED(shader->GetFunction(code.data(), &size)))
            throw std::runtime_error("MME shader bytecode");
        return Hash(code.data(), code.size());
    }
    static std::array<bool, 16> PixelSamplers(IDirect3DPixelShader9* shader) {
        std::array<bool, 16> used{};
        if (!shader)
            return used;
        UINT bytes = 0;
        if (FAILED(shader->GetFunction(nullptr, &bytes)))
            throw std::runtime_error("MME sampler shader size");
        std::vector<DWORD> code(bytes / sizeof(DWORD));
        if (FAILED(shader->GetFunction(code.data(), &bytes)))
            throw std::runtime_error("MME sampler shader tokens");
        for (std::size_t i = 1; i < code.size();) {
            const DWORD opcode = code[i] & 0xFFFF;
            if (opcode == 0xFFFF)
                break;
            const auto length = opcode == 0xFFFE ? (code[i] >> 16 & 0x7FFF) : (code[i] >> 24 & 15);
            if (i + length >= code.size())
                throw std::runtime_error("MME sampler instruction length");
            if (opcode != 0xFFFE)
                for (std::size_t j = 1; j <= length; ++j) {
                    const DWORD token = code[i + j];
                    const DWORD type = (token >> 28 & 7) | (token >> 8 & 0x18);
                    const DWORD index = token & 0x7FF;
                    if ((token & 0x80000000) && type == D3DSPR_SAMPLER && index < used.size())
                        used[index] = true;
                }
            i += length + 1;
        }
        return used;
    }
    template <class T> void ShaderConstants(std::ostream& out, T* shader, bool pixel) {
        out << '{';
        if (!shader) {
            out << '}';
            return;
        }
        UINT size = 0;
        shader->GetFunction(nullptr, &size);
        std::vector<DWORD> code(size / sizeof(DWORD));
        if (FAILED(shader->GetFunction(code.data(), &size)))
            throw std::runtime_error("MME shader constants bytecode");
        if (code.empty() || (code[0] >> 8 & 255) < 2 || (code[0] >> 8 & 255) > 3)
            throw std::runtime_error("MME pass capture requires shader model 2 or 3");
        std::array<bool, 256> used{}, defined{};
        // SM2/3 instruction lengths exclude the instruction token. COMMENT
        // lengths use bits 16..30. Decode register tokens, excluding DEF data.
        for (std::size_t i = 1; i < code.size();) {
            const DWORD opcode = code[i] & 0xFFFF;
            if (opcode == 0xFFFF)
                break;
            const auto length = opcode == 0xFFFE ? (code[i] >> 16 & 0x7FFF) : (code[i] >> 24 & 15);
            if (i + length >= code.size())
                throw std::runtime_error("MME shader instruction length");
            if (opcode == 81 && length > 0) // DEF embeds a local float4.
                defined[code[i + 1] & 255] = true;
            else if (opcode != 0xFFFE && opcode != 47 && opcode != 48) {
                for (std::size_t j = 1; j <= length; ++j) {
                    const DWORD token = code[i + j];
                    const DWORD type = (token >> 28 & 7) | (token >> 8 & 0x18);
                    const DWORD index = token & 0x7FF;
                    if (!(token & 0x80000000) || type != 2 || index >= used.size())
                        continue;
                    unsigned count = 1;
                    if (token & 0x2000) // Relative indexing can consume the entire array.
                        count = static_cast<unsigned>(used.size() - index);
                    else if (j == 3 && opcode >= 20 && opcode <= 24) {
                        constexpr unsigned rows[] = {4, 3, 4, 3, 2};
                        count = rows[opcode - 20];
                    }
                    for (unsigned k = 0; k < count && index + k < used.size(); ++k)
                        used[index + k] = true;
                }
            }
            i += length + 1;
        }
        std::array<float, 1024> constants{};
        const UINT count = pixel ? 224 : 256;
        const HRESULT result = pixel ? real->GetPixelShaderConstantF(0, constants.data(), count)
                                     : real->GetVertexShaderConstantF(0, constants.data(), count);
        if (FAILED(result))
            throw std::runtime_error("MME full shader constants");
        bool first = true;
        for (UINT i = 0; i < count; ++i) {
            if (!used[i] || defined[i])
                continue;
            if (!first)
                out << ',';
            first = false;
            std::array<DWORD, 4> words{};
            std::memcpy(words.data(), constants.data() + i * 4, sizeof words);
            out << '"' << i << "\":[" << words[0] << ',' << words[1] << ',' << words[2] << ','
                << words[3] << ']';
        }
        out << '}';
    }
    static void Description(std::ostream& out, const D3DSURFACE_DESC& d) {
        out << "[" << d.Width << ',' << d.Height << ',' << static_cast<unsigned>(d.Format) << ','
            << d.MultiSampleType << ']';
    }
    static UINT PixelBytes(D3DFORMAT format) {
        switch (format) {
        case D3DFMT_A8:
        case D3DFMT_L8:
            return 1;
        case D3DFMT_R16F:
            return 2;
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_R32F:
        case D3DFMT_G16R16F:
            return 4;
        case D3DFMT_A16B16G16R16F:
        case D3DFMT_G32R32F:
            return 8;
        case D3DFMT_A32B32G32R32F:
            return 16;
        default:
            return 0;
        }
    }
    static std::uint64_t HashRows(const void* data, INT pitch, std::size_t rowBytes, UINT height) {
        const auto stride = static_cast<std::ptrdiff_t>(pitch);
        const auto magnitude = pitch < 0 ? -static_cast<std::int64_t>(pitch) : pitch;
        if (!data || static_cast<std::uint64_t>(magnitude) < rowBytes)
            throw std::runtime_error("invalid MME target readback pitch");
        auto hash = std::uint64_t{14695981039346656037ull};
        for (UINT y = 0; y < height; ++y)
            hash = Hash(static_cast<const char*>(data) + static_cast<std::ptrdiff_t>(y) * stride,
                        rowBytes, hash);
        return hash;
    }
    [[noreturn]] void ReadbackFailure(const char* operation, DWORD index,
                                      const D3DSURFACE_DESC& desc, HRESULT result) const {
        std::ostringstream message;
        message << "MME pass capture: stage=" << captureContext << " draw=" << passes.size()
                << " target=" << index << " desc=";
        Description(message, desc);
        message << " operation=" << operation << " HRESULT=0x" << std::hex
                << static_cast<unsigned>(result);
        throw std::runtime_error(message.str());
    }
    PassTarget ReadTarget(DWORD index, IDirect3DSurface9* surface) {
        PassTarget target;
        target.present = true;
        auto& desc = target.desc;
        const auto describe = surface->GetDesc(&desc);
        if (FAILED(describe))
            ReadbackFailure("GetDesc", index, desc, describe);
        const auto pixelBytes = PixelBytes(desc.Format);
        if (!pixelBytes)
            ReadbackFailure("unsupported format", index, desc, D3DERR_NOTAVAILABLE);
        Owned<IDirect3DTexture9> texture;
        Owned<IDirect3DSurface9> copy;
        HRESULT result;
        if (desc.Format == D3DFMT_A8 || desc.Format == D3DFMT_L8) {
            // D3D9 plain surfaces reject these formats. A system-memory texture
            // level preserves their raw bytes without a color conversion.
            result = real->CreateTexture(desc.Width, desc.Height, 1, 0, desc.Format,
                                         D3DPOOL_SYSTEMMEM, &texture.p, nullptr);
            if (FAILED(result))
                ReadbackFailure("CreateTexture", index, desc, result);
            result = texture.p->GetSurfaceLevel(0, &copy.p);
            if (FAILED(result))
                ReadbackFailure("GetSurfaceLevel", index, desc, result);
        } else {
            result = real->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                       D3DPOOL_SYSTEMMEM, &copy.p, nullptr);
            if (FAILED(result))
                ReadbackFailure("CreateOffscreenPlainSurface", index, desc, result);
        }
        result = real->GetRenderTargetData(surface, copy.p);
        if (FAILED(result))
            ReadbackFailure("GetRenderTargetData", index, desc, result);
        D3DLOCKED_RECT lock{};
        result = copy.p->LockRect(&lock, nullptr, D3DLOCK_READONLY);
        if (FAILED(result))
            ReadbackFailure("LockRect", index, desc, result);
        try {
            target.hash = HashRows(lock.pBits, lock.Pitch,
                                   static_cast<std::size_t>(desc.Width) * pixelBytes, desc.Height);
        } catch (...) {
            copy.p->UnlockRect();
            throw;
        }
        result = copy.p->UnlockRect();
        if (FAILED(result))
            ReadbackFailure("UnlockRect", index, desc, result);
        ++readbacks;
        return target;
    }
    static void RequireSamePasses(const std::string& stage,
                                  const std::vector<std::string>& original,
                                  const std::vector<std::string>& port,
                                  const std::vector<PassTargets>& originalTargets,
                                  const std::vector<PassTargets>& portTargets) {
        if (originalTargets.size() != original.size() || portTargets.size() != port.size())
            throw std::runtime_error("incomplete MME target capture");
        const auto common = original.size() < port.size() ? original.size() : port.size();
        for (std::size_t i = 0; i < common; ++i) {
            if (original[i] == port[i])
                continue;
            std::ostringstream message;
            message << "MME first pass difference: stage=" << stage << " draw=" << i;
            for (unsigned target = 0; target < 4; ++target) {
                const auto& a = originalTargets[i][target];
                const auto& b = portTargets[i][target];
                if (a == b)
                    continue;
                message << " target=" << target << " original=";
                Description(message, a.desc);
                message << ':' << std::hex << a.hash << std::dec << " port=";
                Description(message, b.desc);
                message << ':' << std::hex << b.hash;
                throw std::runtime_error(message.str());
            }
            message << " shaders/constants/textures/depth/states differ";
            throw std::runtime_error(message.str());
        }
        if (original.size() != port.size()) {
            std::ostringstream message;
            message << "MME first pass difference: stage=" << stage << " draw=" << common
                    << " pass count original=" << original.size() << " port=" << port.size();
            throw std::runtime_error(message.str());
        }
    }
    // Read back each public render target after an actual draw. Store hashes
    // instead of retaining default-pool resources across effect changes/Reset.
    void CapturePass(HRESULT drawResult) {
        if (!capturePasses)
            return;
        if (FAILED(drawResult))
            ReadbackFailure("draw", 0, {}, drawResult);
        PassTargets targets{};
        std::ostringstream out;
        out << "{\"result\":" << static_cast<unsigned>(drawResult) << ",\"targets\":[";
        for (DWORD i = 0; i < 4; ++i) {
            if (i)
                out << ',';
            Owned<IDirect3DSurface9> target;
            const auto result = real->GetRenderTarget(i, &target.p);
            if (result == D3DERR_NOTFOUND) {
                out << "null";
                continue;
            }
            if (FAILED(result) || !target.p)
                ReadbackFailure("GetRenderTarget", i, {}, FAILED(result) ? result : E_POINTER);
            targets[i] = ReadTarget(i, target.p);
            out << "{\"desc\":";
            Description(out, targets[i].desc);
            out << ",\"readback\":0,\"hash\":\"" << std::hex << targets[i].hash << std::dec
                << "\"}";
        }
        Owned<IDirect3DSurface9> depth;
        out << "],\"depth\":";
        if (SUCCEEDED(real->GetDepthStencilSurface(&depth.p))) {
            D3DSURFACE_DESC desc{};
            depth.p->GetDesc(&desc);
            Description(out, desc);
        } else
            out << "null";
        Owned<IDirect3DVertexShader9> vs;
        Owned<IDirect3DPixelShader9> ps;
        real->GetVertexShader(&vs.p);
        real->GetPixelShader(&ps.p);
        out << ",\"vs\":\"" << std::hex << ShaderHash(vs.p) << "\",\"ps\":\"" << ShaderHash(ps.p)
            << std::dec << "\",\"vs_constants\":";
        ShaderConstants(out, vs.p, false);
        out << ",\"ps_constants\":";
        ShaderConstants(out, ps.p, true);
        auto samplers = PixelSamplers(ps.p);
        if (!ps.p) {
            for (DWORD stage = 0; stage < 8; ++stage) {
                DWORD colorOp = D3DTOP_DISABLE;
                real->GetTextureStageState(stage, D3DTSS_COLOROP, &colorOp);
                if (colorOp == D3DTOP_DISABLE)
                    break;
                for (bool alpha : {false, true}) {
                    DWORD op = colorOp;
                    if (alpha)
                        real->GetTextureStageState(stage, D3DTSS_ALPHAOP, &op);
                    if (op == D3DTOP_DISABLE)
                        continue;
                    for (int arg = 0; arg < 3; ++arg) {
                        if ((arg == 0 && op != D3DTOP_MULTIPLYADD && op != D3DTOP_LERP) ||
                            (arg == 1 && op == D3DTOP_SELECTARG2) ||
                            (arg == 2 && op == D3DTOP_SELECTARG1))
                            continue;
                        constexpr D3DTEXTURESTAGESTATETYPE colors[] = {
                            D3DTSS_COLORARG0, D3DTSS_COLORARG1, D3DTSS_COLORARG2};
                        constexpr D3DTEXTURESTAGESTATETYPE alphas[] = {
                            D3DTSS_ALPHAARG0, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2};
                        DWORD value = 0;
                        real->GetTextureStageState(stage, alpha ? alphas[arg] : colors[arg],
                                                   &value);
                        if ((value & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
                            samplers[stage] = true;
                    }
                }
            }
        }
        out << ",\"textures\":[";
        for (DWORD i = 0; i < 16; ++i) {
            if (i)
                out << ',';
            Owned<IDirect3DBaseTexture9> texture;
            // A shader can leave unrelated fixed-function textures bound.
            // Match only sampled inputs, as with shader-read constants above.
            if (samplers[i] && SUCCEEDED(real->GetTexture(i, &texture.p)) && texture.p &&
                texture.p->GetType() == D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc{};
                static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0, &desc);
                Description(out, desc);
            } else
                out << "null";
        }
        out << "],\"states\":{";
        bool firstState = true;
        for (unsigned state = 0; state <= D3DRS_BLENDOPALPHA; ++state) {
            DWORD value = 0;
            if (FAILED(real->GetRenderState(static_cast<D3DRENDERSTATETYPE>(state), &value)))
                continue;
            if (!firstState)
                out << ',';
            firstState = false;
            out << '"' << state << "\":" << value;
        }
        out << "}}";
        passes.push_back(out.str());
        passTargets.push_back(targets);
    }
    void Record(D3DPRIMITIVETYPE type, int base, UINT min, UINT vertices, UINT start, UINT count) {
        std::array<DWORD, 272> row{};
        row[0] = type;
        row[1] = base;
        row[2] = min;
        row[3] = vertices;
        row[4] = start;
        row[5] = count;
        D3DVIEWPORT9 viewport{};
        real->GetViewport(&viewport);
        row[6] = viewport.Width;
        row[7] = viewport.Height;
        real->GetRenderState(D3DRS_ALPHABLENDENABLE, &row[8]);
        real->GetRenderState(D3DRS_SRCBLEND, &row[9]);
        real->GetRenderState(D3DRS_DESTBLEND, &row[10]);
        real->GetRenderState(D3DRS_ZWRITEENABLE, &row[11]);
        real->GetRenderState(D3DRS_ZFUNC, &row[12]);
        real->GetRenderState(D3DRS_CULLMODE, &row[13]);
        std::array<float, 256> constants{};
        // External effects record all shader-read registers in .passes instead.
        // Unused device registers can contain stale values after native Reset.
        if (!capturePasses) {
            real->GetVertexShaderConstantF(0, constants.data(), 32);
            real->GetPixelShaderConstantF(0, constants.data() + 128, 32);
        }
        std::memcpy(row.data() + 16, constants.data(), sizeof constants);
        draws.push_back(row);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start,
                                            UINT count) override {
        Record(type, 0, 0, 0, start, count);
        const auto result = real->DrawPrimitive(type, start, count);
        CapturePass(result);
        return result;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base, UINT min,
                                                   UINT vertices, UINT start, UINT count) override {
        Record(type, base, min, vertices, start, count);
        const auto result = real->DrawIndexedPrimitive(type, base, min, vertices, start, count);
        CapturePass(result);
        return result;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* data,
                                              UINT stride) override {
        Record(type, 0, 0, 0, 0, count);
        draws.back()[14] = 1;
        draws.back()[15] = stride;
        ++screenDraws;
        const auto result = real->DrawPrimitiveUP(type, count, data, stride);
        CapturePass(result);
        return result;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min, UINT vertices,
                                                     UINT count, const void* indices,
                                                     D3DFORMAT format, const void* data,
                                                     UINT stride) override {
        Record(type, static_cast<int>(format), min, vertices, 0, count);
        draws.back()[14] = 2;
        draws.back()[15] = stride;
        ++screenDraws;
        const auto result =
            real->DrawIndexedPrimitiveUP(type, min, vertices, count, indices, format, data, stride);
        CapturePass(result);
        return result;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage,
                                            D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DTexture9** texture, HANDLE* shared) override {
        const HRESULT result =
            real->CreateTexture(width, height, levels, usage, format, pool, texture, shared);
        if (SUCCEEDED(result) && (usage & D3DUSAGE_RENDERTARGET) && capturePasses) {
            Owned<IDirect3DSurface9> surface;
            if (FAILED((*texture)->GetSurfaceLevel(0, &surface.p)))
                throw std::runtime_error("initialize oracle texture surface");
            InitializeTarget(surface.p, width, height);
        }
        if (SUCCEEDED(result) && (usage & D3DUSAGE_RENDERTARGET) && format == D3DFMT_A8R8G8B8 &&
            width == height) {
            offscreen = *texture;
            offscreenSize = width;
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override {
        offscreen = nullptr;
        offscreenSize = 0;
        return real->Reset(pp);
    }
    void SaveDraws(const std::filesystem::path& path) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(draws.data()), draws.size() * sizeof(draws[0]));
        if (!file.good())
            throw std::runtime_error("write MME draw trace");
        draws.clear();
        if (capturePasses) {
            auto passPath = path;
            passPath.replace_extension(".passes");
            std::ofstream passFile(passPath);
            for (const auto& pass : passes)
                passFile << pass << '\n';
            if (!passFile.good())
                throw std::runtime_error("write MME pass trace");
            passes.clear();
            passTargets.clear();
        }
    }
};
