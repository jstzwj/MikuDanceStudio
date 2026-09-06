// ===========================================================================
// d3dx9.h - MMEffect 内置模块专用的最小 D3DX9 ABI 镜像
// ===========================================================================
// MikuDanceStudio 不依赖 DirectX SDK 头文件构建（见 include/mikudancestudio/
// d3dx_dyn.hpp 的既有约定）。本头文件以同样的方式自写声明：COM 接口的
// vtable 槽位顺序、结构体布局均为二进制 ABI 事实，与原 d3dx9effect.h /
// d3dx9shader.h / d3dx9core.h 的顺序逐一核对（SetInt@26、SetTechnique@58、
// Begin@63、OnLostDevice@69 与宿主 fx_slots.hpp 的原版二进制实证槽位一致）。
//
// 仅覆盖内置 MMEffect 模块实际调用的表面；未被调用的槽位保留声明位置、
// 参数从简。自由函数（D3DXCreateEffectFromFileW 等）由 src/d3dx9_dyn.cpp
// 转发到运行时加载的 d3dx9_XX.dll，不引入构建期 d3dx9.lib 依赖。
// =========================================================================//
#ifndef MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_
#define MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_

#include <d3d9.h>

// ---------------------------------------------------------------------------
// 基础类型
// ---------------------------------------------------------------------------
typedef const char* D3DXHANDLE;

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

// ID3DXInclude 仅以 nullptr 传入 D3DXCreateEffectFromFileW。
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

// ID3DXEffectPool：效果共享池，仅作不透明 COM 对象使用。
struct ID3DXEffectPool : public IUnknown {};

// ---------------------------------------------------------------------------
// 描述符（d3dx9effect.h / d3dx9shader.h 布局）
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
// 效果编译标志（d3dx9effect.h）
// ---------------------------------------------------------------------------
#define D3DXFX_DONOTSAVESTATE        (1 << 0)
#define D3DXFX_DONOTSAVESHADERSTATE  (1 << 1)
#define D3DXFX_DONOTSAVESAMPLERSTATE (1 << 2)
#define D3DXFX_NOT_CLONEABLE         (1 << 11)
#define D3DXFX_LARGEADDRESSAWARE     (1 << 17)

// ---------------------------------------------------------------------------
// ID3DXEffect / ID3DXBaseEffect 完整镜像
// ---------------------------------------------------------------------------
// 槽位 3..56 = ID3DXBaseEffect 的 54 个方法；57..78 = ID3DXEffect 追加的
// 22 个方法。顺序为 ABI 事实（见文件头注释）。
struct ID3DXEffect : public IUnknown {
    // --- ID3DXBaseEffect（槽 3..56）--------------------------------------
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

    // --- ID3DXEffect（槽 57..78）------------------------------------------
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
// 自由函数（src/d3dx9_dyn.cpp 转发到运行时加载的 d3dx9_XX.dll）
// ---------------------------------------------------------------------------
extern "C" {

HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice9* device, unsigned int width,
                                 unsigned int height, unsigned int mipLevels,
                                 unsigned long usage, D3DFORMAT format, D3DPOOL pool,
                                 IDirect3DTexture9** texture);
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
D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* out, const D3DXVECTOR3* eye,
                                      const D3DXVECTOR3* at, const D3DXVECTOR3* up);
const char* WINAPI D3DXGetVertexShaderProfile(IDirect3DDevice9* device);
const char* WINAPI D3DXGetPixelShaderProfile(IDirect3DDevice9* device);
unsigned int WINAPI D3DXGetShaderVersion(const unsigned long* function);
HRESULT WINAPI D3DXGetShaderInputSemantics(const unsigned long* function,
                                           D3DXSEMANTIC* semantics,
                                           unsigned int* count);
HRESULT WINAPI D3DXDisassembleEffect(ID3DXEffect* effect, BOOL enableColorCode,
                                     ID3DXBuffer** disassembly);

}  // extern "C"

#endif  // MIKUDANCESTUDIO_MME_D3DX9_MIRROR_H_
