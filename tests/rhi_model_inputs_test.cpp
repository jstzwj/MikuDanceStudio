#include "mikudancestudio/render_model_inputs.hpp"
#include "mikudancestudio/render_model_geometry.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "../src/render/rhi_model_constants.hpp"
#include <cmath>
#include <cstdio>
using namespace mikudancestudio;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    // Cancellation distinguishes the measured float32 matrix preparation
    // from a double accumulator (which would produce 1 instead of 0).
    rhi::ModelPipeline transport;transport.vertexLayout=rhi::VertexLayout::PositionColor;
    transport.shader.world={16777216,1,-16777216,0,0,1,0,0,0,0,1,0,0,0,0,1};
    transport.shader.view={1,0,0,0,1,1,0,0,1,0,1,0,0,0,0,1};
    transport.shader.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::array<float,16> prepared{0,1,-16777216,0,1,1,0,0,1,0,1,0,0,0,0,1};
    rhi::detail::ModelConstants constants;
    CHECK(rhi::detail::BuildModelConstants(transport,{0,0,64,64,0,1},true,constants));
    CHECK(constants.compatibility.worldView==prepared && constants.compatibility.combined==prepared);
    CHECK(constants.vertex.w==transport.shader.world && constants.vertex.v==transport.shader.view);
    CHECK(!rhi::ModelOptions{}.diagnosticSpecularFma);
    render::ModelFrameInputs frame;frame.scene.frame=42;frame.scene.camera.parentModel=-1;
    frame.scene.camera.distance=-10;frame.scene.camera.fov=60;frame.scene.lightState={.5f,.25f,.75f,0,-1,0};
    frame.world=frame.lightRotation={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    frame.view.aspect=2;frame.view.viewport={7,11,100,50,.1f,.9f};
    render::ModelMaterialInputs material;material.diffuse={.4f,.3f,.2f,1};material.ambient=material.diffuse;material.emissive={.1f,.2f,.3f,0};material.shininess=5;
    rhi::ModelPipeline pipeline;CHECK(render::BuildModelInputs(frame,material,{},pipeline));
    // Independent geometric facts: fixed +Z view translates by the opposite
    // eye, LH near maps to zero and far to W, aspect scales X only.
    CHECK(pipeline.shader.view[14]==10);
    CHECK(std::abs(pipeline.shader.projection[5]/pipeline.shader.projection[0]-2)<1e-6f);
    auto near=rhi::TransformShaderVector({0,0,1,1},pipeline.shader.projection);
    auto far=rhi::TransformShaderVector({0,0,100000,1},pipeline.shader.projection);
    CHECK(std::abs(near[2])<1e-6f && std::abs(far[2]/far[3]-1)<1e-6f);
    CHECK(pipeline.shader.lights[0].direction[1]==-1 && pipeline.shader.lights[0].specular[2]==.75f);
    CHECK(pipeline.raster.depthCompare==rhi::DepthComparison::LessEqual && pipeline.stencilReference==1);
    CHECK(pipeline.shader.layout.stages[0].coordinates==rhi::MaterialCoordinates::CameraNormal);
    CHECK(render::CompareModelInputs(pipeline,pipeline).empty());
    auto shadowPipeline=pipeline;shadowPipeline.shadow.mode=rhi::BuiltinShadowMode::Material;
    auto disabledReceiver=shadowPipeline;disabledReceiver.shadow.receive=false;
    const auto shadowDifference=render::CompareModelInputs(shadowPipeline,disabledReceiver);
    CHECK(shadowDifference.size()==1 && shadowDifference[0].field=="shadow.receive");
    CHECK((rhi::detail::BuildBuiltinShadowConstants(shadowPipeline,*pipeline.viewport).options[3]&65536)!=0);
    CHECK((rhi::detail::BuildBuiltinShadowConstants(disabledReceiver,*pipeline.viewport).options[3]&65536)==0);
    auto changed=pipeline;changed.shader.lights[0].direction[1]+=.125f;changed.viewport->x=8;changed.raster.depthWrite=false;
    const auto differences=render::CompareModelInputs(changed,pipeline);CHECK(differences.size()==3);
    bool light=false,view=false,depth=false;for(const auto& d:differences){light|=d.field=="light0.direction" && d.component==1 && d.generated-d.reference==.125;
        view|=d.field=="viewport.x";depth|=d.field=="raster.depthWrite";}CHECK(light && view && depth);
    // Scene camera and editor navigation must not accidentally share distance
    // or FOV. Orthographic depth is independent of perspective near/far.
    frame.view.editorNavigation=true;frame.view.editorEye={2,3,-20};frame.view.editorFov=45;frame.view.editorPerspective=1;
    CHECK(render::BuildModelInputs(frame,material,{},changed));CHECK(changed.shader.view[12]==-2 && changed.shader.projection[0]==.1f && changed.shader.projection[10]==.0013f);
    frame.view.editorEye[2]=0;CHECK(!render::BuildModelInputs(frame,material,{},changed));frame.view.editorNavigation=false;
    material.diffuse[3]=.5f;material.base="base.png";material.sphere="reflection.spa";
    CHECK(render::BuildModelInputs(frame,material,{},changed));CHECK(changed.raster.cull==rhi::BatchCull::None);
    CHECK(changed.shader.layout.stages[1].role==rhi::MaterialTextureRole::Base && changed.shader.layout.stages[2].color==rhi::MaterialCombine::Add);
    material.pmd=false;material.base.clear();material.sphereMode=3;CHECK(render::BuildModelInputs(frame,material,{},changed));
    CHECK(changed.shader.layout.stages[1].coordinates==rhi::MaterialCoordinates::MeshUv1);
    frame.world[0]=0;CHECK(!render::BuildModelInputs(frame,material,{},changed));frame.world[0]=1;
    frame.scene.camera.fov=180;CHECK(!render::BuildModelInputs(frame,material,{},changed));frame.scene.camera.fov=60;
    render::ModelPassSettings unsupported;unsupported.anisotropic=false;CHECK(!render::BuildModelInputs(frame,material,unsupported,changed));
    // Consume a generated input with owned CPU geometry + a texture, through
    // the same renderer used by native backends. Resource/cache recovery is
    // exercised with this producer in rhi_backend_scene_test.
    render::ModelGeometry geometry(1,3);const std::uint16_t indices[]={0,1,2};CHECK(geometry.SetIndices(rhi::IndexFormat::UInt16,indices,sizeof(indices)));CHECK(geometry.Publish());
    rhi::ModelReplayStream stream;stream.target.width=128;stream.target.height=64;stream.events.push_back({1,rhi::ModelReplayOp::Clear});
    rhi::ModelReplayResource texture;texture.key={4,1};texture.kind=rhi::ReplayResourceKind::Texture;texture.texture.levels.push_back({1,1,{255,128,64,255}});
    stream.resources.push_back(texture);rhi::ModelReplayEvent publish;publish.sequence=2;publish.op=rhi::ModelReplayOp::PublishResource;publish.resource=texture.key;stream.events.push_back(publish);
    rhi::ModelReplayDraw draw;draw.pipeline=pipeline;draw.indexCount=3;draw.textures[0]=texture.key;CHECK(render::AppendModelGeometry(stream,geometry.Snapshot(),draw));stream.complete=true;
    CHECK(rhi::ValidateModelReplay(stream));
    // A model submission shares geometry and textures, retains original draw
    // ranges/order and rejects conflicting bytes before committing versions.
    render::ModelGeometry multiGeometry(20,3);const std::uint16_t repeated[]={0,1,2,0,1,2,0,1,2};
    CHECK(multiGeometry.SetIndices(rhi::IndexFormat::UInt16,repeated,sizeof(repeated)) && multiGeometry.Publish());
    render::ModelMaterialPacket opaque;opaque.material=12;opaque.indexCount=3;opaque.pipeline=pipeline;
    auto& toon=opaque.textures[0];toon.identity="shared-toon";toon.content={rhi::RgbaImage{1,1,{255,128,64,255}},1};
    auto transparent=opaque;transparent.material=13;transparent.firstIndex=3;transparent.pipeline.raster.cull=rhi::BatchCull::None;
    transparent.pipeline.shader.diffuse[3]=transparent.pipeline.shader.nativeDiffuse[3]=.5f;
    auto alternate=opaque;alternate.material=14;alternate.firstIndex=6;alternate.textures[0].identity="other-toon";
    alternate.textures[0].content.image.pixels={64,128,255,255};alternate.pipeline.raster.depthWrite=false;
    render::ModelSubmissionBuilder builder;std::vector<render::ModelMaterialPacket> packets={opaque,transparent,alternate};rhi::ModelReplayStream modelStream;
    CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},packets,modelStream));
    CHECK(modelStream.resources.size()==4);
    std::vector<rhi::ModelReplayDraw> draws;for(const auto& e:modelStream.events)if(e.op==rhi::ModelReplayOp::Draw)draws.push_back(e.draw);
    CHECK(draws.size()==3 && draws[0].firstIndex==0 && draws[1].firstIndex==3 && draws[2].firstIndex==6);
    CHECK(draws[0].textures[0].id==draws[1].textures[0].id && draws[0].textures[0].id!=draws[2].textures[0].id);
    CHECK(draws[1].pipeline.raster.cull==rhi::BatchCull::None && !draws[2].pipeline.raster.depthWrite);
    auto same=modelStream;CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},packets,same));
    CHECK(same.events.back().draw.textures[0].version==draws[2].textures[0].version);
    auto referenced=packets;for(auto& p:referenced){p.textureResources[0]=std::make_shared<const rhi::MaterialTextureAsset>(p.textures[0]);p.textures[0]={};}
    rhi::ModelReplayStream shared;CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},referenced,shared));
    CHECK(shared.resources.size()==same.resources.size() && shared.events.back().draw.textures[0].id==same.events.back().draw.textures[0].id);
    auto bad=packets;bad[1].material=11;CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},bad,same).reason=="material_order_or_range");
    bad=packets;bad[1].textures[0].content.image.pixels[0]++;CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},bad,same).reason=="conflicting_material_texture");
    CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},packets,same));CHECK(same.events.back().draw.textures[0].version==1);
    packets[2].textures[0].content.image.pixels[0]++;CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},packets,same));
    CHECK(same.events.back().draw.textures[0].id==draws[2].textures[0].id && same.events.back().draw.textures[0].version==2);
    CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{}, {opaque},same));
    CHECK(builder.Build(multiGeometry.Snapshot(),stream.target,{},packets,same));CHECK(same.events.back().draw.textures[0].version==3);
    bad=packets;bad[2].textures[0].mips=rhi::TextureDesc::Mips::GenerateLinear;CHECK(!builder.Build(multiGeometry.Snapshot(),stream.target,{},bad,same));
    // A lower Mip or recipe change gets a new immutable version even when top
    // RGBA is unchanged. SceneRenderer restores the complete CPU level set.
    auto mipPacket=opaque;auto& mipAsset=mipPacket.textures[0];mipAsset.identity="full-mip";
    mipAsset.content.image={2,2,{0,10,20,255,10,20,30,255,20,30,40,255,30,40,50,255}};
    mipAsset.content.revision=rhi::ImageRevision(mipAsset.content.image);
    CHECK(rhi::PrepareMaterialMipChain(mipAsset,rhi::MipPreparationPolicy::BoxLinear));
    render::ModelSubmissionBuilder mipBuilder;
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {mipPacket},same));
    const auto key=same.events.back().draw.textures[0];CHECK(same.resources[0].texture.levels.size()==2);
    auto mipLevels=mipAsset.mipChain->levels;mipLevels[1].pixels[0]++;
    CHECK(rhi::SealPreparedMipChain(mipAsset.content.image,rhi::MipPreparationPolicy::Dx9AutoLinear,77,mipLevels,*mipAsset.mipChain));
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {mipPacket},same));
    CHECK(same.events.back().draw.textures[0].id==key.id && same.events.back().draw.textures[0].version==2);
    CHECK(rhi::SealPreparedMipChain(mipAsset.content.image,rhi::MipPreparationPolicy::Dx9AutoLinear,78,mipLevels,*mipAsset.mipChain));
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {mipPacket},same));CHECK(same.events.back().draw.textures[0].version==3);
    auto invalidMip=mipPacket;invalidMip.textures[0].mipChain->levels[1].pixels[0]++;
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {invalidMip},same).reason=="invalid_cpu_mip_chain");
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {mipPacket},same));CHECK(same.events.back().draw.textures[0].version==3);
    auto rawChanged=mipPacket;rawChanged.textures[0].prepared=rawChanged.textures[0].content;
    rawChanged.textures[0].usePrepared=true;rawChanged.textures[0].content.image.pixels[0]++;
    rawChanged.textures[0].content.revision=rhi::ImageRevision(rawChanged.textures[0].content.image);
    CHECK(mipBuilder.Build(multiGeometry.Snapshot(),stream.target,{}, {rawChanged},same));CHECK(same.events.back().draw.textures[0].version==4);
    std::puts("CPU model inputs and ordered multi-material submissions: shared identities, immutable versions, switches and failed-build rollback PASS");return 0;
}
