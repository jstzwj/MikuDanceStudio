#pragma once
#include "mikudancestudio/rhi.hpp"
#include "mikudancestudio/rhi_material_textures.hpp"
#include <functional>
#include "mikudancestudio/rhi_swapchain.hpp"

struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
namespace mikudancestudio::rhi {
#ifdef MIKUDANCESTUDIO_RHI_D3D9
std::unique_ptr<Device> CreateD3D9Device(IDirect3DDevice9* device);
// Device must wrap this same native device and outlive the controller.
std::unique_ptr<Swapchain> CreateD3D9Swapchain(Device&, IDirect3DDevice9*);
// Optional legacy numerical compatibility; DLL absence returns no capability.
std::unique_ptr<TexturePreparer> CreateD3D9TexturePreparer(IDirect3DDevice9* device);
#endif
// Borrow an already active D3D9 scene for one line batch. The callback keeps
// host draw interception (MME) in the application; the adapter owns the
// vertex buffer and restores all device state before returning.
using InSceneLineDraw = std::function<long(IDirect3DDevice9*, unsigned, unsigned)>;
using InSceneIndexedDraw = std::function<long(IDirect3DDevice9*, unsigned, unsigned, unsigned)>;
enum class OverlayAtlasSlot {
    Sprite,
    Text
};
struct OverlayCacheStats {
    std::uint64_t uploads = 0;
    std::uint64_t hits = 0;
};
class BorrowedOverlayCache {
public:
    virtual ~BorrowedOverlayCache() = default;
    // A matching revision may omit pixels. A new revision must supply RGBA8.
    virtual bool Draw(OverlayAtlasSlot slot, std::uint64_t revision, TextureDesc texture,
                      TexturePixels pixels, const void* vertices, std::size_t bytes,
                      SamplerDesc sampler, InSceneLineDraw draw, long* drawResult,
                      bool readOnlyDepthStencil = false) = 0;
    virtual OverlayCacheStats Stats(OverlayAtlasSlot slot) const = 0;
};
class BorrowedModelTextureCache {
public:
    virtual ~BorrowedModelTextureCache() = default;
    // Only non-null entries replace a native binding. Disabled/stale stages
    // stay borrowed; all external bindings restore before returning.
    virtual bool DrawMaterial(const std::array<const MaterialTextureAsset*, 3>& textures,
                              const void* vertices, std::size_t vertexBytes, const void* indices,
                              std::size_t indexBytes, std::uint32_t first, std::uint32_t count,
                              BatchRasterState raster, InSceneIndexedDraw draw, long* result,
                              bool readOnly = false, bool* issued = nullptr,
                              const MaterialShaderInputs* shaderInputs = nullptr) = 0;
    bool Draw(const MaterialTextureAsset& toon, const void* vertices, std::size_t vertexBytes,
              const void* indices, std::size_t indexBytes, std::uint32_t first, std::uint32_t count,
              BatchRasterState raster, InSceneIndexedDraw draw, long* result, bool readOnly = false,
              bool* issued = nullptr) {
        return DrawMaterial({&toon, nullptr, nullptr}, vertices, vertexBytes, indices, indexBytes,
                            first, count, raster, std::move(draw), result, readOnly, issued);
    }
    virtual OverlayCacheStats Stats() const = 0;
};
#ifdef MIKUDANCESTUDIO_RHI_D3D9
std::unique_ptr<BorrowedModelTextureCache> CreateBorrowedModelTextureCache(IDirect3DDevice9*);

// Only XYZ/normal/UV and uint16 indices are accepted in the first bridge.
// Textures, lights, transforms and material constants are borrowed. The
// adapter owns its mesh buffers and restores all captured device state.
bool DrawModelBatchInScene(IDirect3DDevice9* device, const void* vertices, std::size_t vertexBytes,
                           const void* indices, std::size_t indexBytes, std::uint32_t firstIndex,
                           std::uint32_t indexCount, BatchRasterState raster,
                           InSceneIndexedDraw draw, long* drawResult,
                           bool readOnlyDepthStencil = false, bool* drawIssued = nullptr);

std::unique_ptr<BorrowedOverlayCache> CreateBorrowedOverlayCache(IDirect3DDevice9* device);
void ForgetApplicationOverlayCache(IDirect3DDevice9* device);
OverlayCacheStats ApplicationOverlayCacheStats(IDirect3DDevice9* device, OverlayAtlasSlot slot);
bool DrawCachedHudSpriteInScene(IDirect3DDevice9* device, OverlayAtlasSlot slot,
                                std::uint64_t revision, TextureDesc texture, TexturePixels pixels,
                                const void* vertices, std::size_t bytes, SamplerDesc sampler,
                                InSceneLineDraw draw, long* drawResult,
                                bool readOnlyDepthStencil = false);
bool DrawSelectionBoxInScene(IDirect3DDevice9* device, const void* vertices, std::size_t bytes,
                             InSceneLineDraw draw, long* drawResult,
                             bool readOnlyDepthStencil = false);
bool DrawHudSpriteInScene(IDirect3DDevice9* device, TextureDesc texture, TexturePixels pixels,
                          const void* vertices, std::size_t bytes, SamplerDesc sampler,
                          InSceneLineDraw draw, long* drawResult,
                          bool readOnlyDepthStencil = false);
#else
inline std::unique_ptr<TexturePreparer> CreateD3D9TexturePreparer(IDirect3DDevice9*) {
    return {};
}
inline std::unique_ptr<BorrowedModelTextureCache>
CreateBorrowedModelTextureCache(IDirect3DDevice9*) {
    return {};
}
inline bool DrawModelBatchInScene(IDirect3DDevice9*, const void*, std::size_t, const void*,
                                  std::size_t, std::uint32_t, std::uint32_t, BatchRasterState,
                                  InSceneIndexedDraw, long* result, bool = false,
                                  bool* issued = nullptr) {
    if (result)
        *result = -1;
    if (issued)
        *issued = false;
    return false;
}
inline std::unique_ptr<BorrowedOverlayCache> CreateBorrowedOverlayCache(IDirect3DDevice9*) {
    return {};
}
inline void ForgetApplicationOverlayCache(IDirect3DDevice9*) {
}
inline OverlayCacheStats ApplicationOverlayCacheStats(IDirect3DDevice9*, OverlayAtlasSlot) {
    return {};
}
inline bool DrawCachedHudSpriteInScene(IDirect3DDevice9*, OverlayAtlasSlot, std::uint64_t,
                                       TextureDesc, TexturePixels, const void*, std::size_t,
                                       SamplerDesc, InSceneLineDraw, long* drawResult,
                                       bool = false) {
    if (drawResult)
        *drawResult = -1;
    return false;
}
inline bool DrawSelectionBoxInScene(IDirect3DDevice9*, const void*, std::size_t, InSceneLineDraw,
                                    long* drawResult, bool = false) {
    if (drawResult)
        *drawResult = -1;
    return false;
}
inline bool DrawHudSpriteInScene(IDirect3DDevice9*, TextureDesc, TexturePixels, const void*,
                                 std::size_t, SamplerDesc, InSceneLineDraw, long* drawResult,
                                 bool = false) {
    if (drawResult)
        *drawResult = -1;
    return false;
}
#endif
}
