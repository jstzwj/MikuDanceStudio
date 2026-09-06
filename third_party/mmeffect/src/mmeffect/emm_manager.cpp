// emm_manager.cpp - see emm_manager.h
#include "emm_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <regex>
#include <unordered_map>

#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_util.h"
#include "model_data.h"

namespace mme {

namespace {

// --- IniFile-shaped line scanner that preserves duplicate keys -------------
// (the original reads EMM files through the same parser family as IniFile,
// FUN_180047BD0: comment strip "[;#].*$", trim "^\s+|\s+$", section
// "^\[([^\[\]]+)\]$", pair "^([^=\s]+)\s*=\s*(.*)$" - the four byte-verified
// patterns; see ini_file.h). Sections keep a vector of ordered key rows so the
// repeated Obj keys of the Object/Effect sections survive.
struct EmmKeyRow {
    std::string key;
    std::string value;
};
struct EmmSection {
    std::string name;
    std::vector<EmmKeyRow> rows;
};

void Trim(std::string* s)
{
    // regex "^\s+|\s+$" replacement
    size_t begin = 0;
    size_t end = s->size();
    while (begin < end && isspace(static_cast<unsigned char>((*s)[begin]))) {
        ++begin;
    }
    while (end > begin && isspace(static_cast<unsigned char>((*s)[end - 1]))) {
        --end;
    }
    *s = s->substr(begin, end - begin);
}

std::vector<EmmSection> ParseEmmLines(const char* path)
{
    std::vector<EmmSection> sections;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rt") != 0 || file == nullptr) {
        return sections;
    }
    char line[0x400];
    while (fgets(line, sizeof(line), file) != nullptr) {
        std::string text(line);
        // comment strip "[;#].*$"
        size_t cut = text.find_first_of(";#");
        if (cut != std::string::npos) {
            text.erase(cut);
        }
        Trim(&text);
        if (text.empty()) {
            continue;
        }
        if (text.size() >= 2 && text[0] == '[' && text[text.size() - 1] == ']') {
            EmmSection section;
            section.name = text.substr(1, text.size() - 2);
            Trim(&section.name);
            sections.push_back(section);
            continue;
        }
        // pair "^([^=\s]+)\s*=\s*(.*)$"
        size_t eq = text.find('=');
        if (eq == std::string::npos || sections.empty()) {
            continue;
        }
        EmmKeyRow row;
        row.key = text.substr(0, eq);
        Trim(&row.key);
        row.value = text.substr(eq + 1);
        Trim(&row.value);
        sections.back().rows.push_back(row);
    }
    fclose(file);
    return sections;
}

const EmmSection* FindSection(const std::vector<EmmSection>& sections, const char* name)
{
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].name == name) {
            return &sections[i];
        }
    }
    return nullptr;
}

// ".show" suffix handling (0x1800b54f8): the suffix records the shown state of
// an [Object] row; strip it and report the flag.
bool SplitShowSuffix(std::string* value)
{
    const char kShow[] = ".show";
    const size_t kShowLen = 5;
    if (value->size() >= kShowLen &&
        value->compare(value->size() - kShowLen, kShowLen, kShow) == 0) {
        value->erase(value->size() - kShowLen);
        return true;
    }
    return false;
}

// Manager state ------------------------------------------------------------
std::map<std::string, EmmAssignment>& AssignmentsRef()
{
    static std::map<std::string, EmmAssignment> table;
    return table;
}
std::vector<EmmOwnerRule>& OwnerRulesRef()
{
    static std::vector<EmmOwnerRule> rules;
    return rules;
}
std::string& DefaultEffectRef()
{
    static std::string path;
    return path;
}

ModelData* FindModelByName(const std::string& objectName)
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        if (ctx->models[i] != nullptr && ctx->models[i]->name() == objectName) {
            return ctx->models[i];
        }
    }
    return nullptr;
}

// Apply one assignment to a live model: record it in ModelData and load the
// effect through the engine (the load logs "Loading effect file: ...").
void ApplyEffectToModel(ModelData* model, const std::string& effectPath)
{
    if (model == nullptr) {
        return;
    }
    model->setEffectFile(effectPath);
    if (effectPath.empty()) {
        return;
    }
    MmeContext* ctx = g_context;
    IDirect3DDevice9* device = ctx != nullptr ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, effectPath);
    if (loaded != nullptr && loaded->effect == nullptr && !loaded->errorText.empty()) {
        // [0x18000b880] the failed-load path: log the error text, then the
        // localized "Failed to load effect file:" + path + "\n\n" + errors.
        MmeLogWrite(loaded->errorText.c_str(), 0);
        std::string message = "\xCE\xDE\xB7\xA8\xBC\xD3\xD4\xD8\xCC\xD8\xD0\xA7\xCE\xC4\xBC\xFE\x3A";
        message += effectPath;
        message += "\n\n";
        message += loaded->errorText;
        MmeLogWrite(message.c_str(), 1);   // dedup + MessageBoxA (FUN_180009080 flag)
        MmeEngineUnloadEffectFile(effectPath);
        model->setEffectFile(std::string());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// EmmFile
// ---------------------------------------------------------------------------

bool EmmFile::Load(const char* path)
{
    version_.clear();
    objects_.clear();
    assignments_.clear();
    ownerRules_.clear();
    defaultEffect_.clear();

    if (path == nullptr || MmeEngineQueryFileStamp(path) == 0) {
        MmeLogWrite((std::string("Error: failed to open file ") +
                     (path != nullptr ? path : "") + "\n").c_str(), 0);
        return false;
    }

    std::vector<EmmSection> sections = ParseEmmLines(path);

    // [0x1800b5500] "section 'Info' was not found" / [0x1800b5520] "key
    // 'Version' was not found"
    const EmmSection* info = FindSection(sections, "Info");
    if (info == nullptr) {
        MmeLogWrite("section 'Info' was not found\n", 0);
        return false;
    }
    const EmmKeyRow* versionRow = nullptr;
    for (size_t i = 0; i < info->rows.size(); ++i) {
        if (info->rows[i].key == "Version") {
            versionRow = &info->rows[i];
            break;
        }
    }
    if (versionRow == nullptr) {
        MmeLogWrite("key 'Version' was not found\n", 0);
        return false;
    }
    version_ = versionRow->value;
    if (version_ != "0.37") {
        // [0x1800b5430] "MikuMikuEffect ver.0.37 cannot support this EMM file
        // format:\n<path>\n"
        MmeLogWrite((std::string("MikuMikuEffect ver.0.37 cannot support this EMM "
                                 "file format:\n") + path + "\n").c_str(), 0);
        return false;
    }

    // [0x1800b5540] "section 'Object' was not found"
    const EmmSection* objectSection = FindSection(sections, "Object");
    if (objectSection == nullptr) {
        MmeLogWrite("section 'Object' was not found\n", 0);
        return false;
    }
    // [0x1800b5600] "key 'Obj' was not found"
    if (objectSection->rows.empty()) {
        MmeLogWrite("key 'Obj' was not found\n", 0);
        return false;
    }
    for (size_t i = 0; i < objectSection->rows.size(); ++i) {
        if (objectSection->rows[i].key != "Obj") {
            continue;
        }
        std::string name = objectSection->rows[i].value;
        bool shown = SplitShowSuffix(&name);
        objects_.push_back(name);
        EmmAssignment assignment;
        assignment.shown = shown;
        assignment.effectPath = "";   // filled by the [Effect] rows below
        assignments_[name] = assignment;
    }

    // [0x1800b5560] "section 'Effect' was not found"
    const EmmSection* effectSection = FindSection(sections, "Effect");
    if (effectSection == nullptr) {
        MmeLogWrite("section 'Effect' was not found\n", 0);
        return false;
    }
    // Walk the Effect rows: "Default" = the default row; "Owner" starts a rule
    // whose effect path selects the following "Obj" rows (until the next
    // Owner/Default row).
    const EmmKeyRow* currentOwner = nullptr;
    bool sawOwner = false;
    bool sawDefault = false;
    for (size_t i = 0; i < effectSection->rows.size(); ++i) {
        const EmmKeyRow& row = effectSection->rows[i];
        if (row.key == "Default") {
            sawDefault = true;
            defaultEffect_ = row.value;
            currentOwner = nullptr;
            continue;
        }
        if (row.key == "Owner") {
            sawOwner = true;
            currentOwner = &row;
            EmmOwnerRule rule;
            rule.pattern = row.value;
            rule.effectPath = row.value;
            ownerRules_.push_back(rule);
            continue;
        }
        if (row.key == "Obj" && currentOwner != nullptr) {
            std::string name = row.value;
            SplitShowSuffix(&name);
            // [0x1800b55c0] "unknown object '" + name + "'"
            bool known = false;
            for (size_t k = 0; k < objects_.size(); ++k) {
                if (objects_[k] == name) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                MmeLogWrite((std::string("unknown object '") + name + "'\n").c_str(), 0);
                continue;
            }
            std::map<std::string, EmmAssignment>::iterator it = assignments_.find(name);
            if (it != assignments_.end()) {
                it->second.effectPath = currentOwner->value;
            }
        }
    }
    if (!sawDefault) {
        MmeLogWrite("key 'Default' was not found\n", 0);   // [0x1800b5580]
        return false;
    }
    if (!sawOwner) {
        MmeLogWrite("key 'Owner' was not found\n", 0);     // [0x1800b55a0]
        return false;
    }
    return true;
}

bool EmmFile::Save(const char* path) const
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wt") != 0 || file == nullptr) {
        return false;
    }
    fprintf(file, "[Info]\nVersion=0.37\n");
    fprintf(file, "[Object]\n");
    // Objects known to the EMM (registered models when saving live state).
    for (std::map<std::string, EmmAssignment>::const_iterator it = assignments_.begin();
         it != assignments_.end(); ++it) {
        // ".show" suffix marks the shown state (see SplitShowSuffix).
        fprintf(file, "Obj=%s%s\n", it->first.c_str(), it->second.shown ? ".show" : "");
    }
    fprintf(file, "[Effect]\n");
    fprintf(file, "Default=%s\n", defaultEffect_.c_str());
    // One Owner row per distinct effect path, with the objects assigned to it.
    {
        std::vector<std::string> paths;
        for (std::map<std::string, EmmAssignment>::const_iterator it = assignments_.begin();
             it != assignments_.end(); ++it) {
            if (it->second.effectPath.empty()) {
                continue;
            }
            if (std::find(paths.begin(), paths.end(), it->second.effectPath) == paths.end()) {
                paths.push_back(it->second.effectPath);
            }
        }
        for (size_t p = 0; p < paths.size(); ++p) {
            fprintf(file, "Owner=%s\n", paths[p].c_str());
            for (std::map<std::string, EmmAssignment>::const_iterator it = assignments_.begin();
                 it != assignments_.end(); ++it) {
                if (it->second.effectPath == paths[p]) {
                    fprintf(file, "Obj=%s\n", it->first.c_str());
                }
            }
        }
    }
    fclose(file);
    return true;
}

// ---------------------------------------------------------------------------
// Manager entry points
// ---------------------------------------------------------------------------

std::map<std::string, EmmAssignment>& MmeEmmAssignments()
{
    return AssignmentsRef();
}

const std::string& MmeEmmDefaultEffect()
{
    return DefaultEffectRef();
}

void MmeEmmSetDefaultEffect(const std::string& path)
{
    DefaultEffectRef() = path;
}

bool MmeEmmLoad(const char* path)
{
    EmmFile emm;
    if (!emm.Load(path)) {
        return false;
    }
    AssignmentsRef() = emm.assignments();
    OwnerRulesRef() = emm.ownerRules();
    DefaultEffectRef() = emm.defaultEffect();

    // [FUN_180030590] apply to the live models (by ModelData name match).
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        for (size_t i = 0; i < ctx->models.size(); ++i) {
            ModelData* model = ctx->models[i];
            if (model == nullptr) {
                continue;
            }
            std::map<std::string, EmmAssignment>::const_iterator it =
                AssignmentsRef().find(model->name());
            if (it != AssignmentsRef().end()) {
                model->setShown(it->second.shown);
                ApplyEffectToModel(model, it->second.effectPath);
            }
        }
    }
    return true;
}

bool MmeEmmSave(const char* path)
{
    // [FUN_1800315F0] serialize the live model assignments.
    EmmFile emm;
    std::map<std::string, EmmAssignment> liveAssignments;
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        for (size_t i = 0; i < ctx->models.size(); ++i) {
            ModelData* model = ctx->models[i];
            if (model == nullptr) {
                continue;
            }
            EmmAssignment assignment;
            assignment.effectPath = model->effectFile();
            assignment.shown = model->shown();
            liveAssignments[model->name()] = assignment;
        }
    }
    emm.SetAssignments(liveAssignments);
    emm.SetDefaultEffect(DefaultEffectRef());
    return emm.Save(path);
}

bool MmeAssignEffect(unsigned long long objectId, int subsetIndex, const std::string& path)
{
    // [FUN_180030590] per-object / per-subset assignment.
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return false;
    }
    std::unordered_map<unsigned long long, ModelData*>::const_iterator it =
        ctx->modelRegistry.find(objectId);
    if (it == ctx->modelRegistry.end() || it->second == nullptr) {
        return false;
    }
    ModelData* model = it->second;
    (void)subsetIndex;   // per-subset bindings live in the effect-owner manager
                         // (Phase 3 subset expansion); Phase 2 assigns the
                         // whole object like the non-expanded dialog path.

    EmmAssignment assignment;
    assignment.effectPath = path;
    assignment.shown = model->shown();
    AssignmentsRef()[model->name()] = assignment;
    ApplyEffectToModel(model, path);
    return true;
}

std::string MmeFindEffectFileForModel(ModelData* model)
{
    // MMEffect.txt lines 20-38: (1) same-name .fx, (2) "[<fx>]" embedded name,
    // (3) the EMM "(default)" row. No logging here - the "AutoLoading: " log
    // belongs to the EMM autoload (MmeAutoLoadEmmForPmm, FUN_1800570D0).
    if (model == nullptr || model->filename() == nullptr) {
        return std::string();
    }
    const std::string filename = model->filename();

    char drive[3] = { 0 };
    char dir[0x100] = { 0 };
    char base[0x100] = { 0 };
    char ext[0x40] = { 0 };
    if (_splitpath_s(filename.c_str(), drive, sizeof(drive), dir, sizeof(dir),
                     base, sizeof(base), ext, sizeof(ext)) != 0) {
        return std::string();
    }

    // (1) <dir><base>.fx beside the model file.
    char candidate[0x104];
    if (_makepath_s(candidate, sizeof(candidate), drive, dir, base, ".fx") == 0 &&
        _access_s(candidate, 4) == 0) {
        return std::string(candidate);
    }

    // (2) the embedded convention: "<name>[<fx>].<ext>" -> <dir><fx>.
    const std::string baseName(base);
    size_t open = baseName.find('[');
    if (open != std::string::npos) {
        size_t close = baseName.find(']', open);
        if (close != std::string::npos && close > open + 1) {
            std::string embedded = baseName.substr(open + 1, close - open - 1);
            if (_makepath_s(candidate, sizeof(candidate), drive, dir,
                            embedded.c_str(), nullptr) == 0 &&
                _access_s(candidate, 4) == 0) {
                return std::string(candidate);
            }
        }
    }

    // (3) EMM Owner rules: the first rule whose regex matches the object name
    // selects its effect file. The original uses boost::regex (ECMAScript
    // grammar); std::regex::ECMAScript is the documented replacement.
    {
        std::vector<EmmOwnerRule>& rules = OwnerRulesRef();
        for (size_t r = 0; r < rules.size(); ++r) {
            try {
                std::regex re(rules[r].pattern, std::regex::ECMAScript);
                if (std::regex_search(baseName, re) ||
                    std::regex_search(std::string(base) + ext, re)) {
                    if (_access_s(rules[r].effectPath.c_str(), 4) == 0) {
                        return rules[r].effectPath;
                    }
                }
            } catch (const std::regex_error&) {
                // An invalid Owner pattern is skipped (the original reports a
                // regex_error which the EMM loader surfaces as a load failure;
                // per-row skipping keeps the remaining rules usable).
            }
        }
    }

    // (4) the EMM default row (only when the file exists).
    const std::string& fallback = DefaultEffectRef();
    if (!fallback.empty() && fallback != "(none)" && _access_s(fallback.c_str(), 4) == 0) {
        return fallback;
    }
    return std::string();
}

void MmeAutoSaveEmmForPmm(const wchar_t* pmmPath)
{
    // [0x180057290] FUN_180057290: gate on g_emmAutoSave (DAT_1800d72e0),
    // narrow-convert the PMM path (FUN_180063340 = CP 0), skip "(invalid)",
    // build "<dir>\<name>.emm", then the save dialog FUN_1800431E0.
    if (g_emmAutoSave == 0 || pmmPath == nullptr) {
        return;
    }
    std::string pmmAnsi = MmeWideToAnsi(pmmPath);
    if (pmmAnsi == "(invalid)") {
        return;   // the "(invalid)" marker: nothing to save [big-C 70427-70435]
    }
    std::string emmPath;
    if (!MmeBuildEmmPathForPmm(pmmAnsi.c_str(), &emmPath)) {
        return;
    }
    // Divergence: the original opens the EMM save dialog here (FUN_1800431E0)
    // so the user can pick the target path. Phase 2 has no dialog; the save
    // goes straight to the sibling .emm path (the dialog's default).
    MmeEmmSave(emmPath.c_str());
}

void MmeAutoLoadEmmForPmm(const char* pmmAnsi)
{
    // [0x1800570D0] FUN_1800570D0: splitpath/makepath "<dir>\<name>.emm";
    // when it exists: log "AutoLoading: <emm>\n\n" and load via FUN_180042F60.
    if (pmmAnsi == nullptr) {
        return;
    }
    std::string emmPath;
    if (!MmeBuildEmmPathForPmm(pmmAnsi, &emmPath)) {
        return;
    }
    if (_access_s(emmPath.c_str(), 4) != 0) {
        return;
    }
    MmeLogWrite(("AutoLoading: " + emmPath + "\n\n").c_str(), 0);
    MmeEmmLoad(emmPath.c_str());
}

bool MmeBuildEmmPathForPmm(const char* pmmAnsi, std::string* out)
{
    if (pmmAnsi == nullptr || out == nullptr) {
        return false;
    }
    char drive[3] = { 0 };
    char dir[0x100] = { 0 };
    char base[0x100] = { 0 };
    if (_splitpath_s(pmmAnsi, drive, sizeof(drive), dir, sizeof(dir),
                     base, sizeof(base), nullptr, 0) != 0) {
        return false;
    }
    char emmPath[0x104];
    if (_makepath_s(emmPath, sizeof(emmPath), drive, dir, base, ".emm") != 0) {
        return false;
    }
    *out = emmPath;
    return true;
}

} // namespace mme
