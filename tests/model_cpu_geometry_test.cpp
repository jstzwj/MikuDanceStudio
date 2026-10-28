// Exercise the actual deformation producer and legacy workers, not a second
// implementation of their arithmetic. No native graphics device is needed.
#include "../src/model/model_skinning.cpp"
#include <memory>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
using namespace mikudancestudio;
int main(){
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS",nullptr);
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_AUDIT",nullptr);
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_ACCEPTED",nullptr);
    SetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY","1");
    auto app=std::make_unique<MMDApp>();auto model=std::make_unique<mdl::ModelRecord>();
    auto* bytes=model.get();
    constexpr unsigned count=67;mdl::BoneRecord bones[4]{};mdl::ModelMaterialRecord material{};
    std::vector<mdl::PmxVertex> source(count);std::vector<std::int32_t> indices(count);
    model->vertexCount=count;model->indexCount=count;model->boneCount=4;model->boneTable=bones;
    model->physicsMode=2;model->pmxVertexIndexSize=4;model->pmxVertices=source.data();model->indices=indices.data();model->materials=&material;
    material.edgeSize=.75f;material.edgeColor[0]=.125f;material.edgeColor[1]=.25f;material.edgeColor[2]=.5f;material.edgeColor[3]=.375f;
    for(unsigned b=0;b<4;++b){for(unsigned c=0;c<4;++c)bones[b].matInit[c*5]=1;for(unsigned c=0;c<3;++c)bones[b].matInit[12+c]=(b+1)*.25f*(c+1);}
    for(unsigned i=0;i<count;++i){auto& v=source[i];indices[i]=i;
        for(unsigned c=0;c<3;++c){v.position[c]=(i+1)*.03125f*(c+1);v.normal[c]=.125f*(c+1);v.sdef.center[c]=.25f*(c+1);}
        v.uv[0]=i*.125f;v.uv[1]=-static_cast<float>(i)*.25f;v.weightType=static_cast<mdl::PmxWeightType>(i%5);
        for(unsigned b=0;b<4;++b){v.bone[b]=b;v.weight[b]=.25f;}
        v.edgeScale=.5f;v.hasPmxEdgeData=i%2;
        for(unsigned c=0;c<4;++c)for(unsigned u=0;u<4;++u)v.additionalUvByComponent[c][u]=(i+1)*(c+1)*(u+1)*.0625f;
    }
    std::uint64_t previousId=0;
    for(unsigned uv=0;uv<=4;++uv){model->pmxAdditionalUvCount=uv;InitializeModelCpuGeometry(app.get(),bytes);
        CHECK(app->CpuModelFrames().size()==1);auto& frame=*app->CpuModelFrames().at(bytes);auto& geometry=frame.geometry;
        CHECK(geometry.Snapshot().main.key.id>previousId);previousId=geometry.Snapshot().main.key.id;
        CHECK(geometry.MainSize()==count*(32+16*uv));
        render::ModelGeometry legacy(100, count,uv);std::memcpy(legacy.MainData(),geometry.MainData(),geometry.MainSize());legacy.Edges()=geometry.Edges();
        for(unsigned repeat=0;repeat<8;++repeat){
            bones[1].matInit[12]+=.03125f;source[4].position[0]+=1; // QDEF must retain its initialized pose.
            const bool opaque=repeat%2!=0;
            SkinPmxDispatch(bytes,.125f,opaque,uv,legacy.Edges().data(),legacy.MainData());
            CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,opaque)==&frame);
            CHECK(!std::memcmp(legacy.MainData(),geometry.MainData(),geometry.MainSize()));
            CHECK(!std::memcmp(legacy.Edges().data(),geometry.Edges().data(),count*16));
            CompleteCpuGeometry(app.get(),bytes,&frame,legacy.MainData(),legacy.Edges().data(),false);CHECK(frame.verified && frame.ready && !frame.failed);
            const auto* retained=reinterpret_cast<const mdl::SkinnedVertexBase*>(static_cast<const unsigned char*>(geometry.MainData())+4*(32+16*uv));
            CHECK(retained->position[0]!=(source[4].position[0]));
        }
        const auto version=geometry.Snapshot().main.key.version;CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,true) && geometry.Snapshot().main.key.version==version);
        CHECK(frame.verified && !frame.ready); // A missing native comparison cannot draw a previous-ready frame.
        // Restore/apply morphs before skinning. Reapplying one weight must not
        // accumulate deformation; zero weight must restore the load-time base.
        mdl::PmdVertexMorphEntry base{0,{1,2,3}},offset{0,{.5f,-.25f,.125f}};
        mdl::PmxUvMorphEntry uvBase{0,{.125f,.25f,0,0}},uvOffset{0,{.5f,-.5f,0,0}};
        mdl::MorphRecord morphs[2]{};morphs[0].type=1;morphs[0].value=.5f;morphs[0].offsetCount=1;morphs[0].vertexEntries=&offset;
        morphs[1].type=3;morphs[1].value=.5f;morphs[1].uvCounts[0]=1;morphs[1].uvEntries[0]=&uvOffset;
        model->morphs=morphs;model->morphCount=2;model->morph0Table=&base;model->morph0Count=1;
        model->uvMorphTables.byFamily[0]=&uvBase;model->uvMorphCounts.byFamily[0]=1;
        ApplyPmxVertexMorphs(bytes);CHECK(source[0].position[0]==1.25f && source[0].uv[0]==.375f);
        SkinPmxDispatch(bytes,.125f,true,uv,legacy.Edges().data(),legacy.MainData());
        CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,true));
        CHECK(!std::memcmp(legacy.MainData(),geometry.MainData(),geometry.MainSize()));
        const auto morphed=geometry.Snapshot();ApplyPmxVertexMorphs(bytes);CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,true));
        CHECK(geometry.Snapshot().main.key.version==morphed.main.key.version);
        morphs[0].value=morphs[1].value=0;ApplyPmxVertexMorphs(bytes);CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,true));
        CHECK(source[0].position[0]==1 && source[0].uv[0]==.125f && geometry.Snapshot().main.bytes!=morphed.main.bytes);
        model->morphs=nullptr;model->morphCount=0;model->morph0Table=nullptr;model->morph0Count=0;
        model->uvMorphTables.byFamily[0]=nullptr;model->uvMorphCounts.byFamily[0]=0;
        // A detected mismatch closes the gate; copied uploads are never enabled.
        static_cast<unsigned char*>(legacy.MainData())[0]^=1;
        CompleteCpuGeometry(app.get(),bytes,&frame,legacy.MainData(),legacy.Edges().data(),false);CHECK(frame.failed && !frame.verified && !ProduceCpuGeometry(app.get(),bytes,.125f,true));
    }
    // Reject a malformed narrow PMX index before truncating it to UInt16.
    model->pmxVertexIndexSize=2;indices[0]=65536;InitializeModelCpuGeometry(app.get(),bytes);
    CHECK(app->CpuModelFrames().empty());indices[0]=0;
    // PMD uses contiguous 32-bit CPU indices and must preserve its load-time edge color.
    std::vector<mdl::PmdVertex> pmd(count);std::vector<std::int32_t> pmdIndices(count);
    model->physicsMode=0;model->rawVertices=pmd.data();model->indices=pmdIndices.data();model->pmxVertices=nullptr;
    for(unsigned i=0;i<count;++i){auto& v=pmd[i];v.position[0]=i*.125f;v.normal[1]=.5f;v.bone[0]=0;v.bone[1]=1;v.weightPercent=i%101;pmdIndices[i]=i;}
    InitializeModelCpuGeometry(app.get(),bytes);CHECK(app->CpuModelFrames().size()==1);auto& pmdFrame=*app->CpuModelFrames().at(bytes);
    CHECK(pmdFrame.geometry.Snapshot().indices.bytes.size()==count*2 && pmdFrame.geometry.Snapshot().indices.indexFormat==rhi::IndexFormat::UInt16);
    for(unsigned i=0;i<count;++i){std::uint16_t index=0;std::memcpy(&index,pmdFrame.geometry.Snapshot().indices.bytes.data()+i*2,2);CHECK(index==i);}
    auto original=pmdFrame.geometry.Edges();for(unsigned i=0;i<count;++i)pmdFrame.geometry.Edges()[i].diffuse=0x80102030u+i;
    std::vector<mdl::SkinnedVertexBase> main(count);auto edge=pmdFrame.geometry.Edges();SkinPmd(bytes,.125f,edge.data(),main.data());
    CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,false));CHECK(!std::memcmp(main.data(),pmdFrame.geometry.MainData(),count*32));CHECK(!std::memcmp(edge.data(),pmdFrame.geometry.Edges().data(),count*16));
    mdl::PmdVertexMorphEntry pmdBase{0,{1,2,3}},pmdOffset{0,{.5f,-.25f,.125f}};mdl::MorphRecord pmdMorphs[2]{};
    model->morph0Count=1;model->morph0Table=&pmdBase;model->morphCount=2;model->morphs=pmdMorphs;
    pmdMorphs[1].value=.5f;pmdMorphs[1].offsetCount=1;pmdMorphs[1].vertexEntries=&pmdOffset;
    ApplyPmdVertexMorphs(bytes);CHECK(pmd[0].position[0]==1.25f);CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,false));
    SkinPmd(bytes,.125f,edge.data(),main.data());CHECK(!std::memcmp(main.data(),pmdFrame.geometry.MainData(),count*32));
    const auto morphedPmd=pmdFrame.geometry.Snapshot();ApplyPmdVertexMorphs(bytes);CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,false));
    CHECK(pmdFrame.geometry.Snapshot().main.key.version==morphedPmd.main.key.version);
    pmdMorphs[1].value=0;ApplyPmdVertexMorphs(bytes);CHECK(pmd[0].position[0]==1 && ProduceCpuGeometry(app.get(),bytes,.125f,false));
    CHECK(pmdFrame.geometry.Snapshot().main.bytes!=morphedPmd.main.bytes);
#ifdef MIKUDANCESTUDIO_DIAG
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS","1");SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_ACCEPTED","1");
    CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,false) && pmdFrame.ready && pmdFrame.verified);
    // Direct mode completes without dereferencing any native buffer bytes.
    CompleteCpuGeometry(app.get(),bytes,&pmdFrame,nullptr,nullptr,false);CHECK(pmdFrame.ready && !pmdFrame.failed);
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_AUDIT","1");CHECK(ProduceCpuGeometry(app.get(),bytes,.125f,false) && !pmdFrame.ready);
    SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS",nullptr);SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_AUDIT",nullptr);SetEnvironmentVariableA("MDS_RHI_CPU_INPUTS_ACCEPTED",nullptr);
#endif
    SetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY","0");InitializeModelCpuGeometry(app.get(),bytes);CHECK(app->CpuModelFrames().empty());
    std::puts("CPU producer: PMD, PMX BDEF1/2/4/SDEF/QDEF retention, all UV strides, fresh IDs and failure gate passed");return 0;
}
