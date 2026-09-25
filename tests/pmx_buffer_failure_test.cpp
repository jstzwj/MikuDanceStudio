#include "d3d9_device_fixture.hpp"

#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string caption;
std::string message;
int messageCount = 0;

class FailedIndexBuffer final : public IDirect3DIndexBuffer9 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override {
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return E_NOTIMPL; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT, UINT, void**, DWORD) override { return E_OUTOFMEMORY; }
    HRESULT STDMETHODCALLTYPE Unlock() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC*) override { return E_FAIL; }

private:
    ULONG references = 1;
};

class PmxDeviceFixture final : public SnapshotDeviceFixture {
public:
    explicit PmxDeviceFixture(IDirect3DDevice9* device)
        : SnapshotDeviceFixture(device) {}

    bool failIndexLock = false;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9** output,
        HANDLE* shared) override {
        if (!failIndexLock)
            return SnapshotDeviceFixture::CreateIndexBuffer(
                length, usage, format, pool, output, shared);
        ++indexCreates;
        *output = new FailedIndexBuffer;
        return S_OK;
    }
};

LRESULT CALLBACK CloseErrorBox(int code, WPARAM window, LPARAM data) {
    if (code == HCBT_ACTIVATE) {
        HWND dialog = reinterpret_cast<HWND>(window);
        char className[32]{};
        GetClassNameA(dialog, className, sizeof(className));
        if (std::strcmp(className, "#32770") == 0) {
            char buffer[256]{};
            GetWindowTextA(dialog, buffer, sizeof(buffer));
            caption = buffer;
            HWND label = FindWindowExA(dialog, nullptr, "Static", nullptr);
            if (label != nullptr) {
                GetWindowTextA(label, buffer, sizeof(buffer));
                message = buffer;
            }
            ++messageCount;
            SendMessageA(dialog, WM_COMMAND, IDOK, 0);
        }
    }
    return CallNextHookEx(nullptr, code, window, data);
}

void Check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}

template <typename Type> void Append(std::vector<unsigned char>& bytes,
                                      const Type& value) {
    const auto* source = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), source, source + sizeof(value));
}

std::vector<unsigned char> PmxPrefix(std::uint8_t indexWidth) {
    std::vector<unsigned char> bytes;
    Append(bytes, std::uint8_t{0});
    Append(bytes, 2.0f);
    Append(bytes, std::uint8_t{8});
    const std::uint8_t header[8] = {0, 0, indexWidth, 1, 1, 1, 1, 1};
    bytes.insert(bytes.end(), header, header + sizeof(header));
    for (int index = 0; index < 4; ++index) Append(bytes, std::uint32_t{0});
    Append(bytes, std::int32_t{1});
    const unsigned char vertex[32]{};
    bytes.insert(bytes.end(), vertex, vertex + sizeof(vertex));
    Append(bytes, std::uint8_t{0});
    Append(bytes, std::uint8_t{0});
    Append(bytes, 1.0f);
    Append(bytes, std::int32_t{1});
    if (indexWidth == 4)
        Append(bytes, std::uint32_t{0});
    else
        Append(bytes, std::uint16_t{0});
    return bytes;
}

void RunFailure(PmxDeviceFixture& device, const char* expectedCaption,
                unsigned expectedVertexCreates, unsigned expectedIndexCreates,
                std::uint8_t indexWidth = 2, HWND owner = nullptr) {
    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, directory) != 0, "temporary directory");
    Check(GetTempFileNameW(directory, L"pmx", 0, path) != 0, "temporary file");
    int handle = -1;
    Check(_wsopen_s(&handle, path, _O_BINARY | _O_CREAT | _O_TRUNC | _O_RDWR,
                    _SH_DENYNO, _S_IREAD | _S_IWRITE) == 0, "open PMX fixture");
    const auto bytes = PmxPrefix(indexWidth);
    Check(_write(handle, bytes.data(), static_cast<unsigned>(bytes.size())) ==
          static_cast<int>(bytes.size()), "write PMX fixture");
    _lseek(handle, 0, SEEK_SET);

    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = &device;
    std::vector<unsigned char> storage(mikudancestudio::mdl::kSize, 0);
    mikudancestudio::ModelInitDefaults(storage.data());
    auto& model = *mikudancestudio::mdl::Mdl(storage.data());
    model.physicsFlags = 1;
    model.hwnd = owner;
    mikudancestudio::PathResolutionWorkspace paths{};
    caption.clear();
    message.clear();
    messageCount = 0;
    HHOOK hook = SetWindowsHookExA(WH_CBT, CloseErrorBox, nullptr,
                                  GetCurrentThreadId());
    Check(hook != nullptr, "install dialog hook");
    const bool loaded = mikudancestudio::LoadPMX(
        storage.data(), renderer.get(), 0, 1, paths, handle);
    UnhookWindowsHookEx(hook);
    DeleteFileW(path);
    Check(!loaded, "failure injection must reject PMX");
    Check(messageCount == 1, "expected one PMX error dialog");
    Check(caption == expectedCaption, "wrong PMX error caption");
    Check(message == "The performance of the graphics card doesn't suffice.",
          "wrong PMX error message");
    Check(device.vertexCreates == expectedVertexCreates &&
          device.indexCreates == expectedIndexCreates, "wrong D3D call sequence");

    for (auto* text : model.pmxTextBuffers) ::operator delete(text);
    if (model.vertexBuffer)
        static_cast<IDirect3DVertexBuffer9*>(model.vertexBuffer)->Release();
    if (model.vertexBuffer2)
        static_cast<IDirect3DVertexBuffer9*>(model.vertexBuffer2)->Release();
    if (model.indexBuffer)
        static_cast<IDirect3DIndexBuffer9*>(model.indexBuffer)->Release();
    ::operator delete(model.pmxVertices);
    ::operator delete(mikudancestudio::mdl::PmxIndices(storage.data()));
}

}

int main() {
    try {
        PmxDeviceFixture vertexFailure(nullptr);
        vertexFailure.vertexResults.push_back(E_OUTOFMEMORY);
        RunFailure(vertexFailure, "open file", 1, 0);

        HWND window = CreateWindowW(L"STATIC", L"PMX failure fixture",
                                    WS_OVERLAPPED, 0, 0, 64, 64, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window == nullptr) return 77;
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (d3d == nullptr) { DestroyWindow(window); return 77; }
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        IDirect3DDevice9* real = nullptr;
        const HRESULT created = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &real);
        if (FAILED(created)) { d3d->Release(); DestroyWindow(window); return 77; }
        PmxDeviceFixture indexFailure(real);
        indexFailure.indexResults.push_back(E_OUTOFMEMORY);
        RunFailure(indexFailure, "CreateIndexBuffer", 2, 1, 2, window);
        PmxDeviceFixture edgeFailure(real);
        edgeFailure.vertexResults.push_back(S_OK);
        edgeFailure.vertexResults.push_back(E_OUTOFMEMORY);
        RunFailure(edgeFailure, "open file", 2, 0, 2, window);
        PmxDeviceFixture lockFailure(real);
        lockFailure.failIndexLock = true;
        RunFailure(lockFailure, "pIndBuf->Lock", 2, 1, 2, window);
        PmxDeviceFixture wideLockFailure(real);
        wideLockFailure.failIndexLock = true;
        RunFailure(wideLockFailure, "pIndBuf->Lock", 2, 1, 4, window);
        real->Release();
        d3d->Release();
        DestroyWindow(window);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
