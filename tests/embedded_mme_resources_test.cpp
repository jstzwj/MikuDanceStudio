#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mme_resources.h"
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
bool DialogContains(HMODULE module, int id, const wchar_t* text) {
    HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(id), RT_DIALOG);
    if (!resource)
        return false;
    const auto* data =
        static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)));
    const DWORD size = SizeofResource(module, resource);
    const std::size_t length = (std::wcslen(text) + 1) * sizeof(wchar_t);
    if (!data || length > size)
        return false;
    for (std::size_t offset = 0; offset <= size - length; offset += sizeof(WORD))
        if (std::memcmp(data + offset, text, length) == 0)
            return true;
    return false;
}

int CheckLanguageResources(HMODULE module) {
    // Text goldens recovered from MMEffect v0.37 x64 resources 101/107/109/110/108.
    // Inspect the compiled resources without creating windows or changing UI language.
    struct Item {
        int menu, command;
        const wchar_t* text;
    };
    const Item items[] = {
        {IDR_MME_MENU_JP, 40005, L"エフェクト割当(&M)"},
        {IDR_MME_MENU_JP, 40006, L"エフェクト使用(&E)\tCtrl+Shift+E"},
        {IDR_MME_MENU_JP, 40001, L"自動更新(&O)"},
        {IDR_MME_MENU_JP, 40022, L"割当ファイル自動保存(&S)"},
        {IDR_MME_MENU_JP, 40004, L"全て更新(&R)"},
        {IDR_MME_MENU_JP, 40002, L"ログウインドウ(&L)"},
        {IDR_MME_MENU_JP, 40003, L"MMEffectのバージョン情報(&A)"},
        {IDR_MME_MAPPING_MENU_JP, 40009, L"設定を読込(&O)"},
        {IDR_MME_MAPPING_MENU_JP, 40010, L"設定を保存(&S)"},
        {IDR_MME_MAPPING_MENU_JP, 40016, L"モデル単位で設定読込(&M)"},
        {IDR_MME_MAPPING_MENU_JP, 40017, L"モデル単位で設定保存(&N)"},
        {IDR_MME_MAPPING_MENU_JP, 40011, L"閉じる(&C)"},
        {IDR_MME_MAPPING_MENU_JP, 40007, L"ファイル選択(&F)"},
        {IDR_MME_MAPPING_MENU_JP, 40012, L"解除(&N)"},
        {IDR_MME_MAPPING_MENU_JP, 40013, L"非表示/表示(&H)"},
        {IDR_MME_MAPPING_MENU_JP, 40008, L"default設定でリセット(&R)"},
        {IDR_MME_MAPPING_MENU_JP, 40006, L"サブセット展開(&E)"},
        {IDR_MME_MAPPING_MENU_JP, 40014, L"エフェクトを逆アセンブル(&D)"},
        {IDR_MME_MAPPING_MENU_JP, 40028, L"デバイス検証をスキップ(&S)"},
        {IDR_MME_MENU_EN, 40005, L"Effect Mapping(&M)"},
        {IDR_MME_MENU_EN, 40020, L"Enable Effect(&E)\tCtrl+Shift+E"},
        {IDR_MME_MAPPING_MENU_EN, 40007, L"Set Effect File(&F)"},
    };
    int failures = 0;
    for (const auto& item : items) {
        HMENU menu = LoadMenuW(module, MAKEINTRESOURCEW(item.menu));
        wchar_t text[128]{};
        if (!menu || !GetMenuStringW(menu, item.command, text, 128, MF_BYCOMMAND) ||
            std::wcscmp(text, item.text) != 0) {
            std::fprintf(stderr, "Wrong MME menu text: resource %d command %d\n", item.menu,
                         item.command);
            ++failures;
        }
        if (menu)
            DestroyMenu(menu);
    }
    const wchar_t* dialogTexts[] = {
        L"エフェクトファイル割り当て", L"ファイル選択", L"解除",       L"非表示/表示",
        L"選択項目の一括操作",         L"OK",           L"キャンセル", L"更新"};
    for (const wchar_t* text : dialogTexts) {
        if (!DialogContains(module, IDD_MME_MAPPING, text)) {
            std::fprintf(stderr, "Missing original Japanese mapping dialog text\n");
            ++failures;
        }
    }
    HRSRC mapping = FindResourceA(module, MAKEINTRESOURCEA(IDD_MME_MAPPING), RT_DIALOG);
    const auto* data =
        mapping ? static_cast<const unsigned char*>(LockResource(LoadResource(module, mapping)))
                : nullptr;
    short bounds[4]{};
    if (data && SizeofResource(module, mapping) >= 26)
        std::memcpy(bounds, data + 18, sizeof bounds); // DLGTEMPLATEEX: x, y, cx, cy
    if (bounds[0] != 0 || bounds[1] != 0 || bounds[2] != 373 || bounds[3] != 215 ||
        !DialogContains(module, IDD_MME_MAPPING, L"MS Shell Dlg")) {
        std::fprintf(stderr, "MME mapping dialog layout differs from the original template\n");
        ++failures;
    }
    return failures;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    HMODULE module =
        LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr)
        return 3;
    struct Resource {
        LPCSTR type;
        int id;
    } required[] = {
        {RT_MENU, IDR_MME_MENU_JP},
        {RT_MENU, IDR_MME_MENU_EN},
        {RT_MENU, IDR_MME_MAPPING_MENU_JP},
        {RT_MENU, IDR_MME_MAPPING_MENU_EN},
        {RT_DIALOG, IDD_MME_LOG},
        {RT_DIALOG, IDD_MME_MAPPING},
        {RT_GROUP_ICON, IDI_MME_APP},
        {RT_GROUP_ICON, 100},
        {RT_RCDATA, 117},
        {RT_RCDATA, 118},
        {RT_MANIFEST, 1},
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
        const auto* group =
            static_cast<const unsigned char*>(LockResource(LoadResource(module, groupResource)));
        const DWORD bytes = SizeofResource(module, groupResource);
        WORD count = 0;
        if (group && bytes >= 6)
            std::memcpy(&count, group + 4, 2);
        if (!count || bytes != 6u + 14u * count)
            ++failures;
        else
            for (WORD index = 0; index < count; ++index) {
                WORD id = 0;
                std::memcpy(&id, group + 6 + 14 * index + 12, 2);
                if (id != IDI_MME_IMAGE_FIRST + index ||
                    !FindResourceA(module, MAKEINTRESOURCEA(id), RT_ICON))
                    ++failures;
            }
    }
    failures += CheckLanguageResources(module);
    FreeLibrary(module);
    if (!failures)
        std::puts("Host and MME menus, dialogs, icon images and shaders are embedded");
    return failures ? 1 : 0;
}
