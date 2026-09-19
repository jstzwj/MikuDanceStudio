// ===========================================================================
// DirectShow AVI recording cluster (originals 0x00408F20..0x00409A80).
// ===========================================================================
// All of these are methods of the 0x6C-byte recorder object allocated by
// 0x466D20 and stored at app+0xA06C0 (kPtrSub06c).  Member map (dword index /
// byte offset), reconstructed from 0x409A80/0x409730/0x409320:
//
//   [0]  /0x00  selected compressor IBaseFilter (bound from the video-
//               compressor category by 0x409170/0x409730 via IMoniker)
//   [1]  /0x04  IAMVfwCompressDialogs on the compressor
//               (IID D8D715A3-6E5E-11D0-B3F0-00AA003761C5 @0x53CDC0)
//   [2]  /0x08  compressor state blob (malloc'd by 0x4092A0)
//   [3]  /0x0C  IGraphBuilder
//   [4]  /0x10  MMDxShow source filter (CLSID 2F1713B8-... @0x529848)
//   [5]  /0x14  SampleGrabber filter
//   [6]  /0x18  AVI mux (from ICaptureGraphBuilder2::SetOutputFileName)
//   [7]  /0x1C  file writer (same call)
//   [8]  /0x20  source output pin      [9]  /0x24 grabber input pin
//   [10] /0x28  grabber output pin     [11] /0x2C compressor input pin
//   [12] /0x30  compressor output pin  [13] /0x34 mux video input pin
//   [16] /0x40  mux audio input pin (2nd free pin)
//   [17] /0x44  IMediaControl (IID 56A868B1 @0x53CE80)
//   [18] /0x48  wave source filter     [19] /0x4C wave source output pin
//   [20] /0x50  audio SampleGrabber    [21] /0x54 its input pin
//   [22] /0x58  its output pin
//   [23] /0x5C  IMediaEvent (IID 56A868B6 @0x53CE70)
//   [24] /0x60  compressor state size (bytes)
//   [25] /0x64  selected codec index (combo row minus the "AVI Raw" row)
//   [26] /0x68  MMDxShow frame-push interface (IID ECFAB031-... @0x529980)
//
// Bodies in this file:
//   0x004095D0 FindPinDShow      - enum-pins finder (dir match, skip count)
//   0x004096D0 FindInputPin      - wrapper, PINDIR_INPUT
//   0x00409700 FindOutputPin     - wrapper, PINDIR_OUTPUT
//   0x00409170 BindSelectedCompressor - moniker walk + BindToObject +
//                                      IAMVfwCompressDialogs QI
//   0x00408F20 FillCodecCombo    - codec combo fill (FriendlyName, SJIS)
//   0x00409730 RebindCodecOnComboChange - codec bind on combo change
//   0x004092A0 ShowCodecConfigDialog - codec config dialog + state capture
//   0x00409A80 BuildRecordingGraph - full recording graph build
//
// Vtable-slot notes pinned against the original disassembly:
//   * IMoniker is IPersistStream-derived, so BindToObject lands at slot 8
//     (+0x20) and BindToStorage at slot 9 (+0x24) - 0x409279 / 0x408F20.
//   * IAMVfwCompressDialogs: slot 3 ShowDialog, slot 6 SendDriverMessage
//     (+0x18).  The codec state travels as SendDriverMessage(0x5000=GET,
//     buf, size) / (0x5001=SET, buf, size) - 0x4092A0 and the 0x409A80 tail
//     at 0x40A4E8.
//   * IAMStreamControl (36B73881 @0x53CDF0): StartAt slot 3, StopAt slot 4 -
//     the audio branch bounds the recording with StopAt(seconds*1e7).
//   * IMediaEvent (IDispatch-derived): GetEvent slot 8, FreeEventParams
//     slot 12; IMediaControl: Run slot 7, Stop slot 9 (see 0x409320 in
//     shutdown_cleanup.cpp).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <dshow.h>   // strmif/control/uuids: graph, capture builder, mux cfg
#include <ocidl.h>   // IPropertyBag (moniker BindToStorage for FriendlyName)

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/dshow_recorder.hpp"

namespace mikudancestudio {

// Defined in src/app/shutdown_cleanup.cpp (drain + release run).
void TeardownDShowGraph(DShowRecorder* rec);

namespace {

// ---- GUIDs pinned from the original .rdata -------------------------------
// 0x529848: MMDxShow push-source CLSID (Data_MMDxShow.dll)
const GUID kMmdxShowClsid = {
    0x2f1713b8, 0xdd1f, 0x4186, {0x93, 0xbe, 0xfd, 0xa5, 0x0b, 0xf6, 0x87, 0xc7}};
// 0x529980: MMDxShow frame-push interface (ECFAB031-...)
const GUID kIidPushSource = {
    0xecfab031, 0x72ba, 0x4120, {0xb9, 0xf7, 0x8a, 0x3d, 0x5f, 0xd3, 0x8d, 0xec}};
// 0x53CEE0: MEDIASUBTYPE_ARGB32 (byte-identical to the baseclasses
// constant; x64 copy at 0x7FF7CB549E28).  Fed to ISampleGrabber::
// SetMediaType unconditionally (x64 0x7FF7CB42ADC5).
const GUID kSubtypeArgb32 = {
    0x773c9ac0, 0x3274, 0x11d0, {0xb7, 0x24, 0x00, 0xaa, 0x00, 0x6c, 0x1a, 0x01}};
// 0x53CE50: SampleGrabber CLSID (qedit.h is not shipped with modern SDKs,
// so the CLSID/IID are pinned here instead).
const GUID kClsidSampleGrabber = {
    0xc1f400a0, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const IID kIidISampleGrabber = {
    0x6b652fff, 0x11fe, 0x4fce, {0x92, 0xad, 0x02, 0x66, 0xb5, 0xd7, 0xc7, 0x8f}};

// ---- helpers (same conventions as shutdown_cleanup.cpp) -------------------
template <typename T>
void ReleaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

// IAMVfwCompressDialogs raw slots (slot 3 / slot 6).
// qedit.h is not shipped with modern SDKs (see the CLSID pin above), so the
// SampleGrabber interface is declared here in the canonical qedit.h slot
// order (SetMediaType = slot 4, exactly the slot the original hits).
struct ISampleGrabberCB;
MIDL_INTERFACE("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL bOneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(
        const AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(
        AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL bBufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(LONG* pBufferSize,
                                                       LONG* pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(
        IMediaSample** ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(
        ISampleGrabberCB* pCallback, LONG WhichMethodToCallback) = 0;
};

// ---- JP message strings (byte-exact Shift-JIS from .rdata) ----------------
#include "dshow_record_graph_jp.inc"

}  // namespace

// ===========================================================================
// VA 0x004095D0 - FindPinDShow  (original: sub_4095D0, __stdcall)
// ===========================================================================
// HRESULT FindPin(IBaseFilter* filter, PIN_DIRECTION dir, int skip, IPin** out)
// Enumerates the filter's pins; the pin whose QueryDirection matches `dir`
// after skipping `skip` matches lands in *out (AddRef'ed by Next, NOT
// released on success).  *out is pre-nulled; E_POINTER for a null filter,
// E_FAIL when the enumeration runs dry (the original returns the failure
// through EBX across the release tail - the port spells it out).
// =========================================================================//
HRESULT FindPinDShow(IBaseFilter* filter, int dir, int skip, IPin** outPin) {
    if (filter == nullptr)
        return 0x80004003;                                       // E_POINTER
    *outPin = nullptr;
    IEnumPins* enumPins = nullptr;
    HRESULT hr = filter->EnumPins(&enumPins);
    if (hr < 0)
        return hr;
    HRESULT result = 0x80004005;                                 // E_FAIL
    IPin* pin = nullptr;
    ULONG fetched = 0;
    if (enumPins->Next(1, &pin, &fetched) == 0) {
        for (;;) {
            PIN_DIRECTION d = static_cast<PIN_DIRECTION>(3);
            pin->QueryDirection(&d);                            // 0x409640
            if (d == dir) {
                if (skip == 0) {
                    *outPin = pin;                               // 0x409698
                    result = 0;
                    break;
                }
                --skip;
            }
            pin->Release();                                      // 0x409666
            if (enumPins->Next(1, &pin, &fetched) != 0)
                break;
        }
    }
    enumPins->Release();
    return result;
}

// ===========================================================================
// VA 0x004096D0 - FindInputPin / VA 0x00409700 - FindOutputPin
// (originals: sub_4096D0 / sub_409700, __thiscall - `this` unused)
// ===========================================================================
// Wrappers returning the pin directly (the original's EAX carries the out
// slot, not the HRESULT; callers store it unconditionally).
// =========================================================================//
IPin* FindInputPin(DShowRecorder* /*rec*/, IBaseFilter* filter, int skip) {
    IPin* pin = nullptr;
    FindPinDShow(filter, 0 /*PINDIR_INPUT*/, skip, &pin);
    return pin;
}

IPin* FindOutputPin(DShowRecorder* /*rec*/, IBaseFilter* filter, int skip) {
    IPin* pin = nullptr;
    FindPinDShow(filter, 1 /*PINDIR_OUTPUT*/, skip, &pin);
    return pin;
}

// ===========================================================================
// VA 0x00409170 - BindSelectedCompressor  (original: sub_409170, __thiscall)
// ===========================================================================
// Releases this[0]/this[1], walks the video-compressor moniker enumeration
// skipping this[25] entries, BindToObject's the moniker into this[0]
// (IID_IBaseFilter) and QIs IAMVfwCompressDialogs into this[1].  On success
// the moniker/enumerator/devEnum are left unreleased exactly like the
// original; every failure path releases them and leaves this[0] null.
// =========================================================================//
void BindSelectedCompressor(DShowRecorder* rec) {
    ReleaseCom(rec->compressor);
    ReleaseCom(rec->compressorDialogs);
    int idx = rec->selectedCodec;

    ICreateDevEnum* devEnum = nullptr;
    if (CoCreateInstance(CLSID_SystemDeviceEnum, nullptr,
                         CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                         reinterpret_cast<void**>(&devEnum)) < 0 ||
        devEnum == nullptr)
        return;
    IEnumMoniker* enumMon = nullptr;
    if (devEnum->CreateClassEnumerator(CLSID_VideoCompressorCategory,
                                       &enumMon, 0) < 0 ||
        enumMon == nullptr) {
        devEnum->Release();
        return;
    }
    enumMon->Reset();                                            // 0x4091E0
    IMoniker* mon = nullptr;
    ULONG fetched = 0;
    enumMon->Next(1, &mon, &fetched);
    while (mon != nullptr) {                                     // 0x40920C
        if (idx == 0)
            break;                                               // 0x409210
        --idx;
        mon->Release();
        mon = nullptr;
        enumMon->Next(1, &mon, &fetched);
    }
    if (mon != nullptr) {
        mon->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                          reinterpret_cast<void**>(&rec->compressor));
        if (rec->compressor != nullptr)
            rec->compressor->QueryInterface(
                IID_IAMVfwCompressDialogs,
                reinterpret_cast<void**>(&rec->compressorDialogs));
        return;  // original leaks moniker + enumerators here
    }
    enumMon->Release();
    devEnum->Release();
}

// ===========================================================================
// VA 0x00408F20 - FillCodecCombo: codec combo fill
// (; original: sub_408F20, method of the recorder object)
// =========================================================================//
// Uncompressed-codec row label 「無圧縮」(UTF-16LE 2A 67 27 57 2E 7E at
// x64 0x7FF7CB54A620 / x86 0x529880; IDA's narrow-string view renders the
// bytes as the mojibake "*g'W.~" - do not copy that back in).
static const wchar_t kJpNoCompression[] = {0x672A, 0x5727, 0x7E2E, 0x0000};
// =========================================================================//
// Clears the combo (CB_RESETCONTENT), walks the video-compressor category,
// BindToStorage -> IPropertyBag -> Read(L"FriendlyName"), converts the BSTR
// to Shift-JIS (WideCharToMultiByte CP 3) and CB_ADDSTRINGs it, then appends
// the uncompressed row ("AVI Raw" EN / 「無圧縮」 JP via SendMessageW -
// byte-exact from 0x529880) and CB_SETCURSELs it.  Returns the "AVI Raw"
// row index (saved by the dialog to app+0xA0CD8).
// =========================================================================//
int FillCodecCombo(DShowRecorder* /*rec*/, HWND hCombo, char english) {
    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    ICreateDevEnum* devEnum = nullptr;
    if (CoCreateInstance(CLSID_SystemDeviceEnum, nullptr,
                         CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                         reinterpret_cast<void**>(&devEnum)) < 0) {
        MessageBoxA(hCombo, "Failed to create system enumerator",
                    "DirectShow", 0);
        return 0;
    }
    IEnumMoniker* enumMon = nullptr;
    if (devEnum->CreateClassEnumerator(CLSID_VideoCompressorCategory,
                                       &enumMon, 0) < 0) {
        MessageBoxA(hCombo, "Failed to create class enumerator",
                    "DirectShow", 0);
        devEnum->Release();
        return 0;
    }
    enumMon->Reset();                                            // 0x4091E4 tail
    IMoniker* mon = nullptr;
    ULONG fetched = 0;
    enumMon->Next(1, &mon, &fetched);
    while (mon != nullptr) {
        IPropertyBag* bag = nullptr;
        if (mon->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                               reinterpret_cast<void**>(&bag)) >= 0) {
            VARIANT var;
            var.vt = VT_BSTR;
            var.bstrVal = nullptr;                               // v22[0]=8
            if (bag->Read(L"FriendlyName", &var, nullptr) == 0) {
                const int len = WideCharToMultiByte(
                    3 /*CP_SHIFT_JIS*/, 0, var.bstrVal, -1, nullptr, 0,
                    nullptr, nullptr);
                char* name = static_cast<char*>(std::malloc(len));
                WideCharToMultiByte(
                    3, 0, var.bstrVal, lstrlenW(var.bstrVal) + 1, name, len,
                    nullptr, nullptr);
                SendMessageA(hCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(name));
                std::free(name);
                SysFreeString(var.bstrVal);
            }
            if (bag != nullptr)
                bag->Release();
        }
        mon->Release();
        mon = nullptr;
        enumMon->Next(1, &mon, &fetched);
    }
    LRESULT index;
    if (english)
        index = SendMessageA(hCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("AVI Raw"));
    else
        index = SendMessageW(hCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kJpNoCompression));
    SendMessageA(hCombo, CB_SETCURSEL, index, 0);
    enumMon->Release();
    devEnum->Release();
    return static_cast<int>(index);
}

// ===========================================================================
// VA 0x00409730 - RebindCodecOnComboChange: codec bind on combo change
// (; original: sub_409730, __thiscall)
// ===========================================================================
// sel = new combo row.  Tears down [0]/[11]/[1]/[8]/[4]/[3], rebuilds a bare
// graph + MMDxShow source, seeds the source with a 10x10 ARGB config, then
// re-walks the compressor monikers to `sel`.  BindToObject must succeed and
// ShowDialog(4, hButton) (the codec's own test dialog) must return 0; on
// success the "test" button is enabled, the compressor joins the graph as
// "Compressor" and a trial source->compressor connection runs.  Failure
// disables the button (bind failure also shows the EN/JP message).
// =========================================================================//
void RebindCodecOnComboChange(DShowRecorder* rec, int sel, HWND hButton,
                              char english) {
    rec->selectedCodec = sel;
    rec->compressorStateSize = 0;
    ReleaseCom(rec->compressor);
    ReleaseCom(rec->compressorInput);
    ReleaseCom(rec->compressorDialogs);
    ReleaseCom(rec->sourceOutput);
    ReleaseCom(rec->videoSource);
    ReleaseCom(rec->graph);

    CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IGraphBuilder, reinterpret_cast<void**>(&rec->graph));
    CoCreateInstance(kMmdxShowClsid, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, reinterpret_cast<void**>(&rec->videoSource));
    IGraphBuilder* graph = rec->graph;
    IBaseFilter* source = rec->videoSource;
    if (graph != nullptr && source != nullptr)
        graph->AddFilter(source, L"File Source");
    rec->sourceOutput = FindOutputPin(rec, source, 0);

    IPushSource* push = nullptr;
    if (source != nullptr)
        source->QueryInterface(kIidPushSource,
                               reinterpret_cast<void**>(&push));   // 0x409FE3
    if (push != nullptr) {
        // 0x409730's 10x10 ARGB probe config (40 bytes: 0x28/10/10/
        // 0x00200001/0/400) at a nominal 30 fps.
        BITMAPINFOHEADER cfg = {};
        cfg.biSize = sizeof(cfg);        // 40 (0x28)
        cfg.biWidth = 10;
        cfg.biHeight = 10;
        cfg.biPlanes = 1;                // packed: 0x00200001
        cfg.biBitCount = 32;             // (BI_RGB ARGB)
        cfg.biCompression = BI_RGB;      // 0
        cfg.biSizeImage = 400;           // 10*10*4
        push->SetBitmapInfo(&cfg, sizeof(cfg), 30.0f);             // SetBitmapInfo
        push->Release();
    }

    // ---- compressor moniker walk to `sel` (shared shape with 0x409170) ----
    ICreateDevEnum* devEnum = nullptr;
    CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                     IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum));
    IEnumMoniker* enumMon = nullptr;
    if (devEnum != nullptr)
        devEnum->CreateClassEnumerator(CLSID_VideoCompressorCategory,
                                       &enumMon, 0);
    IMoniker* mon = nullptr;
    ULONG fetched = 0;
    if (enumMon != nullptr) {
        enumMon->Reset();
        enumMon->Next(1, &mon, &fetched);
    }
    while (mon != nullptr && sel != 0) {
        --sel;
        mon->Release();
        mon = nullptr;
        enumMon->Next(1, &mon, &fetched);
    }
    if (mon != nullptr) {
        if (mon->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                              reinterpret_cast<void**>(&rec->compressor)) >= 0) {
            IBaseFilter* comp = rec->compressor;
            IAMVfwCompressDialogs* vfw = nullptr;
            if (comp != nullptr &&
                comp->QueryInterface(IID_IAMVfwCompressDialogs,
                                     reinterpret_cast<void**>(&vfw)) >= 0 &&
                vfw != nullptr) {
                rec->compressorDialogs = vfw;
                if (vfw->ShowDialog(4, hButton) == 0) {
                    EnableWindow(hButton, TRUE);
                    if (graph != nullptr && comp != nullptr)
                        graph->AddFilter(comp, L"Compressor");
                    rec->compressorInput = FindInputPin(rec, comp, 0);
                    if (graph != nullptr)
                        graph->Connect(
                            rec->sourceOutput, rec->compressorInput);
                    if (enumMon != nullptr)
                        enumMon->Release();
                    if (devEnum != nullptr)
                        devEnum->Release();
                    return;
                }
                vfw->Release();
                rec->compressorDialogs = nullptr;
            }
        } else if (english) {
            MessageBoxA(hButton, "This codec cannot use in DirectShow.",
                        "DirectShow", 0);
        } else {
            MessageBoxA(hButton, JP_CANNOT_DS, "DirectShow", 0);
        }
    }
    EnableWindow(hButton, FALSE);                                // 0x4092D0
    if (enumMon != nullptr)
        enumMon->Release();
    if (devEnum != nullptr)
        devEnum->Release();
}

// ===========================================================================
// VA 0x004092A0 - ShowCodecConfigDialog: codec config dialog + state capture
// (; original: sub_4092A0, __thiscall)
// ===========================================================================
// Frees the previous state blob, shows the codec's own config dialog
// (ShowDialog(1, hDlg)), then queries the driver for its settings blob via
// SendDriverMessage(0x5000, ...) - size first (kept in this[24]), then the
// zero-initialized buffer (kept in this[2]).  0x409A80 re-applies it with
// SendDriverMessage(0x5001, ...) before recording.
// =========================================================================//
void ShowCodecConfigDialog(DShowRecorder* rec, HWND hDlg) {
    if (rec->compressorState != nullptr) {
        std::free(rec->compressorState);
        rec->compressorState = nullptr;
    }
    IAMVfwCompressDialogs* vfw = rec->compressorDialogs;
    if (vfw == nullptr)
        return;  // port-side guard (original would fault)
    vfw->ShowDialog(1, hDlg);                                    // 0x4092A9
    long size = vfw->SendDriverMessage(0x5000, 0, 0);
    rec->compressorStateSize = static_cast<std::int32_t>(size);
    if (size > 0) {
        void* blob = std::malloc(static_cast<std::size_t>(size));
        rec->compressorState = blob;
        std::memset(blob, 0, static_cast<std::size_t>(size));
        vfw->SendDriverMessage(
            0x5000,
            static_cast<long>(reinterpret_cast<UINT_PTR>(blob)),
            size);
    }
}

// ===========================================================================
// VA 0x00409A80 - BuildRecordingGraph: the recording graph builder
// (; original: sub_409A80, __thiscall)
// ===========================================================================
// Full build order (each failure shows its EN/JP "DirectShow" MessageBox,
// tears the graph down through 0x409320 and returns false):
//   1. release [11]/[8]/[4]/[3]; rebind the compressor (0x409170) if [0]
//   2. FilterGraph -> [3]; MMDxShow source -> [4]; AddFilter "File Source"
//   3. IPushSource QI -> [26]; GetRate must be >= 1.02 or "dll is too old";
//      SetBitmapInfo(recStruct, 40, fps)
//   4. SampleGrabber -> [5]; ISampleGrabber::SetMediaType(video,
//      ARGB32 unconditionally); AddFilter "Sample Grabber"
//   5. CaptureGraphBuilder2: SetFiltergraph + SetOutputFileName(Avi,
//      outPath, -> [6] mux, [7] writer)
//   6. compressor AddFilter "Compressor"; pin web through 0x4096D0/0x409700;
//      Connect source->grabber(+->compressor)->mux; re-apply the codec state
//      blob (SendDriverMessage 0x5001) when this[24] > 0
//   7. wave branch: AddSourceFilter "Source" -> [18], audio grabber -> [20],
//      mux 2nd input [16]; IAMStreamControl StartAt/StopAt(seconds*1e7);
//      IConfigAviMux::SetMasterStream(1)
//   8. IMediaFilter::SetSyncSource(NULL); IConfigInterleaving put_Mode(2) +
//      put_InterleaveTimes(1s/0.75s)
//   9. IMediaEvent -> [23], IMediaControl -> [17], Run
// =========================================================================//
bool BuildRecordingGraph(DShowRecorder* rec, HWND hwnd, unsigned char english,
                         void* outPath,
               void* config, float fps,
               const wchar_t* wavPath, float seconds) {
    ReleaseCom(rec->compressorInput);
    ReleaseCom(rec->sourceOutput);
    ReleaseCom(rec->videoSource);
    ReleaseCom(rec->graph);
    if (rec->compressor != nullptr)
        BindSelectedCompressor(rec);                             // 0x409B3E

    // 1: filter graph + MMDxShow source
    CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IGraphBuilder, reinterpret_cast<void**>(&rec->graph));
    IGraphBuilder* graph = rec->graph;
    if (graph == nullptr) {
        MessageBoxA(hwnd, english ? "failed create a filter graph"
                                  : JP_FILTERGRAPH, "DirectShow", 0);
        return false;
    }
    CoCreateInstance(kMmdxShowClsid, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IBaseFilter, reinterpret_cast<void**>(&rec->videoSource));
    IBaseFilter* source = rec->videoSource;
    if (source == nullptr) {
        MessageBoxA(hwnd, english
                    ? "cannot read MMDxShow->dll\n\nthere is not "
                      "'MMDxShow->dll' in 'Data' folder.\nplease download "
                      "the newest ver. MikuMikuDance."
                    : JP_CANNOT_READ_DLL, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    if (graph->AddFilter(source, L"File Source") < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not add DIB Sequential source filter to the "
                      "graph."
                    : JP_DIB_ADD, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }

    // 2: frame-push handshake
    IPushSource* push = nullptr;
    if (source->QueryInterface(kIidPushSource,
                               reinterpret_cast<void**>(&push)) < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not get DIB Sequential source filter "
                      "interface."
                    : JP_DIB_IF, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    rec->framePush = push;
    float ver = 0.0f;
    HRESULT hr = push->GetRate(&ver);                            // GetRate
    if (ver < 1.02f || hr < 0) {
        MessageBoxA(hwnd, english
                    ? "MMDxShow->dll is too old.\n\nPlease get newest "
                      "MikuMikuDance and put MMDxShow->dll to "
                      "'Data'folder."
                    : JP_DLL_OLD, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    push->SetBitmapInfo(config, 40, fps);                        // SetBitmapInfo

    // 3: SampleGrabber with the video media type
    if (CoCreateInstance(kClsidSampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IBaseFilter, reinterpret_cast<void**>(&rec->videoGrabber)) < 0) {
        MessageBoxA(hwnd, english ? "Failed create a SampleGrabber filter"
                                  : JP_SG_FILTER, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    IBaseFilter* grabber = rec->videoGrabber;
    ISampleGrabber* sg = nullptr;
    if (grabber->QueryInterface(kIidISampleGrabber,
                                reinterpret_cast<void**>(&sg)) < 0) {
        MessageBoxA(hwnd, english ? "Failed create a SampleGrabber Interface"
                                  : JP_SG_IF, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    {
        // CMediaType() default ctor (0x522DB0): zeroed, bFixedSizeSamples
        // TRUE, lSampleSize 1; SetType(MEDIATYPE_Video) / SetSubtype(...)
        AM_MEDIA_TYPE mt = {};
        mt.bFixedSizeSamples = TRUE;
        mt.lSampleSize = 1;
        mt.majortype = MEDIATYPE_Video;                          // "vids"
        // x64 0x7FF7CB42ADC5: SetSubtype is fed a single GUID
        // (0x7FF7CB549E28 = ARGB32) on every path - no RGB24 variant
        // exists in the x64 image.
        mt.subtype = kSubtypeArgb32;                             // 0x53CEE0
        if (sg->SetMediaType(&mt) < 0) {                         // SetMediaType
            MessageBoxA(hwnd, english ? "Failed insert a SampleGrabber"
                                      : JP_SG_INSERT, "DirectShow", 0);
            TeardownDShowGraph(rec);
            sg->Release();
            return false;
        }
    }
    if (graph->AddFilter(grabber, L"Sample Grabber") < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not add Sample Grabber filter to the graph."
                    : JP_SG_ADD, "DirectShow", 0);
        TeardownDShowGraph(rec);
        if (sg != nullptr)
            sg->Release();
        return false;
    }
    if (sg != nullptr)
        sg->Release();

    // 4: capture builder - mux + file writer
    ICaptureGraphBuilder2* builder = nullptr;
    if (CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr,
                         CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2,
                         reinterpret_cast<void**>(&builder)) < 0 ||
        builder == nullptr) {
        MessageBoxA(hwnd, english ? "failed create a CaptureGraphBuilder"
                                  : JP_CGB, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    if (builder->SetFiltergraph(graph) < 0) {
        MessageBoxA(hwnd, english ? "failed set a filter graph"
                                  : JP_SET_GRAPH, "DirectShow", 0);
        builder->Release();
        TeardownDShowGraph(rec);
        return false;
    }
    if (builder->SetOutputFileName(
            &MEDIASUBTYPE_Avi, static_cast<LPCOLESTR>(outPath),
            &rec->aviMux, &rec->fileWriter) < 0) {
        MessageBoxA(hwnd, english ? "Cannot set up MUX and File Writer"
                                  : JP_MUX, "DirectShow", 0);
        builder->Release();
        TeardownDShowGraph(rec);
        return false;
    }
    builder->Release();
    IBaseFilter* mux = rec->aviMux;
    IBaseFilter* comp = rec->compressor;

    // 5: compressor + pin web
    if (comp != nullptr && graph->AddFilter(comp, L"Compressor") < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not add selected compressor to the graph."
                    : JP_DRIVER, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    rec->sourceOutput = FindOutputPin(rec, source, 0);
    rec->grabberInput = FindInputPin(rec, grabber, 0);
    rec->grabberOutput = FindOutputPin(rec, grabber, 0);
    if (comp != nullptr) {
        rec->compressorInput = FindInputPin(rec, comp, 0);
        rec->compressorOutput = FindOutputPin(rec, comp, 0);
    }
    rec->muxVideoInput = FindInputPin(rec, mux, 0);
    IPin* srcOut = rec->sourceOutput;
    IPin* grabIn = rec->grabberInput;
    IPin* grabOut = rec->grabberOutput;
    IPin* compIn = rec->compressorInput;
    IPin* compOut = rec->compressorOutput;
    IPin* muxIn = rec->muxVideoInput;

    if (graph->Connect(srcOut, grabIn) < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not connect SourcePin to pVideoIn."
                    : JP_SRC_PIN, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }
    if (comp != nullptr) {
        if (graph->Connect(grabOut, compIn) < 0) {
            MessageBoxA(hwnd, english
                        ? "Could not connect pVideoOut to CompressorIn."
                        : JP_COMP_IN, "DirectShow", 0);
            TeardownDShowGraph(rec);
            return false;
        }
        if (graph->Connect(compOut, muxIn) < 0) {
            MessageBoxA(hwnd, english
                        ? "Could not use selected compressor"
                        : JP_CANNOT_USE, "DirectShow", 0);
            TeardownDShowGraph(rec);
            return false;
        }
        IAMVfwCompressDialogs* vfw = rec->compressorDialogs;
        if (rec->compressorStateSize > 0 &&
            vfw != nullptr) {
            // 0x40A4E8: re-apply the codec state blob captured by 0x4092A0
            vfw->SendDriverMessage(
                0x5001,
                static_cast<long>(
                    reinterpret_cast<UINT_PTR>(rec->compressorState)),
                rec->compressorStateSize);
        }
    } else if (graph->Connect(grabOut, muxIn) < 0) {
        MessageBoxA(hwnd, english
                    ? "Could not connect pVideoOut to pMuxIn1."
                    : JP_MUX_IN1, "DirectShow", 0);
        TeardownDShowGraph(rec);
        return false;
    }

    // 6: optional wave branch (wavPath == 0 skips silently; a failed
    // AddSourceFilter is what shows the "read WAVE" message)
    if (wavPath != nullptr) {
        IBaseFilter* waveSrc = nullptr;
        graph->AddSourceFilter(wavPath, L"Source", &waveSrc);     // -> [18]
        rec->waveSource = waveSrc;
        if (waveSrc == nullptr) {
            MessageBoxA(hwnd, english
                        ? "Could not read WAVE file.\n\nTrying output "
                          "only movie."
                        : JP_WAVE_READ, "DirectShow", 0);
        } else {
            rec->waveOutput = FindOutputPin(rec, waveSrc, 0);
            rec->muxAudioInput = FindInputPin(rec, mux, 1);
            CoCreateInstance(kClsidSampleGrabber, nullptr,
                             CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                             reinterpret_cast<void**>(&rec->audioGrabber));
            IBaseFilter* grabber2 = rec->audioGrabber;
            ISampleGrabber* sg2 = nullptr;
            if (grabber2 != nullptr)
                grabber2->QueryInterface(kIidISampleGrabber,
                                         reinterpret_cast<void**>(&sg2));
            if (sg2 != nullptr) {
                AM_MEDIA_TYPE mt = {};
                mt.bFixedSizeSamples = TRUE;
                mt.lSampleSize = 1;
                mt.majortype = MEDIATYPE_Audio;                   // "auds"
                sg2->SetMediaType(&mt);                          // SetMediaType
                sg2->Release();
            }
            if (grabber2 != nullptr)
                graph->AddFilter(grabber2, L"AudioGrabber");
            rec->audioGrabberInput = FindInputPin(rec, grabber2, 0);
            rec->audioGrabberOutput = FindOutputPin(rec, grabber2, 0);
            graph->Connect(
                rec->waveOutput, rec->audioGrabberInput);
            if (graph->Connect(
                    rec->audioGrabberOutput, rec->muxAudioInput) >= 0) {
                // bound the audio stream: StartAt(now), StopAt(seconds)
                IAMStreamControl* stream = nullptr;
                IPin* muxAudioIn = rec->muxAudioInput;
                if (muxAudioIn != nullptr)
                    muxAudioIn->QueryInterface(
                        IID_IAMStreamControl,
                        reinterpret_cast<void**>(&stream));
                if (stream != nullptr) {
                    stream->StartAt(nullptr, 0);                  // 0x40A43B
                    // x64 0x7FF7CB42B664..74: movss arg_38, mulss by the
                    // 1e7f slot at 0x7FF7CB552BF8, then cvttss2si r64 -
                    // float math and truncation, not double.
                    REFERENCE_TIME stop =
                        static_cast<REFERENCE_TIME>(seconds * 1e7f);
                    stream->StopAt(&stop, 0, 0);                  // 0x40A46B
                    stream->Release();
                }
                IConfigAviMux* aviMux = nullptr;
                if (mux != nullptr)
                    mux->QueryInterface(IID_IConfigAviMux,
                                        reinterpret_cast<void**>(&aviMux));
                if (aviMux != nullptr) {
                    aviMux->SetMasterStream(1);                   // 0x40A4A6
                    aviMux->Release();
                }
            } else {
                MessageBoxA(hwnd, english
                            ? "Could not out Wave file.\n\nTrying "
                              "output only movie."
                            : JP_WAVE_OUT, "DirectShow", 0);
            }
        }
    }

    // 7: no clock, capture-grade interleave
    IMediaFilter* mediaFilter = nullptr;
    graph->QueryInterface(IID_IMediaFilter,
                          reinterpret_cast<void**>(&mediaFilter));
    if (mediaFilter != nullptr) {
        if (mediaFilter->SetSyncSource(nullptr) < 0)
            MessageBoxA(hwnd, english
                        ? "Could not SetSyncSource NULL on the graph"
                        : JP_SYNC, "DirectShow", 0);
        mediaFilter->Release();
    }
    IConfigInterleaving* interleave = nullptr;
    bool interleaveOk = false;
    if (mux != nullptr)
        mux->QueryInterface(IID_IConfigInterleaving,
                            reinterpret_cast<void**>(&interleave));
    if (interleave != nullptr) {
        if (interleave->put_Mode(
                static_cast<InterleavingMode>(2)) < 0) {
            MessageBoxA(hwnd, english ? "Could not set interleaving mode"
                                      : JP_ILV_MODE, "DirectShow", 0);
        } else {
            // 0x40A620: 1s interleave / 0.75s preroll on the mux
            REFERENCE_TIME full = 10000000, cap = 7500000;
            if (interleave->put_Interleaving(&full, &cap) < 0) {
                MessageBoxA(hwnd, english
                            ? "Could not put interleave time"
                            : JP_ILV_TIME, "DirectShow", 0);
            } else {
                interleaveOk = true;
            }
        }
        interleave->Release();
    }
    if (!interleaveOk) {
        TeardownDShowGraph(rec);                                  // 0x409320
        return false;
    }

    // 8: event sink, control interface, run
    graph->QueryInterface(IID_IMediaEvent,
                          reinterpret_cast<void**>(&rec->mediaEvent));
    graph->QueryInterface(IID_IMediaControl,
                          reinterpret_cast<void**>(&rec->mediaControl));
    IMediaControl* control = rec->mediaControl;
    if (control != nullptr)
        control->Run();                                           // 0x40A646
    return true;
}

}  // namespace mikudancestudio
