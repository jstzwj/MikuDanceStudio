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
// A non-empty line that matches NEITHER the section nor the pair pattern, or
// a k=v line appearing before any section header, fails the whole load with
// "Error: failed to load file: <path> (line N):\n  '<raw line>'"
// (sub_180009080(msg, 1) at LABEL_194, 0x180048b7d-0x180048dc6).

// [big-C 59239] UTF-8 BOM bytes compared on the first line.
const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };

// strings_evidence.md section 7 / PE 0x1800b5260, 0x1800b5280.
const char kErrOpen[]   = "Error: failed to open file ";
const char kErrLoad[]   = "Error: failed to load file: ";

// PE 0x1800b5338 (English) and 0x1800b52f0 (localized variant; Japanese
// Shift-JIS, byte-verified in the PE image: 71 bytes incl. NUL =
// "MikuMikuEffect ver.0.37 " + 0x82CD..0x8142, i.e.
// "はこの形式のファイルをサポートできません。:\n" - NOT a GBK/Chinese string).
const char kErrFormatEn[] = "MikuMikuEffect ver.0.37 cannot support this file format:\n";
const char kErrFormatLocal[] =
    "MikuMikuEffect ver.0.37 "
    "\x82\xCD\x82\xB1\x82\xCC\x8C\x60\x8E\xAE\x82\xCC\x83\x74\x83\x40\x83\x43\x83\x8B"
    "\x82\xF0\x83\x54\x83\x7C\x81\x5B\x83\x67\x82\xB5\x82\xC4\x82\xA2\x82\xDC\x82\xB9"
    "\x82\xF1\x81\x42\x3A\x0A";

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
    int lineNo = 0;             // [0x180048065] 1-based counter, bumped per fgets
    bool haveSection = false;   // original: sections vector (a1+56..a1+64) non-empty
    std::string currentSection;

    while (fgets(line, sizeof(line), f) != nullptr) {
        // [big-C 59237-59294] first-line BOM check.
        if (firstLine) {
            firstLine = false;
            const unsigned char* b = reinterpret_cast<const unsigned char*>(line);
            if (b[0] == kBom[0] && b[1] == kBom[1] && b[2] == kBom[2]) {
                // [0x18004884a] "Error: failed to load file: <path>" with
                // showMessageBox = 0 (log only). The localized format message
                // + path then goes ONLY to a MessageBoxA gated by the shown-
                // message set [0x180048939-0x1800489e7] - the original never
                // logs the format message itself.
                MmeLogWrite((std::string(kErrLoad) + path).c_str(), 0);
                std::string boxMsg =
                    std::string(MmeIsEnglishUiMode() ? kErrFormatEn : kErrFormatLocal) + path;
                if (MmeLogShouldShowMessageBox(boxMsg.c_str())) {
                    MessageBoxA(g_mainWindow, boxMsg.c_str(), "MikuMikuEffect", MB_ICONERROR);
                }
                fclose(f);
                return false;
            }
        }

        ++lineNo;
        std::string text = TrimCopy(StripComment(line));
        if (text.empty()) {
            continue; // [0x1800481cb] empty after comment-strip + trim -> skip
        }

        // [big-C 59363 / 0x1800481e8] section "^\[([^\[\]]+)\]$": the '+'
        // requires at least ONE inner character and no '[' / ']' inside, so
        // "[]" (empty name) does NOT match and falls through to the pair
        // pattern (which also rejects it -> load error below).
        if (text.size() >= 3 && text[0] == '[' && text[text.size() - 1] == ']') {
            bool innerOk = true;
            for (size_t i = 1; i + 1 < text.size(); ++i) {
                if (text[i] == '[' || text[i] == ']') {
                    innerOk = false;
                    break;
                }
            }
            if (innerOk) {
                currentSection = text.substr(1, text.size() - 2);
                sections_[currentSection]; // original pushes a new entry per header
                haveSection = true;
                continue;
            }
        }

        // [big-C 59368+] pair "^([^=\s]+)\s*=\s*(.*)$": a non-empty
        // whitespace-free key before the first '=', optional whitespace on
        // either side of it, the rest is the value.
        size_t eq = text.find('=');
        bool pairOk = false;
        std::string key;
        std::string value;
        if (eq != std::string::npos) {
            key = TrimCopy(text.substr(0, eq));
            if (!key.empty() && key.find_first_of(" \t\n\v\f\r") == std::string::npos) {
                value = text.substr(eq + 1);
                size_t v = 0;
                while (v < value.size() && isspace(static_cast<unsigned char>(value[v]))) {
                    ++v;
                }
                value.erase(0, v); // "\s*" after the '='
                pairOk = true;
            }
        }

        // [0x1800484d7] a k=v line before any section header (sections vector
        // empty), or a line matching neither pattern -> LABEL_194
        // [0x180048b7d-0x180048dc6]: report and fail the whole load. The
        // original splices the RAW fgets buffer (trailing newline included)
        // into  "Error: failed to load file: " + path + " (line N):\n  '"
        // + raw line + "'"  and shows it with sub_180009080(msg, 1).
        if (!pairOk || !haveSection) {
            std::string msg = std::string(kErrLoad) + path +
                              " (line " + std::to_string(lineNo) + "):\n  '" + line + "'";
            MmeLogWrite(msg.c_str(), 1);
            fclose(f);
            return false;
        }

        // [0x1800492e0] getString linear-scans the stored pairs and returns
        // the first hit, so a duplicate key keeps its FIRST value
        // (first-wins). insert() gives exactly that: no overwrite.
        sections_[currentSection].insert(std::make_pair(key, value));
    }

    fclose(f);
    return true;
}

const std::string& IniFile::GetString(const std::string& section, const std::string& key) const
{
    // [big-C 60011-60112 / 0x1800492e0] the original linearly scans the
    // section entries in document order and, for each entry whose name
    // matches, scans its pairs; the first (section,key) hit wins. The map
    // below merges duplicate section names and Load keeps the FIRST value of
    // duplicate keys (insert, no overwrite), which reproduces the same
    // first-wins lookups. Falls back to the global empty string when missing.
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
