// sas_interpreter.cpp - the MMEffect SAS interpreter: STANDARDSGLOBAL scan,
// parameter/semantic validation, texture resource objects and the
// technique/pass model.
//
// Ports (see PHASE3_IMPLEMENTATION_NOTES.md for the per-block inventory):
//   FUN_18000c470 [0x18000c470]  STANDARDSGLOBAL scan + parameter validation
//                                (the two .rdata validation tables at
//                                0x1800B2FA0 / 0x1800B36A0 are transcribed
//                                below as kSemanticTable / kNameTable)
//   FUN_18000f3a0 [0x18000f3a0]  semantic-id -> resource-object dispatch
//   FUN_180011960 [0x180011960]  resource object build from annotations
//   FUN_1800143d0 [0x1800143d0]  texture creation + capability checks
//   FUN_1800169d0 [0x1800169d0]  technique scan (MmdPass/Use*/Subset/Script)
//   FUN_180017a80 [0x180017a80]  pass scan (shader version mix, PSIZE15)
#include "sas_exec.h"

#include "anime_texture.h"   // MmeAnimeConstruct*/RegisterParsed (0x2D parse)
#include "mme_globals.h"   // g_skipValidation (DAT_1800d99d9 - the selector's
                           // validity-byte switch at 0x18001dcb3)
#include "mme_util.h"
#include "mme_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cctype>

namespace mme {

// ===========================================================================
// Static validation tables transcribed from the original binary
// ===========================================================================

// [0x1800B2FA0..0x1800B36A0] semantic table, 56 entries of 32 bytes:
// { u32 id; char* name; u64 (class=lo32, type=hi32); u64 (rows*cols=lo32) }.
// The c470 walk [0x18000e449-0x18000e4df] scans exactly 0x38 (56) entries
// ending at id 0x4E (GroundShadowTarget) - "matWorld" lives only in the
// name table B at 0x1800B36A0.
// D3DXPARAMETER_CLASS: 0=SCALAR 1=VECTOR 2=MATRIX_ROWS 3=MATRIX_COLUMNS
// D3DXPARAMETER_TYPE:  0=VOID 1=BOOL 2=INT 3=FLOAT 5=TEXTURE 7=TEXTURE2D
struct SasSemanticEntry {
    int         id;
    const char* name;
    int         cls;
    int         type;
    int         count;  // expected Rows*Columns (255 = "any")
};

const SasSemanticEntry kSemanticTable[] = {
    {0x00, "World", 2, 3, 16}, {0x01, "WorldInverse", 2, 3, 16},
    {0x02, "WorldTranspose", 2, 3, 16}, {0x03, "WorldInverseTranspose", 2, 3, 16},
    {0x04, "View", 2, 3, 16}, {0x05, "ViewInverse", 2, 3, 16},
    {0x06, "ViewTranspose", 2, 3, 16}, {0x07, "ViewInverseTranspose", 2, 3, 16},
    {0x08, "Projection", 2, 3, 16}, {0x09, "ProjectionInverse", 2, 3, 16},
    {0x0A, "ProjectionTranspose", 2, 3, 16}, {0x0B, "ProjectionInverseTranspose", 2, 3, 16},
    {0x0C, "WorldView", 2, 3, 16}, {0x0D, "WorldViewInverse", 2, 3, 16},
    {0x0E, "WorldViewTranspose", 2, 3, 16}, {0x0F, "WorldViewInverseTranspose", 2, 3, 16},
    {0x10, "ViewProjection", 2, 3, 16}, {0x11, "ViewProjectionInverse", 2, 3, 16},
    {0x12, "ViewProjectionTranspose", 2, 3, 16}, {0x13, "ViewProjectionInverseTranspose", 2, 3, 16},
    {0x14, "WorldViewProjection", 2, 3, 16}, {0x15, "WorldViewProjectionInverse", 2, 3, 16},
    {0x16, "WorldViewProjectionTranspose", 2, 3, 16},
    {0x17, "WorldViewProjectionInverseTranspose", 2, 3, 16},
    {0x18, "Diffuse", 1, 3, 4}, {0x19, "Ambient", 1, 3, 4},
    {0x1A, "Emissive", 1, 3, 4}, {0x1B, "Specular", 1, 3, 4},
    {0x1F, "Position", 1, 3, 4}, {0x20, "Direction", 1, 3, 4},
    {0x1C, "ToonColor", 1, 3, 4}, {0x1D, "EdgeColor", 1, 3, 4},
    {0x1E, "SpecularPower", 1, 3, 255}, {0x21, "ViewportPixelSize", 1, 3, 2},
    {0x22, "Time", 0, 3, 1}, {0x23, "ElapsedTime", 0, 3, 1},
    {0x24, "Time2", 0, 3, 1}, {0x25, "ElapsedTime2", 0, 3, 1},
    {0x26, "RenderColorTarget", 4, 5, 0}, {0x27, "RenderDepthStencilTarget", 4, 5, 0},
    {0x28, "ControlObject", 1, 3, 255}, {0x29, "MaterialTexture", 4, 5, 0},
    {0x2A, "MaterialSphereMap", 4, 5, 0}, {0x2B, "MaterialToonTexture", 4, 5, 0},
    {0x2F, "MousePosition", 1, 3, 2}, {0x30, "LeftMouseDown", 1, 3, 4},
    {0x31, "MiddleMouseDown", 1, 3, 4}, {0x32, "RightMouseDown", 1, 3, 4},
    {0x2D, "AnimatedTexture", 4, 5, 0}, {0x2E, "OffScreenRenderTarget", 4, 5, 0},
    {0x33, "TextureValue", 1, 3, 4},
    {0x4A, "AddingTexture", 1, 3, 4}, {0x4B, "MultiplyingTexture", 1, 3, 4},
    {0x4C, "AddingSphereTexture", 1, 3, 4}, {0x4D, "MultiplyingSphereTexture", 1, 3, 4},
    {0x4E, "GroundShadowColor", 1, 3, 4}
};

// [0x1800B36A0..0x1800B39A8] parameter-name table, 24 entries of 32 bytes
// (matWorld..SphCMul): matched by _stricmp against D3DXPARAMETER_DESC.Name.
const SasSemanticEntry kNameTable[] = {
    {0x34, "matWorld", 2, 3, 16}, {0x14, "matWorldViewProj", 2, 3, 16},
    {0x35, "matLightViewProj", 2, 3, 16}, {0x36, "matRotate", 2, 3, 16},
    {0x39, "EgColor", 1, 3, 4}, {0x1C, "ToonColor", 1, 3, 4},
    {0x37, "LightDir", 1, 3, 4}, {0x3A, "SpcColor", 1, 3, 4},
    {0x38, "Place", 1, 3, 4}, {0x3B, "DifColor", 1, 3, 4},
    {0x3C, "parthf", 0, 1, 1}, {0x3D, "spadd", 0, 1, 1},
    {0x3E, "transp", 0, 1, 1}, {0x3F, "use_texture", 0, 1, 1},
    {0x40, "use_spheremap", 0, 1, 1}, {0x41, "use_subtexture", 0, 1, 1},
    {0x42, "use_toon", 0, 1, 1}, {0x43, "VertexCount", 0, 2, 1},
    {0x44, "SubsetCount", 0, 2, 1}, {0x45, "opadd", 0, 1, 1},
    {0x46, "TexCAdd", 1, 3, 4}, {0x47, "TexCMul", 1, 3, 4},
    {0x48, "SphCAdd", 1, 3, 4}, {0x49, "SphCMul", 1, 3, 4}
};

// ===========================================================================
// Small helpers
// ===========================================================================

// SasLogLine/SasLogFormat are defined in sas_exec.cpp (declared in sas_exec.h).

static std::string ToLowerAscii(const std::string& s) {
    std::string out(s);
    // [FUN_180011960 L15276-15280] ctype do_tolower loop over the string.
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

// [sub_180011800] trim " \t\v\r\n" (strchr set 0x1800B4168; no \f) from both
// ends of `s`. Used by the DefaultEffect row parser for both the key and the
// value text.
static std::string SasTrimWhitespace(const std::string& s) {
    static const char* kWs = " \t\v\r\n";
    size_t begin = 0;
    while (begin < s.size() && strchr(kWs, s[begin]) != nullptr) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && strchr(kWs, s[end - 1]) != nullptr) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// ===========================================================================
// Annotation getters (FUN_18000ee00 / FUN_18000f2f0 ports)
// ===========================================================================

// FUN_18000f2f0: annotation lookup. The original never uses D3DX's
// GetAnnotationByName - it walks GetAnnotation(param, i) and compares the
// annotation names with _stricmp, so annotation matching is case-insensitive.
static D3DXHANDLE SasFindAnnotation(ID3DXEffect* effect, D3DXHANDLE param,
                                    const char* name) {
    D3DXPARAMETER_DESC pd;
    if (effect->GetParameterDesc(param, &pd) != S_OK) {
        return nullptr;
    }
    for (UINT i = 0; i < pd.Annotations; ++i) {
        D3DXHANDLE ann = effect->GetAnnotation(param, i);
        if (ann == nullptr) {
            continue;
        }
        D3DXPARAMETER_DESC ad;
        if (effect->GetParameterDesc(ann, &ad) != S_OK || ad.Name == nullptr) {
            continue;
        }
        if (_stricmp(ad.Name, name) == 0) {
            return ann;
        }
    }
    return nullptr;
}

// FUN_18000ee00: read a string annotation. mode 0 = silent, 1 = error when
// missing, 2 = warning when missing. Returns false when the annotation is
// absent or not a string ("Error: type of annotation '%s' is invalid.").
static bool SasGetAnnotationString(ID3DXEffect* effect, D3DXHANDLE param,
                                   const char* name, int mode,
                                   std::string* out, SasEffect* sas) {
    out->clear();
    D3DXHANDLE ann = SasFindAnnotation(effect, param, name);
    if (ann == nullptr) {
        if (mode == 1 || mode == 2) {
            D3DXPARAMETER_DESC pd;
            const char* pname = "?";
            if (effect->GetParameterDesc(param, &pd) == S_OK && pd.Name != nullptr) {
                pname = pd.Name;
            }
            SasLogFormat(sas,
                         mode == 1
                             ? "Error: annotation '%s' is not found. (parameter: %s)\n"
                             : "Warning: annotation '%s' is not found. ignored. (parameter: %s)\n",
                         name, pname);
            if (mode == 1) {
                sas->hasErrors = true;
            }
        }
        return false;
    }
    LPCSTR value = nullptr;
    if (effect->GetString(ann, &value) != S_OK || value == nullptr) {
        D3DXPARAMETER_DESC pd;
        const char* pname = "?";
        if (effect->GetParameterDesc(param, &pd) == S_OK && pd.Name != nullptr) {
            pname = pd.Name;
        }
        SasLogFormat(sas, "Error: type of annotation '%s' is invalid. (parameter: %s)\n",
                     name, pname);
        sas->hasErrors = true;
        return false;
    }
    *out = value;
    return true;
}

// ===========================================================================
// Texture format tables, transcribed 1:1 from the x64 original
//   name pointers [0x1800D72F0..0x1800D74D0) -> string blob
//                  [0x1800B2774..0x1800B2A00)
//   values         [0x1800B39A0..0x1800B3A90)  (60 dwords)
// 60 entries, matched with stricmp after a strnicmp("FMT_",4) /
// strnicmp("D3DFMT_",7) prefix strip [sub_180011960, 0x180012C43..0x180012DE7;
// 反查 sub_180011920——两处循环上界均为 0x3C=60，名字/值表同索引配对].
// NOTE (2026-09 复核，前注有误): 前版注释称 "x64 值表 [32..53] 区间整体
// 错位一格且缺 0x6F，两份原版不一致"。逐字节重读 x64 0x1800B39A0 全部
// 60 项后否证：FourCC 段 'UYVY','RGBG','YUY2','GRGB','DXT1'..'DXT5','MET1'
// 与 L16=0x51、R16F=0x6F 均逐项就位，x64 序列与 x86 完全一致——本表
// 即 x64 1:1 转录，D24S8=0x4B 等争议项无任何偏差，勿再"修复"。
// ===========================================================================

struct SasFormatEntry {
    const char* name;
    D3DFORMAT   format;
};

const SasFormatEntry kFormatTable[] = {
    {"UNKNOWN", D3DFMT_UNKNOWN}, {"R8G8B8", D3DFMT_R8G8B8},
    {"A8R8G8B8", D3DFMT_A8R8G8B8}, {"X8R8G8B8", D3DFMT_X8R8G8B8},
    {"R5G6B5", D3DFMT_R5G6B5}, {"X1R5G5B5", D3DFMT_X1R5G5B5},
    {"A1R5G5B5", D3DFMT_A1R5G5B5}, {"A4R4G4B4", D3DFMT_A4R4G4B4},
    {"R3G3B2", D3DFMT_R3G3B2}, {"A8", D3DFMT_A8},
    {"A8R3G3B2", D3DFMT_A8R3G3B2}, {"X4R4G4B4", D3DFMT_X4R4G4B4},
    {"A2B10G10R10", D3DFMT_A2B10G10R10}, {"A8B8G8R8", D3DFMT_A8B8G8R8},
    {"X8B8G8R8", D3DFMT_X8B8G8R8}, {"G16R16", D3DFMT_G16R16},
    {"A2R10G10B10", D3DFMT_A2R10G10B10}, {"A16B16G16R16", D3DFMT_A16B16G16R16},
    {"A8P8", D3DFMT_A8P8}, {"P8", D3DFMT_P8},
    {"L8", D3DFMT_L8}, {"A8L8", D3DFMT_A8L8},
    {"A4L4", D3DFMT_A4L4}, {"V8U8", D3DFMT_V8U8},
    {"L6V5U5", D3DFMT_L6V5U5}, {"X8L8V8U8", D3DFMT_X8L8V8U8},
    {"Q8W8V8U8", D3DFMT_Q8W8V8U8}, {"V16U16", D3DFMT_V16U16},
    {"A2W10V10U10", D3DFMT_A2W10V10U10}, {"UYVY", D3DFMT_UYVY},
    {"R8G8_B8G8", D3DFMT_R8G8_B8G8}, {"YUY2", D3DFMT_YUY2},
    {"G8R8_G8B8", D3DFMT_G8R8_G8B8}, {"DXT1", D3DFMT_DXT1},
    {"DXT2", D3DFMT_DXT2}, {"DXT3", D3DFMT_DXT3},
    {"DXT4", D3DFMT_DXT4}, {"DXT5", D3DFMT_DXT5},
    {"D16_LOCKABLE", D3DFMT_D16_LOCKABLE}, {"D32", D3DFMT_D32},
    {"D15S1", D3DFMT_D15S1}, {"D24S8", D3DFMT_D24S8},
    {"D24X8", D3DFMT_D24X8}, {"D24X4S4", D3DFMT_D24X4S4},
    {"D16", D3DFMT_D16}, {"D32F_LOCKABLE", D3DFMT_D32F_LOCKABLE},
    {"D24FS8", D3DFMT_D24FS8}, {"L16", D3DFMT_L16},
    {"VERTEXDATA", D3DFMT_VERTEXDATA}, {"INDEX16", D3DFMT_INDEX16},
    {"INDEX32", D3DFMT_INDEX32}, {"Q16W16V16U16", D3DFMT_Q16W16V16U16},
    {"MULTI2_ARGB8", D3DFMT_MULTI2_ARGB8}, {"R16F", D3DFMT_R16F},
    {"G16R16F", D3DFMT_G16R16F}, {"A16B16G16R16F", D3DFMT_A16B16G16R16F},
    {"R32F", D3DFMT_R32F}, {"G32R32F", D3DFMT_G32R32F},
    {"A32B32G32R32F", D3DFMT_A32B32G32R32F}, {"CxV8U8", D3DFMT_CxV8U8}
};

// [sub_180011960 LABEL_224..] the original strips the prefix with
// strnicmp("FMT_", 4) first, then strnicmp("D3DFMT_", 7), and matches the
// remainder against the table with stricmp (case-insensitive). The input
// here is already lowercased.
static bool SasParseFormatName(const std::string& lower, D3DFORMAT* out) {
    std::string s = lower;
    if (s.compare(0, 4, "fmt_") == 0) {
        s = s.substr(4);
    } else if (s.compare(0, 7, "d3dfmt_") == 0) {
        s = s.substr(7);
    }
    for (size_t i = 0; i < sizeof(kFormatTable) / sizeof(kFormatTable[0]); ++i) {
        if (s == ToLowerAscii(kFormatTable[i].name)) {
            *out = kFormatTable[i].format;
            return true;
        }
    }
    return false;
}

// sub_180011920: format -> display name for the "%dx%d(%s) -> %dx%d(%s)"
// adjustment warning; values outside the table render as "UNKNOWN" (the
// reverse-lookup's miss string, same sentinel as the name-side "UNKNOWN"
// row at index 0 - a driver-private FourCC shows as UNKNOWN, not %08X).
static const char* SasFormatDisplayName(D3DFORMAT fmt, char* /*buf*/, size_t /*bufSize*/) {
    for (size_t i = 0; i < sizeof(kFormatTable) / sizeof(kFormatTable[0]); ++i) {
        if (kFormatTable[i].format == fmt) {
            return kFormatTable[i].name;
        }
    }
    return "UNKNOWN";
}

// ===========================================================================
// FUN_180011960 - build a resource object from a parameter's annotations
// ===========================================================================

// [L15457-15531] semantic-specific texture-type restrictions:
//   0x26 RenderColorTarget: rejects TEXTURE3D (8)
//   0x27 RenderDepthStencilTarget: rejects TEXTURE3D (8) / TEXTURECUBE (9)
//   0x2E OffScreenRenderTarget: rejects TEXTURE3D (8) / TEXTURECUBE (9)
static bool SasIsTextureTypeAllowed(int semanticId, int textureType) {
    if (semanticId == 0x26 && textureType == 8) {
        return false;
    }
    if ((semanticId == 0x27 || semanticId == 0x2E) &&
        (textureType == 8 || textureType == 9)) {
        return false;
    }
    return true;
}

// The FUN_180011960 record build. semanticId: 0x26/0x27/0x2C/0x2E (the
// semantics sub_18000F3A0 routes into sub_180011960) plus the two INLINE
// semantics 0x2D (ANIMATEDTEXTURE) and 0x33 (TEXTUREVALUE), which the
// original handles entirely inside sub_18000F3A0 cases 45/51 - the port
// services them in the exclusive branches near the top of this function
// and returns before the shared record machinery. Returns 0 = resource
// registered, 1 = hard error (FUN_180011960 returns 1), 2 = skipped
// without an error flag (the 0x2C-without-ResourceName path,
// FUN_180011960 returns 0 and creates nothing). UNCERTAIN: the original
// builds its record as a C++ object with boost::shared_ptr texture
// members; the std container here is a documented divergence.
static int SasBuildResourceObject(SasEffect* sas, int semanticId,
                                  D3DXHANDLE param) {
    ID3DXEffect* effect = sas->effect;
    D3DXPARAMETER_DESC pd;
    if (effect->GetParameterDesc(param, &pd) != S_OK || pd.Name == nullptr) {
        return 1;
    }

    // [sub_180011960 prologue] `if (!desc.Annotations && (desc.Flags & 1))
    // return 0` - a LITERAL parameter (desc.Flags bit 0) that carries NO
    // annotations at all returns immediately: no resource object is built,
    // nothing is registered and no warning is logged. Every 0x26/0x27/0x2C/
    // 0x2E parameter reaching this function takes this early exit.
    if (pd.Annotations == 0 && (pd.Flags & 1) != 0) {
        return 2;  // = the original's plain 0: skipped, no error flag
    }

    SasResource res;
    res.param = param;
    res.name = pd.Name;
    res.semanticId = semanticId;
    res.textureType = pd.Type;  // [L15253] param type; may be overridden below
    if (pd.Type == D3DXPT_TEXTURE) {
        res.textureType = 5;
    } else if (pd.Type == D3DXPT_TEXTURE2D) {
        res.textureType = 7;
    } else if (pd.Type == D3DXPT_TEXTURE3D) {
        res.textureType = 8;
    } else if (pd.Type == D3DXPT_TEXTURECUBE) {
        res.textureType = 9;
    }

    // =========================================================================
    // [sub_18000F3A0 case 45, 0x1800100C5] ANIMATEDTEXTURE. 0x2D NEVER
    // reaches sub_180011960 in the original (the semantic dispatch handles
    // it fully inline), so it must NOT consume the ResourceType/Dimensions/
    // ViewportRatio/Miplevels/Format machinery below - only ResourceName /
    // Offset / Speed / SeekVariable are read. The CAnimeGIF / CAnimePNG
    // object is constructed AT PARSE TIME so its error string can reject
    // the effect load, then registered into the ctx+0x48 set (the original
    // stores {param, param, {0x2D,count}, obj, seekParam} through
    // sub_18001F090; the anime module owns the object from here on).
    // Gates: `annotation absent && desc.Flags bit 0 (literal)` skips
    // silently (0x1800100DC); sas+0x3A (a load-mode flag the port never
    // sets - see the notes in anime_texture.h) skips everything.
    // =========================================================================
    if (semanticId == 0x2D) {
        if (SasFindAnnotation(effect, param, "ResourceName") == nullptr &&
            (pd.Flags & 1) != 0) {
            return 2;   // literal parameter without the annotation: skip
        }
        std::string resourceName;
        if (!SasGetAnnotationString(effect, param, "ResourceName", 1,
                                    &resourceName, sas)) {
            return 1;   // mode-1 line logged + hasErrors by the getter
        }
        // [sub_18000EA80 via 0x1800100D2..] the shared resolver: fullpath
        // against the loader CWD (the effect directory) + openability check.
        // Failure logs "Error: failed to open file: %s (parameter: %s)\n"
        // and the empty result is a silent hard error (no second line).
        std::string resolved;
        {
            char absolute[MAX_PATH] = { 0 };
            if (_fullpath(absolute, resourceName.c_str(),
                          sizeof(absolute)) != nullptr &&
                GetFileAttributesA(absolute) != INVALID_FILE_ATTRIBUTES) {
                resolved = absolute;
            } else {
                SasLogFormat(sas,
                             "Error: failed to open file: %s (parameter: %s)\n",
                             resourceName.c_str(), pd.Name);
                sas->hasErrors = true;
                return 1;
            }
        }
        // [0x180010119 / 0x1800B4048] length < 4 or an extension other than
        // ".png"/".gif" (lowercased last 4 chars of the RAW annotation):
        // "Error: unsupported file type: %s (parameter: '%s')\n".
        std::string ext;
        if (resourceName.size() >= 4) {
            ext = ToLowerAscii(resourceName.substr(resourceName.size() - 4));
        }
        if (resourceName.size() < 4 || (ext != ".png" && ext != ".gif")) {
            SasLogFormat(sas,
                         "Error: unsupported file type: %s (parameter: '%s')\n",
                         resourceName.c_str(), pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        // [0x1800B4008] "Loading texture file: " (the original writes the
        // notice to the log-window sink; the port funnels it through the
        // effect log like the 0x2C load notice - documented divergence).
        SasLogFormat(sas, "Loading texture file: %s\n", resolved.c_str());
        std::string ctorError;
        void* animeObject =
            (ext == ".png") ? MmeAnimeConstructPng(sas->device,
                                                   resolved.c_str(), &ctorError)
                            : MmeAnimeConstructGif(sas->device,
                                                   resolved.c_str(), &ctorError);
        if (animeObject == nullptr || !ctorError.empty()) {
            // [sub_180023440] the ctor's error string is appended verbatim
            // ("failed to open '<path>'\n" family) and the load FAILS.
            if (!ctorError.empty()) {
                SasLogFormat(sas, "%s", ctorError.c_str());
            } else {
                SasLogFormat(sas, "failed to load '%s'\n", resolved.c_str());
            }
            if (animeObject != nullptr) {
                MmeAnimeDestroyObject(animeObject);
            }
            sas->hasErrors = true;
            return 1;
        }
        // [0x1800B4068 + 0x1800B4050/0x1800B4080 + 0x1800B4058] Offset /
        // Speed: a present-but-unreadable annotation is a hard error
        // ("Error: failed to get 'Offset' (parameter: 'X')\n").
        double animeOffset = 0.0;
        double animeSpeed = 1.0;
        {
            D3DXHANDLE oAnn = SasFindAnnotation(effect, param, "Offset");
            if (oAnn != nullptr) {
                float value = 0.0f;
                if (effect->GetFloat(oAnn, &value) != S_OK) {
                    SasLogFormat(sas,
                                 "Error: failed to get 'Offset' (parameter: '%s')\n",
                                 pd.Name);
                    sas->hasErrors = true;
                    MmeAnimeDestroyObject(animeObject);
                    return 1;
                }
                animeOffset = value;
            }
            D3DXHANDLE sAnn = SasFindAnnotation(effect, param, "Speed");
            if (sAnn != nullptr) {
                float value = 0.0f;
                if (effect->GetFloat(sAnn, &value) != S_OK) {
                    SasLogFormat(sas,
                                 "Error: failed to get 'Speed' (parameter: '%s')\n",
                                 pd.Name);
                    sas->hasErrors = true;
                    MmeAnimeDestroyObject(animeObject);
                    return 1;
                }
                animeSpeed = value;
            }
        }
        // [0x1800B4088 + 0x1800B40A0/0x1800B40C8 + 0x1800B4098] SeekVariable:
        // parse-time GetParameterByName; the named parameter must exist and
        // be BOOL/INT/FLOAT (the original accepts 1/2/3 while the message
        // says "must be 'float'").
        D3DXHANDLE seekParam = 0;
        std::string seekName;
        if (SasGetAnnotationString(effect, param, "SeekVariable", 0,
                                   &seekName, sas)) {
            seekParam = effect->GetParameterByName(nullptr, seekName.c_str());
            if (seekParam == nullptr) {
                SasLogFormat(sas,
                             "Error: unknown parameter name: %s (parameter: '%s')\n",
                             seekName.c_str(), pd.Name);
                sas->hasErrors = true;
                MmeAnimeDestroyObject(animeObject);
                return 1;
            }
            D3DXPARAMETER_DESC sd;
            if (effect->GetParameterDesc(seekParam, &sd) != S_OK ||
                (sd.Type != D3DXPT_BOOL && sd.Type != D3DXPT_INT &&
                 sd.Type != D3DXPT_FLOAT)) {
                SasLogFormat(sas,
                             "Error: type of SeekVariable must be 'float': %s "
                             "(parameter: '%s')\n",
                             seekName.c_str(), pd.Name);
                sas->hasErrors = true;
                MmeAnimeDestroyObject(animeObject);
                return 1;
            }
        }
        res.resourceName = resolved;
        res.offset = static_cast<float>(animeOffset);
        res.speed = static_cast<float>(animeSpeed);
        res.seekVariable = seekName;
        res.seekParam = seekParam;
        // [sub_18001F090] the parse-time registration into the ctx+0x48 set
        // (the object ownership passes to the anime module).
        MmeAnimeRegisterParsed(g_context, animeObject, effect, param,
                               sas->path.c_str(), animeOffset, animeSpeed,
                               seekParam);
        sas->resources.push_back(res);
        return 0;
    }

    // =========================================================================
    // [sub_18000F3A0 case 51, 0x1800114xx] TEXTUREVALUE. Also fully inline:
    // TextureName (mode 1 after the literal skip) must name a parameter of
    // type TEXTURE(5)/TEXTURE2D(7) in this effect; the record only carries
    // {target texture param, the TextureValue parameter's Elements count}.
    // No D3D texture is created for the 0x33 record - the per-frame consumer
    // (sub_18001B0A0 case 51 -> sub_180063AA0) reads the CURRENT texture of
    // the named parameter back into a float4 array and SetVectorArray's it.
    // =========================================================================
    if (semanticId == 0x33) {
        if (SasFindAnnotation(effect, param, "TextureName") == nullptr &&
            (pd.Flags & 1) != 0) {
            return 2;   // literal parameter without the annotation: skip
        }
        std::string textureName;
        if (!SasGetAnnotationString(effect, param, "TextureName", 1,
                                    &textureName, sas)) {
            return 1;   // mode-1 line logged + hasErrors by the getter
        }
        // [0x1800B4150 + 0x1800B4098] "Error: unknown texture name: %s
        // (parameter: '%s')\n" when GetParameterByName misses.
        D3DXHANDLE texParam =
            effect->GetParameterByName(nullptr, textureName.c_str());
        if (texParam == nullptr) {
            SasLogFormat(sas,
                         "Error: unknown texture name: %s (parameter: '%s')\n",
                         textureName.c_str(), pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        // [0x1800B4178 + 0x1800B4098] "Error: invalid texture type: %s
        // (parameter: '%s')\n" unless the target is TEXTURE/TEXTURE2D.
        D3DXPARAMETER_DESC td;
        if (effect->GetParameterDesc(texParam, &td) != S_OK ||
            (td.Type != D3DXPT_TEXTURE && td.Type != D3DXPT_TEXTURE2D)) {
            SasLogFormat(sas,
                         "Error: invalid texture type: %s (parameter: '%s')\n",
                         textureName.c_str(), pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        res.textureValueName = textureName;
        res.textureValueParam = texParam;
        res.textureValueElements =
            static_cast<int>(pd.Elements);   // [f3a0 v152 = desc.Elements]
        sas->resources.push_back(res);
        return 0;
    }

    // [L15251-15452] "ResourceType" annotation ("2d"/"3d"/"cube", lowercased)
    // overrides the declared type and must not conflict with it.
    std::string resourceType;
    if (SasGetAnnotationString(effect, param, "ResourceType", 0, &resourceType, sas)) {
        const std::string lower = ToLowerAscii(resourceType);
        int resolved = res.textureType;
        if (lower == "2d") {
            resolved = 7;
        } else if (lower == "3d") {
            resolved = 8;
        } else if (lower == "cube") {
            resolved = 9;
        } else {
            SasLogFormat(sas,
                         "Error: value of annotation 'ResourceType' is invalid: %s  "
                         "(parameter: %s)\n",
                         resourceType.c_str(), pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        if (res.textureType != 5 && res.textureType != resolved) {
            SasLogFormat(sas,
                         "Error: annotation 'ResourceType' conflicts with type of "
                         "parameter.  (parameter: %s)\n",
                         pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        res.textureType = resolved;
    }

    // [L15457+] per-semantic texture-type checks.
    if (!SasIsTextureTypeAllowed(semanticId, res.textureType)) {
        SasLogFormat(sas, "Error: invalid texture type.  (parameter: %s)\n", pd.Name);
        sas->hasErrors = true;
        return 1;
    }

    // [strings 0x1800B4248..0x1800B4280] dimension annotations are mutually
    // exclusive: ViewportRatio vs Dimensions vs Width/Height(/Depth).
    // [sub_180011960 0x1800122xx-0x1800123d8] the original looks up all five
    // annotation handles FIRST and raises the exclusion error (prefix
    // 0x1800B4280 closed by asc_1800B3B98 "')\n'") BEFORE reading any value.
    // The raw triple keeps the original's -1 = "unset" encoding
    // [sub_180011960 0x1800123bb-0x1800123e0]: every component starts at -1
    // (= D3DX_DEFAULT downstream) and negative annotation values normalize
    // back to -1 [0x1800125eb-0x1800125fc / 0x1800127c5-0x1800127d0].
    // A value READ failure is "Error: failed to get 'X' (parameter: 'Y')"
    // [prefix 0x1800B4068 + "' (parameter: '" 0x1800B4058 + "')\n"], NOT the
    // "value of annotation" style - that prefix (0x1800B41A0) exists only on
    // the ResourceType branch.
    static const char* const kWhdNames[3] = {"Width", "Height", "Depth"};
    D3DXHANDLE vrAnn = SasFindAnnotation(effect, param, "ViewportRatio");
    D3DXHANDLE dimAnn = SasFindAnnotation(effect, param, "Dimensions");
    D3DXHANDLE whdAnn[3] = {nullptr, nullptr, nullptr};
    bool hasWhdAnn[3] = {false, false, false};
    for (int i = 0; i < 3; ++i) {
        whdAnn[i] = SasFindAnnotation(effect, param, kWhdNames[i]);
        hasWhdAnn[i] = (whdAnn[i] != nullptr);
    }
    int groupCount = (vrAnn != nullptr ? 1 : 0) + (dimAnn != nullptr ? 1 : 0) +
                     ((hasWhdAnn[0] || hasWhdAnn[1] || hasWhdAnn[2]) ? 1 : 0);
    if (groupCount > 1) {
        SasLogFormat(sas,
                     "Error: annotations 'ViewportRatio', 'Dimensions' and "
                     "('Width','Height','Depth') are mutually exclusive.  "
                     "(parameter: '%s')\n",
                     pd.Name);
        sas->hasErrors = true;
        return 1;
    }

    float viewportRatio[2] = {1.0f, 1.0f};
    bool hasViewportRatio = false;
    int dimensions[3] = {-1, -1, -1};
    bool hasDimensions = false;
    int width = -1, height = -1, depth = -1;
    bool hasWHD = false;

    if (vrAnn != nullptr) {
        // [0x1800123f8-0x180012419] GetVectorArray(handle, &ratio, 2); a
        // failed read is the hard error below and returns the HRESULT.
        D3DXVECTOR4 vec(0.0f, 0.0f, 0.0f, 0.0f);
        if (effect->GetVector(vrAnn, &vec) != S_OK) {
            // [0x18001241F] "Error: failed to get 'ViewportRatio' ..."
            SasLogFormat(sas,
                         "Error: failed to get 'ViewportRatio' (parameter: '%s')\n",
                         pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        // ViewportRatio is float2; the first two components carry x/y.
        viewportRatio[0] = vec.x;
        viewportRatio[1] = vec.y;
        hasViewportRatio = true;
    } else if (dimAnn != nullptr) {
        // [0x18001258c-0x1800125af] the annotation's own descriptor must
        // report Columns in 1..3 (an int array of one to three elements);
        // anything else is a hard error. [0x1800125c8] a failed GetIntArray
        // is a hard error too - both share the "failed to get" line.
        D3DXPARAMETER_DESC ad;
        if (effect->GetParameterDesc(dimAnn, &ad) != S_OK || ad.Columns < 1 ||
            ad.Columns > 3) {
            SasLogFormat(sas,
                         "Error: failed to get 'Dimensions' (parameter: '%s')\n",
                         pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        int d[3] = {-1, -1, -1};
        if (effect->GetIntArray(dimAnn, d, 3) != S_OK) {
            SasLogFormat(sas,
                         "Error: failed to get 'Dimensions' (parameter: '%s')\n",
                         pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        for (int i = 0; i < 3; ++i) {
            dimensions[i] = (d[i] >= 0) ? d[i] : -1;
        }
        hasDimensions = true;
    } else {
        // [0x18001277e-0x180012810] Width/Height/Depth are read one by one;
        // a present-but-unreadable annotation is a hard error naming the
        // annotation that failed, a negative value normalizes to -1.
        int* const kWhdOut[3] = {&width, &height, &depth};
        for (int i = 0; i < 3; ++i) {
            if (whdAnn[i] == nullptr) {
                continue;
            }
            hasWHD = true;
            int v = 0;
            if (effect->GetInt(whdAnn[i], &v) != S_OK) {
                // [0x1800127E8] "Error: failed to get 'Width'/..."
                SasLogFormat(sas,
                             "Error: failed to get '%s' (parameter: '%s')\n",
                             kWhdNames[i], pd.Name);
                sas->hasErrors = true;
                return 1;
            }
            *kWhdOut[i] = (v >= 0) ? v : -1;
        }
    }

    // [0x18001293C-0x1800129A7] render-target dimension defaults, the
    // original's post-scan logic (only 0x26/0x27/0x2E reach it). The merged
    // raw triple is the Dimensions array XOR the Width/Height/Depth
    // annotations (mutually exclusive; [0x1800125E0/0x1800127CD] negatives
    // normalize back to the -1 sentinel). A CUBE resource whose raw Width is
    // still -1 (and no ViewportRatio) defaults the cube edge to 256 -
    // immediate 0x100 at 0x180012998, NOT the screen size; every other type
    // with BOTH width and height unset synthesizes ViewportRatio {1,1}
    // (= the screen size, 0x180012975-0x18001298C).
    if (semanticId == 0x26 || semanticId == 0x27 || semanticId == 0x2E) {
        int rawW = hasDimensions ? dimensions[0] : width;
        int rawH = hasDimensions ? dimensions[1] : height;
        if (res.textureType == 9) {
            if (rawW == -1 && !hasViewportRatio) {
                if (hasDimensions) {
                    dimensions[0] = 256;
                } else {
                    // No usable Width at all: route the resolution below (and
                    // the raw texWidth storage) through the 256 edge.
                    width = 256;
                    hasWHD = true;
                }
            }
        } else if (rawW == -1 && rawH == -1 && !hasViewportRatio) {
            hasViewportRatio = true;
            viewportRatio[0] = 1.0f;
            viewportRatio[1] = 1.0f;
        }
    }

    // Resolve the requested pixel size. ViewportRatio scales the screen size
    // (REFERENCE.txt: default {1.0,1.0}). The original reads the host's
    // screen-size globals (= the swap-chain back buffer); the port reads the
    // back buffer directly. Sizing from the VIEWPORT instead breaks the
    // MRT invariant at the final composite: the script restores slot 0 to the
    // back buffer while slots 1-3 still point at the (now smaller) offscreen
    // targets - the size mismatch makes the driver silently drop ALL output
    // merges into the back buffer (the frozen-screen bug).
    int screenW = 64, screenH = 64;
    if (sas->device != nullptr) {
        IDirect3DSurface9* backBuffer = nullptr;
        if (SUCCEEDED(sas->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                                 &backBuffer)) &&
            backBuffer != nullptr) {
            D3DSURFACE_DESC bbDesc;
            memset(&bbDesc, 0, sizeof(bbDesc));
            if (SUCCEEDED(backBuffer->GetDesc(&bbDesc)) && bbDesc.Width > 0 &&
                bbDesc.Height > 0) {
                screenW = static_cast<int>(bbDesc.Width);
                screenH = static_cast<int>(bbDesc.Height);
            }
            backBuffer->Release();
        }
    }
    if (hasViewportRatio) {
        res.reqWidth = static_cast<int>(screenW * viewportRatio[0]);
        res.reqHeight = static_cast<int>(screenH * viewportRatio[1]);
        res.reqDepth = 1;
    } else if (hasDimensions) {
        res.reqWidth = dimensions[0];
        res.reqHeight = dimensions[1];
        res.reqDepth = dimensions[2] > 0 ? dimensions[2] : 1;
    } else if (hasWHD) {
        res.reqWidth = width;
        res.reqHeight = height;
        res.reqDepth = depth > 0 ? depth : 1;
    } else {
        res.reqWidth = screenW;
        res.reqHeight = screenH;
        res.reqDepth = 1;
    }
    if (res.reqWidth <= 0) {
        res.reqWidth = screenW;
    }
    if (res.reqHeight <= 0) {
        res.reqHeight = screenH;
    }

    // Raw annotation storage for the file-texture load path
    // (sub_1800143D0 0x18001518a-0x180015182 reads record +0x58/+0x5C).
    // [sub_1800143D0 prologue] when the record's hasViewportRatio byte
    // (+0x64) is set, the original computes W/H as screen size * ratio
    // BEFORE the switch - a file texture (or Function-generated texture)
    // with ViewportRatio is therefore loaded SCALED to screen*ratio, not at
    // its native/-1 size. A negative ratio component keeps the -1 sentinel
    // (= D3DX_DEFAULT). This runs after the 0x26/0x27/0x2E default synthesis
    // above, so a synthesized {1,1} ratio (no dimension annotations) yields
    // the full screen size exactly like the original's +0x64-scaled records.
    if (hasDimensions) {
        res.texWidth = dimensions[0];
        res.texHeight = dimensions[1];
    } else if (hasWHD) {
        res.texWidth = width;
        res.texHeight = height;
    } else if (hasViewportRatio) {
        res.texWidth = (viewportRatio[0] < 0.0f)
                           ? -1
                           : static_cast<int>(static_cast<float>(screenW) *
                                              viewportRatio[0]);
        res.texHeight = (viewportRatio[1] < 0.0f)
                            ? -1
                            : static_cast<int>(static_cast<float>(screenH) *
                                               viewportRatio[1]);
    }

    // [0x1800B42F8/0x1800B4304] Miplevels/Levels: [0x1800129bf-0x180012b77]
    // a present-but-unreadable annotation is a hard error and a negative
    // value keeps the -1 default ([0x1800123bb] the sentinel is -1; negative
    // values fall through the cmovns at 0x180012b77). When the annotation is
    // ABSENT the raw storage defaults to 1 for the render-target semantics
    // 0x26/0x27/0x2E [0x1800129e1-0x1800129f7] and stays -1 (= D3DX_DEFAULT)
    // for everything else (the file-texture path). The raw value (record
    // +0x6C) feeds the file-texture ExA call, the 0x2C generation dispatch
    // AND the 0x27 probe's MipLevels alike; res.mipLevels (resolved, 1
    // floor) stays as the header's resolved copy.
    {
        D3DXHANDLE mann = SasFindAnnotation(effect, param, "Miplevels");
        if (mann == nullptr) {
            mann = SasFindAnnotation(effect, param, "Levels");
        }
        int mipRaw = -1;
        if (mann != nullptr) {
            int m = 0;
            if (effect->GetInt(mann, &m) != S_OK) {
                // [0x180012A22] "Error: failed to get 'Miplevels' ..." - the
                // line always says "Miplevels", even when the annotation was
                // spelled "Levels".
                SasLogFormat(sas,
                             "Error: failed to get 'Miplevels' (parameter: '%s')\n",
                             pd.Name);
                sas->hasErrors = true;
                return 1;
            }
            if (m >= 0) {
                mipRaw = m;
            }
        } else if (semanticId == 0x26 || semanticId == 0x27 ||
                   semanticId == 0x2E) {
            mipRaw = 1;
        }
        res.texMips = mipRaw;
        res.mipLevels = (mipRaw >= 0) ? mipRaw : 1;
    }

    // [0x1800B430C] "Format" annotation: "FMT_" (4) / "D3DFMT_" (7) prefixes
    // are stripped case-insensitively from the RAW annotation text, then the
    // remainder matches the 60-entry table above with stricmp
    // [sub_180011960 LABEL_224..LABEL_235]. An EXPLICIT "UNKNOWN" (table
    // entry 0 -> D3DFMT_UNKNOWN) is stored verbatim - only an ABSENT
    // annotation takes the per-semantic default below, so Format="UNKNOWN"
    // genuinely requests D3DFMT_UNKNOWN (and fails at creation time).
    std::string formatName;
    if (SasGetAnnotationString(effect, param, "Format", 0, &formatName, sas)) {
        std::string lower = ToLowerAscii(formatName);
        D3DFORMAT fmt = D3DFMT_UNKNOWN;
        if (!SasParseFormatName(lower, &fmt)) {
            // [0x1800B4338 + 0x1800B4328 + asc_1800B3B98] the unknown-format
            // error prints the PREFIX-STRIPPED annotation text (the original
            // reassigns the string to substr(4)/substr(7) before matching)
            // and closes with "  (parameter: 'NAME')\n".
            const char* stripped = formatName.c_str();
            if (_strnicmp(stripped, "FMT_", 4) == 0) {
                stripped += 4;
            } else if (_strnicmp(stripped, "D3DFMT_", 7) == 0) {
                stripped += 7;
            }
            SasLogFormat(sas,
                         "Error: unknown texture format: %s  (parameter: '%s')\n",
                         stripped, pd.Name);
            sas->hasErrors = true;
            return 1;
        }
        res.format = fmt;
    } else {
        // [0x180012DF2-0x180012E53] no Format annotation: 0x27 takes
        // the CURRENT depth-stencil surface's format (device vtbl+0x140
        // GetDepthStencilSurface -> vtbl+0x60 GetDesc -> Format; fallback
        // 75 = D3DFMT_D24S8 when the query yields no surface). Everything
        // else defaults to 21 = D3DFMT_A8R8G8B8.
        res.format = D3DFMT_A8R8G8B8;
        if (semanticId == 0x27) {
            if (sas->device != nullptr) {
                IDirect3DSurface9* ds = nullptr;
                if (SUCCEEDED(sas->device->GetDepthStencilSurface(&ds)) &&
                    ds != nullptr) {
                    D3DSURFACE_DESC dd;
                    memset(&dd, 0, sizeof(dd));
                    if (SUCCEEDED(ds->GetDesc(&dd))) {
                        res.format = dd.Format;
                    }
                    ds->Release();
                } else {
                    res.format = D3DFMT_D24S8;
                }
            } else {
                res.format = D3DFMT_D24S8;
            }
        }
    }

    // [0x1800B4430/0x1800B4440/0x1800B4450, FUN_180011960 0x2E record build]
    // ClearColor / ClearDepth / AntiAlias live INSIDE the offscreen-only
    // block (after Description/DefaultEffect and the Info log): non-0x2E
    // semantics never read them. Each annotation is a HARD error when its
    // type cannot be read ("' is invalid. (parameter: ...)"); the original's
    // AntiAlias failure path reuses the ClearColor message text.

    // [0x1800B43D0/0x1800B43E8] Description / DefaultEffect (offscreen only).
    if (semanticId == 0x2E) {
        SasGetAnnotationString(effect, param, "Description", 0, &res.description, sas);
        if (SasGetAnnotationString(effect, param, "DefaultEffect", 0,
                                   &res.defaultEffect, sas)) {
            // [sub_180011960 0x180013301-0x1800135F4] parse the annotation
            // into the ordered (key -> value) rows stored at offscreen record
            // +0x78. Split on ';' (sub_1800116A0, memchr 0x3B with an
            // unconditional tail push); a blank token is skipped silently
            // (0x180013646), but any other token without '=' - or with an
            // empty trimmed key - fails the load with the syntax-error line
            // below (0x18001372C). The key/value are trim()ed
            // (sub_180011800, " \t\v\r\n"); the value is compared
            // case-insensitively: empty -> "none" (0x1800135AC), else the
            // hide/none/main_default keywords canonicalize (0x1800134B8/
            // 0x1800135B3/0x180013531), anything else resolves to the
            // absolute path of the referenced .fx against the effect
            // directory (sub_18000EA80; the parse runs inside the loader's
            // ScopedChdir so _fullpath matches the original's chdir+fullpath
            // pair). A path that cannot be opened fails the whole load - the
            // original destroys the offscreen record and returns failure
            // (0x180013652) after the resolver logged the line below.
            size_t cursor = 0;
            while (cursor <= res.defaultEffect.size()) {
                size_t semi = res.defaultEffect.find(';', cursor);
                size_t end = (semi == std::string::npos)
                                 ? res.defaultEffect.size()
                                 : semi;
                std::string token = res.defaultEffect.substr(cursor, end - cursor);
                cursor = (semi == std::string::npos)
                             ? res.defaultEffect.size() + 1
                             : semi + 1;
                const size_t eq = token.find('=');
                if (eq == std::string::npos &&
                    SasTrimWhitespace(token).empty()) {
                    continue;   // blank token: skipped, no error
                }
                const std::string key = SasTrimWhitespace(token.substr(0, eq));
                if (eq == std::string::npos || key.empty()) {
                    SasLogFormat(sas,
                                 "Error: 'DefaultEffect' syntax error: %s  "
                                 "(parameter: %s)\n",
                                 token.c_str(), pd.Name);
                    sas->hasErrors = true;
                    return 1;
                }
                std::string value = SasTrimWhitespace(token.substr(eq + 1));
                const std::string lowered = ToLowerAscii(value);
                if (lowered.empty() || lowered == "none") {
                    value = "none";
                } else if (lowered == "hide") {
                    value = "hide";
                } else if (lowered == "main_default") {
                    value = "main_default";
                } else {
                    char absolute[MAX_PATH] = { 0 };
                    if (_fullpath(absolute, value.c_str(),
                                  sizeof(absolute)) == nullptr ||
                        GetFileAttributesA(absolute) ==
                            INVALID_FILE_ATTRIBUTES) {
                        SasLogFormat(sas,
                                     "Error: failed to open file: %s "
                                     "(parameter: %s)\n",
                                     value.c_str(), pd.Name);
                        sas->hasErrors = true;
                        return 1;
                    }
                    value = absolute;
                }
                res.defaultEffectMap.push_back(std::make_pair(key, value));
            }
        }
        // [0x1800B43A8] "Info: new OffScreen RenderTarget: "
        SasLogFormat(sas, "Info: new OffScreen RenderTarget: %s\n", pd.Name);
        D3DXHANDLE cc = SasFindAnnotation(effect, param, "ClearColor");
        if (cc != nullptr) {
            D3DXVECTOR4 vec(1.0f, 1.0f, 1.0f, 1.0f);  // seeded like the original
            if (effect->GetVector(cc, &vec) != S_OK) {
                SasLogFormat(sas,
                             "Error: annotation 'ClearColor' is invalid. (parameter: %s)\n",
                             pd.Name);
                sas->hasErrors = true;
                return 1;
            }
            // [FUN_180011960 clamp loop] each component < 0 -> 0.0f,
            // > 1.0f -> 1.0f, then one packed D3DCOLOR store at record +0x10:
            // A = (int)(w*255) << 24 | R = (u8)(x*255) << 16 |
            // G = (u8)(y*255) << 8 | B = (u8)(z*255).
            float f[4] = {vec.x, vec.y, vec.z, vec.w};
            for (int i = 0; i < 4; ++i) {
                if (f[i] < 0.0f) {
                    f[i] = 0.0f;
                } else if (f[i] > 1.0f) {
                    f[i] = 1.0f;
                }
            }
            res.clearColorArgb =
                (static_cast<unsigned long>(static_cast<int>(f[3] * 255.0f))
                 << 24) |
                (static_cast<unsigned long>(static_cast<unsigned char>(
                     static_cast<int>(f[0] * 255.0f))) << 16) |
                (static_cast<unsigned long>(static_cast<unsigned char>(
                     static_cast<int>(f[1] * 255.0f))) << 8) |
                static_cast<unsigned long>(static_cast<unsigned char>(
                    static_cast<int>(f[2] * 255.0f)));
            res.hasClearColor = true;
        }
        D3DXHANDLE cd = SasFindAnnotation(effect, param, "ClearDepth");
        if (cd != nullptr) {
            // GetVector with count 1 in the original - the single float lands
            // at record +0x14 verbatim (no clamping).
            float z = 0.0f;
            if (effect->GetFloat(cd, &z) != S_OK) {
                SasLogFormat(sas,
                             "Error: annotation 'ClearDepth' is invalid. (parameter: %s)\n",
                             pd.Name);
                sas->hasErrors = true;
                return 1;
            }
            res.clearDepth = z;
            res.hasClearDepth = true;
        }
        D3DXHANDLE aa = SasFindAnnotation(effect, param, "AntiAlias");
        if (aa != nullptr) {
            BOOL v = FALSE;
            if (effect->GetBool(aa, &v) != S_OK) {
                // [FUN_180011960] the original builds this failure line from
                // the ClearColor fragments ("ClearColor' is invalid. ...").
                SasLogFormat(sas,
                             "Error: annotation 'ClearColor' is invalid. (parameter: %s)\n",
                             pd.Name);
                sas->hasErrors = true;
                return 1;
            }
            res.antiAlias = (v != FALSE);
        }
    }

    // [0x1800B3FF8] ANIMATEDTEXTURE / 0x1800B4118 TEXTUREVALUE: handled in
    // the exclusive branches at the top of this function (sub_18000F3A0
    // cases 45/51 - neither semantic reaches this shared tail in the
    // original).
    // [LABEL_262, 0x180012E7A] ResourceName is read silently (mode 0) for
    // every semantic that reaches FUN_180011960 (0x26/0x27/0x2C/0x2E) and is
    // resolved IMMEDIATELY through sub_18000EA80: fullpath against the
    // effect directory (the loader's ScopedChdir keeps the CWD there, so a
    // plain _fullpath matches the original's chdir+fullpath pair) plus an
    // _access(path, 4) existence check (sub_1800820F0 = GetFileAttributesA).
    // A path that cannot be resolved/opened logs
    // "Error: failed to open file: %s (parameter: %s)\n"
    // [0x1800B3E60 + 0x1800B3E50 + asc_1800B3C30] and yields the EMPTY
    // string; the compare(0, size, "") == 0 check right after [0x180012E7A+]
    // then returns 1 with NO additional log line - a present-but-unusable
    // ResourceName is a silent hard error, not the "not available" warning
    // (that path only runs when the annotation is ABSENT and the record
    // string stays ""). The record keeps the RESOLVED absolute path and
    // sub_1800143D0 loads that.
    if (semanticId == 0x26 || semanticId == 0x27 || semanticId == 0x2C ||
        semanticId == 0x2E) {
        std::string resourceName;
        if (SasGetAnnotationString(effect, param, "ResourceName", 0,
                                   &resourceName, sas)) {
            char absolute[MAX_PATH] = { 0 };
            if (_fullpath(absolute, resourceName.c_str(),
                          sizeof(absolute)) != nullptr &&
                GetFileAttributesA(absolute) != INVALID_FILE_ATTRIBUTES) {
                res.resourceName = absolute;
            } else {
                SasLogFormat(sas,
                             "Error: failed to open file: %s (parameter: %s)\n",
                             resourceName.c_str(), pd.Name);
                res.resourceName.clear();
            }
            if (res.resourceName.empty()) {
                return 1;  // silent: LABEL_270 with no second message
            }
        }
    }
    // [0x1800B4358 "Function" / 0x1800B4364 "Target", reads at
    // 0x180012F43/0x180012F7D] both annotations are read silently (mode 0)
    // for EVERY resource semantic. "Function" (record +0x70) names the
    // texture-shader entry point that the 0x2C generation path compiles FROM
    // THE EFFECT'S OWN .fx FILE (pSrcFile = the loader FullPath at sas+0x48,
    // sub_1800143D0 call site 0x1800152A6 - NOT a separate shader file).
    // "Target" (record +0x98) is the compile profile and defaults to
    // "tx_1_0" when the annotation is absent [0x180012FAD-0x180012FC3].
    // Only the 0x2C availability check below and SasEnsureResourceTexture's
    // generation branch consume them.
    std::string function;
    SasGetAnnotationString(effect, param, "Function", 0, &function, sas);
    res.functionName = function;
    std::string target;
    if (!SasGetAnnotationString(effect, param, "Target", 0, &target, sas)) {
        target = "tx_1_0";
    }
    res.functionTarget = target;

    // [LABEL_278, 0x180012FE0-0x1800130B9] 0x2C with neither ResourceName
    // nor Function: log "Warning: texture '<name>' is not available.\n"
    // (skipped for literal parameters, desc.Flags bit 0), create NO object
    // and return without the error flag (FUN_180011960 returns 0).
    if (semanticId == 0x2C && res.resourceName.empty() && function.empty()) {
        if (!(pd.Flags & 1)) {
            SasLogFormat(sas, "Warning: texture '%s' is not available.\n", pd.Name);
        }
        return 2;
    }

    sas->resources.push_back(res);
    return 0;
}

// ===========================================================================
// FUN_18000f3a0 - semantic registration dispatch
// ===========================================================================

// Called for every parameter whose semantic/name matched a validation table
// entry and for every texture-typed parameter (id 0x2C). Returns nonzero on
// error (the c470 error flag). Non-resource semantics return 0: their runtime
// values are bound by material_bind (Phase 2), not by the SAS interpreter.
static int SasRegisterSemantic(SasEffect* sas, int semanticId, D3DXHANDLE param) {
    switch (semanticId) {
        case 0x26:  // RenderColorTarget
        case 0x27:  // RenderDepthStencilTarget
        case 0x2C:  // (pseudo-id) plain texture parameter
        case 0x2D:  // AnimatedTexture (original: inline in FUN_18000f3a0)
        case 0x2E:  // OffScreenRenderTarget
        case 0x33:  // TextureValue (original: inline in FUN_18000f3a0)
            // [sub_18000F3A0] only 0x26/0x27/0x2C/0x2E route into the
            // resource build (sub_180011960); status 2 (skipped, no error
            // flag) reports success to the caller like the original's
            // return 0.
            return (SasBuildResourceObject(sas, semanticId, param) == 1) ? 1 : 0;
        default:
            // [sub_18000F3A0 default -> LABEL_412] every other semantic -
            // including 0x29 MaterialTexture, 0x2A MaterialSphereMap and
            // 0x2B MaterialToonTexture - only registers a runtime value
            // (sub_18001F090) and never builds a texture resource; the
            // material texture itself is bound per material by
            // material_bind. No resource, no parse-time SetTexture.
            return 0;
    }
}

// ===========================================================================
// FUN_18000c470 - the parse core
// ===========================================================================

// Shared validation helper for one table entry vs a parameter desc. The
// decompile shapes (L12441-12506 for the semantic table, L12580-12627 for the
// name table) reduce to these rules:
//   - MATRIX_COLUMNS normalizes to MATRIX_ROWS
//   - matrix needs Rows==4, vector needs Rows==1
//   - class/type/count mismatches carry per-semantic exceptions (table A);
//     the name table (B) is strict - verified 2026-09: the B branch
//     (LABEL_458) is class/type/count + Rows shape only, no exception and
//     no Elements/Flags gate
//   - arrays (Elements != 0) are rejected except TextureValue (0x33) -
//     table A path only [0x18000e66c]
static bool SasValidateAgainstEntry(const SasSemanticEntry& e,
                                    const D3DXPARAMETER_DESC& pd, bool strict) {
    int cls = static_cast<int>(pd.Class);
    if (cls == 3) {
        cls = 2;
    }
    int type = static_cast<int>(pd.Type);
    int count = static_cast<int>(pd.Rows) * static_cast<int>(pd.Columns);

    // Shape check (matrix 4 rows, vector 1 row).
    if (cls == 2 && pd.Rows != 4) {
        return false;
    }
    if (cls == 1 && pd.Rows != 1) {
        return false;
    }

    // Class check with per-semantic exceptions (skipped when strict).
    if (e.cls != cls && !strict) {
        bool ok = false;
        if (e.id == 0x1E) {            // SpecularPower: scalar accepted
            ok = (cls == 0);
        } else if (e.id == 0x28) {     // ControlObject: scalar or matrix
            ok = (cls == 0 || cls == 2);
        } else if (e.cls == 0 && cls == 1) {  // 1-element vector as scalar
            ok = (count == 1);
        }
        if (!ok) {
            return false;
        }
    } else if (e.cls != cls) {
        return false;
    }

    // Type check: TEXTURE accepts TEXTURE2D; ControlObject accepts BOOL.
    if (e.type != type) {
        bool ok = (e.type == 5 && type == 7);
        if (!ok && !strict && e.id == 0x28 && type == 1 && pd.Columns == 1) {
            ok = true;
        }
        if (!ok) {
            return false;
        }
    }

    // Element-count check with per-semantic exceptions (skipped when strict).
    if (e.count != 255 && e.count != count) {
        bool ok = false;
        if (!strict) {
            unsigned int ucount = static_cast<unsigned int>(count);
            if (e.id == 0x1E) {
                ok = ((ucount - 1u) <= 3u) && count != 2;  // {1,3,4}
            } else if (e.id == 0x28) {
                ok = ((ucount - 1u) <= 3u) && count != 2 && count != 16;
            } else if (e.id == 0x18 || e.id == 0x19 || e.id == 0x1A ||
                       e.id == 0x1B || e.id == 0x1F || e.id == 0x20 ||
                       e.id == 0x1C || e.id == 0x1D) {
                ok = (e.count == 4 && count == 3);  // float3 for float4
            } else if (e.id == 0x30 || e.id == 0x31 || e.id == 0x32) {
                ok = true;  // mouse-button vectors accept any size
            }
        }
        if (!ok) {
            return false;
        }
    }

    // Arrays are invalid except TextureValue - TABLE A ONLY [0x18000e66c:
    // cmp [desc+0x20 = Elements],0 then id!=0x33 feeds the semantic-table
    // error flag]. The name-table B branch (LABEL_458, 0x18000e88d-0x18000e9e4)
    // has no Elements/Flags gate at all: class/type/count + Rows shape only.
    if (!strict && pd.Elements != 0 && e.id != 0x33) {
        return false;
    }
    return true;
}

// [FUN_18000c470 L12284-12306 + L19885-19917] script normalization:
// append ';', remove "\s+" (c470) / collapse "\s*(;\s*)+" -> ";" (18200),
// strip a leading ';'. This port uses the combined c470 behavior for the
// STANDARDSGLOBAL Script and the 18200 behavior for technique/pass scripts
// (implemented in sas_exec.cpp).
static std::string SasNormalizeStandardsGlobalScript(const std::string& in) {
    std::string s = in;
    s += ';';
    // regex_replace("\\s+", "", ...) [DAT_1800b3d20]
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (!isspace(static_cast<unsigned char>(s[i]))) {
            out += s[i];
        }
    }
    // regex_replace(";;+", ";", ...) [DAT_1800b3d28]
    std::string out2;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == ';') {
            out2 += ';';
            while (i + 1 < out.size() && out[i + 1] == ';') {
                ++i;
            }
        } else {
            out2 += out[i];
        }
    }
    // regex_replace("^;", "", ...) [DAT_1800b3d2c]
    if (!out2.empty() && out2[0] == ';') {
        out2 = out2.substr(1);
    }
    return out2;
}

// Tokenize with the original's "^((\\w+)=([^=;]*));(.*)" loop
// [DAT_1800b3d30] and append valid "Technique=" entries to techniqueOrder.
// [0x18000d415 -> loc_18000DF83-0x18000E187] returns true (FATAL - the
// whole c470 returns 1 at loc_18000DF7B and the loader rejects the effect)
// when the token loop stops on a NON-EMPTY remainder - i.e. even when some
// tokens already matched, any unparsable leftover rejects the effect. Only
// a fully consumed script continues to the parameter walk (LAB_18000E43A).
static bool SasParseStandardsGlobalScript(SasEffect* sas,
                                          const std::string& script,
                                          const char* paramName) {
    std::string rest = script;
    while (!rest.empty()) {
        // regex ^((\w+)=([^=;]*));(.*)
        size_t eq = rest.find('=');
        size_t semi = rest.find(';');
        if (eq == std::string::npos || semi == std::string::npos || eq > semi) {
            break;
        }
        std::string whole = rest.substr(0, semi);        // m[1] "cmd=value"
        std::string command = rest.substr(0, eq);        // m[2]
        std::string value = rest.substr(eq + 1, semi - eq - 1);  // m[3]
        std::string remainder = (semi + 1 <= rest.size()) ? rest.substr(semi + 1) : "";
        // \w+ command check (no spaces survived normalization).
        bool cmdOk = !command.empty();
        for (size_t i = 0; i < command.size(); ++i) {
            if (!isalnum(static_cast<unsigned char>(command[i])) && command[i] != '_') {
                cmdOk = false;
                break;
            }
        }
        if (!cmdOk) {
            break;
        }
        rest = remainder;
        if (ToLowerAscii(command) != "technique") {
            // [0x1800B3DB0] "Warning: unsupported script command: "
            SasLogFormat(sas,
                         "Warning: unsupported script command: %s  (parameter: %s)\n",
                         whole.c_str(), paramName);
            continue;
        }
        // Single form "Technique=MainTech" or chain "Technique=A?B:C".
        // [0x18000d77a] find('?') splits two DIFFERENT failure policies:
        //   - no '?': the whole value is ONE technique name. GetTechniqueByName
        //     returning NULL logs "Error: unknown technique name: %s  (parameter:
        //     %s)" [0x18000dd12-0x18000dd8d, string 0x1800B3D90] then goto
        //     LABEL_330 -> the c470 early return 1 at 0x18000ea41 - the loader
        //     rejects the whole effect file (FATAL).
        //   - 'A?B:C' chain: [LAB_18000da09] the list after '?' is ':' separated
        //     and tried in order; every name that resolves is pushed to
        //     techniqueOrder. An unknown name only logs "Warning: unknown
        //     technique name: ..." [0x18000d8aa-0x18000d925, string 0x1800B3D68]
        //     and the walk CONTINUES with the next name (non-fatal).
        const size_t qpos = value.find('?');
        if (qpos == std::string::npos) {
            if (!value.empty()) {
                D3DXHANDLE h = sas->effect->GetTechniqueByName(value.c_str());
                if (h != nullptr) {
                    sas->techniqueOrder.push_back(h);
                } else {
                    SasLogFormat(sas,
                                 "Error: unknown technique name: %s  (parameter: %s)\n",
                                 value.c_str(), paramName);
                    sas->hasErrors = true;
                    return true;   // FATAL: c470's LABEL_330 early return 1
                }
            }
            continue;
        }
        std::string list = value.substr(qpos + 1);
        while (!list.empty()) {
            size_t colon = list.find(':');
            std::string name = (colon == std::string::npos) ? list : list.substr(0, colon);
            if (!name.empty()) {
                D3DXHANDLE h = sas->effect->GetTechniqueByName(name.c_str());
                if (h != nullptr) {
                    sas->techniqueOrder.push_back(h);
                } else {
                    // [0x1800B3D68] "Warning: unknown technique name: " - the
                    // chain form keeps going with the next ':' segment.
                    SasLogFormat(sas, "Warning: unknown technique name: %s  (parameter: %s)\n",
                                 name.c_str(), paramName);
                }
            }
            if (colon == std::string::npos) {
                break;
            }
            list = list.substr(colon + 1);
        }
    }
    if (!rest.empty()) {
        // [LAB_18000e1a0 c470 L12684] "Error: script syntax error: " with the
        // unparsed remainder (the original erases up to the first ';').
        size_t semi = rest.find(';');
        std::string tail = (semi == std::string::npos) ? rest : rest.substr(0, semi);
        SasLogFormat(sas, "Error: script syntax error: %s  (parameter: %s)\n",
                     tail.c_str(), paramName);
        sas->hasErrors = true;
        return true;  // fatal: c470 returns 1, the effect is rejected
    }
    return false;
}

// The FUN_18000c470 parameter walk (LAB_18000e43a): for each top-level
// parameter, match Table A by semantic, else Table B by name, else route
// texture-typed parameters to the resource path (id 0x2C).
static void SasValidateParameters(SasEffect* sas) {
    ID3DXEffect* effect = sas->effect;
    D3DXEFFECT_DESC ed;
    memset(&ed, 0, sizeof(ed));
    if (effect->GetDesc(&ed) != S_OK) {
        SasLogLine(sas, "Error: cannot read the effect description.\n");
        sas->hasErrors = true;
        return;
    }
    for (UINT i = 0; i < ed.Parameters; ++i) {
        D3DXHANDLE param = effect->GetParameter(nullptr, i);
        if (param == nullptr) {
            continue;
        }
        D3DXPARAMETER_DESC pd;
        if (effect->GetParameterDesc(param, &pd) != S_OK) {
            continue;
        }
        const char* name = (pd.Name != nullptr) ? pd.Name : "(null)";
        const char* semantic = (pd.Semantic != nullptr) ? pd.Semantic : "";

        bool matched = false;
        bool invalid = false;
        int semanticId = 0;

        // Table A: semantic match (_stricmp) [L12434-12438].  The semantic
        // table carries the per-semantic exceptions (float3 for float4,
        // scalar/matrix ControlObject, ...) - SasValidateAgainstEntry runs
        // non-strict here, strict=false.
        for (size_t k = 0; k < sizeof(kSemanticTable) / sizeof(kSemanticTable[0]); ++k) {
            if (_stricmp(semantic, kSemanticTable[k].name) != 0) {
                continue;
            }
            // [0x18000e4e9-0x18000e52c] Class==D3DXPC_OBJECT with one of
            // DIFFUSE/AMBIENT/EMISSIVE/SPECULAR/SPECULARPOWER/POSITION/
            // DIRECTION skips the table A validation entirely (NO error)
            // and falls through to the name table B walk below.
            if (pd.Class == D3DXPC_OBJECT &&
                (kSemanticTable[k].id == 0x18 || kSemanticTable[k].id == 0x19 ||
                 kSemanticTable[k].id == 0x1A || kSemanticTable[k].id == 0x1B ||
                 kSemanticTable[k].id == 0x1E || kSemanticTable[k].id == 0x1F ||
                 kSemanticTable[k].id == 0x20)) {
                break;  // leave matched=false -> table B runs
            }
            matched = true;
            semanticId = kSemanticTable[k].id;
            if (!SasValidateAgainstEntry(kSemanticTable[k], pd, false)) {
                invalid = true;
                // [L12508-12515] "Error: type of parameter '%s' is invalid
                // (semantic: %s).\n"
                SasLogFormat(sas,
                             "Error: type of parameter '%s' is invalid (semantic: %s).\n",
                             name, kSemanticTable[k].name);
            } else if (SasRegisterSemantic(sas, semanticId, param) != 0) {
                invalid = true;
            }
            break;
        }

        // Table B: name match [L12559-12576]; strict, no exceptions
        // (strict=true - the decompile shows no exception branches here).
        // [0x18000e8ef-0x18000e901] a strict-validation failure is graded:
        // ids 0x3F-0x42 (use_texture/use_spheremap/use_subtexture/use_toon)
        // record "Error: type of parameter '%s' is invalid.\n" and the
        // error flag; EVERY other name-table entry fails SILENTLY and
        // drops to the generic texture-typed check below (loc_18000E9E6).
        if (!matched) {
            for (size_t k = 0; k < sizeof(kNameTable) / sizeof(kNameTable[0]); ++k) {
                if (_stricmp(name, kNameTable[k].name) != 0) {
                    continue;
                }
                if (SasValidateAgainstEntry(kNameTable[k], pd, true)) {
                    matched = true;
                    semanticId = kNameTable[k].id;
                    if (SasRegisterSemantic(sas, semanticId, param) != 0) {
                        invalid = true;
                    }
                } else if (kNameTable[k].id >= 0x3F && kNameTable[k].id <= 0x42) {
                    // [L12593-12596] "Error: type of parameter '%s' is
                    // invalid.\n" - the flag is set and the parameter is
                    // dropped (no texture fallback for these four).
                    invalid = true;
                    SasLogFormat(sas, "Error: type of parameter '%s' is invalid.\n", name);
                }
                // else: silent skip -> unmatched, the texture check below runs
                break;  // the name matched a table B entry; stop scanning
            }
        }

        // Unmatched texture-typed parameters -> generic resource (0x2C)
        // [L12629-12637].
        if (!matched) {
            int t = static_cast<int>(pd.Type);
            if (t == 5 || t == 7 || t == 8 || t == 9) {
                if (SasRegisterSemantic(sas, 0x2C, param) != 0) {
                    invalid = true;
                }
            }
        }

        // CONTROLOBJECT (semantic 0x28) [sub_18000F3A0 case 0x28]: collect
        // the parameter with its annotations - the per-frame resolver (the
        // original's EffectFrameParamSetter, sub_180057BC0) reads them every
        // frame. The annotation literals are "Name" [0x1800B40F8] and "Item"
        // [0x1800B4100] (matched case-insensitively). "Name" is mandatory:
        // read with mode 1 - when missing the original logs the error and
        // FUN_18000f3a0 returns 1, so the parameter is dropped here too.
        // "Item" is optional (mode 0; an empty value registers no item).
        // A literal parameter (desc.Flags bit 0) without the annotation is
        // skipped silently instead [sub_18000F3A0 prologue].
        if (matched && semanticId == 0x28) {
            if (SasFindAnnotation(effect, param, "Name") == nullptr &&
                (pd.Flags & 1) != 0) {
                // silent skip
            } else {
                SasControlObject control;
                control.param = param;
                if (SasGetAnnotationString(effect, param, "Name", 1,
                                           &control.objectName, sas)) {
                    std::string item;
                    if (SasGetAnnotationString(effect, param, "Item", 0, &item,
                                               sas) &&
                        !item.empty()) {
                        control.itemName = item;
                    }
                    sas->controls.push_back(control);
                }
            }
        }

        if (invalid) {
            sas->hasErrors = true;
        }
    }
}

// ===========================================================================
// FUN_1800143d0 - texture creation + capability checks
// ===========================================================================

// Raw "Depth" storage for the volume file-texture load (record +0x60): the
// Dimensions array's third element XOR the "Depth" annotation (mutually
// exclusive), -1 sentinel when absent or negative [sub_180011960 var_128
// triple, 0x1800125E0 / 0x1800127CD]. SasResource (sas_exec.h) carries no raw
// depth slot, so the (immutable) effect annotations are re-queried here -
// equivalent to the parse-time fill the original reads back from its record.
static int SasRawDepthAnnotation(ID3DXEffect* effect, D3DXHANDLE param) {
    D3DXHANDLE dann = SasFindAnnotation(effect, param, "Dimensions");
    if (dann != nullptr) {
        int d[3] = {-1, -1, -1};
        if (effect->GetIntArray(dann, d, 3) == S_OK && d[2] >= 0) {
            return d[2];
        }
        return -1;
    }
    D3DXHANDLE dh = SasFindAnnotation(effect, param, "Depth");
    if (dh != nullptr) {
        int v = 0;
        if (effect->GetInt(dh, &v) == S_OK && v >= 0) {
            return v;
        }
    }
    return -1;
}

// [sub_180093660 = the statically linked dxerr9 DXGetErrorDescriptionA] the
// 0x2C Function-generation failure lines embed the HRESULT's DESCRIPTION
// text ("...'): <text> [%08X]\n"). The original's full table covers every
// D3D/D3DX/XAudio/XML HRESULT; only the codes this path can realistically
// produce are mirrored here (string bytes verified at 0x1800ADEF8/0x1800AE180/
// 0x1800ADED0/0x1800ADEC0/0x1800AA968/0x1800AAA28), anything unknown falls
// back to dxerr's "n/a" (0x1800B2548).
static const char* SasDxErrorDescription(HRESULT hr) {
    switch (static_cast<unsigned long>(hr)) {
        case 0x8876086Cul: return "Invalid call";               // D3DERR_INVALIDCALL
        case 0x8876086Aul: return "Not available";              // D3DERR_NOTAVAILABLE
        case 0x8876086Bul: return "Out of video memory";        // D3DERR_OUTOFVIDEOMEMORY
        case 0x88760B6Eul: return "Invalid data";               // D3DXERR_INVALIDDATA
        case 0x8007000Eul: return "Ran out of memory";          // E_OUTOFMEMORY
        case 0x80004005ul: return "An undetermined error occurred";  // E_FAIL
        default: return "n/a";
    }
}

// [sub_1800143D0 LABEL_278, 0x180015E98 `cmp r12d, 8876086Ah`] every texture
// CREATION / file-load failure funnels into one reporter. hr 0x8876086A
// (D3DERR_NOTAVAILABLE - the disassembly's literal; neither 0x88760866 nor
// 0x88760B6E) prints "'): unsupported parameter\n" with no code suffix
// [0x1800B4508 + 0x1800B44B8 + 0x1800B44F0]; any other hr appends
// "'): <DXGetErrorDescription> [%08X]\n" (description from sub_180093660,
// hex via "%08X"). The message says "create texture" for the FILE load
// failures too - there is no separate "failed to load texture" line.
static void SasLogCreateTextureError(SasEffect* sas, const char* name,
                                     HRESULT hr) {
    if (static_cast<unsigned long>(hr) == 0x8876086Aul) {
        SasLogFormat(sas,
                     "Error: failed to create texture (parameter: '%s'): "
                     "unsupported parameter\n",
                     name);
    } else {
        SasLogFormat(sas,
                     "Error: failed to create texture (parameter: '%s'): %s [%08X]\n",
                     name, SasDxErrorDescription(hr),
                     static_cast<unsigned int>(hr));
    }
}

// Create the D3D texture for a resource object. Ports the observable part of
// FUN_1800143d0: capability-driven size/format adjustment with the
// "%dx%d(%s) -> %dx%d(%s)" warning, then D3DXCreateTexture /
// D3DXCreateCubeTexture / CreateDepthStencilSurface-class creation.
// UNCERTAIN: ANIMATEDTEXTURE loading lives in Phase 3b (anime_texture).
static bool SasEnsureResourceTexture(SasEffect* sas, SasResource* res) {
    if (res->texture != nullptr || res->surface != nullptr) {
        return true;
    }
    if (sas->device == nullptr) {
        return false;
    }
    IDirect3DDevice9* device = sas->device;
    D3DCAPS9 caps;
    device->GetDeviceCaps(&caps);

    // File texture (plain 0x2C with a "ResourceName"): load from the file
    // instead of creating a blank texture. [sub_1800143D0 case 44 first
    // branch] the original dispatches by textureType (+0x20): volume (8) ->
    // D3DXCreateVolumeTextureFromFileExA, cube (9) ->
    // D3DXCreateCubeTextureFromFileExA, everything else (5/6/7) ->
    // D3DXCreateTextureFromFileExA - all three with the ANSI path verbatim
    // and Width/Height(/Depth)/MipLevels taken straight from the resource
    // record (+0x58/+0x5C/+0x60/+0x6C = the raw Dimensions/Width/Height/
    // Depth, Miplevels and ViewportRatio-scaled storage, -1 =
    // D3DX_DEFAULT when absent); Pool is MANAGED, Filter/MipFilter are
    // 0xFFFFFFFF, ColorKey/Usage are 0 and the format is UNKNOWN. The name
    // was already resolved to an absolute path at parse time (the
    // sub_18000EA80 port in SasBuildResourceObject), so the load needs no
    // CWD here - and MANAGED file textures are not rebuilt on device reset
    // [sub_180014270].
    // [sub_1800143D0 switch on the semantic] the FromFile load lives INSIDE
    // case ',' (0x2C) only: a 0x26/0x27/0x2E with a ResourceName still
    // creates its render target / depth surface (the parse resolved and
    // validated the file, but the loaded image is never used). 0x2D
    // (ANIMATEDTEXTURE) and 0x33 (TEXTUREVALUE) have NO branch in
    // sub_1800143D0 at all: the anime object owns its own upload texture
    // (constructed at parse time - see SasBuildResourceObject) and the 0x33
    // record creates nothing (its per-frame consumer only READS the texture
    // of the parameter named by TextureName).
    if (res->semanticId == 0x2D || res->semanticId == 0x33) {
        return true;
    }
    if (res->semanticId == 0x2C && !res->resourceName.empty()) {
        // The record holds the RESOLVED absolute path since the parse-time
        // sub_18000EA80 port (see SasBuildResourceObject) - sub_1800143D0
        // loads exactly that string, no further resolution happens here.
        // [sub_1800143D0 case 44, 0x180014F4E] right before the type
        // dispatch the original writes the load notice to the log window
        // sink (sub_180009080 - the same sink the adjustment warnings and
        // the OffScreen "Info:" line use; the port funnels those through
        // SasLogFormat): "Loading texture file: " + the resolved path
        // [sub_180023070 concat, 0x1800B4008].
        SasLogFormat(sas, "Loading texture file: %s\n",
                     res->resourceName.c_str());
        IDirect3DBaseTexture9* fileTex = nullptr;
        HRESULT hr;
        if (res->textureType == 8) {
            IDirect3DVolumeTexture9* fileVol = nullptr;
            hr = D3DXCreateVolumeTextureFromFileExA(
                device, res->resourceName.c_str(),
                static_cast<unsigned int>(res->texWidth),
                static_cast<unsigned int>(res->texHeight),
                static_cast<unsigned int>(
                    SasRawDepthAnnotation(sas->effect, res->param)),
                static_cast<unsigned int>(res->texMips),
                0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,
                nullptr, nullptr, &fileVol);
            fileTex = fileVol;
        } else if (res->textureType == 9) {
            IDirect3DCubeTexture9* fileCube = nullptr;
            hr = D3DXCreateCubeTextureFromFileExA(
                device, res->resourceName.c_str(),
                static_cast<unsigned int>(res->texWidth),
                static_cast<unsigned int>(res->texMips),
                0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,
                nullptr, nullptr, &fileCube);
            fileTex = fileCube;
        } else {
            IDirect3DTexture9* file2d = nullptr;
            hr = D3DXCreateTextureFromFileExA(
                device, res->resourceName.c_str(),
                static_cast<unsigned int>(res->texWidth),
                static_cast<unsigned int>(res->texHeight),
                static_cast<unsigned int>(res->texMips),
                0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,
                nullptr, nullptr, &file2d);
            fileTex = file2d;
        }
        if (SUCCEEDED(hr) && fileTex != nullptr) {
            res->texture = fileTex;
            res->defaultPool = false;  // MANAGED survives a device reset
            sas->effect->SetTexture(res->param, fileTex);
            return true;
        }
        // [LABEL_128 -> LABEL_278] the FromFile failure reports through the
        // shared create-texture line (with the hr, NOT a "failed to load
        // texture" text of its own).
        SasLogCreateTextureError(sas, res->name.c_str(), hr);
        sas->hasErrors = true;
        return false;
    }

    // [sub_1800143D0 case ',' second branch, gate 0x18001523A
    // (cmp [rsi+80h], 0 = Function string non-empty) through 0x180016437]
    // 0x2C with a "Function" annotation and no ResourceName: generate the
    // texture with a texture shader. The entry point is compiled FROM THE
    // EFFECT'S OWN .fx FILE (pSrcFile = sas+0x48 FullPath) with the record's
    // "Target" as the profile ("tx_1_0" default), no defines, no include,
    // flags 0 and no constant table (call at 0x1800152A6); the compiled
    // ID3DXBuffer's GetBufferPointer bytecode is wrapped by
    // D3DXCreateTextureShader (0x180015688) and the ID3DXTextureShader is
    // used once and released immediately (LABEL_326) - never stored. The raw
    // dimension triple (+0x58/+0x5C/+0x60) defaults EVERY -1 component to 64
    // (the 3-iteration loop after 0x1800158B7); Miplevels (+0x6C) stays
    // verbatim (-1 = D3DX_DEFAULT). The filled texture is created with
    // usage 0, pool MANAGED and the record's Format (A8R8G8B8 default for
    // 0x2C) - like the file path it survives device resets and is NOT
    // rebuilt by sub_180014270 (the +100 default-pool flag stays clear).
    // Dispatch is by textureType (+0x20): 8 = volume, 9 = cube, else 2D.
    // The compile / texture-shader / fill failures log "Error: failed to
    // fill texture (parameter: '<name>'): ..." (the compiler's own
    // error-buffer text when D3DX produced one, else the DX error
    // description + " [%08X]"); the PLAIN texture creations instead route
    // LABEL_275 -> LABEL_276 -> LABEL_278 and report through the shared
    // "Error: failed to create texture ..." line. All failures reject the
    // effect (FUN_180011960 returns the hr).
    if (res->semanticId == 0x2C && !res->functionName.empty()) {
        ID3DXBuffer* shaderCode = nullptr;
        ID3DXBuffer* errors = nullptr;
        HRESULT hr = D3DXCompileShaderFromFileA(
            sas->path.c_str(), nullptr, nullptr, res->functionName.c_str(),
            res->functionTarget.c_str(), 0, &shaderCode, &errors, nullptr);
        if (FAILED(hr)) {
            if (errors != nullptr && errors->GetBufferSize() != 0) {
                // [0x1800152B7-0x18001534E] the error buffer's content (the
                // HLSL compiler's message text) replaces the code text.
                SasLogFormat(sas,
                             "Error: failed to fill texture (parameter: '%s'): %s\n",
                             res->name.c_str(),
                             static_cast<const char*>(errors->GetBufferPointer()));
            } else {
                SasLogFormat(sas,
                             "Error: failed to fill texture (parameter: '%s'): %s [%08X]\n",
                             res->name.c_str(), SasDxErrorDescription(hr),
                             static_cast<unsigned int>(hr));
            }
            if (errors != nullptr) {
                errors->Release();
            }
            if (shaderCode != nullptr) {
                shaderCode->Release();
            }
            sas->hasErrors = true;
            return false;
        }
        ID3DXTextureShader* textureShader = nullptr;
        hr = D3DXCreateTextureShader(
            static_cast<const unsigned long*>(shaderCode->GetBufferPointer()),
            &textureShader);
        if (FAILED(hr)) {
            SasLogFormat(sas,
                         "Error: failed to fill texture (parameter: '%s'): %s [%08X]\n",
                         res->name.c_str(), SasDxErrorDescription(hr),
                         static_cast<unsigned int>(hr));
            if (errors != nullptr) {
                errors->Release();
            }
            if (shaderCode != nullptr) {
                shaderCode->Release();
            }
            sas->hasErrors = true;
            return false;
        }
        // [0x1800158B7 tail] both buffers are dropped once the texture
        // shader exists (the bytecode was copied into it).
        if (errors != nullptr) {
            errors->Release();
        }
        shaderCode->Release();
        // The raw annotation triple with every -1 component defaulted to 64.
        // Depth re-queries the immutable annotations (SasRawDepthAnnotation,
        // the +0x60 slot the port's record does not carry).
        unsigned int w =
            (res->texWidth < 0) ? 64u : static_cast<unsigned int>(res->texWidth);
        unsigned int h =
            (res->texHeight < 0) ? 64u : static_cast<unsigned int>(res->texHeight);
        int rawDepth = SasRawDepthAnnotation(sas->effect, res->param);
        unsigned int d = (rawDepth < 0) ? 64u : static_cast<unsigned int>(rawDepth);
        unsigned int mips = static_cast<unsigned int>(res->texMips);
        IDirect3DBaseTexture9* generated = nullptr;
        HRESULT fillHr;
        if (res->textureType == 8) {
            IDirect3DVolumeTexture9* volume = nullptr;
            hr = D3DXCreateVolumeTexture(device, w, h, d, mips, 0, res->format,
                                         D3DPOOL_MANAGED, &volume);
            if (FAILED(hr)) {
                if (volume != nullptr) {
                    volume->Release();
                }
                textureShader->Release();
                // [LABEL_275 -> LABEL_278] plain creation failures report
                // through the create-texture line, not "failed to fill".
                SasLogCreateTextureError(sas, res->name.c_str(), hr);
                sas->hasErrors = true;
                return false;
            }
            fillHr = D3DXFillVolumeTextureTX(volume, textureShader);
            generated = volume;
        } else if (res->textureType == 9) {
            IDirect3DCubeTexture9* cube = nullptr;
            hr = D3DXCreateCubeTexture(device, w, mips, 0, res->format,
                                       D3DPOOL_MANAGED, &cube);
            if (FAILED(hr)) {
                if (cube != nullptr) {
                    cube->Release();
                }
                textureShader->Release();
                // [LABEL_275 -> LABEL_278]
                SasLogCreateTextureError(sas, res->name.c_str(), hr);
                sas->hasErrors = true;
                return false;
            }
            fillHr = D3DXFillCubeTextureTX(cube, textureShader);
            generated = cube;
        } else {
            IDirect3DTexture9* tex = nullptr;
            hr = D3DXCreateTexture(device, w, h, mips, 0, res->format,
                                   D3DPOOL_MANAGED, &tex);
            if (FAILED(hr)) {
                if (tex != nullptr) {
                    tex->Release();
                }
                textureShader->Release();
                // [LABEL_275 -> LABEL_278]
                SasLogCreateTextureError(sas, res->name.c_str(), hr);
                sas->hasErrors = true;
                return false;
            }
            fillHr = D3DXFillTextureTX(tex, textureShader);
            generated = tex;
        }
        if (FAILED(fillHr)) {
            SasLogFormat(sas,
                         "Error: failed to fill texture (parameter: '%s'): %s [%08X]\n",
                         res->name.c_str(), SasDxErrorDescription(fillHr),
                         static_cast<unsigned int>(fillHr));
            generated->Release();
            textureShader->Release();
            sas->hasErrors = true;
            return false;
        }
        res->texture = generated;
        res->defaultPool = false;  // MANAGED survives a device reset
        textureShader->Release();
        sas->effect->SetTexture(res->param, generated);
        return true;
    }

    int w = res->reqWidth;
    int h = res->reqHeight;
    D3DFORMAT fmt = res->format;
    int origW = w, origH = h;
    D3DFORMAT origFmt = fmt;

    // [0x1800B4460/0x1800B4478/0x1800B4498] automatic size/format adjustment.
    if (w > static_cast<int>(caps.MaxTextureWidth)) {
        w = static_cast<int>(caps.MaxTextureWidth);
    }
    if (h > static_cast<int>(caps.MaxTextureHeight)) {
        h = static_cast<int>(caps.MaxTextureHeight);
    }
    if (w <= 0) {
        w = 64;
    }
    if (h <= 0) {
        h = 64;
    }

    if (res->semanticId == 0x27) {
        // [sub_1800143D0 case 39, 0x1800147BE-0x180014ABB] depth target:
        //  (1) a temporary D3DXCreateTexture(dev, w, h, RAW mips(+0x6C),
        //      usage = 1 = D3DUSAGE_RENDERTARGET, A8R8G8B8, pool = 0 =
        //      D3DPOOL_DEFAULT) [0x1800147E5: [rsp+20h]=1, [rsp+28h]=15h,
        //      [rsp+30h]=r15d(0); 0x1800147ED: r9d=[rsi+6Ch]] probes the
        //      driver-adjusted size (GetSurfaceLevel(0) -> GetDesc), then is
        //      released;
        //  (2) CreateDepthStencilSurface with that size, the REQUESTED
        //      format and the probe's multisample parameters;
        //  (3) a 1x1 A8R8G8B8 MANAGED placeholder texture is kept in the
        //      texture slot - SetTexture binds this placeholder while the
        //      depth surface hangs off the record for the runtime redirect;
        //  (4) the depth surface's actual desc feeds the adjustment warning.
        IDirect3DTexture9* probe = nullptr;
        HRESULT hr = D3DXCreateTexture(device, static_cast<UINT>(w),
                                       static_cast<UINT>(h),
                                       static_cast<unsigned int>(res->texMips),
                                       D3DUSAGE_RENDERTARGET,
                                       D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                       &probe);
        if (hr != S_OK || probe == nullptr) {
            SasLogCreateTextureError(sas, res->name.c_str(), hr);
            sas->hasErrors = true;
            return false;
        }
        UINT mw = static_cast<UINT>(w);
        UINT mh = static_cast<UINT>(h);
        D3DMULTISAMPLE_TYPE msType = D3DMULTISAMPLE_NONE;
        DWORD msQuality = 0;
        IDirect3DSurface9* level = nullptr;
        if (probe->GetSurfaceLevel(0, &level) == S_OK && level != nullptr) {
            D3DSURFACE_DESC ld;
            memset(&ld, 0, sizeof(ld));
            if (level->GetDesc(&ld) == S_OK) {
                mw = ld.Width;
                mh = ld.Height;
                msType = ld.MultiSampleType;
                msQuality = ld.MultiSampleQuality;
            }
            level->Release();
        }
        probe->Release();
        IDirect3DSurface9* surf = nullptr;
        hr = device->CreateDepthStencilSurface(mw, mh, fmt, msType, msQuality,
                                               FALSE, &surf, nullptr);
        if (hr != S_OK || surf == nullptr) {
            SasLogCreateTextureError(sas, res->name.c_str(), hr);
            sas->hasErrors = true;
            return false;
        }
        res->surface = surf;
        res->defaultPool = true;  // CreateDepthStencilSurface is DEFAULT-pool
        IDirect3DTexture9* placeholder = nullptr;
        hr = D3DXCreateTexture(device, 1, 1, 1, 0, D3DFMT_A8R8G8B8,
                               D3DPOOL_MANAGED, &placeholder);
        if (hr != S_OK || placeholder == nullptr) {
            surf->Release();
            res->surface = nullptr;
            SasLogCreateTextureError(sas, res->name.c_str(), hr);
            sas->hasErrors = true;
            return false;
        }
        res->texture = placeholder;
        // [FUN_180011960 0x1800140e4-0x180014109] every successfully created
        // 0x27 resource registers into the process-wide depth registry:
        // globalMap[record texture] = record surface (sub_18001EC90
        // operator[] + store on qword_1800D9C48, gated on semanticId == 0x27
        // && texture != null). The device-reset rebuild repeats the same
        // registration [sub_180014270 0x180014326 / sub_180016660
        // 0x18001672a] - this shared branch runs for both the parse-time
        // creation and the SasRecreateResources rebuild, so one call covers
        // 0x180011960 / sub_180014270 / sub_180016660 alike. The erase
        // counterpart fires from the SasResource destructor (the
        // FUN_18000B210 case 39 unload erase).
        SasRegisterDepthSurface(res->texture, res->surface);
        D3DSURFACE_DESC dd;
        memset(&dd, 0, sizeof(dd));
        if (surf->GetDesc(&dd) == S_OK) {
            w = static_cast<int>(dd.Width);
            h = static_cast<int>(dd.Height);
            fmt = dd.Format;
        }
    } else {
        bool isRenderTarget = (res->semanticId == 0x26 || res->semanticId == 0x2E);
        DWORD usage = isRenderTarget ? D3DUSAGE_RENDERTARGET : 0;
        D3DPOOL pool = isRenderTarget ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        // [0x180014558 / 0x180014AE5] the mip count handed to creation is the
        // record's RAW +0x6C storage (res->texMips: render-target semantics
        // default it to 1 when the annotation is absent, a negative
        // annotation keeps 0xFFFFFFFF): 0 or 0xFFFFFFFF routes 0x26/0x2E to
        // a direct device->CreateTexture with Usage = 0x401 =
        // D3DUSAGE_RENDERTARGET|D3DUSAGE_AUTOGENMIPMAP; anything else goes
        // to D3DXCreateTexture with the value verbatim.
        UINT mip = static_cast<unsigned int>(res->texMips);
        if (res->textureType == 9) {
            // [0x1800144E8-0x180014516] cube render target (0x26 is the only
            // semantic that admits cube - 0x27/0x2E reject it at parse): no
            // 0/-1 special case, no level-0 surface fetch and no desc
            // readback, so no adjustment warning either. The raw mip count
            // goes straight to D3DXCreateCubeTexture (usage = 1 = RT, pool =
            // DEFAULT, edge = the adjusted width [+0x58]).
            IDirect3DCubeTexture9* cube = nullptr;
            HRESULT hr = D3DXCreateCubeTexture(device, static_cast<UINT>(w),
                                               mip, usage, fmt, pool, &cube);
            if (hr != S_OK || cube == nullptr) {
                SasLogCreateTextureError(sas, res->name.c_str(), hr);
                sas->hasErrors = true;
                return false;
            }
            res->texture = cube;
            res->defaultPool = isRenderTarget;  // RT forces D3DPOOL_DEFAULT
            sas->effect->SetTexture(res->param, res->texture);
            return true;
        }
        IDirect3DTexture9* tex = nullptr;
        HRESULT hr;
        if (isRenderTarget && (mip == 0u || mip == 0xFFFFFFFFu)) {
            // [0x180014597-0x1800145C7] 0x26 passes Levels = the raw value
            // itself (r9d is never reloaded on this path: 0 via the ==0
            // test, 0xFFFFFFFF via the ==-1 test); [0x180014B24-0x180014B57]
            // 0x2E zeroes it explicitly (xor r9d, r9d). Usage = 0x401, the
            // format comes from the record, pool = DEFAULT, pSharedHandle
            // = 0.
            UINT levels = (res->semanticId == 0x26) ? mip : 0u;
            hr = device->CreateTexture(static_cast<UINT>(w),
                                       static_cast<UINT>(h), levels, 0x401, fmt,
                                       D3DPOOL_DEFAULT, &tex, nullptr);
        } else {
            hr = D3DXCreateTexture(device, static_cast<UINT>(w),
                                   static_cast<UINT>(h), mip, usage, fmt, pool,
                                   &tex);
        }
        if (hr != S_OK || tex == nullptr) {
            SasLogCreateTextureError(sas, res->name.c_str(), hr);
            sas->hasErrors = true;
            return false;
        }
        D3DSURFACE_DESC desc;
        memset(&desc, 0, sizeof(desc));
        if (tex->GetLevelDesc(0, &desc) == S_OK) {
            w = static_cast<int>(desc.Width);
            h = static_cast<int>(desc.Height);
            fmt = desc.Format;
        }
        res->texture = tex;
        res->defaultPool = isRenderTarget;  // RT usage forces D3DPOOL_DEFAULT
        if (res->textureType == 5 || res->textureType == 7) {
            tex->GetSurfaceLevel(0, &res->surface);
        }
        if (res->semanticId == 0x2E) {
            // [sub_1800143D0 case 46, 0x180014bd7-0x180014c76] EVERY
            // offscreen render target gets a paired depth stencil surface,
            // created unconditionally (the AntiAlias annotation does NOT
            // gate it): CreateDepthStencilSurface with the CREATED texture's
            // own desc size and multisample parameters (a texture is never
            // multisampled, so effectively D3DMULTISAMPLE_NONE/0), format
            // 75 = D3DFMT_D24S8, Discard = FALSE, stored at offscreen+8
            // (sub_180002800 releases any previous occupant first). When the
            // AntiAlias annotation is set the CURRENT render target's
            // MultiSampleType/Quality are additionally mirrored into the
            // record (+0x1C/+0x20) - the creation call above never reads
            // those, they are plain storage.
            res->offscreenMsType = D3DMULTISAMPLE_NONE;
            res->offscreenMsQuality = 0;
            if (res->antiAlias) {
                IDirect3DSurface9* rt = nullptr;
                if (device->GetRenderTarget(0, &rt) == S_OK && rt != nullptr) {
                    D3DSURFACE_DESC rtDesc;
                    memset(&rtDesc, 0, sizeof(rtDesc));
                    if (rt->GetDesc(&rtDesc) == S_OK) {
                        res->offscreenMsType = rtDesc.MultiSampleType;
                        res->offscreenMsQuality = rtDesc.MultiSampleQuality;
                    }
                    rt->Release();
                }
            }
            if (res->offscreenDepth != nullptr) {
                res->offscreenDepth->Release();
                res->offscreenDepth = nullptr;
            }
            IDirect3DSurface9* depth = nullptr;
            HRESULT dhr = device->CreateDepthStencilSurface(
                desc.Width, desc.Height, D3DFMT_D24S8, desc.MultiSampleType,
                desc.MultiSampleQuality, FALSE, &depth, nullptr);
            if (dhr != S_OK || depth == nullptr) {
                SasLogCreateTextureError(sas, res->name.c_str(), dhr);
                sas->hasErrors = true;
                if (res->surface != nullptr) {
                    res->surface->Release();
                    res->surface = nullptr;
                }
                tex->Release();
                res->texture = nullptr;
                return false;
            }
            res->offscreenDepth = depth;
        }
    }

    if (origW != w || origH != h || origFmt != fmt) {
        // "%dx%d(%s) -> %dx%d(%s)" + "Warning: texture format of '%s' was
        // automatically adjusted: " [0x1800B4460..0x1800B4498, sub_180011920
        // supplies the format names; for 0x27 the comparison is against the
        // created DEPTH surface's desc (0x180014998-0x180014ABB)].
        char from[96], to[96];
        char fmtA[16], fmtB[16];
        _snprintf_s(from, sizeof(from), _TRUNCATE, "%dx%d(%s)", origW, origH,
                    SasFormatDisplayName(origFmt, fmtA, sizeof(fmtA)));
        _snprintf_s(to, sizeof(to), _TRUNCATE, "%dx%d(%s)", w, h,
                    SasFormatDisplayName(fmt, fmtB, sizeof(fmtB)));
        SasLogFormat(sas, "Warning: texture format of '%s' was automatically adjusted: "
                          "%s -> %s\n",
                     res->name.c_str(), from, to);
    }

    // sub_180011960 tail: once sub_1800143D0 finishes creating the texture,
    // the original registers the resource (sub_18001F090) and immediately
    // binds it to the effect parameter: ID3DXBaseEffect vtbl+416 =
    // SetTexture(param, resource->texture), run for every resource that
    // reached the tail (0x26/0x27/0x2C/0x2E alike; the depth target binds
    // the 1x1 dummy texture created in sub_1800143D0 case 39 - see the
    // branch above). The device-reset path sub_180014270 repeats the same
    // vtbl+416 call after recreating each resource. Creation failures
    // returned above and never reach this bind, matching the original
    // early-outs.
    sas->effect->SetTexture(res->param, res->texture);
    return true;
}

// ===========================================================================
// FUN_1800169d0 - technique scan / FUN_180017a80 - pass scan
// ===========================================================================

// Parse a "Subset" annotation value ("0-4,7,9-" style) into the range map
// [L18856-18941 / sub_1800169D0 0x1800174a3-0x180017748]. State machine:
//   - spaces separate tokens; "N" adds {N,N}; "A-B" adds {A,B};
//   - "N-" followed by ',' or end-of-string adds {N,0x7fffffff};
//   - a fully-consumed spec with NO ranges adds the 0x7fffffff sentinel
//     pair - a range NO subset can match (the technique is hidden).
// Returns false on any parse failure (misplaced '-', negative or
// non-numeric token, trailing garbage): sub_1800169D0 then logs
// "Error: invalid subset range: ..." and the technique scan returns 1,
// which the loader [sub_18000BC90 0x18000c423-0x18000c42a] treats as a
// fatal error - the whole effect is REJECTED.
static bool SasParseSubsetRanges(SasTechnique* tech, const std::string& value) {
    tech->subsets.clear();
    tech->allSubsets = false;
    const char* p = value.c_str();
    long start = -1;
    while (*p != '\0' || start >= 0) {
        if (isspace(static_cast<unsigned char>(*p))) {
            ++p;
            continue;
        }
        if (*p == '-') {
            break;  // leading/misplaced '-' -> parse error
        }
        char* end = nullptr;
        long v = strtol(p, &end, 0);
        if (v < 0) {
            break;  // negative subset index -> parse error
        }
        if (end == p) {
            // No digits consumed: only valid when it closes an open range
            // ("N-" directly followed by ',' or the end of the string).
            if (start < 0 || (*end != ',' && *end != '\0')) {
                break;
            }
            tech->subsets[static_cast<int>(start)] = 0x7fffffff;
            start = -1;
            if (*end == '\0') {
                break;
            }
            p = end + 1;  // past ','
            continue;
        }
        for (; isspace(static_cast<unsigned char>(*end)); ++end) {
        }
        p = end;
        if (*end == ',' || *end == '\0') {
            if (start >= 0) {
                tech->subsets[static_cast<int>(start)] = static_cast<int>(v);
                start = -1;
            } else {
                tech->subsets[static_cast<int>(v)] = static_cast<int>(v);
            }
            if (*end == '\0') {
                break;
            }
            ++p;  // past ','
            continue;
        }
        if (*end != '-' || start >= 0) {
            break;  // trailing garbage / second '-' -> parse error
        }
        start = v;
        ++p;  // past '-'
    }
    if (*p != '\0') {
        return false;  // stopped on an unparsable character
    }
    if (tech->subsets.empty()) {
        // [sub_1800169D0 0x18001774a-0x18001776b] a fully-consumed spec with
        // NO ranges (e.g. Subset="") inserts the (0x7fffffff, 0x7fffffff)
        // sentinel pair. The selector [sub_18001DB50 0x18001dc11-0x18001dc3e]
        // walks the ranges as a RESTRICTION: the sentinel's min (0x7fffffff)
        // exceeds every real subset index, so the technique matches NO subset
        // ("hidden" - not "no restriction"; allSubsets stays false).
        tech->subsets[0x7fffffff] = 0x7fffffff;
    }
    return true;
}

// FUN_1800169d0: scan one technique (annotations + validation flags).
// Returns true when any error was recorded - every error in the original
// sets its v6 return value, sub_180016900 ORs the per-technique and
// per-pass results, and the loader [sub_18000BC90 0x18000c423-0x18000c42a]
// rejects the whole effect on a nonzero return (same path as a c470 error).
static bool SasScanTechnique(SasEffect* sas, D3DXHANDLE hTech) {
    ID3DXEffect* effect = sas->effect;
    SasTechnique tech;
    tech.handle = hTech;
    bool scanError = false;
    D3DXTECHNIQUE_DESC td;
    if (effect->GetTechniqueDesc(hTech, &td) != S_OK) {
        return false;
    }
    tech.name = (td.Name != nullptr) ? td.Name : "";
    tech.empty = (td.Passes == 0);  // "empty technique" convention

    // The original keeps D3DX validation and shader capability checks
    // separate (0x180016A5C/0x180016A70). SkipValidation uses the latter.
    tech.hardwareOk = (effect->ValidateTechnique(hTech) == S_OK);
    D3DCAPS9 caps = {};
    tech.shaderCapsOk = sas->device != nullptr && sas->device->GetDeviceCaps(&caps) == S_OK;

    std::string script;
    for (UINT a = 0; a < td.Annotations; ++a) {
        D3DXHANDLE ann = effect->GetAnnotation(hTech, a);
        if (ann == nullptr) {
            continue;
        }
        D3DXPARAMETER_DESC ad;
        if (effect->GetParameterDesc(ann, &ad) != S_OK || ad.Name == nullptr) {
            continue;
        }
        if (_stricmp(ad.Name, "MmdPass") == 0) {
            LPCSTR s = nullptr;
            if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                // [L18512-18516] "Error: type of annotation 'MmdPass' is
                // invalid. (technique: %s)\n"
                SasLogFormat(sas, "Error: type of annotation 'MmdPass' is invalid. "
                                  "(technique: %s)\n",
                             tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
                continue;
            }
            const std::string value = s;
            const std::string lower = ToLowerAscii(value);
            if (lower == "object") {
                tech.mmdPass = kSasPassObject;
            } else if (lower == "object_ss") {
                tech.mmdPass = kSasPassObjectSS;
            } else if (lower == "edge") {
                tech.mmdPass = kSasPassEdge;   // [0x1800B456C] "edge" = mode 3
            } else if (lower == "shadow") {
                tech.mmdPass = kSasPassShadow;
            } else if (lower == "zplot") {
                tech.mmdPass = kSasPassZplot;  // [0x1800B456C] "zplot" = mode 4
            } else {
                // [L18696-18701] "Error: unknown pass mode: %s (technique: %s)\n"
                SasLogFormat(sas, "Error: unknown pass mode: %s  (technique: %s)\n",
                             value.c_str(), tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
            }
        } else if (_stricmp(ad.Name, "UseTexture") == 0 ||
                   _stricmp(ad.Name, "UseSpheremap") == 0 ||
                   _stricmp(ad.Name, "UseToon") == 0) {
            BOOL v = FALSE;
            bool ok = (effect->GetBool(ann, &v) == S_OK);
            if (!ok) {
                SasLogFormat(sas, "Error: type of annotation '%s' is invalid. "
                                  "(technique: %s)\n",
                             ad.Name, tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
                continue;
            }
            // [0x1800171b4/0x180017306/0x180017458] the annotation stores
            // byte 0/1 over the 0xFF wildcard default (see SasTechnique).
            if (_stricmp(ad.Name, "UseTexture") == 0) {
                tech.useTexture = (v != FALSE) ? 1 : 0;
            } else if (_stricmp(ad.Name, "UseSpheremap") == 0) {
                tech.useSpheremap = (v != FALSE) ? 1 : 0;
            } else {
                tech.useToon = (v != FALSE) ? 1 : 0;
            }
        } else if (_stricmp(ad.Name, "Subset") == 0) {
            LPCSTR s = nullptr;
            if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                SasLogFormat(sas, "Error: type of annotation 'Subset' is invalid. "
                                  "(technique: %s)\n",
                             tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
                continue;
            }
            if (!SasParseSubsetRanges(&tech, s)) {
                // [0x1800175f4-0x180017650] "Error: invalid subset range:
                // '<value>' (technique: <name>)\n" + v6=1 (fatal).
                SasLogFormat(sas, "Error: invalid subset range: '%s' (technique: %s)\n",
                             s, tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
            }
        } else if (_stricmp(ad.Name, "Script") == 0) {
            LPCSTR s = nullptr;
            if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                SasLogFormat(sas, "Error: type of annotation 'Script' is invalid. "
                                  "(technique: %s)\n",
                             tech.name.c_str());
                sas->hasErrors = true;
                scanError = true;
                continue;
            }
            tech.hasScript = true;
            script = s;
        }
    }

    // Passes [FUN_180017a80].
    for (UINT p = 0; p < td.Passes; ++p) {
        D3DXHANDLE hPass = effect->GetPass(hTech, p);
        if (hPass == nullptr) {
            continue;
        }
        SasPass pass;
        pass.handle = hPass;
        pass.index = static_cast<int>(p);
        D3DXPASS_DESC pdd;
        memset(&pdd, 0, sizeof(pdd));
        if (effect->GetPassDesc(hPass, &pdd) == S_OK) {
            pass.name = (pdd.Name != nullptr) ? pdd.Name : "";
            // [L19120-19142] PSIZE15 vertex-input probe + shader version mix.
            // D3DXFX_NOT_CLONEABLE discards the shader token streams; the
            // normal effect creation path retains them for this inspection.
            if (pdd.pVertexShaderFunction != nullptr) {
                pass.hasCustomShaders = true;
                UINT count = 0;
                if (D3DXGetShaderInputSemantics(pdd.pVertexShaderFunction, nullptr,
                                                &count) == S_OK &&
                    count > 0) {
                    std::vector<D3DXSEMANTIC> sems(static_cast<size_t>(count));
                    if (D3DXGetShaderInputSemantics(pdd.pVertexShaderFunction, &sems[0],
                                                    &count) == S_OK) {
                        for (UINT s = 0; s < count; ++s) {
                            // usage 4, usage index 15 = the PSIZE15 probe
                            if (sems[s].Usage == 4 && sems[s].UsageIndex == 15) {
                                pass.needsPsize15 = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (pdd.pPixelShaderFunction != nullptr) {
                pass.hasCustomShaders = true;
            }
            // [MME_SasScanPass 0x180017bb1/0x180017bbf] both versions are
            // truncated to 16 bits (movzx eax, ax) before the mix check: the
            // 0xFFFE (vs) / 0xFFFF (ps) token high half drops out, so vs_3_0
            // and ps_3_0 both become 0x0300 and compare equal. A missing
            // shader keeps version 0.
            unsigned int vsVer = (pdd.pVertexShaderFunction != nullptr)
                                     ? static_cast<unsigned int>(
                                           static_cast<unsigned short>(
                                               D3DXGetShaderVersion(
                                                   pdd.pVertexShaderFunction)))
                                     : 0u;
            unsigned int psVer = (pdd.pPixelShaderFunction != nullptr)
                                     ? static_cast<unsigned int>(
                                           static_cast<unsigned short>(
                                               D3DXGetShaderVersion(
                                                   pdd.pPixelShaderFunction)))
                                     : 0u;
            if (vsVer > static_cast<unsigned short>(caps.VertexShaderVersion) ||
                psVer > static_cast<unsigned short>(caps.PixelShaderVersion)) {
                tech.shaderCapsOk = false;
            }
            // [L19143 0x180017bc6] (vs >= 3.0 || ps >= 3.0) && vs != ps
            bool mixOk = !(((vsVer >= 0x300u) || (psVer >= 0x300u)) &&
                           (vsVer != psVer));
            pass.shaderMixOk = mixOk;
            if (!mixOk) {
                tech.shaderMixOk = false;
                // [0x1800B4630] "Error: vs_3_0 or ps_3_0 may not be used with
                // any other shader versions. (pass: %s, technique: %s)\n"
                const std::string error =
                    "Error: vs_3_0 or ps_3_0 may not be used with any other "
                    "shader versions. (pass: " + pass.name +
                    ", technique: " + tech.name + ")\n";
                // The direct diagnostic precedes the path-qualified modal
                // message (0x180017D66..0x180017EF8). Each effect path has its
                // own entry in the shared, phase-scoped suppression set.
                MmeLogWrite(error.c_str(), 0);
                const std::string message = sas->path + "\n\n" + error;
                if (MmeLogShouldShowMessageBox(message.c_str())) {
                    MessageBoxA(g_mainWindow, message.c_str(), "MikuMikuEffect",
                                MB_ICONERROR);
                }
                // This diagnostic leaves the scan result unchanged. Only
                // annotation/script failures below reject the effect.
            }
        }
        // Pass "Script" annotation [FUN_180017a80 L19288-19307].
        if (pdd.Annotations > 0) {
            for (UINT a = 0; a < pdd.Annotations; ++a) {
                D3DXHANDLE ann = effect->GetAnnotation(hPass, a);
                if (ann == nullptr) {
                    continue;
                }
                D3DXPARAMETER_DESC ad;
                if (effect->GetParameterDesc(ann, &ad) != S_OK || ad.Name == nullptr) {
                    continue;
                }
                if (_stricmp(ad.Name, "Script") == 0) {
                    LPCSTR s = nullptr;
                    if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                        SasLogFormat(sas,
                                     "Error: type of annotation 'Script' is invalid. "
                                     "(pass: %s, technique: %s)\n",
                                     pass.name.c_str(), tech.name.c_str());
                        sas->hasErrors = true;
                        scanError = true;  // [FUN_180017a80] error return
                    } else {
                        pass.hasScript = true;
                        pass.scriptText = s;
                    }
                }
            }
        }
        tech.passes.push_back(pass);
    }

    sas->techniques.push_back(tech);

    // Compile scripts now that the records exist (FUN_180018200 compile side,
    // implemented in sas_exec.cpp). The original compiles lazily with the
    // technique context string; behavior identical.
    if (tech.hasScript) {
        if (SasCompileScript(sas, script, hTech, nullptr, tech.name, "",
                             &sas->techniques.back().cmds)) {
            scanError = true;  // [0x180017a2a-0x180017a31] compile failure ORs into v6
        }
    }
    for (size_t p = 0; p < sas->techniques.back().passes.size(); ++p) {
        SasPass& pass = sas->techniques.back().passes[p];
        if (pass.hasScript) {
            if (SasCompileScript(sas, pass.scriptText, hTech, pass.handle, tech.name,
                                 pass.name, &pass.cmds)) {
                scanError = true;
            }
        }
    }
    return scanError;
}

// ===========================================================================
// Public API
// ===========================================================================

SasEffect* SasParse(ID3DXEffect* effect, const std::string& pathAnsi,
                    IDirect3DDevice9* device, std::string* outFailureLog) {
    if (effect == nullptr) {
        return nullptr;
    }
    SasEffect* sas = new SasEffect();
    sas->effect = effect;
    sas->device = device;
    sas->path = pathAnsi;

    D3DXEFFECT_DESC ed;
    memset(&ed, 0, sizeof(ed));
    if (effect->GetDesc(&ed) != S_OK) {
        SasLogLine(sas, "Error: cannot read the effect description.\n");
        sas->hasErrors = true;
    }

    // [FUN_18000c470 L11844-11841] initial technique enumeration: every
    // technique handle in declaration order goes into techniqueOrder. A
    // valid STANDARDSGLOBAL Script annotation later REPLACES this list (the
    // clear at 0x18000d072) - see the Script handling below.
    for (UINT t = 0; t < ed.Techniques; ++t) {
        D3DXHANDLE h = effect->GetTechnique(t);
        if (h != nullptr) {
            sas->techniqueOrder.push_back(h);
        }
    }

    // STANDARDSGLOBAL scan [FUN_18000c470 main block].
    bool parseFailed = false;
    for (UINT i = 0; i < ed.Parameters && !parseFailed; ++i) {
        D3DXHANDLE param = effect->GetParameter(nullptr, i);
        if (param == nullptr) {
            continue;
        }
        D3DXPARAMETER_DESC pd;
        if (effect->GetParameterDesc(param, &pd) != S_OK) {
            continue;
        }
        if (pd.Semantic == nullptr || _stricmp(pd.Semantic, "STANDARDSGLOBAL") != 0) {
            continue;
        }
        const char* pname = (pd.Name != nullptr) ? pd.Name : "(null)";

        float version = 0.0f;
        if (effect->GetFloat(param, &version) != S_OK || version <= 0.0f) {
            // [L11853-11857] "Error: SAS version is invalid. (parameter: '%s')\n"
            SasLogFormat(sas, "Error: SAS version is invalid. (parameter: '%s')\n", pname);
            parseFailed = true;
            break;
        }
        if (version != 0.8f) {
            // [L11876-11882] "Error: SAS version '%g' is not supported.
            // (parameter: '%s')\n"
            char vbuf[32];
            _snprintf_s(vbuf, sizeof(vbuf), _TRUNCATE, "%g", version);
            SasLogFormat(sas, "Error: SAS version '%s' is not supported. (parameter: '%s')\n",
                         vbuf, pname);
            parseFailed = true;
            break;
        }

        // ScriptOutput [L11916-12008]: must be "color".
        std::string output;
        if (SasGetAnnotationString(effect, param, "ScriptOutput", 0, &output, sas)) {
            if (ToLowerAscii(output) != "color") {
                SasLogFormat(sas,
                             "Error: output type '%s' is not supported. (parameter: %s)\n",
                             output.c_str(), pname);
                parseFailed = true;
                break;
            }
        }

        // ScriptClass [L12011-12145]: object / scene / sceneorobject.
        std::string cls;
        if (SasGetAnnotationString(effect, param, "ScriptClass", 0, &cls, sas)) {
            const std::string lower = ToLowerAscii(cls);
            if (lower == "object") {
                sas->scriptClass = kSasClassObject;
            } else if (lower == "scene") {
                sas->scriptClass = kSasClassScene;
            } else if (lower == "sceneorobject") {
                sas->scriptClass = kSasClassSceneOrObject;
            } else {
                SasLogFormat(sas, "Error: effect class '%s' is not supported. (parameter: %s)\n",
                             cls.c_str(), pname);
                parseFailed = true;
                break;
            }
        }

        // ScriptOrder [L12147-12280]: standard / preprocess / postprocess.
        std::string order;
        if (SasGetAnnotationString(effect, param, "ScriptOrder", 0, &order, sas)) {
            const std::string lower = ToLowerAscii(order);
            if (lower == "standard") {
                sas->scriptOrder = kSasOrderStandard;
            } else if (lower == "preprocess") {
                sas->scriptOrder = kSasOrderPreprocess;
            } else if (lower == "postprocess") {
                sas->scriptOrder = kSasOrderPostprocess;
            } else {
                SasLogFormat(sas, "Error: effect order '%s' is not supported. (parameter: %s)\n",
                             order.c_str(), pname);
                parseFailed = true;
                break;
            }
        }

        // Script [L12284+]: the technique search order. A syntax error in
        // the normalized script is FATAL (c470 returns 1 at loc_18000DF7B
        // -> the loader rejects the effect), exactly like the other
        // STANDARDSGLOBAL failures below/above. A successfully read Script
        // annotation REPLACES the candidate list: [0x18000d072]
        // MME_VectorClearTrivial empties sas+0xF8 immediately after
        // sub_18000EE00 succeeds (annotation present AND a string), before
        // the Technique= names are pushed back - techniques the script does
        // not name stay invisible to the selector (sub_18001DB50 iterates
        // this vector only). A missing / non-string Script keeps the full
        // declaration-order list from the enumeration above. An empty
        // Technique= list leaves the vector empty; the selector then
        // returns the null handle with found=1 [0x18001dbb1 -> LABEL_34].
        std::string script;
        if (SasGetAnnotationString(effect, param, "Script", 0, &script, sas)) {
            sas->techniqueOrder.clear();
            std::string normalized = SasNormalizeStandardsGlobalScript(script);
            if (SasParseStandardsGlobalScript(sas, normalized, pname)) {
                parseFailed = true;
                break;
            }
        }
        // [0x18000c5ce-0x18000c5f7] the entry scan breaks at the FIRST
        // STANDARDSGLOBAL parameter: after it is processed (successfully),
        // the original jumps straight to the parameter walk (LAB_18000e43a)
        // and never looks at another STANDARDSGLOBAL again - any additional
        // ones are silently ignored (no log; the parameter walk's tables do
        // not match the semantic either, so they register nothing).
        break;
    }

    if (parseFailed) {
        // [LAB_18000ea41] the failure path returns without building the model.
        // The error lines were appended to the parse model's log (a1+0x70 in
        // the original, which lives on in the loader's error report); hand
        // them out before SasUnload deletes the object.
        if (outFailureLog != nullptr) {
            *outFailureLog = sas->log;
        }
        SasUnload(sas);
        return nullptr;
    }

    // The MMD-standard convention (REFERENCE.txt Tips): a post effect is a
    // scene-class effect ordered postprocess.
    sas->postEffect = (sas->scriptClass != kSasClassObject) &&
                      (sas->scriptOrder == kSasOrderPostprocess);

    // Parameter validation walk [LAB_18000e43a].
    SasValidateParameters(sas);

    // Technique/pass model build [FUN_1800169d0 / FUN_180017a80]. The
    // techniqueOrder (every technique in declaration order, or ONLY the
    // STANDARDSGLOBAL Script= names when a valid Script annotation
    // replaced the list at 0x18000d072) fixes the enumeration order.
    // [sub_18000BC90
    // 0x18000c423-0x18000c42a] a nonzero sub_180016900 result (any
    // technique- or pass-scan error) makes the loader reject the effect -
    // the same treatment as a c470 error.
    std::vector<D3DXHANDLE> seen;
    for (size_t i = 0; i < sas->techniqueOrder.size(); ++i) {
        bool dup = false;
        for (size_t k = 0; k < seen.size(); ++k) {
            if (seen[k] == sas->techniqueOrder[i]) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            seen.push_back(sas->techniqueOrder[i]);
        }
    }
    for (size_t i = 0; i < seen.size(); ++i) {
        if (SasScanTechnique(sas, seen[i])) {
            // [sub_18000BC90 0x18000c423-0x18000c42a] same failure-report
            // treatment as the c470 walk: the log carries the exact scan
            // error lines the original's loader shows verbatim.
            if (outFailureLog != nullptr) {
                *outFailureLog = sas->log;
            }
            SasUnload(sas);
            return nullptr;
        }
    }

    // Create the D3D objects for resource textures eagerly (the original
    // defers some of this; creation here keeps SasApplyCommand simple).
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        SasEnsureResourceTexture(sas, &sas->resources[i]);
    }

    return sas;
}

void SasUnload(SasEffect* sas) {
    if (sas == nullptr) {
        return;
    }
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        if (sas->resources[i].surface != nullptr) {
            sas->resources[i].surface->Release();
        }
        if (sas->resources[i].offscreenDepth != nullptr) {
            // [FUN_18000b210 offscreen dtor sub_18000B750] the paired depth
            // stencil lives on the offscreen record and dies with it.
            sas->resources[i].offscreenDepth->Release();
        }
        if (sas->resources[i].texture != nullptr) {
            sas->resources[i].texture->Release();
        }
    }
    delete sas;
}

// [sub_180014270 0x1800142e7] the device-reset rebuild only touches
// records whose semantic is 0x26/0x27/0x2C/0x2E AND whose cached desc
// flag at +100 is set (= created in D3DPOOL_DEFAULT). MANAGED resources -
// every 0x2C file texture - survive the reset and are NOT reloaded from
// disk. (The broader sub_180016660 device-recreate path additionally
// resets 0x2D animated textures and calls effect->OnResetDevice; the
// port's anime_texture / effect_engine modules own those halves.)
static bool SasNeedsResetRebuild(const SasResource& res) {
    return res.defaultPool &&
           (res.semanticId == 0x26 || res.semanticId == 0x27 ||
            res.semanticId == 0x2C || res.semanticId == 0x2E);
}

void SasReleaseDeviceResources(SasEffect* sas) {
    if (sas == nullptr) {
        return;
    }
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        SasResource& res = sas->resources[i];
        if (!SasNeedsResetRebuild(res)) {
            continue;  // MANAGED / non-rebuildable: survives the reset
        }
        if (res.surface != nullptr) {
            res.surface->Release();
            res.surface = nullptr;
        }
        if (res.offscreenDepth != nullptr) {
            // The paired offscreen depth stencil is D3DPOOL_DEFAULT like the
            // render-target texture itself; sub_1800143D0's prologue drops
            // the offscreen record's +8 slot the same way before recreating.
            res.offscreenDepth->Release();
            res.offscreenDepth = nullptr;
        }
        if (res.texture != nullptr) {
            // [sub_1800143D0 prologue 0x180014434-0x180014441] before an
            // 0x27 rebuild the original ERASES the record's CURRENT texture
            // from the process-wide depth registry (sub_18001ED20 equal-range
            // erase keyed by the old pointer) and only then releases the old
            // D3D objects - a device-reset rebuild therefore never leaves a
            // stale key pointing at a released texture in the map. The port
            // releases eagerly here (before Reset), so the erase belongs at
            // the same point: drop the key BEFORE Release.
            if (res.semanticId == 0x27) {
                SasUnregisterDepthSurface(res.texture);
            }
            // Remove the effect's reference before releasing ours.
            if (sas->effect != nullptr && res.param != nullptr) {
                sas->effect->SetTexture(res.param, nullptr);
            }
            res.texture->Release();
            res.texture = nullptr;
        }
    }
}

void SasRecreateResources(SasEffect* sas, IDirect3DDevice9* device) {
    // Device-reset hook (PHASE3 integration note #7): the D3DPOOL_DEFAULT
    // resources (render targets / depth stencils) died with the reset -
    // [sub_180014270] only those are dropped and re-created, then the
    // texture parameters are re-bound. MANAGED resources stay as-is.
    if (sas == nullptr || device == nullptr) {
        return;
    }
    SasReleaseDeviceResources(sas);
    sas->device = device;
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        if (!SasNeedsResetRebuild(sas->resources[i])) {
            continue;
        }
        if (SasEnsureResourceTexture(sas, &sas->resources[i]) &&
            sas->resources[i].texture != nullptr) {
            sas->effect->SetTexture(sas->resources[i].param,
                                    sas->resources[i].texture);
        }
    }
}

bool SasHasPostEffect(const SasEffect* sas) {
    return sas != nullptr && sas->postEffect;
}

SasScriptClass SasGetScriptClass(const SasEffect* sas) {
    return sas != nullptr ? static_cast<SasScriptClass>(sas->scriptClass)
                          : kSasClassObject;
}

SasScriptOrder SasGetScriptOrder(const SasEffect* sas) {
    return sas != nullptr ? static_cast<SasScriptOrder>(sas->scriptOrder)
                          : kSasOrderStandard;
}

const char* SasGetLog(const SasEffect* sas) {
    static const char kEmpty[] = "";
    return (sas != nullptr) ? sas->log.c_str() : kEmpty;
}

bool SasHadErrors(const SasEffect* sas) {
    return sas != nullptr && sas->hasErrors;
}

int SasGetTechniqueCount(const SasEffect* sas) {
    return sas != nullptr ? static_cast<int>(sas->techniques.size()) : 0;
}

bool SasGetTechniqueInfo(const SasEffect* sas, int index, SasTechniqueInfo* out) {
    if (sas == nullptr || out == nullptr || index < 0 ||
        index >= static_cast<int>(sas->techniques.size())) {
        return false;
    }
    const SasTechnique& t = sas->techniques[static_cast<size_t>(index)];
    out->name = t.name.c_str();
    out->handle = t.handle;
    out->mmdPass = t.mmdPass;
    // Nonzero reports the filter state the original byte carries: true for
    // an explicit Use*=true annotation AND for the 0xFF wildcard default
    // (negative = "accepts any material", see SasTechnique).
    out->useTexture = (t.useTexture != 0);
    out->useSpheremap = (t.useSpheremap != 0);
    out->useToon = (t.useToon != 0);
    out->hardwareOk = t.hardwareOk;
    out->shaderMixOk = t.shaderMixOk;
    out->hasScript = t.hasScript;
    out->empty = t.empty;
    out->passCount = static_cast<int>(t.passes.size());
    return true;
}

bool SasGetPassInfo(const SasEffect* sas, int techIndex, int passIndex,
                    SasPassInfo* out) {
    if (sas == nullptr || out == nullptr || techIndex < 0 ||
        techIndex >= static_cast<int>(sas->techniques.size())) {
        return false;
    }
    const SasTechnique& t = sas->techniques[static_cast<size_t>(techIndex)];
    if (passIndex < 0 || passIndex >= static_cast<int>(t.passes.size())) {
        return false;
    }
    const SasPass& p = t.passes[static_cast<size_t>(passIndex)];
    out->name = p.name.c_str();
    out->handle = p.handle;
    out->index = p.index;
    out->hasScript = p.hasScript;
    out->hasCustomShaders = p.hasCustomShaders;
    out->needsPsize15 = p.needsPsize15;
    return true;
}

bool SasIsSubsetAllowed(const SasEffect* sas, int techIndex, int subset) {
    if (sas == nullptr || techIndex < 0 ||
        techIndex >= static_cast<int>(sas->techniques.size())) {
        return false;
    }
    const SasTechnique& t = sas->techniques[static_cast<size_t>(techIndex)];
    if (t.allSubsets) {
        return true;
    }
    for (std::map<int, int>::const_iterator it = t.subsets.begin();
         it != t.subsets.end(); ++it) {
        if (subset >= it->first && subset <= it->second) {
            return true;
        }
    }
    return false;
}

// [sub_18001DB50 0x18001DB50-0x18001DD1F] the technique selector. Ports the
// original's iteration over the sas+0xF8 order vector (the techniqueOrder
// here: every technique in declaration order, or - after a valid
// STANDARDSGLOBAL Script annotation [0x18000d072 clear] - ONLY the
// Technique= names it lists) and the per-record filter chain:
//   *(_DWORD*)record == drawMode   [0x18001dbcd]  MmdPass match
//   subset-range walk              [0x18001dc11-0x18001dc3e] ordered-map
//                                   membership; no Subset annotation (empty
//                                   map, record+64 == 0) skips the check
//   record+5/+6/+7 signed compare  [0x18001dc65-0x18001dcb1] Use* bytes:
//                                   negative (0xFF default) = wildcard,
//                                   otherwise (byte != 0) == material state
//   validity byte                   [0x18001dcb3] SkipValidation
//                                   (DAT_1800d99d9) selects the shader-caps
//                                   record+9 over the ValidateTechnique
//                                   record+8
// The FIRST valid match returns immediately; a matched-but-invalid technique
// is remembered (the LAST one wins) and returned only when the whole order
// was walked without a valid match - the original still selects (and draws)
// it, with the "found" byte cleared. The "Error: some techniques cannot run
// on this hardware:" report keyed off that byte in sub_18001DD20 is dead
// code in the shipped binary (its flag is only ever cleared - 0x18001DDBE /
// 0x18001E2E8 / 0x18001E3C8), so an invalid fallback never fails the load.
D3DXHANDLE SasSelectTechnique(const SasEffect* sas, int drawMode, int subset,
                              bool useTexture, bool useSpheremap, bool useToon,
                              bool* validMatch) {
    if (validMatch != nullptr) {
        *validMatch = true;   // the out "found" byte defaults to 1
    }
    // [0x18001db6d] no effect (or no SAS model) / negative subset: the null
    // handle with found = 1 - the caller draws through the host pipeline.
    if (sas == nullptr || sas->effect == nullptr || subset < 0) {
        return nullptr;
    }
    D3DXHANDLE fallback = nullptr;   // the last matched-but-invalid handle
    for (size_t o = 0; o < sas->techniqueOrder.size(); ++o) {
        // sub_18001F1A0: the map[handle] record lookup (inserts a wildcard
        // default record for an unknown handle). Every ordered handle here
        // was scanned, so the miss branch cannot fire; skip defensively.
        const SasTechnique* tech = nullptr;
        for (size_t t = 0; t < sas->techniques.size(); ++t) {
            if (sas->techniques[t].handle == sas->techniqueOrder[o]) {
                tech = &sas->techniques[t];
                break;
            }
        }
        if (tech == nullptr) {
            continue;
        }
        if (tech->mmdPass != drawMode) {
            continue;
        }
        if (!tech->allSubsets) {
            bool inRange = false;
            for (std::map<int, int>::const_iterator it = tech->subsets.begin();
                 it != tech->subsets.end(); ++it) {
                if (subset >= it->first && subset <= it->second) {
                    inRange = true;
                    break;
                }
            }
            if (!inRange) {
                continue;
            }
        }
        if (tech->useTexture >= 0 &&
            (tech->useTexture != 0) != useTexture) {
            continue;
        }
        if (tech->useSpheremap >= 0 &&
            (tech->useSpheremap != 0) != useSpheremap) {
            continue;
        }
        if (tech->useToon >= 0 && (tech->useToon != 0) != useToon) {
            continue;
        }
        const bool valid = g_skipValidation ? tech->shaderCapsOk
                                            : tech->hardwareOk;
        if (valid) {
            return tech->handle;
        }
        fallback = tech->handle;
    }
    if (fallback != nullptr && validMatch != nullptr) {
        *validMatch = false;
    }
    return fallback;
}

}  // namespace mme
