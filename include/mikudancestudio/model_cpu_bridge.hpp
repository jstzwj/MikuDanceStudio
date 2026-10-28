#pragma once
namespace mikudancestudio {
class MMDApp;
namespace mdl {
struct ModelRecord;
}
extern MMDApp* g_Block;
// Opt-in via MDS_RHI_CPU_GEOMETRY=1. Initialize from loader CPU records before
// PostLoadInit can deform them; never initialize by locking a native buffer.
void InitializeModelCpuGeometry(MMDApp*, mdl::ModelRecord* model, bool required = false);
// Runs the existing restore/morph/skinning workers with final solved CPU bone
// matrices. Neither publication nor readiness depends on native buffers.
bool UpdateCpuOnlyModelGeometry(MMDApp*, mdl::ModelRecord* model, float edgeDistance,
                                bool opaqueEdge);
float CpuModelEdgeDistance(mdl::ModelRecord* model, const float frameWorld[16],
                           const float camera[3], float fov);
}
