#pragma once
#include <cstddef>

namespace mikudancestudio {
class D3DRenderer;

// Windows ACP first, then the renderer's ordered locale fallbacks.
void ConvertAnsiToWide(const D3DRenderer* renderer, const char* source,
                       wchar_t* destination, int destinationWords);
// CP932 first; a default-character substitution also triggers the locale chain.
// An empty source leaves the destination untouched.
void WideToSjis(const D3DRenderer* renderer, char* destination,
                const wchar_t* source, std::size_t destinationBytes);
}  // namespace mikudancestudio
