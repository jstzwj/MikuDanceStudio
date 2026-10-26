#include "native_host_fixture.hpp"

namespace {
int Run(const std::filesystem::path& executable, const std::filesystem::path& hack,
        const std::filesystem::path& enginePath, const std::filesystem::path& output) {
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "native host device caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF || (caps.PixelShaderVersion >> 8 & 255) < 3)
        return 77;
    OriginalPhysicsModule image(executable);
    NativeMmdQueries queries(image);
    auto mmh = std::make_unique<NativeMmhOracle>(hack, queries);
    NativeMmeOracle engine(enginePath, [&](const char* library, const char* symbol) {
        return _stricmp(library, "MMHack.dll") == 0 ? mmh->Export<FARPROC>(symbol)
                                                    : queries.Export(symbol);
    });
    BindEngine(*mmh, static_cast<HMODULE>(engine.Module()), f.window);
    auto* wrapped = mmh->Device(f.device);
    NativeHostDevice device(f, queries, wrapped);
    f.device = &device;
    f.renderer->device = &device;
    struct RestoreDevice {
        Fixture& f;
        NativeHostDevice& d;
        ~RestoreDevice() {
            f.device = d.real;
            f.renderer->device = f.device;
        }
    } restore{f, device};
    auto& model = f.LoadModel(0, 1);
    mmh->RegisterModel(model);
    SharedToons toons(f);
    StandardEffect standard(*f.renderer, &device, 3);
    supplied = standard.effect.p;
    mmh->ReplaceImport("D3DXCreateEffectFromResourceA",
                       reinterpret_cast<FARPROC>(SupplyStandardEffect));
    using Create =
        HRESULT(WINAPI*)(IDirect3DDevice9*, HMODULE, const char*, const D3DXMACRO*, ID3DXInclude*,
                         DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    Com<ID3DXEffect> registered;
    Hr(mmh->At<Create>(sizeof(void*) == 8 ? 0x4440 : 0x40D0)(
           wrapped, nullptr, nullptr, nullptr, nullptr, 0, nullptr, registered.Out(), nullptr),
       "native standard effect registration");
    struct NativeLifetime {
        std::unique_ptr<NativeMmhOracle>& mmh;
        NativeHostDevice& device;
        ~NativeLifetime() {
            mmh.reset();
            device.enabled = false;
        }
    } nativeLifetime{mmh, device};
    auto& app = *f.app;
    app.ResetSceneLight();
    app.CameraMode() = 1;
    app.PlaybackActive() = 1;
    app.CameraFov() = 30;
    app.ShadowDistance() = .01125f;
    app.ShadowMode() = 2;
    app.CameraDistance() = 0;
    app.state.selfShadowEnabled = 1;
    EnableWindow(GetDlgItem(f.window, panel::kCurrentFrameEdit), FALSE);
#if defined(_M_IX86)
    unsigned control;
    _controlfp_s(&control, _PC_24, _MCW_PC);
#endif
    D3DXMATRIX world, view, projection;
    D3DXMatrixIdentity(&world);
    std::memcpy(&app.ViewRotationTransform(), &world, sizeof world);
    const D3DXVECTOR3 eye(1, .5f, -5), center(1, .5f, 0), up(0, 1, 0);
    std::memcpy(app.CameraPosition(), &eye, sizeof eye);
    app.ViewOffsetX() = 0;
    app.ViewOffsetY() = .5f;
    D3DXMatrixLookAtLH(&view, &eye, &center, &up);
    D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
    UpdateModelVertexBuffers(&app, &model, &world._11);
    std::filesystem::create_directories(output);
    std::map<std::string, Image> images;
    auto draw = [&](const char* name, UINT size = kSize) {
        auto image = Draw(f, standard, view, projection, world, output, name, nullptr, size);
        images[name] = image;
        return image;
    };
    const auto baseline = draw("unhosted");
    device.enabled = true;
    SameImage(baseline, draw("native_unassigned"), "native host unassigned image");
    const auto simple = f.directory / "simple.fx";
    std::ofstream(simple) << R"FX(
float4x4 WVP : WORLDVIEWPROJECTION;
float4 VS(float4 p:POSITION):POSITION { return mul(p,WVP); }
float4 PS():COLOR { return float4(0.2,0.7,0.3,0.6); }
technique Main < string MMDPass="object"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
technique Shadowed < string MMDPass="object_ss"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
)FX";
    Mapping(f, &engine, simple);
    const auto assigned = draw("native_assigned");
    Check(assigned.color != baseline.color, "native effect changes pixels");
    SameImage(assigned, draw("native_repeated"), "native repeated frame");
    const auto replacement = f.directory / "replacement.fx";
    {
        std::ifstream input(simple);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        const auto offset = text.find("0.2,0.7,0.3");
        Check(offset != std::string::npos, "replacement effect fixture");
        text.replace(offset, std::strlen("0.2,0.7,0.3"), "0.8,0.1,0.2");
        std::ofstream(replacement) << text;
    }
    Mapping(f, &engine, replacement);
    const auto replaced = draw("native_replaced");
    Check(replaced.color != assigned.color, "native replacement changes pixels");
    Mapping(f, &engine, simple);
    auto reload = [&] {
        auto* previous = app.ModelSlot(0);
        const std::filesystem::path path = previous->path;
        app.ModelSlot(0) = nullptr;
        app.ActiveRenderObject() = nullptr;
        Hr(mikudancestudio::mme::BeginScene(&app, &device), "unloaded frame begin");
        Hr(mikudancestudio::mme::EndScene(&app, &device), "unloaded frame end");
        ModelDispose(previous);
        // Retain the old allocation until a new identity is established.
        auto& loaded = f.LoadModelFile(path, 0, 1);
        Check(&loaded != previous, "reload creates a distinct query object");
        ::operator delete(previous);
        if (mmh)
            mmh->RegisterModel(loaded);
        UpdateModelVertexBuffers(&app, &loaded, &world._11);
    };
    reload();
    draw("native_reload_before_assignment");
    Mapping(f, &engine, simple);
    SameImage(assigned, draw("native_reloaded"), "native reloaded object image");
    const auto resized = draw("native_resized", 384);
    Check(!resized.color.empty(), "native resized frame");
    Mapping(f, &engine, {});
    SameImage(baseline, draw("native_detached"), "native effect detach");
    // Real Reset runs both original MMHack notifications and native MMEffect
    // resource teardown/rebuild; production PostDeviceReset owns the standard FX.
    auto& pp = f.renderer->presentParameters;
    pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = f.window;
    pp.BackBufferWidth = pp.BackBufferHeight = 900;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    standard.ReleaseTargets();
    Hr(standard.effect->OnLostDevice(), "standard FX lost");
    Hr(device.Reset(&pp), "native host Reset");
    Hr(standard.effect->OnResetDevice(), "standard FX reset");
    SameImage(baseline, draw("native_reset"), "native host reset image");
    mmh.reset();
    device.enabled = false;
    device.trackCreation = false;
    MmeHostInitializeRuntime(GetModuleHandleW(nullptr));
    mikudancestudio::mme::OnDeviceCreated(&app, f.window);
    struct PortLifetime {
        MMDApp& app;
        ~PortLifetime() {
            mikudancestudio::mme::OnDeviceDestroyed(&app);
            MmeHostShutdownRuntime();
        }
    } portLifetime{app};
    SameImage(baseline, draw("port_unassigned"), "port/native unassigned image");
    Mapping(f, nullptr, simple);
    SameImage(assigned, draw("port_assigned"), "port/native assigned image");
    Mapping(f, nullptr, replacement);
    SameImage(replaced, draw("port_replaced"), "port/native replaced image");
    Mapping(f, nullptr, simple);
    reload();
    draw("port_reload_before_assignment");
    Mapping(f, nullptr, simple);
    SameImage(assigned, draw("port_reloaded"), "port/native reloaded image");
    SameImage(resized, draw("port_resized", 384), "port/native resized image");
    Mapping(f, nullptr, {});
    SameImage(baseline, draw("port_detached"), "port/native detached image");
    standard.ReleaseTargets();
    Hr(standard.effect->OnLostDevice(), "port standard lost");
    MmeHostOnLostDevice(&device);
    Hr(device.Reset(&pp), "port Reset");
    MmeHostOnResetDevice(&device);
    Hr(standard.effect->OnResetDevice(), "port standard reset");
    SameImage(baseline, draw("port_reset"), "port/native Reset image");
    std::ofstream report(output / "report.json");
    report
        << "{\"architecture\":\"" << (sizeof(void*) == 8 ? "x64" : "x86")
        << "\",\"query_source\":\"original MMD\",\"interception\":\"original MMHack\",\"pairs\":[";
    bool comma = false;
    for (const char* stage : {"unassigned", "assigned", "replaced", "reload_before_assignment",
                              "reloaded", "resized", "detached", "reset"}) {
        const auto& n = images.at(std::string("native_") + stage);
        const auto& p = images.at(std::string("port_") + stage);
        SameImage(n, p, stage);
        if (comma)
            report << ',';
        comma = true;
        report << "{\"stage\":\"" << stage
               << "\",\"color_differences\":0,\"shadow_differences\":0,\"matrix_differences\":0}";
    }
    report << "],\"failures\":0}\n";
    Check(report.good(), "native host parity report");
    std::puts("Native MMD queries + MMHack + MMEffect and port GPU lifecycle matched");
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 9, "expected --original exe --mmhack dll --mme dll --output directory");
        Check(std::wstring(argv[1]) == L"--original" && std::wstring(argv[3]) == L"--mmhack" &&
                  std::wstring(argv[5]) == L"--mme" && std::wstring(argv[7]) == L"--output",
              "native render arguments");
        return Run(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[4]),
                   std::filesystem::absolute(argv[6]), std::filesystem::absolute(argv[8]));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
