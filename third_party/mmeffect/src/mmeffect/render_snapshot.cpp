// render_snapshot.cpp - the snapshot capture/update ports (see render_snapshot.h).
#include "render_snapshot.h"

#include <d3dx9.h>

#include "mmhack_api.h"  // state queries + GetCurrentEffect

#include "model_data.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "pass_planner.h"   // MmeAcquireBindingContext (FUN_180055690)

namespace mme {

int MmeCachePerVertexValue(ModelData* model)
{
    // [0x18005af70] FUN_18005af70: if ModelData+0x3c is negative, query
    // GetStreamSource(0) and compute desc.Size / stride once; returns the
    // cached value (or -1 when unavailable). Note: the failure path does NOT
    // store -1 into the field (verified in the decompile).
    if (model == nullptr) {
        return -1;
    }
    int cached = model->cachedPerVertexValue();
    if (cached >= 0) {
        return cached;
    }

    IDirect3DDevice9* device = model->device();
    if (device == nullptr) {
        return -1;
    }
    IDirect3DVertexBuffer9* vertexBuffer = nullptr;
    UINT offset = 0;
    UINT stride = 0;
    // device slot 0x328 (original wrapper numbering) = GetStreamSource.
    if (FAILED(device->GetStreamSource(0, &vertexBuffer, &offset, &stride))) {
        return -1;
    }
    if (vertexBuffer == nullptr || stride == 0) {
        if (vertexBuffer != nullptr) {
            vertexBuffer->Release();   // [L33]
        }
        return -1;                     // [L35] cached value untouched
    }
    D3DVERTEXBUFFER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    vertexBuffer->GetDesc(&desc);      // buffer slot 0x68 = GetDesc [L38]
    int perVertex = static_cast<int>(desc.Size / stride);   // [L39]
    model->setCachedPerVertexValue(perVertex);              // [L40]
    vertexBuffer->Release();                                // [L41-42]
    return perVertex;
}

void MmeCaptureCurrentRenderState(ModelData* model, RenderSnapshot* snap)
{
    // [0x180059c60] MME_CaptureCurrentRenderState.
    ID3DXEffect* effect = static_cast<ID3DXEffect*>(GetCurrentEffect());
    BOOL effectFileUsed = IsEffectFileUsed();

    IDirect3DBaseTexture9* baseTexture = GetTexture();
    snap->base_texture = baseTexture;                       // +0x38 [L41-43]
    snap->base_texture_present = baseTexture != nullptr ? 1 : 0;  // +0x56

    snap->toon_texture = GetToonTexture();                  // +0x40 [L44-45]
    snap->sphere_texture = GetSphereMapTexture();           // +0x48 [L46-47]
    snap->sphere_mode = GetSphereMapMode();                 // +0x50 [L48-49]
    snap->effect_file_used = effectFileUsed != 0 ? 1 : 0;   // +0x55 [L51]
    snap->toon_used = IsToonUsed() != 0 ? 1 : 0;            // +0x54 [L50-52]
    snap->subset_index = GetCurrentSubsetIndex();           // +0x28 [L53-54]

    if (!effectFileUsed) {
        // Fixed-function path: reconstruct the model world from the live
        // device transforms [L55-104].
        IDirect3DDevice9* device = model != nullptr ? model->device() : nullptr;
        D3DMATRIX world;
        memset(&world, 0, sizeof(world));
        if (device != nullptr) {
            device->GetTransform(D3DTS_WORLD, &world);      // [L56-57]
        }
        D3DXMATRIX modelWorld;
        D3DXMatrixMultiply(&modelWorld,
                           reinterpret_cast<const D3DXMATRIX*>(&world),
                           reinterpret_cast<const D3DXMATRIX*>(&g_invWorldAtBegin));
        memcpy(&snap->model_world, &modelWorld, sizeof(D3DMATRIX));  // +0x58
        memcpy(&snap->effect_world, &modelWorld, sizeof(D3DMATRIX)); // +0x98
        // [L83-84] zero the effect_world translation row (_41,_42,_43):
        // snapshot+0xc8 (8 bytes) and +0xc8 (4 bytes) per the decompile.
        snap->effect_world.m[3][0] = 0.0f;
        snap->effect_world.m[3][1] = 0.0f;
        snap->effect_world.m[3][2] = 0.0f;

        // [L93-103] light_view_projection = model_world * light-view-proj.
        D3DXMATRIX lvp;
        D3DXMatrixMultiply(&lvp, &modelWorld,
                           reinterpret_cast<const D3DXMATRIX*>(&g_lightViewProjMatrix));
        memcpy(&snap->light_view_projection, &lvp, sizeof(D3DMATRIX));  // +0x158

        // [FUN_18001b5b0 matrix-binder equivalence] the original's parameter
        // binder composes matWorldViewProj at bind time as matWorld *
        // (world-at-BeginScene * view * proj) = deviceWorld * view * proj.
        // The capture's +0x118 slot is only filled by the effect branch, so
        // the binder composition is folded here: matWorldViewProj =
        // model_world * g_viewProjMatrix (the OnBeginScene caches). Without
        // this a fixed-function-path draw would bind an all-zero matrix and
        // every vertex collapses (the blank-viewport bug).
        D3DXMATRIX wvp;
        D3DXMatrixMultiply(&wvp, &modelWorld,
                           reinterpret_cast<const D3DXMATRIX*>(&g_viewProjMatrix));
        memcpy(&snap->world_view_projection, &wvp, sizeof(D3DMATRIX));  // +0x118
    } else {
        // Effect path: read the matrices/colors from the current effect
        // [L105-143]. (The original calls the effect through vtable slots
        // 0x138/0x108/0xb8 = GetMatrix/GetVector/GetBool; we use the standard
        // ID3DXEffect interface, which is what GetCurrentEffect returns in
        // the rebuilt suite.)
        if (effect != nullptr) {
            D3DXMATRIX m;
            // The original GetMatrix calls write straight into the snapshot;
            // on a failed lookup D3DX zeroes the destination, so zero the
            // scratch first and copy unconditionally (a failed read must not
            // leak stack garbage into the matrix slots).
            memset(&m, 0, sizeof(m));
            effect->GetMatrix(g_paramHandles[kParamMatWorldViewProj], &m);
            memcpy(&snap->world_view_projection, &m, sizeof(D3DMATRIX));  // +0x118 [L106]
            memset(&m, 0, sizeof(m));
            effect->GetMatrix(g_paramHandles[kParamMatLightViewProj], &m);
            memcpy(&snap->light_view_projection, &m, sizeof(D3DMATRIX));  // +0x158 [L107]
            if (model != nullptr && model->kind() == 0) {
                // Accessory world matrix from the current render plan.
                snap->model_world = model->planMatrix();
                memset(&m, 0, sizeof(m));
                effect->GetMatrix(g_paramHandles[kParamMatWorld], &m);
                memcpy(&snap->effect_world, &m, sizeof(D3DMATRIX));           // +0x98 [L110]
            } else {
                // Model: identity matrices [L112-133].
                memset(&snap->model_world, 0, sizeof(D3DMATRIX));
                snap->model_world.m[0][0] = 1.0f;
                snap->model_world.m[1][1] = 1.0f;
                snap->model_world.m[2][2] = 1.0f;
                snap->model_world.m[3][3] = 1.0f;
                memset(&snap->effect_world, 0, sizeof(D3DMATRIX));
                snap->effect_world.m[0][0] = 1.0f;
                snap->effect_world.m[1][1] = 1.0f;
                snap->effect_world.m[2][2] = 1.0f;
                snap->effect_world.m[3][3] = 1.0f;
            }
            D3DXVECTOR4 v;
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamEgColor], &v);
            memcpy(&snap->edge_color, &v, sizeof(D3DCOLORVALUE));             // +0x198 [L134]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamToonColor], &v);
            memcpy(&snap->toon_color, &v, sizeof(D3DCOLORVALUE));             // +0x1a8 [L135]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamSpcColor], &v);
            memcpy(&snap->specular_color, &v, sizeof(D3DCOLORVALUE));         // +0x1b8 [L136]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamDifColor], &v);
            memcpy(&snap->diffuse_color, &v, sizeof(D3DCOLORVALUE));          // +0x1c8 [L137]
            BOOL b = FALSE;
            effect->GetBool(g_paramHandles[kParamTransp], &b);
            snap->transparent = b;                                            // +0x218 [L138]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamTexCAdd], &v);
            memcpy(&snap->texture_add, &v, sizeof(D3DCOLORVALUE));            // +0x1d8 [L139]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamTexCMul], &v);
            memcpy(&snap->texture_multiply, &v, sizeof(D3DCOLORVALUE));       // +0x1e8 [L140]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamSphCAdd], &v);
            memcpy(&snap->sphere_add, &v, sizeof(D3DCOLORVALUE));             // +0x1f8 [L141]
            memset(&v, 0, sizeof(v));
            effect->GetVector(g_paramHandles[kParamSphCMul], &v);
            memcpy(&snap->sphere_multiply, &v, sizeof(D3DCOLORVALUE));        // +0x208 [L142]
        }
    }

    // [L144] matRotate is read in BOTH branches.
    if (effect != nullptr) {
        D3DXMATRIX m;
        memset(&m, 0, sizeof(m));
        effect->GetMatrix(g_paramHandles[kParamMatRotate], &m);
        memcpy(&snap->rotation, &m, sizeof(D3DMATRIX));                   // +0xd8
    }
}

void MmeUpdateModelRenderSnapshot(ModelData* model, RenderSnapshot* snap)
{
    // [0x180059ba0] MME_UpdateModelRenderSnapshot.
    if (model == nullptr || snap == nullptr) {
        return;
    }
    int drawType = snap->draw_type;          // +0x2c raw value [L14]

    MmeCachePerVertexValue(model);           // [L15] FUN_18005af70 (result unused)
    MmeCaptureCurrentRenderState(model, snap);  // [L16]

    // [L17-35] normalize draw type 1..5 -> 0..4; other values leave the
    // record untouched.
    switch (drawType) {
    case 1: snap->draw_type_index = 0; break;
    case 2: snap->draw_type_index = 1; break;
    case 3: snap->draw_type_index = 2; break;
    case 4: snap->draw_type_index = 3; break;
    case 5: snap->draw_type_index = 4; break;
    default: return;
    }

    // [L36-42] for render-class 1/2 models with a negative cached subset
    // index: cache the record into ModelData+0x138 (0x220 memcpy) and store
    // the binding context at snapshot+0x20 (= ModelData+0x158).
    if ((model->renderClass() == 1 || model->renderClass() == 2) &&
        model->snapshot().subset_index < 0) {
        MmeContext* ctx = MmeGetContext();
        model->snapshot() = *snap;                       // [L38]
        void* bindingContext = MmeAcquireBindingContext(ctx);          // [L39 FUN_180055690]
        model->snapshot().binding_context = bindingContext;            // [L40]
        // [L41] the original Captures the state block via vtable slot 0x20
        // (standard IDirect3DStateBlock9 layout); the port Captures here.
        if (bindingContext != nullptr) {
            static_cast<IDirect3DStateBlock9*>(bindingContext)->Capture();
        }
    }
}

// MmeApplyModelRenderSnapshot [0x18005a1e0] is implemented in material_bind.cpp.

} // namespace mme
