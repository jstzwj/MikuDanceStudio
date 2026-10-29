#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/rhi_scene_preview.hpp"
#include <cstdio>
#include <cstring>
using namespace mikudancestudio::rhi;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,preview.Reason().c_str());return 1;}}while(0)
ModelReplayStream Fixture(unsigned char red=41){
    ModelReplayStream s;s.complete=true;s.target.width=s.target.height=64;s.clear={0,0,0,1};
    const float v[]={-.8f,.8f,.3f,0,0,-1,0,0, .8f,.8f,.3f,0,0,-1,1,0, -.8f,-.8f,.3f,0,0,-1,0,1};
    ModelReplayResource vertex;vertex.key={1,1};vertex.bytes.resize(sizeof(v));std::memcpy(vertex.bytes.data(),v,sizeof(v));s.resources.push_back(vertex);
    ModelReplayResource index;index.key={2,1};index.kind=ReplayResourceKind::Indices;index.bytes={0,0,1,0,2,0};s.resources.push_back(index);
    ModelReplayResource texture;texture.key={3,red};texture.kind=ReplayResourceKind::Texture;texture.texture.levels.push_back({1,1,{red,151,223,255}});s.resources.push_back(texture);
    s.events.push_back({1,ModelReplayOp::Clear});
    for(const auto& r:s.resources){ModelReplayEvent e;e.sequence=s.events.size()+1;e.op=ModelReplayOp::PublishResource;e.resource=r.key;s.events.push_back(e);}
    ModelReplayEvent e;e.sequence=5;e.op=ModelReplayOp::Draw;e.draw.vertices=vertex.key;e.draw.indices=index.key;e.draw.textures[0]=texture.key;e.draw.vertexCount=e.draw.indexCount=3;
    auto& p=e.draw.pipeline;p.raster.alphaBlend=false;p.raster.cull=BatchCull::None;
    for(auto* m:{&p.shader.world,&p.shader.view,&p.shader.projection,&p.shader.normalView})*m={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    p.shader.nativeDiffuse=p.shader.diffuse={1,1,1,1};auto& stage=p.shader.layout.stages[0];stage.role=MaterialTextureRole::Base;stage.color=MaterialCombine::Modulate;stage.colorArg2=MaterialColorSource::LitDiffuse;
    p.shader.samplers[0]={SamplerFilter::Point,SamplerFilter::Point,SamplerFilter::Point,SamplerAddress::Clamp,SamplerAddress::Clamp,1};s.events.push_back(e);return s;
}
int main(int argc,char** argv){
    ScenePreviewController preview;const std::string mode=argc>1?argv[1]:"dx11-warp";
    BackendOptions options;options.backend=mode.find("dx12")!=std::string::npos?Backend::D3D12:Backend::D3D11;
    options.driver=mode.find("hardware")!=std::string::npos?BackendDriver::Hardware:BackendDriver::Software;
    struct Window{HWND value=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden controller test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);~Window(){DestroyWindow(value);}}window;
    CHECK(window.value && !IsWindowVisible(window.value));SwapchainDesc desc;desc.window={WindowSystem::Win32,reinterpret_cast<std::uintptr_t>(window.value)};desc.width=desc.height=64;desc.present=PresentMode::Immediate;
    for(unsigned cycle=0;cycle<3;++cycle){
        CHECK(preview.Open(desc,options) && preview.Unload() && preview.Unload() && preview.Close());
        CHECK(preview.Open(desc,options));auto stream=Fixture();RgbaImage image;
        CHECK(preview.Draw(stream,&image) && image.pixels[(16*64+16)*4]==41);
        const auto uploads=preview.GetRenderer()->Stats().uploads;
        for(unsigned n=0;n<12;++n){CHECK(preview.Resize(64+(n%3)*8,64+(n%2)*8) && preview.Draw(stream));}
        CHECK(preview.GetRenderer()->Stats().uploads==uploads);
        CHECK(preview.Resize(0,0) && preview.State()==PreviewState::Suspended && preview.Draw(stream));
        CHECK(preview.Resize(64,64) && preview.State()==PreviewState::Ready && preview.Draw(stream,&image));
        CHECK(!IsWindowVisible(window.value));
        const auto identity=preview.GetDevice()->Status().identity;
        CHECK(preview.ReplaceDevice() && preview.GetDevice()->Status().identity!=identity);
        CHECK(preview.Draw(stream,&image) && image.pixels[(16*64+16)*4]==41);
        CHECK(preview.GetRenderer()->Stats().uploads==2*uploads);
        preview.Block("unsupported_scene");CHECK(preview.State()==PreviewState::Blocked);
        CHECK(!preview.Resize(64,64) && !preview.Draw(stream));
        preview.Resume();CHECK(preview.Resize(64,64) && preview.Draw(stream));
        CHECK(preview.Unload() && preview.GetRenderer()->Stats().residentResources==0 && preview.GetRenderer()->Stats().cpuResources==0);
        stream=Fixture(73);preview.Resume();CHECK(preview.Resize(64,64) && preview.Draw(stream,&image) && image.pixels[(16*64+16)*4]==73);
        CHECK(preview.Close() && preview.State()==PreviewState::Closed && !preview.GetDevice() && !preview.GetRenderer());
        CHECK(preview.Close());
    }
    auto invalid=desc;invalid.samples=8;CHECK(!preview.Open(invalid,options) && preview.State()==PreviewState::Failed && !preview.Draw(Fixture()));
    CHECK(preview.Close() && preview.Open(desc,options) && preview.Draw(Fixture()));
    CHECK(!IsWindowVisible(window.value));
    std::printf("PASS %s: repeat close/open, resize, zero-size suspension, replacement, scene unload and recovery; no window shown\n",mode.c_str());return 0;
}
