#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mme_resources.h"
#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr) return 3;
    struct Resource { LPCSTR type; int id; } required[] = {
        {RT_MENU, IDR_MME_MENU_JP}, {RT_MENU, IDR_MME_MENU_EN},
        {RT_MENU, IDR_MME_MAPPING_MENU_JP}, {RT_MENU, IDR_MME_MAPPING_MENU_EN},
        {RT_DIALOG, IDD_MME_LOG}, {RT_DIALOG, IDD_MME_MAPPING},
        {RT_GROUP_ICON, IDI_MME_APP}, {RT_GROUP_ICON, 100},
        {RT_RCDATA, 117}, {RT_RCDATA, 118}, {RT_MANIFEST, 1},
    };
    int failures = 0;
    for (const auto& resource : required) {
        if (!FindResourceA(module, MAKEINTRESOURCEA(resource.id), resource.type)) {
            std::fprintf(stderr, "Missing embedded resource %d\n", resource.id);
            ++failures;
        }
    }
    HRSRC groupResource = FindResourceA(module, MAKEINTRESOURCEA(IDI_MME_APP), RT_GROUP_ICON);
    if (groupResource) {
        const auto* group = static_cast<const unsigned char*>(
            LockResource(LoadResource(module, groupResource)));
        const DWORD bytes = SizeofResource(module, groupResource);
        WORD count = 0;
        if (group && bytes >= 6) std::memcpy(&count, group + 4, 2);
        if (!count || bytes != 6u + 14u * count) ++failures;
        else for (WORD index = 0; index < count; ++index) {
            WORD id = 0;
            std::memcpy(&id, group + 6 + 14 * index + 12, 2);
            if (id != IDI_MME_IMAGE_FIRST + index ||
                !FindResourceA(module, MAKEINTRESOURCEA(id), RT_ICON)) ++failures;
        }
    }
    FreeLibrary(module);
    if (!failures) std::puts("Host and MME menus, dialogs, icon images and shaders are embedded");
    return failures ? 1 : 0;
}
