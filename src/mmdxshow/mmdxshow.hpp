// =============================================================================
// MMDxShow.dll — Phase A skeleton (MikuDanceStudio 1:1 replica)
// =============================================================================
// Target binary : MikuMikuDanceE_v932/Data/MMDxShow.dll
//               (32-bit x86, VC8 / MSVCR80, DirectShow push-source filter)
// Image base    : 0x10000000   (all binary addresses below use this base)
//
// The x64 flavour ships as MikuMikuDanceE_v932x64/Data/MMDxShow.dll (image
// base 0x180000000, VC10 / MSVCR100 rebuild of the same source).  It exports
// the same five ordinals, keeps every GUID/string/registration blob and every
// vtable slot order identical, and differs ONLY in data layout (8-byte
// pointers, 40-byte CRITICAL_SECTION, natural 8-byte alignment).  Where the
// layout is pinned below, both flavours are pinned: x86 asserts carry the
// VC8 offsets, the _M_X64 variants carry the VC10 rebuild's (each derived
// from that binary's constructors / operator-new sizes the same way).
//
// Everything in this header was derived from the ORIGINAL binary:
//   * class hierarchy and per-interface vtable slot order  -> .rdata RTTI
//     (CompleteObjectLocators at 0x10008A78..0x10009448 + type descriptors
//     at 0x1000B000..0x1000B2D0) and from the vtables themselves
//     (0x1000817C .. 0x100087A8).
//   * GUID values -> .rdata (see below for the exact addresses).
//   * function bodies -> Hex-Rays decompilation via IDA (ground truth; the
//     corpus under translated/MMDxShow/*.cpp was used as a cross-check only).
//
// Implementations: dll_main.cpp and enumerators.cpp handle COM construction;
// source_base.cpp, push_source.cpp and push_pin.cpp implement the filter.
// Native class layouts remain pinned independently for both original ABIs.
//
// =============================================================================

#pragma once

#ifndef _MMDXSHOW_H_
#define _MMDXSHOW_H_

#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <strmif.h> // IBaseFilter, IPin, IEnumPins, IEnumMediaTypes,
                    // IFilterMapper, IFilterMapper2, REGFILTER2, ...

#ifndef VFW_E_ENUM_OUT_OF_SYNC
#define VFW_E_ENUM_OUT_OF_SYNC ((HRESULT)0x80040203L)
#endif

// Transport-path trace (stderr, opt-in via MMDX_TRACE=1).  Used by the
// Phase B push-source port while the frame pipeline was being verified.
inline void MMDXTrace(const char* fmt, ...) {
    static const bool enabled = !!std::getenv("MMDX_TRACE");
    if (!enabled)
        return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

// -----------------------------------------------------------------------------
// IAMovieSetup — present in the binary's vtables (CSource/CBaseFilter third
// interface at object offset +0x10) but no longer declared by modern SDK
// headers; declare it only if the SDK has not.
// vtable @0x100084C8 (CSource):  QI, AddRef, Release, Register, Unregister
// -----------------------------------------------------------------------------
#ifndef __IAMovieSetup_INTERFACE_DEFINED__
#define __IAMovieSetup_INTERFACE_DEFINED__
#undef INTERFACE
#define INTERFACE IAMovieSetup
DECLARE_INTERFACE_(IAMovieSetup, IUnknown) {
    STDMETHOD(Register)(THIS) PURE;
    STDMETHOD(Unregister)(THIS) PURE;
};
#endif // __IAMovieSetup_INTERFACE_DEFINED__

// =============================================================================
// GUIDs (values extracted from the original .rdata)
// =============================================================================

// --- the filter CLSID served by the class factory ---------------------------
// .rdata 0x10008384; referenced by g_Templates[0].m_ClsID and by the
// AMOVIESETUP_FILTER blob at 0x100083C0.  Matches the EXE-side contract
// document (IID 2F1713B8-DD1F-4186-93BE-FDA50BF687C7).
// {2F1713B8-DD1F-4186-93BE-FDA50BF687C7}
DEFINE_GUID(MMDXSHOW_CLSID_PushSourceDIBSq, 0x2f1713b8, 0xdd1f, 0x4186, 0x93, 0xbe, 0xfd, 0xa5, 0xb,
            0xf6, 0x87, 0xc7);

// --- the custom frame-push interface the MMD EXE talks to -------------------
// .rdata 0x1000830C.  Matches the EXE-side contract document
// (IID ECFAB031-72BA-4120-B9F7-8A3D5FD38DEC).
// Second base of CPushSourceDIBSq at object offset +0x70; 5 methods.
// {ECFAB031-72BA-4120-B9F7-8A3D5FD38DEC}
DEFINE_GUID(MMDXSHOW_IID_IPushSource, 0xecfab031, 0x72ba, 0x4120, 0xb9, 0xf7, 0x8a, 0x3d, 0x5f,
            0xd3, 0x8d, 0xec);

// --- ActiveMovie-1.0-era IID_IEnumMediaTypes --------------------------------
// .rdata 0x10008620.  NOTE: this is NOT the modern DirectShow
// {89C31040-846B-11CE-8D43-00AA004CEC69} — the original DLL was compiled
// against old ActiveMovie headers and CEnumMediaTypes::QueryInterface
// compares against THIS value only.  Kept bit-faithful.
// {89C31040-846B-11CE-97D3-00AA0055595A}
DEFINE_GUID(MMDXSHOW_IID_IEnumMediaTypes, 0x89c31040, 0x846b, 0x11ce, 0x97, 0xd3, 0x0, 0xaa, 0x0,
            0x55, 0x59, 0x5a);

// The remaining GUIDs used by the registration path are the standard
// CLSID_FilterMapper2 {CDA42200-...} / IID_IFilterMapper2 {B79BB0B0-...} /
// CLSID_FilterMapper {E436EBB2-...} / IID_IFilterMapper {56A868A3-...}
// (.rdata 0x10008670 / 0x100085B0 / 0x10008680 / 0x100085C0) and are taken
// from <strmif.h> + uuid.lib; they are instantiated in dll_main.cpp via
// #include <initguid.h>.

// =============================================================================
// Module state — the original .data globals
// =============================================================================

// .data 0x1000B2FC ("Addend").  Object/module reference count.
// Incremented by every CClassFactory/CUnknown-ish construction
// (0x10004F80: InterlockedIncrement), decremented in 0x10004FA0.
extern LONG g_cModuleRef;

// .data 0x1000B3A4.  IClassFactory::LockServer(TRUE/FALSE) counter
// (plain ++/--, NOT interlocked — faithful to 0x100058D0).
extern LONG g_cServerLocks;

// .data 0x1000B310.  Instance handle stored by DllMain (0x10005930);
// used by DllRegisterServer's GetModuleFileNameA.
extern HINSTANCE g_hInst;

// .data 0x1000B308.  Platform id; defaults to 1 (VER_PLATFORM_WIN32_WINDOWS)
// and overwritten from GetVersionExA in DllMain.
extern DWORD g_amPlatform;

// OSVERSIONINFOA used by DllMain.
extern OSVERSIONINFOA g_osVer;

// Module handle kept for the ole32 delay-load shim (the "ole32.dll" /
// "CoInitializeEx" GetProcAddress path).  0x10004FA0 calls FreeLibrary on it
// when the module refcount drops to zero; it is loaded by the Phase-B ole32
// helper (fcn_10005be0/0x10005c10 cluster).
extern HMODULE g_hOle32Lib;

// --- module lock helpers (0x10004F80 / 0x10004FA0) --------------------------
void MMDxShow_LockModule(void);   // InterlockedIncrement(&g_cModuleRef)
void MMDxShow_UnlockModule(void); // InterlockedDecrement + FreeLibrary shim

// 0x10001000 — 16-byte comparator used everywhere the original compares
// IIDs/CLSIDs.  Returns nonzero when the two GUIDs are EQUAL (the "lexicographic
// diff" shape is just memcmp(a,b,16)==0 after inlining).  Kept as a bool here.
bool MMDxShow_IsEqualGUID16(const GUID* a, const GUID* b);

// 0x10004FD0 — shared "return this interface pointer" tail of every QI in
// this DLL:  if (!ppv) E_POINTER; *ppv = self; self->AddRef(); return S_OK.
// (Implemented once in dll_main.cpp so all Phase-B QIs can reuse it.)
HRESULT MMDxShow_ReturnSelf(void* pSelf, void** ppv);

// =============================================================================
// AMediaType helpers (strmbase CMediaType equivalents, C ABI)
//   0x10004B80 / 0x10004DC0 : InitMediaType   — memset(0x48), bFixedSizeSamples
//                                                 = TRUE, lSampleSize = 1
//   0x10004D70 / 0x10004DB0 : FreeMediaType   — frees pbFormat, releases pUnk
//   0x10004D00              : CopyMediaType   — memcpy + deep copy + AddRef
// (InitMediaType/FreeMediaType are defined in enumerators.cpp — Phase A —
//  because the enumerators need them; Phase B code reuses them.)
// =============================================================================
void MMDxShow_InitMediaType(AM_MEDIA_TYPE* pmt);
void MMDxShow_FreeMediaType(AM_MEDIA_TYPE* pmt);
HRESULT MMDxShow_CopyMediaType(AM_MEDIA_TYPE* pmtDst, const AM_MEDIA_TYPE* pmtSrc);

// =============================================================================
// Registration data — the original .rdata blob (addresses in comments)
// =============================================================================

// 0x10008394 : { &MEDIATYPE_Video (0x10008720), &GUID_NULL (0x100089EC) }
struct MMDXSHOW_MEDIATYPE_SETUP {
    const CLSID* clsMajorType; // 0x10008720 MEDIATYPE_Video
    const CLSID* clsSubType;   // 0x100089EC GUID_NULL (unspecified)
};

// 0x1000839C (36 bytes — identical layout to AMOVIESETUP_PIN / REGFILTERPINS)
struct MMDXSHOW_PIN_SETUP {
    const WCHAR* strName;                        // 0x10008374 L"Output"
    BOOL bRendered;                              // FALSE
    BOOL bOutput;                                // TRUE
    BOOL bZero;                                  // FALSE
    BOOL bMany;                                  // FALSE
    const CLSID* clsConnectsToFilter;            // 0x100089EC GUID_NULL
    const WCHAR* strConnectsToPin;               // NULL
    UINT nMediaTypes;                            // 1
    const MMDXSHOW_MEDIATYPE_SETUP* lpMediaType; // 0x10008394
};

// 0x100083C0 (20 bytes).  NOTE: this is NOT the SDK AMOVIESETUP_FILTER
// (which lacks a name member); the original defined its own struct with the
// friendly name at [1].  RegisterFilterMapper2 (0x100055F0) builds a REGFILTER2
// {1, dwMerit, nPins, lpPin} from fields [2..4] of this struct.
struct MMDXSHOW_FILTER_SETUP {
    const CLSID* clsID;              // 0x10008384 {2F1713B8-...}
    const WCHAR* szName;             // 0x10008328 L"PushSource DIBBitmap Sequence Filter"
    DWORD dwMerit;                   // 0x00200000
    UINT nPins;                      // 1
    const MMDXSHOW_PIN_SETUP* lpPin; // 0x1000839C
};

// 0x1000B210 : LPFNNEWCOMOBJECT.  The original returns a CUnknown* whose
// PRIMARY (INonDelegatingUnknown) vtable is used by
// CClassFactory::CreateInstance (sub_10005A00 calls slots +4/+0/+8 and the
// slot +0x0C scalar deleting destructor).
class CUnknown;
typedef CUnknown* (*MMDXSHOW_LPNEWCOMOBJECT)(LPUNKNOWN pUnkOuter, HRESULT* phr);

// Phase B entry point (binary 0x10001B70): news 0x78 bytes and runs the
// CPushSourceDIBSq constructor (0x10001A70).  Defined with the Phase-B filter.
CUnknown* MMDxShow_NewPushSourceDIBSq(LPUNKNOWN pUnkOuter, HRESULT* phr);

// 0x1000B208 — g_Templates; 0x1000B21C — g_cTemplates (= 1)
struct CFactoryTemplate {
    const WCHAR* m_Name;                                    // 0x1000B208 -> 0x10008328
    const CLSID* m_ClsID;                                   // 0x1000B20C -> 0x10008384
    MMDXSHOW_LPNEWCOMOBJECT m_lpfnNew;                      // 0x1000B210 -> 0x10001B70
    void (*m_lpfnInit)(BOOL bLoading, const CLSID* rclsid); // 0x1000B214 -> NULL
    const MMDXSHOW_FILTER_SETUP* m_pAMovieSetup_Filter;     // 0x1000B218 -> 0x100083C0
};
extern const CFactoryTemplate g_Templates[1];
extern const int g_cTemplates;

// =============================================================================
// Class hierarchy (exactly as the RTTI says)
// =============================================================================
//
//   CBaseObject                         (no vtable; empty bookkeeping base)
//   INonDelegatingUnknown               (+0x00 primary vtable root)
//   CUnknown         : INonDelegatingUnknown, CBaseObject      [0x00..0x0B]
//   CBaseFilter      : CUnknown, IBaseFilter, IAMovieSetup     [+0x0C, +0x10]
//   CSource          : CBaseFilter                            (nothing new)
//   CPushSourceDIBSq : CSource, IPushSource                   [+0x70]
//
//   CAMThread        (root, object +0x00, size 0x48)
//   CBasePin         : CUnknown, IPin, IQualityControl         [+0x0C, +0x54? see below]
//   CBaseOutputPin   : CBasePin
//   CSourceStream    : CAMThread, CBaseOutputPin               (CBaseOutputPin at +0x48)
//   CPushPinDIBSq    : CSourceStream
//
//   CEnumPins        : IEnumPins        (Phase A, fully implemented)
//   CEnumMediaTypes  : IEnumMediaTypes  (Phase A, fully implemented)
//   CClassFactory    : IClassFactory    (Phase A, dll_main.cpp)
//
// x64 flavour (VC10 rebuild): same hierarchy and vtable slot order, with
//   CUnknown 0x18 | IBaseFilter +0x18 | IAMovieSetup +0x20 | IPushSource +0xB0
//   CSource 0xB0   | CPushSourceDIBSq 0xC0
//   CAMThread 0x78 | CBasePin subobject +0x78 (IPin +0x90, IQualityControl
//   +0x98 relative to the pin object), size 0xF0 | CPushPinDIBSq 0x660
//
// RTTI verification (CompleteObjectLocator -> ClassHierarchyDescriptor):
//   CSource        COL 0x100092B4/0x100092C8/0x100092DC (off 0, 0xC, 0x10)
//   CPushSourceDIBSq COL 0x10008F2C (off 0) + 0x10009228/0x1000923C/0x10009250
//   CSourceStream  COL 0x10009264/0x10009278/0x1000928C/0x100092A0
//   CPushPinDIBSq  COL 0x10008A78/0x10008EF0/0x10008F04/0x10008F18
// =============================================================================

// -----------------------------------------------------------------------------
// CBaseObject — empty bookkeeping base in the original (RTTI only).  It
// carries no data and no vtable slot; omitted here so the CUnknown layout
// matches the binary exactly.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// INonDelegatingUnknown (RTTI TD 0x1000B0A0) — the strmbase "inner" IUnknown.
// The vtable at object offset +0x00 of every filter/pin IS this interface plus
// whatever virtuals the derived classes append.
// -----------------------------------------------------------------------------
class INonDelegatingUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid,
                                                                  void** ppv) = 0; // +0x00
    virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef() = 0;                     // +0x04
    virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease() = 0;                    // +0x08
};

// -----------------------------------------------------------------------------
// CUnknown : INonDelegatingUnknown
//   CORRECTED layout (verified from the QI/AddRef adjustor thunks reading
//   this-8 / this-0xC and the interlocked stubs 0x10005080/0x100050A0):
//     +0x00 vptr | +0x04 m_pOuterUnknown | +0x08 m_cRef
//   => sizeof(CUnknown) == 0x0C, which is why IBaseFilter sits at +0x0C.
//   x64 (VC10 ctor 0x1800051D0): +0x00 vptr | +0x08 m_pOuterUnknown |
//   +0x10 m_cRef => sizeof == 0x18, IBaseFilter at +0x18.
//   The three NonDelegating methods are overridden HERE (shared AddRef/Release
//   stubs 0x10005080/0x100050A0 interlock m_cRef; NonDelegatingRelease
//   re-increments to 1 before invoking the deleting dtor on zero).  Derived
//   classes override QI only (filter 0x10002790, pin 0x10002DC0).
// -----------------------------------------------------------------------------
class CUnknown : public INonDelegatingUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv); // +0x00
    virtual ULONG STDMETHODCALLTYPE NonDelegatingAddRef();  // +0x04 0x10005080
    virtual ULONG STDMETHODCALLTYPE NonDelegatingRelease(); // +0x08 0x100050A0
    virtual ~CUnknown();                                    // vtable +0x0C
protected:
    CUnknown(const char* /*pName*/, LPUNKNOWN pUnkOuter)
        : m_pOuterUnknown(pUnkOuter ? pUnkOuter : (LPUNKNOWN)(INonDelegatingUnknown*)this),
          m_cRef(0) {}
    LPUNKNOWN m_pOuterUnknown; // object +0x04
    LONG m_cRef;               // object +0x08 (interlocked)
};

// -----------------------------------------------------------------------------
// CBaseFilter : CUnknown, IBaseFilter, IAMovieSetup      (Phase B)
//   object layout: +0x00 CUnknown (0x0C) | +0x0C IBaseFilter vptr (15 slots)
//                 | +0x10 IAMovieSetup vptr (5 slots)
//   x64: +0x00 CUnknown (0x18) | +0x18 IBaseFilter | +0x20 IAMovieSetup
//   IBaseFilter vtable @0x100084E4 (CSource) / @0x100082A4 (CPushSourceDIBSq):
//     +0x00..0x08  QueryInterface/AddRef/Release      0x10001180/0x10001710/0x100011A0
//     +0x0C  GetClassID                              0x10002860
//     +0x10  Stop                                    0x100037A0
//     +0x14  Pause                                   0x10003850
//     +0x18  Run                                     0x10003910
//     +0x1C  GetState                                0x10002890
//     +0x20  SetSyncSource                           0x10003680
//     +0x24  GetSyncSource                           0x10003700
//     +0x28  EnumPins                                0x10004A10
//     +0x2C  FindPin                                 0x10001CA0
//     +0x30  QueryFilterInfo                         0x100028F0
//     +0x34  JoinFilterGraph                         0x10002950
//     +0x38  QueryVendorInfo                         0x10002A60
//   IAMovieSetup vtable @0x100084C8:
//     +0x00..0x08  QI/AddRef/Release                 0x10001920/0x10001900/0x10002740
//     +0x0C  Register                                0x10003A10
//     +0x10  Unregister                              0x10003A80
//   PRIMARY (INonDelegatingUnknown) vtable continues after CUnknown's 4 slots:
//     +0x10  StreamTime                              0x100028B0  (graph-clock delta)
//     +0x14  GetPinVersion                           0x10002AB0  (used by CEnumPins)
//     +0x18  GetPinCount                             0x10002090  (pure)
//     +0x1C  GetPin(int)                             0x100020B0  (pure; CSource
//                                    returns m_ppPins[n]+0x48 == CBasePin subobj)
//     +0x20  GetSetupData                            0x100011D0  (returns NULL)
// -----------------------------------------------------------------------------
class CBaseFilter : public CUnknown, public IBaseFilter, public IAMovieSetup {
public:
    // --- primary (INonDelegatingUnknown) vtable, continued ---
    // StreamTime — was Filter_v10_StreamTime (primary vtable +0x10).  strmbase
    // spells it StreamTime(CRefTime&); the binary ABI is the same single
    // pointer argument (retn 4) to the 64-bit time cell.
    virtual HRESULT StreamTime(REFERENCE_TIME* prtStream); // +0x10  0x100028B0
    virtual LONG GetPinVersion();                          // +0x14  0x10002AB0
    virtual int GetPinCount() = 0;                         // +0x18  CSource 0x10002090
    virtual class CBasePin* GetPin(int n) = 0;             // +0x1C  CSource 0x100020B0
    // GetSetupData — was Filter_v20 (primary vtable +0x20).  strmbase:
    // virtual LPAMOVIESETUP_FILTER GetSetupData(); the 0x100011D0 stub is
    // `xor eax,eax; ret` -> NULL, so Register/Unregister take the S_FALSE path.
    virtual const MMDXSHOW_FILTER_SETUP* GetSetupData(); // +0x20  0x100011D0

    // --- IBaseFilter overrides (sub-vtable at object +0x0C, canonical) ---
    // Delegating IUnknown for the IBaseFilter/IAMovieSetup branches
    // (binary: per-branch small forwards 0x10001180/0x10001710/0x100011A0).
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    STDMETHOD(GetClassID)(CLSID* pClassID);                          // 0x10002860
    STDMETHOD(Stop)();                                               // 0x100037A0
    STDMETHOD(Pause)();                                              // 0x10003850
    STDMETHOD(Run)(REFERENCE_TIME tStart);                           // 0x10003910
    STDMETHOD(GetState)(DWORD dwMillis, FILTER_STATE* pState);       // 0x10002890
    STDMETHOD(SetSyncSource)(IReferenceClock* pClock);               // 0x10003680
    STDMETHOD(GetSyncSource)(IReferenceClock** ppClock);             // 0x10003700
    STDMETHOD(EnumPins)(IEnumPins** ppEnum);                         // 0x10004A10
    STDMETHOD(FindPin)(LPCWSTR Id, IPin** ppPin);                    // 0x10001CA0
    STDMETHOD(QueryFilterInfo)(FILTER_INFO* pInfo);                  // 0x100028F0
    STDMETHOD(JoinFilterGraph)(IFilterGraph* pGraph, LPCWSTR pName); // 0x10002950
    STDMETHOD(QueryVendorInfo)(LPWSTR* pVendorInfo);                 // 0x10002A60

    // --- IAMovieSetup overrides (sub-vtable at object +0x10) ---
    STDMETHOD(Register)();   // 0x10003A10
    STDMETHOD(Unregister)(); // 0x10003A80

    // --- NonDelegating QI override (primary +0x00) ---
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid,
                                                          void** ppv) override; // 0x10002790

protected:
    CBaseFilter(const char* pName, LPUNKNOWN pUnkOuter) : CUnknown(pName, pUnkOuter) {}

public:
    // Data members — byte layout pinned to the original 0x100049B0 ctor:
    //   +0x14 state, +0x18 clock, +0x20 tStart(8), +0x28 CLSID INLINE (16
    //   bytes, +0x28..0x37 — GetClassID copies it out via this[7..10] of
    //   the +0x0C branch), +0x38 pLock (=&m_CritSec), +0x3C name,
    //   +0x40 graph, +0x44 event sink, +0x48 pin version(1), +0x50 pin
    //   count, +0x54 pin array, +0x58 critsec(24B) -> sizeof == 0x70.
    //  x64 rebuild (ctor 0x180003F80): +0x28 state, +0x30 clock, +0x38
    //  tStart, +0x40 CLSID, +0x50 pLock, +0x58 name, +0x60 graph, +0x68
    //  event sink, +0x70 pin version, +0x78 pin count, +0x80 pin array,
    //  +0x88 critsec(40B) -> sizeof(CSource) == 0xB0.
    // (A previous declaration ordered m_pGraph at +0x28 which pushed every
    //  later member down 4 bytes; the C++ member references then desynced
    //  from the F_xxx hard-offset accessors - JoinFilterGraph crashed
    //  EnterCriticalSection on a NULL pLock. public so the offsetof
    //  static_asserts below compile; the pin also locks m_CritSec across
    //  classes like the original.)
    FILTER_STATE m_State;           // +0x14 / x64 +0x28
    IReferenceClock* m_pClock;      // +0x18 / x64 +0x30
    REFERENCE_TIME m_tStart;        // +0x20 (8 bytes; compiler pads +0x1C) / x64 +0x38
    CLSID m_clsid;                  // +0x28 (16 bytes, by value) / x64 +0x40
    CRITICAL_SECTION* m_pLockSlot;  // +0x38 (ctor stores &m_CritSec here) / x64 +0x50
    WCHAR* m_pName;                 // +0x3C / x64 +0x58
    IFilterGraph* m_pGraph;         // +0x40 / x64 +0x60
    IUnknown* m_pEventSink;         // +0x44 / x64 +0x68
    LONG m_PinVersion;              // +0x48 (ctor inits 1) / x64 +0x70
    int m_pad4C;                    // +0x4C (unidentified) / x64 pad at +0x74
    int m_cPins;                    // +0x50 / x64 +0x78
    class CSourceStream** m_ppPins; // +0x54 / x64 +0x80
    CRITICAL_SECTION m_CritSec;     // +0x58 (24 bytes) / x64 +0x88 (40 bytes)
};

// -----------------------------------------------------------------------------
// CSource : CBaseFilter                                (Phase B)
//   Own vtables @0x10008524/0x100084E4/0x100084C8 (same targets as CBaseFilter,
//   separate RTTI COLs 0x100092B4/0x100092C8/0x100092DC).  sizeof(CSource) ==
//   0x70 (CPushSourceDIBSq is 0x78 with IPushSource's vptr at +0x70).
// -----------------------------------------------------------------------------
class CSource : public CBaseFilter {
public:
    CSource(
        const char* pName, LPUNKNOWN pUnkOuter,
        const CLSID* pClsID); // 0x10001E90 -> CBaseFilter 0x100049B0(name, punk, critsec, clsid)
    virtual ~CSource();       // +0x0C 0x10002750
    // primary vtable overrides:
    int GetPinCount() override;       // +0x18 0x10002090
    CBasePin* GetPin(int n) override; // +0x1C 0x100020B0
    // pin-array management used by CSourceStream (Phase B, source_base.cpp)
    void AddPin(CSourceStream* pPin); // part of 0x10001A70/ctor chain
    void RemovePin(CSourceStream* pPin);
};

// Sizes / member offsets pinned per flavour: x86 against the 0x100049B0 ctor
// and operator new(0x70) at 0x10001B70; x64 against the VC10 rebuild's ctor
// 0x180003F80 and the 0xC0 filter allocation at 0x180001A60 (CSource part
// ends at the IPushSource vptr, +0xB0).
#if defined(_M_X64)
static_assert(sizeof(CSource) == 0xB0, "x64 CSource must end at the IPushSource vptr slot (+0xB0)");
static_assert(offsetof(CSource, m_State) == 0x28, "m_State");
static_assert(offsetof(CSource, m_pClock) == 0x30, "m_pClock");
static_assert(offsetof(CSource, m_tStart) == 0x38, "m_tStart");
static_assert(offsetof(CSource, m_clsid) == 0x40, "inline CLSID");
static_assert(offsetof(CSource, m_pLockSlot) == 0x50, "pLock slot");
static_assert(offsetof(CSource, m_pName) == 0x58, "m_pName");
static_assert(offsetof(CSource, m_pGraph) == 0x60, "m_pGraph");
static_assert(offsetof(CSource, m_pEventSink) == 0x68, "m_pEventSink");
static_assert(offsetof(CSource, m_PinVersion) == 0x70, "m_PinVersion");
static_assert(offsetof(CSource, m_cPins) == 0x78, "m_cPins");
static_assert(offsetof(CSource, m_ppPins) == 0x80, "m_ppPins");
static_assert(offsetof(CSource, m_CritSec) == 0x88, "m_CritSec");
#elif defined(_M_IX86)
static_assert(sizeof(CSource) == 0x70,
              "CSource layout must match operator new(0x70) at 0x10001B70");
// CBaseFilter member offsets pinned to the original 0x100049B0 ctor (the
// class-incomplete rule keeps these out of the class body).
static_assert(offsetof(CSource, m_State) == 0x14, "m_State");
static_assert(offsetof(CSource, m_pClock) == 0x18, "m_pClock");
static_assert(offsetof(CSource, m_tStart) == 0x20, "m_tStart");
static_assert(offsetof(CSource, m_clsid) == 0x28, "inline CLSID");
static_assert(offsetof(CSource, m_pLockSlot) == 0x38, "pLock slot");
static_assert(offsetof(CSource, m_pName) == 0x3C, "m_pName");
static_assert(offsetof(CSource, m_pGraph) == 0x40, "m_pGraph");
static_assert(offsetof(CSource, m_pEventSink) == 0x44, "m_pEventSink");
static_assert(offsetof(CSource, m_PinVersion) == 0x48, "m_PinVersion");
static_assert(offsetof(CSource, m_cPins) == 0x50, "m_cPins");
static_assert(offsetof(CSource, m_ppPins) == 0x54, "m_ppPins");
static_assert(offsetof(CSource, m_CritSec) == 0x58, "m_CritSec");
#endif

// -----------------------------------------------------------------------------
// IPushSource : IUnknown — the EXE-side frame contract (5 methods)
//   IID  {ECFAB031-72BA-4120-B9F7-8A3D5FD38DEC}   (.rdata 0x1000830C)
//   vtable @0x10008268 (CPushSourceDIBSq):
//     +0x00 QueryInterface   0x10001930
//     +0x04 AddRef           0x10001910
//     +0x08 Release          0x10001940
//     +0x0C 0x10001730 — stores a 0x28-byte BITMAPINFOHEADER + int at the pin
//            (pin+1440), computes AvgTimePerFrame = 10000000/fps; fails with
//            E_FAIL once streaming has started (pin+1459 flag).
//     +0x10 0x100017D0 — streaming-state getter (corpus: GetStreamingState)
//     +0x14 0x10001800 — StartStreaming (corpus name)
//     +0x18 0x10001840 — corpus name: BeginStreaming
//     +0x1C 0x10001880 — writes *pRate = 1.02 (double) when not streaming
// -----------------------------------------------------------------------------
interface IPushSource : public IUnknown {
    // (+0x00..+0x08 QI/AddRef/Release are per-class forwards:
    //  CPushSourceDIBSq provides 0x10001930/0x10001910/0x10001940.)
    virtual HRESULT STDMETHODCALLTYPE SetBitmapInfo(
        const void* pBitmapInfo, int n,
        float fps) = 0; // 0x10001730 (retn 0x10) — was PushSource_v0C_SetBitmapInfo
    virtual HRESULT STDMETHODCALLTYPE GetStreamingState(
        void* pState) = 0; // 0x100017D0 — was PushSource_v10_GetStreamingState
    // StartStreaming's parameter is the EXE's frame-bits pointer: DWORD on
    // the x86 ABI, a full 8-byte store in the x64 rebuild (DWORD_PTR covers
    // both).  Stored verbatim at the pin's frame-bits slot.
    virtual HRESULT STDMETHODCALLTYPE StartStreaming(
        DWORD_PTR dwBits) = 0; // 0x10001800 — was PushSource_v14_StartStreaming
    virtual HRESULT STDMETHODCALLTYPE BeginStreaming(
        void) = 0; // 0x10001840 — was PushSource_v18_BeginStreaming
    virtual HRESULT STDMETHODCALLTYPE GetRate(
        float* pRate) = 0; // 0x10001880 (fstp dword, 1.02f) — was PushSource_v1C_GetRate
};

class CPushPinDIBSq; // forward: parked-pin member below

// -----------------------------------------------------------------------------
// CPushSourceDIBSq : CSource, IPushSource              (Phase B)
//   object: +0x70 IPushSource vptr; sizeof == 0x78 (operator new(0x78) at
//   0x10001B70).  x64: +0xB0 vptr, sizeof == 0xC0 (operator new(0xC0) at
//   0x180001A60).  The class factory serves CLSID 2F1713B8-... via
//   g_Templates[0].m_lpfnNew -> MMDxShow_NewPushSourceDIBSq.
// -----------------------------------------------------------------------------
class CPushSourceDIBSq : public CSource, public IPushSource {
public:
    CPushSourceDIBSq(LPUNKNOWN pUnkOuter, HRESULT* phr); // TODO(phase B) 0x10001A70
    virtual ~CPushSourceDIBSq();                         // +0x0C 0x10001B50
    // NonDelegating QI override (primary +0x00, 0x100018B0):
    // IID_IPushSource -> ReturnSelf(this+0x70); else CBaseFilter's.
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
    // IPushSource delegating IUnknown (0x10001930/0x10001910/0x10001940)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    // IPushSource (declared above) — Phase B defines:
    STDMETHODIMP SetBitmapInfo(const void* pBitmapInfo, int n, float fps) override;
    STDMETHODIMP GetStreamingState(void* pState) override;
    STDMETHODIMP StartStreaming(DWORD_PTR dwBits) override;
    STDMETHODIMP BeginStreaming(void) override;
    STDMETHODIMP GetRate(float* pRate) override;
    // member: the parked pin (ctor 0x10001A70)
    CPushPinDIBSq* m_pPin; // +0x74 / x64 +0xB8
};
#if defined(_M_X64)
static_assert(sizeof(CPushSourceDIBSq) == 0xC0, "x64 filter layout must match operator new(0xC0)");
static_assert(offsetof(CPushSourceDIBSq, m_pPin) == 0xB8, "x64 m_pPin");
#elif defined(_M_IX86)
static_assert(sizeof(CPushSourceDIBSq) == 0x78, "filter layout must match operator new(0x78)");
#endif

// -----------------------------------------------------------------------------
// CAMThread — primary base of CSourceStream (object offset +0x00, size 0x48).
//   Root vtable @0x100084A4 (CSourceStream) / @0x10008244 (CPushPinDIBSq):
//     +0x00 ThreadProc           0x10002570 (SHARED GetRequest/Reply command
//                                   pump — verified: GetRequest 0x10005E80
//                                   waits +0x04 and returns +0x0C; cmds 1/2
//                                   dispatch vtable+0x0C, exit vtable+0x10)
//     +0x04 scalar deleting dtor 0x100021C0 / 0x10001A50 (per-class; CSource's
//                                   dtor deletes pins through this slot)
//     +0x08 FillBuffer (pure)    0x1000668A (_purecall) / 0x100014E0
//     +0x0C OnThreadInit         0x100011D0 (stub returns 0 — NOT E_NOTIMPL;
//                  was CamThread_v0C.  ThreadProc start hook: a negative
//                  return aborts the pump with Reply(that value))
//     +0x10 OnThreadExit         0x100011D0 (was CamThread_v10.  ThreadProc
//                  exit hook: ThreadProc returns (hook() < 0))
//     +0x14 OnLoopEnter          0x100011D0 (was CamThread_v14.  Called once
//                  at the top of DoBufferProcessingLoop)
//     +0x18 DoBufferProcessingLoop 0x10002630 (was CamThread_v18; the
//                  strmbase CSourceStream::DoBufferProcessingLoop body)
//     +0x1C GetMediaType(AM_MEDIA_TYPE*) 0x10003370 stub / 0x100011F0
//   Data layout (verified from dtor core 0x10005D50: CAMEvent dtors on
//   +0x04/+0x08, DeleteCriticalSection on +0x18/+0x30):
//     +0x04 hEventSend | +0x08 hEventReply | +0x0C uParam | +0x10 uReply
//     +0x14 hThread | +0x18 CritSec (24B) | +0x30 CritSec (24B) => 0x48
//   x64 (ctor 0x180006080): +0x08/+0x10 events, +0x18/+0x1C params,
//   +0x20 hThread, +0x28/+0x50 critsecs (40B each) => 0x78.
// -----------------------------------------------------------------------------
class CAMThread {
    friend struct MMDxShowStateAccess;

public:
    virtual DWORD ThreadProc();                            // +0x00 0x10002570
    virtual ~CAMThread();                                  // +0x04 per-class
    virtual HRESULT FillBuffer(IMediaSample* pSample) = 0; // +0x08
    virtual DWORD
    OnThreadInit(); // +0x0C 0x100011D0 (was CamThread_v0C; name inferred from the ThreadProc call site — no strmbase counterpart survives)
    virtual DWORD
    OnThreadExit(); // +0x10 0x100011D0 (was CamThread_v10; name inferred from the ThreadProc call site)
    virtual DWORD
    OnLoopEnter(); // +0x14 0x100011D0 (was CamThread_v14; name inferred from the DoBufferProcessingLoop call site)
    virtual DWORD
    DoBufferProcessingLoop(); // +0x18 0x10002630 (was CamThread_v18; strmbase CSourceStream name)
    // (+0x1C GetMediaType(CMediaType*) is introduced by CSourceStream because
    //  CBasePin::GetMediaType dispatches it through the CAMThread-root vtable.)
    // thread API (Phase B, source_base.cpp): GetRequest 0x10005E80 /
    // CheckRequest / Reply 0x10005ED0 / CallWorker / CreateThread / Close
protected:
    CAMThread();
    HANDLE m_hEventSend;             // +0x04 (auto-reset; waited by GetRequest) / x64 +0x08
    HANDLE m_hEventReply;            // +0x08 / x64 +0x10
    DWORD m_uParam;                  // +0x0C (read by GetRequest) / x64 +0x18
    DWORD m_uReply;                  // +0x10 / x64 +0x1C
    HANDLE m_hThread;                // +0x14 / x64 +0x20
    CRITICAL_SECTION m_CritSecSend;  // +0x18 (24 bytes) / x64 +0x28 (40 bytes)
    CRITICAL_SECTION m_CritSecReply; // +0x30 (24 bytes) / x64 +0x50 (40 bytes)
};
#if defined(_M_X64)
static_assert(sizeof(CAMThread) == 0x78, "x64 CAMThread layout must match the VC10 rebuild");
#elif defined(_M_IX86)
static_assert(sizeof(CAMThread) == 0x48, "CAMThread layout must match the binary");
#endif

// -----------------------------------------------------------------------------
// CBasePin : CUnknown, IPin, IQualityControl           (Phase B)
//   RTTD (CPushPinDIBSq CHD 0x10008A8C): CBasePin at +0x48 of the pin object,
//   with its OWN CUnknown/INonDelegating vptr at +0x48 (23 slots), IPin vptr at
//   +0x54 (18 slots, canonical COM order), IQualityControl vptr at +0x58
//   (5 slots).  NOTE: because CSourceStream's CBaseOutputPin base starts at
//   +0x48 and CUnknown is 0x0C bytes, IPin lands at +0x54 exactly as observed.
//   x64: CBasePin subobject at pin+0x78 (CUnknown 0x18 head), IPin vptr at
//   +0x90, IQualityControl at +0x98; subobject size 0xF0 (ctor 0x180004720).
//   Primary (INonDelegating) vtable @0x10008444 (CSourceStream)/@0x100081E4.
//   Slot order and per-slot stack arity (retn N) match the DirectShow
//   baseclasses (strmbase amfilter.h) one-to-one: CBasePin's virtuals
//   GetMediaTypeVersion/Active/Inactive/Run/CheckMediaType/SetMediaType/
//   CheckConnect/BreakConnect/CompleteConnect/GetMediaType, then the
//   CBaseOutputPin additions DecideAllocator/DecideBufferSize/
//   GetDeliveryBuffer/Deliver/InitAllocator/DeliverEndOfStream/
//   DeliverBeginFlush/DeliverEndFlush/DeliverNewSegment.
//     +0x00/+0x04/+0x08  NonDelegating QI/AddRef/Release  0x10002DC0/0x10002E50/0x10002E70
//     +0x0C  scalar deleting dtor                          0x10001E50 / 0x10001950
//     +0x10  GetMediaTypeVersion                           0x10003030 (used by CEnumMediaTypes)
//     +0x14  Active                                        0x10002370 (ret 0; dispatched by
//             CBaseFilter::Pause with NO stack argument)
//     +0x18  Inactive                                      0x100024B0 (ret 0; Stop)
//     +0x1C  Run(REFERENCE_TIME)                           0x10003040 (ret 8; CBaseFilter::Run
//             forwards tStart as one 64-bit argument)
//     +0x20  CheckMediaType(const AM_MEDIA_TYPE*)          0x100021E0 / 0x10001430
//     +0x24  SetMediaType(const AM_MEDIA_TYPE*)            0x10002E90
//     +0x28  CheckConnect(IPin*)                           0x100030F0
//     +0x2C  BreakConnect                                  0x10003130
//     +0x30  CompleteConnect(IPin*)                        0x100030D0 (delegates to the
//             pin's own +0x38 slot)
//     +0x34  GetMediaType(int, AM_MEDIA_TYPE*)             0x100022B0 (locks the
//             filter CS; iPosition==0 dispatches to the CAMThread-root +0x1C
//             single-arg GetMediaType; else S_FALSE)
//     +0x38  DecideAllocator                               0x100031A0
//     +0x3C  DecideBufferSize                              0x10001310
//     +0x40  GetDeliveryBuffer                             0x100032A0
//     +0x44  Deliver                                       0x100032E0
//     +0x48  InitAllocator                                 0x10003190
//     +0x4C  DeliverEndOfStream                            0x10003310
//     +0x50  DeliverBeginFlush                             0x10003380
//     +0x54  DeliverEndFlush                               0x100033A0
//     +0x58  DeliverNewSegment                             0x100033C0
//   IPin vtable @0x10008194 — canonical order:
//     QI/AddRef/Release stubs (0x10001180/0x10001710/0x100011A0), Connect
//     0x100047B0, ReceiveConnection 0x10004160, Disconnect 0x100042D0,
//     ConnectedTo 0x10002F20, ConnectionMediaType 0x10004360, QueryPinInfo
//     0x10002F60, QueryDirection 0x10002FE0, QueryId 0x10001D40, QueryAccept
//     0x10003000, EnumMediaTypes 0x100048D0, QueryInternalConnections
//     0x100011C0, EndOfStream/BeginFlush/EndFlush 0x10003370 stubs,
//     NewSegment 0x10003090.
//   IQualityControl vtable @0x1000817C: QI/AddRef/Release
//     (0x10001920/0x10001900/0x10002740), SetSink 0x100011E0, Notify 0x10003050.
// -----------------------------------------------------------------------------
class CBasePin : public CUnknown, public IPin, public IQualityControl {
    friend struct MMDxShowStateAccess;

public:
    // --- primary (INonDelegatingUnknown) vtable, continued ---
    virtual LONG GetMediaTypeVersion(); // +0x10 0x10003030
    // Active — was mislabeled CheckConnect (vtable +0x14).  CBaseFilter::Pause
    // dispatches this slot with NO stack argument (ret 0): allocator commit +
    // worker-thread start.  strmbase: CBasePin::Active(void).
    virtual HRESULT Active(); // +0x14 0x10002370
    // Inactive — was mislabeled BreakConnect (vtable +0x18).  Dispatched by
    // CBaseFilter::Stop (ret 0): allocator decommit + worker park.
    virtual HRESULT Inactive(); // +0x18 0x100024B0
    // Run — was mislabeled CompleteConnect (vtable +0x1C).  CBaseFilter::Run
    // forwards tStart here; the 5-byte default body 0x10003040 ignores it
    // (`xor eax,eax; ret 8` — one 64-bit argument, matching REFERENCE_TIME).
    virtual HRESULT Run(REFERENCE_TIME tStart); // +0x1C 0x10003040
    virtual HRESULT
    CheckMediaType(const AM_MEDIA_TYPE* pmt) = 0; // +0x20 (derived: 0x100021E0/0x10001430)
    virtual HRESULT SetMediaType(const AM_MEDIA_TYPE* pmt); // +0x24 0x10002E90 (was Pin_v24)
    // CheckConnect — was Pin_v28 (vtable +0x28, 0x100030F0): validate the peer
    // (QueryPinInfo + same-filter check via the +0x1C slot — an original bug
    // kept bit-faithful) and QI it for IMemInputPin into +0x9C.
    virtual HRESULT CheckConnect(IPin* pPeer); // +0x28 0x100030F0
    virtual HRESULT BreakConnect();            // +0x2C 0x10003130 (was Pin_v2C)
    virtual HRESULT CompleteConnect(
        IPin* pReceivePin); // +0x30 0x100030D0 (was Pin_v30; delegates to the pin's +0x38 slot)
    virtual HRESULT GetMediaType(int iPosition, AM_MEDIA_TYPE* pmt); // +0x34 0x100022B0
    virtual HRESULT DecideAllocator(IMemInputPin* pPin,
                                    IMemAllocator** ppAlloc); // +0x38 0x100031A0 (was Pin_v38)
    virtual HRESULT DecideBufferSize(
        IMemAllocator* pAlloc,
        ALLOCATOR_PROPERTIES*
            pProps); // +0x3C 0x10001310 (was Pin_v3C; pure in CBaseOutputPin, overridden by CPushPinDIBSq)
    virtual HRESULT GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                      REFERENCE_TIME* pEndTime,
                                      DWORD dwFlags); // +0x40 0x100032A0 (was Pin_v40)
    virtual HRESULT Deliver(IMediaSample* pSample);   // +0x44 0x100032E0 (was Pin_v44)
    virtual HRESULT InitAllocator(
        IMemAllocator**
            ppAlloc); // +0x48 0x10003190 (was Pin_v48; CoCreateInstance pull of CLSID_MemoryAllocator)
    virtual HRESULT DeliverEndOfStream(); // +0x4C 0x10003310 (was Pin_v4C)
    virtual HRESULT DeliverBeginFlush();  // +0x50 0x10003380 (was Pin_v50)
    virtual HRESULT DeliverEndFlush();    // +0x54 0x100033A0 (was Pin_v54)
    virtual HRESULT DeliverNewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop,
                                      double dRate); // +0x58 0x100033C0 (was Pin_v58)

    // --- IPin overrides (sub-vtable at object +0x0C, canonical order) ---
    // Delegating IUnknown for the IPin/IQualityControl branches
    // (binary: per-branch small forwards 0x10001180/0x10001710/0x100011A0).
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    STDMETHOD(Connect)(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt);          // 0x100047B0
    STDMETHOD(ReceiveConnection)(IPin* pConnector, const AM_MEDIA_TYPE* pmt); // 0x10004160
    STDMETHOD(Disconnect)();                                                  // 0x100042D0
    STDMETHOD(ConnectedTo)(IPin** ppPin);                                     // 0x10002F20
    STDMETHOD(ConnectionMediaType)(AM_MEDIA_TYPE* pmt);                       // 0x10004360
    STDMETHOD(QueryPinInfo)(PIN_INFO* pInfo);                                 // 0x10002F60
    STDMETHOD(QueryDirection)(PIN_DIRECTION* pPinDir);                        // 0x10002FE0
    STDMETHOD(QueryId)(LPWSTR* Id);                                           // 0x10001D40
    STDMETHOD(QueryAccept)(const AM_MEDIA_TYPE* pmt);                         // 0x10003000
    STDMETHOD(EnumMediaTypes)(IEnumMediaTypes** ppEnum);                      // 0x100048D0
    STDMETHOD(QueryInternalConnections)(IPin** apPin, ULONG* nPin);           // 0x100011C0 (stub)
    STDMETHOD(EndOfStream)(); // 0x10003370 (stub E_NOTIMPL)
    STDMETHOD(BeginFlush)();  // 0x10003370
    STDMETHOD(EndFlush)();    // 0x10003370
    STDMETHOD(NewSegment)(REFERENCE_TIME tStart, REFERENCE_TIME tEnd,
                          double dRate); // 0x10003090

    // --- IQualityControl overrides (sub-vtable at object +0x10) ---
    // (its IUnknown slots are compiler adjustor thunks to the trio above,
    //  exactly like the binary's 0x10001920/0x10001900/0x10002740)
    STDMETHOD(SetSink)(IQualityControl* piqc);        // 0x100011E0 (stub)
    STDMETHOD(Notify)(IBaseFilter* pSelf, Quality q); // 0x10003050

    // --- NonDelegating overrides (primary vtable) ---
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid,
                                                          void** ppv) override; // 0x10002DC0
    ULONG STDMETHODCALLTYPE NonDelegatingAddRef() override;  // 0x10002E50: owner filter AddRef
    ULONG STDMETHODCALLTYPE NonDelegatingRelease() override; // 0x10002E70: owner filter Release

protected:
    CBasePin(const char* pName, CBaseFilter* pFilter, HRESULT* phr);
    // Data members (offsets relative to the CBasePin subobject; subobject
    // ends at pin+0xF0 => size 0xA8; gaps are explicit padding; every slot
    // below is 4 bytes so the total is pinned by sizeof(CPushPinDIBSq)==0x5B8):
    //   x64 (VC10 ctor 0x180004720): +0x28 name, +0x30 connected, +0x38 dir,
    //   +0x40 pLock, +0x48..0x4A flags, +0x50 owner, +0x58 qsink, +0x60 type
    //   version, +0x68 m_mt (88-byte AM_MEDIA_TYPE), +0xC0/+0xC8/+0xD0 the
    //   NewSegment cache, +0xD8 allocator, +0xE0 input pin, +0xE8 filter
    //   back-pointer => subobject size 0xF0.
    WCHAR* m_pName;      // +0x14 strmbase m_pName: the wide pin name (was m_pinPad14)
    IPin* m_Connected;   // +0x18
    PIN_DIRECTION m_dir; // +0x1C (this slot was mislabeled "m_pName" before; PINDIR_OUTPUT here)
    CRITICAL_SECTION* m_pLock;
    unsigned char m_bRun;
    unsigned char m_bConnectWhileActive;
    unsigned char m_bEnumPeerTypesFirst;
    CBaseFilter*
        m_pFilterOwner; // +0x28 owner filter: NonDelegating AddRef/Release and QueryPinInfo go through it (slot was mislabeled "m_pQSinkOwner" before)
    void*
        m_pQSink; // +0x2C IQualityControl::Notify stores its pSender here (old-baseclasses shape; never read back — was m_pinPad2C)
    LONG m_TypeVersion; // +0x30
    AM_MEDIA_TYPE m_mediaType;
    REFERENCE_TIME m_tStart;
    REFERENCE_TIME m_tStop;
    double m_dRate;
    IMemAllocator* m_pAllocator; // +0x98 / x64 +0xD8
    IMemInputPin* m_pInputPin;   // +0x9C / x64 +0xE0
    CBaseFilter* m_pFilter;      // +0xA0 / x64 +0xE8
#if !defined(_M_X64)
    void* m_pinPadA4; // +0xA4 (closes CBasePin at 0xA8; the
                      //  x64 subobject already ends at 0xF0)
#endif
};

// CBaseOutputPin : CBasePin — adds no vtable slots (its RTTI TD exists at
// 0x1000B050 but its vtables equal CBasePin's).
class CBaseOutputPin : public CBasePin {
protected:
    CBaseOutputPin(const char* pName, CBaseFilter* pFilter, HRESULT* phr)
        : CBasePin(pName, pFilter, phr) {}
};

// -----------------------------------------------------------------------------
// CSourceStream : CAMThread, CBaseOutputPin            (Phase B)
//   CAMThread at +0x00 (0x48 bytes), CBaseOutputPin at +0x48.
//   x64: CAMThread 0x78 bytes, CBaseOutputPin at +0x78 => 0x168 total.
// -----------------------------------------------------------------------------
class CSourceStream : public CAMThread, public CBaseOutputPin {
public:
    CSourceStream(const char* pName, HRESULT* phr, CSource* pParent, LPCWSTR pName2);
    // root vtable (deleting dtor lands at +0x04 automatically: 0x100021C0)
    virtual DWORD ThreadProc() override; // +0x00 shared 0x10002570
    virtual ~CSourceStream();            // root +0x04; core 0x10001DC0
    virtual HRESULT CheckMediaType(const AM_MEDIA_TYPE* pmt) override; // primary +0x20, 0x100021E0
    virtual HRESULT GetMediaType1(AM_MEDIA_TYPE* pmt);                 // root +0x1C 0x10003370 stub
    // (FillBuffer stays pure here: root +0x08 = _purecall 0x1000668A in the
    //  binary's CSourceStream vtable; only CPushPinDIBSq overrides it.)
};

// -----------------------------------------------------------------------------
// CPushPinDIBSq : CSourceStream                        (Phase B)
//   overrides: root+0x04 deleting dtor 0x10001A50, root+0x08 FillBuffer
//   0x100014E0, root+0x1C GetMediaType1 0x100011F0, primary+0x20
//   CheckMediaType 0x10001430, IPin sub-vtable overrides as listed for
//   0x10008194.  (ThreadProc at root +0x00 stays the shared 0x10002570.)
// -----------------------------------------------------------------------------
class CPushPinDIBSq : public CSourceStream {
public:
    CPushPinDIBSq(HRESULT* phr, CSource* pParent); // 0x10001960 (binary param order)
    virtual ~CPushPinDIBSq(); // primary +0x0C 0x10001950 / root +0x04 0x10001A50
    virtual HRESULT FillBuffer(IMediaSample* pSample) override;        // 0x100014E0
    virtual HRESULT GetMediaType1(AM_MEDIA_TYPE* pmt) override;        // 0x100011F0
    virtual HRESULT CheckMediaType(const AM_MEDIA_TYPE* pmt) override; // 0x10001430
    // DecideBufferSize — was Pin_v3C (primary vtable +0x3C, 0x10001310);
    // CSourceStream leaves the slot pure, exactly like strmbase's
    // CBaseOutputPin::DecideBufferSize PURE.
    virtual HRESULT
    DecideBufferSize(IMemAllocator* pAlloc,
                     ALLOCATOR_PROPERTIES* pProps) override; // primary +0x3C 0x10001310

    // ---- data members (absolute pin-object offsets; 0x48 CAMThread head,
    //      0xA8 CBasePin subobject ends at 0xF0) ----
    //      x64 (VC10 rebuild, operator new(0x660) at 0x1800019D2): the same
    //      members at +0x168 pad, +0x178 frame count, +0x180/+0x1A8 critsecs
    //      (40B each), +0x1D0 display block, +0x638 push cfg, +0x640 extra,
    //      +0x648 avg-per-frame, +0x650..0x653 flags, +0x658 frame bits.
#if defined(_M_X64)
    void* m_padF0[2]; // x64 +0x168..+0x178 (ctor zeroes two qwords)
#else
    void* m_padF0[4]; // +0xF0..+0x100 (ctor zeroes)
#endif
    int m_frameCount;            // +0x100 (SetTime sample counter) / x64 +0x178
    CRITICAL_SECTION m_csRender; // +0x104 (24B; FillBuffer hold) / x64 +0x180 (40B)
#if !defined(_M_X64)
    void* m_pad11C; // +0x11C (pad to 0x120; the x64 critsec
                    //  is 40B and meets m_csDisplay directly)
#endif
    CRITICAL_SECTION m_csDisplay; // +0x120 (24B) / x64 +0x1A8 (40B)
    // Both original display probes clear 0x464 bytes. Native alignment places
    // the x64 push-config pointer at +0x638 (ctor 0x180001913, setter
    // 0x180001695, dtor 0x180001070); x86 retains +0x5A0.
    unsigned char m_displayBlock[0x464];
#if !defined(_M_X64)
    void* m_pad59C;
#endif
    void* m_pPushCfg;             // Heap block: 0x2C allocated, 0x28-byte bitmap header copied.
    int m_nPushExtra;             // +0x5A4 (SetBitmapInfo's n, pin-direct) / x64 +0x640
    LONGLONG m_rtAvgPerFrame;     // +0x5A8 (10000000/(i64)(u64)fps, pin-direct) / x64 +0x648
    unsigned char m_bBitmapSet;   // +0x5B0 / x64 +0x650
    unsigned char m_bFrameAck;    // +0x5B1 / x64 +0x651
    unsigned char m_bFrameReady;  // +0x5B2 / x64 +0x652
    unsigned char m_bStreamEnded; // +0x5B3 / x64 +0x653
    void* m_pFrameBits;           // +0x5B4  => sizeof == 0x5B8 / x64 +0x658 => 0x660
};
#if defined(_M_X64)
static_assert(sizeof(CPushPinDIBSq) == 0x660, "x64 pin layout must match operator new(0x660)");
static_assert(offsetof(CPushPinDIBSq, m_frameCount) == 0x178, "x64 m_frameCount");
static_assert(offsetof(CPushPinDIBSq, m_csRender) == 0x180, "x64 m_csRender");
static_assert(offsetof(CPushPinDIBSq, m_csDisplay) == 0x1A8, "x64 m_csDisplay");
static_assert(offsetof(CPushPinDIBSq, m_displayBlock) == 0x1D0, "x64 m_displayBlock");
static_assert(offsetof(CPushPinDIBSq, m_pPushCfg) == 0x638, "x64 m_pPushCfg");
static_assert(offsetof(CPushPinDIBSq, m_nPushExtra) == 0x640, "x64 m_nPushExtra");
static_assert(offsetof(CPushPinDIBSq, m_rtAvgPerFrame) == 0x648, "x64 m_rtAvgPerFrame");
static_assert(offsetof(CPushPinDIBSq, m_bStreamEnded) == 0x653, "x64 m_bStreamEnded");
static_assert(offsetof(CPushPinDIBSq, m_pFrameBits) == 0x658, "x64 m_pFrameBits");
static_assert(offsetof(CPushPinDIBSq, m_padF0) == 0x168,
              "x64 CBasePin subobject must end at 0x168");
#elif defined(_M_IX86)
static_assert(sizeof(CPushPinDIBSq) == 0x5B8,
              "pin layout must match operator new size in the binary");
static_assert(offsetof(CPushPinDIBSq, m_padF0) == 0xF0, "CBasePin subobject must end at 0xF0");
static_assert(offsetof(CPushPinDIBSq, m_frameCount) == 0x100, "m_frameCount");
static_assert(offsetof(CPushPinDIBSq, m_csRender) == 0x104, "m_csRender");
static_assert(offsetof(CPushPinDIBSq, m_csDisplay) == 0x120, "m_csDisplay");
static_assert(offsetof(CPushPinDIBSq, m_displayBlock) == 0x138, "m_displayBlock");
static_assert(offsetof(CPushPinDIBSq, m_pPushCfg) == 0x5A0, "m_pPushCfg");
static_assert(offsetof(CPushPinDIBSq, m_nPushExtra) == 0x5A4, "m_nPushExtra");
static_assert(offsetof(CPushPinDIBSq, m_rtAvgPerFrame) == 0x5A8, "m_rtAvgPerFrame");
static_assert(offsetof(CPushPinDIBSq, m_bStreamEnded) == 0x5B3, "m_bStreamEnded");
static_assert(offsetof(CPushPinDIBSq, m_pFrameBits) == 0x5B4, "m_pFrameBits");
#endif

// =============================================================================
// CEnumPins : IEnumPins — Phase A, fully implemented (enumerators.cpp)
//   vtable @0x1000854C (8 slots):
//     +0x00 QueryInterface  0x10002B40   +0x04 AddRef  0x10002BA0
//     +0x08 Release         0x10002BC0   +0x0C Next    0x10003B20
//     +0x10 Skip            0x10003C20   +0x14 Reset   0x10002BF0
//     +0x18 Clone           0x100044F0   +0x1C ~dtor   0x10003B00
//   object (0x30 bytes):
//     +0x04 m_Position, +0x08 m_PinCount, +0x0C m_pFilter, +0x10 m_PinVersion,
//     +0x14 m_cRef, +0x18 pin keep-alive/dedup list (24 bytes)
//   x64 (ctor 0x1800042F0): 0x48 bytes — +0x08/+0x0C/+0x10/+0x18/+0x1C as
//   above, +0x20 list (0x28 bytes: two pointers, three LONGs, pad, pointer).
//   ctor 0x10004420(this, CBaseFilter*, CEnumPins* cloneSrc)
// =============================================================================
class CEnumPins : public IEnumPins {
public:
    CEnumPins(CBaseFilter* pFilter, CEnumPins* pEnum);
    virtual ~CEnumPins();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(Next)(ULONG cPins, IPin** ppPins, ULONG* pcFetched);
    STDMETHOD(Skip)(ULONG cPins);
    STDMETHOD(Reset)();
    STDMETHOD(Clone)(IEnumPins** ppEnum);

private:
    // linked-list of pins already handed out (binary 0x10005F30..0x100060C0)
    struct PinNode {
        PinNode* pPrev;
        PinNode* pNext;
        CBasePin* pPin;
    };
    struct PinList {
        PinNode* pHead; // +0x00
        PinNode* pTail; // +0x04
        LONG nCount;    // +0x08
        LONG nCapacity; // +0x0C (10)
        LONG nFree;     // +0x10 (free-node count)
        PinNode* pFree; // +0x14 (free-node stack, linked via pNext)
    };

    // list helpers (definitions in enumerators.cpp, binary addresses there)
    static void ListInit(PinList* pList);                           // 0x10005F30
    static PinNode* ListPushBack(PinList* pList, CBasePin* pPin);   // 0x10006010
    static void ListFreeActive(PinList* pList);                     // 0x10005F50
    static void ListDestroy(PinList* pList);                        // 0x100060C0
    static void ListCopy(PinList* pList, const PinList* pSrc);      // 0x10006070
    static PinNode* ListFind(const PinList* pList, CBasePin* pPin); // 0x10005FD0
    void RefreshPinState();                                         // 0x10002C30

    LONG m_Position;        // +0x04
    LONG m_PinCount;        // +0x08  (cached GetPinCount())
    CBaseFilter* m_pFilter; // +0x0C
    LONG m_PinVersion;      // +0x10  (cached GetPinVersion())
    LONG m_cRef;            // +0x14
    PinList m_Pins;         // +0x18 / x64 +0x20
};
#if defined(_M_X64)
static_assert(sizeof(CEnumPins) == 0x48,
              "x64 CEnumPins layout must match the 0x180004B70 operator new(0x48)");
#elif defined(_M_IX86)
static_assert(sizeof(CEnumPins) == 0x30,
              "CEnumPins layout must match 0x100044F0's operator new(0x30)");
#endif

// =============================================================================
// CEnumMediaTypes : IEnumMediaTypes — Phase A, fully implemented (enumerators.cpp)
//   vtable @0x10008570 (8 slots):
//     +0x00 QueryInterface  0x10002C80   +0x04 AddRef  0x10002CE0
//     +0x08 Release         0x10002D00   +0x0C Next    0x10003C90
//     +0x10 Skip            0x10003E10   +0x14 Reset   0x10002D30
//     +0x18 Clone           0x10004600   +0x1C ~dtor   0x10003C70
//   object (0x14 bytes): +0x04 m_Position, +0x08 m_pPin, +0x0C m_Version,
//                        +0x10 m_cRef;  x64 (ctor inlined at 0x180004A70):
//                        0x20 bytes, same members at +0x08/+0x10/+0x18/+0x1C
//   ctor 0x100045A0(this, CBasePin*, CEnumMediaTypes* cloneSrc)
//   NOTE: QI accepts the ActiveMovie-1.0 IID_IEnumMediaTypes
//         {89C31040-846B-11CE-97D3-00AA0055595A} (see GUID above).
// =============================================================================
class CEnumMediaTypes : public IEnumMediaTypes {
public:
    CEnumMediaTypes(CBasePin* pPin, CEnumMediaTypes* pEnum);
    virtual ~CEnumMediaTypes();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(Next)(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pcFetched);
    STDMETHOD(Skip)(ULONG cMediaTypes);
    STDMETHOD(Reset)();
    STDMETHOD(Clone)(IEnumMediaTypes** ppEnum);

private:
    LONG m_Position;  // +0x04 / x64 +0x08
    CBasePin* m_pPin; // +0x08 / x64 +0x10
    LONG m_Version;   // +0x0C  (cached GetMediaTypeVersion()) / x64 +0x18
    LONG m_cRef;      // +0x10 / x64 +0x1C
};
#if defined(_M_X64)
static_assert(sizeof(CEnumMediaTypes) == 0x20,
              "x64 CEnumMediaTypes layout must match the 0x180004A70 operator new(0x20)");
#elif defined(_M_IX86)
static_assert(sizeof(CEnumMediaTypes) == 0x14,
              "CEnumMediaTypes layout must match 0x10004600's operator new(0x14)");
#endif

// =============================================================================
// CClassFactory : IClassFactory — Phase A, fully implemented (dll_main.cpp)
//   vtable @0x10008798: QI 0x100059A0, AddRef 0x100058C0, Release 0x10005B00,
//   CreateInstance 0x10005A00, LockServer 0x100058D0.
//   object (0x0C): +0x04 m_pTemplate, +0x08 m_cRef (ctor sets 0;
//   DllGetClassObject AddRefs once on the way out).  x64 (DllGetClassObject
//   0x180005EB0): 0x18 bytes, +0x08 m_pTemplate, +0x10 m_cRef.
// =============================================================================
class CClassFactory : public IClassFactory {
public:
    CClassFactory(const CFactoryTemplate* pTemplate);
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppv);
    STDMETHOD(LockServer)(BOOL fLock);

private:
    const CFactoryTemplate* m_pTemplate; // +0x04 / x64 +0x08
    LONG m_cRef;                         // +0x08 / x64 +0x10
};
#if defined(_M_X64)
static_assert(sizeof(CClassFactory) == 0x18,
              "x64 class factory must match the operator new(0x18) in DllGetClassObject");
#elif defined(_M_IX86)
static_assert(sizeof(CClassFactory) == 0x0C,
              "class factory must match the operator new(0x0C) in DllGetClassObject");
#endif

// =============================================================================
// Exports (defined in dll_main.cpp; the build wires the .def)
// =============================================================================
extern "C" {
HRESULT WINAPI DllCanUnloadNow(void);
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv);
HRESULT WINAPI DllRegisterServer(void);
HRESULT WINAPI DllUnregisterServer(void);
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);
}

#endif // _MMDXSHOW_H_
