// mme_util.h - small helpers shared by the MMEffect reconstruction.
//
// The original uses CP 0 (system ANSI = CP932 on Japanese MMD installs) for all
// of its string conversions: MultiByteToWideChar(0, ...) / WideCharToMultiByte(0, ...)
// (big-C 6978 / 79539). We keep CP 0 for behavioral parity.
#pragma once

#include <string>

namespace mme {

// WideCharToMultiByte(CP 0) equivalent [big-C 79539].
std::string MmeWideToAnsi(const wchar_t* ws);

// MultiByteToWideChar(CP 0) equivalent [big-C 6978].
std::wstring MmeAnsiToWide(const char* s);

// wsprintfA/_snprintf-style formatter (FUN_1800608c0 shape). Result truncated
// like the original's fixed 40-byte scratch usage; callers keep messages short.
std::string MmeFormat(const char* fmt, ...);

// strlen-style helper matching the original's inline scan loops
// (e.g. 1800564a0 L53-60).
unsigned long long MmeStrlen(const char* s);

} // namespace mme
