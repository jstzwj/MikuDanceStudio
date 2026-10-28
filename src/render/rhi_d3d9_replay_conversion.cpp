#include "mikudancestudio/rhi_d3d9_replay_conversion.hpp"
#include <cstring>
#include <limits>
namespace mikudancestudio::rhi {
ModelReplayResult AppendD3D9ReplaySnapshot(ModelReplayStream& stream, const D3D9ReplaySnapshot& c,
                                           bool allowPositionColor) {
    ModelReplayResult result;
    result.sequence = stream.events.size() + 1;
    auto fail = [&](const char* why) {
        result.reason = why;
        return result;
    };
    bool missing = false;
    auto state = [&](const std::vector<D3D9ReplayState>& states, unsigned stage, unsigned key) {
        for (const auto& s : states)
            if (s.stage == stage && s.key == key)
                return s.value;
        missing = true;
        return DWORD(0);
    };
    auto rs = [&](unsigned k) { return state(c.renderStates, 0, k); };
    auto ts = [&](unsigned s, unsigned k) { return state(c.stageStates, s, k); };
    auto ss = [&](unsigned s, unsigned k) { return state(c.samplerStates, s, k); };
    const bool positionColor = c.fvf == (D3DFVF_XYZ | D3DFVF_DIFFUSE) && c.stride == 16;
    const unsigned arity = c.type == D3DPT_TRIANGLELIST ? 3 : c.type == D3DPT_LINELIST ? 2 : 0;
    if (!arity ||
        !(positionColor ||
          (c.fvf == (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1) && c.stride == 32)) ||
        c.offset || c.base)
        return fail("unsupported_geometry_layout_or_base");
    if (positionColor && !allowPositionColor)
        return fail("position_color_not_enabled");
    const ModelViewport viewport{c.viewport.X,      c.viewport.Y,    c.viewport.Width,
                                 c.viewport.Height, c.viewport.MinZ, c.viewport.MaxZ};
    if (!ValidModelViewport(viewport, stream.target.width, stream.target.height))
        return fail("invalid_viewport");
    for (auto k : {D3DRS_FOGENABLE, D3DRS_DITHERENABLE, D3DRS_VERTEXBLEND, D3DRS_CLIPPLANEENABLE,
                   D3DRS_SCISSORTESTENABLE, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_DEPTHBIAS,
                   D3DRS_SRGBWRITEENABLE, D3DRS_TWOSIDEDSTENCILMODE})
        if (rs(k))
            return fail("unsupported_render_state");
    if (rs(D3DRS_FILLMODE) != D3DFILL_SOLID || rs(D3DRS_SHADEMODE) != D3DSHADE_GOURAUD ||
        rs(D3DRS_ZENABLE) > 1 || rs(D3DRS_MULTISAMPLEMASK) != 0xffffffffu)
        return fail("unsupported_raster_mode");
    ModelReplayDraw draw;
    auto& p = draw.pipeline;
    auto& s = p.shader;
    p.vertexLayout = positionColor ? VertexLayout::PositionColor : VertexLayout::PositionNormalUv;
    p.topology =
        c.type == D3DPT_LINELIST ? PrimitiveTopology::LineList : PrimitiveTopology::TriangleList;
    p.viewport = viewport;
    auto color = [](D3DCOLORVALUE v) { return std::array<float, 4>{v.r, v.g, v.b, v.a}; };
    std::memcpy(s.world.data(), &c.world, 64);
    std::memcpy(s.view.data(), &c.view, 64);
    std::memcpy(s.projection.data(), &c.projection, 64);
    if (positionColor)
        s.normalView = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    else if (!BuildShaderNormalMatrix(s.world, s.view, s.normalView))
        return fail("invalid_normal_matrix");
    s.nativeDiffuse = color(c.material.Diffuse);
    s.materialAmbient = color(c.material.Ambient);
    s.materialSpecular = color(c.material.Specular);
    s.emissive = color(c.material.Emissive);
    s.shininess = c.material.Power;
    const auto ambient = rs(D3DRS_AMBIENT);
    s.globalAmbient = {((ambient >> 16) & 255) / 255.f, ((ambient >> 8) & 255) / 255.f,
                       (ambient & 255) / 255.f, ((ambient >> 24) & 255) / 255.f};
    s.lighting = rs(D3DRS_LIGHTING) != 0;
    s.normalizeNormals = rs(D3DRS_NORMALIZENORMALS) != 0;
    s.specularEnabled = rs(D3DRS_SPECULARENABLE) != 0;
    // Without lighting this FVF supplies no COLOR0: DX9's diffuse is white,
    // independent of D3DMATERIAL9 (including its alpha).
    if (!s.lighting && !positionColor)
        s.nativeDiffuse = {1, 1, 1, 1};
    s.localViewer = rs(D3DRS_LOCALVIEWER) != 0;
    s.colorVertex = rs(D3DRS_COLORVERTEX) != 0;
    unsigned source = 0;
    for (auto k : {D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
                   D3DRS_SPECULARMATERIALSOURCE, D3DRS_EMISSIVEMATERIALSOURCE}) {
        const auto v = rs(k);
        if (v > 2)
            return fail("unsupported_material_source");
        s.materialSources[source++] = static_cast<std::uint8_t>(v);
    }
    for (const auto& l : c.lights)
        if (l.enabled) {
            if (l.index >= 8 || l.value.Type != D3DLIGHT_DIRECTIONAL)
                return fail("unsupported_light");
            auto& out = s.lights[l.index];
            out.enabled = true;
            out.direction = {l.value.Direction.x, l.value.Direction.y, l.value.Direction.z};
            out.diffuse = color(l.value.Diffuse);
            out.ambient = color(l.value.Ambient);
            out.specular = color(l.value.Specular);
        }
    auto compare = [&](unsigned value) {
        if (value < 1 || value > 8)
            missing = true;
        return static_cast<DepthComparison>(value - 1);
    };
    auto blend = [&](unsigned value) {
        switch (value) {
        case D3DBLEND_ZERO:
            return BlendFactor::Zero;
        case D3DBLEND_ONE:
            return BlendFactor::One;
        case D3DBLEND_SRCCOLOR:
            return BlendFactor::SourceColor;
        case D3DBLEND_INVSRCCOLOR:
            return BlendFactor::InvSourceColor;
        case D3DBLEND_SRCALPHA:
            return BlendFactor::SourceAlpha;
        case D3DBLEND_INVSRCALPHA:
            return BlendFactor::InvSourceAlpha;
        case D3DBLEND_DESTALPHA:
            return BlendFactor::DestinationAlpha;
        case D3DBLEND_INVDESTALPHA:
            return BlendFactor::InvDestinationAlpha;
        case D3DBLEND_DESTCOLOR:
            return BlendFactor::DestinationColor;
        case D3DBLEND_INVDESTCOLOR:
            return BlendFactor::InvDestinationColor;
        case D3DBLEND_SRCALPHASAT:
            return BlendFactor::SourceAlphaSaturate;
        default:
            missing = true;
            return BlendFactor::Zero;
        }
    };
    p.raster.depthTest = rs(D3DRS_ZENABLE) != 0;
    p.raster.depthWrite = rs(D3DRS_ZWRITEENABLE) != 0;
    p.raster.depthCompare = compare(rs(D3DRS_ZFUNC));
    const auto cull = rs(D3DRS_CULLMODE);
    if (cull < 1 || cull > 3)
        return fail("unsupported_cull");
    p.raster.cull = static_cast<BatchCull>(cull - 1);
    p.raster.alphaBlend = rs(D3DRS_ALPHABLENDENABLE) != 0;
    p.raster.sourceBlend = blend(rs(D3DRS_SRCBLEND));
    p.raster.destinationBlend = blend(rs(D3DRS_DESTBLEND));
    p.colorOperation = static_cast<ModelBlendOp>(rs(D3DRS_BLENDOP));
    p.separateAlpha = rs(D3DRS_SEPARATEALPHABLENDENABLE) != 0;
    p.sourceAlpha = blend(rs(D3DRS_SRCBLENDALPHA));
    p.destinationAlpha = blend(rs(D3DRS_DESTBLENDALPHA));
    p.alphaOperation = static_cast<ModelBlendOp>(rs(D3DRS_BLENDOPALPHA));
    const auto writes = rs(D3DRS_COLORWRITEENABLE);
    if (writes > 15)
        return fail("unsupported_color_mask");
    p.colorWriteMask = static_cast<std::uint8_t>(writes);
    p.stencil = rs(D3DRS_STENCILENABLE) != 0;
    p.stencilCompare = compare(rs(D3DRS_STENCILFUNC));
    p.stencilReference = rs(D3DRS_STENCILREF);
    p.stencilReadMask = static_cast<std::uint8_t>(rs(D3DRS_STENCILMASK));
    p.stencilWriteMask = static_cast<std::uint8_t>(rs(D3DRS_STENCILWRITEMASK));
    p.stencilFail = static_cast<ModelStencilOp>(rs(D3DRS_STENCILFAIL));
    p.stencilDepthFail = static_cast<ModelStencilOp>(rs(D3DRS_STENCILZFAIL));
    p.stencilPass = static_cast<ModelStencilOp>(rs(D3DRS_STENCILPASS));
    s.alphaTest = rs(D3DRS_ALPHATESTENABLE) != 0;
    s.alphaCompare = compare(rs(D3DRS_ALPHAFUNC));
    const auto ref = rs(D3DRS_ALPHAREF);
    if (ref > 255)
        return fail("unsupported_alpha_reference");
    s.alphaReference = static_cast<std::uint8_t>(ref);
    // Build an isolated delta; callers receive no half-published resources.
    std::vector<ModelReplayResource> resources;
    std::vector<ModelReplayEvent> events;
    std::uint64_t version = 1;
    for (const auto& r : stream.resources) {
        if (r.key.version == (std::numeric_limits<std::uint64_t>::max)())
            return fail("resource_version_overflow");
        if (r.key.version >= version)
            version = r.key.version + 1;
    }
    auto publish = [&](ModelReplayResource r) {
        ModelReplayEvent e;
        e.op = ModelReplayOp::PublishResource;
        e.resource = r.key;
        resources.push_back(std::move(r));
        events.push_back(e);
    };
    ModelReplayResource vb;
    vb.key = {1, version};
    vb.vertexLayout = p.vertexLayout;
    vb.bytes = c.vertexBytes;
    if (positionColor)
        for (std::size_t at = 0; at + 16 <= vb.bytes.size(); at += 16)
            std::swap(vb.bytes[at + 12], vb.bytes[at + 14]); // native BGRA -> portable RGBA
    draw.vertices = vb.key;
    publish(std::move(vb));
    if (c.vertexBytes.size() % c.stride || c.count > 0xffffffffu / arity ||
        c.vertexBytes.size() / c.stride > 0xffffffffu)
        return fail("invalid_native_draw_range");
    const auto available = c.vertexBytes.size() / c.stride;
    draw.indexed = c.indexed;
    if (c.indexed) {
        ModelReplayResource ib;
        ib.key = {2, version};
        ib.kind = ReplayResourceKind::Indices;
        ib.bytes = c.indexBytes;
        if (c.indexFormat != D3DFMT_INDEX16 && c.indexFormat != D3DFMT_INDEX32)
            return fail("unsupported_index_format");
        ib.indexFormat =
            c.indexFormat == D3DFMT_INDEX16 ? IndexFormat::UInt16 : IndexFormat::UInt32;
        draw.indices = ib.key;
        publish(std::move(ib));
        if (c.minimum > available || c.vertices > available - c.minimum)
            return fail("invalid_native_draw_range");
        draw.firstIndex = c.start;
        draw.indexCount = c.count * arity;
        draw.vertexCount = static_cast<unsigned>(available);
        const unsigned indexStep = c.indexFormat == D3DFMT_INDEX16 ? 2 : 4;
        if (c.start > c.indexBytes.size() / indexStep ||
            draw.indexCount > c.indexBytes.size() / indexStep - c.start)
            return fail("invalid_native_draw_range");
        for (std::size_t i = c.start; i < std::size_t(c.start) + draw.indexCount; ++i) {
            std::uint32_t value = 0;
            std::memcpy(&value, c.indexBytes.data() + i * indexStep, indexStep);
            if (value < c.minimum || value - c.minimum >= c.vertices)
                return fail("index_out_of_range");
        }
    } else {
        // Unused native index bindings must not leak into the portable draw.
        draw.firstVertex = c.start;
        draw.drawVertexCount = c.count * arity;
        if (c.start > available || draw.drawVertexCount > available - c.start)
            return fail("invalid_native_draw_range");
    }
    for (unsigned stage = 0; stage < 8; ++stage) {
        const auto op = ts(stage, D3DTSS_COLOROP);
        if (op == D3DTOP_DISABLE)
            break;
        if (stage >= 3 || (op != D3DTOP_MODULATE && op != D3DTOP_ADD))
            return fail("unsupported_texture_cascade");
        if (ts(stage, D3DTSS_COLORARG1) != D3DTA_TEXTURE ||
            ts(stage, D3DTSS_COLORARG2) != (stage ? D3DTA_CURRENT : D3DTA_DIFFUSE) ||
            ts(stage, D3DTSS_ALPHAOP) != D3DTOP_MODULATE ||
            ts(stage, D3DTSS_ALPHAARG1) != D3DTA_TEXTURE ||
            ts(stage, D3DTSS_ALPHAARG2) != D3DTA_CURRENT ||
            ts(stage, D3DTSS_RESULTARG) != D3DTA_CURRENT)
            return fail("unsupported_texture_arguments");
        const auto& t = c.textureInfo[stage];
        // Native DX9 terminates this canonical cascade at a TEXTURE argument
        // with no binding, preserving the previous diffuse/alpha. It does not
        // sample white (which would change ADD), or resume at a later stage.
        // Established by rhi_d3d9_null_texture_native on x64 and x86.
        if (!t.present) {
            if (!c.textureBytes[stage].empty())
                return fail("unbound_texture_has_bytes");
            break;
        }
        const auto coords = ts(stage, D3DTSS_TEXCOORDINDEX),
                   transform = ts(stage, D3DTSS_TEXTURETRANSFORMFLAGS);
        if (!((coords == 0 && transform == 0) ||
              (coords == D3DTSS_TCI_CAMERASPACENORMAL && transform == D3DTTFF_COUNT2)))
            return fail("unsupported_texture_coordinates");
        if (positionColor && coords)
            return fail("position_color_has_no_normal_coordinates");
        auto& input = s.layout.stages[stage];
        input.role = stage == 0
                         ? (c.accessoryMaterial
                                ? (coords ? MaterialTextureRole::Sphere : MaterialTextureRole::Base)
                                : MaterialTextureRole::Toon)
                     : stage == 1 && !coords ? MaterialTextureRole::Base
                                             : MaterialTextureRole::Sphere;
        input.coordinates =
            coords ? MaterialCoordinates::CameraNormal : MaterialCoordinates::MeshUv0;
        input.color = op == D3DTOP_ADD ? MaterialCombine::Add : MaterialCombine::Modulate;
        input.colorArg2 = stage ? MaterialColorSource::Previous : MaterialColorSource::LitDiffuse;
        if (input.role == MaterialTextureRole::Sphere)
            s.layout.sphere = op == D3DTOP_ADD ? SphereOperation::Add : SphereOperation::Multiply;
        std::memcpy(s.textureTransforms[stage].data(), &c.textureMatrices[stage], 64);
        const auto min = ss(stage, D3DSAMP_MINFILTER), mag = ss(stage, D3DSAMP_MAGFILTER),
                   mip = ss(stage, D3DSAMP_MIPFILTER), u = ss(stage, D3DSAMP_ADDRESSU),
                   v = ss(stage, D3DSAMP_ADDRESSV);
        if (min < 1 || min > 3 || mag < 1 || mag > 3 || mip < 1 || mip > 3 || (u != 1 && u != 3) ||
            (v != 1 && v != 3) || ss(stage, D3DSAMP_MIPMAPLODBIAS) ||
            ss(stage, D3DSAMP_MAXMIPLEVEL))
            return fail("unsupported_sampler");
        s.samplers[stage] = {static_cast<SamplerFilter>(min - 1),
                             static_cast<SamplerFilter>(mag - 1),
                             static_cast<SamplerFilter>(mip - 1),
                             u == 1 ? SamplerAddress::Wrap : SamplerAddress::Clamp,
                             v == 1 ? SamplerAddress::Wrap : SamplerAddress::Clamp,
                             ss(stage, D3DSAMP_MAXANISOTROPY)};
        s.srgbSample[stage] = ss(stage, D3DSAMP_SRGBTEXTURE) != 0;
        if (t.lod || (t.format != D3DFMT_A8R8G8B8 && t.format != D3DFMT_X8R8G8B8) || !t.levels ||
            t.levels > 15 || !t.width || !t.height || t.width > 16384 || t.height > 16384)
            return fail("unsupported_texture_format_mips_or_lod");
        if (t.autogen && (t.levels != 1 ||
                          (t.autogenFilter != D3DTEXF_POINT && t.autogenFilter != D3DTEXF_LINEAR)))
            return fail("unsupported_autogen_description");
        if (t.autogen && c.preparedMips[stage].levels.empty())
            return fail("autogen_mips_not_materialized");
        if (!t.autogen && !c.preparedMips[stage].levels.empty())
            return fail("unexpected_prepared_mips");
        ModelReplayResource texture;
        texture.kind = ReplayResourceKind::Texture;
        texture.key = {10 + stage, version};
        texture.texture.srgb = s.srgbSample[stage];
        std::size_t offset = 0;
        unsigned w = t.width, h = t.height;
        for (unsigned level = 0; level < t.levels; ++level) {
            const auto size = std::size_t(w) * h * 4;
            if (offset > c.textureBytes[stage].size() ||
                size > c.textureBytes[stage].size() - offset)
                return fail("truncated_texture_bytes");
            RgbaImage image{w, h, std::vector<std::uint8_t>(size)};
            for (std::size_t i = 0; i < size; i += 4) {
                auto* src = c.textureBytes[stage].data() + offset + i;
                auto* dst = image.pixels.data() + i;
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = t.format == D3DFMT_X8R8G8B8 ? 255 : src[3];
            }
            texture.texture.levels.push_back(std::move(image));
            offset += size;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        if (offset != c.textureBytes[stage].size())
            return fail("excess_texture_bytes");
        if (t.autogen) {
            const auto& prepared = c.preparedMips[stage];
            if (prepared.generateMips || prepared.srgb)
                return fail("invalid_prepared_mips");
            w = t.width;
            h = t.height;
            unsigned count = 1;
            for (unsigned a = w, b = h; a > 1 || b > 1;
                 a = a > 1 ? a / 2 : 1, b = b > 1 ? b / 2 : 1)
                ++count;
            if (prepared.levels.size() != count)
                return fail("incomplete_prepared_mips");
            for (const auto& mip : prepared.levels) {
                if (mip.width != w || mip.height != h ||
                    mip.pixels.size() != std::size_t(w) * h * 4)
                    return fail("invalid_prepared_mips");
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
            if (prepared.levels[0].pixels != texture.texture.levels[0].pixels)
                return fail("prepared_mip_top_mismatch");
            texture.texture.levels =
                prepared.levels; // owned immutable bytes, never regenerate on another backend
        }
        draw.textures[stage] = texture.key;
        publish(std::move(texture));
    }
    if (missing)
        return fail("missing_or_invalid_native_state");
    ModelReplayEvent event;
    event.op = ModelReplayOp::Draw;
    event.draw = draw;
    event.label = "DX9 captured model";
    events.push_back(event);
    auto candidate = stream;
    candidate.complete = true;
    for (auto& r : resources)
        candidate.resources.push_back(std::move(r));
    for (auto& e : events) {
        e.sequence = candidate.events.size() + 1;
        candidate.events.push_back(std::move(e));
    }
    result = ValidateModelReplay(candidate);
    if (!result)
        return result;
    candidate.complete = stream.complete;
    stream = std::move(candidate);
    return result;
}
}
