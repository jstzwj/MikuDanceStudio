#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mikudancestudio/model.hpp"
namespace mikudancestudio { void ModelInitDefaults(unsigned char*); }
int main(){ using namespace mikudancestudio; using mdl::ModelRecord;
auto* p=(unsigned char*)calloc(1,sizeof(ModelRecord)); ModelInitDefaults(p); auto& m=*mdl::Mdl(p);
printf("materials@%zu=%p boneKeyCursors@%zu=%p boneTrackActive@%zu=%p morphKeyCursors@%zu=%p morphTrackActive@%zu=%p localTransforms@%zu scenePtr=%p\n",offsetof(ModelRecord,materials),m.materials,offsetof(ModelRecord,boneKeyCursors),m.boneKeyCursors,offsetof(ModelRecord,boneTrackActive),m.boneTrackActive,offsetof(ModelRecord,morphKeyCursors),m.morphKeyCursors,offsetof(ModelRecord,morphTrackActive),m.morphTrackActive,offsetof(ModelRecord,localTransforms),m.scenePtr);
printf("morph0Count@%zu physOffsetCount@%zu physLastFrame@%zu gap4@%zu modelDirectory@%zu pmxTextBuffers=%p,%p,%p,%p\n",offsetof(ModelRecord,morph0Count),offsetof(ModelRecord,physOffsetCount),offsetof(ModelRecord,physLastFrame),offsetof(ModelRecord,gap4),offsetof(ModelRecord,modelDirectory),m.pmxTextBuffers[0],m.pmxTextBuffers[1],m.pmxTextBuffers[2],m.pmxTextBuffers[3]);m.pmxTextEncoding=0; m.pmxAdditionalUvCount=0; m.pmxVertexIndexSize=2; m.pmxTextureIndexSize=1; m.pmxMaterialIndexSize=1; m.pmxBoneIndexSize=2; m.pmxMorphIndexSize=1; m.pmxRigidIndexSize=1; printf("pmxTextEncoding@%zu poseTraceSlot=%zu poseTraceAfterPmxHeader=%p\n",offsetof(ModelRecord,pmxTextEncoding),mdl::kPoseTraceBuffer,mdl::PoseTraceBuffer(p)); free(p);}

