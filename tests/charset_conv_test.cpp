#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <locale.h>
#include <memory>

using namespace mikudancestudio;

int main() {
    auto renderer = std::make_unique<D3DRenderer>();
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) { std::fprintf(stderr, "%s\n", name); ++failures; }
    };
    const char* localeNames[] = {".1250", ".1254", ".1255", ".1256"};
    const wchar_t* inputs[] = {L"\u0141", L"\u011e", L"\u05d0", L"\u0639"};
    for (int i = 0; i < 4; ++i) {
        renderer->localeTable[i] = _create_locale(LC_ALL, localeNames[i]);
        check(renderer->localeTable[i] != nullptr, "create candidate locale");
    }
    if (failures) return failures;

    char result[32] = "retained";
    WideToSjis(renderer.get(), result, L"", sizeof(result));
    check(std::strcmp(result, "retained") == 0, "empty wide source preserves destination");
    WideToSjis(renderer.get(), result, L"\u521d\u97f3", sizeof(result));
    check(std::strcmp(result, "\x8f\x89\x89\xb9") == 0, "CP932 wins over locale candidates");
    for (int i = 0; i < 4; ++i) {
        BOOL substituted = FALSE;
        char probe[32]{};
        WideCharToMultiByte(932, 0, inputs[i], -1, probe, sizeof(probe),
                            nullptr, &substituted);
        check(substituted != FALSE, "sample must trigger CP932 fallback");
        // CRT CP1250 best-fit maps G-breve to ASCII G and stops there.
        const unsigned char expected[] = {0xa3, 0x47, 0xe0, 0xda};
        WideToSjis(renderer.get(), result, inputs[i], sizeof(result));
        check(static_cast<unsigned char>(result[0]) == expected[i] && result[1] == '\0',
              "ordered locale fallback keeps first successful conversion");
    }
    WideToSjis(renderer.get(), result, L"\U0001f600", sizeof(result));
    check(result[0] == '\0', "all locale candidates fail");
    char shortResult[2]{};
    WideToSjis(renderer.get(), shortResult, L"\u0141\u0141", sizeof(shortResult));
    check(shortResult[0] == '\0', "STRUNCATE advances to the next locale");
    wchar_t wide[32] = L"retained";
    ConvertAnsiToWide(renderer.get(), "", wide, 32);
    check(wide[0] == L'\0', "empty narrow source clears destination");
    ConvertAnsiToWide(nullptr, "model.pmx", wide, 32);
    check(std::wcscmp(wide, L"model.pmx") == 0, "startup ASCII without renderer");
    for (auto locale : renderer->localeTable) _free_locale(locale);
    for (int successIndex = 0; successIndex < 4; ++successIndex) {
        for (int i = 0; i < 4; ++i)
            renderer->localeTable[i] = _create_locale(
                LC_ALL, i == successIndex ? ".1250" : "C");
        WideToSjis(renderer.get(), result, L"\u0141", sizeof(result));
        check(static_cast<unsigned char>(result[0]) == 0xa3 && result[1] == '\0',
              "each of four fallback positions can supply the first success");
        for (auto locale : renderer->localeTable) _free_locale(locale);
    }
    return failures;
}
