#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <array>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"

namespace mikudancestudio {
namespace {
const std::array<_locale_t, 4>& CpuLocales() {
    // Same candidates as renderer initialization, with CPU-only ownership.
    // Creating locale objects does not change the process/global locale.
    struct Locales {
        std::array<_locale_t, 4> values{_create_locale(LC_ALL, ""), _create_locale(LC_ALL, ".OCP"),
                                        _create_locale(LC_ALL, ".ACP"),
                                        _create_locale(LC_ALL, "JPN")};
        ~Locales() {
            for (auto value : values)
                if (value)
                    _free_locale(value);
        }
    };
    static const Locales locales;
    return locales.values;
}
}

void ConvertAnsiToWide(const D3DRenderer* renderer, const char* source, wchar_t* destination,
                       int destinationWords) {
    wcscpy_s(destination, destinationWords, L"");
    if (*source == '\0')
        return;

    const int count = MultiByteToWideChar(CP_ACP, 0, source, -1, nullptr, 0);
    auto buffer = std::make_unique<wchar_t[]>(count);
    if (MultiByteToWideChar(CP_ACP, 0, source, -1, buffer.get(), count)) {
        wcsncpy_s(destination, destinationWords, buffer.get(), _TRUNCATE);
        return;
    }
    // Startup can precede renderer construction. Keep its safe CRT fallback.
    if (!renderer) {
        mbstowcs_s(nullptr, destination, destinationWords, source, _TRUNCATE);
        return;
    }
    for (auto locale : renderer->localeTable) {
        if (_mbstowcs_s_l(nullptr, destination, destinationWords, source, _TRUNCATE, locale) == 0)
            break;
    }
}

void WideToSjis(const D3DRenderer* renderer, char* destination, const wchar_t* source,
                std::size_t destinationBytes) {
    if (*source == L'\0')
        return;

    const int count = WideCharToMultiByte(932, 0, source, -1, nullptr, 0, nullptr, nullptr);
    auto buffer = std::make_unique<char[]>(count);
    BOOL usedDefault = FALSE;
    if (WideCharToMultiByte(932, 0, source, -1, buffer.get(), count, nullptr, &usedDefault) &&
        !usedDefault) {
        strncpy_s(destination, destinationBytes, buffer.get(), _TRUNCATE);
        return;
    }
    const auto cpu = renderer ? nullptr : CpuLocales().data();
    for (unsigned i = 0; i < 4; ++i) {
        const auto locale = renderer ? renderer->localeTable[i] : cpu[i];
        // STRUNCATE is nonzero: like any other failure it advances the chain.
        if (_wcstombs_s_l(nullptr, destination, destinationBytes, source, _TRUNCATE, locale) == 0)
            break;
    }
}
} // namespace mikudancestudio
