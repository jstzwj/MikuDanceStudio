// mmhack_registry.cpp - 对象数据图、纹理缓存与技术缓存（原 MMHack 注册层
// 的去 hook 化保留部分）。
//
// 原版的三个指针键包装器注册表（IDirect3D9/IDirect3DDevice9/ID3DXMesh，插
// 入 FUN_180007020/FUN_1800069d0/FUN_1800066b0）与网格->文件簿记随设备包装
// 机制一并去除——本工程由宿主直接调用，无需对象包装。保留的缓存：对象 id
// 键 objData 图（DAT_18006e958）、技术句柄图（DAT_18006ccd0 族）、文件名键
// 纹理缓存（FUN_180010d90 hash 族，含 toon01..10 裸名别名）。
#include "mmhack_state.h"

#include <cwctype>

// ---------------------------------------------------------------------------
// 每对象数据图  [DAT_18006e958; 访问器 FUN_18000f2b0]
// ---------------------------------------------------------------------------
MmhObjData* MmhGetObjectData(unsigned long long id)
{
    if (!MmhIsKnownObjectId(id))
        return &g_mmh.objDataDefault;            // [FUN_18000f2b0 头节点回退]
    auto it = g_mmh.objData.find(id);
    if (it == g_mmh.objData.end())
        return &g_mmh.objDataDefault;
    return &it->second;
}

// [FUN_18000fb60 近似]: id 有效当且仅当它出现在宿主当前的 PMD/附件 id 列表。
bool MmhIsKnownObjectId(unsigned long long id)
{
    int n = ExpGetPmdNum();
    for (int i = 0; i < n; i++)
        if ((unsigned long long)(uintptr_t)ExpGetPmdID(i) == id)
            return true;
    n = ExpGetAcsNum();
    for (int i = 0; i < n; i++)
        if ((unsigned long long)(uintptr_t)ExpGetAcsID(i) == id)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// 纹理缓存  [hash 图族 FUN_180010d90 / FUN_180010c40]
// ---------------------------------------------------------------------------
// [FUN_18000f3d0] 原版对内置 toon 特殊处理：basename 为 toon01.bmp ..
// toon10.bmp 的相对路径额外以裸文件名登记——这正是材质表存储的键，也是
// GetToonTexture [FUN_18000f810] 查找（含 "toon10.bmp" 回退）使用的键。
// 没有该别名时缓存只保存完整路径，所有内置 toon 查找都会落空。
static bool MmhToonBasenameAlias(const std::wstring& name, std::wstring* bare)
{
    size_t slash = name.find_last_of(L"/\\");
    std::wstring base = (slash == std::wstring::npos) ? name : name.substr(slash + 1);
    if (base.size() != 10)
        return false;
    std::wstring low = base;
    for (size_t i = 0; i < low.size(); i++)
        low[i] = (wchar_t)towupper(low[i]);
    if (low.compare(0, 5, L"TOON0") != 0 && low.compare(0, 5, L"TOON1") != 0)
        return false;
    // toon01.bmp .. toon10.bmp: "TOON" + ('0'|'1') + digit + ".bmp"
    wchar_t c4 = low[4], c5 = low[5];
    if (c4 != L'0' && c4 != L'1')
        return false;
    if (c5 < L'0' || c5 > L'9')
        return false;
    // 数字 00-09 仅带 '0' 前缀 (toon01..toon09)，10 带 '1'
    if (c4 == L'0' && c5 == L'0')
        return false;
    if (low.compare(6, 4, L".BMP") != 0)
        return false;
    *bare = base;
    return true;
}

void MmhRecordTextureFile(const std::wstring& name, IDirect3DBaseTexture9* tex)
{
    if (tex == nullptr)
        return;
    MmhState::TexCacheEntry& e = g_mmh.texCache[name];
    if (e.tex == nullptr)
        e.tex = tex;                             // 该文件名的首个纹理
    // toonTex 保持 null 直到 GetToonTexture 请求（节点 +0x50 惰性初始化，
    // FUN_18000f810）。
    // [FUN_18000f3d0 toon 特例] 为内置 toon 额外登记裸 "toonNN.bmp" 键。
    std::wstring bare;
    if (MmhToonBasenameAlias(name, &bare) && bare != name) {
        MmhState::TexCacheEntry& b = g_mmh.texCache[bare];
        if (b.tex == nullptr)
            b.tex = tex;
    }
}

IDirect3DBaseTexture9* MmhGetCurrentToonTexture(unsigned long long modelDataId)
{
    // [FUN_18000f810] entry = 模型数据的 materials[currentSubsetIndex]；
    // 其 toon 名查纹理缓存，然后 "toon10.bmp"，最后 ""。
    (void)modelDataId;
    MmhObjData* data = MmhGetObjectData(modelDataId);
    if (data != nullptr &&
        g_mmh.currentSubsetIndex >= 0 &&
        (unsigned long)g_mmh.currentSubsetIndex < data->mats.size()) {
        const std::wstring& toonName = data->mats[g_mmh.currentSubsetIndex].texName;
        if (!toonName.empty()) {
            auto it = g_mmh.texCache.find(toonName);
            if (it != g_mmh.texCache.end()) {
                if (it->second.toonTex == nullptr && it->second.tex != nullptr)
                    it->second.toonTex = it->second.tex;
                if (it->second.toonTex != nullptr)
                    return it->second.toonTex;
            }
        }
    }
    auto itDefault = g_mmh.texCache.find(L"toon10.bmp");
    if (itDefault != g_mmh.texCache.end()) {
        if (itDefault->second.toonTex == nullptr && itDefault->second.tex != nullptr)
            itDefault->second.toonTex = itDefault->second.tex;
        if (itDefault->second.toonTex != nullptr)
            return itDefault->second.toonTex;
    }
    auto itEmpty = g_mmh.texCache.find(L"");
    if (itEmpty != g_mmh.texCache.end()) {
        if (itEmpty->second.toonTex == nullptr && itEmpty->second.tex != nullptr)
            itEmpty->second.toonTex = itEmpty->second.tex;
        if (itEmpty->second.toonTex != nullptr)
            return itEmpty->second.toonTex;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 技术缓存  [FUN_1800061a0 D3DXHANDLE hash 图；存表索引]
// ---------------------------------------------------------------------------
void MmhRecordTecHandle(D3DXHANDLE h, int index)
{
    if (h != nullptr)
        g_mmh.tecCache[(const char*)h] = index;
}

int MmhTecCacheIndex(const char* tecName)
{
    auto it = g_mmh.tecCache.find(tecName);
    return it == g_mmh.tecCache.end() ? -1 : it->second;
}

// --- 12 MME 技术描述符 [0x18005bdf0 静态表] ---------------------------------
const MmhTecDesc g_tecTable[MMH_TEC_COUNT] = {
    { "ColorRenderTec",            0x00000000u },
    { "ZValuePlotTec",             0x00000000u },
    { "BufferShadowTec",           0x00000000u },
    { "BShadowTextureTec",         0x00000001u },
    { "BShadowSphiaTec",           0x00000100u },
    { "BShadowSphiaTextureTec",    0x00000101u },
    { "BShadowTexCd2Tec",          0x00010100u },
    { "BShadowTextureTexCd2Tec",   0x00010101u },
    { "DiffuseBufferShadowTec",    0x01000000u },
    { "DiffuseBSTextureTec",       0x01000001u },
    { "DiffuseBSSphiaTec",         0x01000100u },
    { "DiffuseBSSphiaTexCd2Tec",   0x01000101u },
};
