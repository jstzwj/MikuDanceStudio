#include "mikudancestudio/rhi_d3d9.hpp"
#include "mikudancestudio/rhi_d3d9_mip_capture.hpp"
#define NOMINMAX
#include <d3d9.h>
#include <vector>

namespace mikudancestudio::rhi {
namespace {
using LoadSurface = HRESULT(WINAPI*)(IDirect3DSurface9*, const void*, const RECT*, const void*,
                                     D3DFORMAT, UINT, const void*, const RECT*, DWORD, D3DCOLOR);
class LegacyPreparer final : public TexturePreparer {
    IDirect3DDevice9* device_;
    HMODULE library_;
    LoadSurface load_;

public:
    LegacyPreparer(IDirect3DDevice9* device, HMODULE library, LoadSurface load)
        : device_(device), library_(library), load_(load) {
        device_->AddRef();
    }
    ~LegacyPreparer() override {
        device_->Release();
        FreeLibrary(library_);
    }
    bool SupportsMipPolicy(MipPreparationPolicy policy) const override {
        D3DCAPS9 caps{};
        return policy == MipPreparationPolicy::Dx9AutoLinear &&
               SUCCEEDED(device_->GetDeviceCaps(&caps)) &&
               caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) &&
               (caps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0;
    }
    bool PrepareMips(const RgbaImage& source, MipPreparationPolicy policy, PreparedMipChain& output,
                     bool sceneActive) override {
        output = {};
        if (!SupportsMipPolicy(policy) || !source.width || !source.height || source.width > 4096 ||
            source.height > 4096 || (source.width & (source.width - 1)) ||
            (source.height & (source.height - 1)) ||
            source.pixels.size() != std::size_t(source.width) * source.height * 4 ||
            FAILED(device_->TestCooperativeLevel()))
            return false;
        IDirect3D9* api = nullptr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DADAPTER_IDENTIFIER9 adapter{};
        if (FAILED(device_->GetCreationParameters(&creation)) || FAILED(device_->GetDirect3D(&api)))
            return false;
        const bool identified =
            SUCCEEDED(api->GetAdapterIdentifier(creation.AdapterOrdinal, 0, &adapter));
        api->Release();
        if (!identified)
            return false;
        // Numerical provider identity survives Reset/recreation on the same
        // adapter/driver; it is not the transient RHI device identity or epoch.
        std::uint64_t provider = 14695981039346656037ull;
        const auto hash = [&](std::uint64_t v) {
            for (unsigned i = 0; i < 8; ++i) {
                provider ^= (v >> (i * 8)) & 255;
                provider *= 1099511628211ull;
            }
        };
        hash(1);
        hash(adapter.VendorId);
        hash(adapter.DeviceId);
        hash(adapter.DriverVersion.QuadPart);
        hash(creation.DeviceType);
        if (!provider)
            provider = 1;
        IDirect3DTexture9* texture = nullptr;
        if (FAILED(device_->CreateTexture(source.width, source.height, 0, D3DUSAGE_AUTOGENMIPMAP,
                                          D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
            return false;
        bool ready = SUCCEEDED(texture->SetAutoGenFilterType(D3DTEXF_LINEAR));
        D3DLOCKED_RECT lock{};
        ready = ready && SUCCEEDED(texture->LockRect(0, &lock, nullptr, 0));
        if (ready) {
            for (unsigned y = 0; y < source.height; ++y)
                for (unsigned x = 0; x < source.width; ++x) {
                    const auto* p = source.pixels.data() + (std::size_t(y) * source.width + x) * 4;
                    auto* q = static_cast<std::uint8_t*>(lock.pBits) + std::size_t(y) * lock.Pitch +
                              x * 4;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    q[3] = p[3];
                }
            ready = SUCCEEDED(texture->UnlockRect(0));
        }
        ModelTextureData data;
        if (ready) {
            texture->GenerateMipSubLevels();
            ready = SUCCEEDED(CaptureD3D9GeneratedMips(device_, texture, sceneActive, data));
        }
        texture->Release();
        return ready &&
               SealPreparedMipChain(source, policy, provider, std::move(data.levels), output);
    }
    bool Prepare(const RgbaImage& source, std::uint32_t width, std::uint32_t height,
                 RgbaImage& prepared) override {
        if (&source == &prepared) {
            const auto copy = source;
            return Prepare(copy, width, height, prepared);
        }
        prepared = {};
        if (!source.width || !source.height || source.width > 4096 || source.height > 4096 ||
            !width || !height || width > 4096 || height > 4096 ||
            source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4 ||
            FAILED(device_->TestCooperativeLevel()))
            return false;
        D3DCAPS9 caps{};
        if (FAILED(device_->GetDeviceCaps(&caps)) || width > caps.MaxTextureWidth ||
            height > caps.MaxTextureHeight)
            return false;
        std::vector<std::uint8_t> bgra(source.pixels.size());
        for (std::size_t i = 0; i < bgra.size(); i += 4) {
            bgra[i] = source.pixels[i + 2];
            bgra[i + 1] = source.pixels[i + 1];
            bgra[i + 2] = source.pixels[i];
            bgra[i + 3] = source.pixels[i + 3];
        }
        IDirect3DTexture9* texture = nullptr;
        // Scratch native texture is never exposed as an RHI handle or borrowed
        // from the host. Normal cache upload owns the final GPU texture.
        if (FAILED(device_->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                          &texture, nullptr)))
            return false;
        IDirect3DSurface9* surface = nullptr;
        const RECT sourceRect{0, 0, static_cast<LONG>(source.width),
                              static_cast<LONG>(source.height)};
        const bool loaded = SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
                            SUCCEEDED(load_(surface, nullptr, nullptr, bgra.data(), D3DFMT_A8R8G8B8,
                                            source.width * 4, nullptr, &sourceRect, 0x80004u, 0));
        if (surface)
            surface->Release();
        D3DLOCKED_RECT lock{};
        bool ready = loaded && SUCCEEDED(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY));
        if (ready) {
            prepared.width = width;
            prepared.height = height;
            prepared.pixels.resize(static_cast<std::size_t>(width) * height * 4);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const auto* p =
                        static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                    auto* q =
                        prepared.pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    q[3] = p[3];
                }
            ready = SUCCEEDED(texture->UnlockRect(0));
        }
        texture->Release();
        if (!ready)
            prepared = {};
        return ready;
    }
};
}
std::unique_ptr<TexturePreparer> CreateD3D9TexturePreparer(IDirect3DDevice9* device) {
    if (!device)
        return {};
    // Load only the installed system library, never a scene-local DLL.
    const auto library = LoadLibraryExW(L"d3dx9_43.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto load =
        library
            ? reinterpret_cast<LoadSurface>(GetProcAddress(library, "D3DXLoadSurfaceFromMemory"))
            : nullptr;
    if (!load) {
        if (library)
            FreeLibrary(library);
        return {};
    }
    return std::make_unique<LegacyPreparer>(device, library, load);
}
}
