// mme_util.cpp - see mme_util.h
#include "mme_util.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mme {

std::string MmeWideToAnsi(const wchar_t* ws)
{
    if (ws == nullptr) {
        return std::string();
    }
    // [big-C 79539] WideCharToMultiByte(0, 0, ws, -1, NULL, 0, NULL, NULL) sizing pass,
    // then the conversion into the string buffer (CP 0 = system ANSI / CP932).
    int need = WideCharToMultiByte(0, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return std::string();
    }
    std::string out;
    out.resize(static_cast<size_t>(need) - 1); // drop the terminating L'\0'
    WideCharToMultiByte(0, 0, ws, -1, &out[0], need, nullptr, nullptr);
    return out;
}

std::wstring MmeAnsiToWide(const char* s)
{
    if (s == nullptr) {
        return std::wstring();
    }
    // [big-C 6978] MultiByteToWideChar(0, 0, s, -1, NULL, 0) sizing pass.
    int need = MultiByteToWideChar(0, 0, s, -1, nullptr, 0);
    if (need <= 0) {
        return std::wstring();
    }
    std::wstring out;
    out.resize(static_cast<size_t>(need) - 1);
    MultiByteToWideChar(0, 0, s, -1, &out[0], need);
    return out;
}

std::string MmeFormat(const char* fmt, ...)
{
    // FUN_1800608c0 calls wsprintfA-shaped formatting into a stack buffer;
    // we use vsnprintf with a bounded buffer for the same observable output.
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) {
        n = static_cast<int>(sizeof(buf) - 1);
    } else if (n > static_cast<int>(sizeof(buf) - 1)) {
        n = static_cast<int>(sizeof(buf) - 1);
    }
    buf[n] = '\0';
    return std::string(buf, static_cast<size_t>(n));
}

unsigned long long MmeStrlen(const char* s)
{
    // [1800564a0 L53-60] the original scans with a decrementing counter and
    // uses ~uVar12 - 1 as the length; identical result to strlen for NUL-safe
    // inputs, but we additionally tolerate nullptr like the callers' fallbacks.
    if (s == nullptr) {
        return 0;
    }
    return static_cast<unsigned long long>(std::strlen(s));
}

} // namespace mme
