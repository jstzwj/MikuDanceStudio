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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cctype>

namespace mme {

// ===========================================================================
// Static validation tables transcribed from the original binary
// ===========================================================================

// [0x1800B2FA0..0x1800B36A8] semantic table, 56 entries of 32 bytes:
// { u32 id; char* name; u64 (class=lo32, type=hi32); u64 (rows*cols=lo32) }.
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
    {0x4E, "GroundShadowColor", 1, 3, 4}, {0x34, "matWorld", 2, 3, 16}
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

// ===========================================================================
// Annotation getters (FUN_18000ee00 port)
// ===========================================================================

// FUN_18000ee00: read a string annotation. mode 0 = silent, 1 = error when
// missing, 2 = warning when missing. Returns false when the annotation is
// absent or not a string ("Error: type of annotation '%s' is invalid.").
static bool SasGetAnnotationString(ID3DXEffect* effect, D3DXHANDLE param,
                                   const char* name, int mode,
                                   std::string* out, SasEffect* sas) {
    out->clear();
    D3DXHANDLE ann = effect->GetAnnotationByName(param, name);
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

// GetFloat on a named annotation (silent; used for Offset/Speed/etc.).
static bool SasGetAnnotationFloat(ID3DXEffect* effect, D3DXHANDLE param,
                                  const char* name, float* out) {
    D3DXHANDLE ann = effect->GetAnnotationByName(param, name);
    if (ann == nullptr) {
        return false;
    }
    return effect->GetFloat(ann, out) == S_OK;
}

static bool SasGetAnnotationInt(ID3DXEffect* effect, D3DXHANDLE param,
                                const char* name, int* out) {
    D3DXHANDLE ann = effect->GetAnnotationByName(param, name);
    if (ann == nullptr) {
        return false;
    }
    return effect->GetInt(ann, out) == S_OK;
}

static bool SasGetAnnotationBool(ID3DXEffect* effect, D3DXHANDLE param,
                                 const char* name, bool* out) {
    D3DXHANDLE ann = effect->GetAnnotationByName(param, name);
    if (ann == nullptr) {
        return false;
    }
    BOOL v = FALSE;
    if (effect->GetBool(ann, &v) != S_OK) {
        return false;
    }
    *out = (v != FALSE);
    return true;
}

// ===========================================================================
// Texture format name table [0x1800B2774..0x1800B29F8, FUN_180093660 pair]
// ===========================================================================

struct SasFormatEntry {
    const char* name;
    D3DFORMAT   format;
};

const SasFormatEntry kFormatTable[] = {
    {"A8R8G8B8", D3DFMT_A8R8G8B8}, {"X8R8G8B8", D3DFMT_X8R8G8B8},
    {"R8G8B8", D3DFMT_R8G8B8}, {"R5G6B5", D3DFMT_R5G6B5},
    {"X1R5G5B5", D3DFMT_X1R5G5B5}, {"A1R5G5B5", D3DFMT_A1R5G5B5},
    {"A4R4G4B4", D3DFMT_A4R4G4B4}, {"X4R4G4B4", D3DFMT_X4R4G4B4},
    {"A8B8G8R8", D3DFMT_A8B8G8R8}, {"X8B8G8R8", D3DFMT_X8B8G8R8},
    {"A2B10G10R10", D3DFMT_A2B10G10R10}, {"A2R10G10B10", D3DFMT_A2R10G10B10},
    {"G16R16", D3DFMT_G16R16}, {"A16B16G16R16", D3DFMT_A16B16G16R16},
    {"A16B16G16R16F", D3DFMT_A16B16G16R16F}, {"A32B32G32R32F", D3DFMT_A32B32G32R32F},
    {"G32R32F", D3DFMT_G32R32F}, {"G16R16F", D3DFMT_G16R16F},
    {"D16", D3DFMT_D16}, {"D24S8", D3DFMT_D24S8}, {"D24X8", D3DFMT_D24X8},
    {"D32", D3DFMT_D32}, {"L8", D3DFMT_L8}, {"A8", D3DFMT_A8},
    {"A8L8", D3DFMT_A8L8}, {"Q8W8V8U8", D3DFMT_Q8W8V8U8},
    {"V16U16", D3DFMT_V16U16}, {"UNKNOWN", D3DFMT_UNKNOWN}
};

static bool SasParseFormatName(const std::string& lower, D3DFORMAT* out) {
    for (size_t i = 0; i < sizeof(kFormatTable) / sizeof(kFormatTable[0]); ++i) {
        if (lower == ToLowerAscii(kFormatTable[i].name)) {
            *out = kFormatTable[i].format;
            return true;
        }
    }
    return false;
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

// The FUN_180011960 record build. semanticId: 0x26/0x27/0x2C/0x2D/0x2E/0x33.
// Returns false (and logs) on a hard error. UNCERTAIN: the original builds
// its record as a C++ object with boost::shared_ptr texture members; the
// std container here is a documented divergence.
static bool SasBuildResourceObject(SasEffect* sas, int semanticId,
                                   D3DXHANDLE param) {
    ID3DXEffect* effect = sas->effect;
    D3DXPARAMETER_DESC pd;
    if (effect->GetParameterDesc(param, &pd) != S_OK || pd.Name == nullptr) {
        return false;
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
            return false;
        }
        if (res.textureType != 5 && res.textureType != resolved) {
            SasLogFormat(sas,
                         "Error: annotation 'ResourceType' conflicts with type of "
                         "parameter.  (parameter: %s)\n",
                         pd.Name);
            sas->hasErrors = true;
            return false;
        }
        res.textureType = resolved;
    }

    // [L15457+] per-semantic texture-type checks.
    if (!SasIsTextureTypeAllowed(semanticId, res.textureType)) {
        SasLogFormat(sas, "Error: invalid texture type.  (parameter: %s)\n", pd.Name);
        sas->hasErrors = true;
        return false;
    }

    // [strings 0x1800B4248..0x1800B4280] dimension annotations are mutually
    // exclusive: ViewportRatio vs Dimensions vs Width/Height(/Depth).
    float viewportRatio[2] = {1.0f, 1.0f};
    bool hasViewportRatio = false;
    int dimensions[3] = {0, 0, 0};
    bool hasDimensions = false;
    int width = 0, height = 0, depth = 0;
    bool hasWHD = false;

    D3DXHANDLE ann = effect->GetAnnotationByName(param, "ViewportRatio");
    if (ann != nullptr) {
        D3DXVECTOR4 vec(0.0f, 0.0f, 0.0f, 0.0f);
        if (effect->GetVector(ann, &vec) == S_OK) {
            // ViewportRatio is float2; the first two components carry x/y.
            viewportRatio[0] = vec.x;
            viewportRatio[1] = vec.y;
            hasViewportRatio = true;
        }
    }
    ann = effect->GetAnnotationByName(param, "Dimensions");
    if (ann != nullptr) {
        int d[3] = {0, 0, 0};
        UINT n = 3;
        if (effect->GetIntArray(ann, d, n) != S_OK) {
            n = 2;
            if (effect->GetIntArray(ann, d, n) == S_OK) {
                d[2] = 1;
            } else {
                n = 0;
            }
        }
        if (n > 0) {
            dimensions[0] = d[0];
            dimensions[1] = d[1];
            dimensions[2] = (n > 2) ? d[2] : 1;
            hasDimensions = true;
        }
    }
    if (SasGetAnnotationInt(effect, param, "Width", &width) ||
        SasGetAnnotationInt(effect, param, "Height", &height) ||
        SasGetAnnotationInt(effect, param, "Depth", &depth)) {
        hasWHD = true;
    }
    int groupCount = (hasViewportRatio ? 1 : 0) + (hasDimensions ? 1 : 0) +
                     (hasWHD ? 1 : 0);
    if (groupCount > 1) {
        SasLogFormat(sas,
                     "Error: annotations 'ViewportRatio', 'Dimensions' and "
                     "('Width','Height','Depth') are mutually exclusive.  "
                     "(parameter: '%s'\n",
                     pd.Name);
        sas->hasErrors = true;
        return false;
    }

    // Resolve the requested pixel size. ViewportRatio scales the current
    // backbuffer size (REFERENCE.txt: default {1.0,1.0}). UNCERTAIN: the
    // original reads the host's screen-size globals rather than
    // GetViewport; the observable default (1.0x of the target) is kept.
    int screenW = 64, screenH = 64;
    if (sas->device != nullptr) {
        D3DVIEWPORT9 vp;
        if (sas->device->GetViewport(&vp) == S_OK && vp.Width > 0 && vp.Height > 0) {
            screenW = static_cast<int>(vp.Width);
            screenH = static_cast<int>(vp.Height);
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

    // [0x1800B42F8/0x1800B4304] Miplevels/Levels: 0 = full chain, 1 = none.
    int mip = 1;
    if (SasGetAnnotationInt(effect, param, "Miplevels", &mip) ||
        SasGetAnnotationInt(effect, param, "Levels", &mip)) {
        res.mipLevels = mip;
    }

    // [0x1800B430C] "Format" annotation, "D3DFMT_"-prefixed names accepted
    // (the original matches after stripping the prefix; see strings table).
    std::string formatName;
    if (SasGetAnnotationString(effect, param, "Format", 0, &formatName, sas)) {
        std::string lower = ToLowerAscii(formatName);
        const std::string prefix = "d3dfmt_";
        if (lower.compare(0, prefix.size(), prefix) == 0) {
            lower = lower.substr(prefix.size());
        }
        D3DFORMAT fmt = D3DFMT_UNKNOWN;
        if (!SasParseFormatName(lower, &fmt)) {
            SasLogFormat(sas, "Error: unknown texture format: %s\n", formatName.c_str());
            sas->hasErrors = true;
            return false;
        }
        res.format = fmt;
    }
    if (res.format == D3DFMT_UNKNOWN) {
        // REFERENCE.txt defaults: A8R8G8B8 for color targets,
        // D24S8 for depth stencil targets.
        res.format = (semanticId == 0x27) ? D3DFMT_D24S8 : D3DFMT_A8R8G8B8;
    }

    // [0x1800B4430/0x1800B4440] ClearColor / ClearDepth / AntiAlias.
    D3DXHANDLE cc = effect->GetAnnotationByName(param, "ClearColor");
    if (cc != nullptr) {
        float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        D3DXVECTOR4 vec(0.0f, 0.0f, 0.0f, 0.0f);
        if (effect->GetVector(cc, &vec) == S_OK) {
            res.clearColor[0] = vec.x;
            res.clearColor[1] = vec.y;
            res.clearColor[2] = vec.z;
            res.clearColor[3] = vec.w;
            res.hasClearColor = true;
        }
    }
    float cd = 0.0f;
    if (SasGetAnnotationFloat(effect, param, "ClearDepth", &cd)) {
        res.clearDepth = cd;
        res.hasClearDepth = true;
    }
    SasGetAnnotationBool(effect, param, "AntiAlias", &res.antiAlias);

    // [0x1800B43D0/0x1800B43E8] Description / DefaultEffect (offscreen only).
    if (semanticId == 0x2E) {
        SasGetAnnotationString(effect, param, "Description", 0, &res.description, sas);
        SasGetAnnotationString(effect, param, "DefaultEffect", 0, &res.defaultEffect, sas);
        // [0x1800B43A8] "Info: new OffScreen RenderTarget: "
        SasLogFormat(sas, "Info: new OffScreen RenderTarget: %s\n", pd.Name);
    }

    // [0x1800B3FF8/0x1800B4050/0x1800B4080/0x1800B4088] ANIMATEDTEXTURE.
    if (semanticId == 0x2D) {
        SasGetAnnotationString(effect, param, "ResourceName", 2, &res.resourceName, sas);
        SasGetAnnotationFloat(effect, param, "Offset", &res.offset);
        SasGetAnnotationFloat(effect, param, "Speed", &res.speed);
        SasGetAnnotationString(effect, param, "SeekVariable", 0, &res.seekVariable, sas);
    }

    // [0x1800B4118] TEXTUREVALUE.
    if (semanticId == 0x33) {
        SasGetAnnotationString(effect, param, "TextureName", 1, &res.textureValueName, sas);
    }

    // [0x1800B4358/0x1800B4364/0x1800B436C] texture-shader annotations
    // ("Function"/"Target" with tx_1_0) - Phase 3b stub: accepted, unused.
    sas->resources.push_back(res);
    return true;
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
        case 0x2D:  // AnimatedTexture
        case 0x2E:  // OffScreenRenderTarget
        case 0x29:  // MaterialTexture / MaterialSphereMap / MaterialToonTexture
        case 0x2A:
        case 0x2B:
        case 0x33:  // TextureValue (Phase 3b fills the texel array)
            if (!SasBuildResourceObject(sas, semanticId, param)) {
                return 1;
            }
            return 0;
        default:
            // Matrices/vectors/scalars: no SAS-side object. UNCERTAIN: the
            // original may record these in its runtime-value table; the Phase
            // 2 binder covers the observable behavior.
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
//     the name table (B) is strict (UNCERTAIN: no exception branches are
//     visible in the B branch of the decompile)
//   - arrays (Elements != 0) are rejected except TextureValue (0x33)
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

    // Arrays are invalid except TextureValue.
    if (pd.Elements != 0 && e.id != 0x33) {
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
static void SasParseStandardsGlobalScript(SasEffect* sas,
                                          const std::string& script,
                                          const char* paramName) {
    std::string rest = script;
    bool matchedAnything = false;
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
        matchedAnything = true;
        rest = remainder;
        if (ToLowerAscii(command) != "technique") {
            // [0x1800B3DB0] "Warning: unsupported script command: "
            SasLogFormat(sas,
                         "Warning: unsupported script command: %s  (parameter: %s)\n",
                         whole.c_str(), paramName);
            continue;
        }
        // Single form "Technique=MainTech" or chain "Technique=A?B:C"
        // [LAB_18000da09]: the name list after '?' is ':' separated and tried
        // in order; every name that resolves is pushed to techniqueOrder.
        std::string list = value;
        size_t qpos = list.find('?');
        if (qpos != std::string::npos) {
            list = list.substr(qpos + 1);  // [L12838-12845] erase through '?'
        }
        while (!list.empty()) {
            size_t colon = list.find(':');
            std::string name = (colon == std::string::npos) ? list : list.substr(0, colon);
            if (!name.empty()) {
                D3DXHANDLE h = sas->effect->GetTechniqueByName(name.c_str());
                if (h != nullptr) {
                    sas->techniqueOrder.push_back(h);
                } else {
                    // [0x1800B3D90 region] "Warning: unknown technique name: "
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
    if (!matchedAnything && !rest.empty()) {
        // [LAB_18000e1a0 c470 L12684] "Error: script syntax error: " with the
        // unparsed remainder (the original erases up to the first ';').
        size_t semi = rest.find(';');
        std::string tail = (semi == std::string::npos) ? rest : rest.substr(0, semi);
        SasLogFormat(sas, "Error: script syntax error: %s  (parameter: %s)\n",
                     tail.c_str(), paramName);
        sas->hasErrors = true;
    }
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

        // Table A: semantic match (_stricmp) [L12434-12438].
        for (size_t k = 0; k < sizeof(kSemanticTable) / sizeof(kSemanticTable[0]); ++k) {
            if (_stricmp(semantic, kSemanticTable[k].name) != 0) {
                continue;
            }
            matched = true;
            semanticId = kSemanticTable[k].id;
            if (!SasValidateAgainstEntry(kSemanticTable[k], pd, true)) {
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

        // Table B: name match [L12559-12576]; strict, no exceptions.
        if (!matched) {
            for (size_t k = 0; k < sizeof(kNameTable) / sizeof(kNameTable[0]); ++k) {
                if (_stricmp(name, kNameTable[k].name) != 0) {
                    continue;
                }
                matched = true;
                semanticId = kNameTable[k].id;
                if (!SasValidateAgainstEntry(kNameTable[k], pd, false)) {
                    invalid = true;
                    // [L12593-12596] "Error: type of parameter '%s' is invalid.\n"
                    SasLogFormat(sas, "Error: type of parameter '%s' is invalid.\n", name);
                } else if (SasRegisterSemantic(sas, semanticId, param) != 0) {
                    invalid = true;
                }
                break;
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

        if (invalid) {
            sas->hasErrors = true;
        }
    }
}

// ===========================================================================
// FUN_1800143d0 - texture creation + capability checks
// ===========================================================================

// Create the D3D texture for a resource object. Ports the observable part of
// FUN_1800143d0: capability-driven size/format adjustment with the
// "%dx%d(%s) -> %dx%d(%s)" warning, then D3DXCreateTexture /
// CreateDepthStencilSurface-class creation. UNCERTAIN: volume/cube creation
// and ANIMATEDTEXTURE loading live in Phase 3b (anime_texture); this port
// creates 2D textures and depth surfaces only.
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
        // Depth stencil surface (RENDERDEPTHSTENCILTARGET): not readable
        // afterwards (REFERENCE.txt note), so no texture object is kept.
        IDirect3DSurface9* surf = nullptr;
        HRESULT hr = device->CreateDepthStencilSurface(
            static_cast<UINT>(w), static_cast<UINT>(h), fmt, D3DMULTISAMPLE_NONE, 0,
            FALSE, &surf, nullptr);
        if (hr != S_OK || surf == nullptr) {
            SasLogFormat(sas, "Error: failed to create texture (parameter: '%s').\n",
                         res->name.c_str());
            sas->hasErrors = true;
            return false;
        }
        res->surface = surf;
    } else {
        bool isRenderTarget = (res->semanticId == 0x26 || res->semanticId == 0x2E);
        DWORD usage = isRenderTarget ? D3DUSAGE_RENDERTARGET : 0;
        D3DPOOL pool = isRenderTarget ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        UINT mip = (res->mipLevels == 0) ? 0u : 1u;  // 0 = full chain, else 1
        if (res->textureType == 8 || res->textureType == 9) {
            // Volume / cube resources are Phase 3b (anime_texture owns the
            // animated variants); log and skip the creation for now.
            return true;
        }
        IDirect3DTexture9* tex = nullptr;
        HRESULT hr = D3DXCreateTexture(device, static_cast<UINT>(w),
                                       static_cast<UINT>(h), mip, usage, fmt, pool,
                                       &tex);
        if (hr != S_OK || tex == nullptr) {
            SasLogFormat(sas, "Error: failed to create texture (parameter: '%s').\n",
                         res->name.c_str());
            sas->hasErrors = true;
            return false;
        }
        D3DSURFACE_DESC desc;
        if (tex->GetLevelDesc(0, &desc) == S_OK) {
            w = static_cast<int>(desc.Width);
            h = static_cast<int>(desc.Height);
            fmt = desc.Format;
        }
        res->texture = tex;
        if (res->textureType == 5 || res->textureType == 7) {
            tex->GetSurfaceLevel(0, &res->surface);
        }
    }

    if (origW != w || origH != h || origFmt != fmt) {
        // "%dx%d(%s) -> %dx%d(%s)" + "Warning: texture format of '%s' was
        // automatically adjusted: " [0x1800B4460..0x1800B4498]
        char from[64], to[64];
        _snprintf_s(from, sizeof(from), _TRUNCATE, "%dx%d", origW, origH);
        _snprintf_s(to, sizeof(to), _TRUNCATE, "%dx%d", w, h);
        SasLogFormat(sas, "Warning: texture format of '%s' was automatically adjusted: "
                          "%s -> %s\n",
                     res->name.c_str(), from, to);
    }
    return true;
}

// ===========================================================================
// FUN_1800169d0 - technique scan / FUN_180017a80 - pass scan
// ===========================================================================

// Parse a "Subset" annotation value ("0-4,7,9-" style) into the range map
// [L18856-18941]. Ranges are inclusive; "9-" means 9..0x7fffffff; an empty or
// fully-consumed spec with no ranges means "all subsets" (the original adds
// the 0x7fffffff sentinel pair).
static void SasParseSubsetRanges(SasTechnique* tech, const std::string& value) {
    tech->subsets.clear();
    tech->allSubsets = false;
    const char* p = value.c_str();
    long start = -1;
    bool any = false;
    while (*p != '\0') {
        if (isspace(static_cast<unsigned char>(*p))) {
            ++p;
            continue;
        }
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '-') {
            // Open-ended range "N-".
            if (start >= 0) {
                tech->subsets[static_cast<int>(start)] = 0x7fffffff;
                any = true;
                start = -1;
            } else {
                tech->allSubsets = true;  // malformed leading '-' -> all
                return;
            }
            ++p;
            continue;
        }
        char* end = nullptr;
        long v = strtol(p, &end, 0);
        if (end == p || v < 0) {
            // [L18999] "Error: invalid subset range: '%s' (technique: %s)\n"
            // is raised by the caller when parsing fails mid-string.
            tech->allSubsets = true;
            return;
        }
        p = end;
        if (start >= 0) {
            tech->subsets[static_cast<int>(start)] = static_cast<int>(v);
            any = true;
            start = -1;
        } else if (*p == '-') {
            start = v;
        } else {
            tech->subsets[static_cast<int>(v)] = static_cast<int>(v);
            any = true;
        }
    }
    if (start >= 0) {
        tech->subsets[static_cast<int>(start)] = 0x7fffffff;
        any = true;
    }
    if (!any) {
        // [L18989-18994] sentinel pair meaning "no restriction".
        tech->subsets[0x7fffffff] = 0x7fffffff;
        tech->allSubsets = true;
    }
}

// FUN_1800169d0: scan one technique (annotations + validation flags).
static void SasScanTechnique(SasEffect* sas, D3DXHANDLE hTech) {
    ID3DXEffect* effect = sas->effect;
    SasTechnique tech;
    tech.handle = hTech;
    D3DXTECHNIQUE_DESC td;
    if (effect->GetTechniqueDesc(hTech, &td) != S_OK) {
        return;
    }
    tech.name = (td.Name != nullptr) ? td.Name : "";
    tech.empty = (td.Passes == 0);  // "empty technique" convention

    // [L18482-18485] hardware + shader-mix validity flags.
    tech.hardwareOk = (effect->ValidateTechnique(hTech) == S_OK);
    // FUN_1800167d0's vs_3_0/ps_3_0 mix check runs per pass (FUN_180017a80);
    // the technique flag aggregates it.
    tech.shaderMixOk = true;

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
                continue;
            }
            const std::string value = s;
            const std::string lower = ToLowerAscii(value);
            if (lower == "object") {
                tech.mmdPass = kSasPassObject;
            } else if (lower == "object_ss") {
                tech.mmdPass = kSasPassObjectSS;
            } else if (lower == "edge") {
                tech.mmdPass = kSasPassZplot;  // [0x1800B456C] "edge" = mode 3
            } else if (lower == "shadow") {
                tech.mmdPass = kSasPassShadow;
            } else if (lower == "zplot") {
                tech.mmdPass = kSasPassZplot;
            } else {
                // [L18696-18701] "Error: unknown pass mode: %s (technique: %s)\n"
                SasLogFormat(sas, "Error: unknown pass mode: %s  (technique: %s)\n",
                             value.c_str(), tech.name.c_str());
                sas->hasErrors = true;
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
                continue;
            }
            if (_stricmp(ad.Name, "UseTexture") == 0) {
                tech.useTexture = (v != FALSE);
            } else if (_stricmp(ad.Name, "UseSpheremap") == 0) {
                tech.useSpheremap = (v != FALSE);
            } else {
                tech.useToon = (v != FALSE);
            }
        } else if (_stricmp(ad.Name, "Subset") == 0) {
            LPCSTR s = nullptr;
            if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                SasLogFormat(sas, "Error: type of annotation 'Subset' is invalid. "
                                  "(technique: %s)\n",
                             tech.name.c_str());
                sas->hasErrors = true;
                continue;
            }
            SasParseSubsetRanges(&tech, s);
        } else if (_stricmp(ad.Name, "Script") == 0) {
            LPCSTR s = nullptr;
            if (effect->GetString(ann, &s) != S_OK || s == nullptr) {
                SasLogFormat(sas, "Error: type of annotation 'Script' is invalid. "
                                  "(technique: %s)\n",
                             tech.name.c_str());
                sas->hasErrors = true;
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
            // The token streams in D3DXPASS_DESC are only populated when the
            // effect was created without cloning (D3DXFX_NOT_CLONEABLE);
            // UNCERTAIN: the original's effect creation flags may null them.
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
            unsigned int vsVer = (pdd.pVertexShaderFunction != nullptr)
                                     ? D3DXGetShaderVersion(pdd.pVertexShaderFunction)
                                     : 0u;
            unsigned int psVer = (pdd.pPixelShaderFunction != nullptr)
                                     ? D3DXGetShaderVersion(pdd.pPixelShaderFunction)
                                     : 0u;
            // [L19143] (vs < 3.0 && ps < 3.0) || vs == ps -> ok
            bool mixOk = ((vsVer < 0x300u) && (psVer < 0x300u)) || (vsVer == psVer);
            pass.shaderMixOk = mixOk;
            if (!mixOk) {
                tech.shaderMixOk = false;
                // [0x1800B4630] "Error: vs_3_0 or ps_3_0 may not be used with
                // any other shader versions. (pass: %s, technique: %s)\n"
                // The original also raises a MessageBoxA here; the parent owns
                // UI, so this port logs only (documented divergence).
                SasLogFormat(sas,
                             "Error: vs_3_0 or ps_3_0 may not be used with any other "
                             "shader versions. (pass: %s, technique: %s)\n",
                             pass.name.c_str(), tech.name.c_str());
                sas->hasErrors = true;
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
        SasCompileScript(sas, script, hTech, nullptr, tech.name, "",
                         &sas->techniques.back().cmds);
    }
    for (size_t p = 0; p < sas->techniques.back().passes.size(); ++p) {
        SasPass& pass = sas->techniques.back().passes[p];
        if (pass.hasScript) {
            SasCompileScript(sas, pass.scriptText, hTech, pass.handle, tech.name,
                             pass.name, &pass.cmds);
        }
    }
}

// ===========================================================================
// Public API
// ===========================================================================

SasEffect* SasParse(ID3DXEffect* effect, const std::string& pathAnsi,
                    IDirect3DDevice9* device) {
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
    // technique handle in declaration order goes into techniqueOrder.
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

        // Script [L12284+]: the technique search order.
        std::string script;
        if (SasGetAnnotationString(effect, param, "Script", 0, &script, sas)) {
            std::string normalized = SasNormalizeStandardsGlobalScript(script);
            SasParseStandardsGlobalScript(sas, normalized, pname);
        }
    }

    if (parseFailed) {
        // [LAB_18000ea41] the failure path returns without building the model.
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
    // techniqueOrder (STANDARDSGLOBAL Script= names first, declaration order
    // remainder) fixes the enumeration order.
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
        SasScanTechnique(sas, seen[i]);
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
        if (sas->resources[i].texture != nullptr) {
            sas->resources[i].texture->Release();
        }
    }
    delete sas;
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
    out->useTexture = t.useTexture;
    out->useSpheremap = t.useSpheremap;
    out->useToon = t.useToon;
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

}  // namespace mme
