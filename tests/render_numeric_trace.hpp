#pragma once

#include "mme_recording_device.hpp"

// Public D3D9 state immediately before each original/ported draw. Shader-read
// constants exclude stale registers that cannot affect the current program.
struct RenderNumericTrace : MmeRecordingDevice {
    using MmeRecordingDevice::MmeRecordingDevice;
    std::vector<std::array<DWORD, 65>> states;
    std::vector<std::string> constants;
    std::filesystem::path shaderDirectory;
    template <class T> void SaveShader(T* shader, const char* stage) {
        if (!shader || shaderDirectory.empty())
            return;
        const auto path = shaderDirectory / (std::to_string(ShaderHash(shader)) + stage + ".asm");
        if (std::filesystem::exists(path))
            return;
        UINT size = 0;
        shader->GetFunction(nullptr, &size);
        std::vector<DWORD> code(size / sizeof(DWORD));
        if (FAILED(shader->GetFunction(code.data(), &size)))
            throw std::runtime_error("read numeric shader");
        Owned<ID3DXBuffer> assembly;
        using Disassemble = HRESULT(WINAPI*)(const DWORD*, BOOL, const char*, ID3DXBuffer**);
        const auto module =
            GetModuleHandleW(sizeof(void*) == 8 ? L"d3dx9_43.dll" : L"d3dx9_32.dll");
        const auto disassemble =
            reinterpret_cast<Disassemble>(GetProcAddress(module, "D3DXDisassembleShader"));
        if (!disassemble || FAILED(disassemble(code.data(), FALSE, nullptr, &assembly.p)))
            throw std::runtime_error("disassemble numeric shader");
        std::ofstream file(path);
        file.write(static_cast<const char*>(assembly.p->GetBufferPointer()),
                   assembly.p->GetBufferSize());
        if (!file.good())
            throw std::runtime_error("write numeric shader");
    }

    void Begin() {
        draws.clear();
        states.clear();
        constants.clear();
    }
    void Capture() {
        std::array<DWORD, 65> state{};
        D3DMATERIAL9 material{};
        real->GetMaterial(&material);
        static_assert(sizeof material == 17 * sizeof(DWORD), "D3D9 material ABI");
        std::memcpy(state.data(), &material, sizeof material);
        unsigned offset = 17;
        for (auto kind : {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION}) {
            D3DMATRIX matrix{};
            real->GetTransform(kind, &matrix);
            std::memcpy(state.data() + offset, &matrix, sizeof matrix);
            offset += 16;
        }
        states.push_back(state);
        Owned<IDirect3DVertexShader9> vertex;
        Owned<IDirect3DPixelShader9> pixel;
        real->GetVertexShader(&vertex.p);
        real->GetPixelShader(&pixel.p);
        SaveShader(vertex.p, ".vs");
        SaveShader(pixel.p, ".ps");
        std::ostringstream out;
        out << "{\"vs\":" << ShaderHash(vertex.p) << ",\"ps\":" << ShaderHash(pixel.p)
            << ",\"vertex\":";
        ShaderConstants(out, vertex.p, false);
        out << ",\"pixel\":";
        ShaderConstants(out, pixel.p, true);
        DWORD fvf = 0;
        real->GetFVF(&fvf);
        out << ",\"fvf\":" << fvf;
        out << ",\"texture_stages\":[";
        for (DWORD stage = 0; stage < 3; ++stage) {
            if (stage)
                out << ',';
            out << '[';
            for (DWORD kind = 1; kind <= 32; ++kind) {
                DWORD value = 0;
                real->GetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(kind),
                                           &value);
                out << (kind == 1 ? "" : ",") << value;
            }
            out << ']';
        }
        out << ']';
        if (capturePasses && !vertex.p && fvf == (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)) {
            Owned<IDirect3DVertexBuffer9> transformed;
            constexpr DWORD transformedFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
            constexpr UINT transformedStride = 28;
            if (SUCCEEDED(real->CreateVertexBuffer(3 * transformedStride, 0, transformedFvf,
                                                   D3DPOOL_SYSTEMMEM, &transformed.p, nullptr)) &&
                SUCCEEDED(real->ProcessVertices(0, 0, 3, transformed.p, nullptr, 0))) {
                void* bytes = nullptr;
                if (SUCCEEDED(transformed.p->Lock(0, 0, &bytes, D3DLOCK_READONLY))) {
                    DWORD diffuse = 0;
                    std::memcpy(&diffuse, static_cast<const char*>(bytes) + 16, sizeof diffuse);
                    out << ",\"processed_diffuse\":" << diffuse;
                    transformed.p->Unlock();
                }
            }
        }
        if (capturePasses) {
            Owned<IDirect3DVertexBuffer9> buffer;
            UINT offset = 0, stride = 0;
            if (SUCCEEDED(real->GetStreamSource(0, &buffer.p, &offset, &stride)) && buffer.p) {
                D3DVERTEXBUFFER_DESC description{};
                buffer.p->GetDesc(&description);
                void* bytes = nullptr;
                if (FAILED(buffer.p->Lock(0, 0, &bytes, D3DLOCK_READONLY)))
                    throw std::runtime_error("read numeric draw vertices");
                const auto hash = Hash(bytes, description.Size);
                const auto path = shaderDirectory / (std::to_string(hash) + ".vertices");
                if (!shaderDirectory.empty() && !std::filesystem::exists(path)) {
                    std::ofstream file(path, std::ios::binary);
                    file.write(static_cast<const char*>(bytes), description.Size);
                    if (!file.good())
                        throw std::runtime_error("write numeric draw vertices");
                }
                buffer.p->Unlock();
                out << ",\"geometry\":[" << hash << ',' << offset << ',' << stride << ','
                    << description.Size << ']';
            }
        }
        out << '}';
        constants.push_back(out.str());
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start,
                                            UINT count) override {
        Capture();
        return MmeRecordingDevice::DrawPrimitive(type, start, count);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base, UINT min,
                                                   UINT vertices, UINT start, UINT count) override {
        Capture();
        return MmeRecordingDevice::DrawIndexedPrimitive(type, base, min, vertices, start, count);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* data,
                                              UINT stride) override {
        Capture();
        return MmeRecordingDevice::DrawPrimitiveUP(type, count, data, stride);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min, UINT vertices,
                                                     UINT count, const void* indices,
                                                     D3DFORMAT format, const void* data,
                                                     UINT stride) override {
        Capture();
        return MmeRecordingDevice::DrawIndexedPrimitiveUP(type, min, vertices, count, indices,
                                                          format, data, stride);
    }
    void Save(const std::filesystem::path& directory, const std::string& name) {
        if (directory.empty())
            return;
        SaveDraws(directory / (name + ".draws"));
        std::ofstream raw(directory / (name + ".states"), std::ios::binary);
        raw.write(reinterpret_cast<const char*>(states.data()), states.size() * sizeof(states[0]));
        std::ofstream json(directory / (name + ".constants.jsonl"));
        for (const auto& line : constants)
            json << line << '\n';
        if (!raw.good() || !json.good())
            throw std::runtime_error("write numeric render trace");
    }
};
