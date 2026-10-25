#include <initguid.h>
#include "../src/mmdxshow/mmdxshow.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 1;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::fprintf(stderr, "LoadLibrary: %lu\n", GetLastError());
        return 1;
    }
    auto getFactory = reinterpret_cast<HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**)>(
        GetProcAddress(dll, "DllGetClassObject"));
    IClassFactory* factory = nullptr;
    if (!getFactory || FAILED(getFactory(MMDXSHOW_CLSID_PushSourceDIBSq, IID_IClassFactory,
                                         reinterpret_cast<void**>(&factory))))
        return 1;
    IPushSource* push = nullptr;
    const HRESULT created =
        factory->CreateInstance(nullptr, MMDXSHOW_IID_IPushSource, reinterpret_cast<void**>(&push));
    factory->Release();
    if (FAILED(created) || !push)
        return 1;
    IBaseFilter* filter = nullptr;
    Check(push->QueryInterface(IID_IBaseFilter, reinterpret_cast<void**>(&filter)) == S_OK,
          "query adjusted filter branch");
    if (filter) {
        CLSID clsid{};
        Check(filter->GetClassID(&clsid) == S_OK &&
                  IsEqualGUID(clsid, MMDXSHOW_CLSID_PushSourceDIBSq),
              "filter class ID uses the inline native GUID");
        FILTER_STATE state = State_Running;
        Check(filter->GetState(0, &state) == S_OK && state == State_Stopped,
              "filter starts stopped");
        IEnumPins* pins = nullptr;
        Check(filter->EnumPins(&pins) == S_OK && pins, "enumerate the source pin");
        if (pins) {
            IPin* pin = nullptr;
            ULONG fetched = 0;
            Check(pins->Next(1, &pin, &fetched) == S_OK && fetched == 1, "one source pin");
            if (pin) {
                PIN_DIRECTION direction = PINDIR_INPUT;
                Check(pin->QueryDirection(&direction) == S_OK && direction == PINDIR_OUTPUT,
                      "pin direction is read from its own base subobject");
                PIN_INFO info{};
                Check(pin->QueryPinInfo(&info) == S_OK && info.pFilter == filter,
                      "pin retains its owner filter");
                if (info.pFilter)
                    info.pFilter->Release();
                Check(pin->NewSegment(123, 456, .5) == S_OK,
                      "cache segment bounds and playback rate");
                LPWSTR id = nullptr;
                Check(pin->QueryId(&id) == S_OK && id && std::wcscmp(id, L"1") == 0,
                      "pin ID preserves the one-based original index");
                CoTaskMemFree(id);
                pin->Release();
            }
            pins->Release();
        }
        filter->Release();
    }
    // Test fixtures pin actual binary offsets independently of offsetof assertions.
#if defined(_M_X64)
    constexpr std::size_t pushBranch = 0xB0, pinMember = 0xB8, configMember = 0x638;
#else
    constexpr std::size_t pushBranch = 0x70, pinMember = 0x74, configMember = 0x5A0;
#endif
    auto* root = reinterpret_cast<unsigned char*>(push) - pushBranch;
    auto* pin = *reinterpret_cast<unsigned char**>(root + pinMember);
    Check(*reinterpret_cast<void**>(pin + configMember) == nullptr,
          "constructor clears the original config slot");
    BITMAPINFOHEADER bitmap{};
    bitmap.biSize = 40;
    bitmap.biWidth = 640;
    bitmap.biHeight = 480;
    bitmap.biPlanes = 1;
    bitmap.biBitCount = 32;
    bitmap.biSizeImage = 640 * 480 * 4;
    Check(push->SetBitmapInfo(&bitmap, 17, 30.0f) == S_OK, "bitmap configuration succeeds");
    const void* config = *reinterpret_cast<void**>(pin + configMember);
    Check(config && std::memcmp(config, &bitmap, 40) == 0,
          "bitmap resides at the original ABI offset");
    BYTE state = 99;
    Check(push->GetStreamingState(&state) == S_OK && state == 1,
          "configuration acknowledges the initial frame");
    float version = 0;
    Check(push->GetRate(&version) == S_OK && version == 1.02f, "protocol version is 1.02");
    unsigned char pixels[16]{};
    Check(push->StartStreaming(reinterpret_cast<DWORD_PTR>(pixels)) == S_OK,
          "frame submission succeeds");
    Check(push->GetStreamingState(&state) == S_OK && state == 0,
          "frame submission clears acknowledgement");
    Check(push->BeginStreaming() == S_OK, "end latch succeeds once");
    Check(push->GetStreamingState(&state) == E_FAIL && push->GetRate(&version) == E_FAIL &&
              push->StartStreaming(0) == E_FAIL && push->SetBitmapInfo(&bitmap, 0, 30) == E_FAIL &&
              push->BeginStreaming() == E_FAIL,
          "latched methods return E_FAIL");
    push->Release();
    FreeLibrary(dll);
    return failures ? 1 : 0;
}
