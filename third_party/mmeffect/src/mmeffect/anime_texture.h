// anime_texture.h - the animated-texture engine (CAnimeGIF / CAnimeTex /
// CAnimePNG region, 0x180001000-0x180008000) and the ctx+0x48 registry.
//
// Evidence:
//   - OnEndScene [0x180058400 L17]: FUN_180001320(ctx + 0x48) - the ctx+0x48
//     sub-object is ticked every frame (gated on IsEditMode inside).
//   - CAnimeGIF ctor FUN_180004810 (big-C 5408): GdipLoadImageFromFile,
//     GdipImageGetFrameDimensionsList + GdipImageGetFrameCount (frame count),
//     GdipGetPropertyItemSize/Item(0x5100 = PropertyTagFrameDelay) - the
//     per-frame delay values are stored *10 (centiseconds -> milliseconds).
//   - CAnimeTex::SetFrame FUN_180005360 (big-C 5966): when the frame index
//     changes, GdipImageSelectActiveFrame(image, FrameDimensionTime, frame),
//     then LockRect(0) on the IDirect3DTexture9 and
//     GdipCreateBitmapFromScan0(w, h, pitch, PixelFormat32bppARGB, pBits) -
//     GDI+ draws DIRECTLY into the locked texture memory through a Graphics
//     with CompositingModeSourceCopy (GdipDrawImageI + GdipFlush), then
//     UnlockRect. Texture creation FUN_180005500 (big-C 6066):
//     D3DXCreateTexture(device, w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
//     D3DPOOL_DEFAULT).
//   - Annotations per REFERENCE.txt ANIMATEDTEXTURE (docs/REFERENCE.txt ~840):
//       string ResourceName (required, .gif / APNG .png)
//       float  Offset       (start offset, seconds, default 0)
//       float  Speed        (playback speed multiplier, default 1.0)
//       string SeekVariable (drive the animation from a parameter value)
//       bool   SyncInEditMode (0x1800b4108; sync to edit-mode time)
//     The SAS interpreter (sas_interpreter.cpp, semanticId 0x2D) already
//     parses ResourceName/Offset/Speed/SeekVariable into SasResource; this
//     module registers one runtime entry per such resource and reads
//     SyncInEditMode directly from the effect annotation.
//   - Resource paths resolve like the original's FUN_1800675e0 helper
//     (big-C 81473, used from the animated-texture code at big-C 3054):
//     PathRelativePathToA against the exe directory / the effect directory.
//   - GDI+ lifecycle: the original GdiplusStartup's in DllMain
//     (DLL_PROCESS_ATTACH, big-C 69501-69520) and GdiplusShutdown's on
//     DLL_PROCESS_DETACH. Ported identically in mme_ui.cpp.
#pragma once

#include <d3d9.h>
#include <d3dx9.h>

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

// [FUN_180001320] the per-frame tick (OnEndScene). Scans every loaded
// effect's SAS resources for ANIMATEDTEXTURE entries (registering new ones),
// advances the animation time, decodes the current frame through GDI+ into
// the parameter's DYNAMIC A8R8G8B8 texture and effect->SetTexture's it.
// Edit-mode sync is honored per entry (SyncInEditMode).
void MmeAnimeTick(MmeContext* ctx);

// Device-loss/reacquire support: the DYNAMIC/DEFAULT textures die with the
// device. The original recreates them through FUN_180005500 on the next
// SetFrame; the port releases them here (called from the anime tick lazily
// as well - an entry with a null texture recreates on demand).
void MmeAnimeOnDeviceLost(MmeAnimatedTextureSet* set);

} // namespace mme
