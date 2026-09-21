// Test-only forwarding device. All surfaces and GPU operations are real D3D9;
// only the two creation HRESULTs can be injected. No vtable or DLL hooks.
#pragma once
#include <d3d9.h>
#include <deque>

struct SnapshotDeviceFixture : IDirect3DDevice9 {
    IDirect3DDevice9* real; // borrowed; forwarded AddRef/Release keep it alive
    unsigned colorCreates = 0, depthCreates = 0;
    std::deque<HRESULT> colorResults, depthResults;
    explicit SnapshotDeviceFixture(IDirect3DDevice9* device) : real(device) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,  void** ppvObj) override {
        return real->QueryInterface(riid, ppvObj);
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return real->AddRef();
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return real->Release();
    }
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {
        return real->TestCooperativeLevel();
    }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override {
        return real->GetAvailableTextureMem();
    }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override {
        return real->EvictManagedResources();
    }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override {
        return real->GetDirect3D(ppD3D9);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override {
        return real->GetDeviceCaps(pCaps);
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override {
        return real->GetDisplayMode(iSwapChain, pMode);
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override {
        return real->GetCreationParameters(pParameters);
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override {
        return real->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
    }
    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override {
        return real->SetCursorPosition(X, Y, Flags);
    }
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override {
        return real->ShowCursor(bShow);
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override {
        return real->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override {
        return real->GetSwapChain(iSwapChain, pSwapChain);
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override {
        return real->GetNumberOfSwapChains();
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override {
        return real->Reset(pPresentationParameters);
    }
    HRESULT STDMETHODCALLTYPE Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) override {
        return real->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override {
        return real->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override {
        return real->GetRasterStatus(iSwapChain, pRasterStatus);
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override {
        return real->SetDialogBoxMode(bEnableDialogs);
    }
    void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) override {
        return real->SetGammaRamp(iSwapChain, Flags, pRamp);
    }
    void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override {
        return real->GetGammaRamp(iSwapChain, pRamp);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override {
        return real->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override {
        return real->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override {
        return real->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override {
        return real->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override {
        return real->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        ++colorCreates;
        if (!colorResults.empty()) {
            const HRESULT result = colorResults.front(); colorResults.pop_front();
            if (result != S_OK) { *ppSurface = nullptr; return result; }
        }
        return real->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        ++depthCreates;
        if (!depthResults.empty()) {
            const HRESULT result = depthResults.front(); depthResults.pop_front();
            if (result != S_OK) { *ppSurface = nullptr; return result; }
        }
        return real->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) override {
        return real->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override {
        return real->UpdateTexture(pSourceTexture, pDestinationTexture);
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override {
        return real->GetRenderTargetData(pRenderTarget, pDestSurface);
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override {
        return real->GetFrontBufferData(iSwapChain, pDestSurface);
    }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override {
        return real->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
    }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) override {
        return real->ColorFill(pSurface, pRect, color);
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override {
        return real->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override {
        return real->SetRenderTarget(RenderTargetIndex, pRenderTarget);
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override {
        return real->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override {
        return real->SetDepthStencilSurface(pNewZStencil);
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override {
        return real->GetDepthStencilSurface(ppZStencilSurface);
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override {
        return real->BeginScene();
    }
    HRESULT STDMETHODCALLTYPE EndScene() override {
        return real->EndScene();
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override {
        return real->Clear(Count, pRects, Flags, Color, Z, Stencil);
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) override {
        return real->SetTransform(State, pMatrix);
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override {
        return real->GetTransform(State, pMatrix);
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix) override {
        return real->MultiplyTransform(state, matrix);
    }
    HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9* pViewport) override {
        return real->SetViewport(pViewport);
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override {
        return real->GetViewport(pViewport);
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9* pMaterial) override {
        return real->SetMaterial(pMaterial);
    }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override {
        return real->GetMaterial(pMaterial);
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, CONST D3DLIGHT9* arg1) override {
        return real->SetLight(Index, arg1);
    }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9* arg1) override {
        return real->GetLight(Index, arg1);
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override {
        return real->LightEnable(Index, Enable);
    }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override {
        return real->GetLightEnable(Index, pEnable);
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, CONST float* pPlane) override {
        return real->SetClipPlane(Index, pPlane);
    }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override {
        return real->GetClipPlane(Index, pPlane);
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
        return real->SetRenderState(State, Value);
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override {
        return real->GetRenderState(State, pValue);
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override {
        return real->CreateStateBlock(Type, ppSB);
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override {
        return real->BeginStateBlock();
    }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override {
        return real->EndStateBlock(ppSB);
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus) override {
        return real->SetClipStatus(pClipStatus);
    }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override {
        return real->GetClipStatus(pClipStatus);
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override {
        return real->GetTexture(Stage, ppTexture);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override {
        return real->SetTexture(Stage, pTexture);
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override {
        return real->GetTextureStageState(Stage, Type, pValue);
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
        return real->SetTextureStageState(Stage, Type, Value);
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override {
        return real->GetSamplerState(Sampler, Type, pValue);
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override {
        return real->SetSamplerState(Sampler, Type, Value);
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) override {
        return real->ValidateDevice(pNumPasses);
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries) override {
        return real->SetPaletteEntries(PaletteNumber, pEntries);
    }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override {
        return real->GetPaletteEntries(PaletteNumber, pEntries);
    }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override {
        return real->SetCurrentTexturePalette(PaletteNumber);
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override {
        return real->GetCurrentTexturePalette(PaletteNumber);
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(CONST RECT* pRect) override {
        return real->SetScissorRect(pRect);
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override {
        return real->GetScissorRect(pRect);
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override {
        return real->SetSoftwareVertexProcessing(bSoftware);
    }
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override {
        return real->GetSoftwareVertexProcessing();
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override {
        return real->SetNPatchMode(nSegments);
    }
    float STDMETHODCALLTYPE GetNPatchMode() override {
        return real->GetNPatchMode();
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override {
        return real->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override {
        return real->DrawIndexedPrimitive(primitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override {
        return real->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override {
        return real->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override {
        return real->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override {
        return real->CreateVertexDeclaration(pVertexElements, ppDecl);
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override {
        return real->SetVertexDeclaration(pDecl);
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override {
        return real->GetVertexDeclaration(ppDecl);
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override {
        return real->SetFVF(FVF);
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override {
        return real->GetFVF(pFVF);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) override {
        return real->CreateVertexShader(pFunction, ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override {
        return real->SetVertexShader(pShader);
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override {
        return real->GetVertexShader(ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override {
        return real->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override {
        return real->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override {
        return real->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override {
        return real->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) override {
        return real->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override {
        return real->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override {
        return real->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override {
        return real->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override {
        return real->SetStreamSourceFreq(StreamNumber, Setting);
    }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override {
        return real->GetStreamSourceFreq(StreamNumber, pSetting);
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override {
        return real->SetIndices(pIndexData);
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override {
        return real->GetIndices(ppIndexData);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) override {
        return real->CreatePixelShader(pFunction, ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override {
        return real->SetPixelShader(pShader);
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override {
        return real->GetPixelShader(ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override {
        return real->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override {
        return real->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override {
        return real->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override {
        return real->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) override {
        return real->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override {
        return real->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo) override {
        return real->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
    }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo) override {
        return real->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
    }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override {
        return real->DeletePatch(Handle);
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override {
        return real->CreateQuery(Type, ppQuery);
    }
};
