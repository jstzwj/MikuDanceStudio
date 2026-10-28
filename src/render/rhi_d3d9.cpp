#include "mikudancestudio/rhi_d3d9.hpp"
#define NOMINMAX
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace mikudancestudio::rhi {
namespace {
struct Resource {
    std::uint32_t generation = 1;
    BufferDesc description;
    TextureDesc textureDescription;
    IDirect3DVertexBuffer9* vertices = nullptr;
    IDirect3DIndexBuffer9* indices = nullptr;
    IDirect3DTexture9* texture = nullptr;
    bool alive = false;
    bool uploaded = false;
    void Release() {
        if (vertices)
            vertices->Release();
        if (indices)
            indices->Release();
        if (texture)
            texture->Release();
        vertices = nullptr;
        indices = nullptr;
        texture = nullptr;
        uploaded = false;
    }
};
class D3D9Device final : public Device {
    static DWORD Compare(CompareOp compare) {
        switch (compare) {
        case CompareOp::Always:
            return D3DCMP_ALWAYS;
        case CompareOp::LessEqual:
            return D3DCMP_LESSEQUAL;
        case CompareOp::Greater:
            return D3DCMP_GREATER;
        case CompareOp::GreaterEqual:
            return D3DCMP_GREATEREQUAL;
        }
        return 0;
    }
    static DWORD Filter(SamplerFilter filter) {
        switch (filter) {
        case SamplerFilter::Point:
            return D3DTEXF_POINT;
        case SamplerFilter::Linear:
            return D3DTEXF_LINEAR;
        case SamplerFilter::Anisotropic:
            return D3DTEXF_ANISOTROPIC;
        }
        return 0;
    }
    static DWORD Address(SamplerAddress address) {
        switch (address) {
        case SamplerAddress::Wrap:
            return D3DTADDRESS_WRAP;
        case SamplerAddress::Clamp:
            return D3DTADDRESS_CLAMP;
        }
        return 0;
    }

public:
    explicit D3D9Device(IDirect3DDevice9* device) : device_(device) { device_->AddRef(); }
    ~D3D9Device() override {
        if (inFrame_)
            device_->EndScene();
        for (auto& resource : resources_)
            resource.Release();
        device_->Release();
    }
    Backend BackendType() const override { return Backend::D3D9; }
    Capabilities Features() const override { return {}; }
    DeviceStatus Status() const override {
        return {identity_, epoch_, resetPending_ ? DeviceState::ResetPending : DeviceState::Ready};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, true, false};
    }
    Handle CreateBuffer(BufferDesc description, const void* initialData) override {
        if (resetPending_ || !description.size ||
            description.size > std::numeric_limits<UINT>::max() ||
            description.usage == BufferUsage::Uniform ||
            (description.usage == BufferUsage::Index &&
             description.size % (description.indexFormat == IndexFormat::UInt32 ? 4u : 2u) != 0))
            return {};
        Resource resource;
        resource.description = description;
        const DWORD usage =
            description.dynamic ? D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY : D3DUSAGE_WRITEONLY;
        const D3DPOOL pool = description.dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        const auto bytes = static_cast<UINT>(description.size);
        const auto result =
            description.usage == BufferUsage::Vertex
                ? device_->CreateVertexBuffer(bytes, usage, 0, pool, &resource.vertices, nullptr)
                : device_->CreateIndexBuffer(bytes, usage,
                                             description.indexFormat == IndexFormat::UInt32
                                                 ? D3DFMT_INDEX32
                                                 : D3DFMT_INDEX16,
                                             pool, &resource.indices, nullptr);
        if (FAILED(result))
            return {};
        if (initialData && !Copy(resource, 0, initialData, description.size)) {
            resource.Release();
            return {};
        }
        resource.alive = true;
        return Insert(resource);
    }
    Handle CreateTexture(TextureDesc description) override {
        if (resetPending_ || !description.width || !description.height ||
            description.width > std::numeric_limits<UINT>::max() / 4u ||
            description.height > std::numeric_limits<UINT>::max() / (description.width * 4u) ||
            description.format != TextureFormat::Rgba8 ||
            (description.mips != TextureDesc::Mips::SingleLevel &&
             description.mips != TextureDesc::Mips::GenerateLinear))
            return {};
        Resource resource;
        resource.textureDescription = description;
        if (FAILED(device_->CreateTexture(
                description.width, description.height, 1,
                description.mips == TextureDesc::Mips::GenerateLinear ? D3DUSAGE_AUTOGENMIPMAP : 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &resource.texture, nullptr)))
            return {};
        if (description.mips == TextureDesc::Mips::GenerateLinear &&
            FAILED(resource.texture->SetAutoGenFilterType(D3DTEXF_LINEAR))) {
            resource.Release();
            return {};
        }
        resource.alive = true;
        return Insert(resource);
    }
    bool UploadTexture(Handle handle, TexturePixels pixels) override {
        auto* resource = Find(handle);
        if (!resource || !resource->texture || inFrame_ || resetPending_ || !pixels.data)
            return false;
        const std::size_t rowBytes =
            static_cast<std::size_t>(resource->textureDescription.width) * 4;
        const std::size_t height = resource->textureDescription.height;
        if (pixels.rowPitch < rowBytes ||
            height > 0 && pixels.rowPitch > (std::numeric_limits<std::size_t>::max() - rowBytes) /
                                                (height - 1 ? height - 1 : 1) ||
            pixels.bytes < (height - 1) * pixels.rowPitch + rowBytes)
            return false;
        D3DLOCKED_RECT locked{};
        if (FAILED(resource->texture->LockRect(0, &locked, nullptr, 0)))
            return false;
        if (locked.Pitch < 0 || static_cast<std::size_t>(locked.Pitch) < rowBytes) {
            resource->texture->UnlockRect(0);
            return false;
        }
        const auto* source = static_cast<const std::uint8_t*>(pixels.data);
        for (std::size_t y = 0; y < height; ++y) {
            auto* target = static_cast<std::uint8_t*>(locked.pBits) + y * locked.Pitch;
            const auto* row = source + y * pixels.rowPitch;
            for (std::size_t x = 0; x < rowBytes; x += 4) {
                target[x] = row[x + 2];
                target[x + 1] = row[x + 1];
                target[x + 2] = row[x];
                target[x + 3] = row[x + 3];
            }
        }
        const bool unlocked = SUCCEEDED(resource->texture->UnlockRect(0));
        if (unlocked && resource->textureDescription.mips == TextureDesc::Mips::GenerateLinear)
            resource->texture->GenerateMipSubLevels();
        if (unlocked)
            resource->uploaded = true;
        return unlocked;
    }
    bool BindTexture(std::uint32_t slot, Handle handle) override {
        auto* resource = Find(handle);
        if (!resource || !resource->texture || !resource->uploaded || !inFrame_ ||
            !linePipelineReady_ || slot != 0)
            return false;
        if (FAILED(device_->SetTexture(slot, resource->texture)))
            return false;
        boundTexture_ = handle;
        return true;
    }
    bool Upload(Handle handle, std::size_t offset, const void* data, std::size_t size) override {
        auto* resource = Find(handle);
        return resource && !resetPending_ && resource->description.dynamic && !inFrame_ &&
               Copy(*resource, offset, data, size);
    }
    bool Destroy(Handle handle) override {
        auto* resource = Find(handle);
        if (!resource || inFrame_)
            return false;
        if (resource->texture && boundTexture_.device == handle.device &&
            boundTexture_.index == handle.index && boundTexture_.generation == handle.generation) {
            if (FAILED(device_->SetTexture(0, nullptr)))
                return false;
            boundTexture_ = {};
        }
        resource->Release();
        resource->alive = false;
        if (++resource->generation == 0)
            ++resource->generation;
        return true;
    }
    bool BeginFrame(ClearColor color) override {
        if (inFrame_ || ready_ || resetPending_ || !ValidColor(color))
            return false;
        const auto channel = [](float value) { return static_cast<DWORD>(value * 255.0f + 0.5f); };
        const D3DCOLOR clear = D3DCOLOR_ARGB(channel(color.alpha), channel(color.red),
                                             channel(color.green), channel(color.blue));
        if (FAILED(
                device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.0f, 0)) ||
            FAILED(device_->BeginScene()))
            return false;
        inFrame_ = true;
        linePipelineReady_ = false;
        return true;
    }
    bool SetFixedPipeline(FixedPipelineState pipeline) override {
        linePipelineReady_ = false;
        const bool lines = pipeline.topology == PrimitiveTopology::LineList &&
                           pipeline.layout == VertexLayout::ScreenPositionColor;
        const bool texturedTriangles = pipeline.topology == PrimitiveTopology::TriangleList &&
                                       pipeline.layout == VertexLayout::ScreenPositionColorUv;
        if (!inFrame_ || (!lines && !texturedTriangles) || !Compare(pipeline.depthCompare) ||
            !Compare(pipeline.alphaCompare) || !Compare(pipeline.stencilCompare) ||
            !Filter(pipeline.sampler.min) || !Filter(pipeline.sampler.mag) ||
            !Filter(pipeline.sampler.mip) || !Address(pipeline.sampler.u) ||
            !Address(pipeline.sampler.v) || pipeline.sampler.maxAnisotropy == 0 ||
            (pipeline.cull != RasterCull::None && pipeline.cull != RasterCull::CounterClockwise) ||
            (pipeline.colorStage != FixedColorStage::DiffuseOnly &&
             pipeline.colorStage != FixedColorStage::TextureDiffuseModulate) ||
            (pipeline.stencilPass != StencilOperation::Keep &&
             pipeline.stencilPass != StencilOperation::Replace))
            return false;
        const bool legacyStage = pipeline.colorStage == FixedColorStage::TextureDiffuseModulate;
        linePipelineReady_ =
            SUCCEEDED(device_->SetVertexShader(nullptr)) &&
            SUCCEEDED(device_->SetPixelShader(nullptr)) &&
            SUCCEEDED(device_->SetTexture(0, nullptr)) &&
            SUCCEEDED(device_->SetTextureStageState(
                0, D3DTSS_COLOROP, legacyStage ? D3DTOP_MODULATE : D3DTOP_SELECTARG1)) &&
            SUCCEEDED(device_->SetTextureStageState(0, D3DTSS_COLORARG1,
                                                    legacyStage ? D3DTA_TEXTURE : D3DTA_DIFFUSE)) &&
            SUCCEEDED(device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE)) &&
            SUCCEEDED(device_->SetTextureStageState(
                0, D3DTSS_ALPHAOP, legacyStage ? D3DTOP_MODULATE : D3DTOP_SELECTARG1)) &&
            SUCCEEDED(device_->SetTextureStageState(0, D3DTSS_ALPHAARG1,
                                                    legacyStage ? D3DTA_TEXTURE : D3DTA_DIFFUSE)) &&
            SUCCEEDED(device_->SetTextureStageState(0, D3DTSS_ALPHAARG2,
                                                    legacyStage ? D3DTA_CURRENT : D3DTA_DIFFUSE)) &&
            SUCCEEDED(device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE)) &&
            SUCCEEDED(device_->SetTextureStageState(
                1, D3DTSS_ALPHAOP, legacyStage ? D3DTOP_MODULATE : D3DTOP_DISABLE)) &&
            SUCCEEDED(
                device_->SetSamplerState(0, D3DSAMP_MINFILTER, Filter(pipeline.sampler.min))) &&
            SUCCEEDED(
                device_->SetSamplerState(0, D3DSAMP_MAGFILTER, Filter(pipeline.sampler.mag))) &&
            SUCCEEDED(
                device_->SetSamplerState(0, D3DSAMP_MIPFILTER, Filter(pipeline.sampler.mip))) &&
            SUCCEEDED(device_->SetSamplerState(0, D3DSAMP_ADDRESSU, Address(pipeline.sampler.u))) &&
            SUCCEEDED(device_->SetSamplerState(0, D3DSAMP_ADDRESSV, Address(pipeline.sampler.v))) &&
            SUCCEEDED(device_->SetSamplerState(0, D3DSAMP_MAXANISOTROPY,
                                               pipeline.sampler.maxAnisotropy)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_LIGHTING, pipeline.lighting ? TRUE : FALSE)) &&
            SUCCEEDED(device_->SetRenderState(
                D3DRS_CULLMODE,
                pipeline.cull == RasterCull::CounterClockwise ? D3DCULL_CCW : D3DCULL_NONE)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ZENABLE, pipeline.depthTest ? TRUE : FALSE)) &&
            SUCCEEDED(
                device_->SetRenderState(D3DRS_ZWRITEENABLE, pipeline.depthWrite ? TRUE : FALSE)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ZFUNC, Compare(pipeline.depthCompare))) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ALPHATESTENABLE,
                                              pipeline.alphaTest ? TRUE : FALSE)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ALPHAFUNC, Compare(pipeline.alphaCompare))) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ALPHAREF, pipeline.alphaReference)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILENABLE,
                                              pipeline.stencilTest ? TRUE : FALSE)) &&
            SUCCEEDED(
                device_->SetRenderState(D3DRS_STENCILFUNC, Compare(pipeline.stencilCompare))) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILREF, pipeline.stencilReference)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILMASK, pipeline.stencilReadMask)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILWRITEMASK, pipeline.stencilWriteMask)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_STENCILPASS,
                                              pipeline.stencilPass == StencilOperation::Replace
                                                  ? D3DSTENCILOP_REPLACE
                                                  : D3DSTENCILOP_KEEP)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_ALPHABLENDENABLE,
                                              pipeline.alphaBlend ? TRUE : FALSE)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_COLORWRITEENABLE,
                                              D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                                  D3DCOLORWRITEENABLE_BLUE |
                                                  D3DCOLORWRITEENABLE_ALPHA)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_DEPTHBIAS, 0)) &&
            SUCCEEDED(device_->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0)) &&
            SUCCEEDED(device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE |
                                      (texturedTriangles ? D3DFVF_TEX1 : 0)));
        if (linePipelineReady_)
            currentPipeline_ = pipeline;
        boundTexture_ = {};
        return linePipelineReady_;
    }
    bool Draw(NonIndexedDraw draw) override {
        if (!IsCurrentSubmission(Status(), draw.submission))
            return false;
        if (draw.layout && *draw.layout != currentPipeline_.layout)
            return false;
        auto* vertex = Find(draw.vertices);
        const bool triangles = currentPipeline_.topology == PrimitiveTopology::TriangleList;
        const UINT stride = triangles ? 28u : 20u;
        const UINT verticesPerPrimitive = triangles ? 3u : 2u;
        if (!inFrame_ || !linePipelineReady_ || (triangles && !Find(boundTexture_)) || !vertex ||
            !vertex->vertices || !draw.vertexCount || draw.vertexCount % verticesPerPrimitive ||
            draw.firstVertex > vertex->description.size / stride ||
            draw.vertexCount > vertex->description.size / stride - draw.firstVertex)
            return false;
        if (FAILED(device_->SetStreamSource(0, vertex->vertices, 0, stride)))
            return false;
        if (borrowedDraw_) {
            borrowedResult_ =
                borrowedDraw_(device_, draw.firstVertex, draw.vertexCount / verticesPerPrimitive);
            return SUCCEEDED(borrowedResult_);
        }
        return SUCCEEDED(device_->DrawPrimitive(triangles ? D3DPT_TRIANGLELIST : D3DPT_LINELIST,
                                                draw.firstVertex,
                                                draw.vertexCount / verticesPerPrimitive));
    }
    bool DrawBorrowedBatch(const void* vertices, std::size_t bytes, FixedPipelineState pipeline,
                           TextureDesc textureDescription, TexturePixels texturePixels,
                           InSceneLineDraw callback, long* result, bool readOnlyDepthStencil,
                           Handle suppliedTexture = {}) {
        if (result)
            *result = E_FAIL;
        const bool triangles = pipeline.topology == PrimitiveTopology::TriangleList &&
                               pipeline.layout == VertexLayout::ScreenPositionColorUv;
        const std::size_t stride = triangles ? 28u : 20u;
        if (!vertices || bytes == 0 ||
            bytes > static_cast<std::size_t>(std::numeric_limits<UINT>::max()) ||
            bytes % (stride * (triangles ? 3u : 2u)) != 0 || !callback ||
            (triangles && !texturePixels.data && !suppliedTexture) ||
            (suppliedTexture && (!Find(suppliedTexture) || !Find(suppliedTexture)->uploaded)) ||
            inFrame_ || resetPending_)
            return false;
        Handle texture = suppliedTexture;
        const bool ownsTexture = !suppliedTexture;
        if (triangles && ownsTexture) {
            texture = CreateTexture(textureDescription);
            if (!texture || !UploadTexture(texture, texturePixels)) {
                if (texture)
                    Destroy(texture);
                return false;
            }
        }
        const Handle buffer = CreateBuffer({BufferUsage::Vertex, bytes, false}, vertices);
        if (!buffer) {
            if (ownsTexture && texture)
                Destroy(texture);
            return false;
        }
        IDirect3DStateBlock9* previous = nullptr;
        const bool captured = SUCCEEDED(device_->CreateStateBlock(D3DSBT_ALL, &previous)) &&
                              previous && SUCCEEDED(previous->Capture());
        if (!captured) {
            if (previous)
                previous->Release();
            Destroy(buffer);
            if (ownsTexture && texture)
                Destroy(texture);
            return false;
        }
        inFrame_ = true;
        borrowedDraw_ = std::move(callback);
        borrowedResult_ = E_FAIL;
        const bool configured =
            SetFixedPipeline(pipeline) && (!triangles || BindTexture(0, texture)) &&
            (!readOnlyDepthStencil ||
             (SUCCEEDED(device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) &&
              SUCCEEDED(device_->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP)) &&
              SUCCEEDED(device_->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP)) &&
              SUCCEEDED(device_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP))));
        const bool drawn =
            configured &&
            Draw(Record(NonIndexedDraw{buffer, 0, static_cast<std::uint32_t>(bytes / stride)}));
        borrowedDraw_ = {};
        inFrame_ = false;
        linePipelineReady_ = false;
        if (result)
            *result = borrowedResult_;
        const bool restored = SUCCEEDED(previous->Apply());
        previous->Release();
        boundTexture_ = {};
        const bool destroyed = Destroy(buffer);
        const bool textureDestroyed = !ownsTexture || !texture || Destroy(texture);
        return drawn && restored && destroyed && textureDestroyed;
    }
    bool DrawBorrowedModel(const void* vertices, std::size_t vertexBytes, const void* indices,
                           std::size_t indexBytes, std::uint32_t first, std::uint32_t count,
                           BatchRasterState raster, InSceneIndexedDraw callback, long* result,
                           bool readOnly, bool* issued, std::array<Handle, 3> ownedTextures = {},
                           const MaterialShaderInputs* shaderInputs = nullptr) {
        if (result)
            *result = E_FAIL;
        if (issued)
            *issued = false;
        const auto recordedStatus = Status();
        const SubmissionIdentity submission{recordedStatus.identity, recordedStatus.epoch};
        if (!vertices || !indices || !callback || inFrame_ || resetPending_ || !vertexBytes ||
            vertexBytes % 32 || indexBytes % 2 || !count || count % 3 || first > indexBytes / 2 ||
            count > indexBytes / 2 - first || vertexBytes > std::numeric_limits<UINT>::max() ||
            indexBytes > std::numeric_limits<UINT>::max())
            return false;
        for (std::size_t i = first; i < first + count; ++i) {
            std::uint16_t index = 0;
            std::memcpy(&index, static_cast<const unsigned char*>(indices) + i * 2, 2);
            if (index >= vertexBytes / 32)
                return false;
        }
        // Stable portable enum -> explicit DX9 mapping, no Windows tokens
        // cross the BatchRasterState boundary.
        constexpr DWORD comparisons[] = {D3DCMP_NEVER,        D3DCMP_LESS,    D3DCMP_EQUAL,
                                         D3DCMP_LESSEQUAL,    D3DCMP_GREATER, D3DCMP_NOTEQUAL,
                                         D3DCMP_GREATEREQUAL, D3DCMP_ALWAYS};
        constexpr DWORD blends[] = {
            D3DBLEND_ZERO,      D3DBLEND_ONE,          D3DBLEND_SRCCOLOR,   D3DBLEND_INVSRCCOLOR,
            D3DBLEND_SRCALPHA,  D3DBLEND_INVSRCALPHA,  D3DBLEND_DESTALPHA,  D3DBLEND_INVDESTALPHA,
            D3DBLEND_DESTCOLOR, D3DBLEND_INVDESTCOLOR, D3DBLEND_SRCALPHASAT};
        constexpr DWORD culls[] = {D3DCULL_NONE, D3DCULL_CW, D3DCULL_CCW};
        const auto comparison = static_cast<unsigned>(raster.depthCompare);
        const auto source = static_cast<unsigned>(raster.sourceBlend);
        const auto destination = static_cast<unsigned>(raster.destinationBlend);
        const auto cull = static_cast<unsigned>(raster.cull);
        if (comparison >= 8 || source >= 11 || destination >= 11 || cull >= 3)
            return false;
        // Buffer creation can grow resources_, so retain COM pointers, not entries.
        std::array<IDirect3DTexture9*, 3> textures{};
        for (unsigned stage = 0; stage < 3; ++stage)
            if (ownedTextures[stage]) {
                const auto* entry = Find(ownedTextures[stage]);
                if (!entry || !entry->texture || !entry->uploaded)
                    return false;
                textures[stage] = entry->texture;
            }
        const auto vb = CreateBuffer({BufferUsage::Vertex, vertexBytes}, vertices);
        const auto ib = CreateBuffer({BufferUsage::Index, indexBytes}, indices);
        if (!vb || !ib) {
            if (vb)
                Destroy(vb);
            if (ib)
                Destroy(ib);
            return false;
        }
        IDirect3DStateBlock9* previous = nullptr;
        const bool captured = SUCCEEDED(device_->CreateStateBlock(D3DSBT_ALL, &previous)) &&
                              previous && SUCCEEDED(previous->Capture());
        bool configured = captured;
        if (shaderInputs) {
            configured = configured && ValidateMaterialShaderInputs(*shaderInputs);
            const auto& s = *shaderInputs;
            for (auto pair : {std::pair{D3DTS_WORLD, &s.world}, std::pair{D3DTS_VIEW, &s.view},
                              std::pair{D3DTS_PROJECTION, &s.projection}})
                if (configured)
                    configured = SUCCEEDED(device_->SetTransform(
                        pair.first, reinterpret_cast<const D3DMATRIX*>(pair.second->data())));
            const auto color = [](const auto& c) { return D3DCOLORVALUE{c[0], c[1], c[2], c[3]}; };
            D3DMATERIAL9 material{};
            material.Diffuse = color(s.nativeDiffuse);
            material.Ambient = color(s.materialAmbient);
            material.Specular = color(s.materialSpecular);
            material.Emissive = color(s.emissive);
            material.Power = s.shininess;
            if (configured)
                configured = SUCCEEDED(device_->SetMaterial(&material));
            for (unsigned i = 0; i < 8 && configured; ++i) {
                if (s.lights[i].enabled) {
                    D3DLIGHT9 light{};
                    light.Type = D3DLIGHT_DIRECTIONAL;
                    const auto& l = s.lights[i];
                    light.Direction = {l.direction[0], l.direction[1], l.direction[2]};
                    light.Diffuse = color(l.diffuse);
                    light.Ambient = color(l.ambient);
                    light.Specular = color(l.specular);
                    configured = SUCCEEDED(device_->SetLight(i, &light)) &&
                                 SUCCEEDED(device_->LightEnable(i, TRUE));
                } else {
                    BOOL enabled = FALSE;
                    if (SUCCEEDED(device_->GetLightEnable(i, &enabled)) && enabled)
                        configured = SUCCEEDED(device_->LightEnable(i, FALSE));
                }
            }
        }
        const auto state = [&](D3DRENDERSTATETYPE key, DWORD value) {
            if (configured)
                configured = SUCCEEDED(device_->SetRenderState(key, value));
        };
        if (shaderInputs) {
            const auto& s = *shaderInputs;
            state(D3DRS_LIGHTING, s.lighting);
            state(D3DRS_NORMALIZENORMALS, s.normalizeNormals);
            state(D3DRS_SPECULARENABLE, s.specularEnabled);
            state(D3DRS_LOCALVIEWER, s.localViewer);
            state(D3DRS_COLORVERTEX, s.colorVertex);
            const auto byte = [](float v) {
                return static_cast<DWORD>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
            };
            state(D3DRS_AMBIENT, D3DCOLOR_ARGB(byte(s.globalAmbient[3]), byte(s.globalAmbient[0]),
                                               byte(s.globalAmbient[1]), byte(s.globalAmbient[2])));
            unsigned j = 0;
            for (auto key : {D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
                             D3DRS_SPECULARMATERIALSOURCE, D3DRS_EMISSIVEMATERIALSOURCE})
                state(key, s.materialSources[j++]);
            state(D3DRS_ALPHATESTENABLE, s.alphaTest);
            state(D3DRS_ALPHAFUNC, static_cast<DWORD>(s.alphaCompare) + 1);
            state(D3DRS_ALPHAREF, s.alphaReference);
            for (unsigned i = 0; i < 3 && configured; ++i) {
                const auto& stage = s.layout.stages[i];
                if (stage.role == MaterialTextureRole::None)
                    continue;
                const auto arg = [](MaterialColorSource a) {
                    return a == MaterialColorSource::Texture      ? D3DTA_TEXTURE
                           : a == MaterialColorSource::LitDiffuse ? D3DTA_DIFFUSE
                                                                  : D3DTA_CURRENT;
                };
                for (auto pair :
                     {std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{
                          D3DTSS_COLOROP,
                          stage.color == MaterialCombine::Add ? D3DTOP_ADD : D3DTOP_MODULATE},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_COLORARG1,
                                                                 arg(stage.colorArg1)},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_COLORARG2,
                                                                 arg(stage.colorArg2)},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_ALPHAOP, D3DTOP_MODULATE},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_ALPHAARG1,
                                                                 arg(stage.alphaArg1)},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_ALPHAARG2,
                                                                 arg(stage.alphaArg2)},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{
                          D3DTSS_TEXCOORDINDEX,
                          stage.coordinates == MaterialCoordinates::CameraNormal
                              ? D3DTSS_TCI_CAMERASPACENORMAL
                          : stage.coordinates == MaterialCoordinates::MeshUv1 ? 1u
                                                                              : 0u},
                      std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{
                          D3DTSS_TEXTURETRANSFORMFLAGS,
                          stage.coordinates == MaterialCoordinates::MeshUv0 ? 0u : 2u}})
                    if (configured)
                        configured =
                            SUCCEEDED(device_->SetTextureStageState(i, pair.first, pair.second));
                if (configured)
                    configured = SUCCEEDED(device_->SetTransform(
                        static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + i),
                        reinterpret_cast<const D3DMATRIX*>(s.textureTransforms[i].data())));
                const auto& sampler = s.samplers[i];
                for (auto pair :
                     {std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_MINFILTER,
                                                            static_cast<DWORD>(sampler.min) + 1},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_MAGFILTER,
                                                            static_cast<DWORD>(sampler.mag) + 1},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_MIPFILTER,
                                                            static_cast<DWORD>(sampler.mip) + 1},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{
                          D3DSAMP_ADDRESSU, sampler.u == SamplerAddress::Wrap ? 1u : 3u},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{
                          D3DSAMP_ADDRESSV, sampler.v == SamplerAddress::Wrap ? 1u : 3u},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_MAXANISOTROPY,
                                                            sampler.maxAnisotropy},
                      std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_SRGBTEXTURE,
                                                            s.srgbSample[i] ? 1u : 0u}})
                    if (configured)
                        configured =
                            SUCCEEDED(device_->SetSamplerState(i, pair.first, pair.second));
            }
        }
        state(D3DRS_ZENABLE, raster.depthTest ? D3DZB_TRUE : D3DZB_FALSE);
        state(D3DRS_ZWRITEENABLE, raster.depthWrite && !readOnly);
        state(D3DRS_ZFUNC, comparisons[comparison]);
        state(D3DRS_ALPHABLENDENABLE, raster.alphaBlend);
        state(D3DRS_SRCBLEND, blends[source]);
        state(D3DRS_DESTBLEND, blends[destination]);
        state(D3DRS_CULLMODE, culls[cull]);
        if (readOnly) {
            state(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            state(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
            state(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
        }
        if (configured)
            configured = SUCCEEDED(device_->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)) &&
                         SUCCEEDED(device_->SetStreamSource(0, Find(vb)->vertices, 0, 32)) &&
                         SUCCEEDED(device_->SetIndices(Find(ib)->indices));
        for (unsigned stage = 0; stage < 3; ++stage)
            if (configured && textures[stage])
                configured = SUCCEEDED(device_->SetTexture(stage, textures[stage]));
        long hr = E_FAIL;
        if (configured && IsCurrentSubmission(Status(), submission)) {
            if (issued)
                *issued = true;
            hr = callback(device_, static_cast<unsigned>(vertexBytes / 32), first, count / 3);
        }
        if (result)
            *result = hr;
        const bool restored = captured && SUCCEEDED(previous->Apply());
        if (previous)
            previous->Release();
        Destroy(vb);
        Destroy(ib);
        return configured && SUCCEEDED(hr) && restored;
    }
    bool DrawIndexed(IndexedDraw draw) override {
        if (!IsCurrentSubmission(Status(), draw.submission))
            return false;
        auto* vertex = Find(draw.vertices);
        auto* index = Find(draw.indices);
        const UINT stride = draw.layout == VertexLayout::PositionColor ? 16 : 32;
        const DWORD fvf = draw.layout == VertexLayout::PositionColor
                              ? D3DFVF_XYZ | D3DFVF_DIFFUSE
                              : D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;
        if (!inFrame_ || linePipelineReady_ || draw.layout == VertexLayout::ScreenPositionColor ||
            !vertex || !index || !vertex->vertices || !index->indices || !draw.vertexCount ||
            !draw.indexCount || draw.indexCount % 3 ||
            draw.vertexCount > vertex->description.size / stride ||
            draw.firstIndex >
                index->description.size /
                    (index->description.indexFormat == IndexFormat::UInt32 ? 4u : 2u) ||
            draw.indexCount >
                index->description.size /
                        (index->description.indexFormat == IndexFormat::UInt32 ? 4u : 2u) -
                    draw.firstIndex)
            return false;
        return SUCCEEDED(device_->SetStreamSource(0, vertex->vertices, 0, stride)) &&
               SUCCEEDED(device_->SetIndices(index->indices)) && SUCCEEDED(device_->SetFVF(fvf)) &&
               SUCCEEDED(device_->SetRenderState(D3DRS_LIGHTING, FALSE)) &&
               SUCCEEDED(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, draw.vertexCount,
                                                       draw.firstIndex, draw.indexCount / 3));
    }
    bool EndFrame() override {
        if (!inFrame_)
            return false;
        const auto result = device_->EndScene();
        inFrame_ = false;
        linePipelineReady_ = false;
        ready_ = SUCCEEDED(result);
        return ready_;
    }
    bool Present() override {
        if (!ready_)
            return false;
        ready_ = false;
        return SUCCEEDED(device_->Present(nullptr, nullptr, nullptr, nullptr));
    }
    bool PrepareForReset() override {
        if (inFrame_ || resetPending_)
            return false;
        // Lost devices may reject unbinding. Default-pool references still
        // have to be released before Reset can recover the native device.
        const auto canRelease = [](HRESULT hr) {
            return SUCCEEDED(hr) || hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
        };
        if (!canRelease(device_->SetStreamSource(0, nullptr, 0, 0)) ||
            !canRelease(device_->SetIndices(nullptr)))
            return false;
        for (auto& resource : resources_)
            if (resource.alive && resource.description.dynamic) {
                resource.Release();
                resource.alive = false;
                if (++resource.generation == 0)
                    ++resource.generation;
            }
        ready_ = false;
        linePipelineReady_ = false;
        resetPending_ = true;
        return true;
    }
    bool RestoreAfterReset() override {
        if (!resetPending_ || device_->TestCooperativeLevel() != D3D_OK)
            return false;
        resetPending_ = false;
        if (++epoch_ == 0)
            ++epoch_;
        return true;
    }

private:
    static bool ValidColor(ClearColor color) {
        for (float channel : {color.red, color.green, color.blue, color.alpha})
            if (!std::isfinite(channel) || channel < 0 || channel > 1)
                return false;
        return true;
    }
    static bool Copy(Resource& resource, std::size_t offset, const void* data, std::size_t size) {
        if ((!data && size) || offset > resource.description.size ||
            size > resource.description.size - offset)
            return false;
        if (!size)
            return true;
        void* destination = nullptr;
        const DWORD flags =
            resource.description.dynamic && offset == 0 && size == resource.description.size
                ? D3DLOCK_DISCARD
                : 0;
        const HRESULT lock =
            resource.vertices
                ? resource.vertices->Lock(static_cast<UINT>(offset), static_cast<UINT>(size),
                                          &destination, flags)
                : resource.indices->Lock(static_cast<UINT>(offset), static_cast<UINT>(size),
                                         &destination, flags);
        if (FAILED(lock))
            return false;
        std::memcpy(destination, data, size);
        return SUCCEEDED(resource.vertices ? resource.vertices->Unlock()
                                           : resource.indices->Unlock());
    }
    Handle Insert(Resource resource) {
        for (std::size_t index = 0; index < resources_.size(); ++index)
            if (!resources_[index].alive) {
                resource.generation = resources_[index].generation;
                resources_[index] = resource;
                return {static_cast<std::uint32_t>(index + 1), resource.generation, identity_};
            }
        if (resources_.size() >= std::numeric_limits<std::uint32_t>::max()) {
            resource.Release();
            return {};
        }
        resources_.push_back(resource);
        return {static_cast<std::uint32_t>(resources_.size()), 1, identity_};
    }
    Resource* Find(Handle handle) {
        if (!handle || handle.device != identity_ || handle.index > resources_.size() ||
            !handle.index)
            return nullptr;
        auto& resource = resources_[handle.index - 1];
        return resource.alive && resource.generation == handle.generation ? &resource : nullptr;
    }
    IDirect3DDevice9* device_;
    std::vector<Resource> resources_;
    const std::uint64_t identity_ = detail::NextDeviceIdentity();
    std::uint64_t epoch_ = 1;
    bool inFrame_ = false, ready_ = false;
    bool resetPending_ = false, linePipelineReady_ = false;
    FixedPipelineState currentPipeline_{};
    InSceneLineDraw borrowedDraw_;
    Handle boundTexture_{};
    long borrowedResult_ = E_FAIL;
};

class D3D9BorrowedModelTextureCache final : public BorrowedModelTextureCache {
public:
    explicit D3D9BorrowedModelTextureCache(IDirect3DDevice9* device)
        : device_(device), cache_(device_) {}
    bool DrawMaterial(const std::array<const MaterialTextureAsset*, 3>& assets,
                      const void* vertices, std::size_t vertexBytes, const void* indices,
                      std::size_t indexBytes, std::uint32_t first, std::uint32_t count,
                      BatchRasterState raster, InSceneIndexedDraw draw, long* result, bool readOnly,
                      bool* issued, const MaterialShaderInputs* shaderInputs) override {
        if (result)
            *result = E_FAIL;
        if (issued)
            *issued = false;
        std::array<Handle, 3> textures{};
        for (unsigned stage = 0; stage < 3; ++stage)
            if (assets[stage]) {
                textures[stage] = cache_.Get(*assets[stage]);
                if (!textures[stage])
                    return false;
            }
        return device_.DrawBorrowedModel(vertices, vertexBytes, indices, indexBytes, first, count,
                                         raster, std::move(draw), result, readOnly, issued,
                                         textures, shaderInputs);
    }
    OverlayCacheStats Stats() const override { return {cache_.Uploads(), cache_.Hits()}; }

private:
    D3D9Device device_;
    MaterialTextureCache cache_;
};
class D3D9BorrowedOverlayCache final : public BorrowedOverlayCache {
    struct Entry {
        Handle texture{};
        TextureDesc description{};
        std::uint64_t revision = 0;
        OverlayCacheStats stats{};
    };

public:
    explicit D3D9BorrowedOverlayCache(IDirect3DDevice9* device) : device_(device) {}
    bool Draw(OverlayAtlasSlot slot, std::uint64_t revision, TextureDesc description,
              TexturePixels pixels, const void* vertices, std::size_t bytes, SamplerDesc sampler,
              InSceneLineDraw draw, long* drawResult, bool readOnlyDepthStencil) override {
        if (drawResult)
            *drawResult = E_FAIL;
        if (revision == 0 || description.format != TextureFormat::Rgba8)
            return false;
        auto& entry = entries_[slot == OverlayAtlasSlot::Text ? 1 : 0];
        const bool hit = entry.texture && entry.revision == revision &&
                         entry.description.width == description.width &&
                         entry.description.height == description.height &&
                         entry.description.format == description.format;
        if (hit) {
            ++entry.stats.hits;
        } else {
            if (!pixels.data)
                return false;
            const Handle next = device_.CreateTexture(description);
            if (!next)
                return false;
            if (!device_.UploadTexture(next, pixels)) {
                device_.Destroy(next);
                return false;
            }
            if (entry.texture && !device_.Destroy(entry.texture)) {
                device_.Destroy(next);
                return false;
            }
            entry.texture = next;
            entry.description = description;
            entry.revision = revision;
            ++entry.stats.uploads;
        }
        auto pipeline = HudSpritePipeline();
        pipeline.sampler = sampler;
        return device_.DrawBorrowedBatch(vertices, bytes, pipeline, {}, {}, std::move(draw),
                                         drawResult, readOnlyDepthStencil, entry.texture);
    }
    OverlayCacheStats Stats(OverlayAtlasSlot slot) const override {
        return entries_[slot == OverlayAtlasSlot::Text ? 1 : 0].stats;
    }

private:
    D3D9Device device_;
    Entry entries_[2]{};
};
std::unordered_map<IDirect3DDevice9*, std::unique_ptr<BorrowedOverlayCache>> g_applicationCaches;
}
std::unique_ptr<Device> CreateD3D9Device(IDirect3DDevice9* device) {
    if (!device)
        return {};
    return std::make_unique<D3D9Device>(device);
}
std::unique_ptr<BorrowedModelTextureCache>
CreateBorrowedModelTextureCache(IDirect3DDevice9* device) {
    if (!device)
        return {};
    return std::make_unique<D3D9BorrowedModelTextureCache>(device);
}
std::unique_ptr<BorrowedOverlayCache> CreateBorrowedOverlayCache(IDirect3DDevice9* device) {
    if (!device)
        return {};
    return std::make_unique<D3D9BorrowedOverlayCache>(device);
}
void ForgetApplicationOverlayCache(IDirect3DDevice9* device) {
    g_applicationCaches.erase(device);
}
OverlayCacheStats ApplicationOverlayCacheStats(IDirect3DDevice9* device, OverlayAtlasSlot slot) {
    const auto found = g_applicationCaches.find(device);
    return found == g_applicationCaches.end() ? OverlayCacheStats{} : found->second->Stats(slot);
}
bool DrawCachedHudSpriteInScene(IDirect3DDevice9* device, OverlayAtlasSlot slot,
                                std::uint64_t revision, TextureDesc texture, TexturePixels pixels,
                                const void* vertices, std::size_t bytes, SamplerDesc sampler,
                                InSceneLineDraw draw, long* drawResult, bool readOnlyDepthStencil) {
    if (drawResult)
        *drawResult = E_FAIL;
    if (!device)
        return false;
    auto& cache = g_applicationCaches[device];
    if (!cache)
        cache = CreateBorrowedOverlayCache(device);
    return cache && cache->Draw(slot, revision, texture, pixels, vertices, bytes, sampler,
                                std::move(draw), drawResult, readOnlyDepthStencil);
}
bool DrawModelBatchInScene(IDirect3DDevice9* device, const void* vertices, std::size_t vertexBytes,
                           const void* indices, std::size_t indexBytes, std::uint32_t first,
                           std::uint32_t count, BatchRasterState raster, InSceneIndexedDraw draw,
                           long* result, bool readOnly, bool* issued) {
    if (issued)
        *issued = false;
    if (!device) {
        if (result)
            *result = E_FAIL;
        return false;
    }
    D3D9Device adapter(device);
    return adapter.DrawBorrowedModel(vertices, vertexBytes, indices, indexBytes, first, count,
                                     raster, std::move(draw), result, readOnly, issued);
}
bool DrawSelectionBoxInScene(IDirect3DDevice9* device, const void* vertices, std::size_t bytes,
                             InSceneLineDraw draw, long* drawResult, bool readOnlyDepthStencil) {
    if (!device)
        return false;
    D3D9Device adapter(device);
    return adapter.DrawBorrowedBatch(vertices, bytes, SelectionBoxPipeline(), {}, {},
                                     std::move(draw), drawResult, readOnlyDepthStencil);
}
bool DrawHudSpriteInScene(IDirect3DDevice9* device, TextureDesc texture, TexturePixels pixels,
                          const void* vertices, std::size_t bytes, SamplerDesc sampler,
                          InSceneLineDraw draw, long* drawResult, bool readOnlyDepthStencil) {
    if (!device)
        return false;
    D3D9Device adapter(device);
    auto pipeline = HudSpritePipeline();
    pipeline.sampler = sampler;
    return adapter.DrawBorrowedBatch(vertices, bytes, pipeline, texture, pixels, std::move(draw),
                                     drawResult, readOnlyDepthStencil);
}
}
