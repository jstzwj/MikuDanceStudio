#include "mikudancestudio/runtime_cpu_scene.hpp"
#include "mikudancestudio/runtime_cpu_tracks.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "cpu_simulation_reference.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <sstream>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
using namespace mikudancestudio;
using namespace mikudancestudio::format;
#define CHECK(x) do{if(!(x)){std::cerr<<"line "<<__LINE__<<": "<<#x<<" reason="<<reason<<"\n";return 1;}}while(0)
namespace {
LONG WINAPI CrashReport(EXCEPTION_POINTERS* exception){
    const auto process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
    std::cerr<<"CPU scene exception=0x"<<std::hex<<exception->ExceptionRecord->ExceptionCode<<std::dec<<"\n";
    auto context=*exception->ContextRecord;STACKFRAME64 frame{};DWORD machine;
#if defined(_M_IX86)
    machine=IMAGE_FILE_MACHINE_I386;frame.AddrPC.Offset=context.Eip;frame.AddrStack.Offset=context.Esp;frame.AddrFrame.Offset=context.Ebp;
#else
    machine=IMAGE_FILE_MACHINE_AMD64;frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rbp;
#endif
    frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
    for(unsigned n=0;n<24;++n){char bytes[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(bytes);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement=0;
        std::cerr<<"0x"<<std::hex<<frame.AddrPC.Offset<<std::dec;
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::cerr<<" "<<symbol->Name<<"+"<<displacement;std::cerr<<"\n";
        if(!StackWalk64(machine,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;}
    return EXCEPTION_EXECUTE_HANDLER;
}
int Read(void* p,void* data,std::size_t size){auto& file=*static_cast<std::istream*>(p);file.read(static_cast<char*>(data),size);return int(file.gcount());}
struct Reference {
    PhysicsScene physics{};SceneSession session;CpuSimulationState state;bool playing=false;
    std::array<std::unique_ptr<mdl::ModelRecord>,255> models;
    std::array<mdl::ModelRecord*,255> slots{};
    ~Reference(){for(auto i=models.rbegin();i!=models.rend();++i)if(*i)ModelDispose(i->get());DisposePhysicsWorld(&physics);}
    CpuSimulationContext Context(){return {{slots.data(),slots.size()},physics.world,LegacyCpuSimulationOps()};}
    bool Load(const SceneSession& source,const std::map<std::uint8_t,std::filesystem::path>& paths,std::string& reason){
        session=source;if(!SceneConstruct(&physics,nullptr))return false;
        state.physicsMode=session.Document().physics.playbackMode;
        for(const auto& source:session.Document().models){auto& record=models[source.slot];record=std::make_unique<mdl::ModelRecord>();auto* raw=record.get();ModelInitDefaults(raw);PathResolutionWorkspace workspace{};
            if(!ModelLoadPMD(raw,nullptr,paths.at(source.slot).c_str(),nullptr,0,0,1,&physics,workspace,ModelResourceMode::CpuOnly) ||
               !PublishCpuPmmTracks(raw,source,session.Document().envelope.header.version,reason))return false;
            slots[source.slot]=raw;
            // Independent assertions of disk-to-native link/payload publication.
            const auto& m=*raw;
            for(std::size_t n=0;n<source.boneKeys.dense.size();++n)if(m.boneKeys[n].next!=source.boneKeys.dense[n].next)return false;
            for(const auto& key:source.boneKeys.sparse)if(m.boneKeys[key.index].physicsDisabled!=key.value.physicsDisabled ||
                std::memcmp(m.boneKeys[key.index].position,key.value.position.data(),12))return false;
        }
        return Seek(session.Pose().frame);
    }
    bool Apply(){for(std::size_t n=0;n<session.Pose().models.size();++n){const auto& pose=session.Pose().models[n];
        if(!ApplyMappedCpuPmmPose(slots[pose.slot],session.Document().models[n],pose,session.Document().envelope.header.version))return false;}return true;}
    void Gravity(){const auto& g=session.Pose().gravity;state.gravityMagnitude=g.acceleration;state.gravityDirection=g.direction;state.gravityNoise=g.noise;state.gravityNoiseEnabled=g.noiseEnabled;}
    bool Seek(unsigned frame){if(!session.Seek(frame))return false;playing=false;
        for(auto* raw:slots)if(raw)SeekModelFrame(*raw,int(frame),state.physicsMode);
        if(!Apply())return false;Gravity();state.cursorSeconds=float(frame)/30;state.remainingSeconds=0;state.resetPending=1;
        cpu_reference::Frame(state,Context(),{});return true;}
    bool Start(){if(!session.StartPlayback() || !session.Advance(state.cursorSeconds))return false;
        for(auto* raw:slots)if(raw)InitModelTrackCursors(raw,state.cursorSeconds,state.physicsMode);
        playing=true;Gravity();return Apply();}
    static void AdvanceTracks(void* user,CpuSimulationState& state,bool substep){auto& r=*static_cast<Reference*>(user);
        for(auto* raw:r.slots)if(raw)AdvanceModelKeyframes(raw,state.cursorSeconds,state.physicsMode);
        if(!r.session.Advance(state.cursorSeconds) || !r.Apply())std::abort();if(!substep)r.Gravity();}
    void Advance(float target,float delta){state.remainingSeconds=delta;cpu_reference::Catchup(state,Context(),target,-1,{this,AdvanceTracks,nullptr});
        CpuSimulationFrame frame;frame.deltaSeconds=delta;frame.playbackActive=playing;cpu_reference::Frame(state,Context(),frame);}
    bool Equal(const RuntimeCpuScene& scene,unsigned checkpoint){
        for(const auto& source:session.Document().models){CpuSolvedModel actual;if(!scene.Snapshot(source.slot,actual))return false;const auto& m=*slots[source.slot];
            if(actual.visible!=m.loadComplete || actual.displayCursor!=m.displayKeyCursor || actual.displayActive!=m.displayTrackActive)return false;
            for(unsigned n=0;n<m.boneCount;++n){const auto& x=actual.bones[n];const auto& y=m.boneTable[n];
                d3dx::D3DXMATRIXF rest,skin,expected;auto& math=d3dx::Get();
                math.translation(&rest,y.position[0],y.position[1],y.position[2]);std::memcpy(&skin,y.matInit,64);math.multiply(&expected,&rest,&skin);
                render::ModelMatrix attached;std::string reason;
                if(!scene.BoneAttachment(source.slot,n,attached,reason) || std::memcmp(attached.data(),&expected,64))return false;
                const auto equal=[&](const float* a,const float* b,unsigned count,const char* field){for(unsigned c=0;c<count;++c)if(!std::isfinite(a[c]) || std::memcmp(a+c,b+c,4)){
                    std::cerr<<"slot="<<unsigned(source.slot)<<" bone="<<n<<" checkpoint="<<checkpoint<<" field="<<field<<" component="<<c<<" actual="<<a[c]<<" expected="<<b[c]<<"\n";return false;}return true;};
                if(!equal(x.skin.data(),y.matInit,16,"skin") || !equal(x.local.data(),y.matLocal,16,"local") || !equal(x.world.data(),y.matWorld,16,"world") || !equal(x.extra.data(),y.matExtra,16,"extra") ||
                   !equal(x.pose.data(),y.trans,3,"translation") || !equal(x.pose.data()+3,y.rotQuat,4,"rotation") || !equal(x.backup.data(),y.ikBackup,7,"backup") ||
                   !equal(x.working.data(),y.ikWorkingPos,3,"working_translation") || !equal(x.working.data()+3,y.ikWorkingQuat,4,"working_rotation") ||
                   x.cursor!=m.boneKeyCursors[n] || x.active!=m.boneTrackActive[n] || x.physicsDisabled!=y.physicsDisabled || x.armedFrame!=y.rigidIdx)return false;
            }
            for(unsigned n=0;n<m.ikChainCount;++n)if(actual.ikEnabled[n]!=m.ikChains[n].enabled)return false;
            for(unsigned n=0;n<m.morphCount;++n)if(actual.morphs[n]!=m.morphs[n].value)return false;
            for(unsigned n=0;n<m.rigidCount;++n){const auto* body=static_cast<btRigidBody*>(m.rigidTable[n].body);std::array<float,38> expected{};
                body->getWorldTransform().getOpenGLMatrix(expected.data());body->getInterpolationWorldTransform().getOpenGLMatrix(expected.data()+16);
                for(unsigned c=0;c<3;++c){expected[32+c]=body->getLinearVelocity()[c];expected[35+c]=body->getAngularVelocity()[c];}
                if(actual.bodies[n]!=expected){std::cerr<<"body mismatch slot="<<unsigned(source.slot)<<" body="<<n<<" checkpoint="<<checkpoint<<"\n";return false;}}
        }
        return true;
    }
};
}
int main(int argc,char** argv){
    std::cout<<std::unitbuf;
    SetUnhandledExceptionFilter(CrashReport);
    const bool external=argc==4 && std::string(argv[1])=="--scene";
    if(argc!=2 && !external)return 2;std::string reason;
    const auto root=std::filesystem::absolute(external?std::filesystem::path(argv[2]).parent_path():std::filesystem::path(argv[1]));
    std::vector<std::string> names=external?std::vector<std::string>{std::filesystem::path(argv[2]).filename().string()}:
        std::vector<std::string>{"v1.pmm","v2.pmm","pmx.pmm","ik-v1.pmm","physics-v2.pmm","sdef-physics-v2.pmm","bound-v1.pmm","bound-v2.pmm","passes-v2.pmm"};
    unsigned checks=0;
    for(const auto& name:names){
        const auto path=root/name;SceneResourceResolver resolver(path);SceneSession session;std::ifstream file(path,std::ios::binary);
        if(external){const auto directory=std::filesystem::absolute(argv[3]);
            resolver.SetPathDecoder([directory](const std::string& raw)->std::optional<std::filesystem::path>{return directory/std::filesystem::path(raw).filename();});}
        if(!SceneSession::Load(Read,&file,SceneResourceResolver::Resolve,&resolver,session)){file.clear();std::cerr<<"load failed "<<name<<" offset="<<file.tellg()<<"\n";return 1;}
        std::size_t expectedBodies=0;
        for(const auto& model:session.Models()){CHECK(model);expectedBodies+=model->kind==ModelKind::Pmd?model->pmd.rigids.size():model->pmx.rigids.size();}
        const bool physics=std::string(name).find("physics")!=std::string::npos;
        for(int mode=0;mode<(physics && !external?4:1);++mode){auto source=session;
            if(physics)CHECK(source.Edit([&](auto& document){document.physics.playbackMode=std::uint8_t(mode);}));
            RuntimeCpuScene scene;Reference reference;CHECK(scene.Load(source,resolver.Resolved(),reason));CHECK(reference.Load(source,resolver.Resolved(),reason));
            CHECK(scene.BodyCount()==expectedBodies);CHECK(reference.Equal(scene,checks++));
            for(unsigned frame:{3u,10u,15u,0u}){CHECK(scene.Seek(frame,reason));CHECK(reference.Seek(frame));CHECK(reference.Equal(scene,checks++));
                if(physics && mode>=2 && frame==10){CpuSolvedModel pose;CHECK(scene.Snapshot(1,pose));CHECK(pose.bones[0].physicsDisabled==1);}}
            CHECK(scene.StartPlayback(reason));CHECK(reference.Start());
            float previous=0;
            for(float target:{.03f,.09f,.21f,.33333334f,.4f,.5f}){CHECK(scene.Advance(target,target-previous,reason));reference.Advance(target,target-previous);
                CHECK(reference.Equal(scene,checks++));previous=target;}
            scene.Pause();reference.playing=false;CHECK(scene.Tick(.02f,reason));reference.state.remainingSeconds=.02f;CpuSimulationFrame frame;frame.deltaSeconds=.02f;
            cpu_reference::Frame(reference.state,reference.Context(),frame);CHECK(reference.Equal(scene,checks++));
            const auto drawable=std::find_if(scene.Order().begin(),scene.Order().end(),[&](auto slot){return scene.Model(slot)->Geometry()!=nullptr;});CHECK(drawable!=scene.Order().end());
            const auto drawSlot=*drawable;const auto vertices=scene.Model(drawSlot)->Geometry()->main.bytes;
            CHECK(!scene.Advance(.6f,.1f,reason));CHECK(scene.Model(drawSlot)->Geometry()->main.bytes==vertices);
            CHECK(!scene.Load(source,{},reason));CHECK(scene.Model(drawSlot)->Geometry()->main.bytes==vertices);
            if(physics){const auto oldBodies=scene.BodyCount();CHECK(!scene.Edit([](auto& document){document.models[0].boneKeys.dense[0].next=999999;},reason));CHECK(scene.BodyCount()==oldBodies);
                CHECK(!scene.Edit([](auto& document){document.physics.playbackMode=255;},reason));
                const auto edit=[](auto& document){document.models[0].boneKeys.sparse[0].value.position[0]+=.25f;document.models[0].displayKeys.dense[0].ikEnabled[0]=0;};
                CHECK(scene.Edit(edit,reason));CHECK(reference.session.Edit(edit));
                for(const auto& model:reference.session.Document().models)CHECK(PublishCpuPmmTracks(reference.slots[model.slot],model,reference.session.Document().envelope.header.version,reason));
                CHECK(reference.Seek(reference.session.Pose().frame));CHECK(reference.Equal(scene,checks++));
                CHECK(scene.Load(source,resolver.Resolved(),reason));CHECK(scene.BodyCount()==oldBodies);}
            std::cout<<"PASS CPU scene "<<name<<" physicsMode="<<scene.Simulation().physicsMode<<" models="<<scene.Order().size()<<" bodies="<<scene.BodyCount()<<"\n";
        }
    }
    if(!external){RuntimeCpuScene boneOnly;CHECK(boneOnly.LoadModel(root/"bone-only.pmd",reason));
        CHECK(!boneOnly.Model(0)->Geometry() && boneOnly.BodyCount()==0 && boneOnly.Tick(.03f,reason));
        CpuSolvedModel pose;CHECK(boneOnly.Snapshot(0,pose) && pose.bones.size()==1);}
    {RuntimeCpuScene empty;render::ModelMatrix out;out.fill(17);const auto saved=out;
        CHECK(!empty.BoneAttachment(0,0,out,reason) && out==saved);
        CHECK(!empty.Model(255) && !empty.Visible(255));}
    std::cout<<"PASS shared CPU scene: "<<checks<<" exact native checkpoints, IK, physics arming, collisions, PMX bone morph/SDEF, atomic reload/edit; no graphics device\n";
}
