// Shared D3DX9 declarations for the host and built-in effect engine.
// The COM layouts match the Microsoft D3DX9 ABI. Free functions are linked
// through a normal generated import library, requiring the original runtime
// at process load (x64: d3dx9_43.dll; x86: d3dx9_32.dll).
#ifndef MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_
#define MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_

#include <d3d9.h>
#include <cstddef>

struct ID3DXFile;
extern "C" HRESULT WINAPI D3DXFileCreate(ID3DXFile** file);

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------
typedef const char* D3DXHANDLE;

enum D3DXIMAGE_FILEFORMAT {
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_HDR = 7,
    D3DXIFF_PFM = 8,
    D3DXIFF_FORCE_DWORD = 0x7FFFFFFF
};

struct D3DXIMAGE_INFO {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    D3DXIMAGE_FILEFORMAT ImageFileFormat;
};

// D3DX writes all seven fields, even when a caller only needs the dimensions.
// The SDK layout is identical on x86 and x64; a four-UINT prefix is too small.
static_assert(sizeof(D3DXIMAGE_FILEFORMAT) == 4);
static_assert(sizeof(D3DXIMAGE_INFO) == 28);
static_assert(offsetof(D3DXIMAGE_INFO, Format) == 16);
static_assert(offsetof(D3DXIMAGE_INFO, ResourceType) == 20);
static_assert(offsetof(D3DXIMAGE_INFO, ImageFileFormat) == 24);

struct D3DXVECTOR2 {
    float x, y;
    D3DXVECTOR2() {}
    D3DXVECTOR2(float fx, float fy) : x(fx), y(fy) {}
};

struct D3DXVECTOR3 {
    float x, y, z;
    D3DXVECTOR3() {}
    D3DXVECTOR3(float fx, float fy, float fz) : x(fx), y(fy), z(fz) {}
};

struct D3DXVECTOR4 {
    float x, y, z, w;
    D3DXVECTOR4() {}
    D3DXVECTOR4(float fx, float fy, float fz, float fw)
        : x(fx), y(fy), z(fz), w(fw) {}
};

struct D3DXMATRIX : public D3DMATRIX {
    D3DXMATRIX() {}
    D3DXMATRIX(const D3DMATRIX& m) { *static_cast<D3DMATRIX*>(this) = m; }
};

struct D3DXMACRO {
    const char* Name;
    const char* Definition;
};

struct D3DXMATERIAL {
    D3DMATERIAL9 MatD3D;
    LPSTR pTextureFilename;
};

// ID3DXInclude is passed only as nullptr to D3DXCreateEffectFromFileW.
struct ID3DXInclude {
    virtual HRESULT __stdcall Open(void) = 0;
    virtual HRESULT __stdcall Close(void) = 0;
};

// ---------------------------------------------------------------------------
// ID3DXBuffer（d3dx9core.h：IUnknown + GetBufferPointer/GetBufferSize）
// ---------------------------------------------------------------------------
struct ID3DXBuffer : public IUnknown {
    virtual void* __stdcall GetBufferPointer() = 0;
    virtual unsigned long __stdcall GetBufferSize() = 0;
};

// ID3DXBaseMesh / ID3DXMesh, declaration order and signatures verified against
// the June 2010 SDK d3dx9mesh.h. No device or mesh vtable interception is used.
struct D3DXATTRIBUTERANGE {
    DWORD AttribId, FaceStart, FaceCount, VertexStart, VertexCount;
};
struct ID3DXMesh;
struct ID3DXBaseMesh : public IUnknown {
    virtual HRESULT __stdcall DrawSubset(DWORD attribute) = 0;
    virtual DWORD __stdcall GetNumFaces() = 0;
    virtual DWORD __stdcall GetNumVertices() = 0;
    virtual DWORD __stdcall GetFVF() = 0;
    virtual HRESULT __stdcall GetDeclaration(D3DVERTEXELEMENT9* declaration) = 0;
    virtual DWORD __stdcall GetNumBytesPerVertex() = 0;
    virtual DWORD __stdcall GetOptions() = 0;
    virtual HRESULT __stdcall GetDevice(IDirect3DDevice9** device) = 0;
    virtual HRESULT __stdcall CloneMeshFVF(DWORD options, DWORD fvf,
        IDirect3DDevice9* device, ID3DXMesh** mesh) = 0;
    virtual HRESULT __stdcall CloneMesh(DWORD options, const D3DVERTEXELEMENT9* declaration,
        IDirect3DDevice9* device, ID3DXMesh** mesh) = 0;
    virtual HRESULT __stdcall GetVertexBuffer(IDirect3DVertexBuffer9** buffer) = 0;
    virtual HRESULT __stdcall GetIndexBuffer(IDirect3DIndexBuffer9** buffer) = 0;
    virtual HRESULT __stdcall LockVertexBuffer(DWORD flags, void** data) = 0;
    virtual HRESULT __stdcall UnlockVertexBuffer() = 0;
    virtual HRESULT __stdcall LockIndexBuffer(DWORD flags, void** data) = 0;
    virtual HRESULT __stdcall UnlockIndexBuffer() = 0;
    virtual HRESULT __stdcall GetAttributeTable(D3DXATTRIBUTERANGE* table, DWORD* size) = 0;
    virtual HRESULT __stdcall ConvertPointRepsToAdjacency(const DWORD* points, DWORD* adjacency) = 0;
    virtual HRESULT __stdcall ConvertAdjacencyToPointReps(const DWORD* adjacency, DWORD* points) = 0;
    virtual HRESULT __stdcall GenerateAdjacency(FLOAT epsilon, DWORD* adjacency) = 0;
    virtual HRESULT __stdcall UpdateSemantics(D3DVERTEXELEMENT9* declaration) = 0;
};
struct ID3DXMesh : public ID3DXBaseMesh {
    virtual HRESULT __stdcall LockAttributeBuffer(DWORD flags, DWORD** data) = 0;
    virtual HRESULT __stdcall UnlockAttributeBuffer() = 0;
    virtual HRESULT __stdcall Optimize(DWORD flags, const DWORD* adjacencyIn,
        DWORD* adjacencyOut, DWORD* faceRemap, ID3DXBuffer** vertexRemap, ID3DXMesh** mesh) = 0;
    virtual HRESULT __stdcall OptimizeInplace(DWORD flags, const DWORD* adjacencyIn,
        DWORD* adjacencyOut, DWORD* faceRemap, ID3DXBuffer** vertexRemap) = 0;
    virtual HRESULT __stdcall SetAttributeTable(const D3DXATTRIBUTERANGE* table, DWORD size) = 0;
};

// ID3DXEffectPool: effect-sharing pool, used only as an opaque COM object.
struct ID3DXEffectPool : public IUnknown {};

// ID3DXTextureShader: texture shader object created by D3DXCreateTextureShader in the 0x2C Function
// generation branch (sub_1800143D0 case ','). The original immediately Releases it afterward,
// without retaining it for reuse (LABEL_326), so an opaque pointer suffices for the Fill*TX signatures.
struct ID3DXTextureShader : public IUnknown {};

// ---------------------------------------------------------------------------
// Descriptors (d3dx9effect.h / d3dx9shader.h layouts)
// ---------------------------------------------------------------------------
typedef enum D3DXPARAMETER_CLASS {
    D3DXPC_SCALAR = 0,
    D3DXPC_VECTOR = 1,
    D3DXPC_MATRIX_ROWS = 2,
    D3DXPC_MATRIX_COLUMNS = 3,
    D3DXPC_OBJECT = 4,
    D3DXPC_STRUCT = 5,
    D3DXPC_FORCE_DWORD = 0x7fffffff
} D3DXPARAMETER_CLASS;

typedef enum D3DXPARAMETER_TYPE {
    D3DXPT_VOID = 0,
    D3DXPT_BOOL = 1,
    D3DXPT_INT = 2,
    D3DXPT_FLOAT = 3,
    D3DXPT_STRING = 4,
    D3DXPT_TEXTURE = 5,
    D3DXPT_TEXTURE1D = 6,
    D3DXPT_TEXTURE2D = 7,
    D3DXPT_TEXTURE3D = 8,
    D3DXPT_TEXTURECUBE = 9,
    D3DXPT_SAMPLER = 10,
    D3DXPT_SAMPLER1D = 11,
    D3DXPT_SAMPLER2D = 12,
    D3DXPT_SAMPLER3D = 13,
    D3DXPT_SAMPLERCUBE = 14,
    D3DXPT_PIXELSHADER = 15,
    D3DXPT_VERTEXSHADER = 16,
    D3DXPT_PIXELFRAGMENT = 17,
    D3DXPT_VERTEXFRAGMENT = 18,
    D3DXPT_UNSUPPORTED = 19,
    D3DXPT_FORCE_DWORD = 0x7fffffff
} D3DXPARAMETER_TYPE;

struct D3DXEFFECT_DESC {
    const char* Creator;
    unsigned int Parameters;
    unsigned int Techniques;
    unsigned int Functions;
};

#define D3DX_PARAMETER_SHARED (1 << 0)
#define D3DX_PARAMETER_LITERAL (1 << 1)
#define D3DX_PARAMETER_ANNOTATION (1 << 2)
struct D3DXPARAMETER_DESC {
    const char* Name;
    const char* Semantic;
    D3DXPARAMETER_CLASS Class;
    D3DXPARAMETER_TYPE Type;
    unsigned int Rows;
    unsigned int Columns;
    unsigned int Elements;
    unsigned int Annotations;
    unsigned int StructMembers;
    unsigned long Flags;
    unsigned int Bytes;
};

struct D3DXTECHNIQUE_DESC {
    const char* Name;
    unsigned int Passes;
    unsigned int Annotations;
};

struct D3DXPASS_DESC {
    const char* Name;
    unsigned int Annotations;
    const unsigned long* pVertexShaderFunction;
    const unsigned long* pPixelShaderFunction;
};

struct D3DXFUNCTION_DESC {
    const char* Name;
    unsigned int Annotations;
};

struct D3DXSEMANTIC {
    unsigned int Usage;
    unsigned int UsageIndex;
};

// ---------------------------------------------------------------------------
// Effect compilation flags (d3dx9effect.h)
// ---------------------------------------------------------------------------
#define D3DXFX_DONOTSAVESTATE        (1 << 0)
#define D3DXFX_DONOTSAVESHADERSTATE  (1 << 1)
#define D3DXFX_DONOTSAVESAMPLERSTATE (1 << 2)
#define D3DXFX_NOT_CLONEABLE         (1 << 11)
#define D3DXFX_LARGEADDRESSAWARE     (1 << 17)

// ---------------------------------------------------------------------------
// Complete ID3DXEffect / ID3DXBaseEffect mirror
// ---------------------------------------------------------------------------
// Slots 3..56 = ID3DXBaseEffect's 54 methods; 57..78 = ID3DXEffect's additional
// 22 methods. The order is fixed by the ABI (see the file header).
struct ID3DXEffect : public IUnknown {
    // --- ID3DXBaseEffect (slots 3..56) --------------------------------------
    virtual HRESULT __stdcall GetDesc(D3DXEFFECT_DESC* desc) = 0;                 // 3
    virtual HRESULT __stdcall GetParameterDesc(D3DXHANDLE parameter,
                                               D3DXPARAMETER_DESC* desc) = 0;    // 4
    virtual HRESULT __stdcall GetTechniqueDesc(D3DXHANDLE technique,
                                               D3DXTECHNIQUE_DESC* desc) = 0;    // 5
    virtual HRESULT __stdcall GetPassDesc(D3DXHANDLE pass,
                                          D3DXPASS_DESC* desc) = 0;              // 6
    virtual HRESULT __stdcall GetFunctionDesc(D3DXHANDLE function,
                                              void* desc) = 0;                   // 7
    virtual D3DXHANDLE __stdcall GetParameter(D3DXHANDLE object,
                                              unsigned int index) = 0;           // 8
    virtual D3DXHANDLE __stdcall GetParameterByName(D3DXHANDLE object,
                                                    const char* name) = 0;       // 9
    virtual D3DXHANDLE __stdcall GetParameterBySemantic(D3DXHANDLE object,
                                                        const char* semantic) = 0;// 10
    virtual D3DXHANDLE __stdcall GetParameterElement(D3DXHANDLE array,
                                                     unsigned int index) = 0;    // 11
    virtual D3DXHANDLE __stdcall GetTechnique(unsigned int index) = 0;            // 12
    virtual D3DXHANDLE __stdcall GetTechniqueByName(const char* name) = 0;        // 13
    virtual D3DXHANDLE __stdcall GetPass(D3DXHANDLE technique,
                                        unsigned int index) = 0;                 // 14
    virtual D3DXHANDLE __stdcall GetPassByName(D3DXHANDLE technique,
                                               const char* name) = 0;             // 15
    virtual D3DXHANDLE __stdcall GetFunction(unsigned int index) = 0;             // 16
    virtual D3DXHANDLE __stdcall GetFunctionByName(const char* name) = 0;         // 17
    virtual D3DXHANDLE __stdcall GetAnnotation(D3DXHANDLE object,
                                               unsigned int index) = 0;          // 18
    virtual D3DXHANDLE __stdcall GetAnnotationByName(D3DXHANDLE object,
                                                     const char* name) = 0;      // 19
    virtual HRESULT __stdcall SetValue(D3DXHANDLE parameter, const void* data,
                                       unsigned int bytes) = 0;                  // 20
    virtual HRESULT __stdcall GetValue(D3DXHANDLE parameter, void* data,
                                       unsigned int bytes) = 0;                  // 21
    virtual HRESULT __stdcall SetBool(D3DXHANDLE parameter, BOOL value) = 0;      // 22
    virtual HRESULT __stdcall GetBool(D3DXHANDLE parameter, BOOL* value) = 0;     // 23
    virtual HRESULT __stdcall SetBoolArray(D3DXHANDLE parameter, const BOOL* values,
                                           unsigned int count) = 0;               // 24
    virtual HRESULT __stdcall GetBoolArray(D3DXHANDLE parameter, BOOL* values,
                                           unsigned int count) = 0;               // 25
    virtual HRESULT __stdcall SetInt(D3DXHANDLE parameter, int value) = 0;        // 26
    virtual HRESULT __stdcall GetInt(D3DXHANDLE parameter, int* value) = 0;       // 27
    virtual HRESULT __stdcall SetIntArray(D3DXHANDLE parameter, const int* values,
                                          unsigned int count) = 0;                // 28
    virtual HRESULT __stdcall GetIntArray(D3DXHANDLE parameter, int* values,
                                          unsigned int count) = 0;                // 29
    virtual HRESULT __stdcall SetFloat(D3DXHANDLE parameter, float value) = 0;    // 30
    virtual HRESULT __stdcall GetFloat(D3DXHANDLE parameter, float* value) = 0;   // 31
    virtual HRESULT __stdcall SetFloatArray(D3DXHANDLE parameter, const float* values,
                                            unsigned int count) = 0;              // 32
    virtual HRESULT __stdcall GetFloatArray(D3DXHANDLE parameter, float* values,
                                            unsigned int count) = 0;              // 33
    virtual HRESULT __stdcall SetVector(D3DXHANDLE parameter,
                                        const D3DXVECTOR4* vector) = 0;          // 34
    virtual HRESULT __stdcall GetVector(D3DXHANDLE parameter,
                                        D3DXVECTOR4* vector) = 0;                // 35
    virtual HRESULT __stdcall SetVectorArray(D3DXHANDLE parameter,
                                             const D3DXVECTOR4* vector,
                                             unsigned int count) = 0;             // 36
    virtual HRESULT __stdcall GetVectorArray(D3DXHANDLE parameter,
                                             D3DXVECTOR4* vector,
                                             unsigned int count) = 0;             // 37
    virtual HRESULT __stdcall SetMatrix(D3DXHANDLE parameter,
                                        const D3DMATRIX* matrix) = 0;            // 38
    virtual HRESULT __stdcall GetMatrix(D3DXHANDLE parameter,
                                        D3DMATRIX* matrix) = 0;                  // 39
    virtual HRESULT __stdcall SetMatrixArray(D3DXHANDLE parameter,
                                             const D3DMATRIX* matrix,
                                             unsigned int count) = 0;             // 40
    virtual HRESULT __stdcall GetMatrixArray(D3DXHANDLE parameter,
                                             D3DMATRIX* matrix,
                                             unsigned int count) = 0;             // 41
    virtual HRESULT __stdcall SetMatrixPointerArray(D3DXHANDLE parameter,
                                                    const D3DMATRIX** matrix,
                                                    unsigned int count) = 0;      // 42
    virtual HRESULT __stdcall GetMatrixPointerArray(D3DXHANDLE parameter,
                                                    D3DMATRIX** matrix,
                                                    unsigned int count) = 0;      // 43
    virtual HRESULT __stdcall SetMatrixTranspose(D3DXHANDLE parameter,
                                                 const D3DMATRIX* matrix) = 0;    // 44
    virtual HRESULT __stdcall GetMatrixTranspose(D3DXHANDLE parameter,
                                                 D3DMATRIX* matrix) = 0;          // 45
    virtual HRESULT __stdcall SetMatrixTransposeArray(D3DXHANDLE parameter,
                                                      const D3DMATRIX* matrix,
                                                      unsigned int count) = 0;     // 46
    virtual HRESULT __stdcall GetMatrixTransposeArray(D3DXHANDLE parameter,
                                                      D3DMATRIX* matrix,
                                                      unsigned int count) = 0;     // 47
    virtual HRESULT __stdcall SetMatrixTransposePointerArray(D3DXHANDLE parameter,
                                                             const D3DMATRIX** matrix,
                                                             unsigned int count) = 0;// 48
    virtual HRESULT __stdcall GetMatrixTransposePointerArray(D3DXHANDLE parameter,
                                                             D3DMATRIX** matrix,
                                                             unsigned int count) = 0;// 49
    virtual HRESULT __stdcall SetString(D3DXHANDLE parameter,
                                        const char* string) = 0;                  // 50
    virtual HRESULT __stdcall GetString(D3DXHANDLE parameter,
                                        const char** string) = 0;                 // 51
    virtual HRESULT __stdcall SetTexture(D3DXHANDLE parameter,
                                         IDirect3DBaseTexture9* texture) = 0;     // 52
    virtual HRESULT __stdcall GetTexture(D3DXHANDLE parameter,
                                         IDirect3DBaseTexture9** texture) = 0;    // 53
    virtual HRESULT __stdcall GetPixelShader(D3DXHANDLE parameter,
                                             IDirect3DPixelShader9** shader) = 0; // 54
    virtual HRESULT __stdcall GetVertexShader(D3DXHANDLE parameter,
                                              IDirect3DVertexShader9** shader) = 0; // 55
    virtual HRESULT __stdcall SetArrayRange(D3DXHANDLE parameter, unsigned int start,
                                            unsigned int end) = 0;                // 56

    // --- ID3DXEffect (slots 57..78) ------------------------------------------
    virtual HRESULT __stdcall GetPool(ID3DXEffectPool** pool) = 0;                // 57
    virtual HRESULT __stdcall SetTechnique(D3DXHANDLE technique) = 0;             // 58
    virtual D3DXHANDLE __stdcall GetCurrentTechnique() = 0;                       // 59
    virtual HRESULT __stdcall ValidateTechnique(D3DXHANDLE technique) = 0;        // 60
    virtual HRESULT __stdcall FindNextValidTechnique(D3DXHANDLE technique,
                                                     D3DXHANDLE* next) = 0;       // 61
    virtual BOOL __stdcall IsParameterUsed(D3DXHANDLE parameter,
                                           D3DXHANDLE technique) = 0;             // 62
    virtual HRESULT __stdcall Begin(unsigned int* passes,
                                    unsigned long flags) = 0;                     // 63
    virtual HRESULT __stdcall BeginPass(unsigned int pass) = 0;                   // 64
    virtual HRESULT __stdcall CommitChanges() = 0;                                // 65
    virtual HRESULT __stdcall EndPass() = 0;                                      // 66
    virtual HRESULT __stdcall End() = 0;                                          // 67
    virtual HRESULT __stdcall GetDevice(IDirect3DDevice9** device) = 0;           // 68
    virtual HRESULT __stdcall OnLostDevice() = 0;                                 // 69
    virtual HRESULT __stdcall OnResetDevice() = 0;                                // 70
    virtual HRESULT __stdcall SetStateManager(void* manager) = 0;                 // 71
    virtual HRESULT __stdcall GetStateManager(void** manager) = 0;                // 72
    virtual HRESULT __stdcall BeginParameterBlock() = 0;                          // 73
    virtual D3DXHANDLE __stdcall EndParameterBlock() = 0;                         // 74
    virtual HRESULT __stdcall ApplyParameterBlock(D3DXHANDLE block) = 0;          // 75
    virtual HRESULT __stdcall DeleteParameterBlock(D3DXHANDLE block) = 0;         // 76
    virtual HRESULT __stdcall CloneEffect(IDirect3DDevice9* device,
                                          ID3DXEffect** effect) = 0;              // 77
    virtual HRESULT __stdcall SetRawValue(D3DXHANDLE parameter, const void* data,
                                          unsigned int byteOffset,
                                          unsigned int bytes) = 0;                // 78
};

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Host-side imports share the same required runtime as the effect engine.
struct D3DXQUATERNION { float x, y, z, w; };
extern "C" {

HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice9* device, unsigned int width,
                                 unsigned int height, unsigned int mipLevels,
                                 unsigned long usage, D3DFORMAT format, D3DPOOL pool,
                                 IDirect3DTexture9** texture);
// [MMEffect.dll static import 0x1800A4720] RENDERCOLORTARGET cube creation
// (sub_1800143D0 case 38, call site 0x180014516: usage=1=RT, pool=0=DEFAULT,
// edge length = record +0x58, Miplevels = unchanged record +0x6C, with no 0/-1 special case).
HRESULT WINAPI D3DXCreateCubeTexture(IDirect3DDevice9* device,
                                     unsigned int size, unsigned int mipLevels,
                                     unsigned long usage, D3DFORMAT format,
                                     D3DPOOL pool,
                                     IDirect3DCubeTexture9** cubeTexture);
// SAS "ResourceName" file texture (originally a static import through MMHack's D3DXCreateTextureFromFileExA).
// Parameters are opaque (D3DX_DEFAULT = 0xFFFFFFFF is passed); colorKey/mip chains
// use d3dx defaults.
HRESULT WINAPI D3DXCreateTextureFromFileExW(
    IDirect3DDevice9* device, const wchar_t* srcFile, unsigned int width,
    unsigned int height, unsigned int mipLevels, unsigned long usage,
    D3DFORMAT format, D3DPOOL pool, unsigned long filter, unsigned long mipFilter,
    unsigned long colorKey, D3DXIMAGE_INFO* srcInfo, PALETTEENTRY* palette,
    IDirect3DTexture9** texture);
// SAS "ResourceName" file texture (originally a static import through MMHack's D3DXCreateTextureFromFileExA,
// IAT 0x1800A4708). Same parameter surface as ExW; Width/Height/MipLevels
// are supplied by the caller from stored annotations (D3DX_DEFAULT = 0xFFFFFFFF); colorKey/mip chains
// use d3dx defaults.
HRESULT WINAPI D3DXCreateTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int width,
    unsigned int height, unsigned int mipLevels, unsigned long usage,
    D3DFORMAT format, D3DPOOL pool, unsigned long filter, unsigned long mipFilter,
    unsigned long colorKey, D3DXIMAGE_INFO* srcInfo, PALETTEENTRY* palette,
    IDirect3DTexture9** texture);
// [MMEffect.dll static imports 0x1800A4710 / 0x1800A4718] Case 44 file texture dispatch by
// textureType: cube(9) and volume(8) variants. Parameters match ExA
// (pool=MANAGED, filter/mipFilter=D3DX_DEFAULT supplied by caller; cube's second
// size parameter is edge length + Miplevels; volume adds Depth = unchanged record +0x60).
HRESULT WINAPI D3DXCreateCubeTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int size,
    unsigned int mipLevels, unsigned long usage, D3DFORMAT format, D3DPOOL pool,
    unsigned long filter, unsigned long mipFilter, unsigned long colorKey,
    D3DXIMAGE_INFO* srcInfo, PALETTEENTRY* palette, IDirect3DCubeTexture9** cubeTexture);
HRESULT WINAPI D3DXCreateVolumeTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int width,
    unsigned int height, unsigned int depth, unsigned int mipLevels,
    unsigned long usage, D3DFORMAT format, D3DPOOL pool, unsigned long filter,
    unsigned long mipFilter, unsigned long colorKey, D3DXIMAGE_INFO* srcInfo,
    PALETTEENTRY* palette, IDirect3DVolumeTexture9** volumeTexture);
// [MMEffect.dll static import 0x1800A46F0] Volume texture creation for the 0x2C Function generation
// branch (sub_1800143D0 after 0x1800158B7: Width/Height/Depth = record +0x58/
// +0x5C/+0x60 normalized from -1 to 64, Miplevels = unchanged +0x6C, usage=0,
// pool=1=MANAGED）。
HRESULT WINAPI D3DXCreateVolumeTexture(IDirect3DDevice9* device,
                                       unsigned int width, unsigned int height,
                                       unsigned int depth, unsigned int mipLevels,
                                       unsigned long usage, D3DFORMAT format,
                                       D3DPOOL pool,
                                       IDirect3DVolumeTexture9** volumeTexture);
// [MMEffect.dll static import 0x1800A4700, call site 0x1800152A6] 0x2C Function
// generation branch: compile the entry point ("Function" annotation) from the effect's own .fx file
// (sas+0x48 FullPath); profile comes from "Target" (default tx_1_0); defines/include/flags
// are always empty, ppShader/ppErrorMsgs are ID3DXBuffer**, and the ninth parameter ppConstantTable
// is NULL.
HRESULT WINAPI D3DXCompileShaderFromFileA(
    const char* srcFile, const D3DXMACRO* defines, ID3DXInclude* include,
    const char* functionName, const char* profile, unsigned long flags,
    ID3DXBuffer** shader, ID3DXBuffer** errorMsgs, void** constantTable);
// [MMEffect.dll static import 0x1800A46F8, call site 0x180015688] Wrap the compiled
// ID3DXBuffer::GetBufferPointer bytecode as ID3DXTextureShader.
HRESULT WINAPI D3DXCreateTextureShader(const unsigned long* function,
                                       ID3DXTextureShader** textureShader);
// [MMEffect.dll static imports 0x1800A46E0/0x1800A46E8/0x1800A4740] Fill calls dispatched by textureType
// (2D/cube/volume, sub_1800143D0 case ',').
HRESULT WINAPI D3DXFillTextureTX(IDirect3DTexture9* texture,
                                 ID3DXTextureShader* textureShader);
HRESULT WINAPI D3DXFillCubeTextureTX(IDirect3DCubeTexture9* cubeTexture,
                                     ID3DXTextureShader* textureShader);
HRESULT WINAPI D3DXFillVolumeTextureTX(IDirect3DVolumeTexture9* volumeTexture,
                                       ID3DXTextureShader* textureShader);
// Debug dump: save a surface as PNG (D3DXSaveSurfaceToFileA).
HRESULT WINAPI D3DXSaveSurfaceToFileA(const char* destFile, unsigned long format,
                                      IDirect3DSurface9* srcSurface,
                                      const void* srcPalette,
                                      const void* srcRect);
HRESULT WINAPI D3DXCreateEffectPool(ID3DXEffectPool** pool);
HRESULT WINAPI D3DXCreateEffectFromFileW(IDirect3DDevice9* device,
                                         const wchar_t* srcFile,
                                         const D3DXMACRO* defines,
                                         ID3DXInclude* include,
                                         unsigned long flags, ID3DXEffectPool* pool,
                                         ID3DXEffect** effect,
                                         ID3DXBuffer** compilationErrors);
D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX* out, const D3DXMATRIX* a,
                                      const D3DXMATRIX* b);
D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX* out, float* determinant,
                                     const D3DXMATRIX* matrix);
// [MMEffect.dll static import d3dx9_43!D3DXMatrixTranspose, IAT 0x1800A46A0]
// Matrix transpose. All three original call sites (sub_180057720+0xE5 / sub_18005EA40+0x164 /
// sub_18005EBE0+0x16E) transpose in place with pOut==pM (RCX/RDX point to the same stack matrix),
// ignoring the return value; this implements optional transpose semantics for CONTROLOBJECT matrix annotations
// (select source matrix -> optional Inverse -> optional Transpose -> effect->SetMatrix).
// Forwarding to the real d3dx9 export guarantees in-place transpose semantics matching the original runtime.
D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX* out, const D3DXMATRIX* matrix);
D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* out, const D3DXVECTOR3* eye,
                                      const D3DXVECTOR3* at, const D3DXVECTOR3* up);
// [MMEffect.dll imports] the original links these statically from d3dx9_lib;
// forwarded here for the CONTROLOBJECT default matrices / light direction
// transforms (sub_180058133 defaults, sub_180057A20 DIRECTION normalize).
// EXCEPTION - D3DXMatrixIdentity is NOT in the import table: the original
// inlines the identity matrix at 0x1800581d0 (inside sub_180057BC0:
// xorps xmm0,xmm0 zero-fill + movss stores into the stack matrix, 1.0f
// diagonal). d3dx9_XX.dll likewise has no such export (the SDK ships it
// as a header D3DXINLINE), so this header fills the matrix inline
// instead of forwarding - a GetProcAddress here would be forever null.
D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX* out, float sx, float sy,
                                     float sz);
inline D3DXMATRIX* WINAPI D3DXMatrixIdentity(D3DXMATRIX* out) {
    for (int row = 0; row != 4; ++row)
        for (int column = 0; column != 4; ++column)
            out->m[row][column] = row == column ? 1.0f : 0.0f;
    return out;
}
D3DXVECTOR3* WINAPI D3DXVec3TransformNormal(D3DXVECTOR3* out,
                                            const D3DXVECTOR3* v,
                                            const D3DXMATRIX* matrix);
D3DXVECTOR3* WINAPI D3DXVec3Normalize(D3DXVECTOR3* out,
                                      const D3DXVECTOR3* v);
const char* WINAPI D3DXGetVertexShaderProfile(IDirect3DDevice9* device);
const char* WINAPI D3DXGetPixelShaderProfile(IDirect3DDevice9* device);
unsigned int WINAPI D3DXGetShaderVersion(const unsigned long* function);
HRESULT WINAPI D3DXGetShaderInputSemantics(const unsigned long* function,
                                           D3DXSEMANTIC* semantics,
                                           unsigned int* count);
HRESULT WINAPI D3DXDisassembleEffect(ID3DXEffect* effect, BOOL enableColorCode,
                                     ID3DXBuffer** disassembly);


HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(IDirect3DDevice9*, LPCVOID, UINT,
    UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR,
    D3DXIMAGE_INFO*, PALETTEENTRY*, IDirect3DTexture9**);
HRESULT WINAPI D3DXCreateEffectFromResourceA(IDirect3DDevice9*, HMODULE, LPCSTR,
    const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
D3DXMATRIX* WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX*, float, float, float, float);
D3DXMATRIX* WINAPI D3DXMatrixRotationX(D3DXMATRIX*, float);
D3DXMATRIX* WINAPI D3DXMatrixRotationY(D3DXMATRIX*, float);
D3DXMATRIX* WINAPI D3DXMatrixRotationZ(D3DXMATRIX*, float);
D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX*, float, float, float);
D3DXVECTOR4* WINAPI D3DXVec3Transform(D3DXVECTOR4*, const D3DXVECTOR3*, const D3DXMATRIX*);
D3DXVECTOR3* WINAPI D3DXVec3TransformCoord(D3DXVECTOR3*, const D3DXVECTOR3*, const D3DXMATRIX*);
HRESULT WINAPI D3DXLoadMeshFromXInMemory(LPCVOID, DWORD, DWORD, IDirect3DDevice9*,
    ID3DXBuffer**, ID3DXBuffer**, ID3DXBuffer**, DWORD*, ID3DXMesh**);
HRESULT WINAPI D3DXLoadMeshFromXW(LPCWSTR, DWORD, IDirect3DDevice9*,
    ID3DXBuffer**, ID3DXBuffer**, ID3DXBuffer**, DWORD*, ID3DXMesh**);
HRESULT WINAPI D3DXComputeNormals(ID3DXBaseMesh*, const DWORD*);
D3DXQUATERNION* WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION*, const D3DXMATRIX*);
D3DXQUATERNION* WINAPI D3DXQuaternionMultiply(D3DXQUATERNION*, const D3DXQUATERNION*, const D3DXQUATERNION*);
D3DXMATRIX* WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX*, const D3DXQUATERNION*);
void WINAPI D3DXQuaternionToAxisAngle(const D3DXQUATERNION*, D3DXVECTOR3*, float*);
D3DXQUATERNION* WINAPI D3DXQuaternionNormalize(D3DXQUATERNION*, const D3DXQUATERNION*);
D3DXQUATERNION* WINAPI D3DXQuaternionInverse(D3DXQUATERNION*, const D3DXQUATERNION*);
D3DXQUATERNION* WINAPI D3DXQuaternionRotationAxis(D3DXQUATERNION*, const D3DXVECTOR3*, float);
HRESULT WINAPI D3DXSaveSurfaceToFileW(LPCWSTR, int, IDirect3DSurface9*, const void*, const RECT*);
}  // extern "C"

#endif  // MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_
