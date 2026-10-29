#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/render_model_geometry.hpp"
#include "mikudancestudio/render_scene_frame.hpp"
#include <cstdio>
#include <set>
#include <map>
using namespace mikudancestudio::rhi;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
// Deliberately delayed completion makes premature retirement observable.
struct DelayedDevice final:Device {
    std::uint64_t identity=detail::NextDeviceIdentity(),epoch=1,issued=0,completed=0;unsigned uploads=0,destroyed=0,draws=0;
    bool frame=false,failUpload=false,failSignal=false,failDestroy=false,removed=false,completeOnWait=false,failTarget=false;unsigned width=0,height=0;std::set<unsigned> handles;std::map<unsigned,std::uint64_t> uses;
    std::map<unsigned,BufferDesc> descriptions;std::map<unsigned,std::vector<std::uint8_t>> contents;
    std::vector<CompletionToken> waits;Handle lastVertices;
    Backend BackendType()const override{return Backend::Null;}Capabilities Features()const override{return {};}
    DeviceStatus Status()const override{return {identity,epoch,removed?DeviceState::Removed:DeviceState::Ready};}
    ModelCapabilities ModelFeatures()const override{return {true,true,true,true,true,true,true,true,true,true,false,512};}
    Handle CreateBuffer(BufferDesc desc,const void* data)override{if(failUpload)return {};handles.insert(++uploads);descriptions[uploads]=desc;if(data)contents[uploads].assign(static_cast<const std::uint8_t*>(data),static_cast<const std::uint8_t*>(data)+desc.size);return {uploads,1,identity};}
    Handle CreateTexture(TextureDesc)override{return CreateBuffer({},nullptr);}
    std::vector<RgbaImage> mipUpload;
    Handle CreateModelTexture(const ModelTextureData& data)override{mipUpload=data.levels;return CreateBuffer({},nullptr);}
    bool UploadTexture(Handle,TexturePixels)override{return false;}
    unsigned bufferWrites=0;
    bool Upload(Handle h,std::size_t offset,const void* data,std::size_t size)override{
        if(failUpload || h.device!=identity || !handles.count(h.index) || !descriptions[h.index].dynamic || uses[h.index]>completed || offset || !data || size!=descriptions[h.index].size)return false;
        contents[h.index].assign(static_cast<const std::uint8_t*>(data),static_cast<const std::uint8_t*>(data)+size);++bufferWrites;return true;
    }
    bool BindTexture(std::uint32_t,Handle h)override{return handles.count(h.index) && h.device==identity;}
    bool Destroy(Handle h)override{if(failDestroy || h.device!=identity || (!removed && uses[h.index]>completed) || !handles.erase(h.index))return false;uses.erase(h.index);++destroyed;return true;}
    bool BeginFrame(ClearColor)override{if(frame)return false;frame=true;return true;}
    bool EndFrame()override{if(!frame)return false;frame=false;return true;}bool Present()override{return false;}
    bool SetFixedPipeline(FixedPipelineState)override{return false;}bool SetModelPipeline(const ModelPipeline&)override{return frame;}
    bool Draw(NonIndexedDraw d)override{if(!frame || !IsCurrentSubmission(Status(),d.submission) || !handles.count(d.vertices.index))return false;lastVertices=d.vertices;uses[d.vertices.index]=issued+1;++draws;return true;}
    bool DrawIndexed(IndexedDraw d)override{if(!frame || !IsCurrentSubmission(Status(),d.submission) || !handles.count(d.vertices.index)||!handles.count(d.indices.index))return false;lastVertices=d.vertices;uses[d.vertices.index]=uses[d.indices.index]=issued+1;++draws;return true;}
    bool PrepareForReset()override{return false;}bool RestoreAfterReset()override{return false;}
    bool EnsureModelTarget(const HistoryDesc& t)override{if(frame || failTarget)return false;if(width!=t.width || height!=t.height){width=t.width;height=t.height;++epoch;}return true;}
    CompletionToken SignalCompletion()override{return failSignal?CompletionToken{}:CompletionToken{identity,++issued};}
    CompletionState PollCompletion(CompletionToken t)override{return t.device!=identity || !t.value || t.value>issued?CompletionState::Invalid:t.value<=completed?CompletionState::Complete:CompletionState::Pending;}
    bool WaitCompletion(CompletionToken t,std::uint32_t)override{waits.push_back(t);if(completeOnWait && t.device==identity && t.value<=issued)completed=(std::max)(completed,t.value);return PollCompletion(t)==CompletionState::Complete;}
};
ModelReplayStream Fixture(unsigned version=1) {
    ModelReplayStream s;s.complete=true;s.target.width=s.target.height=64;
    ModelReplayResource v;v.key={1,1};v.bytes.resize(96);s.resources.push_back(v);
    ModelReplayResource i;i.key={2,1};i.kind=ReplayResourceKind::Indices;i.bytes={0,0,1,0,2,0};s.resources.push_back(i);
    ModelReplayResource t;t.key={3,version};t.kind=ReplayResourceKind::Texture;t.texture.levels.push_back({1,1,{static_cast<unsigned char>(version),13,71,255}});s.resources.push_back(t);
    s.events.push_back({1,ModelReplayOp::Clear});for(const auto& r:s.resources){ModelReplayEvent e;e.sequence=s.events.size()+1;e.op=ModelReplayOp::PublishResource;e.resource=r.key;s.events.push_back(e);}
    ModelReplayEvent e;e.sequence=5;e.op=ModelReplayOp::Draw;auto& d=e.draw;d.vertices={1,1};d.indices={2,1};d.textures[0]=t.key;d.indexCount=d.vertexCount=3;
    for(auto* m:{&d.pipeline.shader.world,&d.pipeline.shader.view,&d.pipeline.shader.projection,&d.pipeline.shader.normalView})*m={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto& stage=d.pipeline.shader.layout.stages[0];stage.role=MaterialTextureRole::Base;stage.color=MaterialCombine::Modulate;stage.colorArg2=MaterialColorSource::LitDiffuse;s.events.push_back(e);return s;
}
ModelReplayStream Moving(unsigned version, unsigned size=96) {
    auto stream=Fixture();stream.resources[0].key.version=version;
    stream.resources[0].bytes.resize(size);stream.resources[0].bytes[0]=version%251;
    stream.events[1].resource=stream.resources[0].key;stream.events.back().draw.vertices=stream.resources[0].key;
    return stream;
}
int VerifyBufferPool() {
    DelayedDevice gpu;gpu.completeOnWait=true;SceneRenderer renderer(gpu);PreparedModelFrame frame;CompletionToken token;
    for(unsigned n=1;n<=100;++n){auto stream=Moving(n);CHECK(renderer.Prepare(1,stream,frame) && renderer.Submit(frame,token));
        const auto stats=renderer.Stats();CHECK(stats.bufferCreations<=6 && stats.residentResources==3 && stats.pooledBuffers+stats.retiringResources<=4);
        CHECK(gpu.contents[gpu.lastVertices.index][0]==n%251);}
    CHECK(renderer.Stats().bufferReuses>90 && renderer.Stats().bufferWaits>0);
    CHECK(gpu.completed<gpu.issued && !gpu.waits.empty()); // waits did not drain unrelated latest work
    const auto created=renderer.Stats().bufferCreations;const auto latest=gpu.lastVertices;
    // A timeout preserves the active prepared frame and all its immutable bytes.
    gpu.completeOnWait=false;CHECK(renderer.Prepare(1,Moving(101),frame).reason=="resource_upload_failed");
    CHECK(renderer.Submit(frame,token) && gpu.lastVertices.index==latest.index && gpu.contents[latest.index][0]==100);
    gpu.completed=gpu.issued;CHECK(renderer.Collect());
    const auto writes=gpu.bufferWrites;
    CHECK(renderer.Prepare(1,Moving(99),frame).reason=="retired_resource_version" && gpu.bufferWrites==writes);
    gpu.failUpload=true;CHECK(!renderer.Prepare(1,Moving(101),frame));gpu.failUpload=false;
    CHECK(renderer.Submit(frame,token) && gpu.contents[latest.index][0]==100);
    gpu.completed=gpu.issued;gpu.failTarget=true;
    CHECK(renderer.Prepare(1,Moving(101),frame).reason=="target_prepare_failed");gpu.failTarget=false;
    CHECK(renderer.Submit(frame,token) && gpu.contents[latest.index][0]==100);
    // Completed slots of obsolete sizes are evicted at the same fixed bound.
    gpu.completed=gpu.issued;gpu.completeOnWait=true;
    for(unsigned n=101;n<=150;++n){CHECK(renderer.Prepare(1,Moving(n,96*(n%5+1)),frame) && renderer.Submit(frame,token));
        CHECK(gpu.handles.size()<=7 && renderer.Stats().pooledBuffers+renderer.Stats().retiringResources<=4);}
    gpu.completed=gpu.issued;CHECK(renderer.Unload(1) && gpu.handles.empty() && !renderer.Stats().pooledBuffers);
    CHECK(created<=6);return 0;
}
int VerifySimultaneousVersions() {
    DelayedDevice gpu;SceneRenderer renderer(gpu);PreparedModelFrame frame;CompletionToken token;
    auto stream=Moving(1);
    for(unsigned n=2;n<=6;++n)stream.resources.push_back(Moving(n).resources[0]);
    auto draw=stream.events.back().draw;stream.events.resize(1);
    for(const auto& resource:stream.resources)if(resource.key.id!=1){ModelReplayEvent e;e.sequence=stream.events.size()+1;e.op=ModelReplayOp::PublishResource;e.resource=resource.key;stream.events.push_back(e);}
    for(unsigned n=1;n<=6;++n){ModelReplayEvent publish;publish.sequence=stream.events.size()+1;publish.op=ModelReplayOp::PublishResource;publish.resource={1,n};stream.events.push_back(publish);
        ModelReplayEvent e;e.sequence=stream.events.size()+1;e.op=ModelReplayOp::Draw;e.draw=draw;e.draw.vertices={1,n};stream.events.push_back(e);}
    CHECK(renderer.Prepare(1,stream,frame) && renderer.Submit(frame,token));
    // Deliberately retaining six versions is legal. Promotion must preserve
    // every active allocation while preparing a single replacement version.
    CHECK(renderer.Prepare(1,Moving(7),frame) && renderer.Submit(frame,token));
    CHECK(gpu.contents[gpu.lastVertices.index][0]==7);
    CHECK(renderer.Unload(1) && !renderer.Stats().residentResources && !renderer.Stats().pooledBuffers);
    CHECK(!gpu.handles.empty()); // all submitted versions still await completion
    gpu.completed=gpu.issued;CHECK(renderer.Collect() && gpu.handles.empty());return 0;
}
int main(){DelayedDevice old,newDevice;SceneRenderer renderer(old);auto stream=Fixture();PreparedModelFrame f;CHECK(renderer.Prepare(1,stream,f));
    CHECK(VerifyBufferPool()==0);
    CHECK(VerifySimultaneousVersions()==0);
    CompletionToken token;for(unsigned n=0;n<1000;++n){CHECK(renderer.Submit(f,token));old.completed=token.value;}
    CHECK(old.uploads==3 && renderer.Stats().targetRebuilds==1 && renderer.Stats().submissions==1000);
    old.completed=old.issued-1;PreparedModelFrame edited;CHECK(renderer.Prepare(1,Fixture(2),edited));CHECK(old.uploads==4 && renderer.Stats().retiringResources==1 && old.destroyed==0);
    CHECK(renderer.Submit(f,token).reason=="superseded_prepared_frame");CHECK(renderer.Submit(edited,token));
    old.completed=old.issued;CHECK(renderer.Collect() && old.destroyed==1 && renderer.Stats().retiringResources==0);
    PreparedModelFrame rejected;auto conflict=Fixture(2);conflict.resources[2].texture.levels[0].pixels[0]++;const auto epoch=old.epoch;
    CHECK(renderer.Prepare(1,conflict,rejected).reason=="immutable_version_content_changed" && old.uploads==4 && old.epoch==epoch);
    CHECK(renderer.Prepare(1,Fixture(1),rejected).reason=="retired_resource_version");
    auto invalid=Fixture(3);invalid.events.back().op=ModelReplayOp::Unsupported;CHECK(!renderer.Prepare(1,invalid,rejected) && old.uploads==4);
    auto oversized=Fixture(3);const auto draw=oversized.events.back();for(unsigned n=1;n<513;++n){auto e=draw;e.sequence=oversized.events.size()+1;oversized.events.push_back(e);}
    CHECK(renderer.Prepare(1,oversized,rejected).reason=="backend_draw_limit" && old.uploads==4 && old.epoch==epoch);
    CHECK(renderer.Submit(edited,token));CHECK(!renderer.Rebind(newDevice,0) && newDevice.uploads==0);old.completed=old.issued;
    CHECK(renderer.Rebind(newDevice));CHECK(renderer.Submit(edited,token).reason=="superseded_prepared_frame");
    CHECK(renderer.Prepare(1,Fixture(2),edited) && newDevice.uploads==3);CHECK(renderer.Submit(edited,token));
    auto resized=Fixture(2);resized.target.width=80;PreparedModelFrame large;CHECK(renderer.Prepare(1,resized,large));CHECK(newDevice.uploads==3 && renderer.Submit(edited,token).reason=="superseded_prepared_frame");
    newDevice.completed=newDevice.issued;CHECK(renderer.Unload(1));CHECK(renderer.Stats().residentResources==0 && newDevice.handles.empty());
    CHECK(renderer.Prepare(1,Fixture(),f));CHECK(renderer.Submit(large,token).reason=="superseded_prepared_frame");
    // Namespace separation prevents identical IDs in different scenes aliasing.
    PreparedModelFrame other;CHECK(renderer.Prepare(2,Fixture(),other));CHECK(renderer.Stats().residentResources==6);
    auto update=Fixture(2);newDevice.failUpload=true;CHECK(renderer.Prepare(1,update,rejected).reason=="resource_upload_failed");newDevice.failUpload=false;
    CHECK(renderer.Submit(f,token));newDevice.completed=newDevice.issued;CHECK(renderer.Unload(1) && renderer.Unload(2));
    CHECK(renderer.Stats().residentResources==0 && renderer.Stats().retiringResources==0);
    CHECK(renderer.Prepare(1,Fixture(),f));newDevice.failDestroy=true;CHECK(!renderer.Detach() && renderer.Stats().residentResources==3);newDevice.failDestroy=false;
    CHECK(renderer.Detach() && renderer.Stats().residentResources==0 && renderer.Stats().cpuResources==3);
    CHECK(renderer.Prepare(1,Fixture(),rejected).reason=="device_not_bound");CHECK(renderer.Submit(f,token).reason=="device_not_bound");CHECK(renderer.Rebind(newDevice));
    DelayedDevice moving;SceneRenderer animated(moving);PreparedModelFrame pose;
    mikudancestudio::render::ModelGeometry geometry(101,3);const std::uint16_t indexData[]={0,1,2};
    CHECK(geometry.SetIndices(IndexFormat::UInt16,indexData,sizeof(indexData)));
    for(unsigned n=1;n<=1000;++n){auto update=Fixture();
        static_cast<mikudancestudio::render::ModelVertex*>(geometry.MainData())[0].position[0]=n*.001f;CHECK(geometry.Publish());
        update.resources[0]=geometry.Snapshot().main;update.resources[1]=geometry.Snapshot().indices;
        update.events[1].resource=update.resources[0].key;update.events[2].resource=update.resources[1].key;
        update.events.back().draw.vertices=update.resources[0].key;update.events.back().draw.indices=update.resources[1].key;
        CHECK(animated.Prepare(1,update,pose) && animated.Submit(pose,token));moving.completed=moving.issued>3?moving.issued-3:0;CHECK(animated.Collect());
        CHECK(animated.Stats().residentResources==3 && animated.Stats().retiringResources<=3);}
    CHECK(animated.Stats().uploads==1002 && animated.Stats().textureUploads==1);
    CHECK(animated.Stats().bufferCreations<=6 && animated.Stats().bufferReuses>990 && moving.bufferWrites>990);
    moving.completed=moving.issued;CHECK(animated.Unload(1) && moving.handles.empty());
    DelayedDevice failing;SceneRenderer poisoned(failing);PreparedModelFrame poisonFrame;CHECK(poisoned.Prepare(1,Fixture(),poisonFrame));
    failing.failSignal=true;CHECK(poisoned.Submit(poisonFrame,token).reason=="completion_signal_failed");
    CHECK(poisoned.Submit(poisonFrame,token).reason=="untracked_gpu_work" && !poisoned.Collect());
    CHECK(poisoned.Prepare(1,Fixture(2),rejected).reason=="untracked_gpu_work" && !poisoned.Rebind(newDevice));
    CHECK(failing.destroyed==0); // A missing completion boundary must not retire GPU objects.
    failing.removed=true;CHECK(poisoned.Rebind(newDevice) && failing.handles.empty());
    CHECK(poisoned.Prepare(1,Fixture(),poisonFrame) && poisoned.Submit(poisonFrame,token));newDevice.completed=newDevice.issued;
    DelayedDevice mipOld,mipNew;SceneRenderer mipRenderer(mipOld);auto mipStream=Fixture();
    RgbaImage mipSource{2,2,{0,0,0,255,4,8,12,255,8,16,24,255,12,24,36,255}};PreparedMipChain chain;
    CHECK(PreparePortableMipChain(mipSource,MipPreparationPolicy::BoxLinear,chain));mipStream.resources[2].texture.levels=chain.levels;
    PreparedModelFrame mipFrame;CHECK(mipRenderer.Prepare(1,mipStream,mipFrame) && mipRenderer.Submit(mipFrame,token));
    CHECK(mipOld.mipUpload.size()==2 && mipOld.mipUpload[1].pixels==std::vector<std::uint8_t>({6,12,18,255}));
    const auto count=mipOld.uploads;CHECK(mipRenderer.Prepare(1,mipStream,mipFrame) && mipOld.uploads==count);
    auto mipConflict=mipStream;mipConflict.resources[2].texture.levels[1].pixels[0]++;
    CHECK(mipRenderer.Prepare(1,mipConflict,rejected).reason=="immutable_version_content_changed");
    mipOld.completed=mipOld.issued;CHECK(mipRenderer.Rebind(mipNew) && !mipRenderer.Submit(mipFrame,token));
    CHECK(mipRenderer.Prepare(1,mipStream,mipFrame) && mipNew.mipUpload.size()==2 &&
        mipNew.mipUpload[1].pixels==chain.levels[1].pixels && mipRenderer.Submit(mipFrame,token));
    mipNew.completed=mipNew.issued;CHECK(mipRenderer.Detach());
    // The full-frame producer runs every frame, while SceneRenderer and its
    // resource cache stay resident. Geometry changes; texture and index uploads
    // must remain constant. Independent local IDs cannot alias across objects.
    DelayedDevice liveOld,liveNew;SceneRenderer live(liveOld);
    mikudancestudio::render::CpuSceneFrameAssembler assembler;
    mikudancestudio::render::SceneFrameDesc desc;desc.target.width=desc.target.height=64;
    PreparedModelFrame resident;ModelReplayStream assembled;
    for(unsigned n=0;n<1000;++n){desc.frame=n;assembler.Begin(desc);
        auto input=Fixture();input.source.frame=n;input.resources[0].bytes[0]=n%251;
        assembler.Append({0,mikudancestudio::render::SceneDrawFamily::ModelMaterial,0,0,input,{"shared:toon"}});
        auto accessory=Fixture();accessory.source.frame=n;assembler.Append({0,mikudancestudio::render::SceneDrawFamily::AccessoryMaterial,0,0,accessory,{"shared:toon"}});
        CHECK(assembler.Finish(assembled) && live.Prepare(1,assembled,resident) && live.Submit(resident,token));
        liveOld.completed=liveOld.issued;CHECK(live.Collect());
        CHECK(live.Stats().residentResources==5 && live.Stats().retiringResources==0 && assembler.Stats().identityCount==5);}
    CHECK(live.Stats().uploads==1004 && live.Stats().textureUploads==1 && live.Stats().bufferCreations<=6 && liveOld.bufferWrites>990);
    // One logical asset cannot publish contradictory bytes in the same frame.
    assembler.Begin(desc);auto conflicting=Fixture();conflicting.source.frame=desc.frame;
    assembler.Append({0,mikudancestudio::render::SceneDrawFamily::ModelMaterial,0,0,conflicting,{"shared:toon"}});
    conflicting.resources[2].texture.levels[0].pixels[0]++;
    assembler.Append({0,mikudancestudio::render::SceneDrawFamily::AccessoryMaterial,0,0,conflicting,{"shared:toon"}});
    CHECK(assembler.Finish(assembled).reason=="conflicting_cpu_resource_in_frame" && live.Submit(resident,token));liveOld.completed=liveOld.issued;
    CHECK(live.Rebind(liveNew) && !live.Submit(resident,token));
    CHECK(live.Prepare(1,assembled,resident) && live.Submit(resident,token) && liveNew.uploads==5);
    liveNew.completed=liveNew.issued;
    // Removing the last drawable still presents a clear, releases resident
    // resources, and leaves no stale geometry on screen.
    assembler.Begin(desc);CHECK(assembler.Finish(assembled) && assembled.resources.empty());
    CHECK(live.Prepare(1,assembled,resident) && live.Submit(resident,token));
    liveNew.completed=liveNew.issued;CHECK(live.Collect() && liveNew.handles.empty() && live.Stats().cpuResources==0);
    CHECK(live.Unload(1));assembler.Reset();
    CHECK(assembler.Stats().identityCount==0 && live.Stats().cpuResources==0);
    std::puts("Scene renderer cache, full Mip versions/recovery, CPU frames, namespaces, stale frames and delayed completion passed");return 0;}
