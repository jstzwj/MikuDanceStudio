// mmhack_material_table.cpp - per-model material table parsed from the model
// file itself.
//
// Port of FUN_18000e020 [0x18000e020] (MMHack.dll.c lines 12819-12998): the
// model file is opened, dispatched by the 3-byte magic ("Pmd" -> FUN_18000c960,
// "PMX" -> FUN_18000bbf0), and afterwards every material entry's toon-texture
// string that is not one of the built-in "toon01.bmp".."toon10.bmp" names is
// rewritten into an absolute "<model dir>/<name>" path with '/' separators.
//
// Entry layout (0x78 bytes in the original; three std::wstring here):
//   +0x00 texName - toon texture reference of the material
//   +0x28 name0   - material name (kind 0 for GetMaterialName)
//   +0x50 name1   - material name EN (kind 1 for GetMaterialName)
#include "mmhack_state.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {

// Thrown on malformed model data; the original throws the same way from every
// parse helper (ThrowInfo DAT_180068128, payload = ftell at the failure point).
struct MmhParseException { long offset; };

inline void MmhThrowAt(FILE* f)
{
    MmhParseException e;
    e.offset = ftell(f);
    throw e;
}

// [FUN_18000b8f0] Read exactly `size` bytes, or fseek-skip when buf == null.
void MmhReadBytes(FILE* f, unsigned long size, void* buf)
{
    if (size == 0)
        return;
    if (buf == nullptr) {
        if (fseek(f, (long)size, SEEK_CUR) != 0)
            MmhThrowAt(f);
        return;
    }
    if (fread(buf, 1, size, f) != size)
        MmhThrowAt(f);
}

unsigned char MmhReadU8(FILE* f)
{
    unsigned char v = 0;
    MmhReadBytes(f, 1, &v);
    return v;
}

unsigned short MmhReadU16(FILE* f)
{
    unsigned short v = 0;
    MmhReadBytes(f, 2, &v);
    return v;
}

unsigned long MmhReadU32(FILE* f)
{
    unsigned long v = 0;
    MmhReadBytes(f, 4, &v);
    return v;
}

// [FUN_18000b7c0] MultiByteToWideChar wrapper (codepage 0 = CP_ACP, 0xfde9 =
// CP_UTF8). Returns "" for a null input and "(invalid)" when conversion fails.
std::wstring MmhAnsiToWide(const char* s, unsigned int codepage)
{
    if (s == nullptr)
        return std::wstring();
    int n = MultiByteToWideChar(codepage, 0, s, -1, nullptr, 0);
    if (n == 0)
        return L"(invalid)";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(codepage, 0, s, -1, &out[0], n);
    out.resize((size_t)n - 1);                    // drop the terminator
    return out;
}

// [FUN_18000b980] PMX text-buffer reader: i32 byte length, then raw bytes
// interpreted as UTF-16LE (encoding 0) or UTF-8 / CP 0xfde9 (encoding 1).
void MmhReadPmxText(FILE* f, int encoding, std::wstring* out)
{
    unsigned long len = MmhReadU32(f);
    if ((long)len < 0)
        MmhThrowAt(f);
    if (len == 0) {
        out->clear();                             // empty text buffer
        return;
    }
    std::vector<char> buf((size_t)len);
    MmhReadBytes(f, len, buf.data());
    if (encoding == 0) {
        out->assign(reinterpret_cast<const wchar_t*>(buf.data()), len / 2);
        return;
    }
    int n = MultiByteToWideChar(0xfde9 /*CP_UTF8*/, 0, buf.data(), (int)len,
                                nullptr, 0);
    if (n == 0)
        MmhThrowAt(f);
    std::vector<wchar_t> wide((size_t)n);
    MultiByteToWideChar(0xfde9, 0, buf.data(), (int)len, wide.data(), n);
    out->assign(wide.data(), (size_t)n);
}

// [FUN_18000bb40] PMX header text: i32 length, skipped without conversion.
void MmhSkipPmxText(FILE* f)
{
    unsigned long len = MmhReadU32(f);
    if ((long)len < 0)
        MmhThrowAt(f);
    MmhReadBytes(f, len, nullptr);
}

inline bool MmhIsSep(wchar_t c)
{
    return c == L'/' || c == L'\\';
}

// [FUN_1800127b0] End of the root-name / root-directory prefix of a path.
size_t MmhRootNameEnd(const std::wstring& s)
{
    size_t n = s.size();
    if (n == 0)
        return 0;
    if (n == 2 && MmhIsSep(s[0]) && MmhIsSep(s[1]))
        return 0;
    if (MmhIsSep(s[n - 1]))
        return n - 1;
    size_t x = s.find_last_of(L"/\\");            // [FUN_180011dd0] from n-1
    if (x == std::wstring::npos) {
        if (n < 2)
            return 0;
        x = s.rfind(L':');                        // [FUN_180012360] from n-2
        if (x == std::wstring::npos || x > n - 2)
            return 0;
    }
    if (x == 1 && MmhIsSep(s[0]))
        return 0;
    return x + 1;
}

// [FUN_1800128c0] Start of the root-directory part, given the root-name end.
size_t MmhRootDirStart(const std::wstring& s, size_t rootEnd)
{
    size_t n = s.size();
    if (rootEnd > 2 && n > 2 && s[1] == L':' && MmhIsSep(s[2]))
        return 2;                                 // "C:\"
    if (rootEnd == 2 && n >= 2 && MmhIsSep(s[0]) && MmhIsSep(s[1]))
        return std::wstring::npos;                // bare "\\\\"
    if (rootEnd > 4 && n > 4 && MmhIsSep(s[0]) && MmhIsSep(s[1]) &&
        s[2] == L'?' && MmhIsSep(s[3])) {
        size_t f = s.find_first_of(L"/\\", 4);    // "\\\\?\\x:" device paths
        return (f != std::wstring::npos && f < rootEnd) ? f : std::wstring::npos;
    }
    if (rootEnd > 3 && n > 3 && MmhIsSep(s[0]) && MmhIsSep(s[1]) &&
        !MmhIsSep(s[2])) {
        size_t f = s.find_first_of(L"/\\", 2);    // "\\\\srv\\share" UNC
        return (f != std::wstring::npos && f < rootEnd) ? f : std::wstring::npos;
    }
    if (rootEnd == 0)
        return std::wstring::npos;
    if (!MmhIsSep(s[0]))
        return std::wstring::npos;
    return 0;
}

// [FUN_180012f20] Start position of the filename (npos = no directory part).
size_t MmhFilenameStart(const std::wstring& s)
{
    size_t n = s.size();
    size_t rootEnd = MmhRootNameEnd(s);
    bool hasSep = (n != 0 && rootEnd < n) && MmhIsSep(s[rootEnd]);
    size_t rootDir = MmhRootDirStart(s, rootEnd);
    if (rootEnd != 0) {
        while (rootEnd - 1 != rootDir) {
            if (!MmhIsSep(s[rootEnd - 1]))
                break;
            rootEnd -= 1;
            if (rootEnd == 0)
                break;
        }
    }
    if (rootEnd == 1 && rootDir == 0 && hasSep)
        return std::wstring::npos;
    return rootEnd;
}

// [FUN_180013e90] parent_path(): everything before the last separator; root
// paths keep their separator; empty result when there is no directory part.
std::wstring MmhPathParentDir(const wchar_t* path)
{
    std::wstring s(path ? path : L"");
    size_t pos = MmhFilenameStart(s);
    if (pos == std::wstring::npos)
        return std::wstring();
    return s.substr(0, pos);
}

// [FUN_1800139d0] path append ("operator/="): inserts a '\\' unless the addend
// is rooted or the base already ends in ':', '/' or '\\'.
void MmhPathAppend(std::wstring* base, const std::wstring& addend)
{
    if (addend.empty())
        return;
    if (!MmhIsSep(addend[0]) && !base->empty()) {
        wchar_t last = (*base)[base->size() - 1];
        if (last != L':' && !MmhIsSep(last))
            *base += L'\\';
    }
    *base += addend;
}

// [FUN_180013870] Copy with every '\\' replaced by '/' (generic format).
std::wstring MmhGenericPath(const std::wstring& p)
{
    std::wstring out = p;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] == L'\\')
            out[i] = L'/';
    }
    return out;
}

// [FUN_1800149a0 + FUN_180014730] replace_extension(ext): strip the current
// extension (last '.' inside the filename part) and append "." + ext when ext
// does not already start with a dot.
void MmhReplaceExtension(std::wstring* path, const wchar_t* ext)
{
    size_t sep = path->find_last_of(L"/\\");
    size_t fnameStart = (sep == std::wstring::npos) ? 0 : sep + 1;
    size_t dot = path->find_last_of(L'.');
    if (dot != std::wstring::npos && dot > fnameStart)
        path->resize(dot);
    size_t elen = wcslen(ext);
    if (elen != 0) {
        if (ext[0] != L'.')
            *path += L'.';
        path->append(ext, elen);
    }
}

// ---------------------------------------------------------------------------
// [FUN_18000bbf0] PMX parser (lines 11403-11959). Fills `out` with one entry
// per material: +0x00 toon texture, +0x28 name (JP), +0x50 name (EN). The
// numeric material fields (0x41 bytes), texture/sphere indices and the memo
// text are skipped.
void MmhParsePmx(FILE* f, std::vector<MmhMaterialEntry>* out)
{
    out->clear();
    unsigned char hdr[17];
    MmhReadBytes(f, 17, hdr);
    float version;
    memcpy(&version, hdr + 4, 4);
    if (3.0 <= (double)version)
        MmhThrowAt(f);
    unsigned char globals = hdr[8];
    int encoding = hdr[9];
    unsigned char addlUv = hdr[10];
    unsigned char idxSize[6];
    memcpy(idxSize, hdr + 11, 6);
    if (globals < 8)
        MmhThrowAt(f);
    if (globals > 8)
        MmhReadBytes(f, globals - 8, nullptr);
    if (encoding > 1)
        MmhThrowAt(f);
    for (int i = 0; i < 6; i++) {
        if (idxSize[i] != 1 && idxSize[i] != 2 && idxSize[i] != 4)
            MmhThrowAt(f);
    }
    const unsigned long vtxIdx = idxSize[0];
    const unsigned long texIdx = idxSize[1];
    const unsigned long boneIdx = idxSize[3];

    MmhSkipPmxText(f);                            // name (JP)
    MmhSkipPmxText(f);                            // name (EN)
    MmhSkipPmxText(f);                            // comment (JP)
    MmhSkipPmxText(f);                            // comment (EN)

    unsigned long vertCount = MmhReadU32(f);
    for (unsigned long i = 0; i < vertCount; i++) {
        MmhReadBytes(f, (addlUv + 2) * 0x10, nullptr);   // pos+normal+uv+addlUV
        unsigned char weight = MmhReadU8(f);
        switch (weight) {
        case 0: MmhReadBytes(f, boneIdx, nullptr); break;
        case 1: MmhReadBytes(f, boneIdx * 2 + 4, nullptr); break;
        case 2: MmhReadBytes(f, boneIdx * 4 + 0x10, nullptr); break;
        case 3: MmhReadBytes(f, boneIdx * 2 + 0x28, nullptr); break;
        default: MmhThrowAt(f);
        }
        MmhReadBytes(f, 4, nullptr);              // edge scale
    }

    unsigned long surfCount = MmhReadU32(f);
    MmhReadBytes(f, vtxIdx * surfCount, nullptr);

    unsigned long texCount = MmhReadU32(f);
    std::vector<std::wstring> textures((size_t)texCount);
    for (unsigned long i = 0; i < texCount; i++)
        MmhReadPmxText(f, encoding, &textures[i]);

    unsigned long matCount = MmhReadU32(f);
    out->reserve((size_t)matCount);
    for (unsigned long m = 0; m < matCount; m++) {
        MmhMaterialEntry e;
        MmhReadPmxText(f, encoding, &e.name0);    // material name (JP) -> +0x28
        MmhReadPmxText(f, encoding, &e.name1);    // material name (EN) -> +0x50
        MmhReadBytes(f, 0x41, nullptr);           // diffuse/specular/ambient/flag/edge
        MmhReadBytes(f, texIdx * 2 + 1, nullptr); // texture+sphere index, sphere mode
        unsigned char toonFlag = MmhReadU8(f);
        if (toonFlag == 0) {
            long toonRef = 0;
            if (texIdx != 0) {
                unsigned long raw = 0;
                MmhReadBytes(f, texIdx, &raw);
                switch (texIdx) {                 // sign-extend the index
                case 1: toonRef = (signed char)raw; break;
                case 2: toonRef = (signed short)raw; break;
                default: toonRef = (long)raw; break;
                }
            }
            // The original indexes the texture table whenever toonRef < texCount
            // (signed), which reads out of bounds for -1; out-of-range values
            // are kept as an empty string here.
            if (toonRef >= 0 && (unsigned long)toonRef < texCount)
                e.texName = textures[(size_t)toonRef];
        } else {
            unsigned char shared = MmhReadU8(f);
            if (shared < 10) {
                wchar_t buf[0x20];
                swprintf(buf, 0x20, L"toon%02d.bmp", (int)shared + 1);
                e.texName = buf;
            }
        }
        std::wstring memo;                        // read and discarded
        MmhReadPmxText(f, encoding, &memo);
        MmhReadBytes(f, 4, nullptr);              // face vertex count
        out->push_back(e);
    }
}

// ---------------------------------------------------------------------------
// [FUN_18000c960] PMD parser (lines 11963-12815). PMD has no material names:
// the per-material toon index is collected from each material record, the
// built-in toon table (10 x 100 byte ANSI names) is read from the file tail,
// and the material names come from the model-side "<model>.txt" companion file
// (path = model path with the extension replaced by "txt" [DAT_18005ccf0]).
void MmhParsePmd(FILE* f, const wchar_t* modelPath, std::vector<MmhMaterialEntry>* out)
{
    out->clear();
    unsigned char hdr[0x11b];
    MmhReadBytes(f, 0x11b, hdr);
    float version;
    memcpy(&version, hdr + 3, 4);
    if ((double)version != 1.0)
        MmhThrowAt(f);

    unsigned long vertCount = MmhReadU32(f);
    MmhReadBytes(f, vertCount * 0x26, nullptr);
    unsigned long faceCount = MmhReadU32(f);
    MmhReadBytes(f, faceCount * 2, nullptr);

    unsigned long matCount = MmhReadU32(f);
    std::vector<unsigned char> toonIdx((size_t)matCount);
    for (unsigned long m = 0; m < matCount; m++) {
        MmhReadBytes(f, 0x2c, nullptr);           // diffuse+specular+ambient
        toonIdx[(size_t)m] = MmhReadU8(f);        // toon index
        MmhReadBytes(f, 0x19, nullptr);           // edge+face count+texture name
    }

    unsigned short boneCount = MmhReadU16(f);
    MmhReadBytes(f, (unsigned long)boneCount * 0x27, nullptr);
    unsigned short ikCount = MmhReadU16(f);
    for (unsigned short i = 0; i < ikCount; i++) {
        MmhReadBytes(f, 4, nullptr);
        unsigned char links = MmhReadU8(f);
        MmhReadBytes(f, (unsigned long)links * 2 + 6, nullptr);
    }
    unsigned short morphCount = MmhReadU16(f);
    for (unsigned short i = 0; i < morphCount; i++) {
        MmhReadBytes(f, 0x14, nullptr);           // morph name
        unsigned long vcount = MmhReadU32(f);
        MmhReadBytes(f, vcount * 0x10 + 1, nullptr);
    }
    unsigned char frameCount = MmhReadU8(f);
    MmhReadBytes(f, (unsigned long)frameCount * 2, nullptr);
    unsigned char toonCount = MmhReadU8(f);
    MmhReadBytes(f, (unsigned long)toonCount * 0x32, nullptr);
    unsigned long tailCount = MmhReadU32(f);
    MmhReadBytes(f, tailCount * 3, nullptr);
    unsigned char extFlag = MmhReadU8(f);
    if (extFlag != 0) {
        MmhReadBytes(f, 0x114, nullptr);
        MmhReadBytes(f, (unsigned long)boneCount * 5 * 4, nullptr);
        MmhReadBytes(f, ((unsigned long)morphCount * 5 - 5) * 4, nullptr);
        MmhReadBytes(f, (unsigned long)toonCount * 0x32, nullptr);
    }

    // built-in toon table: exactly 10 entries of 100 ANSI bytes each
    std::vector<std::string> toonTable;
    toonTable.reserve(10);
    for (int i = 0; i < 10; i++) {
        char name[100];
        MmhReadBytes(f, 100, name);
        size_t len = strnlen(name, sizeof(name)); // original uses strlen()
        toonTable.push_back(std::string(name, len));
    }

    // companion "<model>.txt": one material name per line, UTF-8 with BOM or
    // ANSI (CP_ACP); every line is kept, including empty ones
    std::vector<std::wstring> lines;
    {
        std::wstring txtPath = modelPath;
        MmhReplaceExtension(&txtPath, L"txt");
        FILE* tf = nullptr;
        if (_wfopen_s(&tf, txtPath.c_str(), L"rb") == 0 && tf != nullptr) {
            // [12566-12587] a short BOM read throws (empty/corrupt .txt);
            // a missing file only leaves the name list empty
            unsigned int codepage = 0;            // CP_ACP
            unsigned char bom[3];
            if (fread(bom, 1, 3, tf) == 3 &&
                bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
                codepage = 0xfde9;                // CP_UTF8
            else
                fseek(tf, 0, SEEK_SET);
            char line[0x400];
            while (fgets(line, sizeof(line), tf) != nullptr) {
                char* crlf = strpbrk(line, "\r\n");
                if (crlf != nullptr)
                    *crlf = '\0';
                lines.push_back(MmhAnsiToWide(line, codepage));
            }
            fclose(tf);
        }
    }

    for (unsigned long m = 0; m < matCount; m++) {
        MmhMaterialEntry e;
        unsigned char ti = toonIdx[(size_t)m];
        if (ti < 10) {
            if ((size_t)ti < toonTable.size())
                e.texName = MmhAnsiToWide(toonTable[(size_t)ti].c_str(), 0);
            else {
                wchar_t buf[0x20];
                swprintf(buf, 0x20, L"toon%02d.bmp", (int)ti + 1);
                e.texName = buf;
            }
        }
        if ((size_t)m < lines.size()) {
            e.name0 = lines[(size_t)m];           // +0x28
            e.name1 = lines[(size_t)m];           // +0x50 (same line, original)
        }
        out->push_back(e);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// [FUN_18000e020] Model material table loader. Opens the model file, dispatches
// on the 3-byte magic, then resolves every material's toon texture against the
// model directory (built-in toon01..toon10.bmp stay untouched).
void MmhMaterialTableLoadFromFile(const wchar_t* modelPath,
                                  std::vector<MmhMaterialEntry>* out)
{
    if (modelPath == nullptr)
        return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, modelPath, L"rb") != 0 || f == nullptr)
        return;                                   // original: silent no-op
    try {
        char magic[3];
        if (fread(magic, 1, 3, f) != 3)
            MmhThrowAt(f);
        fseek(f, 0, SEEK_SET);
        if (_strnicmp(magic, "Pmd", 3) == 0)
            MmhParsePmd(f, modelPath, out);
        else if (_strnicmp(magic, "PMX", 3) == 0)
            MmhParsePmx(f, out);
        else
            MmhThrowAt(f);                        // unknown magic -> 0xffffffff

        // toon post-processing [lines 12889-12990]
        std::wstring dir = MmhPathParentDir(modelPath);
        for (size_t i = 0; i < out->size(); i++) {
            MmhMaterialEntry& e = (*out)[i];
            const std::wstring& tex = e.texName;
            if (tex.empty())
                continue;
            bool builtin = false;
            if (tex.size() == 10 && wcsncmp(tex.c_str(), L"toon", 4) == 0 &&
                wcsncmp(tex.c_str() + 6, L".bmp", 5) == 0) {
                if (tex[4] == L'0' && tex[5] >= L'1' && tex[5] <= L'9')
                    builtin = true;               // toon01..toon09.bmp
                if (tex[4] == L'1' && tex[5] == L'0')
                    builtin = true;               // toon10.bmp
            }
            if (!builtin) {
                std::wstring combined = dir;
                MmhPathAppend(&combined, tex);
                e.texName = MmhGenericPath(combined);
            }
        }
    } catch (...) {
        // The original lets these exceptions escape BeginScene (uncaught in
        // MMD); swallow them so a malformed file degrades to a partial table.
    }
    fclose(f);
}
