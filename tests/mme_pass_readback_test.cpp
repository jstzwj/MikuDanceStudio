#include "mme_recording_device.hpp"
#include <cstdio>
#include <functional>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Hr(HRESULT result, const char* message) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: HRESULT=0x%08lX\n", message, static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}
void Reject(const std::function<void()>& action, const char* diagnostic) {
    try {
        action();
    } catch (const std::runtime_error& error) {
        Check(std::string(error.what()).find(diagnostic) != std::string::npos,
              "failure must identify its cause");
        return;
    }
    throw std::runtime_error("invalid capture/comparison was accepted");
}
using Recording = MmeRecordingDevice;
template <class T> using Com = Recording::Owned<T>;
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"MME pass readback", WS_OVERLAPPED, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};
struct FailedReadback : SnapshotDeviceFixture {
    using SnapshotDeviceFixture::SnapshotDeviceFixture;
    bool failCreate = false;
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT levels, DWORD usage,
                                            D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DTexture9** texture, HANDLE* shared) override {
        if (failCreate) {
            *texture = nullptr;
            return E_OUTOFMEMORY;
        }
        return real->CreateTexture(w, h, levels, usage, format, pool, texture, shared);
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*, IDirect3DSurface9*) override {
        return D3DERR_DEVICELOST;
    }
};
void TestRows() {
    std::array<unsigned char, 17 * 7> a{}, b{};
    for (unsigned y = 0; y < 7; ++y)
        for (unsigned x = 0; x < 17; ++x) {
            a[y * 17 + x] = static_cast<unsigned char>(x * 19 + y * 7);
            b[y * 17 + x] = x < 13 ? a[y * 17 + x] : 0xFE;
        }
    const auto hash = Recording::HashRows(a.data(), 17, 13, 7);
    Check(hash == Recording::HashRows(b.data(), 17, 13, 7), "row padding must be excluded");
    auto reversed = a;
    for (unsigned y = 0; y < 7; ++y)
        std::memcpy(reversed.data() + (6 - y) * 17, a.data() + y * 17, 17);
    Check(hash == Recording::HashRows(reversed.data() + 6 * 17, -17, 13, 7),
          "negative pitch must preserve logical row order on both architectures");
    b[6 * 17 + 12] ^= 1;
    Check(hash != Recording::HashRows(b.data(), 17, 13, 7), "last pixel must affect hash");
    Reject([&] { Recording::HashRows(a.data(), 12, 13, 7); }, "pitch");
    Recording recording(nullptr);
    recording.capturePasses = true;
    Reject([&] { recording.CapturePass(E_FAIL); }, "operation=draw");
    Check(recording.passes.empty(), "failed draws must not produce valid passes");
}
void TestFormat(IDirect3DDevice9* device, D3DFORMAT format) {
    constexpr UINT width = 13, height = 7;
    Com<IDirect3DSurface9> target, source;
    Com<IDirect3DTexture9> upload;
    Hr(device->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, FALSE, &target.p,
                                  nullptr),
       "create single-channel target");
    Hr(device->CreateTexture(width, height, 1, 0, format, D3DPOOL_SYSTEMMEM, &upload.p, nullptr),
       "create patterned source");
    Hr(upload.p->GetSurfaceLevel(0, &source.p), "patterned source level");
    std::array<unsigned char, width * height> expected{};
    D3DLOCKED_RECT lock{};
    Hr(source.p->LockRect(&lock, nullptr, 0), "lock patterned source");
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const auto value = static_cast<unsigned char>(x * 17 + y * 29);
            expected[y * width + x] = value;
            static_cast<unsigned char*>(lock.pBits)[y * lock.Pitch + x] = value;
        }
    Hr(source.p->UnlockRect(), "unlock patterned source");
    Hr(device->UpdateSurface(source.p, nullptr, target.p, nullptr), "upload patterned target");
    Hr(device->SetDepthStencilSurface(nullptr), "detach depth");
    Hr(device->SetRenderTarget(0, target.p), "bind target");
    Recording recording(device);
    recording.capturePasses = true;
    recording.captureContext = "single_channel";
    recording.CapturePass(S_OK);
    const auto baseline = Recording::HashRows(expected.data(), width, width, height);
    Check(recording.passTargets.at(0)[0].hash == baseline,
          "all patterned pixels must survive readback");

    // Overwrite only the last pixel through an actual fixed-function GPU draw.
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    };
    const Vertex quad[] = {{width - 1.5f, height - 1.5f, 0, 1, 0xD7D7D7D7},
                           {width - .5f, height - 1.5f, 0, 1, 0xD7D7D7D7},
                           {width - 1.5f, height - .5f, 0, 1, 0xD7D7D7D7},
                           {width - .5f, height - .5f, 0, 1, 0xD7D7D7D7}};
    Hr(device->SetVertexShader(nullptr), "fixed vertex shader");
    Hr(device->SetPixelShader(nullptr), "fixed pixel shader");
    Hr(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE), "quad format");
    Hr(device->SetRenderState(D3DRS_ZENABLE, FALSE), "disable depth");
    Hr(device->SetRenderState(D3DRS_LIGHTING, FALSE), "disable lighting");
    Hr(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "disable culling");
    Hr(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "disable blending");
    Hr(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1), "select color");
    Hr(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE), "diffuse color");
    Hr(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1), "select alpha");
    Hr(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE), "diffuse alpha");
    Hr(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE), "disable next stage");
    Hr(device->BeginScene(), "begin pixel draw");
    Hr(recording.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "draw last pixel");
    Hr(device->EndScene(), "end pixel draw");
    expected.back() = 0xD7;
    Check(recording.passTargets.at(1)[0].hash ==
              Recording::HashRows(expected.data(), width, width, height),
          "GPU draw must change exactly the last pixel");
    Check(recording.readbacks == 2, "both passes must read back successfully");
    Recording::RequireSamePasses("identical", {recording.passes[0]}, {recording.passes[0]},
                                 {recording.passTargets[0]}, {recording.passTargets[0]});
    Reject(
        [&] {
            Recording::RequireSamePasses("mutation", {recording.passes[0]}, {recording.passes[1]},
                                         {recording.passTargets[0]}, {recording.passTargets[1]});
        },
        "stage=mutation draw=0 target=0");
    Reject(
        [&] {
            Recording::RequireSamePasses("missing", recording.passes, {recording.passes[0]},
                                         recording.passTargets, {recording.passTargets[0]});
        },
        "draw=1 pass count");
    FailedReadback failing(device);
    Recording rejected(&failing);
    rejected.capturePasses = true;
    Reject([&] { rejected.CapturePass(S_OK); }, "operation=GetRenderTargetData");
    failing.failCreate = true;
    Reject([&] { rejected.CapturePass(S_OK); }, "operation=CreateTexture");
    Check(rejected.readbacks == 0 && rejected.passes.empty() && rejected.passTargets.empty(),
          "common readback failures must never count as matching passes");
    std::printf(
        "format=%u: patterned readback, one-pixel draw, diagnostics and failure rejection passed\n",
        static_cast<unsigned>(format));
}
} // namespace

int main() {
    try {
        TestRows();
        Window window;
        Com<IDirect3D9> d3d;
        d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
        if (!window.handle || !d3d.p)
            return 77;
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window.handle;
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        const auto result =
            d3d.p->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.handle,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p);
        if (FAILED(result)) {
            std::printf("SKIP: D3D9 HAL unavailable: 0x%08lX\n",
                        static_cast<unsigned long>(result));
            return 77;
        }
        TestFormat(device.p, D3DFMT_A8);
        TestFormat(device.p, D3DFMT_L8);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
