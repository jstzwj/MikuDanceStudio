#include "mikudancestudio/rhi_shader.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include <cstdio>
#include <cstring>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main() {
    for (unsigned n = 0; n < 65536; ++n)
        if ((n & 0x7c00) != 0x7c00)
            CHECK(FloatToHalf(HalfToFloat(std::uint16_t(n))) == n);
    CHECK(FloatToHalf(1.00048828125f) == 0x3c00);
    CHECK(FloatToHalf(1.00146484375f) == 0x3c02);
    auto d = CreateNullDevice();
    CHECK(d->ShaderFeatures().maxColorAttachments == 4);
    CHECK(d->FormatFeatures(TextureFormat::Rgba16Float).blend);
    RenderTextureDesc desc{8, 4, TextureFormat::Rgba16Float,
                           TextureAccess::ColorAttachment | TextureAccess::Sampled |
                               TextureAccess::CopySource};
    CHECK(QueryShaderTextureSupport(*d, desc));
    auto a = d->CreateRenderTexture(desc), b = d->CreateRenderTexture(desc),
         undefined = d->CreateRenderTexture(desc);
    desc.width = 4;
    auto small = d->CreateRenderTexture(desc);
    CHECK(a && b && small && undefined);
    CHECK(d->BeginFrame({}));
    RenderPassDesc pass;
    pass.colors = {PassAttachment{a}, PassAttachment{small}};
    CHECK(!d->BeginRenderPass(pass));
    pass.colors[1].texture = a;
    CHECK(!d->BeginRenderPass(pass));
    pass.colors[1].texture = b;
    pass.color = PassAttachment{a};
    CHECK(!d->BeginRenderPass(pass));
    pass.color.reset();
    pass.sampled = {a};
    CHECK(!d->BeginRenderPass(pass));
    pass.sampled = {undefined};
    CHECK(!d->BeginRenderPass(pass));
    pass.sampled.clear();
    pass.colors[0].clear = {-2, 4, .5f, .25f};
    pass.colors[1].clear = {8, -.5f, 2, 1};
    CHECK(d->BeginRenderPass(pass));
    ShaderProgramDesc program;
    program.vertex.hlsl = program.pixel.hlsl = "Null contract";
    program.inputs = StandardShaderInputs(VertexLayout::PositionNormalUv);
    program.colorOutputs = {4, 4};
    ShaderPipeline p;
    p.program = std::make_shared<ShaderProgram>(program);
    p.colorFormats = {TextureFormat::Rgba16Float, TextureFormat::Rgba16Float};
    p.raster.depthTest = p.raster.depthWrite = false;
    CHECK(d->SetShaderPipeline(p));
    p.colorFormats[1] = TextureFormat::Rgba8;
    CHECK(!d->SetShaderPipeline(p));
    p.colorFormats.resize(1);
    CHECK(!d->PrepareShaderPipeline(p));
    CHECK(d->EndRenderPass() && d->EndFrame() && d->Present());
    AttachmentReadback read;
    CHECK(d->ReadRenderTexture(a, read) && read.bytes.size() == 8 * 4 * 8);
    const float expected[] = {-2, 4, .5f, .25f};
    for (unsigned n = 0; n < read.bytes.size() / 2; ++n) {
        std::uint16_t h;
        std::memcpy(&h, read.bytes.data() + n * 2, 2);
        CHECK(HalfToFloat(h) == expected[n % 4]);
    }
    CHECK(d->Destroy(b));
    CHECK(d->BeginFrame({}) && !d->BeginRenderPass(pass));
    pass.colors.resize(1);
    pass.sampled = {a};
    CHECK(!d->BeginRenderPass(pass));
    pass.sampled.clear();
    pass.colors[0].store = AttachmentStore::Discard;
    CHECK(d->BeginRenderPass(pass) && d->EndRenderPass());
    pass.colors[0].texture = undefined;
    pass.sampled = {a};
    CHECK(!d->BeginRenderPass(pass));
    CHECK(d->EndFrame() && d->Present());
    CHECK(d->PrepareForReset() && d->RestoreAfterReset());
    CHECK(!d->ReadRenderTexture(a, read));
    std::puts("MRT attachment/layout/dependency/half-float contract passed");
}
