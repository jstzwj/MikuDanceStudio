// =============================================================================
// MMDxShow.dll — Phase A: exports, class factory, registration  (dll_main.cpp)
// =============================================================================
// x64 flavour note (MikuMikuDanceE_v932x64/Data/MMDxShow.dll, VC10 rebuild):
// the registration/module-plumbing logic is identical function-for-function
// (same template walk, same CLSID/InprocServer32/ThreadingModel="Both" keys,
// same IFilterMapper2-then-IFilterMapper fallback, same FilterData via
// REGFILTER2{1, merit 0x200000}, same DllCanUnloadNow/DllMain shape).  The
// rebuild was compiled against the wide flavours of the registry/version
// APIs (its imports are RegCreateKeyW/RegSetValueW/RegEnumKeyExW/GetVersionExW
// + _vsnwprintf), where the VC8 x86 binary imports the ANSI ones.  The
// registry-visible state is byte-identical either way (the registry stores
// UTF-16 natively and converts ANSI on write), so this port keeps the x86
// ANSI shape for both architectures.
//
// Faithful port of the original module-plumbing functions:
//   DllMain                 0x10001C10 (thunk) -> 0x10005930
//   DllCanUnloadNow         0x10005AB0
//   DllGetClassObject       0x10005B30
//   DllRegisterServer       0x10001BF0 -> AMovieDllRegisterServer2(TRUE)
//   DllUnregisterServer     0x10001C00 -> AMovieDllRegisterServer2(FALSE)
//   CClassFactory ctor      0x10005AD0     QI          0x100059A0
//   AddRef                  0x100058C0     Release     0x10005B00
//   CreateInstance          0x10005A00     LockServer  0x100058D0
//   registry helpers        0x10005240 (recursive delete)
//                           0x10005330 (write CLSID/InprocServer32/ThreadingModel)
//                           0x10005550 (remove CLSID key)
//                           0x100055F0 (IFilterMapper2 register/unregister)
//                           0x10003400 (IFilterMapper  register/unregister)
//                           0x10005680 (per-template CLSID key loop)
//                           0x10005720 (AMovieDllRegisterServer2)
//                           0x100058F0 (per-template m_lpfnInit loop)
//   module lock helpers     0x10004F80 / 0x10004FA0
//   QI tail helper          0x10004FD0, GUID compare 0x10001000
// =============================================================================

#include <initguid.h> // instantiate every DEFINE_GUID below in this TU

#include "mmdxshow.hpp"

// --- GUIDs instantiated here (standard ones come from <strmif.h>/<uuid.h>) ---
// .rdata originals: CLSID_FilterMapper2 0x10008670, IID_IFilterMapper2 0x100085B0,
// CLSID_FilterMapper 0x10008680, IID_IFilterMapper 0x100085C0,
// IID_IAMovieSetup 0x10008690, MEDIATYPE_Video 0x10008720, GUID_NULL 0x100089EC,
// IID_IEnumPins 0x10008630, IID_IUnknown 0x10008A0C, IID_IClassFactory 0x100089FC.

// The mapper GUIDs are declared locally so the values are pinned to the
// binary's .rdata regardless of which SDK headers are available (the obsolete
// ActiveMovie CLSID_FilterMapper is missing from modern SDKs).
// {73646976-0000-0010-8000-00AA00389B71}  MEDIATYPE_Video == 0x10008720
DEFINE_GUID(MMDXSHOW_MEDIATYPE_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00,
            0x38, 0x9b, 0x71);
// GUID_NULL == 0x100089EC
DEFINE_GUID(MMDXSHOW_GUID_NULL, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
// {E436EBB2-524F-11CE-9F53-0020AF0BA770}  == 0x10008680
DEFINE_GUID(MMDXSHOW_CLSID_FilterMapper, 0xe436ebb2, 0x524f, 0x11ce, 0x9f, 0x53, 0x0, 0x20, 0xaf,
            0xb, 0xa7, 0x70);
// {56A868A3-0AD4-11CE-B03A-0020AF0BA770}  == 0x100085C0
DEFINE_GUID(MMDXSHOW_IID_IFilterMapper, 0x56a868a3, 0xad4, 0x11ce, 0xb0, 0x3a, 0x0, 0x20, 0xaf, 0xb,
            0xa7, 0x70);
// {CDA42200-BD88-11D0-BD4E-00A0C911CE86}  == 0x10008670
DEFINE_GUID(MMDXSHOW_CLSID_FilterMapper2, 0xcda42200, 0xbd88, 0x11d0, 0xbd, 0x4e, 0x0, 0xa0, 0xc9,
            0x11, 0xce, 0x86);
// {B79BB0B0-33C1-11D1-ABE1-00A0C905F375}  == 0x100085B0
DEFINE_GUID(MMDXSHOW_IID_IFilterMapper2, 0xb79bb0b0, 0x33c1, 0x11d1, 0xab, 0xe1, 0x0, 0xa0, 0xc9,
            0x5, 0xf3, 0x75);

// --- forward declarations (definitions live at the bottom, binary order) -----
static HRESULT AMovieDllRegisterServer2(BOOL bRegister); // 0x10005720
static void AMovieInitServer(BOOL bLoading);             // 0x100058F0

// =============================================================================
// Module state (original .data addresses in comments)
// =============================================================================

LONG g_cModuleRef = 0;       // 0x1000B2FC "Addend"
LONG g_cServerLocks = 0;     // 0x1000B3A4
HINSTANCE g_hInst = NULL;    // 0x1000B310 (hModule)
DWORD g_amPlatform = 0;      // 0x1000B308
OSVERSIONINFOA g_osVer = {}; // 0x1000B30C (VersionInformation)
HMODULE g_hOle32Lib = NULL;  // set by the Phase-B ole32 shim

// 0x10004F80
void MMDxShow_LockModule(void) {
    InterlockedIncrement(&g_cModuleRef);
}

// 0x10004FA0
void MMDxShow_UnlockModule(void) {
    if (InterlockedDecrement(&g_cModuleRef) == 0 && g_hOle32Lib != NULL) {
        FreeLibrary(g_hOle32Lib);
        g_hOle32Lib = NULL;
    }
}

// 0x10001000 — returns nonzero when the two GUIDs are EQUAL.
bool MMDxShow_IsEqualGUID16(const GUID* a, const GUID* b) {
    const DWORD* p = reinterpret_cast<const DWORD*>(a);
    const DWORD* q = reinterpret_cast<const DWORD*>(b);
    for (unsigned i = 4; i; --i) {
        if (*p != *q) {
            // the original walks the remaining bytes individually; the
            // observable result is identical to a plain 16-byte compare
            const BYTE* x = reinterpret_cast<const BYTE*>(a);
            const BYTE* y = reinterpret_cast<const BYTE*>(b);
            for (unsigned j = 0; j < 16; ++j)
                if (x[j] != y[j])
                    return false;
            return true;
        }
        ++p;
        ++q;
    }
    return true;
}

// 0x10004FD0
HRESULT MMDxShow_ReturnSelf(void* pSelf, void** ppv) {
    if (ppv == NULL)
        return E_POINTER;
    *ppv = pSelf;
    // (*(void***)pSelf)[1](pSelf)  — AddRef through the vtable
    reinterpret_cast<IUnknown*>(pSelf)->AddRef();
    return S_OK;
}

// --- AMediaType helpers ------------------------------------------------------

// 0x10004B80 / 0x10004DC0
void MMDxShow_InitMediaType(AM_MEDIA_TYPE* pmt) {
    // memset(this, 0, 0x48); *(DWORD*)(this+0x20) = 1; *(DWORD*)(this+0x28) = 1;
    ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));
    pmt->bFixedSizeSamples = TRUE;
    pmt->lSampleSize = 1;
}

// 0x10004D70 / 0x10004DB0
void MMDxShow_FreeMediaType(AM_MEDIA_TYPE* pmt) {
    if (pmt->cbFormat) {
        CoTaskMemFree(pmt->pbFormat);
        pmt->cbFormat = 0;
        pmt->pbFormat = NULL;
    }
    if (pmt->pUnk != NULL) {
        pmt->pUnk->Release();
        pmt->pUnk = NULL;
    }
}

// 0x10004D00
HRESULT MMDxShow_CopyMediaType(AM_MEDIA_TYPE* pmtDst, const AM_MEDIA_TYPE* pmtSrc) {
    memcpy(pmtDst, pmtSrc, sizeof(AM_MEDIA_TYPE));
    if (pmtSrc->cbFormat) {
        pmtDst->pbFormat = (BYTE*)CoTaskMemAlloc(pmtSrc->cbFormat);
        if (pmtDst->pbFormat == NULL) {
            pmtDst->cbFormat = 0;
            return E_OUTOFMEMORY;
        }
        memcpy(pmtDst->pbFormat, pmtSrc->pbFormat, pmtSrc->cbFormat);
    }
    if (pmtDst->pUnk != NULL)
        pmtDst->pUnk->AddRef();
    return S_OK;
}

// =============================================================================
// Registration data (0x1000B208 g_Templates / 0x1000B21C g_cTemplates)
// =============================================================================

// .rdata 0x10008720 / 0x100089EC
static const MMDXSHOW_MEDIATYPE_SETUP s_MediaTypeSetup = {
    &MMDXSHOW_MEDIATYPE_Video,
    &MMDXSHOW_GUID_NULL,
};

// .rdata 0x1000839C
static const MMDXSHOW_PIN_SETUP s_PinSetup = {
    L"Output",           // 0x10008374
    FALSE,               // bRendered
    TRUE,                // bOutput
    FALSE,               // bZero
    FALSE,               // bMany
    &MMDXSHOW_GUID_NULL, // 0x100089EC
    NULL,                // strConnectsToPin
    1,                   // nMediaTypes
    &s_MediaTypeSetup,
};

// .rdata 0x100083C0
static const MMDXSHOW_FILTER_SETUP s_FilterSetup = {
    &MMDXSHOW_CLSID_PushSourceDIBSq,         // 0x10008384
    L"PushSource DIBBitmap Sequence Filter", // 0x10008328
    0x00200000,                              // dwMerit
    1,                                       // nPins
    &s_PinSetup,
};

// .data 0x1000B208
const CFactoryTemplate g_Templates[1] = {
    {
        L"PushSource DIBBitmap Sequence Filter", // m_Name
        &MMDXSHOW_CLSID_PushSourceDIBSq,         // m_ClsID
        &MMDxShow_NewPushSourceDIBSq,            // m_lpfnNew (0x10001B70)
        NULL,                                    // m_lpfnInit
        &s_FilterSetup,                          // m_pAMovieSetup_Filter
    },
};
const int g_cTemplates = 1; // 0x1000B21C

// =============================================================================
// CClassFactory (vtable 0x10008798)
// =============================================================================

// 0x10005AD0
CClassFactory::CClassFactory(const CFactoryTemplate* pTemplate) {
    MMDxShow_LockModule();   // 0x10004F80 — InterlockedIncrement(&g_cModuleRef)
    m_pTemplate = pTemplate; // +0x04
    m_cRef = 0;              // +0x08 (DllGetClassObject AddRefs on the way out)
}

// 0x100059A0
STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == NULL)
        return E_POINTER;
    *ppv = NULL;
    if (MMDxShow_IsEqualGUID16(&riid, &IID_IUnknown) ||
        MMDxShow_IsEqualGUID16(&riid, &IID_IClassFactory))
        return MMDxShow_ReturnSelf(this, ppv);
    return E_NOINTERFACE;
}

// 0x100058C0 — plain ++, NOT interlocked (faithful)
STDMETHODIMP_(ULONG) CClassFactory::AddRef() {
    return (ULONG)++m_cRef;
}

// 0x10005B00
STDMETHODIMP_(ULONG) CClassFactory::Release() {
    const bool bLast = (m_cRef-- == 1);
    const ULONG cRef = (ULONG)m_cRef;
    if (bLast) {
        MMDxShow_UnlockModule(); // 0x10004FA0
        delete this;
        return 0;
    }
    return cRef;
}

// 0x10005A00
STDMETHODIMP CClassFactory::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void** ppv) {
    if (ppv == NULL)
        return E_POINTER;
    if (pUnkOuter != NULL && !MMDxShow_IsEqualGUID16(&riid, &IID_IUnknown))
        return E_NOINTERFACE;
    *ppv = NULL;

    HRESULT hr;
    // m_pTemplate->m_lpfnNew(pUnkOuter, &hr) — returns a CUnknown*; the binary
    // then drives it through the PRIMARY (INonDelegatingUnknown) vtable.
    CUnknown* pUnknown = m_pTemplate->m_lpfnNew(pUnkOuter, &hr);
    if (pUnknown == NULL) {
        if (hr >= 0)
            hr = E_OUTOFMEMORY;
        return hr;
    }
    if (hr >= 0) {
        pUnknown->NonDelegatingAddRef();                       // primary +0x04
        hr = pUnknown->NonDelegatingQueryInterface(riid, ppv); // primary +0x00
        pUnknown->NonDelegatingRelease();                      // primary +0x08
    } else {
        delete pUnknown; // scalar deleting dtor, primary +0x0C, flag 1
    }
    return hr;
}

// 0x100058D0 — plain ++/--
STDMETHODIMP CClassFactory::LockServer(BOOL fLock) {
    if (fLock)
        ++g_cServerLocks;
    else
        --g_cServerLocks;
    return S_OK;
}

// =============================================================================
// Exports
// =============================================================================

// 0x10005AB0
extern "C" HRESULT WINAPI DllCanUnloadNow(void) {
    // return g_cServerLocks > 0 || g_cModuleRef != 0 ? S_FALSE : S_OK;
    if (g_cServerLocks > 0 || g_cModuleRef != 0)
        return S_FALSE;
    return S_OK;
}

// 0x10005B30
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!MMDxShow_IsEqualGUID16(&riid, &IID_IUnknown) &&
        !MMDxShow_IsEqualGUID16(&riid, &IID_IClassFactory))
        return E_NOINTERFACE;

    if (g_cTemplates <= 0)
        return CLASS_E_CLASSNOTAVAILABLE; // 0x80040111

    const CFactoryTemplate* pTemplate = NULL;
    int i = 0;
    for (;; ++i) {
        if (i >= g_cTemplates)
            return CLASS_E_CLASSNOTAVAILABLE;
        if (MMDxShow_IsEqualGUID16(g_Templates[i].m_ClsID, &rclsid)) {
            pTemplate = &g_Templates[i];
            break;
        }
    }

    CClassFactory* pFactory = new CClassFactory(pTemplate); // operator new(0xC) / x64 new(0x18)
    *ppv = pFactory;
    if (pFactory == NULL)
        return E_OUTOFMEMORY;
    pFactory->AddRef(); // vtable +0x04
    return S_OK;
}

// 0x10005930 (the real body; 0x10001C10 is a 5-byte thunk)
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_DETACH) {
        AMovieInitServer(FALSE); // 0x100058F0(0)
        return TRUE;
    }
    if (fdwReason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(hinstDLL);

    g_amPlatform = 1; // VER_PLATFORM_WIN32_WINDOWS default
    g_osVer.dwOSVersionInfoSize = sizeof(g_osVer);
    if (GetVersionExA(&g_osVer))
        g_amPlatform = g_osVer.dwPlatformId;

    g_hInst = hinstDLL;

    AMovieInitServer(TRUE); // 0x100058F0(1)
    return TRUE;
}

extern "C" HRESULT WINAPI DllRegisterServer(void) {
    return AMovieDllRegisterServer2(TRUE); // 0x10005720(1)
}

extern "C" HRESULT WINAPI DllUnregisterServer(void) {
    return AMovieDllRegisterServer2(FALSE); // 0x10005720(0)
}

// =============================================================================
// Registration internals
// =============================================================================

// 0x10005240 — recursively delete a registry key
static LSTATUS RecursiveDeleteKeyA(HKEY hKey, LPCSTR lpSubKey) {
    HKEY hk;
    CHAR szName[260];
    DWORD cchName;
    FILETIME ftLastWriteTime;

    if (lstrlenA(lpSubKey) == 0)
        return (LSTATUS)0x80004005L; // the original returns E_FAIL here
    if (RegOpenKeyExA(hKey, lpSubKey, 0, KEY_ALL_ACCESS /*0x02000000*/, &hk) == ERROR_SUCCESS) {
        for (cchName = 260; RegEnumKeyExA(hk, 0, szName, &cchName, NULL, NULL, NULL,
                                          &ftLastWriteTime) == ERROR_SUCCESS;
             cchName = 260) {
            RecursiveDeleteKeyA(hk, szName);
        }
        RegCloseKey(hk);
        RegDeleteKeyA(hKey, lpSubKey);
    }
    return ERROR_SUCCESS;
}

// 0x10005330 — HKCR\CLSID\{...} = name, InprocServer32 = module, ThreadingModel = "Both"
// (the x64 rebuild's 0x180005730 is the same body over the W APIs, including
//  the fixed 260-element buffer passed as a 0x208-byte RegSetValueW size)
static HRESULT RegSetClassIdKey(REFCLSID clsid, LPCWSTR wszName, LPCWSTR wszModule,
                                LPCWSTR wszThreadingModel /* L"Both" */,
                                LPCWSTR wszServerKey /* L"InprocServer32" */) {
    HKEY hkClsid, hkServer;
    CHAR szKey[260];
    WCHAR wszGuid[40];
    LSTATUS lr;

    StringFromGUID2(clsid, wszGuid, 39);
    wsprintfA(szKey, "CLSID\\%ls", wszGuid);

    lr = RegCreateKeyA(HKEY_CLASSES_ROOT, szKey, &hkClsid);
    if (lr)
        return HRESULT_FROM_WIN32(lr);

    wsprintfA(szKey, "%ls", wszName);
    lr = RegSetValueA(hkClsid, NULL, REG_SZ, szKey, 260);
    if (lr == ERROR_SUCCESS) {
        wsprintfA(szKey, "%ls", wszServerKey);
        lr = RegCreateKeyA(hkClsid, szKey, &hkServer);
        if (lr == ERROR_SUCCESS) {
            wsprintfA(szKey, "%ls", wszModule);
            lr = RegSetValueA(hkServer, NULL, REG_SZ, szKey, lstrlenA(szKey) + 1);
            if (lr == ERROR_SUCCESS) {
                wsprintfA(szKey, "%ls", wszThreadingModel);
                lr = RegSetValueExA(hkServer, "ThreadingModel", 0, REG_SZ, (const BYTE*)szKey,
                                    lstrlenA(szKey) + 1);
            }
            RegCloseKey(hkServer);
        }
    }
    RegCloseKey(hkClsid);
    if (lr > 0)
        return (HRESULT)((WORD)lr | 0x80070000); // faithful WORD-cast quirk
    return (HRESULT)lr;
}

// 0x10005550
static HRESULT RegRemoveClassIdKey(REFCLSID clsid) {
    CHAR szKey[260];
    WCHAR wszGuid[40];

    StringFromGUID2(clsid, wszGuid, 39);
    wsprintfA(szKey, "CLSID\\%ls", wszGuid);
    RecursiveDeleteKeyA(HKEY_CLASSES_ROOT, szKey);
    return S_OK;
}

// 0x10005680 — write/remove the CLSID keys for every template
static HRESULT RegisterServerClassIds(LPCWSTR wszModule, BOOL bRegister) {
    HRESULT hr = S_OK;
    for (int i = 0; i < g_cTemplates; ++i) {
        const CFactoryTemplate* pT = &g_Templates[i];
        if (bRegister)
            hr = RegSetClassIdKey(*pT->m_ClsID, pT->m_Name, wszModule, L"Both", L"InprocServer32");
        else
            hr = RegRemoveClassIdKey(*pT->m_ClsID);
        if (FAILED(hr))
            break;
    }
    return hr;
}

// 0x100055F0 (x64 0x180005670) — IFilterMapper2 path.
// Builds REGFILTER2 {1, dwMerit, cPins, rgPins} out of the setup blob.
// NOTE: the original calls vtable+0x10 (UnregisterFilter) with three pushed
// arguments (NULL, NULL, clsid) — an artifact of the old headers it was built
// with; the modern two-parameter call is used here (semantically identical).
static HRESULT RegisterFilterMapper2(const MMDXSHOW_FILTER_SETUP* pSetup, IFilterMapper2* pMapper,
                                     BOOL bRegister) {
    if (pSetup == NULL)
        return S_FALSE; // the original returns 1 (S_FALSE)

    // three-argument push (NULL, NULL, clsid), exactly like the binary
    HRESULT hr = pMapper->UnregisterFilter(NULL, NULL, *pSetup->clsID);
    if (bRegister) {
        REGFILTER2 rf2;
        rf2.dwVersion = 1;
        rf2.dwMerit = pSetup->dwMerit;
        rf2.cPins = pSetup->nPins;
        rf2.rgPins = (REGFILTERPINS*)pSetup->lpPin;
        hr = pMapper->RegisterFilter(*pSetup->clsID, pSetup->szName, NULL, NULL, NULL, &rf2);
    }
    return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) ? S_OK : hr;
}

// 0x10003400 — IFilterMapper (v1) path (old ActiveMovie systems).
// External linkage: push_source.cpp's CBaseFilter::Register/Unregister
// documentation references this path.
HRESULT RegisterFilterMapper1(const MMDXSHOW_FILTER_SETUP* pSetup, IFilterMapper* pMapper,
                              BOOL bRegister) {
    if (pSetup == NULL)
        return S_FALSE;

    const CLSID clsid = *pSetup->clsID;
    HRESULT hr = pMapper->UnregisterFilter(clsid); // vtable+0x1C
    if (bRegister) {
        hr = pMapper->RegisterFilter(clsid, pSetup->szName, pSetup->dwMerit); // +0x0C
        if (SUCCEEDED(hr)) {
            for (UINT p = 0; p < pSetup->nPins; ++p) {
                const MMDXSHOW_PIN_SETUP* pPin = &pSetup->lpPin[p];
                hr = pMapper->RegisterPin(clsid, pPin->strName, // +0x14
                                          pPin->bRendered, pPin->bOutput, pPin->bZero, pPin->bMany,
                                          *pPin->clsConnectsToFilter, pPin->strConnectsToPin);
                if (FAILED(hr))
                    break;
                for (UINT t = 0; t < pPin->nMediaTypes; ++t) {
                    const MMDXSHOW_MEDIATYPE_SETUP* pMt = &pPin->lpMediaType[t];
                    hr = pMapper->RegisterPinType(clsid, pPin->strName, // +0x18
                                                  *pMt->clsMajorType, *pMt->clsSubType);
                    if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                        return hr;
                }
            }
        }
    }
    return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) ? S_OK : hr;
}

// 0x10005720
static HRESULT AMovieDllRegisterServer2(BOOL bRegister) {
    CHAR szFile[MAX_PATH];
    WCHAR wszFile[MAX_PATH];
    HRESULT hr;

    if (!GetModuleFileNameA(g_hInst, szFile, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    MultiByteToWideChar(CP_ACP, 0, szFile, lstrlenA(szFile) + 1, wszFile, MAX_PATH);

    if (bRegister) {
        hr = RegisterServerClassIds(wszFile, TRUE);
        if (FAILED(hr))
            return hr;
    }

    CoInitialize(NULL);

    IFilterMapper2* pFM2 = NULL;
    IFilterMapper* pFM1 = NULL;
    hr = CoCreateInstance(MMDXSHOW_CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
                          MMDXSHOW_IID_IFilterMapper2, (void**)&pFM2);
    if (FAILED(hr))
        hr = CoCreateInstance(MMDXSHOW_CLSID_FilterMapper, NULL, CLSCTX_INPROC_SERVER,
                              MMDXSHOW_IID_IFilterMapper, (void**)&pFM1);

    if (SUCCEEDED(hr)) {
        for (int i = 0; i < g_cTemplates; ++i) {
            const MMDXSHOW_FILTER_SETUP* pSetup = g_Templates[i].m_pAMovieSetup_Filter;
            if (pSetup != NULL) {
                if (pFM2 != NULL)
                    hr = RegisterFilterMapper2(pSetup, pFM2, bRegister);
                else
                    hr = RegisterFilterMapper1(pSetup, pFM1, bRegister);
            }
            if (FAILED(hr))
                break;
        }
        IUnknown* pUnk = pFM2 ? (IUnknown*)pFM2 : (IUnknown*)pFM1;
        pUnk->Release(); // vtable +0x08
    }

    CoFreeUnusedLibraries();
    CoUninitialize();

    if (SUCCEEDED(hr) && !bRegister)
        return RegisterServerClassIds(wszFile, FALSE);
    return hr;
}

// 0x100058F0 — run every template's m_lpfnInit (NULL in this DLL -> no-op)
static void AMovieInitServer(BOOL bLoading) {
    for (int i = 0; i < g_cTemplates; ++i) {
        if (g_Templates[i].m_lpfnInit)
            g_Templates[i].m_lpfnInit(bLoading, g_Templates[i].m_ClsID);
    }
}
