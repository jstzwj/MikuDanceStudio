// mmhack_lightmatrix.cpp - GetLightViewProjMatrix computation.
//
// Ported from [0x180001330] (7.8 KB of math; kit named copy
// 180001330_GetLightViewProjMatrix.c). Uses the statically imported
// D3DXMatrixInverse / D3DXMatrixMultiply / D3DXMatrixLookAtLH.
#include "mmhack_state.h"

void MmhComputeLightViewProjMatrix(D3DMATRIX* outView, D3DMATRIX* outProj,
                                   D3DMATRIX* outViewProj)
{
    // Divergence guard: the original dereferences the effect unconditionally
    // (it would crash without an effect); we bail out with zero matrices.
    if (g_mmh.currentEffect == nullptr) {
        if (outView)     memset(outView, 0, sizeof(D3DMATRIX));
        if (outProj)     memset(outProj, 0, sizeof(D3DMATRIX));
        if (outViewProj) memset(outViewProj, 0, sizeof(D3DMATRIX));
        return;
    }
    ID3DXEffect* effect = (ID3DXEffect*)g_mmh.currentEffect;

    // [0x180001330+0x20] D3DXHANDLE of "matLightViewProj" via GetMatrix
    // (slot 0x138/8 = 39; D3DX handles accept parameter name strings).
    D3DXMATRIX lvp;
    effect->GetMatrix("matLightViewProj", &lvp);

    // Accessory z-plot fixup: LVP = inv(acsWorld) * LVP.
    if (g_mmh.currentDrawType == 5 && g_mmh.currentObjectKind == 0) {
        int objIdx = ExpGetCurrentObject();
        int acsNum = ExpGetAcsNum();
        for (int i = 0; i < acsNum; i++) {
            if (ExpGetAcsOrder(i) == objIdx) {
                D3DXMATRIX wmat = ExpGetAcsWorldMat(i);
                D3DXMATRIX inv;
                D3DXMatrixInverse(&inv, nullptr, &wmat);
                D3DXMATRIX tmp;
                D3DXMatrixMultiply(&tmp, &inv, &lvp);
                lvp = tmp;
                break;
            }
        }
    }

    // [0x180001330+0x180] effect->GetDevice (slot 0x220/8 = 68), then the
    // view matrix and light #0 from the device.
    IDirect3DDevice9* dev = nullptr;
    effect->GetDevice(&dev);
    D3DXMATRIX view;
    D3DLIGHT9 light;
    memset(&light, 0, sizeof(light));
    if (dev != nullptr) {
        // [0x180001330+0x1a0] GetLight(0) first, then [0x168/8 = slot 45]
        // GetTransform(D3DTS_VIEW) — same order as the original.
        dev->GetLight(0, &light);                   // slot 52 (+0x1a0)
        dev->GetTransform(D3DTS_VIEW, &view);       // slot 45 (+0x168)
        dev->Release();                             // [0x180001330+0x1c8] release
    } else {
        memset(&view, 0, sizeof(view));
        view.m[0][0] = view.m[1][1] = view.m[2][2] = view.m[3][3] = 1.0f;
    }

    D3DXMATRIX viewInv;
    D3DXMatrixInverse(&viewInv, nullptr, &view);

    // eye = inverse-view translation; target = eye - lightDir * 50;
    // up = (0,0,1). [0x180001330+0x1d0..+0x250]
    D3DXVECTOR3 eye(viewInv.m[3][0], viewInv.m[3][1], viewInv.m[3][2]);
    D3DXVECTOR3 at(eye.x - light.Direction.x * 50.0f,
                   eye.y - light.Direction.y * 50.0f,
                   eye.z - light.Direction.z * 50.0f);
    D3DXVECTOR3 up(0.0f, 0.0f, 1.0f);
    D3DXMATRIX lookAt;
    D3DXMatrixLookAtLH(&lookAt, &at, &eye, &up);

    // lightView = worldMatrix * lookAt   [D3DXMatrixMultiply(&tmp, &world, &lookAt)]
    D3DXMATRIX lightView;
    D3DXMatrixMultiply(&lightView, (const D3DXMATRIX*)&g_mmh.worldMatrix, &lookAt);

    D3DXMATRIX lightViewInv;
    D3DXMATRIX* invResult = D3DXMatrixInverse(&lightViewInv, nullptr, &lightView);
    if (invResult == nullptr) {
        // Singular matrix: D3DXMatrixInverse returned NULL [0x180001330 check
        // lVar5 == 0]; emit the degenerate projection
        // row0..row2 = 0, row3 = (0, -1, 0, 1). [0x180001330+0x430..]
        D3DXMATRIX degenerate;
        memset(&degenerate, 0, sizeof(degenerate));
        degenerate.m[3][1] = -1.0f;
        degenerate.m[3][3] = 1.0f;
        if (outView)
            *outView = lightView;
        if (outProj)
            *outProj = degenerate;
        if (outViewProj) {
            D3DXMATRIX vp;
            D3DXMatrixMultiply(&vp, &lightView, &degenerate);
            *outViewProj = vp;
        }
    }

    // Outputs [0x180001330+0x4f0..]:
    //   outViewProj = matLightViewProj (possibly accessory-adjusted)
    //   outProj     = inv(lightView) * matLightViewProj
    //   outView     = lightView
    if (outViewProj)
        *outViewProj = lvp;
    if (outProj) {
        D3DXMATRIX tmp;
        D3DXMatrixMultiply(&tmp, &lightViewInv, &lvp);
        *outProj = tmp;
    }
    if (outView)
        *outView = lightView;
}
