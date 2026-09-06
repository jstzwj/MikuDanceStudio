// mmhack_material.cpp - GetMaterialName backing logic.
//
// Original [0x180001130, lines 2250-2312]: hash-looks up the per-object cache
// node in the current device wrapper's id map (DAT_18006e7d0), requires
// node->isPmd == 1 and material_index < (materials.end - begin) / 0x78, then
// returns the entry string at +0x28 (kind == 0) or +0x50 (kind != 0); NULL
// when the object is unknown, is an accessory, the index is out of range, or
// the selected wstring is empty. The entries are filled by
// MmhMaterialTableLoadFromFile [FUN_18000e020] from the model file itself.
#include "mmhack_state.h"

const wchar_t* MmhGetMaterialName(unsigned long long object_id,
                                  unsigned long material_index, int name_kind)
{
    std::map<unsigned long long, MmhObjData>::iterator it = g_mmh.objData.find(object_id);
    if (it == g_mmh.objData.end())
        return nullptr;
    MmhObjData& d = it->second;
    if (d.isPmd != true)                         // (int)node[3] == 1
        return nullptr;
    if (material_index >= d.mats.size())
        return nullptr;
    const std::wstring& s = (name_kind == 0) ? d.mats[material_index].name0
                                             : d.mats[material_index].name1;
    if (s.empty())
        return nullptr;
    return s.c_str();
}
