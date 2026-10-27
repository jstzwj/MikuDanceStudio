#include "emm_manager.h"
#include "material_bind.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "model_data.h"

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

const mme::EmmEffectEntry& Entry(const mme::EmmEffectSection& section, const char* key) {
    for (const auto& entry : section.entries)
        if (entry.key == key)
            return entry;
    throw std::runtime_error("missing saved object entry");
}

struct Globals {
    mikudancestudio::MMDApp* app = mikudancestudio::g_Block;
    mme::MmeContext* context = mme::g_context;
    mme::EffectOwnerManager* manager = mme::g_ownerManager;
    ~Globals() {
        mikudancestudio::g_Block = app;
        mme::g_context = context;
        mme::g_ownerManager = manager;
    }
};

void Run() {
    using namespace mikudancestudio;
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::AccessoryRecord, 2> accessories{};
    std::memset(&app->state, 0, sizeof app->state);
    wcscpy_s(model->path, L"C:\\fixture\\first.pmd");
    app->ModelSlot(0) = model.get();
    for (int i = 0; i < 2; ++i) {
        wcscpy_s(accessories[i].sourcePath, L"C:\\fixture\\multi.x");
        app->AccessorySlot(i) = &accessories[i];
    }
    mme::MmeContext context(nullptr);
    mme::SasEffect effect, nestedEffect, secondEffect;
    mme::EffectOwnerManager manager;
    Globals globals;
    g_Block = app.get();
    mme::g_context = &context;
    mme::g_ownerManager = &manager;
    const auto add = [&](void* id, const char* path, int kind) {
        const auto key = reinterpret_cast<unsigned long long>(id);
        auto* data = new mme::ModelData(nullptr, key, path, kind, 2, nullptr);
        context.models.push_back(data);
        context.modelRegistry[key] = data;
        return data;
    };
    auto* pmd = add(model.get(), "C:\\fixture\\first.pmd", 1);
    auto* carrier = add(&accessories[0], "C:\\fixture\\multi.x", 0);
    auto* other = add(&accessories[1], "C:\\fixture\\multi.x", 0);
    const std::string main = "C:\\effects\\Main\\main.fx";
    pmd->setEffectFile(main);
    other->setEffectFile(main);
    carrier->setEffectFile("C:\\effects\\ray.fx");
    carrier->setRenderClass(2);
    auto* binding = new mme::MaterialBinding;
    binding->sas = &effect;
    manager.bindings[{0, carrier, -1}] = binding;

    // These names, owner rows and visibility-presence expectations were
    // captured from MMEffect 0.37's real writer after drawing ray-mmd.
    // Only the local absolute path prefix is replaced in this CPU fixture.
    const std::array<const char*, 9> names = {"EnvLightMap", "FogMap", "LightMap",
                                              "MaterialMap", "PSSM1",  "PSSM2",
                                              "PSSM3",       "PSSM4",  "SSAOMap"};
    for (const char* name : names) {
        mme::SasResource resource;
        resource.name = name;
        resource.semanticId = 0x2E;
        if (resource.name == "MaterialMap") {
            resource.defaultEffectMap = {{"self", "hide"},
                                         {"*.pmd", "C:\\effects\\materials\\material_2.0.fx"},
                                         {"*", "hide"}};
        } else if (resource.name.compare(0, 4, "PSSM") == 0) {
            resource.defaultEffectMap = {{"self", "hide"},
                                         {"*.pmd", "C:\\effects\\shadow\\" + resource.name + ".fx"},
                                         {"*", "hide"}};
        } else if (resource.name == "SSAOMap") {
            resource.defaultEffectMap = {{"self", "hide"},
                                         {"*", "C:\\effects\\shadow\\SSAO visibility 1.0.fx"}};
        } else {
            resource.defaultEffectMap = {{"*", "hide"}};
        }
        effect.resources.push_back(std::move(resource));
    }
    const auto output = std::filesystem::absolute("mme-emm-save.emm").string();
    Check(mme::MmeEmmSave(output.c_str()), "save live mapping without D3D");
    mme::EmmFile saved;
    Check(saved.Load(output.c_str()), "parse saved live mapping");
    Check(saved.objectRecords().size() == 3 && saved.effectSections().size() == 10,
          "save all duplicate instances and nine offscreen owners");
    const auto& root = saved.effectSections().front();
    Check(root.isDefault() && root.entries.size() == 2, "scene carrier has no base path row");
    Check(Entry(root, "Pmd0").effectPath == main && !Entry(root, "Pmd0").hasShow &&
              Entry(root, "Acs1").effectPath == main && !Entry(root, "Acs1").hasShow,
          "implicit visibility must not become explicit show rows");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& section = saved.effectSections()[i + 1];
        Check(section.name == std::string("Effect@") + names[i] && section.ownerKey == "Acs0" &&
                  section.ownerSubsetIndex == -1 && section.ownerMaterialName.empty(),
              "native owner names and scope");
        const auto& self = Entry(section, "Acs0");
        Check(self.effectPath.empty() && self.hasShow && !self.shown && self.subsetPaths.empty(),
              "carrier serializes only its hidden flag");
        const bool utility = i < 3;
        const auto& modelEntry = Entry(section, "Pmd0");
        Check(modelEntry.effectPath ==
                  (utility  ? "none"
                   : i == 3 ? "C:\\effects\\materials\\material_2.0.fx"
                   : i < 8  ? "C:\\effects\\shadow\\" + std::string(names[i]) + ".fx"
                            : "C:\\effects\\shadow\\SSAO visibility 1.0.fx"),
              "native resolved offscreen model assignment");
        Check(modelEntry.hasShow == utility && (!utility || !modelEntry.shown),
              "native model visibility-presence rules");
        const auto& accessoryEntry = Entry(section, "Acs1");
        Check(accessoryEntry.hasShow == (i != 8) && (i == 8 || !accessoryEntry.shown),
              "duplicate accessory has independent scoped visibility");
    }
    other->setShown(true);
    Check(mme::MmeEmmSave(output.c_str()) && saved.Load(output.c_str()), "save explicit true");
    Check(Entry(saved.effectSections().front(), "Acs1").hasShow &&
              Entry(saved.effectSections().front(), "Acs1").shown,
          "explicit true remains distinguishable from an absent row");
    other->resetShown();
    Check(mme::MmeEmmSave(output.c_str()) && saved.Load(output.c_str()), "save reset visibility");
    Check(!Entry(saved.effectSections().front(), "Acs1").hasShow,
          "reset clears visibility row presence");

    pmd->passPlanScratch().renderOrder = 10;
    carrier->passPlanScratch().renderOrder = 20;
    other->passPlanScratch().renderOrder = 30;
    binding->offscreenTurns["MaterialMap"] = 7;
    auto* nestedBinding = new mme::MaterialBinding;
    nestedBinding->sas = &nestedEffect;
    manager.bindings[{7, pmd, 0}] = nestedBinding;
    auto* secondBinding = new mme::MaterialBinding;
    secondBinding->sas = &secondEffect;
    manager.bindings[{0, other, -1}] = secondBinding;
    mme::SasResource nested;
    nested.name = "MaterialMap";
    nested.semanticId = 0x2E;
    nestedEffect.resources.push_back(nested);
    secondEffect.resources.push_back(nested);
    Check(mme::MmeEmmSave(output.c_str()) && saved.Load(output.c_str()), "save nested owners");
    Check(saved.effectSections().size() == 12, "serialize each scoped resource once");
    const auto& nestedSection = saved.effectSections()[5];
    Check(nestedSection.name == "Effect@MaterialMap(1)" && nestedSection.ownerKey == "Pmd0" &&
              nestedSection.ownerSubsetIndex == 0 &&
              nestedSection.ownerMaterialName == "MaterialMap",
          "nested owner suffix and parent follow the native depth-first walk");
    const auto& secondSection = saved.effectSections().back();
    Check(secondSection.name == "Effect@MaterialMap(2)" && secondSection.ownerKey == "Acs1" &&
              secondSection.ownerMaterialName.empty(),
          "same-name owners are numbered by display order, independently of pointer order");

    // The native loader walks absolute display order before consuming
    // same-path Object rows. Named rows whose owner cannot be resolved do
    // not become base assignments (MMEffect x64 2E8D0 / x86 2B970).
    mme::EmmFile incoming;
    incoming.SetObjectRecords(saved.objectRecords());
    incoming.SetDefaultEffect("none");
    std::vector<mme::EmmEffectEntry> entries;
    for (const char* key : {"Pmd0", "Acs0", "Acs1"}) {
        mme::EmmEffectEntry entry;
        entry.key = key;
        entry.effectPath = "none";
        if (entry.key == "Acs0") {
            entry.hasShow = true;
            entry.shown = false;
        }
        entries.push_back(std::move(entry));
    }
    incoming.SetDefaultEntries(entries);
    mme::EmmEffectSection unresolved;
    unresolved.name = "Effect@MaterialMap";
    unresolved.baseName = "MaterialMap";
    unresolved.ownerKey = "Acs0";
    mme::EmmEffectEntry scoped;
    scoped.key = "Pmd0";
    scoped.effectPath = "none";
    scoped.hasShow = true;
    scoped.shown = false;
    unresolved.entries.push_back(scoped);
    incoming.SetOwnerSections({unresolved});
    Check(incoming.Save(output.c_str()), "write reordered incoming mapping");
    pmd->passPlanScratch().renderOrder = 2;
    carrier->passPlanScratch().renderOrder = 3;
    other->passPlanScratch().renderOrder = 1;
    Check(mme::MmeEmmLoad(output.c_str()), "apply mapping without D3D");
    Check(!other->shown() && other->shownRecorded() && carrier->shown() &&
              !carrier->shownRecorded(),
          "duplicate rows match current display order");
    Check(pmd->shown() && !pmd->shownRecorded(),
          "unresolved offscreen visibility must not overwrite base visibility");
}
} // namespace

int main() {
    try {
        Run();
        std::puts("Native EMM save field regression passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
