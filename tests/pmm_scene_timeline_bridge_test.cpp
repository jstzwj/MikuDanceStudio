#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace mikudancestudio;
namespace {
struct Bytes {
    std::vector<std::uint8_t> data;
    std::size_t offset = 0;
};
int Read(void* source, void* destination, std::size_t length) {
    auto& bytes = *static_cast<Bytes*>(source);
    const auto count = std::min(length, bytes.data.size() - bytes.offset);
    if (count) std::memcpy(destination, bytes.data.data() + bytes.offset,
                           count);
    bytes.offset += count;
    return static_cast<int>(count);
}
bool Write(void* context, const void* source, std::size_t length) {
    auto& bytes = *static_cast<Bytes*>(context);
    const auto* first = static_cast<const std::uint8_t*>(source);
    bytes.data.insert(bytes.data.end(), first, first + length);
    return true;
}
bool MakeTwoModelFixture(const Bytes& source, Bytes& output,
                         std::uint8_t& secondSlot) {
    Bytes input = source;
    input.offset = 0;
    format::PmmSceneDocument scene;
    if (!format::ReadPmmSceneDocument(Read, &input, nullptr, nullptr, scene) ||
        scene.models.size() != 1 || scene.models[0].slot + 1 >= kModelSlotCount ||
        scene.sections.size() < 3 || scene.sections[1].kind !=
            format::PmmSceneSectionKind::Model ||
        scene.sections.back().kind != format::PmmSceneSectionKind::Tail ||
        scene.modelRegistrations.size() != 1) return false;
    const auto section = scene.sections[1];
    secondSlot = static_cast<std::uint8_t>(scene.models[0].slot + 1);
    auto copy = std::vector<std::uint8_t>(
        source.data.begin() + section.offset,
        source.data.begin() + section.offset + section.size);
    copy[0] = secondSlot;
    output = source;
    output.offset = 0;
    output.data[54] = 2;
    output.data.insert(output.data.begin() + section.offset + section.size,
                       copy.begin(), copy.end());
    output.data.push_back(secondSlot);
    const auto selection = scene.modelRegistrations[0].selection;
    for (int shift = 0; shift < 32; shift += 8)
        output.data.push_back(static_cast<std::uint8_t>(selection >> shift));
    return true;
}
template<class T>
std::size_t Capacity(const format::PmmKeyTrack<T>& track) {
    std::size_t end = track.dense.size();
    for (const auto& key : track.sparse)
        end = std::max(end, static_cast<std::size_t>(key.index) + 1);
    return end + 1;
}
void CopyBone(const format::PmmBoneKey& source, mdl::BoneKey& target) {
    target.frame = source.frame;
    target.previous = source.previous;
    target.next = source.next;
    std::copy(source.interpolation.begin(), source.interpolation.end(),
              target.interpolation);
    std::copy(source.position.begin(), source.position.end(),
              target.position);
    std::copy(source.rotation.begin(), source.rotation.end(),
              target.rotation);
    target.allocated = source.allocated;
    target.physicsDisabled = source.physicsDisabled;
}
void CopyMorph(const format::PmmMorphKey& source, mdl::MorphKey& target) {
    target.frame = source.frame;
    target.previous = source.previous;
    target.next = source.next;
    target.value = source.value;
    target.allocated = source.allocated;
}
void CopyCamera(const format::PmmCameraKey& source, mdl::CameraKey& target) {
    target.frame = source.frame;
    target.previous = source.previous;
    target.next = source.next;
    target.distance = source.distance;
    std::copy(source.eye.begin(), source.eye.end(), target.eye);
    std::copy(source.target.begin(), source.target.end(), target.target);
    for (int row = 0; row < 4; ++row)
        std::copy(source.interpolation[row].begin(),
                  source.interpolation[row].end(), target.interpolation[row]);
    target.perspective = source.perspective;
    target.fov = source.fov;
    target.parentModel = source.parentModel;
    target.parentBone = source.parentBone;
    target.selected = source.selected;
}
}
int main() {
    const char* path = std::getenv("MDS_PMM_SCENE_SESSION_FIXTURE");
    if (!path || !*path) return 77;
    std::ifstream file(path, std::ios::binary);
    if (!file) return 1;
    Bytes input;
    input.data.assign(std::istreambuf_iterator<char>{file}, {});
    auto session = std::make_unique<format::SceneSession>();
    if (!format::SceneSession::Load(Read, &input, nullptr, nullptr, *session) ||
        session->Document().models.size() != 1) return 2;
    const auto& source = session->Document().models[0];
    if (!source.layout.boneCount ||
        Capacity(source.boneKeys) >= mdl::kBoneKeyCapacity ||
        Capacity(source.morphKeys) >= mdl::kMorphKeyCapacity ||
        Capacity(source.displayKeys) >= mdl::kDisplayKeyCapacity) return 2;
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::BoneRecord> bones(source.layout.boneCount);
    std::vector<mdl::MorphRecord> morphs(source.layout.morphCount);
    std::vector<mdl::IkChain> ik(source.layout.ikCount);
    std::vector<mdl::BoneOrderEntry> selectors(source.rigidCount);
    std::vector<mdl::BoneKey> boneKeys(Capacity(source.boneKeys));
    std::vector<mdl::MorphKey> morphKeys(Capacity(source.morphKeys));
    std::vector<mdl::DisplayKey> displays(Capacity(source.displayKeys));
    std::vector<mdl::CameraKey> cameraKeys(
        Capacity(session->Document().cameraKeys));
    std::vector<mdl::LightKey> lightKeys(Capacity(session->Document().lightKeys));
    const auto copyLight=[&](std::size_t index,const format::PmmLightKey& key){auto& target=lightKeys[index];
        target.frame=key.frame;target.previous=key.previous;target.next=key.next;target.selected=key.selected;
        std::copy(key.direction.begin(),key.direction.end(),target.direction);std::copy(key.color.begin(),key.color.end(),target.color);};
    copyLight(0,session->Document().lightKeys.dense[0]);
    for(const auto& key:session->Document().lightKeys.sparse)copyLight(key.index,key.value);
    std::vector<mdl::SelfShadowKey> shadowKeys;
    if (!session->Document().selfShadowKeys.dense.empty()) {
        shadowKeys.resize(Capacity(session->Document().selfShadowKeys));
        const auto copyShadow = [&](std::size_t index,
                const format::PmmSelfShadowKey& source) {
            auto& target = shadowKeys[index];
            target.frame = source.frame;
            target.previous = source.previous;
            target.next = source.next;
            target.mode = source.mode;
            target.distance = source.distance;
            target.selected = source.selected;
        };
        copyShadow(0, session->Document().selfShadowKeys.dense[0]);
        for (const auto& key : session->Document().selfShadowKeys.sparse)
            copyShadow(key.index, key.value);
    }
    std::vector<mdl::GravityKey> gravityKeys;
    if (!session->Document().gravityKeys.dense.empty()) {
        gravityKeys.resize(Capacity(session->Document().gravityKeys));
        const auto copyGravity = [&](std::size_t index,
                const format::PmmGravityKey& source) {
            auto& target = gravityKeys[index];
            target.frame = source.frame;
            target.previous = source.previous;
            target.next = source.next;
            target.noiseEnabled = source.noiseEnabled;
            target.noise = source.noise;
            target.acceleration = source.acceleration;
            std::copy(source.direction.begin(), source.direction.end(),
                      target.direction);
            target.selected = source.selected;
        };
        copyGravity(0, session->Document().gravityKeys.dense[0]);
        for (const auto& key : session->Document().gravityKeys.sparse)
            copyGravity(key.index, key.value);
    }
    std::vector<std::vector<std::uint8_t>> ikStates(displays.size());
    std::vector<std::vector<mdl::BoneReference>> pairs(displays.size());
    for (std::size_t i = 0; i < source.boneKeys.dense.size(); ++i)
        CopyBone(source.boneKeys.dense[i], boneKeys[i]);
    for (const auto& key : source.boneKeys.sparse)
        CopyBone(key.value, boneKeys[key.index]);
    for (std::size_t i = 0; i < source.morphKeys.dense.size(); ++i)
        CopyMorph(source.morphKeys.dense[i], morphKeys[i]);
    for (const auto& key : source.morphKeys.sparse)
        CopyMorph(key.value, morphKeys[key.index]);
    CopyCamera(session->Document().cameraKeys.dense[0], cameraKeys[0]);
    for (const auto& key : session->Document().cameraKeys.sparse)
        CopyCamera(key.value, cameraKeys[key.index]);
    const auto copyDisplay = [&](std::size_t index,
                                 const format::PmmDisplayKey& key) {
        auto& target = displays[index];
        target.frame = key.frame;
        target.previous = key.previous;
        target.next = key.next;
        target.visible = key.visible;
        target.allocated = key.allocated;
        ikStates[index] = key.ikEnabled;
        target.ikStates = ikStates[index].data();
        for (const auto& pair : key.rigidStates)
            pairs[index].push_back({pair.modelIndex, pair.boneIndex});
        target.selectorStates = pairs[index].data();
    };
    copyDisplay(0, source.displayKeys.dense[0]);
    for (const auto& key : source.displayKeys.sparse)
        copyDisplay(key.index, key.value);
    model->boneCount = source.layout.boneCount;
    model->morphCount = source.layout.morphCount;
    model->ikChainCount = source.layout.ikCount;
    model->boneOrderCount = source.rigidCount;
    model->boneTable = bones.data();
    model->morphs = morphs.data();
    model->ikChains = ik.data();
    model->boneOrderTable = selectors.data();
    model->boneKeys = boneKeys.data();
    model->morphKeys = morphKeys.data();
    model->displayKeys = displays.data();
    for (auto& bone : bones) {
        bone.type = mdl::BoneType::RotateMove;
        bone.slotIndex = -1;
    }
    auto* raw = model.get();
    app->ModelSlot(source.slot) = raw;
    app->CameraKeys() = cameraKeys.data();
    app->LightKeys() = lightKeys.data();
    if (!shadowKeys.empty()) app->ShadowKeys() = shadowKeys.data();
    if (!gravityKeys.empty()) app->GravityKeys() = gravityKeys.data();
    app->AttachPortableSceneSession(std::move(session));
    g_Block = app.get();
    auto* active = app->PortableSceneSession();
    SeekPortableSceneModel(raw, 0);
    if (active->RuntimeTracksChanged() ||
        active->Pose().models[0].bones[0].translation[0] !=
            bones[0].trans[0]) return 3;
    boneKeys[0].position[0] += 5.0f;
    SeekPortableSceneModel(raw, 0);
    if (!active->RuntimeTracksChanged() ||
        active->Pose().models[0].bones[0].translation[0] !=
            boneKeys[0].position[0] ||
        bones[0].trans[0] != boneKeys[0].position[0]) return 4;
    std::uint32_t last = 0;
    while (boneKeys[last].next) last = boneKeys[last].next;
    const auto insertedIndex = static_cast<std::uint32_t>(boneKeys.size() - 1);
    if (insertedIndex <= last || boneKeys[last].frame > 1000000) return 5;
    boneKeys[insertedIndex] = boneKeys[last];
    boneKeys[insertedIndex].frame = boneKeys[last].frame + 10;
    boneKeys[insertedIndex].previous = last;
    boneKeys[insertedIndex].next = 0;
    boneKeys[insertedIndex].position[0] = 42.0f;
    boneKeys[last].next = insertedIndex;
    SeekPortableSceneModel(raw, static_cast<int>(boneKeys[insertedIndex].frame));
    if (active->Pose().models[0].bones[0].translation[0] != 42.0f ||
        bones[0].trans[0] != 42.0f) return 6;
    boneKeys[last].next = 0;
    SeekPortableSceneModel(raw, 0);
    app->PlaybackCursorSeconds() = 0.0f;
    // Accessories round-trip ambient, which overlays LightColor on x86.
    // Starting playback must retain the unadjusted applied specular RGB.
    const std::array<float,3> canonicalLight{.0076678241603076458f,.084635414183139801f,.601f};
    std::copy(canonicalLight.begin(),canonicalLight.end(),app->LightColor());app->ApplyTimelineLightState();
    auto& adjustedLight=app->SceneLight();
    for(float* component:{&adjustedLight.Ambient.r,&adjustedLight.Ambient.g,&adjustedLight.Ambient.b}){
        volatile float value=*component;value=value-.3f;value=value+.3f;*component=value;
    }
    const std::array<float,3> adjustedAmbient{adjustedLight.Ambient.r,adjustedLight.Ambient.g,adjustedLight.Ambient.b};
    const std::array<float,3> scalarMirror{app->LightColor()[0],app->LightColor()[1],app->LightColor()[2]};
    if(adjustedAmbient[0]==canonicalLight[0])return 25;
    StartPortableScenePlayback(app.get());
    if(!active->RuntimeAppliedLight() || !std::equal(canonicalLight.begin(),canonicalLight.end(),active->RuntimeAppliedLight()->begin()) ||
        std::array<float,3>{app->LightColor()[0],app->LightColor()[1],app->LightColor()[2]}!=scalarMirror ||
        std::array<float,3>{adjustedLight.Ambient.r,adjustedLight.Ambient.g,adjustedLight.Ambient.b}!=adjustedAmbient)return 26;
    app->PlaybackCursorSeconds() = 5.0f / 30.0f;
    AdvancePortableScenePlayback(app.get());
    if (!active->Playing() ||
        std::fabs(bones[0].trans[0] -
                  active->Pose().models[0].bones[0].translation[0]) >
            0.00001f) return 7;
    cameraKeys[0].eye[0] += 4.0f;
    lightKeys[0].color[0]+=.125f;
    SeekPortableSceneCamera(app.get(), 0);
    if(active->Pose().lightState[0]!=lightKeys[0].color[0] || active->Document().lightKeys.dense[0].color[0]!=lightKeys[0].color[0])return 23;
    const auto goodNext=lightKeys[0].next;const auto held=active->Pose().lightState;
    lightKeys[0].next=mdl::kTimelineKeyCapacity;SeekPortableSceneCamera(app.get(),1);
    if(active->Pose().lightState!=held || active->Pose().frame!=0)return 24;lightKeys[0].next=goodNext;
    if (active->Pose().camera.position[0] != cameraKeys[0].eye[0] ||
        app->CameraPosition()[0] != cameraKeys[0].eye[0]) return 17;
    app->PlaybackCursorSeconds() = 0.0f;
    StartPortableScenePlayback(app.get());
    app->PlaybackCursorSeconds() = 5.0f / 30.0f;
    AdvancePortableScenePlayback(app.get());
    ApplyPortableSceneCamera(app.get());
    if (!active->Playing() ||
        std::fabs(app->CameraPosition()[0] -
                  active->Pose().camera.position[0]) > 0.00001f)
        return 18;
    if (!shadowKeys.empty()) {
        shadowKeys[0].mode = shadowKeys[0].mode == 1 ? 2 : 1;
        shadowKeys[0].distance += 0.001f;
        SeekPortableSceneSelfShadow(app.get(), 0);
        if (app->ShadowMode() != shadowKeys[0].mode ||
            app->ShadowDistance() != shadowKeys[0].distance ||
            active->Document().selfShadowKeys.dense[0].mode !=
                shadowKeys[0].mode) return 19;
        app->PlaybackCursorSeconds() = 0.0f;
        StartPortableScenePlayback(app.get());
        app->PlaybackCursorSeconds() = 5.0f / 30.0f;
        AdvancePortableScenePlayback(app.get());
        ApplyPortableSceneSelfShadow(app.get());
        if (!active->Playing() ||
            app->ShadowDistance() != active->Pose().selfShadowDistance)
            return 20;
    }
    if (!gravityKeys.empty()) {
        gravityKeys[0].acceleration += 1.0f;
        gravityKeys[0].noise += 2;
        SeekPortableSceneGravity(app.get(), 0);
        if (app->GravityMagnitude() != gravityKeys[0].acceleration ||
            app->GravityNoise() != gravityKeys[0].noise ||
            active->Document().gravityKeys.dense[0].acceleration !=
                gravityKeys[0].acceleration) return 21;
        app->PlaybackCursorSeconds() = 0.0f;
        StartPortableScenePlayback(app.get());
        app->PlaybackCursorSeconds() = 5.0f / 30.0f;
        AdvancePortableScenePlayback(app.get());
        ApplyPortableSceneGravity(app.get());
        if (!active->Playing() ||
            app->GravityMagnitude() != active->Pose().gravity.acceleration ||
            app->GravityNoise() != active->Pose().gravity.noise)
            return 22;
    }

    Bytes doubled;
    std::uint8_t secondSlot = 0;
    if (!MakeTwoModelFixture(input, doubled, secondSlot)) return 8;
    auto multi = std::make_unique<format::SceneSession>();
    if (!format::SceneSession::Load(Read, &doubled, nullptr, nullptr, *multi) ||
        multi->Document().models.size() != 2 ||
        multi->Document().models[1].slot != secondSlot) return 9;
    Bytes saved;
    if (!multi->Edit([](format::PmmSceneDocument& scene) {
            scene.models[1].boneKeys.dense[0].position[0] += 2.0f;
        }) || !multi->Save(Write, &saved)) return 10;
    format::SceneSession reloaded;
    if (!format::SceneSession::Load(Read, &saved, nullptr, nullptr, reloaded) ||
        reloaded.Document().models[1].boneKeys.dense[0].position[0] !=
            multi->Document().models[1].boneKeys.dense[0].position[0]) return 11;

    auto second = std::make_unique<mdl::ModelRecord>(*model);
    auto secondBones = bones;
    auto secondMorphs = morphs;
    auto secondIk = ik;
    auto secondSelectors = selectors;
    auto secondBoneKeys = boneKeys;
    auto secondMorphKeys = morphKeys;
    auto secondDisplays = displays;
    second->boneTable = secondBones.data();
    second->morphs = secondMorphs.data();
    second->ikChains = secondIk.data();
    second->boneOrderTable = secondSelectors.data();
    second->boneKeys = secondBoneKeys.data();
    second->morphKeys = secondMorphKeys.data();
    second->displayKeys = secondDisplays.data();
    auto* secondRaw = second.get();
    app->AttachPortableSceneSession(std::move(multi));
    app->ModelSlot(secondSlot) = secondRaw;
    active = app->PortableSceneSession();
    // Seed each model at the new session's frame. Seeking only the second
    // deliberately leaves the first untouched; its old edited pose is not
    // the baseline for this independent two-model fixture.
    SeekPortableSceneModel(raw, 0);
    SeekPortableSceneModel(secondRaw, 0);
    if (active->Pose().models.size() != 2 ||
        secondBones[0].trans[0] != active->Pose().models[1].bones[0].translation[0] ||
        bones[0].trans[0] != active->Pose().models[0].bones[0].translation[0])
        return 12;
    secondBoneKeys[0].position[0] += 3.0f;
    const float secondBeforeSeek = secondBones[0].trans[0];
    SeekPortableSceneModel(raw, 0);
    if (!active->RuntimeTracksChanged() ||
        secondBones[0].trans[0] != secondBeforeSeek ||
        bones[0].trans[0] != active->Pose().models[0].bones[0].translation[0] ||
        active->Document().models[0].boneKeys.dense[0].position[0] ==
            active->Document().models[1].boneKeys.dense[0].position[0])
        return 13;
    SeekPortableSceneModel(secondRaw, 0);
    if (secondBones[0].trans[0] != secondBoneKeys[0].position[0]) return 13;
    std::uint32_t secondLast = 0;
    while (secondBoneKeys[secondLast].next)
        secondLast = secondBoneKeys[secondLast].next;
    const auto secondInsert = static_cast<std::uint32_t>(secondBoneKeys.size() - 1);
    if (secondInsert <= secondLast) return 14;
    secondBoneKeys[secondInsert] = secondBoneKeys[secondLast];
    secondBoneKeys[secondInsert].frame =
        secondBoneKeys[secondLast].frame + 10;
    secondBoneKeys[secondInsert].previous = secondLast;
    secondBoneKeys[secondInsert].next = 0;
    secondBoneKeys[secondInsert].position[0] = 42.0f;
    secondBoneKeys[secondLast].next = secondInsert;
    SeekPortableSceneModel(raw,
                           static_cast<int>(secondBoneKeys[secondInsert].frame));
    if (secondBones[0].trans[0] == 42.0f ||
        active->Pose().models[1].bones[0].translation[0] != 42.0f ||
        active->Document().models[0].boneKeys.sparse.size() ==
            active->Document().models[1].boneKeys.sparse.size()) return 15;
    SeekPortableSceneModel(secondRaw,
                           static_cast<int>(secondBoneKeys[secondInsert].frame));
    if (secondBones[0].trans[0] != 42.0f) return 15;
    secondBoneKeys[secondLast].next = 0;
    SeekPortableSceneModel(raw, 0);
    app->PlaybackCursorSeconds() = 0.0f;
    StartPortableScenePlayback(app.get());
    app->PlaybackCursorSeconds() = 5.0f / 30.0f;
    AdvancePortableScenePlayback(app.get());
    if (!active->Playing() || active->Pose().models.size() != 2 ||
        std::fabs(bones[0].trans[0] -
                  active->Pose().models[0].bones[0].translation[0]) > 0.00001f ||
        std::fabs(secondBones[0].trans[0] -
                  active->Pose().models[1].bones[0].translation[0]) > 0.00001f)
        return 16;
    g_Block = nullptr;
    return 0;
}
