// CPU-only native reference: call the actual PMX morph/BDEF workers directly.
// No legacy loader, MMDApp, HWND, graphics device or modal error path is entered.
#include "../src/model/model_skinning.cpp"
#include "mikudancestudio/model_runtime.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x) do {if(!(x)){std::cerr<<"line "<<__LINE__<<": "<<#x<<" reason="<<reason<<'\n';return 1;}}while(0)
bool Equal(const float* a,const float* b,unsigned size) {
    for(unsigned n=0;n<size;++n) if(!std::isfinite(a[n]) || !std::isfinite(b[n]) ||
        std::abs(a[n]-b[n])>2e-5f*std::max(1.f,std::abs(b[n]))) {
        std::cerr<<"component="<<n<<" portable="<<a[n]<<" native="<<b[n]<<'\n';return false;
    }
    return true;
}
int main(int argc,char** argv) {
    CHECK(argc==3);
    std::ifstream input(argv[1],std::ios::binary);format::ModelDocument document;
    const auto read=[](void* p,void* out,std::size_t size){auto& in=*static_cast<std::istream*>(p);in.read(static_cast<char*>(out),size);return int(in.gcount());};
    CHECK(format::ReadModelDocument(read,&input,document));
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file(std::fopen(argv[2],"rb"),std::fclose);
    format::VmdDocument motion;CHECK(file && format::ReadVmd(file.get(),motion));
    std::shared_ptr<const runtime::ModelResource> resource;std::shared_ptr<const runtime::MotionClip> clip;
    CHECK(runtime::ModelResource::Create(document,resource,reason));
    CHECK(runtime::MotionClip::Create(resource,motion,clip,reason));
    runtime::ModelInstance instance(resource);CHECK(instance.BindMotion(clip,reason));
    const auto& model=document.pmx;
    for(unsigned frame : {0u,4u,8u,12u,4u,0u}) {
        CHECK(instance.Seek(frame,reason));const auto& snapshot=instance.Snapshot();
        std::vector<mdl::BoneRecord> bones(model.bones.size());std::vector<bool> completed(bones.size());
        std::size_t count=0;
        while(count<bones.size()) {
            const auto previous=count;
            for(std::size_t n=0;n<bones.size();++n) if(!completed[n]) {
                const auto& bone=model.bones[n].fields;
                if(bone.parent>=0 && !completed[bone.parent]) continue;
                d3dx::D3DXMATRIXF rotation,negativeRest,offset,rest,local,temporary,skin;
                auto& api=d3dx::Get();
                api.matrixRotationQuaternion(&rotation,snapshot.bones[n].rotation.data());
                api.translation(&negativeRest,-bone.position[0],-bone.position[1],-bone.position[2]);
                const auto& p=snapshot.bones[n].translation;
                api.translation(&offset,p[0],p[1],p[2]);
                api.translation(&rest,bone.position[0],bone.position[1],bone.position[2]);
                api.multiply(&temporary,&negativeRest,&rotation);
                api.multiply(&local,&temporary,&offset);api.multiply(&temporary,&local,&rest);
                if(bone.parent>=0) {
                    d3dx::D3DXMATRIXF parent;std::memcpy(&parent,bones[bone.parent].matInit,64);
                    api.multiply(&skin,&temporary,&parent);
                } else skin=temporary;
                CHECK(Equal(snapshot.bones[n].local.data(),&temporary.m[0][0],16));
                CHECK(Equal(snapshot.bones[n].skin.data(),&skin.m[0][0],16));
                std::memcpy(bones[n].matInit,&skin,64);completed[n]=true;++count;
            }
            CHECK(count>previous);
        }
        std::vector<mdl::PmxVertex> vertices(model.vertices.size());
        for(std::size_t n=0;n<vertices.size();++n) {
            const auto& source=model.vertices[n];auto& out=vertices[n];
            std::copy_n(source.position,3,out.position);std::copy_n(source.normal,3,out.normal);
            std::copy_n(source.bone,4,out.bone);std::copy_n(source.weight,4,out.weight);
        }
        for(std::size_t n=0;n<model.morphs.size();++n) if(snapshot.morphWeights[n]!=0) {
            std::vector<mdl::PmdVertexMorphEntry> offsets;
            for(const auto& source:model.morphs[n].offsets) {
                mdl::PmdVertexMorphEntry offset{};offset.vertexIndex=source.index;
                std::copy_n(source.values,3,offset.offset);offsets.push_back(offset);
            }
            mdl::MorphRecord morph{};morph.type=1;morph.offsetCount=int(offsets.size());morph.vertexEntries=offsets.data();
            AccumPmxVertexMorph(vertices.data(),morph,snapshot.morphWeights[n],1);
        }
        for(std::size_t n=0;n<vertices.size();++n) {
            mdl::SkinnedVertexBase out{};
            switch(model.vertices[n].weightType) {
            case 0:SkinPmxBdef1(vertices[n],bones.data(),int(bones.size()),out);break;
            case 1:SkinPmxBdef2(vertices[n],bones.data(),int(bones.size()),out);break;
            case 2:SkinPmxBdef4(vertices[n],bones.data(),int(bones.size()),out);break;
            default:CHECK(false);
            }
            CHECK(Equal(snapshot.vertices[n].position.data(),out.position,3));
            CHECK(Equal(snapshot.vertices[n].normal.data(),out.normal,3));
        }
    }
    std::cout<<"P1: six seeks agree with D3DX bone composition and production PMX morph/BDEF workers\n";
    return 0;
}
