// ===========================================================================
// VA 0x00406E90 - InitRenderStates  (original: sub_406E90, 1384 bytes)
// ===========================================================================
// Fixed-function pipeline state block on the render subsystem object
// (the 0x1D574 object at this+657092).  Called from InitD3D tail
// (0x004087B6), from the device-reset path 0x00440DB0 and from the
// command dispatcher 0x0047E8A0.
//
// Device vtable slots used by the original (32-bit IDirect3DDevice9,
// original slot evidence; production calls use the SDK interface):
//   +28   GetDeviceCaps
//   +176  SetTransform
//   +212  LightEnable
//   +228  SetRenderState
//   +268  SetTextureStageState
//   +276  SetSamplerState
//
// Sub-object fields (D3DRenderer; x86 offsets kept for reference):
//   +120024  shaderModelCaps / MaxAnisotropy (caps+0x6C mirrored by InitD3D;
//            x64 0x7FF7CB4280E1 - +0x6C is D3DCAPS9::MaxAnisotropy because
//            MaxTextureWidth/Height sit at +0x58/+0x5C in this struct)
//   +120032  device (IDirect3DDevice9*)
//   +120164  d3dInitialized (stencil-shadow enable byte; edge/depth setup
//            when set)
//   +120176  runtimeToggle (anisotropic filtering enable byte)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

IDirect3DDevice9* DeviceOf(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    if (sub == nullptr)
        return nullptr;
    return sub->device;
}

}  // namespace

void InitRenderStates(MMDApp* app) {
    IDirect3DDevice9* dev = DeviceOf(app);
    if (dev == nullptr)
        return;
    D3DRenderer* sub = app->Renderer();

    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);                 // state 7, 1
    dev->LightEnable(0, TRUE);                                      // vtbl 212
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);                      // state 137
    dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);                // state 29

    // texture cascade: MODULATE alpha/color, arg1 = texture, arg2 = diffuse
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);   // (0,4,4)
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);   // (0,5,2)
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);   // (0,1,4)
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);   // (0,2,2)
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);   // (0,3,0)
    dev->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);   // (1,3,1)

    // Sampler filters, x64 0x7FF7CB4286E9..0x7FF7CB428951: the original
    // groups the writes by sampler STATE, not by stage - all three
    // MINFILTER writes first, then MAGFILTER, then MIPFILTER, then
    // MAXANISOTROPY for all three samplers.  With the toggle on the
    // value is the caps mirror for EVERY sampler: x86 re-reads
    // [this+120024] three times (0x40709B / 0x4070BF / 0x4070D3), x64
    // loads r9d=[this+3A9B0] for samplers 0/1 (0x7FF7CB4287D8 /
    // 0x7FF7CB4287F5) and once more before jmping into the shared
    // sampler-2 tail at 0x7FF7CB42893D (0x7FF7CB42880E).  With the
    // toggle off every write is constant 1: x86 0x4071C3 / 0x4071D4 /
    // 0x4071F1; x64's mov r9d,1 at 0x7FF7CB428937 is only the AF-off
    // operand falling through into that same 0x7FF7CB42893D tail.
    // Identical behavior on both architectures - no per-arch split.
    const DWORD maxAniso = static_cast<DWORD>(sub->shaderModelCaps);
    const DWORD minMagFilter = sub->runtimeToggle != 0 ? D3DTEXF_ANISOTROPIC
                                                       : D3DTEXF_LINEAR;
    const DWORD mipFilter = sub->runtimeToggle != 0 ? D3DTEXF_ANISOTROPIC
                                                    : D3DTEXF_NONE;
    const DWORD maxAnisoAll = sub->runtimeToggle != 0 ? maxAniso : 1;
    for (DWORD s = 0; s < 3; ++s) {
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, minMagFilter);   // (s,6)
    }
    for (DWORD s = 0; s < 3; ++s) {
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, minMagFilter);   // (s,5)
    }
    for (DWORD s = 0; s < 3; ++s) {
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, mipFilter);      // (s,7)
    }
    dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, maxAnisoAll);    // (0,10)
    dev->SetSamplerState(1, D3DSAMP_MAXANISOTROPY, maxAnisoAll);    // (1,10)
    dev->SetSamplerState(2, D3DSAMP_MAXANISOTROPY, maxAnisoAll);    // (2,10)

    // half-texel matrix for stages 1/2 (D3DTS_TEXTURE1=17, D3DTS_TEXTURE2=18)
    D3DMATRIX halfTexel = {
        0.5f,  0.0f,  0.0f, 0.0f,
        0.0f, -0.5f,  0.0f, 0.0f,
        0.0f,  0.0f,  0.0f, 0.0f,
        0.5f,  0.5f,  0.0f, 1.0f,
    };
    dev->SetTransform(D3DTS_TEXTURE1, &halfTexel);                  // (17, m)
    dev->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_MODULATE);   // (2,4,4)
    dev->SetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);   // (2,5,2)
    dev->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);   // (2,2,2)
    dev->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);   // (2,3,1)
    dev->SetTransform(D3DTS_TEXTURE2, &halfTexel);                   // (18, m)
    dev->SetTextureStageState(2, D3DTSS_TEXTURETRANSFORMFLAGS,
                              D3DTTFF_COUNT2);                       // (2,24,2)
    // TCI written as the literal 0x10000 (= D3DTSS_TCI_CAMERASPACENORMAL):
    // that is the exact immediate at 0x7FF7CB428A5C, and the binary contains
    // no 0x30000 (SPHEREMAP) TCI immediate anywhere.  Spelled like
    // accessory.cpp to pin the quirk value.
    dev->SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 0x10000);      // (2,11,0x10000)

    if (sub->d3dInitialized != 0) {  // stencil shadow setup
        dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);              // state 52
        dev->SetRenderState(D3DRS_STENCILMASK, 255);                 // state 58
    }
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);       // state 206
    dev->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_MAX);         // state 209, 5
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);          // state 19, 5
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);      // state 20, 6

    D3DCAPS9 caps;
    dev->GetDeviceCaps(&caps);
    if (caps.AlphaCmpCaps & D3DPCMPCAPS_GREATEREQUAL) {  // caps+52 & 0x40
        dev->SetRenderState(D3DRS_ALPHAREF, 1);                     // state 24
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);           // state 15
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);  // state 25, 7
    }
}

}  // namespace mikudancestudio
