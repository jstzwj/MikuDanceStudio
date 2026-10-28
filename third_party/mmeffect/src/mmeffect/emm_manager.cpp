// emm_manager.cpp - see emm_manager.h for the byte-verified format contract.
#include "emm_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "effect_engine.h"
#include "material_bind.h"   // eager binding build / drop
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_util.h"
#include "model_data.h"
#include "sas_exec.h"

#include "../include/MMDExport.h"  // ExpGetPmd*/ExpGetAcs* host enumeration

namespace mme {

namespace {

// --- Generic IniFile line scanner (FUN_180047BD0; see ini_file.cpp) --------
const unsigned char kUtf8Bom[3] = { 0xEF, 0xBB, 0xBF };   // [0x1800B52E8]

const char kErrOpen[]   = "Error: failed to open file ";
const char kErrLoad[]   = "Error: failed to load file: ";
const char kErrFileFmt[] =
    "MikuMikuEffect ver.0.37 cannot support this file format:\n";
const char kErrEmmFmt[] =
    "MikuMikuEffect ver.0.37 cannot support this EMM file format:\n";
const char kErrFailedMapping[] =
    "Failed to load effect mapping for the following objects:\n";  // [0x1800B4D30]
const char kErrMissingFx[] =
    "The following effect files do not exist:\n";                  // [0x1800B4DA0]
const char kNone[] = "none";                                       // [0x1800B3AB8]

std::string TrimCopy(const std::string& s)
{
    // regex "^\s+|\s+$" [0x1800B52A8]
    size_t b = 0;
    size_t e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string StripComment(const std::string& s)
{
    // regex "[;#].*$" [0x1800B52A0]
    size_t cut = s.find_first_of(";#");
    if (cut == std::string::npos) {
        return s;
    }
    return s.substr(0, cut);
}

// regex "^\[([^\[\]]+)\]$" [0x1800B52B8]
bool MatchSectionHeader(const std::string& text, std::string* name)
{
    if (text.size() < 2 || text[0] != '[' || text[text.size() - 1] != ']') {
        return false;
    }
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        if (text[i] == '[' || text[i] == ']') {
            return false;
        }
    }
    *name = text.substr(1, text.size() - 2);
    return true;
}

// regex "^([^=\s]+)\s*=\s*(.*)$" [0x1800B52D0]
bool MatchKeyValuePair(const std::string& text, std::string* key, std::string* value)
{
    size_t eq = text.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    std::string k = TrimCopy(text.substr(0, eq));
    if (k.empty() || k.find_first_of(" \t") != std::string::npos) {
        return false;
    }
    *key = k;
    *value = TrimCopy(text.substr(eq + 1));
    return true;
}

// --- boost::lexical_cast<int> (sub_180051450): optional sign, >=1 digit,
// full consume, no overflow. Anything else "throws" (= returns false).
bool LexicalCastInt(const std::string& s, int* out)
{
    if (s.empty()) {
        return false;
    }
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-' || s[0] == '+') {
        negative = (s[0] == '-');
        i = 1;
    }
    if (i >= s.size()) {
        return false;
    }
    long long value = 0;
    for (; i < s.size(); ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
        value = value * 10 + (s[i] - '0');
        if (value > 0x7FFFFFFFLL) {
            return false;
        }
    }
    if (negative) {
        value = -value;
    }
    *out = static_cast<int>(value);
    return true;
}

// --- Path helpers -----------------------------------------------------------

// FUN_180067870: normalize to an absolute path (relative paths resolve
// against the current directory, like GetFullPathName).
std::string AbsPathCopy(const std::string& path)
{
    if (path.empty()) {
        return path;
    }
    char full[_MAX_PATH];
    if (_fullpath(full, path.c_str(), sizeof(full)) != nullptr) {
        return std::string(full);
    }
    return path;
}

// FUN_18002E6F0: splitpath filename+extension ("" - actually "(null)" - when
// the split fails).
std::string FileNameOf(const std::string& path)
{
    char fname[0x100] = { 0 };
    char ext[0x40] = { 0 };
    if (_splitpath_s(path.c_str(), nullptr, 0, nullptr, 0,
                     fname, sizeof(fname), ext, sizeof(ext)) != 0) {
        return "(null)";
    }
    return std::string(fname) + ext;
}

std::string JoinPath(const std::string& emmDir, const std::string& path)
{
    // The "Path" global join (FUN_18002A630): the EMM's directory + a
    // possibly relative record path. Absolute/rooted paths pass through.
    if (path.size() >= 2 && isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return path;
    }
    if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        return path;
    }
    if (emmDir.empty()) {
        return path;
    }
    std::string result = emmDir;
    if (result[result.size() - 1] != '/' && result[result.size() - 1] != '\\') {
        result += '\\';
    }
    result += path;
    return result;
}

// Case-insensitive "shorter string equals the tail of the longer" (the
// FUN_18002E8D0 suffix fallback handles one-absolute-one-relative pairs).
bool TailIEqual(const std::string& a, const std::string& b)
{
    size_t n = a.size() < b.size() ? a.size() : b.size();
    if (n == 0 || a.size() == b.size()) {
        return _stricmp(a.c_str(), b.c_str()) == 0;
    }
    const std::string& longer = a.size() > b.size() ? a : b;
    return _stricmp(longer.c_str() + (longer.size() - n), n == a.size() ? a.c_str() : b.c_str()) == 0;
}

bool ObjectPathMatches(const std::string& recordPath, const std::string& objectPath,
                       const std::string& emmDir)
{
    if (_stricmp(recordPath.c_str(), objectPath.c_str()) == 0) {
        return true;
    }
    const std::string absRecord = AbsPathCopy(recordPath);
    const std::string absJoined = AbsPathCopy(JoinPath(emmDir, recordPath));
    const std::string absObject = AbsPathCopy(objectPath);
    if (_stricmp(absRecord.c_str(), absObject.c_str()) == 0) {
        return true;
    }
    if (_stricmp(absJoined.c_str(), absObject.c_str()) == 0) {
        return true;
    }
    return TailIEqual(absRecord, absObject) || TailIEqual(absJoined, absObject);
}

std::string KeyName(bool isModel, int slot)
{
    char buf[32];
    sprintf_s(buf, sizeof(buf), "%s%d", isModel ? "Pmd" : "Acs", slot);
    return std::string(buf);
}

// --- Manager state ------------------------------------------------------------
std::string& DefaultEffectRef()
{
    static std::string path;
    return path;
}

// Resolve a host enumeration index through ExpGetPmdID(i)/ExpGetAcsID(i).
// EMM Pmd<N>/Acs<N> keys instead use the object's absolute draw order.
ModelData* FindModelBySlot(bool isModel, int slot)
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr || slot < 0) {
        return nullptr;
    }
    unsigned long long id = 0;
    bool found = false;
    if (isModel) {
        int num = ExpGetPmdNum();
        if (slot < num) {
            id = reinterpret_cast<unsigned long long>(ExpGetPmdID(slot));
            found = true;
        }
    } else {
        int num = ExpGetAcsNum();
        if (slot < num) {
            id = reinterpret_cast<unsigned long long>(ExpGetAcsID(slot));
            found = true;
        }
    }
    if (!found) {
        return nullptr;
    }
    std::unordered_map<unsigned long long, ModelData*>::const_iterator it =
        ctx->modelRegistry.find(id);
    if (it == ctx->modelRegistry.end() || it->second == nullptr) {
        return nullptr;
    }
    return it->second;
}

void ReportEffectLoadFailure(const std::string& effectPath,
                             const std::shared_ptr<LoadedEffect>& loaded);

// Apply one assignment to a live model: record it in ModelData and load the
// effect through the engine (the load logs "Loading effect file: ...").
void ApplyEffectToModel(ModelData* model, const std::string& effectPath)
{
    if (model == nullptr) {
        return;
    }
    model->setEffectFile(effectPath);
    if (effectPath.empty()) {
        // Un-assign: drop the binding and the SAS class flags (the original
        // rebuilds the owner map at assignment time, FUN_18002ca80 - a stale
        // renderClass would keep a carrier in the pass plan forever).
        MmeDropMaterialBindings(model);
        return;
    }
    // Native 0x18002CC4C..0x18002CC61 releases the previous binding before
    // loading its replacement. 0x18002D0F6..0x18002D10F returns on failure
    // without erasing the working-map path.
    MmeEnsureMaterialBinding(0, model, -1, nullptr, effectPath, nullptr);
    MmeContext* ctx = g_context;
    IDirect3DDevice9* device = ctx != nullptr ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, effectPath, model->materialCount());
    if (loaded != nullptr && loaded->effect == nullptr && !loaded->errorText.empty()) {
        ReportEffectLoadFailure(effectPath, loaded);
        return;
    }
    // Eager binding build (the original's FUN_18002ca80 runs at ASSIGNMENT
    // time, not first draw): a scene-class effect writes its scriptClass/
    // scriptOrder into the ModelData now, so a carrier that never draws as a
    // plain object (hidden helper accessories like ray.x after a PMM load)
    // still enters passPlanA/B and drives the repeat.
    if (loaded != nullptr && loaded->effect != nullptr) {
        MmeResolveModelEffectBinding(model);
        // [0x18000bc10-0x18000bc53] "done.\n\n" (0x1800B3AE0) closes a fresh
        // load/reload of this file: v11 is set only when this round actually
        // ran the loader, so a stamp-unchanged (cache-hit) apply stays silent.
        if (!loaded->doneLogged) {
            loaded->doneLogged = true;
            MmeLogWrite("done.\n\n", 0);
        }
    }
}

// An explicit [n]=none selects the standard shader for that material. It is
// distinct from an absent row, which inherits the whole-object assignment.
void ApplySubsetEffectToModel(ModelData* model, int subsetIndex, const std::string& rawPath)
{
    const std::string effectPath = rawPath == kNone ? std::string() : rawPath;
    model->setSubsetEffect(subsetIndex, effectPath);
    MmeEnsureMaterialBinding(0, model, subsetIndex, nullptr, effectPath, nullptr);
    if (!effectPath.empty()) {
        auto loaded = MmeEngineLoadEffectFile(g_context ? g_context->device : nullptr,
                                              effectPath, model->materialCount());
        if (!loaded || !loaded->effect) {
            if (loaded && !loaded->errorText.empty()) ReportEffectLoadFailure(effectPath, loaded);
            return;
        }
    }
    MmeResolveSubsetEffectBinding(model, subsetIndex, effectPath);
}

// --- EmmFile parse state (the EmmFile virtuals, file-local) -----------------

struct ParsedEmm {
    int version = 0;
    bool versionParsed = false;
    bool abortLogging = false;              // a1+8 = 0 after the version gate
    std::vector<std::string> sectionNames;  // the OnSection registry
    std::vector<EmmObjectRecord> objects;
    std::vector<EmmEffectSection> sections;
    int currentSection = -1;                // *(this+0xA8): last Effect record
    // The this+0x60 registry of FUN_180049EB0: current section name -> the
    // keys already seen in it (outer map sub_18004E8E0, inner count
    // sub_18004F200 / insert sub_18004F0B0).
    std::map<std::string, std::set<std::string>> seenKeys;
};

EmmObjectRecord* FindObjectRecord(ParsedEmm* p, bool isModel, int slot)
{
    for (size_t i = 0; i < p->objects.size(); ++i) {
        if (p->objects[i].isModel == isModel && p->objects[i].slot == slot) {
            return &p->objects[i];
        }
    }
    EmmObjectRecord rec;
    rec.isModel = isModel;
    rec.slot = slot;
    p->objects.push_back(rec);
    return &p->objects.back();
}

EmmEffectEntry* FindEffectEntry(EmmEffectSection* section, const std::string& key)
{
    for (size_t i = 0; i < section->entries.size(); ++i) {
        if (section->entries[i].key == key) {
            return &section->entries[i];
        }
    }
    EmmEffectEntry entry;
    entry.key = key;
    section->entries.push_back(entry);
    return &section->entries.back();
}

bool HasSectionName(const ParsedEmm& p, const char* name)
{
    for (size_t i = 0; i < p.sectionNames.size(); ++i) {
        if (p.sectionNames[i] == name) {
            return true;
        }
    }
    return false;
}

// OnSection (FUN_180049930): duplicate names are rejected; "Info"/"Object"
// register without a record; "Effect" creates the default record;
// ^Effect@((\w+)(\(\d+\))?)$ creates a named record with the bare base name.
bool HandleSection(ParsedEmm* p, const std::string& name)
{
    if (HasSectionName(*p, name.c_str())) {
        return false;   // FUN_18004EA90: already registered
    }
    p->sectionNames.push_back(name);
    if (name == "Info" || name == "Object") {
        return true;
    }
    if (name == "Effect") {
        EmmEffectSection section;
        section.name = name;
        // baseName stays "" (FUN_180049930 assigns the empty string)
        p->sections.push_back(section);
        p->currentSection = static_cast<int>(p->sections.size()) - 1;
        return true;
    }
    static const std::regex namedRe("^Effect@((\\w+)(\\(\\d+\\))?)$",
                                    std::regex::ECMAScript);
    std::cmatch m;
    if (std::regex_match(name.c_str(), m, namedRe)) {
        EmmEffectSection section;
        section.name = name;
        section.baseName = m[2].str();   // what(2): base name without "(n)"
        p->sections.push_back(section);
        p->currentSection = static_cast<int>(p->sections.size()) - 1;
        return true;
    }
    return false;
}

// OnKeyValue (FUN_180049EB0). Returns false for rows the original rejects
// (the driver then logs the "Error: failed to load file: ..." line message).
// `isEmd` is the isEmd flag at this+0x5C (0 = EmmFile, 1 = EmdFile): the
// assignment-row branch accepts the key iff isEmd == (g1 == "Obj")
// [0x18004B282] - the EmmFile takes Pmd/Acs keys only, the EmdFile takes
// "Obj" only.
bool HandleKeyValue(ParsedEmm* p, const std::string& sectionName,
                    const std::string& key, const std::string& value,
                    const std::string& filePath, bool isEmd)
{
    // [0x180049eed-0x180049f2e] the this+0x60 registry runs for EVERY row,
    // before any section dispatch: the key is registered under the current
    // section name (find-or-create), and a second row with the same key in
    // the same section is rejected outright (@0x180049f04) - first wins, the
    // later value never overwrites anything. The registration precedes the
    // empty-value gate, so a rejected row still occupies its key.
    std::set<std::string>& seen = p->seenKeys[sectionName];
    if (seen.count(key) != 0) {
        return false;   // same key twice in one section
    }
    seen.insert(key);
    // [0x180049f33-0x180049f6f] an empty value rejects the row as well (the
    // key registered above stays; later rows with this key are duplicates).
    if (value.empty()) {
        return false;
    }
    if (sectionName == "Info") {
        // Only "Version" is consumed [0x18004A01D gate].
        if (key != "Version") {
            return false;
        }
        int v = 0;
        if (!LexicalCastInt(value, &v)) {
            return false;   // bad_lexical_cast: row skipped
        }
        p->version = v;
        p->versionParsed = true;
        if (v > 0 && v <= 3) {
            return true;
        }
        // [0x1800B5430] + the file path, MessageBox; then a1+8 = 0 (stop the
        // unknown-line log). The load itself is NOT aborted here - the
        // end-of-load validator still requires version != 0.
        MmeLogWrite((std::string(kErrEmmFmt) + filePath).c_str(), 1);
        p->abortLogging = true;
        return false;
    }
    if (sectionName == "Object") {
        // [0x1800B5470] ^((Pmd|Acs)(\d+))$ - the bare form only.
        static const std::regex objectKeyRe("^((Pmd|Acs)(\\d+))$", std::regex::ECMAScript);
        std::cmatch m;
        if (!std::regex_match(key.c_str(), m, objectKeyRe)) {
            return false;
        }
        int slot = 0;
        if (!LexicalCastInt(m[3].str(), &slot)) {
            return false;
        }
        EmmObjectRecord* rec = FindObjectRecord(p, m[2].str() == "Pmd", slot);
        rec->objectPath = value;   // duplicates never reach here (first wins)
        return true;
    }
    if (sectionName.size() >= 6 && memcmp(sectionName.data(), "Effect", 6) == 0) {
        if (p->currentSection < 0) {
            return false;   // rows before any [Effect*] header
        }
        EmmEffectSection* section = &p->sections[p->currentSection];
        if (key == "Default") {
            // Only in the bare section: the record's base-name field is ""
            // [FUN_180049930 @0x18004ACC8 checks rec-0xF0 == ""].
            if (!section->isDefault()) {
                return false;
            }
            section->defaultPath = value;
            return true;
        }
        if (key == "Owner") {
            // [0x1800B5498] value regex; groups 1/4/6.
            static const std::regex ownerValueRe(
                "^((Pmd|Acs)\\d+)(\\[(\\d+)\\])?(@(\\w+(\\(\\d+\\))?))?$",
                std::regex::ECMAScript);
            std::cmatch m;
            if (!std::regex_match(value.c_str(), m, ownerValueRe)) {
                return false;
            }
            int subset = -1;
            if (m[4].matched && !LexicalCastInt(m[4].str(), &subset)) {
                return false;
            }
            section->ownerKey = m[1].str();
            section->ownerSubsetIndex = subset;
            section->ownerMaterialName = m[6].matched ? m[6].str() : "";
            return true;
        }
        // [0x1800B54C8] assignment rows. The key gate at 0x18004B282 accepts
        // the row iff isEmd == (g1 == "Obj"): the EmmFile rejects "Obj" keys,
        // the EmdFile accepts them and rejects "(Pmd|Acs)<N>" instead (that
        // spelling belongs to the EmdFile callback FUN_18004D5A0's callers).
        static const std::regex assignKeyRe(
            "^((Pmd|Acs)\\d+|Obj)(\\[(\\d+)\\])?(\\.show)?$",
            std::regex::ECMAScript);
        std::cmatch m;
        if (!std::regex_match(key.c_str(), m, assignKeyRe)) {
            return false;
        }
        if ((m[1].str() == "Obj") != isEmd) {
            return false;
        }
        int subset = -1;
        if (m[4].matched && !LexicalCastInt(m[4].str(), &subset)) {
            return false;
        }
        EmmEffectEntry* entry = FindEffectEntry(section, m[1].str());
        if (m[5].matched) {
            // ".show" rows: the value must be exactly true/false
            // [0x1800B5200/0x1800B5208]; the flag lands on the whole entry
            // or the [n] subset (FUN_1800343F0 resolver).
            bool shown = false;
            if (value == "true") {
                shown = true;
            } else if (value == "false") {
                shown = false;
            } else {
                return false;
            }
            if (subset < 0) {
                entry->hasShow = true;
                entry->shown = shown;
            } else {
                entry->subsetShows[subset] = shown;
            }
        } else if (subset < 0) {
            entry->effectPath = value;
        } else {
            entry->subsetPaths[subset] = value;
        }
        return true;
    }
    return false;
}

bool ObjectKeyExists(const ParsedEmm& p, const std::string& key)
{
    // "Pmd<N>"/"Acs<N>" spelling of the (type, slot) records.
    for (size_t i = 0; i < p.objects.size(); ++i) {
        if (KeyName(p.objects[i].isModel, p.objects[i].slot) == key) {
            return true;
        }
    }
    return false;
}

// [FUN_18002A9E0] resolve one EMD row value: the literals "none", "hide" and
// "main_default" map to the empty path (unassigned); anything else joins the
// EMD's directory (absolute/rooted paths pass through).
std::string ResolveEmdValue(const std::string& raw, const std::string& emdDir)
{
    if (raw == "none" || raw == "hide" || raw == "main_default") {
        return std::string();
    }
    return JoinPath(emdDir, raw);
}

// --- per-subset show state ("[n].show" rows) ---------------------------------
//
// [sub_18002DA00 0x18002da00] the original keeps the show flag on the
// working map's (owner=0, object, subset) node: +1 = hasShow, +0x30 = the
// value; the WHOLE-object flag lives at key -1 (the port mirrors that one as
// ModelData::shown()). ModelData has no per-subset show field, so the port
// keeps the n >= 0 records in this manager-side registry, keyed by the host
// object id (the same ExpGetPmdID/ExpGetAcsID value the model registry uses).
// Presence in the inner map == hasShow; the value == the true/false row.
std::map<unsigned long long, std::map<int, bool>>& SubsetShowRegistry()
{
    static std::map<unsigned long long, std::map<int, bool>> registry;
    return registry;
}

struct EmmSaveObject
{
    int slot;
    EmmObjectRecord record;
    EmmEffectEntry entry;
    ModelData *model;
};

void ApplyOwnerSections(const std::vector<EmmEffectSection> &sections,
                        const std::map<std::string, ModelData *> &byKey)
{
    struct OwnerTarget
    {
        ModelData *model;
        SasResource *resource;
        unsigned turn;
    };
    std::map<std::string, OwnerTarget> owners;
    std::set<const EmmEffectSection *> applied;
    bool progressed;
    do
    {
        progressed = false;
        for (const auto &section : sections)
        {
            if (section.isDefault())
                continue;
            if (applied.count(&section))
                continue;
            const auto owner = byKey.find(section.ownerKey);
            if (owner == byKey.end() || !owner->second)
                continue;
            MaterialBinding *binding = nullptr;
            if (section.ownerMaterialName.empty())
            {
                binding = MmeFindMaterialBinding(0, owner->second, section.ownerSubsetIndex, true);
            }
            else
            {
                const auto parent = owners.find(section.ownerMaterialName);
                if (parent == owners.end())
                    continue;
                binding = MmeResolveTurnEffectBinding(parent->second.turn, *parent->second.resource,
                                                      parent->second.model, owner->second,
                                                      section.ownerSubsetIndex);
            }
            if (!binding || !binding->sas)
                continue;
            for (auto &resource : binding->sas->resources)
            {
                if (resource.semanticId != 0x2E || resource.name != section.baseName)
                    continue;
                unsigned &turn = binding->offscreenTurns[resource.name];
                if (!turn)
                    turn = ++g_ownerManager->nextTurnId;
                g_ownerManager->turnOwners[turn] = owner->second;
                owners[section.name.substr(7)] = {owner->second, &resource, turn};
                applied.insert(&section);
                progressed = true;
                for (const auto &entry : section.entries)
                {
                    const auto object = byKey.find(entry.key);
                    if (object == byKey.end() || !object->second)
                        continue;
                    const auto id = object->second->objectId();
                    if (!entry.effectPath.empty())
                        resource.effectOverrides[{id, -1}] = entry.effectPath;
                    if (entry.hasShow)
                        resource.shownOverrides[{id, -1}] = entry.shown;
                    for (const auto &row : entry.subsetPaths)
                        if (row.first >= 0 && row.first < object->second->materialCount())
                            resource.effectOverrides[{id, row.first}] = row.second;
                    for (const auto &row : entry.subsetShows)
                        if (row.first >= 0 && row.first < object->second->materialCount())
                            resource.shownOverrides[{id, row.first}] = row.second;
                }
            }
        }
    } while (progressed);
}

std::vector<EmmEffectSection> CollectOwnerSections(const std::vector<EmmSaveObject> &rows)
{
    // RVA 30590 walks assignment-owned offscreen resources recursively;
    // their paths and visibility are scoped to the declaring assignment.
    std::vector<EmmEffectSection> ownerSections;
    std::map<std::string, unsigned> nameCounts;
    if (!g_ownerManager)
        return ownerSections;
    std::vector<const EmmSaveObject *> displayRows;
    for (const auto &row : rows)
        if (row.model)
            displayRows.push_back(&row);
    std::stable_sort(displayRows.begin(), displayRows.end(), [](const EmmSaveObject *a, const EmmSaveObject *b) {
        return a->model->passPlanScratch().renderOrder < b->model->passPlanScratch().renderOrder;
    });
    // Section suffixes follow the native depth-first owner walk, rather
    // than pointer order in the binding lookup table (RVA 30590 tail).
    std::set<unsigned> visitedTurns;
    const auto collect = [&](const auto &self, unsigned turnId, const std::string &parent) -> void {
        if (!visitedTurns.insert(turnId).second)
            return;
        for (const auto *owner : displayRows)
        {
            for (int subset = -1; subset < owner->model->materialCount(); ++subset)
            {
                const auto assignment = g_ownerManager->bindings.find({turnId, owner->model, subset});
                if (assignment == g_ownerManager->bindings.end())
                    continue;
                auto *binding = assignment->second;
                if (!binding || !binding->sas)
                    continue;
                std::map<std::string, SasResource *> resources;
                for (auto &resource : binding->sas->resources)
                    if (resource.semanticId == 0x2E)
                        resources[resource.name] = &resource;
                for (const auto &named : resources)
                {
                    auto &resource = *named.second;
                    EmmEffectSection section;
                    section.baseName = resource.name;
                    const auto count = nameCounts[resource.name]++;
                    const auto label = resource.name + (count ? "(" + std::to_string(count) + ")" : "");
                    section.name = "Effect@" + label;
                    section.ownerKey = owner->entry.key;
                    section.ownerSubsetIndex = subset;
                    section.ownerMaterialName = parent;
                    const auto turn = binding->offscreenTurns.find(resource.name);
                    for (const auto &row : rows)
                    {
                        if (!row.model)
                            continue;
                        EmmEffectEntry entry;
                        entry.key = row.entry.key;
                        const auto id = row.model->objectId();
                        const auto *value = MmeOffscreenEffectValue(resource, row.model, owner->model);
                        const auto *declared =
                            MmeFindDefaultEffectRow(resource.defaultEffectMap, row.model, owner->model);
                        const auto shown = resource.shownOverrides.find({id, -1});
                        entry.hasShow =
                            shown != resource.shownOverrides.end() || (declared && declared->second == "hide");
                        entry.shown = MmeOffscreenObjectShown(resource, row.model, owner->model);
                        if (row.model->renderClass() == 0)
                        {
                            entry.effectPath = !value || *value == "none" || *value == "hide" ? kNone
                                               : *value == "main_default"                     ? row.entry.effectPath
                                                                                              : AbsPathCopy(*value);
                            for (int n = 0; n < row.model->materialCount(); ++n)
                            {
                                const auto path = resource.effectOverrides.find({id, n});
                                if (path != resource.effectOverrides.end())
                                    entry.subsetPaths[n] = path->second;
                                const auto show = resource.shownOverrides.find({id, n});
                                if (show != resource.shownOverrides.end())
                                    entry.subsetShows[n] = show->second;
                            }
                        }
                        section.entries.push_back(std::move(entry));
                    }
                    ownerSections.push_back(std::move(section));
                    if (turn != binding->offscreenTurns.end() && turn->second)
                        self(self, turn->second, label);
                }
            }
        }
    };
    collect(collect, 0, std::string());
    return ownerSections;
}

} // namespace

// ---------------------------------------------------------------------------
// EmmFile
// ---------------------------------------------------------------------------

bool EmmFile::Load(const char* path)
{
    version_ = 0;
    path_.clear();
    objectRecords_.clear();
    effectSections_.clear();

    if (path == nullptr) {
        return false;
    }
    path_ = path;

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rt") != 0 || file == nullptr) {
        MmeLogWrite((std::string(kErrOpen) + path).c_str(), 1);
        return false;
    }

    ParsedEmm parsed;
    char line[0x400];
    bool firstLine = true;
    std::string currentSection;
    int lineNo = 0;

    while (fgets(line, sizeof(line), file) != nullptr) {
        ++lineNo;   // the driver counts every physical line (FUN_180047BD0)
        if (firstLine) {
            firstLine = false;
            const unsigned char* b = reinterpret_cast<const unsigned char*>(line);
            if (b[0] == kUtf8Bom[0] && b[1] == kUtf8Bom[1] && b[2] == kUtf8Bom[2]) {
                MmeLogWrite((std::string(kErrLoad) + path).c_str(), 0);
                MmeLogWrite((std::string(kErrFileFmt) + path).c_str(), 1);
                fclose(file);
                return false;
            }
        }
        std::string text = TrimCopy(StripComment(line));
        if (text.empty()) {
            continue;
        }
        // Rejected sections/rows log "Error: failed to load file: <path>
        // (line N):\n  '<raw line>'" (FUN_180047BD0's reject site: the
        // 0x1800B5280 prefix, then the RAW fgets buffer as-is - newline
        // included - wrapped in quotes).
        const std::string rejectLog =
            std::string(kErrLoad) + path + " (line " + std::to_string(lineNo) +
            "):\n  '" + std::string(line) + "'";
        std::string name;
        std::string key;
        std::string value;
        if (MatchSectionHeader(text, &name)) {
            // On success the driver's current record becomes this section;
            // on rejection (duplicate/unknown name) it stays the previous
            // one and the header itself is logged like a bad row.
            if (HandleSection(&parsed, name)) {
                currentSection = name;
            } else if (!parsed.abortLogging) {
                MmeLogWrite(rejectLog.c_str(), 1);
            }
            continue;
        }
        if (MatchKeyValuePair(text, &key, &value)) {
            if (!HandleKeyValue(&parsed, currentSection, key, value, path_,
                                false) &&
                !parsed.abortLogging) {
                MmeLogWrite(rejectLog.c_str(), 1);
            }
            continue;
        }
        if (!parsed.abortLogging) {
            MmeLogWrite(rejectLog.c_str(), 1);
        }
    }
    fclose(file);

    // FUN_18004B6E0: the end-of-load validator.
    {
        std::string error;
        if (!HasSectionName(parsed, "Info")) {
            error = "section 'Info' was not found";
        } else if (!parsed.versionParsed || parsed.version == 0) {
            error = "key 'Version' was not found";
        } else if (!HasSectionName(parsed, "Object")) {
            error = "section 'Object' was not found";
        } else if (!HasSectionName(parsed, "Effect")) {
            error = "section 'Effect' was not found";
        } else {
            for (size_t i = 0; i < parsed.sections.size() && error.empty(); ++i) {
                const EmmEffectSection& section = parsed.sections[i];
                if (section.isDefault()) {
                    if (section.defaultPath.empty()) {
                        error = "key 'Default' was not found";
                        break;
                    }
                } else {
                    if (section.ownerKey.empty()) {
                        error = "key 'Owner' was not found";
                        break;
                    }
                    if (!ObjectKeyExists(parsed, section.ownerKey)) {
                        error = "unknown object '" + section.ownerKey + "'";
                        break;
                    }
                }
                for (size_t e = 0; e < section.entries.size(); ++e) {
                    if (!ObjectKeyExists(parsed, section.entries[e].key)) {
                        error = "unknown object '" + section.entries[e].key + "'";
                        break;
                    }
                }
            }
        }
        if (!error.empty()) {
            MmeLogWrite((error + "\n").c_str(), 1);
            // [FUN_180047BD0 @EOF] after a failed validator (vtable+0x20)
            // the driver appends its own summary, "Error: failed to load
            // file: <path> (line <total physical lines>)", flag 1 - skipped
            // once the version gate cleared the log flag (a1+8).
            if (!parsed.abortLogging) {
                MmeLogWrite((std::string(kErrLoad) + path + " (line " +
                             std::to_string(lineNo) + ")").c_str(), 1);
            }
            return false;
        }
    }

    version_ = parsed.version;
    objectRecords_ = parsed.objects;
    effectSections_ = parsed.sections;
    return true;
}

bool EmmFile::Save(const char* path) const
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wt") != 0 || file == nullptr) {
        MmeLogWrite((std::string(kErrOpen) + path).c_str(), 1);
        return false;
    }
    // FUN_1800490A0: "[%s]\n" / "%s = %s\n" / one blank line per section.
    fprintf(file, "[Info]\n");
    fprintf(file, "Version = 3\n");   // hardcoded [FUN_18004C160 @0x18004C17B]
    fprintf(file, "\n");

    fprintf(file, "[Object]\n");
    for (size_t i = 0; i < objectRecords_.size(); ++i) {
        const EmmObjectRecord& rec = objectRecords_[i];
        fprintf(file, "%s%d = %s\n", rec.isModel ? "Pmd" : "Acs", rec.slot,
                rec.objectPath.c_str());
    }
    fprintf(file, "\n");

    auto writeEntries = [&](const std::vector<EmmEffectEntry> &entries) {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const EmmEffectEntry &entry = entries[i];
            if (!entry.effectPath.empty())
            {
                fprintf(file, "%s = %s\n", entry.key.c_str(), entry.effectPath.c_str());
            }
            if (entry.hasShow)
            {
                fprintf(file, "%s.show = %s\n", entry.key.c_str(), entry.shown ? "true" : "false");
            }
            if (!entry.subsetPaths.empty() || !entry.subsetShows.empty())
            {
                int maxIndex = -1;
                for (std::map<int, std::string>::const_iterator it = entry.subsetPaths.begin();
                     it != entry.subsetPaths.end(); ++it)
                {
                    if (it->first > maxIndex)
                    {
                        maxIndex = it->first;
                    }
                }
                for (std::map<int, bool>::const_iterator it = entry.subsetShows.begin();
                     it != entry.subsetShows.end(); ++it)
                {
                    if (it->first > maxIndex)
                    {
                        maxIndex = it->first;
                    }
                }
                for (int n = 0; n <= maxIndex; ++n)
                {
                    std::map<int, std::string>::const_iterator pit = entry.subsetPaths.find(n);
                    if (pit != entry.subsetPaths.end())
                    {
                        fprintf(file, "%s[%d] = %s\n", entry.key.c_str(), n, pit->second.c_str());
                    }
                    std::map<int, bool>::const_iterator sit = entry.subsetShows.find(n);
                    if (sit != entry.subsetShows.end())
                    {
                        fprintf(file, "%s[%d].show = %s\n", entry.key.c_str(), n,
                                sit->second ? "true" : "false");
                    }
                }
            }
        }
        fprintf(file, "\n");
    };
    fprintf(file, "[Effect]\n");
    fprintf(file, "Default = %s\n", defaultPath_.c_str());
    writeEntries(defaultEntries_);
    for (const auto &section : effectSections_)
    {
        if (section.isDefault())
            continue;
        fprintf(file, "[%s]\nOwner = %s", section.name.c_str(), section.ownerKey.c_str());
        if (section.ownerSubsetIndex >= 0)
            fprintf(file, "[%d]", section.ownerSubsetIndex);
        if (!section.ownerMaterialName.empty())
            fprintf(file, "@%s", section.ownerMaterialName.c_str());
        fprintf(file, "\n");
        writeEntries(section.entries);
    }
    fclose(file);
    return true;
}

// ---------------------------------------------------------------------------
// EmdFile
// ---------------------------------------------------------------------------

bool EmdFile::Load(const char* path)
{
    version_ = 0;
    versionParsed_ = false;
    hasEntry_ = false;
    path_.clear();
    entry_ = EmmEffectEntry();

    if (path == nullptr) {
        return false;
    }
    path_ = path;

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rt") != 0 || file == nullptr) {
        MmeLogWrite((std::string(kErrOpen) + path).c_str(), 1);
        return false;
    }

    ParsedEmm parsed;
    char line[0x400];
    bool firstLine = true;
    std::string currentSection;
    int lineNo = 0;

    while (fgets(line, sizeof(line), file) != nullptr) {
        ++lineNo;
        if (firstLine) {
            firstLine = false;
            const unsigned char* b = reinterpret_cast<const unsigned char*>(line);
            if (b[0] == kUtf8Bom[0] && b[1] == kUtf8Bom[1] && b[2] == kUtf8Bom[2]) {
                MmeLogWrite((std::string(kErrLoad) + path).c_str(), 0);
                MmeLogWrite((std::string(kErrFileFmt) + path).c_str(), 1);
                fclose(file);
                return false;
            }
        }
        std::string text = TrimCopy(StripComment(line));
        if (text.empty()) {
            continue;
        }
        const std::string rejectLog =
            std::string(kErrLoad) + path + " (line " + std::to_string(lineNo) +
            "):\n  '" + std::string(line) + "'";
        std::string name;
        std::string key;
        std::string value;
        if (MatchSectionHeader(text, &name)) {
            // [FUN_18004D4C0] only the exact names "Info"/"Effect" reach the
            // shared OnSection; everything else (incl. [Object],
            // [Effect@...]) is rejected like an unknown line.
            if ((name == "Info" || name == "Effect") &&
                HandleSection(&parsed, name)) {
                currentSection = name;
            } else if (!parsed.abortLogging) {
                MmeLogWrite(rejectLog.c_str(), 1);
            }
            continue;
        }
        if (MatchKeyValuePair(text, &key, &value)) {
            // [FUN_18004D5A0] the "[Effect] Default" row is rejected before
            // the shared OnKeyValue sees it; everything else is delegated
            // with the isEmd flag set (Obj-only assignment keys).
            bool ok = false;
            if (!(currentSection == "Effect" && key == "Default")) {
                ok = HandleKeyValue(&parsed, currentSection, key, value, path_,
                                    true);
            }
            if (!ok && !parsed.abortLogging) {
                MmeLogWrite(rejectLog.c_str(), 1);
            }
            continue;
        }
        if (!parsed.abortLogging) {
            MmeLogWrite(rejectLog.c_str(), 1);
        }
    }
    fclose(file);

    // [FUN_18004D690] the end-of-load validator: "Info" registered, Version
    // parsed, exactly one [Effect] record with exactly one (Obj) entry.
    {
        std::string error;
        if (!HasSectionName(parsed, "Info")) {
            error = "section 'Info' was not found";
        } else if (!parsed.versionParsed || parsed.version == 0) {
            error = "key 'Version' was not found";
        } else if (parsed.sections.size() != 1) {
            error = "section 'Effect' was not found";
        } else if (parsed.sections[0].entries.size() != 1) {
            error = "key 'Obj' was not found";
        }
        if (!error.empty()) {
            MmeLogWrite((error + "\n").c_str(), 1);
            // [FUN_180047BD0 @EOF] the driver's post-validator summary (same
            // as the EmmFile side): "Error: failed to load file: <path>
            // (line <total physical lines>)", flag 1, gated on a1+8.
            if (!parsed.abortLogging) {
                MmeLogWrite((std::string(kErrLoad) + path + " (line " +
                             std::to_string(lineNo) + ")").c_str(), 1);
            }
            return false;
        }
    }

    version_ = parsed.version;
    entry_ = parsed.sections[0].entries[0];
    hasEntry_ = true;
    return true;
}

bool EmdFile::Save(const char* path) const
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wt") != 0 || file == nullptr) {
        MmeLogWrite((std::string(kErrOpen) + path).c_str(), 1);
        return false;
    }
    // [FUN_18004DC30] Version hardcoded 3; the [Effect] section carries the
    // single "Obj" entry: whole row (always), whole ".show" (when recorded),
    // then "[n]"/"[n].show" pairs for n = 0..max recorded subset.
    fprintf(file, "[Info]\n");
    fprintf(file, "Version = 3\n");
    fprintf(file, "\n");

    fprintf(file, "[Effect]\n");
    if (!entry_.effectPath.empty()) {
        fprintf(file, "Obj = %s\n", entry_.effectPath.c_str());
    }
    if (entry_.hasShow) {
        fprintf(file, "Obj.show = %s\n", entry_.shown ? "true" : "false");
    }
    int maxIndex = -1;
    for (std::map<int, std::string>::const_iterator it = entry_.subsetPaths.begin();
         it != entry_.subsetPaths.end(); ++it) {
        if (it->first > maxIndex) {
            maxIndex = it->first;
        }
    }
    for (std::map<int, bool>::const_iterator it = entry_.subsetShows.begin();
         it != entry_.subsetShows.end(); ++it) {
        if (it->first > maxIndex) {
            maxIndex = it->first;
        }
    }
    for (int n = 0; n <= maxIndex; ++n) {
        std::map<int, std::string>::const_iterator pit = entry_.subsetPaths.find(n);
        if (pit != entry_.subsetPaths.end()) {
            fprintf(file, "Obj[%d] = %s\n", n, pit->second.c_str());
        }
        std::map<int, bool>::const_iterator sit = entry_.subsetShows.find(n);
        if (sit != entry_.subsetShows.end()) {
            fprintf(file, "Obj[%d].show = %s\n", n,
                    sit->second ? "true" : "false");
        }
    }
    fprintf(file, "\n");
    fclose(file);
    return true;
}

// ---------------------------------------------------------------------------
// Manager entry points
// ---------------------------------------------------------------------------

const std::string& MmeEmmDefaultEffect()
{
    return DefaultEffectRef();
}

void MmeEmmSetDefaultEffect(const std::string& path)
{
    DefaultEffectRef() = path;
}

// --- per-subset show state ("[n].show" rows) ---------------------------------

void MmeEmmSetSubsetShown(ModelData* model, int subsetIndex, bool shown)
{
    // Three write entry points: applying EMM (MmeEmmLoad), applying EMD (MmeEmdLoad, replacing the whole table
    // first), and assignment-dialog Hide/Show integration (the original writes working-map (0, object,
    // subset) node +0x30). subsetIndex < 0 denotes the whole object and uses ModelData::setShown.
    if (model == nullptr || subsetIndex < 0) {
        return;
    }
    SubsetShowRegistry()[model->objectId()][subsetIndex] = shown;
}

bool MmeEmmSubsetShowRecorded(const ModelData* model, int subsetIndex, bool* shown)
{
    // hasShow query: an inner-map hit means the "[n].show" line exists.
    if (model == nullptr || subsetIndex < 0 || shown == nullptr) {
        return false;
    }
    std::map<unsigned long long, std::map<int, bool>>::const_iterator outer =
        SubsetShowRegistry().find(model->objectId());
    if (outer == SubsetShowRegistry().end()) {
        return false;
    }
    std::map<int, bool>::const_iterator inner = outer->second.find(subsetIndex);
    if (inner == outer->second.end()) {
        return false;
    }
    *shown = inner->second;
    return true;
}

bool MmeEmmEffectiveSubsetShown(const ModelData* model, int subsetIndex)
{
    // [FUN_18002DB10 0x18002db10] Effective visibility query for drawing/binding paths:
    // [0x18002db34] If the subset node hasShow, use its value; [0x18002db42-0x18002db5b]
    // otherwise fall back to the whole-object record (key -1; ModelData::shown() in the port);
    // [0x18002db61] if neither is recorded, default to visible (true).
    bool shown = false;
    if (MmeEmmSubsetShowRecorded(model, subsetIndex, &shown)) {
        return shown;
    }
    if (model != nullptr) {
        return model->shown();
    }
    return true;
}

void MmeEmmClearSubsetShows(ModelData* model)
{
    // [FUN_180031980] Applying EMD replaces the whole table: first clear all show records for the object (matching
    // erasure of its existing working-map nodes in the original), then write the file's lines.
    if (model == nullptr) {
        return;
    }
    SubsetShowRegistry().erase(model->objectId());
}

bool MmeEmmLoad(const char* path)
{
    EmmFile emm;
    if (!emm.Load(path)) {
        return false;
    }

    // The default row: "none" means no default (FUN_180030590 mode 0).
    const std::string defaultRow = emm.defaultEffect();
    MmeEmmSetDefaultEffect(defaultRow == kNone ? std::string() : defaultRow);

    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return true;
    }

    // RVA 2E8D0 commits a replacement working map through 2DF80, which
    // clears manager+0x30 before copying the parsed assignments. Rows absent
    // from the new file must not retain old subset effects or visibility.
    for (ModelData* model : ctx->models) {
        if (model == nullptr) {
            continue;
        }
        model->clearSubsetEffects();
        MmeEmmClearSubsetShows(model);
        model->resetShown();
        ApplyEffectToModel(model, std::string());
    }

    // --- live objects (the host enumeration FUN_18002E8D0 walks) -----------
    struct LiveObject {
        bool isModel;
        int slot;
        ModelData* model;
        std::string path;
    };
    std::vector<LiveObject> live;
    for (int i = 0; i < ExpGetPmdNum(); ++i) {
        LiveObject obj;
        obj.isModel = true;
        obj.slot = i;
        unsigned long long id = reinterpret_cast<unsigned long long>(ExpGetPmdID(i));
        std::unordered_map<unsigned long long, ModelData*>::const_iterator it =
            ctx->modelRegistry.find(id);
        obj.model = (it != ctx->modelRegistry.end()) ? it->second : nullptr;
        const char* file = ExpGetPmdFilename(i);
        obj.path = file != nullptr ? file : "";
        live.push_back(obj);
    }
    for (int i = 0; i < ExpGetAcsNum(); ++i) {
        LiveObject obj;
        obj.isModel = false;
        obj.slot = i;
        unsigned long long id = reinterpret_cast<unsigned long long>(ExpGetAcsID(i));
        std::unordered_map<unsigned long long, ModelData*>::const_iterator it =
            ctx->modelRegistry.find(id);
        obj.model = (it != ctx->modelRegistry.end()) ? it->second : nullptr;
        const char* file = ExpGetAcsFilename(i);
        obj.path = file != nullptr ? file : "";
        live.push_back(obj);
    }

    // Original 2E8D0 builds its live walk through a map keyed by +0xEC,
    // the cached absolute render order. Slot enumeration gives the wrong
    // duplicate-file match after objects are reordered in the host.
    std::stable_sort(live.begin(), live.end(),
                     [](const LiveObject& a, const LiveObject& b) {
                         const int first = a.model ? a.model->passPlanScratch().renderOrder : 0;
                         const int second = b.model ? b.model->passPlanScratch().renderOrder : 0;
                         return first < second;
                     });

    // [Object] records in the writer's slot-sorted order (FUN_18004C150).
    std::vector<EmmObjectRecord> records = emm.objectRecords();
    std::stable_sort(records.begin(), records.end(),
                     [](const EmmObjectRecord& a, const EmmObjectRecord& b) {
                         return a.slot < b.slot;
                     });

    // The EMM's own directory (the "Path" global FUN_18002E8D0 joins with).
    std::string emmDir;
    {
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char out[_MAX_PATH];
            if (_makepath_s(out, sizeof(out), drive, dir, nullptr, nullptr) == 0) {
                emmDir = out;
            }
        }
    }

    // --- two-pass type+path identity matching (NOT slot based) -------------
    std::map<std::string, ModelData*> byKey;
    std::vector<const LiveObject*> unmatched;
    // Original 2E8D0 / x86 2B970 consumes each record once. Finish the
    // exact-path walk for all objects before trying file-name-only matches;
    // otherwise duplicate files overwrite one key or steal a later exact match.
    std::vector<int> matches(live.size(), -1);
    std::vector<bool> claimed(records.size(), false);
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < live.size(); ++i) {
            if (matches[i] >= 0 || live[i].model == nullptr)
                continue;
            for (size_t r = 0; r < records.size(); ++r) {
                if (claimed[r] || records[r].isModel != live[i].isModel)
                    continue;
                const bool same = pass == 0
                    ? ObjectPathMatches(records[r].objectPath, live[i].path, emmDir)
                    : _stricmp(FileNameOf(records[r].objectPath).c_str(),
                               FileNameOf(live[i].path).c_str()) == 0;
                if (same) {
                    matches[i] = static_cast<int>(r);
                    claimed[r] = true;
                    break;
                }
            }
        }
    }
    for (size_t i = 0; i < live.size(); ++i) {
        if (matches[i] >= 0) {
            const EmmObjectRecord& record = records[matches[i]];
            byKey[KeyName(record.isModel, record.slot)] = live[i].model;
        } else {
            unmatched.push_back(&live[i]);
        }
    }

    // [0x1800B4D30] MessageBoxA(0x40): <emm path>\n + header + "\n  <name>".
    if (!unmatched.empty()) {
        std::string list;
        for (size_t i = 0; i < unmatched.size(); ++i) {
            list += "\n  ";
            list += FileNameOf(unmatched[i]->path);
        }
        MmeLogWrite((std::string(path) + "\n" + kErrFailedMapping + list).c_str(), 1);
    }

    // Apply base assignments first, then resolve owner-scoped rows against
    // their declaring offscreen resources. An Owner line alone assigns no path.
    std::vector<std::string> assignedPaths;
    const std::vector<EmmEffectSection>& sections = emm.effectSections();
    std::map<ModelData*, std::string> basePaths;
    for (const EmmEffectSection& section : sections) {
        if (!section.isDefault()) continue;
        for (const EmmEffectEntry& entry : section.entries) {
            const auto it = byKey.find(entry.key);
            if (it != byKey.end() && it->second != nullptr && !entry.effectPath.empty())
                basePaths[it->second] = entry.effectPath;
        }
    }
    for (ModelData* model : ctx->models) {
        if (model == nullptr) continue;
        const auto it = basePaths.find(model);
        // Native 2DF80 invalidates assignment records; 2CA80 then fills an
        // absent path via 2ACE0 (same-name .fx, embedded name, EMM default).
        // This is essential for scene carriers: 30590 deliberately omits
        // their paths when saving. Explicit "none" suppresses discovery.
        // Resolve roots before subsets/Owner rows so their resources exist.
        const std::string effectPath = it == basePaths.end()
            ? MmeFindEffectFileForModel(model)
            : it->second == kNone ? std::string() : it->second;
        ApplyEffectToModel(model, effectPath);
    }
    for (size_t s = 0; s < sections.size(); ++s) {
        const EmmEffectSection& section = sections[s];
        // Named rows belong to an offscreen resource, never to the base
        // scene's (turn 0, object, subset) assignments (RVA 2E8D0).
        if (!section.isDefault()) continue;
        for (size_t e = 0; e < section.entries.size(); ++e) {
            const EmmEffectEntry& entry = section.entries[e];
            std::map<std::string, ModelData*>::const_iterator it = byKey.find(entry.key);
            if (it == byKey.end() || it->second == nullptr) {
                continue;
            }
            ModelData* model = it->second;
            if (!entry.effectPath.empty()) {
                // "none" = the row exists but unassigns; "" = no row at all.
                const std::string effectPath = entry.effectPath == kNone
                                                   ? std::string()
                                                   : entry.effectPath;
                if (!effectPath.empty()) {
                    assignedPaths.push_back(effectPath);
                }
            }
            if (entry.hasShow) {
                model->setShown(entry.shown);
            }
            for (std::map<int, std::string>::const_iterator sit = entry.subsetPaths.begin();
                 sit != entry.subsetPaths.end(); ++sit) {
                ApplySubsetEffectToModel(model, sit->first, sit->second);
                if (sit->second != kNone && !sit->second.empty()) {
                    assignedPaths.push_back(sit->second);
                }
            }
            // [FUN_18002E8D0 at 0x18002fe7c-0x18002ffa9] Apply "[n].show" lines in the same loop as path lines:
            // subsets containing only a show line (without a path line) also find or create
            // a working node (hasShow=+1, value=+0x30). The iteration bound is the object's material count
            // (*(object+0x38)); ignore an entire line if n is out of range. Runtime behavior is in
            // MmeEmmEffectiveSubsetShown (draw-gate integration).
            const int emmShowCount = model->materialCount();
            for (std::map<int, bool>::const_iterator sit = entry.subsetShows.begin();
                 sit != entry.subsetShows.end(); ++sit) {
                if (sit->first >= 0 && sit->first < emmShowCount) {
                    MmeEmmSetSubsetShown(model, sit->first, sit->second);
                }
            }
        }
    }

    ApplyOwnerSections(sections, byKey);

    // [0x1800B4DA0] MessageBoxA(0x30): distinct assigned paths that do not
    // exist, sorted and deduplicated like FUN_18003D0C0/FUN_18003D250.
    {
        std::sort(assignedPaths.begin(), assignedPaths.end());
        assignedPaths.erase(std::unique(assignedPaths.begin(), assignedPaths.end()),
                            assignedPaths.end());
        std::vector<std::string> missing;
        for (size_t i = 0; i < assignedPaths.size(); ++i) {
            if (_access_s(assignedPaths[i].c_str(), 0) != 0) {
                missing.push_back(assignedPaths[i]);
            }
        }
        if (!missing.empty()) {
            std::string message = kErrMissingFx;
            for (size_t i = 0; i < missing.size(); ++i) {
                message += "\n";
                message += missing[i];
            }
            MmeLogWrite(message.c_str(), 1);
        }
    }
    return true;
}

bool MmeEmmSave(const char* path)
{
    // [FUN_1800315F0] serialize the live state: every host object in the
    // enumeration, absolute paths, "none" sentinels. FUN_1800315F0 copies
    // ModelData+0xec (absolute draw order), not the host enumeration index,
    // into both the key and the sort field (FUN_18004C150). The x86 twin
    // FUN_1002E5B0 reads ModelData+0x84 at 0x1002E680.
    std::vector<EmmSaveObject> rows;
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        for (int pass = 0; pass < 2; ++pass) {
            const bool isModel = (pass == 0);
            const int num = isModel ? ExpGetPmdNum() : ExpGetAcsNum();
            for (int i = 0; i < num; ++i) {
                EmmSaveObject row;
                row.record.isModel = isModel;
                const char* file = isModel ? ExpGetPmdFilename(i) : ExpGetAcsFilename(i);
                row.record.objectPath = file != nullptr ? AbsPathCopy(file) : "";
                row.model = FindModelBySlot(isModel, i);
                row.slot = row.model != nullptr ? row.model->passPlanScratch().renderOrder : 0;
                row.record.slot = row.slot;
                row.entry.key = KeyName(isModel, row.slot);
                if (row.model != nullptr) {
                    const std::string& effect = row.model->effectFile();
                    if (row.model->renderClass() != 0) {
                        // [sub_180030590 @0x1800309c3-0x180030a53] the
                        // +0x364 (scriptOrder/renderClass) carrier gate: a
                        // pass-class carrier serializes NO path row and NO
                        // subset rows - only the whole ".show" record can
                        // survive (the entry is still created; an empty
                        // effectPath keeps EmmFile::Save from emitting the
                        // row, exactly like the original's empty entry path).
                        row.entry.effectPath.clear();
                        row.entry.hasShow = row.model->shownRecorded();
                        row.entry.shown = row.model->shown();
                    } else {
                        row.entry.effectPath =
                            effect.empty() ? kNone : AbsPathCopy(effect);
                        row.entry.hasShow = row.model->shownRecorded();
                        row.entry.shown = row.model->shown();
                        for (std::map<int, std::string>::const_iterator it =
                                 row.model->subsetEffects().begin();
                             it != row.model->subsetEffects().end(); ++it) {
                            row.entry.subsetPaths[it->first] =
                                it->second.empty() ? kNone : AbsPathCopy(it->second);
                        }
                        // [FUN_180030590 @0x180030db2-0x180030e30] "[n].show"
                        // Line synthesis: write one line for each subset node with hasShow (value from +0x30),
                        // bounded by material count (*(object+0x38)); output only subsets recorded in the manager's
                        // registry (as in the original: no record means no line).
                        // EmmFile::Save interleaves these with "[n]" lines for n = 0..max(path,show)
                        // ([FUN_18004C160]).
                        bool emmRowShown = false;
                        for (int n = 0; n < row.model->materialCount(); ++n) {
                            if (MmeEmmSubsetShowRecorded(row.model, n, &emmRowShown)) {
                                row.entry.subsetShows[n] = emmRowShown;
                            }
                        }
                    }
                } else {
                    row.entry.effectPath = kNone;
                }
                rows.push_back(row);
            }
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const EmmSaveObject& a, const EmmSaveObject& b) { return a.slot < b.slot; });

    EmmFile emm;
    std::vector<EmmObjectRecord> objects;
    std::vector<EmmEffectEntry> entries;
    objects.reserve(rows.size());
    entries.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        objects.push_back(rows[i].record);
        entries.push_back(rows[i].entry);
    }
    emm.SetObjectRecords(objects);

    const std::string& def = DefaultEffectRef();
    emm.SetDefaultEffect(def.empty() ? kNone : AbsPathCopy(def));
    emm.SetDefaultEntries(entries);
    emm.SetOwnerSections(CollectOwnerSections(rows));
    return emm.Save(path);
}

bool MmeEmdLoad(const char* path, const std::vector<ModelData*>& targets)
{
    // [FUN_180031980] the per-model mapping: parse + validate, then apply the
    // single Obj entry to every target, replacing each target's whole+subset
    // assignment state first (the working-map walk erases the object's
    // existing nodes before the EMD rows are written back).
    EmdFile emd;
    if (!emd.Load(path)) {
        return false;
    }
    if (targets.empty()) {
        return true;
    }

    std::string emdDir;
    {
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char out[_MAX_PATH];
            if (_makepath_s(out, sizeof(out), drive, dir, nullptr, nullptr) == 0) {
                emdDir = out;
            }
        }
    }

    std::vector<std::string> assignedPaths;
    const EmmEffectEntry& entry = emd.entry();
    for (size_t i = 0; i < targets.size(); ++i) {
        ModelData* model = targets[i];
        if (model == nullptr) {
            continue;
        }
        // Replace: clear the whole-object assignment and every subset first.
        model->setEffectFile(std::string());
        model->clearSubsetEffects();
        MmeDropMaterialBindings(model);
        // [FUN_180031980] the replace-first walk also erases the object's
        // show records (hasShow nodes) before the EMD rows are written back.
        MmeEmmClearSubsetShows(model);
        model->resetShown();

        if (!entry.effectPath.empty()) {
            // "none"/"hide"/"main_default" resolve to the empty path.
            const std::string resolved = ResolveEmdValue(entry.effectPath, emdDir);
            ApplyEffectToModel(model, resolved);
            if (!resolved.empty()) {
                assignedPaths.push_back(resolved);
            }
        }
        if (entry.hasShow) {
            model->setShown(entry.shown);
        }
        // "[n]" rows beyond the object's material count are ignored (the
        // apply walk is bounded by *(object + 0x38)).
        const int materialCount = model->materialCount();
        for (std::map<int, std::string>::const_iterator it = entry.subsetPaths.begin();
             it != entry.subsetPaths.end(); ++it) {
            if (it->first < 0 || it->first >= materialCount) {
                continue;
            }
            const std::string resolved = ResolveEmdValue(it->second, emdDir);
            ApplySubsetEffectToModel(model, it->first,
                                     resolved.empty() ? kNone : resolved);
            if (!resolved.empty()) {
                assignedPaths.push_back(resolved);
            }
        }
        // Apply "[n].show" within the same bounds as "[n]" (material-count limit; ignore out-of-range lines); runtime
        // behavior is in MmeEmmEffectiveSubsetShown.
        for (std::map<int, bool>::const_iterator it = entry.subsetShows.begin();
             it != entry.subsetShows.end(); ++it) {
            if (it->first >= 0 && it->first < materialCount) {
                MmeEmmSetSubsetShown(model, it->first, it->second);
            }
        }
    }

    // [0x1800B4DA0] the missing-file report: distinct assigned paths that do
    // not exist (FUN_18003D0C0/FUN_18003D250 sort+dedup, then the existence
    // walk drops the present ones).
    {
        std::sort(assignedPaths.begin(), assignedPaths.end());
        assignedPaths.erase(std::unique(assignedPaths.begin(), assignedPaths.end()),
                            assignedPaths.end());
        std::vector<std::string> missing;
        for (size_t i = 0; i < assignedPaths.size(); ++i) {
            if (_access_s(assignedPaths[i].c_str(), 0) != 0) {
                missing.push_back(assignedPaths[i]);
            }
        }
        if (!missing.empty()) {
            std::string message = kErrMissingFx;
            for (size_t i = 0; i < missing.size(); ++i) {
                message += "\n";
                message += missing[i];
            }
            MmeLogWrite(message.c_str(), 1);
        }
    }
    return true;
}

bool MmeEmdSave(const char* path, ModelData* model)
{
    // [FUN_180032680] serialize ONE object: the whole row is always written
    // (absolute path resolved against the EMD's directory, else "none");
    // ".show"/"[n]" rows only for live records. Carrier objects
    // (*(object+0x364) != 0) skip the subset walk entirely
    // [@0x18003294f]; "[n].show" rows come from the manager-side registry
    // (one row per recorded subset, value = node +0x30
    // [@0x180032a60-0x180032ad7]).
    if (model == nullptr) {
        return false;
    }
    std::string emdDir;
    {
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char out[_MAX_PATH];
            if (_makepath_s(out, sizeof(out), drive, dir, nullptr, nullptr) == 0) {
                emdDir = out;
            }
        }
    }

    EmmEffectEntry entry;
    entry.key = "Obj";
    const std::string& whole = model->effectFile();
    entry.effectPath = whole.empty() ? kNone : AbsPathCopy(JoinPath(emdDir, whole));
    entry.hasShow = !whole.empty();   // the whole record is live iff assigned
    entry.shown = model->shown();
    if (model->renderClass() == 0) {  // [0x18003294f] the carrier gate
        for (std::map<int, std::string>::const_iterator it = model->subsetEffects().begin();
             it != model->subsetEffects().end(); ++it) {
            entry.subsetPaths[it->first] = it->second.empty()
                                               ? kNone
                                               : AbsPathCopy(JoinPath(emdDir, it->second));
        }
        bool emdShown = false;
        for (int n = 0; n < model->materialCount(); ++n) {
            if (MmeEmmSubsetShowRecorded(model, n, &emdShown)) {
                entry.subsetShows[n] = emdShown;
            }
        }
    }

    EmdFile emd;
    emd.SetEntry(entry);
    return emd.Save(path);
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
    if (subsetIndex >= 0) {
        ApplySubsetEffectToModel(model, subsetIndex, path);
        return true;
    } else {
        model->clearSubsetEffects();
        MmeDropMaterialBindings(model);
    }
    ApplyEffectToModel(model, path);
    return true;
}

// --- [FUN_18002DEF0 / sub_18000B880] Re-resolve bindings during hot reload ----------------------
// See the general comment in emm_manager.h: these entry points never modify assignment maps (effectFile /
// subsetEffects / show records); they rebuild binding references to cache entries from current assignments.

namespace {

// Empty path means all affected bindings for Reload All; otherwise match binding paths exactly.
bool EffectPathInReloadScope(const std::string& bindingPath, const std::string& path)
{
    return path.empty() || bindingPath == path;
}

// [0x18000ba10-0x18000bbcf] Shared assignment/reload failure reporting.
// The working-map path survives; only the failed binding loses its effect.
// A model-specific selection failure must not evict a compatible cached source.
void ReportEffectLoadFailure(const std::string& effectPath,
                         const std::shared_ptr<LoadedEffect>& loaded)
{
    if (loaded == nullptr) {
        return;
    }
    // [0x18000ba10] First part: original errorText + one "\n" (blank-line separator).
    MmeLogWrite((loaded->errorText + "\n").c_str(), 0);
    // [0x18000ba43] Unload the failed entry before assembling the dialog text.
    MmeEngineUnloadEffectFile(effectPath, loaded);
    // [0x18000ba52-0x18000ba74] ExpGetEnglishMode (fallback byte_1800D99DD=0,
    // Japanese) selects the prefix; text = prefix + path + "\n\n" + errorText.
    const char* prefix = MmeIsEnglishUiMode()
        ? "Failed to load effect file:"   // 0x1800b3b18 (EN)
        // unk_1800B3AE8, Shift-JIS "Failed to load effect file:"
        : "\x83\x47\x83\x74\x83\x46\x83\x4E\x83\x67\x83\x74"
          "\x83\x40\x83\x43\x83\x8B\x82\xCC\x93\xC7\x82\xDD"
          "\x8D\x9E\x82\xDD\x82\xC9\x8E\xB8\x94\x73\x82\xB5"
          "\x82\xDC\x82\xB5\x82\xBD\x3A";
    std::string message = prefix;
    message += effectPath;
    message += "\n\n";
    message += loaded->errorText;
    MmeLogWrite(message.c_str(), 1);   // dedup + MessageBoxA (FUN_180009080 flag)
}

// Load (failures have already been reported through sub_18000B880's failure path); return the cached entry on success.
std::shared_ptr<LoadedEffect> ReloadEffectForRebind(IDirect3DDevice9* device,
                                                    const std::string& effectPath, int materialCount)
{
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, effectPath, materialCount);
    if (loaded == nullptr || loaded->effect == nullptr) {
        if (loaded != nullptr && !loaded->errorText.empty()) {
            ReportEffectLoadFailure(effectPath, loaded);
        }
        // Empty errorText = missing file (stamp==0): sub_18000BC90 at 0x18000bd19
        // returns early and silently, without a log or dialog.
        return std::shared_ptr<LoadedEffect>();
    }
    return loaded;
}

// [0x18000bc10-0x18000bc53] v11 latch for "done.\n\n" (0x1800B3AE0): log only once for files
// actually passed through the loader this round (LoadedEffect::doneLogged); cache hits are silent.
void LogDoneOnce(const std::shared_ptr<LoadedEffect>& loaded)
{
    if (loaded != nullptr && !loaded->doneLogged) {
        loaded->doneLogged = true;
        MmeLogWrite("done.\n\n", 0);
    }
}

} // namespace

void MmeReleaseEffectBindings(const std::string& path)
{
    // Consumption phase A: clear effect/sas/owner in affected bindings in place (MmeEnsureMaterialBinding's
    // in-place update branch); the old cache entry executes FUN_18000B210 semantics through ~LoadedEffect
    // when its last reference dies. Assignments remain unchanged; absent bindings get no-effect placeholders (the original
    // creates working-map nodes at assignment time, and failed-load nodes likewise exist as no-effect bindings).
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return;
    }
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        ModelData* model = ctx->models[i];
        if (model == nullptr) {
            continue;
        }
        // Whole-object node (0, model, -1)
        const std::string& whole = model->effectFile();
        if (!whole.empty() && EffectPathInReloadScope(whole, path)) {
            MmeEnsureMaterialBinding(0, model, -1, nullptr, whole, nullptr);
        }
        // [n] subset node (0, model, n): traverse assignments unchanged, releasing only references
        for (std::map<int, std::string>::const_iterator it = model->subsetEffects().begin();
             it != model->subsetEffects().end(); ++it) {
            if (it->second.empty() || !EffectPathInReloadScope(it->second, path)) {
                continue;
            }
            MmeEnsureMaterialBinding(0, model, it->first, nullptr, it->second, nullptr);
        }
    }
}

bool MmeRebindEffectAssignments(const std::string& path)
{
    // Consumption phase B: resolve again from current assignments, whole-object then subsets per model (original
    // sub_18002C910 traversal: (object, -1) followed by (object, 0..count-1) for each object,
    // in model registration order). Failed-load assignments retain their values, waiting for the next
    // stamp change after file repair, as in the original working map.
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return false;
    }
    IDirect3DDevice9* device = ctx->device;
    bool any = false;
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        ModelData* model = ctx->models[i];
        if (model == nullptr) {
            continue;
        }
        const std::string& whole = model->effectFile();
        if (!whole.empty() && EffectPathInReloadScope(whole, path)) {
            std::shared_ptr<LoadedEffect> fresh = ReloadEffectForRebind(device, whole, model->materialCount());
            if (fresh != nullptr) {
                // Rebuild scene-class wiring (renderClass/drawsGeometry/sceneTechIndex) during resolution;
                // resolution internally calls ClearSasBinding when the class falls back to object.
                MmeResolveModelEffectBinding(model);
                LogDoneOnce(fresh);
                any = true;
            }
        }
        for (std::map<int, std::string>::const_iterator it = model->subsetEffects().begin();
             it != model->subsetEffects().end(); ++it) {
            if (it->second.empty() || !EffectPathInReloadScope(it->second, path)) {
                continue;
            }
            std::shared_ptr<LoadedEffect> fresh = ReloadEffectForRebind(device, it->second, model->materialCount());
            if (fresh != nullptr) {
                MmeResolveSubsetEffectBinding(model, it->first, it->second);
                LogDoneOnce(fresh);
                any = true;
            }
        }
    }
    return any;
}

bool MmeAnyEffectBindingForPath(const std::string& path)
{
    // [0x18000b972 `if (!*(a1))`] File-level equivalent: whether any live binding still references this .fx and holds
    // an effect. Traverse the manager binding tree directly (without MmeFindMaterialBinding's
    // offscreen DefaultEffect shortcut; polling semantics are independent of the drawing window).
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr) {
        return false;
    }
    for (std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::const_iterator it =
             manager->bindings.begin();
         it != manager->bindings.end(); ++it) {
        MaterialBinding* binding = it->second;
        if (binding != nullptr && binding->effect != nullptr &&
            binding->effectPath == path) {
            return true;
        }
    }
    return false;
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

    // (3) the EMM default row (only when the file exists; "" = none).
    const std::string& fallback = DefaultEffectRef();
    if (!fallback.empty() && _access_s(fallback.c_str(), 4) == 0) {
        return fallback;
    }
    return std::string();
}

void MmeAutoSaveEmmForPmm(const wchar_t* pmmPath)
{
    // [0x180057290] FUN_180057290: gate on g_emmAutoSave (DAT_1800d72e0),
    // narrow-convert the PMM path (FUN_180063340 = CP 0), skip "(invalid)",
    // build "<dir>\<name>.emm", then hand the path to FUN_1800431E0.
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
    // [FUN_1800431E0 at 0x180043307-0x1800433ea] Save directly and silently without a dialog (behaviorally
    // identical, no divergence): FUN_180057290 passes the prebuilt .emm path as the second argument
    // (0x1800573ed: mov r8, rbx / Buffer); when a2 != NULL, FUN_1800431E0
    // directly calls strcpy_s then FUN_1800315F0 to serialize. The GetSaveFileNameA branch
    // (a2 == NULL) belongs only to menu command 40010 (sub_180044080 at 0x1800451b9:
    // xor edx, edx). Canceling the dialog (return 0) skips saving on the menu path and has no bearing on automatic
    // saving.
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
