// emm_manager.h - the EMM/EMD effect-mapping manager of MMEffect.dll.
//
// Evidence:
//   - RTTI classes EmmFile (TD 0x1800D7758) / EmdFile (TD 0x1800D7738); both
//     parse the same [section]/key=value text shape as IniFile (FUN_180047BD0).
//   - Validation strings (strings_evidence.md section 7):
//       "section 'Info' was not found" 0x1800b5500, "key 'Version' was not
//       found" 0x1800b5520, "section 'Object' was not found" 0x1800b5540,
//       "section 'Effect' was not found" 0x1800b5560, "key 'Default' was not
//       found" 0x1800b5580, "key 'Owner' was not found" 0x1800b55a0,
//       "unknown object '" 0x1800b55c0, "key 'Obj' was not found" 0x1800b5600,
//       ".show" 0x1800b54f8, "MikuMikuEffect ver.0.37 cannot support this (EMM
//       )file format:\n" 0x1800b5338/0x1800b5430.
//   - The EMM loader FUN_18002E8D0 (called from the .emm open dialog
//     FUN_180042F60 with a path, and from the PMM autoload FUN_1800570D0);
//     the EMM save FUN_1800315F0 / save dialog FUN_1800431E0; the application
//     to live models FUN_180030590 / FUN_180031980 (both reference the
//     effect-owner manager DAT_1800d9a40).
//   - PMM linkage (MMEffect.txt lines 79-81): with [MMEffect][EMMAutoSave] the
//     EMM is saved next to the PMM on save (FUN_180057290: splitpath/makepath
//     "<dir><name>.emm" then the save dialog) and reloaded on load
//     (FUN_1800570D0: "<dir><name>.emm" existence check + "AutoLoading: " log).
//   - Auto-assignment on model load (MMEffect.txt lines 20-66):
//       (1) <dir>\<basename>.fx beside the model file, or
//       (2) the embedded convention "<name>[<fx>].<ext>" -> <dir>\<fx>, or
//       (3) the EMM "(default)" row.
//
// Divergences (documented, see PHASE2_IMPLEMENTATION_NOTES.md):
//   - boost::regex -> std::regex (ECMAScript grammar, the boost default).
//   - The exact byte format of the original .emm writer is not recoverable
//     from the decompile; this module implements a self-consistent reader and
//     writer over the evidenced keys (Info/Version, Object/Obj+.show,
//     Effect/Default/Owner+Obj). Round-trips with itself.
//   - The original opens a save dialog (FUN_1800431E0) in the autosave path;
//     Phase 2 has no dialog, so the autosave writes the file directly.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace mme {

class ModelData;

// One per-object assignment row.
struct EmmAssignment {
    std::string effectPath;   // "" = (none)
    bool shown;               // the ".show" suffix state
    EmmAssignment() : shown(true) {}
};

// An Owner rule: a regex over object names with the effect file it selects.
struct EmmOwnerRule {
    std::string pattern;      // source text of the regex
    std::string effectPath;
};

class EmmFile {
public:
    // Parse + validate. Raises (logs) the evidenced error strings; returns
    // false when the file could not be used.
    bool Load(const char* path);

    // Serialize the current assignment state (writer side of FUN_1800315F0).
    bool Save(const char* path) const;

    const std::string& version() const { return version_; }
    const std::vector<std::string>& objects() const { return objects_; }
    const std::map<std::string, EmmAssignment>& assignments() const { return assignments_; }
    const std::vector<EmmOwnerRule>& ownerRules() const { return ownerRules_; }
    const std::string& defaultEffect() const { return defaultEffect_; }

    // Writer-side population (MmeEmmSave serializes the live state).
    void SetAssignments(const std::map<std::string, EmmAssignment>& table)
    {
        assignments_ = table;
    }
    void SetDefaultEffect(const std::string& path) { defaultEffect_ = path; }

private:
    std::string version_;                                    // [Info] Version
    std::vector<std::string> objects_;                       // [Object] Obj rows
    std::map<std::string, EmmAssignment> assignments_;       // object -> assignment
    std::vector<EmmOwnerRule> ownerRules_;                   // [Effect] Owner rows
    std::string defaultEffect_;                              // [Effect] Default
};

// ---------------------------------------------------------------------------
// Manager state + entry points
// ---------------------------------------------------------------------------

// The live assignment table (object name -> assignment). Loaded by
// MmeEmmLoad / extended by MmeAssignEffect / MmeAutoAssignForModel.
std::map<std::string, EmmAssignment>& MmeEmmAssignments();

// The "(default)" row effect path ("" = none).
const std::string& MmeEmmDefaultEffect();
void MmeEmmSetDefaultEffect(const std::string& path);

// [FUN_18002E8D0 / FUN_180030590] load an .emm file and apply the assignments
// to the live models (matching by ModelData name).
bool MmeEmmLoad(const char* path);

// [FUN_1800315F0] save the live assignments to an .emm file.
bool MmeEmmSave(const char* path);

// [FUN_180030590] assign one effect file to (objectId, subsetIndex); pass
// subsetIndex < 0 for the whole object. path "" clears the assignment.
// Returns true when the object was found and the effect (if any) loaded.
bool MmeAssignEffect(unsigned long long objectId, int subsetIndex, const std::string& path);

// Auto-assignment for a freshly registered model (MMEffect.txt (1)): the
// same-name .fx, the "[<fx>]" embedded-name convention, then the EMM default
// row. Returns the narrow effect path ("" when none applies). Loads nothing.
std::string MmeFindEffectFileForModel(ModelData* model);

// [FUN_180057290] PMM-save hook (the SavedPMMFile seam): when EMMAutoSave is
// enabled and the PMM path is valid (not "(invalid)"), build
// "<dir>\<name>.emm" and save. The original opens the save dialog
// (FUN_1800431E0); Phase 2 writes the file directly (documented divergence).
void MmeAutoSaveEmmForPmm(const wchar_t* pmmPath);

// [FUN_1800570D0] PMM-load hook (the LoadedPMMFile seam, called from the pass
// planner): when "<dir>\<name>.emm" exists, log "AutoLoading: <emm>\n\n" and
// load it.
void MmeAutoLoadEmmForPmm(const char* pmmAnsi);

// [FUN_180057290 helper] build "<dir>\<name>.emm" from a PMM path; returns
// false when the PMM path is "(invalid)" or the path cannot be split.
bool MmeBuildEmmPathForPmm(const char* pmmAnsi, std::string* out);

} // namespace mme
