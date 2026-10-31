#include "mikudancestudio/model_runtime.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace mikudancestudio;
using namespace runtime;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
format::PmxText Name(const char* name) {
    format::PmxText out;
    for (; *name; ++name) {
        out.push_back(*name);
        out.push_back(0);
    }
    return out;
}
bool Near(float a, float b) {
    return std::abs(a - b) < 2e-5f;
}
format::ModelDocument Basic() {
    format::ModelDocument doc;
    doc.kind = format::ModelKind::Pmx;
    doc.pmx.header.version = 2;
    doc.pmx.bones.resize(1);
    doc.pmx.bones[0].names[0] = Name("root");
    doc.pmx.bones[0].fields.flags = 8;
    doc.pmx.vertices.resize(1);
    doc.pmx.vertices[0].bone[0] = 0;
    doc.pmx.vertices[0].normal[2] = 1;
    return doc;
}
bool Solve(ModelInstance& instance, const std::vector<format::PmmEvaluatedBone>& bones,
           const std::vector<float>& weights, ModelSnapshot& snapshot) {
    ModelEvaluation e;
    if (!instance.PreparePose(0, bones, weights, e, reason) ||
        !e.Solve(false, nullptr, 0, reason) || !e.Solve(true, nullptr, 0, reason) ||
        !e.Finish(reason) || !instance.Publish(std::move(e), reason))
        return false;
    snapshot = instance.Snapshot();
    return true;
}
int main() {
    auto doc = Basic();
    auto& model = doc.pmx;
    model.header.additionalUvCount = 4;
    model.materials.resize(1);
    model.materials[0].fields.diffuse[0] = .8f;
    model.materials[0].fields.diffuse[3] = .5f;
    model.morphs.resize(6);
    const unsigned types[]{1, 2, 0, 3, 4, 8};
    for (unsigned n = 0; n < 6; ++n) {
        model.morphs[n].names[0] = Name(("m" + std::to_string(n)).c_str());
        model.morphs[n].header.type = types[n];
        model.morphs[n].offsets.resize(1);
        model.morphs[n].offsets[0].index = 0;
    }
    model.morphs[0].offsets[0].values[1] = 2;
    model.morphs[1].offsets[0].values[0] = 1;
    model.morphs[1].offsets[0].values[6] = 1;
    model.morphs[2].offsets[0].values[0] = 2;
    model.morphs[3].offsets[0].values[0] = .2f;
    model.morphs[3].offsets[0].values[1] = .4f;
    for (unsigned c = 0; c < 4; ++c)
        model.morphs[4].offsets[0].values[c] = float(c + 1);
    auto& material = model.morphs[5].offsets[0];
    material.index = -1;
    material.operation = 1;
    material.values[0] = .2f;
    material.values[3] = .4f;
    std::shared_ptr<const ModelResource> resource;
    CHECK(ModelResource::Create(doc, resource, reason));
    ModelInstance instance(resource);
    std::vector<format::PmmEvaluatedBone> poses(1);
    poses[0].rotation[3] = 1;
    ModelSnapshot snapshot;
    CHECK(Solve(instance, poses, {.25f, .5f, .25f, .5f, .5f, .5f}, snapshot));
    CHECK(Near(snapshot.vertices[0].position[0], .5f) &&
          Near(snapshot.vertices[0].position[1], 1.5f));
    CHECK(Near(snapshot.vertices[0].uv[0], .1f) && Near(snapshot.vertices[0].uv[1], .2f));
    CHECK(Near(snapshot.vertices[0].additionalUv[0][3], 2));
    CHECK(Near(snapshot.materials[0].material.diffuse[0], .9f) &&
          Near(snapshot.materials[0].material.diffuse[3], .7f));
    for (unsigned set = 1; set < 4; ++set) {
        model.morphs[4].header.type = 4 + set;
        for (unsigned c = 0; c < 4; ++c)
            model.vertices[0].additionalUv[c][set] = float(10 * set + c);
        std::shared_ptr<const ModelResource> uvResource;
        CHECK(ModelResource::Create(doc, uvResource, reason));
        ModelInstance uvInstance(uvResource);
        CHECK(Solve(uvInstance, poses, {.25f, .5f, .25f, .5f, .5f, .5f}, snapshot));
        for (unsigned c = 0; c < 4; ++c)
            CHECK(Near(snapshot.vertices[0].additionalUv[set][c],
                       float(10 * set + c) + float(c + 1) * .5f));
    }
    ModelEvaluation invalid;
    CHECK(instance.Prepare(0, invalid, reason));
    CHECK(!invalid.Finish(reason));
    CHECK(!instance.Publish(std::move(invalid), reason));
    CHECK(instance.Prepare(0, invalid, reason));
    CHECK(invalid.Solve(false, nullptr, 0, reason));
    CHECK(!invalid.Solve(false, nullptr, 0, reason));
    CHECK(invalid.Solve(true, nullptr, 0, reason) && invalid.Finish(reason));
    CHECK(!invalid.Finish(reason));
    // SDEF collapses to one rigid rotation when both influences agree.
    doc = Basic();
    doc.pmx.bones.push_back(doc.pmx.bones[0]);
    doc.pmx.bones[1].names[0] = Name("second");
    auto& vertex = doc.pmx.vertices[0];
    vertex.position[0] = 2;
    vertex.position[1] = 1;
    vertex.weightType = 3;
    vertex.bone[1] = 1;
    vertex.weight[0] = .3f;
    vertex.center[0] = .5f;
    vertex.r0[1] = .7f;
    vertex.r1[1] = -.2f;
    CHECK(ModelResource::Create(doc, resource, reason));
    ModelInstance sdef(resource);
    poses.resize(2);
    for (auto& pose : poses)
        pose.rotation = {0, 0, std::sqrt(.5f), std::sqrt(.5f)};
    CHECK(Solve(sdef, poses, {}, snapshot));
    CHECK(Near(snapshot.vertices[0].position[0], -1) && Near(snapshot.vertices[0].position[1], 2));
    // Append translation keeps the source multiplication and storage order.
    doc = Basic();
    doc.pmx.bones.push_back(doc.pmx.bones[0]);
    doc.pmx.bones[1].names[0] = Name("append");
    doc.pmx.bones[1].fields.flags = 0x208;
    doc.pmx.bones[1].fields.tailIdx = 0;
    doc.pmx.bones[1].fields.inheritRatio = .5f;
    CHECK(ModelResource::Create(doc, resource, reason));
    ModelInstance append(resource);
    poses.assign(2, {});
    for (auto& pose : poses)
        pose.rotation[3] = 1;
    poses[0].translation[0] = 2;
    CHECK(Solve(append, poses, {}, snapshot));
    CHECK(Near(snapshot.bones[1].skin[12], 1));
    // VPD uses CP932 names and the same fixed-axis correction as registration.
    doc = Basic();
    doc.pmx.bones[0].fields.flags = 0x408;
    doc.pmx.bones[0].fields.axis[0] = 1;
    CHECK(ModelResource::Create(doc, resource, reason));
    format::VpdDocument vpd;
    vpd.bones.resize(1);
    vpd.bones[0].name = "root";
    vpd.bones[0].rotation[1] = std::sqrt(.5f);
    vpd.bones[0].rotation[3] = std::sqrt(.5f);
    std::shared_ptr<const MotionClip> clip;
    CHECK(MotionClip::Create(resource, vpd, clip, reason));
    ModelInstance fixed(resource);
    CHECK(fixed.BindMotion(clip, reason) && fixed.Seek(0, reason));
    CHECK(Near(fixed.Snapshot().bones[0].rotation[0], std::sqrt(.5f)) &&
          fixed.Snapshot().bones[0].rotation[1] == 0);
    // PMD base morph indices refer to the base table, not the global vertex ID.
    format::ModelDocument pmd;
    pmd.kind = format::ModelKind::Pmd;
    pmd.pmd.bones.resize(1);
    std::memcpy(pmd.pmd.bones[0].pmdName, "B", 1);
    pmd.pmd.vertices.resize(2);
    for (auto& v : pmd.pmd.vertices) {
        v.bone[0] = v.bone[1] = 0;
        v.weightPercent = 100;
    }
    pmd.pmd.morphs.resize(2);
    std::memcpy(pmd.pmd.morphs[0].header.name, "base", 4);
    std::memcpy(pmd.pmd.morphs[1].header.name, "smile", 5);
    format::PmdMorphOffsetData base;
    base.vertexIndex = 1;
    base.offset[0] = 3;
    pmd.pmd.morphs[0].offsets.push_back(base);
    format::PmdMorphOffsetData change;
    change.vertexIndex = 0;
    change.offset[1] = 2;
    pmd.pmd.morphs[1].offsets.push_back(change);
    CHECK(ModelResource::Create(pmd, resource, reason));
    ModelInstance old(resource);
    poses.assign(1, {});
    poses[0].rotation[3] = 1;
    CHECK(Solve(old, poses, {0, .5f}, snapshot));
    CHECK(Near(snapshot.vertices[1].position[0], 3) && Near(snapshot.vertices[1].position[1], 1));
    CHECK(snapshot.vertices[0].position[1] == 0);
    std::cout
        << "PMD base mapping, SDEF, append/fixed axis, VPD, group/bone/UV/material morph and staged lifecycle passed\n";
}
