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
//   - big-C 60011-60112 (FUN_1800492e0 = IniFile::getString): returns the
//     stored value or the global empty string when not found.
//   - boost::lexical_cast value conversion (bad_lexical_cast RTTI) is replaced
//     by std::stoi/std::stod with try/catch (documented divergence).
//
// BOM behavior [big-C 59237-59294]: if the FIRST line starts with the UTF-8
// BOM EF BB BF the load fails, logging "Error: failed to load file: <path>"
// (no box) followed by the localized "cannot support this file format" message.
#pragma once

#include <map>
#include <string>

namespace mme {

class IniFile {
public:
    IniFile();

    // [0x180047bd0] Load + parse. Returns true on success (the original's
    // virtual parse call at vtable slot 1; false also triggers the
    // "Error: failed to load file: <path>" log when loadedFlag_ is set).
    bool Load(const char* path);

    // [0x1800492e0] GetString(section, key): empty string when not found.
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
