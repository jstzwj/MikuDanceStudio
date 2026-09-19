// mmhack_registry.cpp - 对象数据图、纹理缓存与技术缓存（原 MMHack 注册层
// 的去 hook 化保留部分）。
//
// 原版的三个指针键包装器注册表（IDirect3D9/IDirect3DDevice9/ID3DXMesh，插
// 入 FUN_180007020/FUN_1800069d0/FUN_1800066b0）与网格->文件簿记随设备包装
// 机制一并去除——本工程由宿主直接调用，无需对象包装。保留的缓存：对象 id
// 键 objData 图（DAT_18006e958）、技术句柄图（DAT_18006ccd0 族）、文件名键
// 纹理缓存（FUN_180010d90 hash 族，含 toon01..10 裸名别名）。
#include "mmhack_state.h"

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
// [FUN_18000f3d0] 原版对内置 toon 特殊处理：完整路径 "data\toonNN.bmp"
// 只以裸文件名 "toonNN.bmp" 登记键（toon 分支 0x18000f5ca 直接跳到登记
// 尾部，跳过 0x18000f5ce-0x18000f5e9 的完整路径登记，规范化完整路径键
// 从不写入）——裸名正是材质表存储的键，也是 GetToonTexture
// [FUN_18000f810] 查找（含 "toon10.bmp" 与空串回退）使用的键；模型
// texName 恰为完整路径形态时原版必然 miss（直落空串回退返回 NULL）。
static bool MmhToonBasenameAlias(const std::wstring& name, std::wstring* bare)
{
    // [0x18000f468] 完整路径必须恰为 15 个宽字符（"data\toonNN.bmp"），且
    // 前 4 字符为 "data"——该前缀比较是 _wcsnicmp（大小写不敏感）
    // [0x18000f48a]。第 5 字符（分隔符）原版不检查，由 basename 提取隐式
    // 限定为 '/' 或 '\'。
    if (name.size() != 15)
        return false;
    if (_wcsnicmp(name.c_str(), L"data", 4) != 0)
        return false;
    size_t slash = name.find_last_of(L"/\\");
    std::wstring base = (slash == std::wstring::npos) ? name : name.substr(slash + 1);
    // [0x18000f468] basename 必须恰为 10 字符。
    if (base.size() != 10)
        return false;
    // [0x18000f4b2] "toon" 前缀是 wcsncmp——大小写敏感（"Toon.." 不匹配）。
    if (wcsncmp(base.c_str(), L"toon", 4) != 0)
        return false;
    // [0x18000f4d5..0x18000f4e8] ".bmp" 后缀是逐字符比较——大小写敏感。
    if (base.compare(6, 4, L".bmp") != 0)
        return false;
    // [0x18000f500..0x18000f554] 数字限定：('0','1'..'9') = toon01..toon09，
    // 或 ('1','0') = toon10。toon00 与 toon11..toon19 均不登记。
    wchar_t c4 = base[4], c5 = base[5];
    if (!(c4 == L'0' && c5 >= L'1' && c5 <= L'9') && !(c4 == L'1' && c5 == L'0'))
        return false;
    *bare = base;
    return true;
}

// [sub_180013870] 斜杠规范化：完整拷贝后将 '\'（0x5C）原地替换为 '/'（0x2F）。
static std::wstring MmhSlashNormalize(const std::wstring& name)
{
    std::wstring normalized = name;
    for (size_t i = 0; i < normalized.size(); i++)
        if (normalized[i] == L'\\')
            normalized[i] = L'/';
    return normalized;
}

void MmhRecordTextureFile(const std::wstring& name, IDirect3DBaseTexture9* tex)
{
    if (tex == nullptr)
        return;
    // [FUN_18000f3d0] 两个互斥登记分支：
    //  toon 命中（数字校验通过后 0x18000f5b3：Src = 规范化 basename）在
    //  0x18000f5ca 跳到登记尾部，只登记裸 "toonNN.bmp" 键；
    //  非 toon（任一模式校验失败 jnz 0x18000f5CE）主登记键 =
    //  slashNormalize(完整路径) [0x18000f5ce..0x18000f5e9]。查询侧
    //  [FUN_18000f810] 以 mats[subset].texName 原样为键——含 '\' 的自定义
    //  toon 引用、以及 toon 分支从不登记的完整路径键，在原版都必然
    //  miss（直落空串回退返回 NULL）。
    std::wstring bare;
    if (MmhToonBasenameAlias(name, &bare)) {
        MmhState::TexCacheEntry& b = g_mmh.texCache[bare];
        if (b.tex == nullptr)
            b.tex = tex;
        // [0x18000f57c] toon01 特例：原版以"清空后的路径 + tex=0"递归调用
        // 自身 [sub_180010390 原地清空 → 空串走 LABEL_47 直落登记尾部]，
        // 即登记空字符串键 ""（GetToonTexture 的最终回退键）。空键表项的
        // 纹理按原版为 null；operator[] 仅在键不存在时创建，不覆盖既有值。
        if (bare == L"toon01.bmp")
            g_mmh.texCache[L""];
        return;
    }
    MmhState::TexCacheEntry& e = g_mmh.texCache[MmhSlashNormalize(name)];
    if (e.tex == nullptr)
        e.tex = tex;                             // 该文件名的首个纹理
    // toonTex 保持 null 直到 GetToonTexture 请求（节点 +0x50 惰性初始化，
    // FUN_18000f810）。
}

IDirect3DBaseTexture9* MmhGetCurrentToonTexture(unsigned long long modelDataId)
{
    // [FUN_18000f810] entry = 模型数据的 materials[currentSubsetIndex]，其
    // toon 名原样查纹理缓存。越界 [0x18000f875]、texName 为空 [0x18000f89b]
    // 与缓存未命中 [0x18000f8c1] 三者均直落 LABEL_17 [0x18000f96c]——只查空
    // 串键，跳过 "toon10.bmp"；后者仅在键存在而 toonTex 惰性初始化后仍空时
    // 才被查询 [0x18000f8ea]。
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
                auto itDefault = g_mmh.texCache.find(L"toon10.bmp");
                if (itDefault != g_mmh.texCache.end()) {
                    if (itDefault->second.toonTex == nullptr && itDefault->second.tex != nullptr)
                        itDefault->second.toonTex = itDefault->second.tex;
                    if (itDefault->second.toonTex != nullptr)
                        return itDefault->second.toonTex;
                }
            }
        }
    }
    // [LABEL_17] 最终回退只查空串键。该键仅由 toon01 特例以 tex=NULL 登记
    // [0x18000f596]，故经此路径按原版返回 NULL。
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
