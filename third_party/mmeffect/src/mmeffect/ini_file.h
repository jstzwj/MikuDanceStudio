// ini_file.h - IniFile (port of FUN_180047BD0 / FUN_1800492E0).
//
// Evidence:
//   - big-C 58929-59600 (FUN_180047bd0 = IniFile::load): fopen_s(path, "rt"),
//     fgets(0x400) per line, UTF-8 BOM check on the first line, then per line:
//       1. strip comment:      regex "[;#].*$"      -> ""
//       2. trim:               regex "^\s+|\s+$"    -> ""
//       3. section match:      regex "^\[([^\[\]]+)\]$"
//       4. key/value match:    regex "^([^=\s]+)\s*=\s*(.*)$"
//     (the four pattern strings were byte-verified in the PE at
//      0x1800b52a0 / 0x1800b52a8 / 0x1800b52b8 / 0x1800b52d0).
//     A non-empty line matching neither pattern (the section regex's '+'
//     needs 1+ inner chars, so "[]" is rejected here), or a k=v line before
//     any section header, fails the whole load with
//       "Error: failed to load file: <path> (line N):\n  '<raw fgets line>'"
//     via sub_180009080(msg, 1) (LABEL_194, 0x180048b7d-0x180048dc6). The
//     vtable hooks used by MMEffect.ini are all `return 1` (0x180029f20
//     through the vtable at 0x1800b4dd8), so those are the only triggers.
//   - big-C 60011-60112 (FUN_1800492e0 = IniFile::getString): linear scan in
//     document order, first (section,key) hit wins - duplicates are first-
//     wins; returns the stored value or the global empty string when absent.
//   - boost::lexical_cast value conversion (bad_lexical_cast RTTI) is replaced
//     by std::stoi/std::stod with try/catch (documented divergence).
//
// BOM behavior [big-C 59237-59294]: if the FIRST line starts with the UTF-8
// BOM EF BB BF the load fails, logging "Error: failed to load file: <path>"
// with box=0 (log only), then showing the localized "cannot support this
// file format" message + path in a MessageBoxA (shown-message dedup applies);
// the format message itself is never written to the log in the original.
#pragma once

#include <map>
#include <string>

namespace mme {

class IniFile {
public:
    IniFile();

    // [0x180047bd0] Load + parse. Returns true on success. Any line that is
    // non-empty after comment-strip+trim but matches neither the section nor
    // the pair pattern, or a k=v line before the first section header, logs
    // "Error: failed to load file: <path> (line N):\n  '<raw line>'"
    // (log + MessageBox) and fails the whole load - MME_Initialize then skips
    // every ini-derived key, exactly like the original's jz at 0x180056640.
    bool Load(const char* path);

    // [0x1800492e0] GetString(section, key): first-wins on duplicate keys and
    // duplicate section headers; empty string when not found.
    const std::string& GetString(const std::string& section, const std::string& key) const;

    // [0x1800494b0 area] GetInt/GetFloat equivalents used by later phases.
    // boost::lexical_cast is replaced by std::stoi/std::stod + try/catch;
    // on failure the default value is returned (the original throws
    // bad_lexical_cast which the callers let propagate).
    int GetInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    float GetFloat(const std::string& section, const std::string& key, float defaultValue = 0.0f) const;

private:
    std::string path_;  // original stores the path in the object [big-C 59131]
    bool loaded_;       // the 2-byte flag the ctor sets to 1 [big-C 59115]
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

// [0x1800564a0 L80-154] the exact "true" comparison used by Initialize for the
// [System] EMMAutoSave / SkipValidation keys: memcmp of min(len,4) bytes against
// "true" plus len == 4 (i.e. the value must be exactly "true").
bool MmeIniValueIsTrue(const std::string& value);

} // namespace mme
