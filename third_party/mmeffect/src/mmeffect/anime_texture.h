// anime_texture.h - the animated-texture engine (the CAnimeGIF / CAnimePNG
// region 0x180001000-0x180008000) and the ctx+0x48 registry.
//
// Evidence (v0.37 x64 binary, verified 2026-09):
//   - 0x2D ANIMATEDTEXTURE is handled fully INLINE in sub_18000F3A0 case 45
//     (0x1800100C5): ResourceName mode 1 after the literal skip gate, the
//     shared sub_18000EA80 path resolution, a last-4-chars lowercased
//     extension dispatch ".png" -> CAnimePNG (new 0x110, ctor sub_1800068A0)
//     / ".gif" -> CAnimeGIF (new 0xA8, ctor sub_180004810), the ctor's error
//     string appended verbatim to the effect log (sub_180023440), then
//     Offset / Speed / SeekVariable reads whose failures are hard errors,
//     and a parse-time registration of {param, {0x2D,count}, obj, seekParam}
//     into the runtime record vector (sub_18001F090). 0x2D NEVER reaches
//     sub_180011960 / sub_1800143D0 (their switches only carry 0x26/0x27/
//     0x2C/0x2E) - there is no 0x2D branch in sub_1800143D0 and no
//     "case 45"/"case 51" anywhere in it.
//   - CAnimeGIF ctor sub_180004810: GdipLoadImageFromFile, frame table from
//     GdipImageGetFrameCount + PropertyTagFrameDelay(0x5100, value*10 ms,
//     frames beyond the table default to 1/30 s) + PropertyTagLoopCount
//     (0x5101, 0xFFFF normalized to 0 = infinite); the texture is created in
//     the ctor itself: D3DXCreateTexture(dev, w, h, 1, D3DUSAGE_DYNAMIC,
//     D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT).
//   - The frame lookup sub_1800050C0 is ABSOLUTE-TIME driven: pos =
//     (time - offset) * speed; pos < 0 -> first frame; loops = (int)(pos /
//     total); loopCount != 0 && loops >= loopCount -> hold the LAST frame;
//     otherwise lower_bound(pos - loops*total) over the cumulative-start
//     time table. Nothing accumulates between frames.
//   - The per-frame driver is sub_18001B5B0 case 45 (run right after each
//     effect->Begin, before BeginPass): time = GetFloatArray(seekParam, &v,
//     1) when a SeekVariable exists, else the host clock; then obj->SetFrame
//     (vtable+0x10), obj->GetTexture (vtable+0x08) and effect->SetTexture -
//     SetTexture is re-asserted every Begin.
//   - CAnimePNG ctor sub_1800068A0 is a complete APNG chunk parser (fopen
//     "rb", PNG signature, IHDR/acTL/fcTL/fdAT walk). fcTL delay defaults:
//     num==0 -> {1,30}, den==0 -> 100. When 4*numFrames*w*h <= 0xA00000 the
//     frames are PRE-DECODED into per-frame MANAGED textures
//     (D3DXCreateTexture usage 0 pool MANAGED); otherwise one DYNAMIC
//     DEFAULT texture is filled on demand. Each frame decodes through the
//     sub_180007A50 IStream trick: PNG signature + the cached pre-frame
//     chunks (chunk 0 = IHDR patched to the FRAME's w/h, own types kept)
//     + the frame's chunk run re-read from the file (IDAT passthrough,
//     fdAT re-tagged IDAT minus its 4-byte sequence) + IEND, then
//     GdipLoadImageFromStreamICM; the tile is composed onto a persistent
//     base bitmap honoring dispose_op (<=1 draw tile; ==1 clear the region
//     after upload; ==2 additionally draw the tile into the locked texture)
//     and blend_op (1 = SourceOver, else SourceCopy), and the base is
//     blitted into the locked texture through GdipCreateBitmapFromScan0.
//   - OnEndScene [0x180058400 L14] ticks sub_180001320(ctx+0x240): the
//     edit-mode PRUNE of the ctx sub-collections, not the frame advance
//     (the advance rides the per-draw registry walks above).
//   - GDI+ lifecycle: GdiplusStartup in DllMain (mme_ui.cpp ports it).
//
// 0x33 TEXTUREVALUE consumption (sub_18001B0A0 case 51 -> sub_180063AA0 ->
// sub_180063B30 -> sub_180064020) also lives here: every frame, before the
// techniques run, the texture bound to the parameter named by "TextureName"
// is read back (render-target/DEFAULT-pool surfaces staged through
// GetRenderTargetData into a cached SYSTEMMEM surface) and its first
// min(elements, w*h) texels are converted to float4s and applied with
// SetVectorArray. Failures / NULL textures / unsupported formats zero the
// array and still call SetVectorArray, exactly like the original.
#pragma once

#include <d3d9.h>
#include <d3dx9.h>

#include <string>

namespace mme {

class MmeContext;

// The ctx+0x48 animated-texture collection (the original is a set of
// std::list/std::map containers of keyed instances; the std::vector here is
// the documented-divergence std container of mme_context.h).
class MmeAnimatedTextureSet {
public:
    MmeAnimatedTextureSet();
    ~MmeAnimatedTextureSet();

    MmeAnimatedTextureSet(const MmeAnimatedTextureSet&) = delete;
    MmeAnimatedTextureSet& operator=(const MmeAnimatedTextureSet&) = delete;

    // Opaque runtime state (defined in anime_texture.cpp).
    void* impl;
};

// [FUN_180001000 ctor area] create the ctx+0x48 collection.
MmeAnimatedTextureSet* MmeAnimeCreate();
// [FUN_1800011d0] destroy the collection (releases every GDI+ image and D3D
// texture; called from the MmeContext destructor).
void MmeAnimeDestroy(MmeAnimatedTextureSet* set);

// [sub_18000F3A0 case 45] parse-time construction. Returns the anime object
// (the CAnimeGIF / CAnimePNG port); on failure the returned object is
// unusable and `error` carries the ctor's exact message line to append to
// the effect log verbatim (sub_180023440):
//   "failed to open '<path>'\n"
//   "failed to open '<path>': source image must be animated\n"
//   "failed to load '<path>'\n"
void* MmeAnimeConstructGif(IDirect3DDevice9* device, const char* path,
                           std::string* error);
void* MmeAnimeConstructPng(IDirect3DDevice9* device, const char* path,
                           std::string* error);
// Destroys an object returned by the constructors above (the CAnimeGIF /
// CAnimePNG deleting destructors).
void MmeAnimeDestroyObject(void* object);

// [sub_18001F090 registration at 0x1800116xx] hand the parse-constructed
// object to the ctx+0x48 set: ownership passes to the set. `seekParam` is
// the parse-time-resolved SeekVariable handle (0 = none). When `ctx` has no
// set yet the object is destroyed immediately (defensive; the engine
// creates the set before any effect loads).
void MmeAnimeRegisterParsed(MmeContext* ctx, void* object,
                            ID3DXEffect* effect, D3DXHANDLE param,
                            const char* effectPath, double offset,
                            double speed, D3DXHANDLE seekParam);

// [FUN_180001320] the per-frame tick (OnEndScene-side entry). Advances every
// registered entry (absolute time / SeekVariable), re-asserts the texture
// binding, consumes the 0x33 TEXTUREVALUE records and prunes entries whose
// owning effect left the engine cache.
void MmeAnimeTick(MmeContext* ctx);

// Device-loss support: the DYNAMIC/DEFAULT textures die with the device.
// [sub_1800164C0 case 45 @0x18001654d] the original's device-lost walk clears
// the texture parameter FIRST (effect->SetTexture(param, NULL) @0x18001655d)
// and THEN calls the vtbl+0x18 slot (@0x18001656a; CAnimeGIF sub_1800054D0 /
// CAnimePNG sub_180008290): release the DYNAMIC/DEFAULT upload texture and
// create nothing while the device is lost.
void MmeAnimeOnDeviceLost(MmeAnimatedTextureSet* set);
// [sub_180016660 case 45 @0x1800166cb-0x1800166d8] the post-Reset walk
// re-creates the upload texture IMMEDIATELY through the vtbl+0x20 slot
// (CAnimeGIF sub_180005500 / CAnimePNG sub_1800082C0: release any remnant,
// then D3DXCreateTexture(dev, w, h, 1, DYNAMIC, A8R8G8B8, DEFAULT)); it runs
// after the effects' OnResetDevice (sub_180016660 calls effect vtbl+560
// before the record walk) and a failed re-create feeds OnResetDevice's
// failure chain ("Failed to reset MikuMikuEffect", last non-zero wins in
// sub_18002DE40/sub_180016660). SetFrame has NO lazy re-create in the
// original (sub_180005360 never builds a texture) - the SetFrame rebuild
// branch in the port is a defensive PORT ADDITION, not the primary path.
// Returns S_OK (all re-created) or the last failing D3DXCreateTexture result.
HRESULT MmeAnimeOnDeviceReset(MmeAnimatedTextureSet* set);

} // namespace mme
