#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/cpu_deform.hpp"
#include "mikudancestudio/vmd_bone_interpolation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace mikudancestudio::runtime {
namespace {
template <class Range> bool Finite(const Range& values) {
    for (float v : values) if (!std::isfinite(v)) return false;
    return true;
}
bool Fail(std::string& reason, const char* code, std::size_t index=0) {
    reason=std::string(code)+":"+std::to_string(index); return false;
}
bool Name(const format::PmxText& bytes, std::string& output) {
    if (bytes.size()%2) return false;
    std::string text;
    for (std::size_t n=0;n<bytes.size();n+=2) {
        unsigned c=bytes[n] | (unsigned(bytes[n+1])<<8);
        if (!c) return false; // embedded terminators cannot identify a track
        if (c>=0xd800 && c<=0xdbff) {
            if (n+3>=bytes.size()) return false;
            const unsigned low=bytes[n+2] | (unsigned(bytes[n+3])<<8);
            if (low<0xdc00 || low>0xdfff) return false;
            c=0x10000+((c-0xd800)<<10)+(low-0xdc00); n+=2;
        } else if (c>=0xdc00 && c<=0xdfff) return false;
        if (c<0x80) text+=char(c);
        else if (c<0x800) { text+=char(0xc0|(c>>6)); text+=char(0x80|(c&63)); }
        else if (c<0x10000) {
            text+=char(0xe0|(c>>12)); text+=char(0x80|((c>>6)&63)); text+=char(0x80|(c&63));
        } else {
            text+=char(0xf0|(c>>18)); text+=char(0x80|((c>>12)&63));
            text+=char(0x80|((c>>6)&63)); text+=char(0x80|(c&63));
        }
    }
    output=std::move(text); return !output.empty();
}
template <std::size_t N> bool MotionName(const std::array<char,N>& bytes,std::string& text) {
    text.clear();
    for (unsigned char c : bytes) {
        if (!c) break;
        // CP932 conversion is a P2 text-service boundary. Never interpret raw
        // CP932 bytes as UTF-8 or depend on the host's current code page.
        if (c>=128 || c<32) return false;
        text+=char(c);
    }
    return !text.empty();
}
template <class Key> void SortTrack(std::vector<Key>& keys) {
    std::stable_sort(keys.begin(),keys.end(),[](const Key& a,const Key& b){return a.frame<b.frame;});
    // Registrar semantics: the last input key for a frame replaces the old one.
    std::vector<Key> result;
    for (const auto& key : keys) {
        if (!result.empty() && result.back().frame==key.frame) result.back()=key;
        else result.push_back(key);
    }
    keys=std::move(result);
}
template <class Key> std::pair<const Key*,const Key*> Bracket(const std::vector<Key>& keys,unsigned frame) {
    const auto right=std::lower_bound(keys.begin(),keys.end(),frame,
                                     [](const Key& key,unsigned f){return key.frame<f;});
    if (right==keys.end()) return {&keys.back(),&keys.back()};
    if (right==keys.begin() || right->frame==frame) return {&*right,&*right};
    return {&*(right-1),&*right};
}
}

bool ModelResource::Create(const format::ModelDocument& source,
                           std::shared_ptr<const ModelResource>& output,std::string& reason) {
    if (source.kind!=format::ModelKind::Pmx) return Fail(reason,"unsupported-model-format");
    const auto& model=source.pmx;
    if (model.header.version!=2 || model.header.textEncoding!=0)
        return Fail(reason,"unsupported-pmx-version-or-encoding");
    if (model.header.additionalUvCount) return Fail(reason,"unsupported-additional-uv");
    if (!model.rigids.empty() || !model.joints.empty()) return Fail(reason,"unsupported-physics");
    auto resource=std::shared_ptr<ModelResource>(new ModelResource);
    std::map<std::string,std::size_t> names;
    std::vector<std::vector<std::size_t>> children(model.bones.size());
    for (std::size_t n=0;n<model.bones.size();++n) {
        const auto& b=model.bones[n].fields;
        if (b.flags & ~0x001f) return Fail(reason,"unsupported-bone-flags",n);
        if (!b.ikLinks.empty()) return Fail(reason,"unsupported-ik",n);
        if (!Finite(b.position)) return Fail(reason,"nonfinite-bone",n);
        if (b.parent < -1 || b.parent>=static_cast<std::int64_t>(model.bones.size()) || b.parent==static_cast<std::int64_t>(n))
            return Fail(reason,"invalid-bone-parent",n);
        if (b.parent>=0) children[b.parent].push_back(n); else resource->order_.push_back(n);
        std::string name;
        if (!Name(model.bones[n].names[0],name)) return Fail(reason,"invalid-bone-name",n);
        if (!names.emplace(name,n).second) return Fail(reason,"ambiguous-bone-name",n);
        resource->boneNames_.push_back(std::move(name));
    }
    for (std::size_t n=0;n<resource->order_.size();++n)
        for (auto child : children[resource->order_[n]]) resource->order_.push_back(child);
    if (resource->order_.size()!=model.bones.size()) return Fail(reason,"cyclic-bone-hierarchy");
    for (std::size_t n=0;n<model.vertices.size();++n) {
        const auto& v=model.vertices[n];
        if (v.weightType>2) return Fail(reason,"unsupported-skinning",n);
        if (!Finite(v.position) || !Finite(v.normal) || !Finite(v.uv)) return Fail(reason,"nonfinite-vertex",n);
        const int count=v.weightType==0?1:v.weightType==1?2:4;
        for (int b=0;b<count;++b)
            if (v.bone[b]<-1 || v.bone[b]>=static_cast<std::int64_t>(model.bones.size()))
                return Fail(reason,"invalid-skin-bone",n);
        if (v.weightType) {
            const int weights=v.weightType==1?1:4; float sum=0;
            for (int b=0;b<weights;++b) {
                if (!std::isfinite(v.weight[b]) || v.weight[b]<0 || v.weight[b]>1)
                    return Fail(reason,"invalid-skin-weight",n);
                sum+=v.weight[b];
            }
            if (weights==4 && std::abs(sum-1)>1e-4f) return Fail(reason,"invalid-skin-weight-sum",n);
        }
    }
    for (auto index : model.indices)
        if (index<0 || index>=static_cast<std::int64_t>(model.vertices.size())) return Fail(reason,"invalid-vertex-index");
    names.clear();
    for (std::size_t n=0;n<model.morphs.size();++n) {
        const auto& morph=model.morphs[n];
        if (morph.header.type!=1) return Fail(reason,"unsupported-morph",n);
        std::string name;
        if (!Name(morph.names[0],name)) return Fail(reason,"invalid-morph-name",n);
        if (!names.emplace(name,n).second) return Fail(reason,"ambiguous-morph-name",n);
        resource->morphNames_.push_back(std::move(name));
        for (const auto& offset : morph.offsets)
            if (offset.index<0 || offset.index>=static_cast<std::int64_t>(model.vertices.size()) ||
                !std::isfinite(offset.values[0]) || !std::isfinite(offset.values[1]) || !std::isfinite(offset.values[2]))
                return Fail(reason,"invalid-morph-offset",n);
    }
    resource->document_=model;
    output=std::move(resource); reason.clear(); return true;
}

bool MotionClip::Create(std::shared_ptr<const ModelResource> resource,const format::VmdDocument& source,
                        std::shared_ptr<const MotionClip>& output,std::string& reason) {
    if (!resource) return Fail(reason,"missing-model-resource");
    if (!source.cameras.empty() || !source.lights.empty() || !source.shadows.empty() || !source.ik.empty())
        return Fail(reason,"unsupported-global-or-ik-motion");
    auto clip=std::shared_ptr<MotionClip>(new MotionClip);
    clip->resource_=std::move(resource);
    std::map<std::string,std::size_t> bones,morphs;
    for (const auto& name : clip->resource_->BoneNames()) bones.emplace(name,bones.size());
    for (const auto& name : clip->resource_->MorphNames()) morphs.emplace(name,morphs.size());
    clip->bones_.resize(bones.size()); clip->morphs_.resize(morphs.size());
    for (auto& keys : clip->bones_) { format::PmmBoneKey key; key.rotation[3]=1; keys.push_back(key); }
    for (auto& keys : clip->morphs_) keys.push_back({});
    for (const auto& sourceKey : source.bones) {
        std::string name;
        if (!MotionName(sourceKey.name,name)) return Fail(reason,"unsupported-vmd-name-encoding");
        if (sourceKey.frame>std::numeric_limits<std::int32_t>::max() ||
            !Finite(sourceKey.position) || !Finite(sourceKey.rotation)) return Fail(reason,"invalid-bone-key");
        float squared=0; for(float v:sourceKey.rotation) squared+=v*v;
        if (std::abs(squared-1)>1e-3f) return Fail(reason,"unsupported-nonunit-rotation");
        if (sourceKey.interpolation[2]==0x63 && sourceKey.interpolation[3]==0x0f)
            return Fail(reason,"unsupported-physics-key");
        const auto found=bones.find(name);
        if (found==bones.end()) { clip->warnings_.push_back("unbound-bone-track:"+name); continue; }
        format::PmmBoneKey key;
        key.frame=sourceKey.frame;
        std::copy_n(sourceKey.position,3,key.position.begin()); std::copy_n(sourceKey.rotation,4,key.rotation.begin());
        key.interpolation=format::DecodeVmdBoneInterpolation(sourceKey.interpolation);
        for(auto v:key.interpolation) if(v>127) return Fail(reason,"invalid-bone-curve");
        clip->bones_[found->second].push_back(key);
    }
    for (const auto& key : source.morphs) {
        std::string name;
        if (!MotionName(key.name,name)) return Fail(reason,"unsupported-vmd-name-encoding");
        if (key.frame>std::numeric_limits<std::int32_t>::max() || !std::isfinite(key.value))
            return Fail(reason,"invalid-morph-key");
        const auto found=morphs.find(name);
        if (found==morphs.end()) { clip->warnings_.push_back("unbound-morph-track:"+name); continue; }
        clip->morphs_[found->second].push_back(key);
    }
    for (auto& keys : clip->bones_) SortTrack(keys);
    for (auto& keys : clip->morphs_) SortTrack(keys);
    output=std::move(clip); reason.clear(); return true;
}

bool ModelInstance::BindMotion(std::shared_ptr<const MotionClip> motion,std::string& reason) {
    if (motion && motion->resource_!=resource_) return Fail(reason,"motion-resource-mismatch");
    motion_=std::move(motion); snapshot_={}; reason.clear(); return true;
}
bool ModelInstance::Seek(std::uint32_t frame,std::string& reason) {
    if (!resource_) return Fail(reason,"missing-model-resource");
    if (frame>std::numeric_limits<std::int32_t>::max()) return Fail(reason,"invalid-frame");
    const auto& model=resource_->document_;
    ModelSnapshot result; result.frame=frame; result.bones.resize(model.bones.size());
    for (auto index : resource_->order_) {
        auto& pose=result.bones[index];
        if (motion_) {
            const auto [left,right]=Bracket(motion_->bones_[index],frame);
            if (left==right) { pose.translation=left->position; pose.rotation=left->rotation; }
            else {
                format::PmmEvaluatedBone evaluated;
                if (!format::InterpolatePmmBoneSegment(*left,*right,frame,evaluated)) return Fail(reason,"bone-evaluation-failed",index);
                pose.translation=evaluated.translation; pose.rotation=evaluated.rotation;
            }
        }
        const auto& bone=model.bones[index].fields;
        const cpu_math::Vec3 rest{bone.position[0],bone.position[1],bone.position[2]};
        pose.local=cpu_math::BoneLocal(rest,pose.translation,pose.rotation);
        pose.skin=bone.parent<0?pose.local:cpu_math::Multiply(pose.local,result.bones[bone.parent].skin);
        pose.worldPosition=cpu_math::TransformPoint(rest,pose.skin);
        if (!Finite(pose.local) || !Finite(pose.skin) || !Finite(pose.worldPosition)) return Fail(reason,"nonfinite-bone-output",index);
    }
    std::vector<cpu_math::Vec3> positions;
    positions.reserve(model.vertices.size());
    for (const auto& v : model.vertices) positions.push_back({v.position[0],v.position[1],v.position[2]});
    result.morphWeights.resize(model.morphs.size());
    for (std::size_t index=0;index<model.morphs.size();++index) {
        float weight=0;
        if (motion_) {
            const auto [left,right]=Bracket(motion_->morphs_[index],frame);
            weight=left==right?left->value:format::InterpolatePmmMorphValue(left->value,right->value,left->frame,right->frame,frame);
        }
        if (!std::isfinite(weight)) return Fail(reason,"nonfinite-morph-output",index);
        result.morphWeights[index]=weight;
        if (weight!=0) for (const auto& offset : model.morphs[index].offsets)
            cpu_deform::AccumulateVertexMorph(positions[offset.index].data(),offset.values,weight);
    }
    constexpr cpu_math::Matrix zero{}; // native PMX negative-index semantics
    for (std::size_t index=0;index<model.vertices.size();++index) {
        const auto& v=model.vertices[index]; VertexPose out;
        const auto matrix=[&](int b) {return v.bone[b]<0?zero.data():result.bones[v.bone[b]].skin.data();};
        if (v.weightType==0) {
            cpu_deform::TransformPosition(out.position.data(),positions[index].data(),matrix(0));
            cpu_deform::TransformNormal(out.normal.data(),v.normal,matrix(0));
        } else if (v.weightType==1) {
            for (int c=0;c<3;++c) {
                out.position[c]=cpu_deform::BlendTransformComponent(positions[index].data(),matrix(0),matrix(1),c,v.weight[0],true);
                out.normal[c]=cpu_deform::BlendTransformComponent(v.normal,matrix(0),matrix(1),c,v.weight[0],false);
            }
        } else {
            const float* matrices[]{matrix(0),matrix(1),matrix(2),matrix(3)};
            cpu_deform::Bdef4(out.position.data(),out.normal.data(),positions[index].data(),v.normal,matrices,v.weight);
        }
        std::copy_n(v.uv,2,out.uv.begin());
        if (!Finite(out.position) || !Finite(out.normal)) return Fail(reason,"nonfinite-vertex-output",index);
        result.vertices.push_back(out);
    }
    snapshot_=std::move(result); reason.clear(); return true;
}
}
