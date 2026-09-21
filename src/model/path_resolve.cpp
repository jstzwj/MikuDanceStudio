// ===========================================================================
// VA 0x004089F0 - ResolveUserFilePath (original: sub_4089F0, 0x47D bytes)
// VA 0x00407DA0 - ConvertMaterialName (original: sub_407DA0, 0x280 bytes)
// VA 0x00407490 - LoadTextureShared  (original: sub_407490, 0x1CD bytes)
// ===========================================================================
// Path helpers of the model-load chain.
//
// 0x4089F0: __thiscall(appPathBuf, widePath).  Resolves a user file by
//   probing, in order: "%s%s"(buf+512 dir, tail-from-"UserFile"), the same
//   with buf+0, the raw path, then the UserFile\Model / UserFile\Wave /
//   UserFile\Accessory fallbacks.  The last 3 wchars (&path[len-3]) are
//   compared case-sensitively against the explicit variants PMD/pmd,
//   WAV/wav, X/x, VAC/vac (inlined wcscmp at 0x7FF7CB429D78..0x7FF7CB42A033;
//   the one-char "X"/"x" forms compare 2 words and can never match a
//   3-wchar tail in the original either - kept as-is).  Result is
//   stored wide at buf wchar 1512 (byte 3024); 0 on failure.
//
// 0x407DA0: __thiscall(renderSub, ansiName, wideOut, sizeInWords, dirW).
//   Converts a narrow (SJIS) PMD material/texture name to wide, trying the
//   ACP conversion first and then four stored _locale_t handles at
//   renderSub dwords 30001..30004 (bytes 120004..120016), keeping the
//   first variant whose file exists.  NOTE: the original passes the wide
//   buffers to _sopen_s - the CRT entry is actually _wsopen_s (wide);
//   documented as an IDA FLIRT mislabel in docs/ARCHITECTURE.md.
//
// 0x407490: __thiscall(renderSub, widePath).  Shared texture cache: up to
//   10000 entries of (texture*, name*, pixel-colour) at renderSub dword 1
//   (byte 4), stride 12.  Loads via D3DXCreateTextureFromFileExW (mipmaps
//   1024 with a no-mipmap retry), stores the bottom-left pixel colour at
//   entry+12..14, returns 1 on success.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// minimal D3DX constants/struct for the dynamic loader (no SDK headers)
constexpr UINT kD3dxDefault = 0xFFFFFFFFu;
struct ImgInfo {                    // first fields of D3DXIMAGE_INFO
    UINT Width, Height, Depth, MipLevels;
    UINT rest[16];
};

bool WsOpenOk(const wchar_t* path) {
    int fh = -1;
    if (_wsopen_s(&fh, path, _O_BINARY, _SH_DENYNO, _S_IREAD) == 0) {
        _close(fh);
        return true;
    }
    return false;
}

}  // namespace

const wchar_t* ResolveUserFilePath(PathResolutionWorkspace& workspace,
                                   const wchar_t* path) {      // 0x4089F0
    wchar_t* out = workspace.resolvedPath;
    const wchar_t* dirBase = workspace.executableDirectory;
    wchar_t tmp[256];
    if (path[0] == L'\0') {
        out[0] = L'\0';
        return out;
    }
    const wchar_t* uf = wcsstr(path, L"UserFile");
    if (uf != nullptr) {
        swprintf_s(tmp, 0x100, L"%s%s", dirBase, uf);
        bool ok = WsOpenOk(tmp);
        if (!ok &&
            workspace.projectDirectory[0] != L'\0') {
        swprintf_s(tmp, 0x100, L"%s%s",
                       workspace.projectDirectory, uf);
            ok = WsOpenOk(tmp);
        }
        if (ok) {
            wcscpy_s(out, 0x100, tmp);
            return out;
        }
    }
    if (WsOpenOk(path)) {
        wcscpy_s(out, 0x100, path);
        return out;
    }
    const size_t len = wcslen(path);
    const wchar_t* ext = &path[len - 3];
    const wchar_t* scan = ext;
    const wchar_t* sub = nullptr;
    if (wcscmp(scan, L"pmd") == 0 || wcscmp(scan, L"PMD") == 0)
        sub = L"UserFile\\Model";
    else if (wcscmp(&path[len - 3], L"wav") == 0 ||
             wcscmp(&path[len - 3], L"WAV") == 0)
        sub = L"UserFile\\Wave";
    else if ((wcscmp(&path[len - 3], L"x") == 0 ||
              wcscmp(&path[len - 3], L"X") == 0) ||
             (wcscmp(&path[len - 3], L"vac") == 0 ||
              wcscmp(&path[len - 3], L"VAC") == 0))
        sub = L"UserFile\\Accessory";
    if (sub == nullptr) {
        out[0] = L'\0';
        return out;
    }
    // walk back to the last path separator (fail if none)
    while (*scan != L'\\') {
        --scan;
        if (scan == path) {
            out[0] = L'\0';
            return out;
        }
    }
    swprintf_s(tmp, 0x100, L"%s%s%s", dirBase, sub, scan);
    if (WsOpenOk(tmp)) {
        wcscpy_s(out, 0x100, tmp);
        return out;
    }
    out[0] = L'\0';
    return out;
}

bool ResolveAnsiUserFile(unsigned char* sub, const char* mbName,
                         wchar_t* wideOut, rsize_t sizeWords,
                         PathResolutionWorkspace& paths) {     // 0x407BA0
    wcscpy_s(wideOut, sizeWords, L"");
    if (mbName == nullptr || mbName[0] == '\0')
        return false;
    wchar_t candidate[512] = {};
    auto resolve = [&]() {
        const wchar_t* value = ResolveUserFilePath(paths, candidate);
        if (value[0] == L'\0')
            return false;
        wcsncpy_s(wideOut, sizeWords, value, _TRUNCATE);
        return true;
    };
    MultiByteToWideChar(CP_ACP, 0, mbName, -1, candidate,
                        static_cast<int>(sizeWords));
    if (resolve())
        return true;
    for (int i = 0; i < 4; ++i) {
        _mbstowcs_s_l(nullptr, candidate, sizeWords, mbName, _TRUNCATE,
            reinterpret_cast<D3DRenderer*>(sub)->localeTable[i]);
        if (resolve())
            return true;
    }
    return false;
}

errno_t ConvertMaterialName(unsigned char* sub, const char* mbName,
                            wchar_t* wideOut, rsize_t sizeWords,
                            const wchar_t* dirW) {             // 0x407DA0
    wchar_t probe[512];
    errno_t result = wcscpy_s(wideOut, sizeWords, L"");
    if (mbName[0] == '\0')
        return result;

    int cw = MultiByteToWideChar(0, 0, mbName, -1, nullptr, 0);
    wchar_t* tmp = static_cast<wchar_t*>(malloc(2 * cw));
    if (MultiByteToWideChar(0, 0, mbName,
                            static_cast<int>(strlen(mbName)) + 1, tmp, cw))
        wcsncpy_s(wideOut, sizeWords, tmp, _TRUNCATE);
    free(tmp);

    // probe ACP result, then the four stored locale conversions
    for (int li = 0; li < 5; ++li) {
        swprintf_s(probe, 0x200, L"%s%s", dirW, wideOut);
        if (WsOpenOk(probe))
            return 0;
        if (li < 4)
            _mbstowcs_s_l(nullptr, wideOut, sizeWords, mbName, _TRUNCATE,
                reinterpret_cast<D3DRenderer*>(sub)->localeTable[li]);
    }
    return result;
}

int LoadTextureShared(unsigned char* sub, wchar_t* path) {     // 0x407490
    if (path[0] == L'\0')
        return 0;
    D3DRenderer* r = reinterpret_cast<D3DRenderer*>(sub);
    IDirect3DDevice9* dev = r->device;
    // Cache = the wrapper's 10000-entry resourcePool (12-byte stride,
    // original write base sub+0 at 0x40750A): heapBuffer holds the name*,
    // comObject the texture*; the rgb triple is written into the tag dword
    // (original +0xC..+0xE), which lands in the NEXT entry's first dword
    // in the flat view (never scanned).  The original scan base is sub+4
    // (0x4074AF), i.e. it reads {name, tex} pairs.
    // (Port fix: an earlier layout read the texture pointer as the name,
    // crashing the wcscmp scan on the second texture - found by desktop
    // runtime testing with Luka_Megurine.pmd, docs/ARCHITECTURE.md §8.)
    int used = 0;
    while (used < 10000) {
        wchar_t* nm = static_cast<wchar_t*>(
            r->resourcePool[used].heapBuffer);
        if (nm == nullptr)
            break;
        if (wcscmp(nm, path) == 0)
            return 1;
        ++used;
    }
    // Pool exhausted: the original fell through and wrote entry[10000],
    // spilling name/texture pointers into localeTable[0..2] (the entries
    // right behind the pool).  10000 distinct textures are unreachable in
    // practice; refuse the cache instead of corrupting the locale pointers.
    if (used >= 10000)
        return 0;
    // The RGB sample occupies the low three bytes of this entry's tag.
    // Do not retain the original x86 wrapper-relative +0xC/+0xE offsets:
    // x64 pointer widening would otherwise overwrite the next cache entry.
    unsigned char* tagBytes =
        reinterpret_cast<unsigned char*>(&r->resourcePool[used].tag);
    auto* entryTex = reinterpret_cast<IDirect3DTexture9**>(
        &r->resourcePool[used].comObject);
    auto* entryName = reinterpret_cast<wchar_t**>(
        &r->resourcePool[used].heapBuffer);
    ImgInfo info = {};
    HRESULT hr;
    auto* d3dx = &d3dx::Get();
    hr = d3dx->fromFileExW(dev, path, kD3dxDefault, kD3dxDefault, 1, 1024,
                           D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, kD3dxDefault,
                           kD3dxDefault, 0, &info, nullptr, entryTex);
    if (info.Width == 1 && info.Height == 1 && hr == 0) {
        if (*entryTex != nullptr) {
            (*entryTex)->Release();
            *entryTex = nullptr;
        }
        hr = d3dx->fromFileExW(dev, path, kD3dxDefault, kD3dxDefault, 1, 0,
                               D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                               kD3dxDefault, kD3dxDefault, 0, &info, nullptr,
                               entryTex);
    }
    if (FAILED(hr) &&
        FAILED(d3dx->fromFileExW(dev, path, kD3dxDefault, kD3dxDefault, 1, 0,
                                 D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                 kD3dxDefault, kD3dxDefault, 0, &info,
                                 nullptr, entryTex))) {
        *entryTex = nullptr;
        return 0;
    }
    const size_t plen = wcslen(path) + 1;
    *entryName = static_cast<wchar_t*>(malloc(2 * plen));
    wcscpy_s(*entryName, plen, path);

    // 内置 MMEffect：按路径登记新纹理（对应原版对
    // D3DXCreateTextureFromFileExW 的 hook 记录；toonNN.bmp 裸名别名由
    // MME 侧自动登记）。
    mme::RecordTexture(path, *entryTex);

    D3DLOCKED_RECT lr;
    // bottom-row probe locks read-only (x64 0x7FF7CB428E31 passes 0x10)
    if (SUCCEEDED((*entryTex)->LockRect(0, &lr, nullptr,
                                        D3DLOCK_READONLY))) {
        const unsigned char* px =
            static_cast<const unsigned char*>(lr.pBits) +
            lr.Pitch * (info.Height ? info.Height - 1 : 0);
        tagBytes[2] = px[0];  // blue
        tagBytes[1] = px[1];  // green
        tagBytes[0] = px[2];  // red
        (*entryTex)->UnlockRect(0);
    }
    return 1;
}

}  // namespace mikudancestudio
