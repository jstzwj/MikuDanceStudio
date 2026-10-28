#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <wrl/client.h>
#include <d3dx9.h>
using namespace mikudancestudio;
using namespace mikudancestudio::format;
using namespace mikudancestudio::render;
using namespace mikudancestudio::rhi;
#define CHECK(x) do{if(!(x)){std::cerr<<"line "<<__LINE__<<": "<<#x<<" reason="<<reason<<"\n";return 1;}}while(0)
namespace {
int Read(void* p,void* out,std::size_t size){auto& in=*static_cast<std::istream*>(p);in.read(static_cast<char*>(out),size);return int(in.gcount());}
bool Write(void* p,const void* data,std::size_t size){auto& out=*static_cast<std::ostream*>(p);out.write(static_cast<const char*>(data),size);return bool(out);}
std::vector<std::string> Labels(const ModelReplayStream& s){std::vector<std::string> names;for(const auto& e:s.events)if(e.op==ModelReplayOp::Draw)names.push_back(e.label);return names;}
ModelFrameInputs Frame(){ModelFrameInputs f;f.view.viewport={0,0,128,96,0,1};f.view.aspect=4.f/3;return f;}
bool SameKeys(const ModelReplayStream& a,const ModelReplayStream& b){if(a.resources.size()!=b.resources.size())return false;
    for(std::size_t n=0;n<a.resources.size();++n)if(a.resources[n].key.id!=b.resources[n].key.id || a.resources[n].key.version!=b.resources[n].key.version)return false;return true;}
int NativeX(const std::filesystem::path& path){
    std::string reason;RuntimeCpuAccessory cpu;CHECK(cpu.Load(path,1,reason));
    using Microsoft::WRL::ComPtr;
    struct Window {HWND h=CreateWindowW(L"STATIC",L"CPU X acceptance",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);~Window(){if(h)DestroyWindow(h);}}window;
    ComPtr<IDirect3D9> api;api.Attach(Direct3DCreate9(D3D_SDK_VERSION));CHECK(api && window.h);
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window.h;ComPtr<IDirect3DDevice9> device;
    const auto hr=api->CreateDevice(0,D3DDEVTYPE_HAL,window.h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);
    if(FAILED(hr)){std::cerr<<"DX9 creation HRESULT="<<std::hex<<unsigned(hr)<<"\n";return 1;}
    ComPtr<ID3DXMesh> mesh,normalized;ComPtr<ID3DXBuffer> materials;DWORD count=0;
    CHECK(SUCCEEDED(D3DXLoadMeshFromXW(path.c_str(),544,device.Get(),nullptr,&materials,nullptr,&count,&mesh)));
    CHECK(SUCCEEDED(mesh->CloneMeshFVF(544,D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1,device.Get(),&normalized)));
    void* vb=nullptr;void* ib=nullptr;DWORD* attributes=nullptr;
    CHECK(SUCCEEDED(normalized->LockVertexBuffer(D3DLOCK_READONLY,&vb)) && SUCCEEDED(normalized->LockIndexBuffer(D3DLOCK_READONLY,&ib)) && SUCCEEDED(normalized->LockAttributeBuffer(D3DLOCK_READONLY,&attributes)));
    // Native vertex splitting/order is not a file contract. Compare material
    // assignment and complete per-triangle position/normal/UV bytes instead.
    std::multiset<std::string> native,portable;
    const auto nativeIndex=[&](unsigned i){return normalized->GetOptions()&1?static_cast<const DWORD*>(ib)[i]:unsigned(static_cast<const WORD*>(ib)[i]);};
    for(unsigned face=0;face<normalized->GetNumFaces();++face){std::string triangle(reinterpret_cast<const char*>(attributes+face),4);
        for(unsigned c=0;c<3;++c)triangle.append(static_cast<const char*>(vb)+std::size_t(nativeIndex(face*3+c))*32,32);native.insert(triangle);}
    normalized->UnlockAttributeBuffer();normalized->UnlockIndexBuffer();normalized->UnlockVertexBuffer();
    auto frame=Frame();frame.scene.camera.fov=45;frame.scene.camera.distance=-45;frame.scene.lightState={.6f,.6f,.6f,-.5f,-1,-.5f};
    frame.world=frame.lightRotation={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};PmmEvaluatedAccessory pose;pose.visible=1;pose.opacity=1;
    HistoryDesc target;target.width=128;target.height=96;std::vector<SceneDrawReplacement> draws;
    CHECK(cpu.Build(frame,frame.world,pose,false,false,target,{1,1,1,1},[&](auto draw){draws.push_back(std::move(draw));},reason));
    const auto& geometry=*cpu.Geometry();
    for(const auto& draw:draws){const auto& command=draw.cpu.events.back().draw;const auto* raw=static_cast<const D3DXMATERIAL*>(materials->GetBufferPointer());CHECK(draw.material<count);
        for(unsigned c=0;c<4;++c){const auto* diffuse=reinterpret_cast<const float*>(&raw[draw.material].MatD3D.Diffuse);
            CHECK(command.pipeline.shader.nativeDiffuse[c]==(c==3?diffuse[c]:diffuse[c]*.1f));}
        for(unsigned face=command.firstIndex/3;face<(command.firstIndex+command.indexCount)/3;++face){std::string triangle(reinterpret_cast<const char*>(&draw.material),4);
            for(unsigned c=0;c<3;++c){unsigned index;std::memcpy(&index,geometry.indices.bytes.data()+(face*3+c)*4,4);triangle.append(reinterpret_cast<const char*>(geometry.main.bytes.data())+std::size_t(index)*32,32);}portable.insert(triangle);}}
    if(native!=portable){const auto dump=[](const char* label,const std::multiset<std::string>& triangles){std::cerr<<label<<"\n";unsigned limit=0;
        for(const auto& tri:triangles){if(limit++==4)break;unsigned material;std::memcpy(&material,tri.data(),4);std::cerr<<"material="<<material;
            for(unsigned c=0;c<3;++c){float v[8];std::memcpy(v,tri.data()+4+c*32,32);std::cerr<<" [";for(float f:v){unsigned bits;std::memcpy(&bits,&f,4);std::cerr<<f<<"/"<<std::hex<<bits<<std::dec<<",";}std::cerr<<"]";}std::cerr<<"\n";}};
        dump("native",native);dump("cpu",portable);}
    CHECK(native==portable);std::cout<<"PASS native D3DX/CPU X triangle, normal, UV, material parity faces="<<native.size()<<" materials="<<count<<"\n";return 0;
}
}
int main(int argc,char** argv){
    if(argc==3 && std::string(argv[1])=="--native-x")return NativeX(std::filesystem::u8path(argv[2]));
    if(argc!=3)return 2;std::string reason;const auto root=std::filesystem::absolute(argv[1]);FileAssetSource assets(argv[2]);
    ToonTextureResolver textures(root,assets);HistoryDesc target;target.width=128;target.height=96;
    // Independent PMX file flags: cast=4, receive=8, outline=16. An absent
    // caster still produces a defined, cleared shadow attachment.
    struct ShadowCase {const char* name;bool cast,receive,outline;};
    for(const auto& item:std::vector<ShadowCase>{{"flags-1f.pmx",true,true,true},
            {"flags-17.pmx",true,false,true},{"flags-1b.pmx",false,true,true},
            {"flags-0c.pmx",true,true,false},{"alpha98.pmd",false,true,true}}) {
        RuntimeCpuScene scene;CHECK(scene.LoadModel(root/item.name,reason));
        CHECK(scene.Model(0)->PrepareMaterials(textures,reason));SceneResourceResolver resolver(root/item.name);
        RuntimeCpuSceneProducer producer;CHECK(producer.Load(scene,resolver,true,assets,reason));
        SelfShadowSettings settings;settings.mode=1;settings.mapSize=64;RenderFrame frame;
        CHECK(producer.BuildFrame(scene,Frame(),target,settings,frame,reason));
        CHECK(frame.passes.size()==2 && frame.targets.size()==4 && ValidateRenderFrame(frame));
        CHECK((!frame.passes[0].draws.empty())==item.cast);
        bool material=false,outline=false;
        for(const auto& draw:frame.passes[1].draws) {
            if(draw.pipeline.shadow.mode==BuiltinShadowMode::Material) {
                material=true;CHECK(draw.pipeline.shadow.receive==item.receive && draw.shadowTarget==3);
            } else outline=true;
        }
        CHECK(material && outline==item.outline);
        settings.mode=0;CHECK(producer.BuildFrame(scene,Frame(),target,settings,frame,reason));
        CHECK(frame.passes.size()==1 && frame.targets.size()==2);
        settings.mode=3;const auto before=frame.resources.size();
        CHECK(!producer.BuildFrame(scene,Frame(),target,settings,frame,reason) && frame.resources.size()==before);
    }
    // .x import has no native mesh/device ownership; a failed reload is atomic.
    RuntimeCpuAccessory accessory;CHECK(accessory.Load(root/"bound.x",1,reason));
    const auto* geometry=accessory.Geometry();CHECK(geometry && geometry->vertexCount==4 && geometry->indices.bytes.size()==24);
    const auto vertices=geometry->main.bytes;CHECK(!accessory.Load(root/"missing.x",1,reason) && accessory.Geometry()->main.bytes==vertices);
    for(const char* name:{"bound-v1.pmm","bound-v2.pmm","passes-v2.pmm"}){
        SceneResourceResolver resolver(root/name);SceneSession session;std::ifstream file(root/name,std::ios::binary);
        CHECK(SceneSession::Load(Read,&file,SceneResourceResolver::Resolve,&resolver,session));
        RuntimeCpuScene scene;CHECK(scene.Load(session,resolver.Resolved(),reason));
        for(auto slot:scene.Order())CHECK(scene.Model(slot)->PrepareMaterials(textures,reason));
        RuntimeCpuSceneProducer producer;CHECK(producer.Load(scene,resolver,true,assets,reason));
        SelfShadowSettings shadow;shadow.mode=1;RenderFrame unsupported;
        CHECK(!producer.BuildFrame(scene,Frame(),target,shadow,unsupported,reason) &&
              reason=="cpu_self_shadow_accessories_not_migrated" && unsupported.passes.empty());
        ModelReplayStream stream;CHECK(producer.Build(scene,Frame(),target,stream,reason));
        const bool ground=std::string(name)=="passes-v2.pmm";
        auto expected=std::vector<std::string>{"accessory_material","accessory_material","model_material","model_outline","accessory_material","accessory_material"};
        if(ground)expected.insert(expected.begin(),{"ground_lines","ground_plane"}),expected.insert(expected.begin()+4,"projected_model_shadow");
        if(Labels(stream)!=expected){std::cerr<<name<<" actual draws:";for(const auto& label:Labels(stream))std::cerr<<" "<<label;std::cerr<<"\n";}
        CHECK(Labels(stream)==expected);
        ModelReplayStream same;CHECK(producer.Build(scene,Frame(),target,same,reason) && SameKeys(stream,same));
        ModelMatrix bone;
        for(unsigned frame:{0u,3u,10u}){
            CHECK(scene.Seek(frame,reason));CpuSolvedModel solved;CHECK(scene.Snapshot(1,solved));
            CHECK(scene.BoneAttachment(1,1,bone,reason));
            // Independent D3DX composition from the PMD rest position (0,6,0)
            // and the final solved matrix, rather than an animation-only pose.
            using M=d3dx::D3DXMATRIXF;M rest,skin,expectedBone;auto& math=d3dx::Get();
            math.translation(&rest,0,6,0);std::memcpy(&skin,solved.bones[1].skin.data(),64);math.multiply(&expectedBone,&rest,&skin);
            CHECK(!std::memcmp(bone.data(),&expectedBone,64));
            const auto& pose=scene.Session()->Pose().accessories[0];M local,part;
            math.scaling(&local,pose.scale*10,pose.scale*10,pose.scale*10);
            math.rotZ(&part,pose.rotation[2]);math.multiply(&local,&local,&part);math.rotX(&part,pose.rotation[0]);math.multiply(&local,&local,&part);
            math.rotY(&part,pose.rotation[1]);math.multiply(&local,&local,&part);math.translation(&part,pose.position[0],pose.position[1],pose.position[2]);math.multiply(&local,&local,&part);
            math.multiply(&local,&local,&rest);math.multiply(&local,&local,&skin);ModelMatrix placement;
            CHECK(scene.AccessoryPlacement(0,placement,reason) && !std::memcmp(placement.data(),&local,64));
            auto f=Frame();CHECK(scene.FrameInputs(f,reason));
            CHECK(f.world[12]==-bone[12] && f.world[13]==-bone[13] && f.world[14]==-bone[14]);
            CHECK(producer.Build(scene,Frame(),target,same,reason));
            CpuSolvedModel after;CHECK(scene.Snapshot(1,after) && after.bodies==solved.bodies);
            for(std::size_t n=0;n<after.bones.size();++n)CHECK(after.bones[n].skin==solved.bones[n].skin);
            bool opacity=false;for(const auto& e:same.events)if(e.label=="accessory_material"){
                opacity=true;CHECK(e.draw.pipeline.shader.nativeDiffuse[3]==.8f*pose.opacity || e.draw.pipeline.shader.nativeDiffuse[3]==.6f*pose.opacity);}
            CHECK(opacity);
        }
        auto guard=bone; // No stale result is published for a missing attachment.
        CHECK(!scene.BoneAttachment(255,0,guard,reason) && guard==bone);
        CHECK(!scene.BoneAttachment(1,-1,guard,reason) && guard==bone);
        CHECK(scene.Edit([](auto& d){d.accessories[0].keyRecords.dense[0].visible=0;d.accessories[0].keyRecords.sparse[0].value.visible=0;},reason));
        CHECK(producer.Build(scene,Frame(),target,same,reason) && Labels(same).size()==expected.size()-2);
        // Save/readback must preserve edited accessory tracks.
        std::ostringstream saved(std::ios::binary);CHECK(scene.Session()->Save(Write,&saved));std::istringstream input(saved.str(),std::ios::binary);SceneSession reread;
        CHECK(SceneSession::Load(Read,&input,SceneResourceResolver::Resolve,&resolver,reread));CHECK(!reread.Pose().accessories[0].visible);
        const auto old=same.events.size();CHECK(scene.Edit([](auto& d){d.accessories[1].keyRecords.dense[0].parentModel=254;d.accessories[1].keyRecords.sparse[0].value.parentModel=254;},reason));
        CHECK(!producer.Build(scene,Frame(),target,same,reason) && same.events.size()==old && reason=="attachment_model_not_loaded");
        std::cout<<"PASS CPU attachments/order/edit/save "<<name<<"\n";
    }
    // Display order differs from physics dependency order. Distinct resource IDs
    // identify which model is drawn first without adding native render metadata.
    {SceneResourceResolver resolver(root/"physics-v2.pmm");SceneSession session;std::ifstream file(root/"physics-v2.pmm",std::ios::binary);
        CHECK(SceneSession::Load(Read,&file,SceneResourceResolver::Resolve,&resolver,session));RuntimeCpuScene scene;CHECK(scene.Load(session,resolver.Resolved(),reason));
        for(auto slot:scene.Order())CHECK(scene.Model(slot)->PrepareMaterials(textures,reason));RuntimeCpuSceneProducer producer;CHECK(producer.Load(scene,resolver,false,assets,reason));
        ModelReplayStream first,swapped;CHECK(producer.Build(scene,Frame(),target,first,reason));
        SceneResourceResolver reverseResolver(root/"reverse-v2.pmm");SceneSession reverseSession;std::ifstream reverseFile(root/"reverse-v2.pmm",std::ios::binary);
        CHECK(SceneSession::Load(Read,&reverseFile,SceneResourceResolver::Resolve,&reverseResolver,reverseSession));
        RuntimeCpuScene reverse;CHECK(reverse.Load(reverseSession,reverseResolver.Resolved(),reason) && reverse.Order()[0]==1);
        for(auto slot:reverse.Order())CHECK(reverse.Model(slot)->PrepareMaterials(textures,reason));
        CHECK(producer.Build(reverse,Frame(),target,swapped,reason));std::vector<std::uint64_t> a,b;
        for(const auto& e:first.events)if(e.op==ModelReplayOp::Draw)a.push_back(e.draw.vertices.id);
        for(const auto& e:swapped.events)if(e.op==ModelReplayOp::Draw)b.push_back(e.draw.vertices.id);
        if(a.size()!=2 || b.size()!=2 || a[0]!=b[1] || a[1]!=b[0]){std::cerr<<"display order IDs before:";for(auto id:a)std::cerr<<" "<<id;std::cerr<<" after:";for(auto id:b)std::cerr<<" "<<id;std::cerr<<"\n";}
        CHECK(a.size()==2 && b.size()==2 && a[0]==b[1] && a[1]==b[0]);}
    std::cout<<"PASS CPU bindings, display/dependency ordering, static X import, opacity, visibility, stable resources, fail-closed frames; no graphics device\n";
}
