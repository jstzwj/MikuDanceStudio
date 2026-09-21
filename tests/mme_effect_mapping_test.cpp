#include "material_bind.h"
#include "model_data.h"
#include <cstdio>

int main() {
    using Rows = std::vector<std::pair<std::string, std::string>>;
    // Null device + accessory metadata avoids GPU/scene initialization while
    // exercising the production ModelData basename and pure mapping query.
    mme::ModelData ray(nullptr, 1, "C:\\effects\\ray-mmd\\ray.x", 0, 0, nullptr);
    mme::ModelData otherRay(nullptr, 2, "C:\\effects\\ray-mmd\\ray.x", 0, 0, nullptr);
    mme::ModelData controller(nullptr, 3, "C:\\effects\\ray_controller.pmx", 0, 0, nullptr);
    mme::ModelData sky(nullptr, 4, "C:\\effects\\Skybox\\Time of day\\Time of day fast.pmx", 0, 0, nullptr);
    mme::ModelData namedSkybox(nullptr, 5, "C:\\effects\\Sky Blue BOX.pmx", 0, 0, nullptr);
    mme::ModelData groundFog(nullptr, 6, "C:\\effects\\GroundFog01.PMX", 0, 0, nullptr);
    mme::ModelData light(nullptr, 7, "C:\\effects\\DirectionalLight.pmx", 0, 0, nullptr);
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const auto expect = [&](const Rows& rows, mme::ModelData* model,
                            mme::ModelData* owner, int index, const char* message) {
        const auto* actual = mme::MmeFindDefaultEffectRow(rows, model, owner);
        check(actual == (index < 0 ? nullptr : &rows[static_cast<size_t>(index)]), message);
    };

    const Rows identity = {{"", "ignored.fx"}, {"self", "hide"}, {"*", "none"}};
    expect(identity, &ray, &ray, 1, "self must match owner identity");
    expect(identity, &otherRay, &ray, 2, "self must not match another object with the same path");
    expect(identity, &ray, nullptr, 2, "self must not match without an owner");
    expect(identity, nullptr, nullptr, -1, "null model must never match self");
    expect({}, &ray, &ray, -1, "empty mapping must not match");
    expect({{"SELF", "hide"}}, &ray, &ray, -1, "self keyword must be case-sensitive");
    const Rows firstMatch = {{"*.PMX", "main_default.fx"}, {"*controller*.pmx", "hide"}};
    expect(firstMatch, &controller, &ray, 0, "first wildcard wins over a later specific row");
    const Rows preciseFirst = {{"*controller*.pmx", "hide"}, {"*.PMX", "main_default.fx"}};
    expect(preciseFirst, &controller, &ray, 0, "specific first row must win");
    expect({{"GroundFog??.pmx", "fog.fx"}}, &groundFog, &ray, 0, "question marks and case folding");
    expect({{"GroundFog?.pmx", "fog.fx"}}, &groundFog, &ray, -1, "question mark consumes exactly one character");
    expect({{"**sky***box*.*", "sky.fx"}}, &namedSkybox, &ray, 0, "consecutive stars and internal basename spans");
    expect({{"C:\\EFFECTS\\RAY_CONTROLLER.PMX", "legacy.fx"}}, &controller, &ray, 0,
           "existing full-path exact compatibility must remain");
    mme::ModelData literalStar(nullptr, 8, "a*b.pmx", 0, 0, nullptr);
    expect({{"a\\*b.pmx", "literal.fx"}}, &literalStar, &ray, 0, "backslash escapes wildcard");
    expect({{"a\\", "invalid.fx"}}, &literalStar, &ray, -1, "trailing escape must not match");

    // Representative rows copied in declaration order from Ray's
    // Shader/textures.fxsub. The three normal startup objects match the
    // trailing hide row in FogMap/LightMap/EnvLightMap, even though the sky
    // filename lives inside a directory named Skybox.
    const Rows fog = {
        {"GroundFog*.*", "./Fog/GroundFog/ground_fog.fx"},
        {"AtmosphericFog*.*", "./Fog/AtmosphericFog/atmospheric_fog.fx"},
        {"VolumetricCube.pmx", "./Fog/VolumetricCube/volumetric_cube.fx"},
        {"VolumetricSphere.pmx", "./Fog/VolumetricSphere/volumetric_sphere.fx"},
        {"*", "hide"}};
    const Rows lights = {{"DirectionalLight.pmx", "./Lighting/DirectionalLight/Default/directional_lighting.fx"},
        {"PointLight.pmx", "./Lighting/PointLight/Default/point_lighting.fx"}, {"*", "hide"}};
    const Rows environment = {{"sky*box*.*", "./Skybox/skylighting_none.fx"}, {"*", "hide"}};
    for (const Rows* rows : {&fog, &lights, &environment}) {
        for (mme::ModelData* model : {&ray, &controller, &sky}) {
            const auto* row = mme::MmeFindDefaultEffectRow(*rows, model, &ray);
            check(row == &rows->back(), "Ray startup objects must reach the hide fallback in utility maps");
            check(!mme::MmeDefaultEffectRowShown(row), "Ray utility-map checkbox must be unchecked");
        }
    }
    expect(fog, &groundFog, &ray, 0, "Ray GroundFog default assignment");
    expect(lights, &light, &ray, 0, "Ray directional light default assignment");
    expect(environment, &namedSkybox, &ray, 0, "Ray basename skybox default assignment");

    const Rows material = {{"self", "hide"}, {"*fog.pmx", "hide"},
        {"*controller*.pmx", "hide"}, {"*editor*.pmx", "hide"},
        {"Volumetric*.pmx", "hide"}, {"sky*box*.*", "./materials/material_skybox.fx"},
        {"LED*.pmx", "./materials/Video/material_screen_led.fx"},
        {"*Light*.pmx", "./materials/Emissive/material_lighting.fx"},
        {"*.pmd", "./materials/material_2.0.fx"}, {"*.pmx", "./materials/material_2.0.fx"},
        {"*.x", "hide"}, {"*", "hide"}};
    expect(material, &ray, &ray, 0, "MaterialMap owner must be hidden by self");
    expect(material, &controller, &ray, 2, "MaterialMap controller must be hidden");
    expect(material, &sky, &ray, 9, "Time of day sky uses the generic PMX material row");
    check(!mme::MmeDefaultEffectRowShown(mme::MmeFindDefaultEffectRow(material, &ray, &ray)),
          "MaterialMap ray checkbox must be unchecked");
    check(!mme::MmeDefaultEffectRowShown(mme::MmeFindDefaultEffectRow(material, &controller, &ray)),
          "MaterialMap controller checkbox must be unchecked");
    check(mme::MmeDefaultEffectRowShown(mme::MmeFindDefaultEffectRow(material, &sky, &ray)),
          "MaterialMap sky checkbox must remain checked");
    check(mme::MmeDefaultEffectRowShown(nullptr), "Missing rule retains the mapping UI's shown state");
    for (const char* value : {"none", "main_default", "main_default.fx", "./main/main.fx", ""}) {
        const Rows rows = {{"*", value}};
        const auto* row = mme::MmeFindDefaultEffectRow(rows, &sky, &ray);
        check(row == &rows[0] && row->second == value, "Query must preserve the assignment value verbatim");
        check(mme::MmeDefaultEffectRowShown(row), "Non-hide assignments must remain shown");
    }

    Rows editable = {{controller.filename(), "none"}, {"*.pmx", "hide"}, {"*", "hide"}};
    const Rows original = editable;
    mme::MmeSetDefaultEffectOverride(editable, &sky, "main_default.fx");
    check(editable.size() == original.size() + 1 && editable.front().first == sky.filename(),
          "Setting an override must prepend the object's full path");
    expect(editable, &sky, &ray, 0, "Explicit override must beat an earlier default wildcard");
    check(mme::MmeDefaultEffectRowShown(mme::MmeFindDefaultEffectRow(editable, &sky, &ray)),
          "An effect assignment overrides the wildcard hide checkbox");
    check(mme::MmeFindDefaultEffectRow(editable, &controller, &ray)->second == "none",
          "Setting an override must not change a sibling object's override");
    check(Rows(editable.begin() + 1, editable.end()) == original,
          "Setting an override must retain all default and sibling rows in order");
    // Exercise a caller passing the current assignment by reference.
    mme::MmeSetDefaultEffectOverride(editable, &sky, editable.front().second);
    check(editable.size() == original.size() + 1 && editable.front().second == "main_default.fx",
          "Replacing an override must neither duplicate it nor invalidate its value");
    std::string upperSkyPath = sky.filename();
    for (char& c : upperSkyPath) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    editable.push_back({upperSkyPath, "duplicate.fx"});
    mme::MmeRemoveDefaultEffectOverride(editable, &sky);
    check(editable == original, "Removal must erase all exact path variants but retain defaults and siblings");
    check(!mme::MmeDefaultEffectRowShown(mme::MmeFindDefaultEffectRow(editable, &sky, &ray)),
          "Removing the override must restore the wildcard hide checkbox");
    mme::MmeSetDefaultEffectOverride(editable, nullptr, "none");
    mme::MmeRemoveDefaultEffectOverride(editable, nullptr);
    check(editable == original, "Null-model override edits must be no-ops");
    Rows basenameDefault = {{"Time of day fast.pmx", "original.fx"}, {"*", "hide"}};
    const Rows originalBasename = basenameDefault;
    mme::MmeSetDefaultEffectOverride(basenameDefault, &sky, "none");
    mme::MmeRemoveDefaultEffectOverride(basenameDefault, &sky);
    check(basenameDefault == originalBasename,
          "Removing full-path overrides must not erase an exact basename default row");
    if (!failures) std::puts("MME effect mapping regression passed");
    return failures ? 1 : 0;
}
