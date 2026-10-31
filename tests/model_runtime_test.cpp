#include "mikudancestudio/model_runtime.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace mikudancestudio;
using namespace runtime;
std::string reason;
#define CHECK(x) do {if(!(x)){std::cerr<<"line "<<__LINE__<<": "<<#x<<" reason="<<reason<<'\n';return 1;}}while(0)
format::PmxText Name(const char* s) {
    format::PmxText result; while(*s){result.push_back(*s++);result.push_back(0);} return result;
}
format::ModelDocument Model() {
    format::ModelDocument doc; doc.kind=format::ModelKind::Pmx;
    auto& m=doc.pmx; m.header.version=2; m.bones.resize(2);
    m.bones[0].names[0]=Name("child"); m.bones[0].fields.parent=1; m.bones[0].fields.position[0]=3;
    m.bones[1].names[0]=Name("root"); m.bones[1].fields.position[0]=2;
    m.vertices.resize(3);
    for (auto& v:m.vertices) {v.position[0]=4; v.normal[0]=1; v.bone[0]=0;}
    m.vertices[1].weightType=1; m.vertices[1].bone[1]=1; m.vertices[1].weight[0]=.25f;
    m.vertices[2].bone[0]=-1; // measured negative PMX reference contributes zero
    m.morphs.resize(1); m.morphs[0].names[0]=Name("smile"); m.morphs[0].header.type=1;
    format::PmxMorphOffsetData offset; offset.index=0; offset.values[2]=2;
    m.morphs[0].offsets.push_back(offset);
    return doc;
}
format::VmdDocument Motion() {
    format::VmdDocument result;
    for (const char* name: {"root","child"}) for(unsigned frame: {10u,0u}) {
        format::VmdBoneKey key; std::memcpy(key.name.data(),name,std::strlen(name)); key.frame=frame;
        key.rotation[3]=1;
        if(frame && name[0]=='r') {key.position[0]=1; key.rotation[2]=std::sqrt(.5f);key.rotation[3]=std::sqrt(.5f);}
        if(frame && name[0]=='c') key.position[1]=1;
        result.bones.push_back(key);
    }
    format::VmdMorphKey key; std::memcpy(key.name.data(),"smile",5);key.frame=10;key.value=.5f;
    result.morphs.push_back(key);return result;
}
bool Near(float a,float b) {return std::abs(a-b)<4e-6f;}
int main() {
    auto doc=Model(); auto motion=Motion();
    std::shared_ptr<const ModelResource> resource;
    CHECK(ModelResource::Create(doc,resource,reason));
    const auto original=resource;
    // Editing/freeing the source document cannot change the immutable resource.
    doc.pmx.vertices[0].position[0]=999;
    std::shared_ptr<const MotionClip> clip;
    CHECK(MotionClip::Create(resource,motion,clip,reason));
    ModelInstance instance(resource), second(resource);
    CHECK(instance.BindMotion(clip,reason) && instance.Seek(10,reason));
    const auto captured=instance.Snapshot();
    CHECK(Near(captured.vertices[0].position[0],2) && Near(captured.vertices[0].position[1],2) && Near(captured.vertices[0].position[2],1));
    CHECK(Near(captured.vertices[0].normal[0],0) && Near(captured.vertices[0].normal[1],1));
    CHECK(Near(captured.vertices[1].position[0],2.75f) && Near(captured.vertices[1].position[1],2));
    CHECK((captured.vertices[2].position==cpu_math::Vec3{}));
    CHECK(Near(captured.bones[0].worldPosition[0],2) && Near(captured.bones[0].worldPosition[1],1));
    CHECK(second.Seek(10,reason) && second.Snapshot().vertices[0].position[0]==4);
    CHECK(instance.Seek(0,reason) && instance.Snapshot().vertices[0].position[0]==4);
    CHECK(instance.Seek(5,reason) && Near(instance.Snapshot().morphWeights[0],.25f));
    CHECK(instance.Seek(10,reason) && instance.Snapshot().vertices[0].position==captured.vertices[0].position);
    CHECK(instance.Seek(100,reason) && instance.Snapshot().vertices[0].position==captured.vertices[0].position);
    CHECK(!instance.Seek(0xffffffff,reason) && instance.Snapshot().frame==100);
    auto invalid=Model(); invalid.pmx.bones[1].fields.parent=0;
    CHECK(!ModelResource::Create(invalid,resource,reason) && reason.find("cyclic-")==0 && resource==original);
    invalid=Model(); invalid.pmx.vertices[0].weightType=4;
    CHECK(!ModelResource::Create(invalid,resource,reason) && reason.find("unsupported-skinning")==0);
    invalid=Model(); invalid.pmx.bones[0].fields.flags=0x8000;
    CHECK(!ModelResource::Create(invalid,resource,reason) && reason.find("unsupported-bone-flags")==0);
    invalid=Model(); invalid.pmx.vertices[0].bone[0]=2;
    CHECK(!ModelResource::Create(invalid,resource,reason) && reason.find("invalid-skin-bone")==0);
    invalid=Model(); invalid.pmx.morphs[0].offsets[0].index=100;
    CHECK(!ModelResource::Create(invalid,resource,reason));
    invalid=Model(); invalid.pmx.morphs[0].header.type=8;
    CHECK(!ModelResource::Create(invalid,resource,reason));
    invalid=Model(); invalid.pmx.rigids.resize(1);
    CHECK(!ModelResource::Create(invalid,resource,reason) && reason.find("invalid-rigid")==0);
    invalid=Model(); invalid.pmx.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!ModelResource::Create(invalid,resource,reason));
    invalid=Model(); invalid.pmx.bones[0].names[0]={0x00,0xd8};
    CHECK(!ModelResource::Create(invalid,resource,reason));
    invalid=Model(); invalid.pmx.bones[0].names[0]=invalid.pmx.bones[1].names[0];
    CHECK(!ModelResource::Create(invalid,resource,reason));
    invalid=Model(); CHECK(ModelResource::Create(invalid,resource,reason));
    CHECK(instance.BindMotion({},reason));
    std::shared_ptr<const MotionClip> other;
    CHECK(MotionClip::Create(resource,motion,other,reason));
    CHECK(!instance.BindMotion(other,reason) && reason.find("motion-resource-mismatch")==0);
    CHECK(instance.BindMotion(clip,reason));
    motion.bones.push_back(motion.bones.front()); motion.bones.back().position[0]=2;
    CHECK(MotionClip::Create(original,motion,clip,reason));
    CHECK(instance.BindMotion(clip,reason) && instance.Seek(10,reason));
    CHECK(Near(instance.Snapshot().vertices[0].position[0],3)); // duplicate last wins
    motion.bones.back().name.fill(0); motion.bones.back().name[0]=char(0x81);
    CHECK(!MotionClip::Create(original,motion,other,reason) && reason.find("invalid-vmd-name-encoding")==0);
    motion=Motion(); motion.bones[0].rotation[3]=0;
    CHECK(!MotionClip::Create(original,motion,other,reason));
    // Snapshots own their values, without pointers into a native model record.
    resource.reset();clip.reset();other.reset();doc={};motion={};
    CHECK(Near(captured.vertices[0].position[2],1));
    const auto detached=[] {
        std::shared_ptr<const ModelResource> r;std::shared_ptr<const MotionClip> c;std::string why;
        if(!ModelResource::Create(Model(),r,why) || !MotionClip::Create(r,Motion(),c,why)) std::abort();
        ModelInstance temporary(r);
        if(!temporary.BindMotion(c,why) || !temporary.Seek(10,why)) std::abort();
        return temporary.Snapshot();
    }();
    CHECK(Near(detached.vertices[0].position[2],1));
    std::cout<<"P1 model hierarchy, VMD, morph/skin, lifetime, repeated seek and rejection passed\n";
    return 0;
}
