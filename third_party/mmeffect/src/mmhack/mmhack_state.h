// mmhack_state.h - 内置 MMEffect 的渲染状态跟踪块（原 MMHack.dll .data 段）。
//
// 每个字段注释保留了原版 .data 槽位地址（证据）。22 个状态查询函数读取本
// 状态块；写入方在原版是 MyDirect3D9/MyDirect3DDevice9/MyD3DXMesh 包装器与
// PMM 文件监视 hook，在本工程是 src/mme_host.cpp 的 MmeHost* 集成层（由
// 宿主渲染管线在对应位置直接调用）。
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <list>

// --- 12 MME 技术描述符，原版 .rdata 静态表 [0x18005bdf0] -------------------
// flags 字节: [0]=使用基础纹理, [1]=sphere(Sphia), [2]=cd2(子纹理), [3]=Diffuse。
// 由 MmeHostSetStandardEffect 注册进技术缓存（原 Hooked_D3DXCreateEffect-
// FromResourceA [0x180004440] 的登记路径）。
struct MmhTecDesc { const char* name; DWORD flags; };
extern const MmhTecDesc g_tecTable[12];
const int MMH_TEC_COUNT = 12;

// 每对象数据记录（原版: std::map<unsigned long long, ObjData>，键为宿主对象
// id，DAT_18006e958；访问器 FUN_18000f2b0）。
// MmhMaterialEntry 为原版 0x78 字节表项，由 MmhMaterialTableLoadFromFile
// [FUN_18000e020] 直接解析模型文件填充：
//   +0x00 texName - toon 纹理引用（目录限定路径，内置 toon01..toon10.bmp 除外）
//   +0x28 name0   - kind 0 名称（PMX 日文名 / PMD "<model>.txt" 第 i 行）
//   +0x50 name1   - kind 1 名称（PMX 英文名 / PMD 同行重复）
struct MmhMaterialEntry {
    std::wstring texName;    // entry +0x00: toon 纹理路径
    std::wstring name0;      // entry +0x28: kind 0 名称（日文）
    std::wstring name1;      // entry +0x50: kind 1 名称（英文）
};
struct MmhObjData {
    unsigned long long id = 0;
    bool isPmd = false;
    std::wstring modelName;              // FUN_18000f340: value+0x30 wstring
    std::vector<MmhMaterialEntry> mats;  // 原版 0x78 字节表项
    int attachPmdIndex = -1;             // +0x240 (-1 = 未附属)
    int attachBoneIndex = 0;             // +0x244
};

// BeginScene 缓存对象（原版: 包装器 +0x58 的 0x50 字节节点表，id 图 +0x30，
// order 图 +0x60；由 [0x180002de0] 构建）。
struct MmhCachedObject {
    unsigned long long id = 0;   // node[2]
    int isPmd = 0;               // node[3]: 1 = PMD 模型, 0 = 附件
    std::string fileName;        // node[1]: ANSI 文件名 (ExpGetPmdFilename)
    void* texRef = nullptr;      // node[4]: 创建时捕获的 AddRef 对象
    MmhObjData* data = nullptr;  // node[5]: MmhGetObjectData(id) 结果
};

// 宿主导出（原 MikuMikuDance.exe / 本工程 MikuMikuDanceE.exe 的 37 个 Exp*
// 的子集）。声明见 MMDExport.h。
#include "MMDExport.h"

// --- 全局状态（单实例）------------------------------------------------------
struct MmhState {
    // --- 每帧/每绘制状态（BeginScene + 绘制转发写入）-----------------------
    unsigned long long currentModelID = 0;   // DAT_18006e868 (GetCurrentModelID)
    int    currentDrawType = 0;              // DAT_18006e798 (GetCurrentDrawType = ExpGetCurrentTechnic)
    int    currentObjectKind = 0;            // DAT_18006e870 (1 = PMD 模型, 0 = 附件)
    int    currentObjectIndex = 0;           // 包装器 +0x28（当前对象）
    int    currentSubsetIndex = -1;          // DAT_18006ccc0 (GetCurrentSubsetIndex, -1 = 无)
    int    blendMode = 0;                    // DAT_18006e7b8 (GetBlendMode; D3DRS_DESTBLEND == D3DBLEND_ONE)
    bool   zbufferUsed = false;              // DAT_18006e7b8 孪生 (GetRenderState(D3DRS_DESTBLEND)==2 -> 1)
    void*  currentEffect = nullptr;          // DAT_18006e7a0 (GetCurrentEffect, ID3DXEffect*)
    bool   effectFileUsed = false;           // DAT_18006e795 (IsEffectFileUsed; 着色器路径激活)
    bool   toonAvailable = false;            // DAT_18006e795 伴随（固定功能路径）

    // --- 纹理捕获（借用指针，每绘制刷新）------------------------------------
    IDirect3DBaseTexture9* baseTexture = nullptr;    // DAT_18006e7a8 (GetTexture)
    IDirect3DBaseTexture9* sphereTexture = nullptr;  // DAT_18006e7b0 (GetSphereMapTexture)
    unsigned char sphereUsed = 0;            // byte @0x18006e789 (flags 字节 1)
    unsigned char subTexUsed = 0;            // byte @0x18006e78a (flags 字节 2 = cd2)
    unsigned char diffuseFlag = 0;           // byte @0x18006e78b (flags 字节 3)
    unsigned char texUsed = 0;               // byte @0x18006e788 (flags 字节 0)
    unsigned char sphereAdd = 0;             // DAT_18006e797 ("spadd" / 加法球贴图)
    bool   toonUsed = false;                 // 派生: IsToonUsed

    // --- 窗口 --------------------------------------------------------------
    HWND mainWindow = nullptr;               // DAT_18006e7e0 (GetMMDMainWindow; 根属主)
    HWND deviceWindow = nullptr;             // DAT_18006e7c8 (hDeviceWindow 跟踪)
    HWND presentWindow = nullptr;            // DAT_18006e7c0 (GetDrawnWindow)

    // --- 杂项 ---------------------------------------------------------------
    float frameTime = 0.0f;                  // DAT_18006e79c (GetCurrentFrameTime = ExpGetFrameTime)
    int   notEditMode = 0;                   // DAT_18006e796 (IsEditMode 返回 !this)
    int   modalFlag = 0;                     // DAT_18006e7bd (粘滞模态对话框标志)
    bool  debugMode = false;                 // DAT_18006e7bc (IsDebugMode; MMEffect.debug 存在)
    bool  effectsDisabled = false;           // DAT_18006e794 (MME 初始化失败/禁用)
    D3DCOLOR clearColor = 0;                 // DAT_18006ccc4 (GetClearColor; Clear 时捕获)

    // --- PMM 文件路径 (LoadedPMMFile / SavedPMMFile) ------------------------
    std::wstring loadedPMMFile;              // DAT_18006cd28 (+ 标志 DAT_18006cd38)
    bool loadedPmmFlag = false;
    std::list<std::wstring> savedPmmList;    // DAT_18006e848/850 (SavedPMMFile 弹出队首)
    std::wstring currentOpenFile;            // DAT_18006cda0 (CreateFileW GENERIC_READ 上下文)

    // --- 全局对象数据图 (DAT_18006e958, 访问器 FUN_18000f2b0) ---------------
    std::map<unsigned long long, MmhObjData> objData;
    MmhObjData objDataDefault;               // 头节点值（空表项）

    // --- 纹理缓存: 文件名 -> 表项 (hash 图 DAT_18006ccd0 族) ----------------
    struct TexCacheEntry {
        IDirect3DBaseTexture9* tex = nullptr;     // 该文件名的首个纹理
        IDirect3DBaseTexture9* toonTex = nullptr; // 惰性 = tex (节点 +0x50)
    };
    std::unordered_map<std::wstring, TexCacheEntry> texCache;

    // --- 技术缓存: D3DXHANDLE -> 表索引 (FUN_1800061a0 图) ------------------
    std::map<const char*, int> tecCache;     // 原版 DAT_18006ccd0 图

    // --- 光照矩阵 (GetLightViewProjMatrix 每次调用重算) ---------------------
    D3DMATRIX worldMatrix;                   // DAT_18006e8b0 (GetTransform(D3DTS_WORLDMATRIX(0)))
    D3DMATRIX worldInverse;                  // DAT_18006e8f0 (world 的 D3DXMatrixInverse)
};
extern MmhState g_mmh;

// --- 由 mmhack_registry.cpp 实现 ---------------------------------------------
MmhObjData* MmhGetObjectData(unsigned long long id);          // [FUN_18000f2b0]
bool  MmhIsKnownObjectId(unsigned long long id);              // [FUN_18000fb60 近似]
IDirect3DBaseTexture9* MmhGetCurrentToonTexture(unsigned long long modelDataId); // [FUN_18000f810]
void  MmhRecordTextureFile(const std::wstring& name, IDirect3DBaseTexture9* tex); // [FUN_18000f3d0]
void  MmhRecordTecHandle(D3DXHANDLE h, int index);            // 技术缓存插入
int   MmhTecCacheIndex(const char* tecName);                  // [FUN_1800061a0 近似]

// --- 由 mmhack_material.cpp 实现 ---------------------------------------------
const wchar_t* MmhGetMaterialName(unsigned long long object_id,
                                  unsigned long material_index, int name_kind);

// --- 由 mmhack_material_table.cpp 实现 ---------------------------------------
// 直接解析模型文件（PMD/PMX）填充每材质表项：+0x00 toon 纹理、+0x28 日文名、
// +0x50 英文名；文件缺失保持表原样，损坏数据部分填充。
void MmhMaterialTableLoadFromFile(const wchar_t* modelPath,
                                  std::vector<MmhMaterialEntry>* out);

// --- 由 mmhack_lightmatrix.cpp 实现 ------------------------------------------
void MmhComputeLightViewProjMatrix(D3DMATRIX* outView, D3DMATRIX* outProj,
                                   D3DMATRIX* outViewProj);
