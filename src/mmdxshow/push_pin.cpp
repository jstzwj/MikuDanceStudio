// =============================================================================
// MMDxShow.dll — Phase B: CPushPinDIBSq (the DIB-sequence push output pin)
// =============================================================================
// Source binary : MikuMikuDanceE_v932/Data/MMDxShow.dll (image base 0x10000000)
// Ground truth  : Hex-Rays decompilation + disassembly of the original.
// x64 flavour   : MikuMikuDanceE_v932x64/Data/MMDxShow.dll (VC10 rebuild,
//                 image base 0x180000000) — same source, so every body below
//                 is shared; only the member offsets shifted (see the map).
//
// CLASS SHAPE (as the .rdata vtables/RTTI say, corrected against the binary):
//
//   object size 0x5B8 (the filter ctor news 0x5B8 bytes for the pin,
//   see 0x10001AF7 operator new(0x5B8); the x64 rebuild news 0x660 at
//   0x1800019D2).
//
//   +0x000  CAMThread root           vtable @0x10008244  (8 slots)
//   +0x048  CBasePin/CBaseOutputPin  vtable @0x100081E4  (23 slots)
//   +0x054  IPin                     vtable @0x10008194  (18 slots)
//   +0x058  IQualityControl          vtable @0x1000817C  (5 slots)
//   (x64: the same four at +0x000 / +0x078 / +0x090 / +0x098)
//
//   Root vtable @0x10008244 (CSourceStream's own is @0x100084A4):
//     +0x00 0x10002570  ThreadProc — the CAMThread request/dispatch state
//                        machine (SHARED with CSourceStream; NOT overridden
//                        by CPushPinDIBSq; CSourceStream::ThreadProc owns it)
//     +0x04 0x10001A50  scalar deleting destructor (calls 0x100010D0)
//     +0x08 0x100014E0  FillBuffer                       [CPushPinDIBSq]
//     +0x0C 0x100011D0  stub "return 0" (S_OK — NOT E_NOINTERFACE)
//     +0x10 0x100011D0  stub "return 0"
//     +0x14 0x100011D0  stub "return 0"
//     +0x18 0x10002630  DoBufferProcessingLoop           [CSourceStream]
//     +0x1C 0x100011F0  GetMediaType(CMediaType*)        [CPushPinDIBSq]
//
//   Pin-primary vtable @0x100081E4 — CPushPinDIBSq overrides exactly three
//   slots relative to CSourceStream (@0x10008440):
//     +0x0C 0x10001950  scalar deleting dtor thunk: sub_10001A50(this-0x48)
//     +0x20 0x10001430  CheckMediaType                  [CPushPinDIBSq]
//     +0x3C 0x10001310  DecideBufferSize (was Pin_v3C)  [CPushPinDIBSq;
//                        CSourceStream has _purecall in this slot]
//   The IPin (@0x10008194) and IQualityControl (@0x1000817C) sub-vtables
//   contain the same function targets as CSourceStream's — no pin-specific
//   overrides — so they are owned by the base-class Phase-B file.
//
// PIN STATE MAP (binary offsets within the pin object; writers/Readers):
//   +0x078  CBasePin::m_pFilter            (set by CBasePin ctor 0x100046B0)
//   +0x07C  CBasePin::m_mt (embedded CMediaType, InitMediaType 0x10004B80)
//   +0x0A0  CBasePin::m_pFilter (second copy, used by 0x10001310)
//   +0x0C0  m_mt.pbFormat  (= pin+0x7C + 0x44)  — FillBuffer/0x10001310 load
//          the DWORD at pbFormat+0x44, i.e. the connected VIDEOINFOHEADER's
//          bmiHeader.biSizeImage.
//   +0x0E8  CSourceStream's m_pParent filter (ctor 0x10002110: this[58]=a4)
//   +0x0F0..+0x0FC  zeroed by the pin ctor (0x10001960)      [m_padF0]
//   +0x100  int  m_frameCount     (FillBuffer start-time numerator)
//   +0x104  CRITICAL_SECTION m_csRender (FillBuffer; 0x18 bytes)
//   +0x120  CRITICAL_SECTION m_csDisplay + 0x464-byte display VIDEOINFO-
//          HEADER/color-table block (ctor -> 0x10005220 -> 0x10005140)
//   +0x5A0  BITMAPINFOHEADER* m_pBMIHPush (new[0x2C], IPushSource +0x0C;
//          0x28 bytes of BIH copied, 4 trailing bytes never written)
//   +0x5A4  int m_nPushExtra            (IPushSource +0x0C second arg)
//   +0x5A8  LONGLONG m_rtAvgPerFrame    (= 10000000 / fps)
//   +0x5B0  BYTE m_bBitmapSet           (1 once SetBitmapInfo succeeded)
//   +0x5B1  BYTE m_bFrameAck            (GetStreamingState reads it;
//                                        FillBuffer sets it to !stopped)
//   +0x5B2  BYTE m_bFrameReady          (EXE push sets 1; FillBuffer clears)
//   +0x5B3  BYTE m_bStreamEnded         (BeginStreaming / dtor set 1)
//   +0x5B4  const void* m_pFrameBits    (EXE push frame DIB bits)
//
//   x64 rebuild: pad +0x168, frame count +0x178, csRender +0x180 (40B),
//   csDisplay +0x1A8, display block +0x1D0 (0x464 bytes),
//   push cfg +0x638, extra +0x640,
//   avg-per-frame +0x648, flags +0x650..+0x653, frame bits +0x658.
//
//   The IPushSource methods that write the push-cfg triple (0x10001730 /
//   0x100017D0 / 0x10001800 / 0x10001840 / 0x10001880) belong to the filter
//   Phase-B file (push_source.cpp); the handshake they implement is
//   documented in FillBuffer below.
//
// PUSH CONFIGURATION (header members, offsets verified against the binary;
// x64 offsets after the slash):
//   m_pPushCfg    +0x5A0/+0x638  void* — operator new(0x2C) block from
//                          0x10001730, 0x28 bytes of EXE-pushed
//                          BITMAPINFOHEADER copied into it (4 trailing
//                          bytes never written)
//   m_nPushExtra  +0x5A4/+0x640  int — SetBitmapInfo's n
//   m_rtAvgPerFrame +0x5A8/+0x648 LONGLONG — 10000000 / fps
// =============================================================================

#include "mmdxshow.hpp"

#include <amvideo.h> // VIDEOINFOHEADER
#include <string.h>  // memcpy / memset

// =============================================================================
// .rdata GUID constants used by the pin (values read straight from the
// original image; the DLL keeps private copies instead of uuid.lib refs)
// =============================================================================

namespace {

// 0x10008720 — bytes "vids" first: MEDIATYPE_Video
// {73646976-0000-0010-8000-00AA00389B71}
const GUID kMediaVideo = {
    0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// 0x10008650 — FORMAT_VideoInfo {05589F80-C356-11CE-BF01-00AA0055595A}
const GUID kFormatVideoInfo = {
    0x05589f80, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}};

// 0x100086A0 — the DLL's private 32-bit subtype; the EXE-side contract doc
// calls it "MEDIASUBTYPE_ARGB32-like".
// {773C9AC0-3274-11D0-B724-00AA006C1A01}
const GUID kSubtypeARGB32 = {
    0x773c9ac0, 0x3274, 0x11d0, {0xb7, 0x24, 0x00, 0xaa, 0x00, 0x6c, 0x1a, 0x01}};

} // anonymous namespace

// =============================================================================
// +0x5A0 triple accessors — see the HEADER NOTE in the file banner.  These
// stay bit-faithful to the binary offsets regardless of the m_pushCfg
// declaration form; replace with direct member access once the header
// declares the pointer/int/LONGLONG members.
// =============================================================================

namespace {

// pin+0x5A0 — the header's m_pPushCfg member, typed as the BITMAPINFOHEADER
// block pointer that 0x10001730 stores there
inline BITMAPINFOHEADER*& PushBIH(CPushPinDIBSq* pPin) {
    return reinterpret_cast<BITMAPINFOHEADER*&>(pPin->m_pPushCfg);
}

} // anonymous namespace

// =============================================================================
// CMediaType member bodies called directly by the pin's media-type code.
// These are file-local because the header only exposes Init/Free/Copy; if
// the base-class Phase-B file also ports them, its versions win — the
// bodies below are byte-faithful for standalone use.
// (AM_MEDIA_TYPE field offsets used: majortype +0, subtype +16,
//  bFixedSizeSamples +32, lSampleSize +40, formattype +44, pUnk +60,
//  cbFormat +64, pbFormat +68.)
// =============================================================================

namespace {

// VA 0x10004AB0 — copy a 16-byte GUID into pmt->majortype (SetType)
void MtSetType_10004AB0(AM_MEDIA_TYPE* pmt, const GUID* pGuid) {
    memcpy(&pmt->majortype, pGuid, sizeof(GUID));
}

// VA 0x10004B00 — copy a 16-byte GUID into pmt->formattype (SetFormatType)
void MtSetFormatType_10004B00(AM_MEDIA_TYPE* pmt, const GUID* pGuid) {
    memcpy(&pmt->formattype, pGuid, sizeof(GUID));
}

// VA 0x10004AD0 — copy a 16-byte GUID into pmt->subtype (SetSubtype)
void MtSetSubtype_10004AD0(AM_MEDIA_TYPE* pmt, const GUID* pGuid) {
    memcpy(&pmt->subtype, pGuid, sizeof(GUID));
}

// VA 0x10004B20 — CMediaType::AllocFormatBuffer(cb).  Returns the (possibly
// reallocated) pbFormat, or NULL when the allocation fails and cb is larger
// than the existing buffer.  CoTaskMem-based, exactly as the original.
BYTE* MtAllocFormatBuffer_10004B20(AM_MEDIA_TYPE* pmt, ULONG cb) {
    if (pmt->cbFormat == cb)
        return pmt->pbFormat;

    BYTE* pNew = (BYTE*)CoTaskMemAlloc(cb);
    if (pNew) {
        if (pmt->cbFormat)
            CoTaskMemFree(pmt->pbFormat);
        pmt->pbFormat = pNew;
        pmt->cbFormat = cb;
        return pNew;
    }
    if (cb > pmt->cbFormat)
        return NULL;
    return pmt->pbFormat; // keep the larger existing buffer
}

// VA 0x10004F10 — CMediaType::SetFormat(pFormat, cb): returns 1 on success,
// 0 when the format buffer could not be provided.
int MtSetFormat_10004F10(AM_MEDIA_TYPE* pmt, const void* pFormat, size_t cb) {
    BYTE* pDst = MtAllocFormatBuffer_10004B20(pmt, (ULONG)cb);
    if (pDst == NULL)
        return 0;
    memcpy(pDst, pFormat, cb);
    return 1;
}

// VA 0x10004AF0 — CMediaType::SetVariableSize: only clears
// bFixedSizeSamples (the original does NOT touch lSampleSize here).
void MtSetVariableSize_10004AF0(AM_MEDIA_TYPE* pmt) {
    pmt->bFixedSizeSamples = 0;
}

// VA 0x10004EF0 — CMediaType::SetSampleSize(lSize); when lSize == 0 the
// original falls through to SetVariableSize (0x10004AF0).
int MtSetSampleSize_10004EF0(AM_MEDIA_TYPE* pmt, LONG lSize) {
    if (lSize == 0) {
        MtSetVariableSize_10004AF0(pmt);
        return 0;
    }
    pmt->bFixedSizeSamples = 1;
    pmt->lSampleSize = (ULONG)lSize;
    return lSize;
}

// VA 0x10004DD0 — CMediaType::operator==(pmt2) reduced to "equal or not"
// (the original builds a memcmp-style sign result and the caller only
// tests nonzero).  Field order: majortype, subtype, formattype, cbFormat,
// pbFormat bytes.  GUID equality via the DLL's own 16-byte comparator
// (0x10001000 == MMDxShow_IsEqualGUID16, nonzero means equal).
BOOL MtIsEqual_10004DD0(const AM_MEDIA_TYPE* pmt1, const AM_MEDIA_TYPE* pmt2) {
    if (!MMDxShow_IsEqualGUID16(&pmt1->majortype, &pmt2->majortype))
        return FALSE;
    if (!MMDxShow_IsEqualGUID16(&pmt1->subtype, &pmt2->subtype))
        return FALSE;
    if (!MMDxShow_IsEqualGUID16(&pmt1->formattype, &pmt2->formattype))
        return FALSE;
    if (pmt1->cbFormat != pmt2->cbFormat)
        return FALSE;
    if (pmt1->cbFormat == 0)
        return TRUE;
    return memcmp(pmt1->pbFormat, pmt2->pbFormat, pmt1->cbFormat) == 0 ? TRUE : FALSE;
}

} // anonymous namespace

// =============================================================================
// DIB capture support cluster (pin-owned; only the pin ctor reaches it)
// =============================================================================

namespace {

// VA 0x10006130 — GetBitmapSize(BITMAPINFOHEADER*).  NOTE: the Phase-A
// ledger mislabels this address as __CxxFrameHandler3; the bytes are the
// classic WIDTHBYTES size computation (verified by disassembly).  Exact
// integer order of the original:
//   t  = biBitCount * biWidth  (unsigned, imul)
//   t += 0x1F; t >>= 3; t &= 0x1FFFFFFC; t *= biHeight
//   if (biHeight < 0) t = -t
LONG GetBitmapSize_10006130(const BITMAPINFOHEADER* pbih) {
    DWORD t = (DWORD)(pbih->biBitCount) * (DWORD)(pbih->biWidth); // imul
    t += 0x1F;
    t >>= 3;
    t &= 0x1FFFFFFC;
    DWORD d = t * (DWORD)pbih->biHeight; // imul
    if (pbih->biHeight < 0)
        d = (DWORD) - (LONG)d; // neg eax
    return (LONG)d;
}

// VA 0x100050E0 — display VIDEOINFOHEADER fixup after the GetDIBits probe.
// Offsets verified against disassembly (+0x3E = biBitCount, +0x44 =
// biSizeImage, +0x50 = biClrUsed, +0x54 = biClrImportant relative to the
// VIDEOINFOHEADER base).
int FixupDisplayVIH_100050E0(VIDEOINFOHEADER* pVIH) {
    ::SetRectEmpty(&pVIH->rcSource); // +0x00
    ::SetRectEmpty(&pVIH->rcTarget); // +0x10

    const WORD wBpp = pVIH->bmiHeader.biBitCount;    // +0x3E
    if (wBpp <= 8 && pVIH->bmiHeader.biClrUsed == 0) // +0x50
        pVIH->bmiHeader.biClrUsed = 1u << wBpp;
    if ((DWORD)pVIH->bmiHeader.biClrImportant > pVIH->bmiHeader.biClrUsed)
        pVIH->bmiHeader.biClrImportant = 1u << wBpp; // NOTE: sets 1<<bpp,
                                                     // not biClrUsed —
                                                     // faithful quirk.
    if (pVIH->bmiHeader.biSizeImage == 0)            // +0x44
        pVIH->bmiHeader.biSizeImage = (DWORD)GetBitmapSize_10006130(&pVIH->bmiHeader);
    return 0;
}

// VA 0x10005140 — probe the display device's format into the pin's +0x120
// block: zero 0x464 bytes, preset bmiHeader.biSize = 40, create a "DISPLAY"
// DC (CreateDCA), a 1x1 compatible bitmap, two NULL-bits GetDIBits calls to
// fill the BITMAPINFOHEADER (+color table), delete the GDI objects, then
// run the fixup above.  Returns E_FAIL only when CreateDCA fails.
//
// Struct at pin+0x120 in the binary:
//   +0x120 CRITICAL_SECTION (m_csDisplay)
//   +0x138 VIDEOINFOHEADER + color table, 0x464 bytes (m_displayBlock)
// x64 probe 0x180005360 clears the same 0x464 bytes at +0x1D0;
// native alignment places the following push-config pointer at +0x638.
HRESULT ProbeDisplayFormat_10005140(CPushPinDIBSq* pPin, LPCSTR pszDevice) {
    VIDEOINFOHEADER* pVIH = reinterpret_cast<VIDEOINFOHEADER*>(pPin->m_displayBlock);
    ::EnterCriticalSection(&pPin->m_csDisplay);
    memset(pPin->m_displayBlock, 0, sizeof(pPin->m_displayBlock));
    pVIH->bmiHeader.biSize = 40; // [pCS+72] = 40

    HDC hdc;
    if (pszDevice && ::lstrcmpiA(pszDevice, "DISPLAY") != 0)
        hdc = ::CreateDCA(NULL, pszDevice, NULL, NULL);
    else
        hdc = ::CreateDCA("DISPLAY", NULL, NULL, NULL);

    if (hdc) {
        HBITMAP hbm = ::CreateCompatibleBitmap(hdc, 1, 1);
        if (hbm) {
            // two identical format queries (bits = NULL), as the original
            ::GetDIBits(hdc, hbm, 0, 1, NULL, (BITMAPINFO*)&pVIH->bmiHeader, 0);
            ::GetDIBits(hdc, hbm, 0, 1, NULL, (BITMAPINFO*)&pVIH->bmiHeader, 0);
            ::DeleteObject(hbm);
        }
        ::DeleteDC(hdc);
        FixupDisplayVIH_100050E0(pVIH);
        ::LeaveCriticalSection(&pPin->m_csDisplay);
        return S_OK;
    }
    ::LeaveCriticalSection(&pPin->m_csDisplay);
    return E_FAIL;
}

// VA 0x10005220 — ctor helper: InitializeCriticalSection + display probe.
// The original ignores ProbeDisplayFormat's failure; so does the port.
// (m_csDisplay itself is initialized here to mirror the binary order:
//  InitializeCriticalSection(+0x104) first, then sub_10005220(+0x120) —
//  the x64 rebuild folds both into 0x180005500, critsecs +0x180/+0x1A8.)
void InitDisplayState_10005220(CPushPinDIBSq* pPin) {
    ::InitializeCriticalSection(&pPin->m_csDisplay);
    ProbeDisplayFormat_10005140(pPin, NULL);
}

} // anonymous namespace

// =============================================================================
// CPushPinDIBSq — construction / destruction
// =============================================================================

// VA 0x10001960 — CPushPinDIBSq::CPushPinDIBSq(pParent, phr)
//
// Binary flow:
//   sub_10002110(this, NULL, phr, pParent, L"Out")   // CSourceStream ctor
//   vtables <- CPushPinDIBSq (root / primary / IPin / IQualityControl)
//   pin+0xF0/0xF4/0xF8/0xFC/0x100 = 0
//   InitializeCriticalSection(pin+0x104)
//   sub_10005220(pin+0x120)          // InitDisplayState (DISPLAY probe)
//   pin+0x5A0 / +0x5A4 / +0x5A8(qword) = 0
//   pin+0x5B0..+0x5B3 = 0
// (x64 rebuild, ctor 0x180001870: pin+0x168..0x178 zeroes, critsecs at
//  +0x180/+0x1A8, then the +0x638/+0x640/+0x648 triple and +0x650 dword.)
//
// The name pushed as the 4th CSourceStream ctor argument is L"Out"
// (.rdata 0x1000831C, x64 .rdata 0x180008748).  The binary's first
// CSourceStream stack argument is a NULL ANSI name (CBaseObject name —
// see 0x10002110 -> 0x10004970 -> 0x100046B0 -> 0x10005000).
CPushPinDIBSq::CPushPinDIBSq(HRESULT* phr, CSource* pParent)
    : CSourceStream(NULL, phr, pParent, L"Out") {
    // the post-CBasePin pad: four dwords (pin+0xF0..+0x100) on x86, two
    // qwords (pin+0x168..+0x178) on x64 — the array width follows the arch
    for (size_t i = 0; i < sizeof(m_padF0) / sizeof(m_padF0[0]); ++i)
        m_padF0[i] = NULL;
    m_frameCount = 0;                         // pin+0x100 / x64 +0x178
    ::InitializeCriticalSection(&m_csRender); // pin+0x104 / x64 +0x180
    InitDisplayState_10005220(this);          // pin+0x120 block / x64 +0x1A8
    PushBIH(this) = NULL;                     // pin+0x5A0 / x64 +0x638
    m_nPushExtra = 0;                         // pin+0x5A4 / x64 +0x640
    m_rtAvgPerFrame = 0;                      // pin+0x5A8 (qword) / x64 +0x648
    m_bBitmapSet = 0;                         // pin+0x5B0 / x64 +0x650
    m_bFrameAck = 0;                          // pin+0x5B1 / x64 +0x651
    m_bFrameReady = 0;                        // pin+0x5B2 / x64 +0x652
    m_bStreamEnded = 0;                       // pin+0x5B3 / x64 +0x653
    // m_pFrameBits (pin+0x5B4 / x64 +0x658) is NOT zeroed by the binary ctor
    // either.
}

// VA 0x100010D0 — CPushPinDIBSq destructor body (entered from the scalar
// deleting dtors: pin-primary+0x0C 0x10001950 -> root 0x10001A50; x64 root
// deleting dtor 0x180001930 -> body 0x180001020).
//
// Binary flow:
//   vtables <- CPushPinDIBSq  (dtor re-stores, compiler artifact)
//   pin+0x5B3 = 1 ; pin+0x5B2 = 1     (wake/unblock FillBuffer's wait)
//   if (pin+0x5A0) { operator delete; pin+0x5A0 = 0; }
//   DeleteCriticalSection(pin+0x120)
//   DeleteCriticalSection(pin+0x104)
//   sub_10001DC0()                    // ~CSourceStream chain (implicit)
CPushPinDIBSq::~CPushPinDIBSq() {
    m_bStreamEnded = 1; // pin+0x5B3 / x64 +0x653 (one WORD
    m_bFrameReady = 1;  // pin+0x5B2 / x64 +0x652  store)
    if (PushBIH(this)) {
        ::operator delete((void*)PushBIH(this)); // scalar delete (??3)
        PushBIH(this) = NULL;
    }
    ::DeleteCriticalSection(&m_csDisplay); // pin+0x120 / x64 +0x1A8
    ::DeleteCriticalSection(&m_csRender);  // pin+0x104 / x64 +0x180
    // ~CSourceStream()/~CBaseOutputPin()/~CBasePin()/~CAMThread() implicit.
}

// =============================================================================
// ThreadProc — NOT overridden by CPushPinDIBSq (root vtable +0x00)
// =============================================================================
//
// The binary pin inherits the CAMThread request/dispatch state machine
// (0x10002570, shared with CSourceStream — the header models this as
// CAMThread::ThreadProc at root+0x00 with CSourceStream::ThreadProc as its
// definition).  Reference body, for the record:
//
//   DWORD CAMThread_StateMachine_10002570()          // BOOL return
//   {
//       while (GetRequest() != 0)                    // 0x10005E80:
//           Reply(E_UNEXPECTED);                     //   wait +0x04, read +0x0C
//       hr = vt[+0x0C](this);                        // init stub -> S_OK
//       if (hr >= 0) {
//           Reply(0);
//           do {
//               code = GetRequest();
//               switch (code) {
//               case 1: case 2: Reply(0);
//                   vt[+0x18](this);                 // DoBufferProcessingLoop
//                   break;                           //   (0x10002630)
//               case 3: case 4: Reply(0); break;
//               default:  Reply(E_NOTIMPL); break;
//               }
//           } while (code != 4);
//           return vt[+0x10](this) < 0;              // exit stub
//       }
//       vt[+0x10](this);
//       Reply(hr);
//       return 1;
//   }

// =============================================================================
// VA 0x100014E0 — CPushPinDIBSq::FillBuffer(IMediaSample*)  (root +0x08)
// =============================================================================
//
// EXE handshake (documented; the writers live in the filter Phase-B file,
// push_source.cpp, which touches the same pin offsets):
//   IPushSource +0x0C 0x10001730 "SetBitmapInfo": fails E_FAIL once
//       +0x5B3 is set; otherwise new[0x2C], memcpy 0x28 bytes of the EXE's
//       BITMAPINFOHEADER into that buffer, +0x5A0 = buffer pointer,
//       +0x5A4 = n, +0x5A8 = 10000000/fps, +0x5B0 = +0x5B1 = 1.
//   IPushSource +0x10 0x100017D0 "GetStreamingState": *pState = +0x5B1
//       (fails E_FAIL when streaming has ended).
//   IPushSource +0x14 0x10001800 per-frame push: fails E_FAIL when ended;
//       else +0x5B1 = 0, +0x5B4 = frame bits, +0x5B2 = 1 (ready).
//   IPushSource +0x18 0x10001840 "BeginStreaming"/end: +0x5B3 = 1,
//       +0x5B1 = +0x5B2 = 0.
//
// FillBuffer, exactly in the original's order:
//   pSample == NULL                      -> E_POINTER (0x80004003)
//   +0x5B3 (streaming ended)             -> S_FALSE (1)   [clean stop]
//   +0x5B0 (no bitmap configured)        -> E_FAIL  (0x80004005)
//   EnterCriticalSection(pin+0x104)
//   pDst  = pSample->GetPointer()        (vt+0x0C; HRESULT ignored)
//   cbSmp = pSample->GetSize()           (vt+0x10)
//   pVIH  = *(pin+0xC0)                  (= m_mt.pbFormat; loaded here but
//                                          only dereferenced after the wait)
//   wait:  while (!+0x5B2 && !+0x5B3) Sleep(1);
//   if (+0x5B3) -> leave, return S_FALSE
//   +0x5B2 = 0                            (consume the frame)
//   cbCopy = *(pVIH + 0x44)               == bmiHeader.biSizeImage of the
//                                          connected type; the port reads it
//                                          from the pushed BIH, which
//                                          CheckMediaType guarantees is
//                                          byte-identical)
//   if (cbCopy >= cbSmp) cbCopy = cbSmp;  (min, unsigned "cmp/jb" form)
//   memcpy(pDst, *(pin+0x5B4), cbCopy)
//   +0x5B1 = (+0x5B3 == 0)               (frame ack for GetStreamingState)
//   rtStart = (INT64)(int)*(int*)(pin+0x100) * *(INT64*)(pin+0x5A8)
//                                          (__allmul; the frame number is
//                                          sign-extended via cdq)
//   rtEnd   = rtStart + AvgTimePerFrame
//   pSample->SetTime(&rtStart, &rtEnd)    (vt+0x18)
//   ++*(int*)(pin+0x100)
//   pSample->SetSyncPoint(TRUE)           (vt+0x20)
//   LeaveCriticalSection; return S_OK
//
// NOTE for the EXE-side contract: the per-sample calls are GetPointer /
// GetSize / SetTime / SetSyncPoint — there is no GetTime/SetMediaTime/
// Discontinuity use in the binary (the contract guess in the Phase-A notes
// is slightly off; SetSyncPoint(TRUE) is the "continuity" marker).
HRESULT CPushPinDIBSq::FillBuffer(IMediaSample* pSample) {
    if (pSample == NULL)
        return E_POINTER; // 0x80004003

    if (m_bStreamEnded) // pin+0x5B3
        return 1;       // S_FALSE

    if (!m_bBitmapSet) // pin+0x5B0
        return E_FAIL; // 0x80004005

    ::EnterCriticalSection(&m_csRender); // pin+0x104

    BYTE* pDst = NULL;
    pSample->GetPointer(&pDst);         // vt+0x0C, result ignored
    LONG cbSample = pSample->GetSize(); // vt+0x10

    // pin+0xC0 (m_mt.pbFormat) is captured before the wait in the binary;
    // only its +0x44 dword (biSizeImage) is used, after the wait.
    MMDXTrace("FillBuffer enter ended=%d ready=%d\n", (int)m_bStreamEnded, (int)m_bFrameReady);
    while (!m_bFrameReady && !m_bStreamEnded)
        ::Sleep(1);
    MMDXTrace("FillBuffer wait done ended=%d ready=%d\n", (int)m_bStreamEnded, (int)m_bFrameReady);

    if (m_bStreamEnded) // pin+0x5B3
    {
        ::LeaveCriticalSection(&m_csRender);
        return 1; // S_FALSE
    }

    m_bFrameReady = 0; // pin+0x5B2

    LONG cbCopy = (LONG)PushBIH(this)->biSizeImage; // *(VIH+0x44)
    if ((ULONG)cbCopy >= (ULONG)cbSample)           // cmp/jb (unsigned)
        cbCopy = cbSample;

    memcpy(pDst, m_pFrameBits, (size_t)cbCopy); // src = pin+0x5B4

    m_bFrameAck = (m_bStreamEnded == 0); // pin+0x5B1

    // rtStart = frameNumber * AvgTimePerFrame (64-bit, count sign-extended)
    REFERENCE_TIME rtStart = (REFERENCE_TIME)(LONG)m_frameCount * (REFERENCE_TIME)m_rtAvgPerFrame;
    REFERENCE_TIME rtEnd = rtStart + (REFERENCE_TIME)m_rtAvgPerFrame;

    pSample->SetTime(&rtStart, &rtEnd); // vt+0x18

    m_frameCount += 1; // pin+0x100

    pSample->SetSyncPoint(TRUE); // vt+0x20
    MMDXTrace("FillBuffer delivered frame %d\n", m_frameCount);

    ::LeaveCriticalSection(&m_csRender);
    return S_OK;
}

// =============================================================================
// VA 0x100011F0 — CPushPinDIBSq::GetMediaType(CMediaType*)  (root +0x1C)
// =============================================================================
//
// Reached through CBasePin::GetMediaType(int,CMediaType*) (0x100022B0,
// primary +0x34) for iPosition == 0 only; other positions return
// VFW_S_NO_MORE_ITEMS (0x0004023F) from there.
//
// Binary flow (lock = the owning filter's critical section: *(pin+0xE8)+0x58
// — the filter pointers at pin+0xA0 / pin+0xE8 reference the same CSource,
// whose m_CritSec sits at +0x58):
//   pmt == NULL                -> E_POINTER
//   +0x5B3 || !+0x5B0          -> E_FAIL
//   VIDEOINFOHEADER vih; memset 0x58
//   vih.AvgTimePerFrame (40)   = *(pin+0x5A8)   [lo],  *(pin+0x5AC) [hi]
//   vih.bmiHeader     (48,40B) = memcpy(*(pin+0x5A0) contents)
//   majortype  <- 0x10008720  "vids" (MEDIATYPE_Video)     (0x10004AB0)
//   formattype <- 0x10008650  FORMAT_VideoInfo              (0x10004B00)
//   SetFormat(vih, 0x58)                                    (0x10004F10)
//   subtype    <- 0x100086A0  {773C9AC0-...}                (0x10004AD0)
//   SetSampleSize(vih.bmiHeader.biSizeImage)  — i.e. VIH+0x44, NOT
//       biBitCount; when it is 0 the type becomes variable-size
//       (bFixedSizeSamples = 0 only, lSampleSize untouched) (0x10004EF0)
//   return S_OK   (SetFormat's failure is ignored, exactly as the original)
HRESULT CPushPinDIBSq::GetMediaType1(AM_MEDIA_TYPE* pmt) {
    if (pmt == NULL)
        return E_POINTER;

    // *(pin+0xE8) == CBasePin::m_pFilter in the port (both binary copies
    // reference the same CSource); the lock is its +0x58 critical section.
    ::EnterCriticalSection(&m_pFilter->m_CritSec);

    if (m_bStreamEnded || !m_bBitmapSet) {
        ::LeaveCriticalSection(&m_pFilter->m_CritSec);
        return E_FAIL;
    }

    // The binary's format block is a 0x58-byte VIDEOINFOHEADER VARIANT:
    // rcSource(+0x00)/rcDest(+0x10), 8 zero bytes at +0x20, AvgTimePerFrame
    // at +0x28 (Src[10..11] = pin+0x5A8), the pushed BITMAPINFOHEADER at
    // +0x30 (qmemcpy(&Src[12])) - i.e. the header sits 8 bytes later than
    // the SDK's VIDEOINFOHEADER (biSizeImage lands at +0x44, which is
    // exactly what FillBuffer/0x10001310 dereference). Reproduced
    // field-for-field; a stock VIDEOINFOHEADER is 0x50 bytes and the
    // SampleGrabber rejects the connection over the format mismatch.
    struct {
        RECT rcSource;              // +0x00
        RECT rcDest;                // +0x10
        DWORD pad20[2];             // +0x20 (zeroed)
        LONGLONG AvgTimePerFrame;   // +0x28
        BITMAPINFOHEADER bmiHeader; // +0x30
    } vih;
    memset(&vih, 0, sizeof(vih)); // 0x58 bytes
    vih.AvgTimePerFrame = (LONGLONG)m_rtAvgPerFrame;
    memcpy(&vih.bmiHeader, PushBIH(this), sizeof(BITMAPINFOHEADER));

    MtSetType_10004AB0(pmt, &kMediaVideo);
    MtSetFormatType_10004B00(pmt, &kFormatVideoInfo);
    MtSetFormat_10004F10(pmt, &vih, sizeof(vih));
    MtSetSubtype_10004AD0(pmt, &kSubtypeARGB32);
    MtSetSampleSize_10004EF0(pmt, (LONG)vih.bmiHeader.biSizeImage);

    ::LeaveCriticalSection(&m_pFilter->m_CritSec);
    return S_OK;
}

// =============================================================================
// VA 0x10001430 — CPushPinDIBSq::CheckMediaType(pmt)  (pin primary +0x20)
// =============================================================================
//
// Binary flow:
//   pmt == NULL -> E_POINTER
//   CMediaType mt;  InitMediaType (0x10004DC0)
//   (root+0x1C)(this-0x48, &mt)   == virtual GetMediaType1(&mt)
//       (result ignored)
//   bSame = (mt == *pmt)          (0x10004DD0)
//   FreeMediaType(&mt)            (0x10004DB0)
//   return bSame ? S_OK : E_FAIL
HRESULT CPushPinDIBSq::CheckMediaType(const AM_MEDIA_TYPE* pmt) {
    if (pmt == NULL)
        return E_POINTER;

    AM_MEDIA_TYPE mt;
    MMDxShow_InitMediaType(&mt);

    GetMediaType1(&mt); // virtual, result ignored

    BOOL bSame = MtIsEqual_10004DD0(&mt, pmt);

    MMDxShow_FreeMediaType(&mt);

    return bSame ? S_OK : E_FAIL;
}

// =============================================================================
// VA 0x10001310 — CPushPinDIBSq::DecideBufferSize (was Pin_v3C)
//     (pin primary vtable +0x3C; CSourceStream leaves the slot pure)
// =============================================================================
//
// Binary flow (this = the CBaseOutputPin subobject at pin+0x48; the lock is
// *(this+0xA0)+0x58 == the owning filter's critical section):
//   pAlloc == NULL || pProps == NULL -> E_POINTER
//   pin+0x5B3 || !pin+0x5B0          -> E_FAIL
//   pProps->cBuffers = 1
//   cb = *(VIH + 0x44)               == bmiHeader.biSizeImage of the
//                                      connected type (see FillBuffer note)
//   if (cb > pProps->cbBuffer) pProps->cbBuffer = cb
//   hr = pAlloc->SetProperties(pProps, &actual)
//        (binary dispatches vtable slot +0x0C — the old ActiveMovie-1.0
//         IMemAllocator layout; the port calls the modern interface method,
//         which is slot +0x14 in current headers)
//   hr < 0                            -> return hr
//   actual.cbBuffer < pProps->cbBuffer -> E_FAIL
//   return S_OK
HRESULT CPushPinDIBSq::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) {
    ::EnterCriticalSection(&m_pFilter->m_CritSec); // filter CS (+0x58)

    if (pAlloc == NULL || pProps == NULL) {
        ::LeaveCriticalSection(&m_pFilter->m_CritSec);
        return E_POINTER;
    }
    if (m_bStreamEnded || !m_bBitmapSet) {
        ::LeaveCriticalSection(&m_pFilter->m_CritSec);
        return E_FAIL;
    }

    pProps->cBuffers = 1;

    LONG cbFrame = (LONG)PushBIH(this)->biSizeImage; // *(VIH+0x44)
    if ((ULONG)cbFrame > (ULONG)pProps->cbBuffer)
        pProps->cbBuffer = cbFrame;

    ALLOCATOR_PROPERTIES actual;
    HRESULT hr = pAlloc->SetProperties(pProps, &actual);
    if (hr < 0) {
        ::LeaveCriticalSection(&m_pFilter->m_CritSec);
        return hr;
    }
    if (actual.cbBuffer < pProps->cbBuffer) {
        ::LeaveCriticalSection(&m_pFilter->m_CritSec);
        return E_FAIL;
    }

    ::LeaveCriticalSection(&m_pFilter->m_CritSec);
    return S_OK;
}
