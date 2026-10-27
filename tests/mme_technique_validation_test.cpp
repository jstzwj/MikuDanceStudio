// CPU-only selection/load regression. The compiled source is a cache fixture;
// no COM method, device, window, or shader compiler is used.
#include "../third_party/mmeffect/src/mmeffect/effect_engine.cpp"
#include "../third_party/mmeffect/src/mmeffect/emm_manager.cpp"
#include "sas_exec.h"

#include <cstdio>
#include <cstdint>
#include <vector>

// Capture only the modal boundary. Production assignment, cache, binding,
// selection and log/dedup code remain in use; no window is ever created.
static std::vector<std::string> messages;
static int WINAPI CaptureMessageBox(HWND, LPCSTR text, LPCSTR, UINT) {
    messages.emplace_back(text);
    return IDOK;
}
#define MessageBoxA CaptureMessageBox
#include "../third_party/mmeffect/src/mmeffect/mme_log.cpp"
#undef MessageBoxA

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

mme::SasTechnique Technique(unsigned int id, const char* name, int mode, bool valid) {
    mme::SasTechnique result;
    result.handle = reinterpret_cast<D3DXHANDLE>(static_cast<std::uintptr_t>(id));
    result.name = name;
    result.mmdPass = mode;
    result.hardwareOk = valid;
    return result;
}

void Order(mme::SasEffect& sas) {
    sas.techniqueOrder.clear();
    for (const auto& technique : sas.techniques) {
        sas.techniqueOrder.push_back(technique.handle);
    }
}

void AssignmentFailures() {
    mme::MmeContext context(nullptr);
    mme::EffectOwnerManager manager;
    auto* oldContext = mme::g_context;
    auto* oldManager = mme::g_ownerManager;
    mme::g_context = &context;
    mme::g_ownerManager = &manager;
    mme::MmeLogFlush(1);
    std::vector<std::shared_ptr<mme::LoadedEffect>> sources;
    auto seed = [&](const char* path, bool valid, int subset = -1) {
        auto source = std::make_shared<mme::LoadedEffect>();
        source->path = path;
        source->effect = reinterpret_cast<ID3DXEffect*>(static_cast<std::uintptr_t>(1));
        source->sas = new mme::SasEffect();
        source->sas->effect = source->effect;
        source->sas->techniques = {Technique(1, "Main", mme::kSasPassObject, valid)};
        if (subset >= 0) {
            source->sas->techniques[0].allSubsets = false;
            source->sas->techniques[0].subsets[subset] = 1;
        }
        Order(*source->sas);
        mme::EffectCacheRef()[path] = source;
        sources.push_back(source);
        return source;
    };
    auto add = [&](unsigned long long id, int count) {
        auto* model = new mme::ModelData(nullptr, id, "cpu.x", 0, count, nullptr);
        context.models.push_back(model);
        context.modelRegistry[id] = model;
        return model;
    };
    auto binding = [&](mme::ModelData* model, int subset = -1) {
        return mme::MmeFindMaterialBinding(0, model, subset, false);
    };
    auto emptyBinding = [&](mme::ModelData* model, int subset, const std::string& path) {
        auto* b = binding(model, subset);
        return b && !b->effect && !b->sas && !b->owner && !b->instance && b->effectPath == path;
    };

    auto shared = seed("cpu-shared.fx", false, 1);
    auto* rejected = add(101, 2);
    Check(mme::MmeAssignEffect(101, -1, shared->path), "First assignment reaches production entry");
    Check(rejected->effectFile() == shared->path && emptyBinding(rejected, -1, shared->path),
          "Failed first assignment keeps its path and an effect-free working binding");
    Check(messages.size() == 1 &&
              messages.back().find("some techniques cannot run") != std::string::npos,
          "Assignment validation failure reports through the captured modal boundary");
    Check(mme::EffectCacheRef().count(shared->path) == 1,
          "Assignment rejection must not evict a compatible compiled source");

    auto* compatible = add(102, 1);
    mme::MmeAssignEffect(102, -1, shared->path);
    auto* good = binding(compatible);
    Check(good && good->owner == shared && good->effect == shared->effect,
          "A smaller model still binds the shared source after rejection");
    mme::MmeAssignEffect(101, -1, shared->path);
    Check(binding(compatible) == good && good->owner == shared && good->effect == shared->effect &&
              mme::EffectCacheRef().at(shared->path) == shared,
          "Rejecting another model leaves the live binding and shared cache unchanged");
    Check(messages.size() == 1, "Repeated identical failures are deduplicated within a phase");

    auto validSubset = seed("cpu-valid-subset.fx", true);
    mme::MmeAssignEffect(101, 0, validSubset->path);
    Check(binding(rejected, 0) && binding(rejected, 0)->effect == validSubset->effect,
          "Material effect assignment uses a real binding");
    auto invalidSubset = seed("cpu-invalid-subset.fx", false);
    mme::MmeAssignEffect(101, 0, invalidSubset->path);
    Check(
        rejected->subsetEffects().at(0) == invalidSubset->path &&
            emptyBinding(rejected, 0, invalidSubset->path) && messages.size() == 2,
        "Failed material replacement keeps its path, releases the old effect and reports failure");
    mme::MmeAssignEffect(101, 0, "none");
    Check(rejected->subsetEffects().at(0).empty() && emptyBinding(rejected, 0, ""),
          "Explicit material none retains a binding that blocks whole-object fallback");

    auto* reloadModel = add(103, 2);
    auto initial = seed("cpu-reload.fx", true);
    initial->sas->scriptClass = mme::kSasClassSceneOrObject;
    initial->sas->scriptOrder = mme::kSasOrderPostprocess;
    initial->sas->drawsGeometry = false;
    mme::MmeAssignEffect(103, -1, initial->path);
    Check(binding(reloadModel) && binding(reloadModel)->effect == initial->effect,
          "Hot reload begins with a valid assignment");
    mme::MmeReleaseEffectBindings(initial->path);
    Check(emptyBinding(reloadModel, -1, initial->path),
          "Reload releases the old effect before loading");
    auto invalidReload = seed("cpu-reload.fx", false);
    Check(!mme::MmeRebindEffectAssignments(initial->path),
          "Invalid hot reload cannot report success");
    Check(reloadModel->effectFile() == initial->path &&
              emptyBinding(reloadModel, -1, initial->path),
          "Failed hot reload retains assignment with no stale effect");
    Check(messages.size() == 3, "Hot reload reports the same validation diagnostic");
    Check(reloadModel->renderClass() == 2 && !reloadModel->drawsGeometry(),
          "Failed reload does not overwrite native class flags before a successful load");
    auto repaired = seed("cpu-reload.fx", true);
    Check(mme::MmeRebindEffectAssignments(initial->path) && binding(reloadModel)->owner == repaired,
          "Repaired hot reload recovers from the retained assignment");
    Check(reloadModel->renderClass() == 0 && reloadModel->drawsGeometry(),
          "A repaired object effect resets previous scene-class flags");
    Check(binding(compatible) == good && good->owner == shared,
          "Reloading a different path leaves another model untouched");

    // Missing files take the native silent path, retaining an effect-free node.
    auto missing = std::make_shared<mme::LoadedEffect>();
    missing->path = "cpu-missing.fx";
    mme::EffectCacheRef()[missing->path] = missing;
    mme::MmeAssignEffect(103, -1, missing->path);
    Check(reloadModel->effectFile() == missing->path &&
              emptyBinding(reloadModel, -1, missing->path) && messages.size() == 3,
          "Missing file retains its path silently and releases the previous effect");
    mme::MmeAssignEffect(101, -1, "");
    Check(rejected->effectFile().empty() && rejected->subsetEffects().empty() &&
              !binding(rejected) && !binding(rejected, 0),
          "Whole-object removal clears subset assignments and bindings");

    for (const auto& source : sources)
        source->effect = nullptr;
    mme::EffectCacheRef().clear();
    mme::MmeLogFlush(0);
    mme::g_context = oldContext;
    mme::g_ownerManager = oldManager;
}
}

int main() {
    const bool oldSkip = mme::g_skipValidation;
    mme::g_skipValidation = false;
    auto source = std::make_shared<mme::LoadedEffect>();
    source->path = "cpu-selection-fixture.fx";
    source->effect = reinterpret_cast<ID3DXEffect*>(static_cast<std::uintptr_t>(1));
    source->sas = new mme::SasEffect();
    auto& sas = *source->sas;
    sas.effect = source->effect;
    sas.techniques = {Technique(2, "Shadowed", mme::kSasPassObjectSS, false),
                      Technique(1, "Main", mme::kSasPassObject, false)};
    Order(sas);
    const std::string expected =
        "Error: some techniques cannot run on this hardware: Main Shadowed\n";
    Check(mme::SasValidateTechniqueSelections(&sas, 25) == expected,
          "Collect each invalid handle once and report in handle order");
    Check(mme::SasValidateTechniqueSelections(&sas, 0).empty() &&
              mme::SasValidateTechniqueSelections(nullptr, 25).empty(),
          "No materials or SAS model needs no validation");

    mme::g_skipValidation = true;
    Check(mme::SasValidateTechniqueSelections(&sas, 25).empty(),
          "SkipValidation uses shader capabilities independently of D3DX validity");
    sas.techniques[0].shaderCapsOk = false;
    Check(mme::SasValidateTechniqueSelections(&sas, 1) ==
              "Error: some techniques cannot run on this hardware: Shadowed\n",
          "SkipValidation still rejects unsupported shader capabilities");
    mme::g_skipValidation = false;

    sas.techniques.push_back(Technique(3, "ValidMain", mme::kSasPassObject, true));
    sas.techniques.push_back(Technique(4, "ValidShadowed", mme::kSasPassObjectSS, true));
    Order(sas);
    Check(mme::SasValidateTechniqueSelections(&sas, 25).empty(),
          "A later valid candidate prevents invalid fallback rejection");
    sas.techniqueOrder = {sas.techniques[0].handle};
    Check(mme::SasValidateTechniqueSelections(&sas, 1) ==
              "Error: some techniques cannot run on this hardware: Shadowed\n",
          "A technique outside the STANDARDSGLOBAL order cannot rescue a selection");

    sas.techniques = {Technique(1, "First", mme::kSasPassObject, false),
                      Technique(2, "Last", mme::kSasPassObject, false)};
    Order(sas);
    Check(mme::SasValidateTechniqueSelections(&sas, 1) ==
              "Error: some techniques cannot run on this hardware: Last\n",
          "Only the last invalid fallback for a selection enters the error set");

    sas.techniques = {Technique(1, "SubsetOne", mme::kSasPassEdge, false)};
    sas.techniques[0].allSubsets = false;
    sas.techniques[0].subsets[1] = 1;
    Order(sas);
    Check(mme::SasValidateTechniqueSelections(&sas, 1).empty() &&
              !mme::SasValidateTechniqueSelections(&sas, 2).empty(),
          "Unmatched subsets pass; validation uses this model's actual material count");
    sas.scriptClass = mme::kSasClassScene;
    Check(mme::SasValidateTechniqueSelections(&sas, 25).empty(),
          "Scene class validates subset zero only");
    sas.scriptClass = mme::kSasClassSceneOrObject;
    Check(!mme::SasValidateTechniqueSelections(&sas, 2).empty(),
          "Scene-or-object class retains all subsets");
    for (int order : {mme::kSasOrderPreprocess, mme::kSasOrderPostprocess}) {
        sas.scriptOrder = order;
        sas.techniques[0].allSubsets = true;
        Check(mme::SasValidateTechniqueSelections(&sas, 25).empty(),
              "Pre/post-process excludes edge, shadow and z-plot tables");
        sas.techniques[0].mmdPass = mme::kSasPassObject;
        Check(!mme::SasValidateTechniqueSelections(&sas, 25).empty(),
              "Pre/post-process still validates its object table");
        sas.techniques[0].mmdPass = mme::kSasPassEdge;
    }
    sas.scriptOrder = mme::kSasOrderStandard;
    sas.techniques[0].allSubsets = false;
    sas.techniques[0].mmdPass = mme::kSasPassObject;
    sas.techniques[0].useTexture = 1;
    sas.techniques[0].useSpheremap = 1;
    sas.techniques[0].useToon = 1;
    Check(!mme::SasValidateTechniqueSelections(&sas, 2).empty(),
          "Object tables validate all eight material flag combinations");
    sas.techniques[0].mmdPass = mme::kSasPassShadow;
    Check(mme::SasValidateTechniqueSelections(&sas, 2).empty(),
          "Shadow tables query only the all-false material state");

    // Exercise the production loader's cache-hit path. A rejected model
    // must not poison the compiled source used by a compatible assignment.
    sas.techniques[0].mmdPass = mme::kSasPassObject;
    sas.techniques[0].useTexture = -1;
    sas.techniques[0].useSpheremap = -1;
    sas.techniques[0].useToon = -1;
    mme::EffectCacheRef()[source->path] = source;
    Check(mme::MmeEngineLoadEffectFile(nullptr, source->path) == source,
          "Metadata-only dialog probe uses zero materials like native 0x18001E587");
    Check(mme::MmeEngineLoadEffectFile(nullptr, source->path, 1) == source,
          "A compatible cache hit retains its compiled source");
    auto rejected = mme::MmeEngineLoadEffectFile(nullptr, source->path, 2);
    Check(rejected && !rejected->effect && !rejected->sas &&
              rejected->errorText ==
                  "Error: some techniques cannot run on this hardware: SubsetOne\n",
          "Loader returns the native rejection and diagnostic on a cache hit");
    Check(source->effect && source->sas && source->errorText.empty() &&
              mme::MmeEngineLoadEffectFile(nullptr, source->path, 1) == source,
          "Rejected subset range leaves other assignments intact");
    mme::g_skipValidation = true;
    sas.techniques[0].shaderCapsOk = true;
    Check(mme::MmeEngineLoadEffectFile(nullptr, source->path, 2) == source,
          "Cached effects obey the current validation policy");
    source->effect = nullptr; // The fixture never owns a real COM object.
    mme::EffectCacheRef().clear();
    mme::g_skipValidation = false;
    if (!failures)
        AssignmentFailures();
    mme::g_skipValidation = oldSkip;
    if (!failures) {
        std::puts("MME technique selection validation passed without GPU or windows");
    }
    return failures ? 1 : 0;
}
