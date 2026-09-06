// ini_file.cpp - see ini_file.h
#include "ini_file.h"

#include <cstdio>
#include <cctype>
#include <cstring>
#include <stdexcept>

#include "mme_globals.h"
#include "mme_log.h"

namespace mme {

namespace {

// Regex patterns used by the original (boost::regex; byte-verified in the PE):
//   0x1800b52a0 "[;#].*$"           0x1800b52a8 "^\s+|\s+$"
//   0x1800b52b8 "^\[([^\[\]]+)\]$"  0x1800b52d0 "^([^=\s]+)\s*=\s*(.*)$"
// They are simple enough to implement as straight-line string scans with
// identical observable results (documented divergence: no boost dependency).

// [big-C 59239] UTF-8 BOM bytes compared on the first line.
const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };

// strings_evidence.md section 7 / PE 0x1800b5260, 0x1800b5280.
const char kErrOpen[]   = "Error: failed to open file ";
const char kErrLoad[]   = "Error: failed to load file: ";

// PE 0x1800b5338 (English) and 0x1800b52f0 (localized variant; GBK bytes
// "MikuMikuEffect ver.0.37 " + B2BB D6A7 B3D6 B4CB B8F1 CADB B5C4 CEC4 BCFE 3A).
const char kErrFormatEn[] = "MikuMikuEffect ver.0.37 cannot support this file format:\n";
const char kErrFormatLocal[] =
    "MikuMikuEffect ver.0.37 \xB2\xBB\xD6\xA7\xB3\xD6\xB4\xCB\xB8\xF1\xCA\xBD\xB5\xC4\xCE\xC4\xBC\xFE\x3A";

std::string TrimCopy(const std::string& s)
{
    // regex "^\s+|\s+$" replacement equivalent.
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
    // regex "[;#].*$" replacement: cut at the first ';' or '#'.
    size_t cut = s.find_first_of(";#");
    if (cut == std::string::npos) {
        return s;
    }
    return s.substr(0, cut);
}

} // namespace

IniFile::IniFile()
    : loaded_(false)
{
    // [big-C 59115] *(undefined2*)(param_1 + 1) = 1;
    loaded_ = true;
}

bool IniFile::Load(const char* path)
{
    if (path == nullptr) {
        return false;
    }
    path_ = path;

    // [big-C 59134] fopen_s(&f, path, "rt"); on failure log with MessageBox.
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rt") != 0 || f == nullptr) {
        MmeLogWrite((std::string(kErrOpen) + path).c_str(), 1);
        return false;
    }

    char line[0x400];
    bool firstLine = true;
    bool parseOk = false;
    std::string currentSection;

    while (fgets(line, sizeof(line), f) != nullptr) {
        // [big-C 59237-59294] first-line BOM check.
        if (firstLine) {
            firstLine = false;
            const unsigned char* b = reinterpret_cast<const unsigned char*>(line);
            if (b[0] == kBom[0] && b[1] == kBom[1] && b[2] == kBom[2]) {
                // BOM found: "Error: failed to load file: <path>" (no box) +
                // the localized format error (box, deduped).
                MmeLogWrite((std::string(kErrLoad) + path).c_str(), 0);
                const char* msg = MmeIsEnglishUiMode() ? kErrFormatEn : kErrFormatLocal;
                MmeLogWrite((std::string(msg) + path).c_str(), 1);
                fclose(f);
                return false;
            }
        }

        std::string text = TrimCopy(StripComment(line));
        if (text.empty()) {
            continue;
        }

        // [big-C 59363] section match "^\[([^\[\]]+)\]$".
        if (text.size() >= 2 && text[0] == '[' && text[text.size() - 1] == ']') {
            bool onlyBrackets = true;
            for (size_t i = 1; i + 1 < text.size(); ++i) {
                if (text[i] == '[' || text[i] == ']') {
                    onlyBrackets = false;
                    break;
                }
            }
            if (onlyBrackets) {
                currentSection = text.substr(1, text.size() - 2);
                continue;
            }
        }

        // [big-C 59368-59378+] pair match "^([^=\s]+)\s*=\s*(.*)$".
        size_t eq = text.find('=');
        if (eq == std::string::npos) {
            continue; // no '=' -> not a key/value line
        }
        std::string key = TrimCopy(text.substr(0, eq));
        if (key.empty() || key.find_first_of(" \t") != std::string::npos) {
            continue; // the regex requires a whitespace-free key
        }
        std::string value = text.substr(eq + 1);
        // "\s*=\s*(.*)$" - the value starts after the spaces following '=';
        // trailing whitespace is already trimmed above (regex 2 ran on the
        // whole line before the pair regex in the original too).
        sections_[currentSection][key] = value;
    }

    parseOk = true;
    fclose(f);
    return parseOk;
}

const std::string& IniFile::GetString(const std::string& section, const std::string& key) const
{
    // [big-C 60011-60112] linear section search, then key search; falls back
    // to the global empty string when not found.
    std::map<std::string, std::map<std::string, std::string>>::const_iterator sit =
        sections_.find(section);
    if (sit != sections_.end()) {
        std::map<std::string, std::string>::const_iterator kit = sit->second.find(key);
        if (kit != sit->second.end()) {
            return kit->second;
        }
    }
    static const std::string kEmpty;
    return kEmpty;
}

int IniFile::GetInt(const std::string& section, const std::string& key, int defaultValue) const
{
    // boost::lexical_cast<int> equivalent; the original lets bad_lexical_cast
    // escape, later phases may keep that contract.
    try {
        return std::stoi(GetString(section, key));
    } catch (const std::exception&) {
        return defaultValue;
    }
}

float IniFile::GetFloat(const std::string& section, const std::string& key, float defaultValue) const
{
    try {
        return std::stof(GetString(section, key));
    } catch (const std::exception&) {
        return defaultValue;
    }
}

bool MmeIniValueIsTrue(const std::string& value)
{
    // [0x1800564a0 L86-101] memcmp(min(len,4), "true") == 0 && len == 4.
    if (value.size() != 4) {
        return false;
    }
    return memcmp(value.data(), "true", 4) == 0;
}

} // namespace mme
