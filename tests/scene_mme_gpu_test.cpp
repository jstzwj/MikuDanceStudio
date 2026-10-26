#include "scene_effect_fixture.hpp"
#include "mme_native_oracle.hpp"
#include "mmhack_api.h"
#include "MMDExport.h"
#include "mme_recording_device.hpp"

namespace {
void WriteEffect(const std::filesystem::path& path, bool secondPass) {
    std::ofstream file(path);
    file << R"FX(
float4x4 WVP : WORLDVIEWPROJECTION;
float4 Diffuse : DIFFUSE < string Object="Geometry"; >;
float4 VS(float4 p:POSITION) : POSITION { return mul(p,WVP); }
float4 PS():COLOR { return float4(0.2,0.7,0.3,0.6); }
technique Main < string MMDPass="object"; > {
 pass P { CullMode=None; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
  VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
)FX";
    if (secondPass)
        file << R"FX(
 pass Add { CullMode=None; AlphaBlendEnable=true; SrcBlend=One; DestBlend=One;
  ZWriteEnable=false; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
)FX";
    file << R"FX(
}


technique Shadowed < string MMDPass="object_ss"; > {
 pass P { CullMode=None; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
  VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
)FX";
    if (secondPass)
        file << R"FX(
 pass Add { CullMode=None; AlphaBlendEnable=true; SrcBlend=One; DestBlend=One;
  ZWriteEnable=false; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
)FX";
    file << "}\n";
    Check(file.good(), "write MME oracle effect");
}

void WriteParameterEffect(const std::filesystem::path& path) {
    std::ofstream file(path);
    file << R"FX(
float4x4 World : WORLD;
float4x4 View : VIEW;
float4x4 Projection : PROJECTION;
float4 Diffuse : DIFFUSE < string Object="Geometry"; >;
float4 LightColor : DIFFUSE < string Object="Light"; >;
float3 LightDirection : DIRECTION < string Object="Light"; >;
struct Vertex { float4 p:POSITION; float light:TEXCOORD0; };
Vertex VS(float4 p:POSITION, float3 n:NORMAL) {
 Vertex o; o.p=mul(mul(mul(p,World),View),Projection);
 o.light=0.2+0.8*saturate(dot(normalize(mul(n,(float3x3)World)),-LightDirection));
 return o;
}
float4 PS(Vertex v):COLOR { return float4(Diffuse.rgb*LightColor.rgb*v.light,Diffuse.a); }
technique Main < string MMDPass="object"; > {
 pass P { CullMode=None; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
  VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
technique Shadowed < string MMDPass="object_ss"; > {
 pass P { CullMode=None; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
  VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
)FX";
    Check(file.good(), "write parameter effect");
}

enum class SubsetAssignment {
    Inherit,
    Effect,
    Standard
};

void Mapping(Fixture& f, NativeMmeOracle* native, const std::string& modelEffect,
             const std::string& accessoryEffect,
             SubsetAssignment subset = SubsetAssignment::Inherit, bool secondAccessory = false) {
    const auto path = f.directory / "sample.emm";
    std::ofstream file(path);
    file << "[Info]\nVersion=3\n[Object]\nPmd0=" << ExpGetPmdFilename(0)
         << "\nAcs0=" << ExpGetAcsFilename(0) << "\nAcs1=" << ExpGetAcsFilename(1)
         << "\n[Effect]\nDefault=none\nPmd0="
         << (subset == SubsetAssignment::Effect ? "none" : modelEffect)
         << "\nAcs0=" << accessoryEffect
         << "\nAcs1=" << (secondAccessory ? accessoryEffect : "none") << '\n';
    if (subset == SubsetAssignment::Effect)
        file << "Pmd0[0]=" << modelEffect << '\n';
    if (subset == SubsetAssignment::Standard)
        file << "Pmd0[0]=none\n";
    file.close();
    Check(file.good(), "write oracle EMM");
    if (native)
        native->LoadMapping(path, f.window);
    else
        Check(::mme::MmeEmmLoad(path.string().c_str()), "port EMM load");
}

std::vector<DWORD> Offscreen(MmeRecordingDevice& device, UINT size,
                             const std::filesystem::path& path) {
    if (!device.offscreen || device.offscreenSize != size)
        std::fprintf(stderr, "offscreen expected=%u actual=%u texture=%p\n", size,
                     device.offscreenSize, device.offscreen);
    Check(device.offscreen && device.offscreenSize == size, "offscreen dimensions");
    Com<IDirect3DSurface9> surface, copy;
    Hr(device.offscreen->GetSurfaceLevel(0, surface.Out()), "offscreen surface");
    Hr(device.CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                          copy.Out(), nullptr),
       "oracle offscreen readback");
    Hr(device.GetRenderTargetData(surface.p, copy.p), "oracle offscreen pixels");
    Hr(D3DXSaveSurfaceToFileW(path.c_str(), kPngFormat, copy.p, nullptr, nullptr),
       "oracle offscreen PNG");
    D3DLOCKED_RECT lock{};
    Hr(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "oracle offscreen lock");
    std::vector<DWORD> pixels(size * size);
    for (UINT y = 0; y < size; ++y)
        std::memcpy(pixels.data() + y * size, static_cast<const char*>(lock.pBits) + y * lock.Pitch,
                    size * 4);
    Hr(copy->UnlockRect(), "oracle offscreen unlock");
    Check(std::set<DWORD>(pixels.begin(), pixels.end()).size() > 1, "offscreen scene geometry");
    return pixels;
}

int Run(const std::filesystem::path& modelFile, const std::filesystem::path& dll,
        const std::filesystem::path& output, const std::filesystem::path& modelEffect,
        const std::filesystem::path& sceneEffect) {
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "oracle caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF || (caps.PixelShaderVersion >> 8 & 255) < 3)
        return 77;
    auto path = modelFile;
    if (path.empty()) {
        path = f.directory / "model.pmd";
        WriteModel(path);
    }
    auto& model = f.LoadModelFile(path, 0, 1);
    MmeRecordingDevice recording(f.device);
    recording.capturePasses = !modelEffect.empty() || !sceneEffect.empty();
    f.device = &recording;
    f.renderer->device = &recording;
    struct DeviceLifetime {
        Fixture& fixture;
        ~DeviceLifetime() {
            auto* device = static_cast<MmeRecordingDevice*>(fixture.device);
            fixture.device = device->real;
            fixture.renderer->device = fixture.device;
        }
    } deviceLifetime{f};
    SharedToons toons(f);
    auto& app = *f.app;
    app.ResetSceneLight();
    app.CameraMode() = 1;
    app.PlaybackActive() = 1;
    app.CameraFov() = 30;
    app.ShadowDistance() = .01125f;
    app.ShadowMode() = 2;
    app.CameraDistance() = 0;
    app.state.selfShadowEnabled = 1;
    app.SetSelectedModelSlot(0);
    f.renderer->shaderModelCaps = caps.MaxAnisotropy;
    SeekModelFrame(model, 0, 0);
    ModelApplyMorphs(&model);
    SetPhysicsMode(&model, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&model, 1, app.ModelSlots(), 0);
    model.toonFlag = 1;
    D3DXMATRIX world, view, projection;
    D3DXMatrixIdentity(&world);
    std::memcpy(&app.ViewRotationTransform(), &world, sizeof world);
    const bool pmx = model.physicsMode == 2;
    const D3DXVECTOR3 center(pmx ? 0.0f : 1.0f, pmx ? 10.0f : .5f, 0);
    const D3DXVECTOR3 eye(center.x, center.y, pmx ? -45.0f : -5.0f), up(0, 1, 0);
    app.ViewOffsetX() = 0;
    app.ViewOffsetY() = center.y;
    std::memcpy(app.CameraPosition(), &eye, sizeof eye);
    D3DXMatrixLookAtLH(&view, &eye, &center, &up);
    D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
    UpdateModelVertexBuffers(&app, &model, &world._11);
    CompositeScene composite(f);
    StandardEffect effect(*f.renderer, f.device, 3);
    if (!output.empty())
        std::filesystem::create_directories(output);
    std::unique_ptr<NativeMmeOracle> native;
    if (!dll.empty())
        native = std::make_unique<NativeMmeOracle>(dll);
    SetMmeOracle(native.get());
    struct ResetOracle {
        ~ResetOracle() { SetMmeOracle(nullptr); }
    } resetOracle;
    auto sample = [&](const std::string& name, UINT size = kSize) {
        recording.draws.clear();
        auto image = Draw(f, effect, view, projection, world, output, name, nullptr, size);
        recording.SaveDraws(output / (name + ".draws"));
        std::ofstream light(output / (name + ".light"), std::ios::binary);
        light.write(reinterpret_cast<const char*>(&image.light), sizeof(image.light));
        Check(light.good(), "write light matrix");
        return image;
    };
    const auto plain = sample("unhosted");
    EnableWindow(GetDlgItem(f.window, panel::kCurrentFrameEdit), FALSE);
    MmeHostInitializeRuntime(GetModuleHandleW(nullptr));
    mikudancestudio::mme::OnDeviceCreated(&app, f.window);
    struct HostLifetime {
        MMDApp& app;
        ~HostLifetime() {
            mikudancestudio::mme::OnDeviceDestroyed(&app);
            MmeHostShutdownRuntime();
        }
    } host{app};
    auto baseline = sample("unassigned");
    SameImage(plain, baseline, "unassigned effect engine changed standard scene");
    if (!modelEffect.empty() || !sceneEffect.empty()) {
        Mapping(f, native.get(), modelEffect.empty() ? "none" : modelEffect.string(),
                sceneEffect.empty() ? "none" : sceneEffect.string());
        const auto assigned = sample("external");
        const auto repeat = sample("external_repeat");
        SameImage(assigned, repeat, "external effect repeated frame");
        Check(assigned.color != baseline.color, "external effect must change pixels");
        // Real packages can depend on target history and restored device state.
        // Compare each lifecycle stage with the native engine in the runner.
        VerifyReset(f, effect, view, projection, world, output, "external_reset", false);
        recording.SaveDraws(output / "external_reset.draws");
        Check(recording.readbacks > 0, "external effect pass readbacks");
        Mapping(f, native.get(), "none", "none");
        SameImage(baseline, sample("external_detached"), "external effect detach");
        std::ofstream report(output / "report.json");
        report << "{\"native\":" << (native ? "true" : "false")
               << ",\"failures\":0,\"up_draws\":" << recording.screenDraws << "}\n";
        Check(report.good() && dialogs == 0, "external effect completion");
        return 0;
    }
    const auto simple = f.directory / "simple.fx";
    const auto multi = f.directory / "multi.fx";
    WriteEffect(simple, false);
    WriteEffect(multi, true);
    Mapping(f, native.get(), simple.string(), "none");
    const auto material = sample("material");
    Check(material.color != baseline.color, "model assignment must change pixels");
    Mapping(f, native.get(), simple.string(), "none", SubsetAssignment::Standard);
    const auto subsetNone = sample("subset_none");
    Check(subsetNone.color != material.color,
          "explicit none must override the whole-object effect");
    Mapping(f, native.get(), simple.string(), "none", SubsetAssignment::Effect);
    sample("subset");
    Mapping(f, native.get(), "none", simple.string());
    const auto accessory = sample("accessory");
    Check(accessory.color != baseline.color, "accessory assignment must change pixels");
    Mapping(f, native.get(), multi.string(), simple.string());
    sample("multipass");
    const auto parameters = f.directory / "parameters.fx";
    WriteParameterEffect(parameters);
    Mapping(f, native.get(), parameters.string(), parameters.string(), SubsetAssignment::Inherit,
            true);
    const auto parameterImage = sample("parameters");
    const float alpha = model.materials[0].diffuse[3];
    model.materials[0].diffuse[3] = alpha * .4f;
    const auto transparent = sample("transparent");
    Check(parameterImage.color != transparent.color, "transparent material must change pixels");
    model.materials[0].diffuse[3] = alpha;
    const auto savedLight = app.SceneLight();
    app.SceneLight().Diffuse.r = .2f;
    app.SceneLight().Direction.x = .4f;
    const auto savedView = view;
    const D3DXVECTOR3 shiftedEye(eye.x + 1, eye.y, eye.z);
    D3DXMatrixLookAtLH(&view, &shiftedEye, &center, &up);
    const auto changedParameters = sample("parameters_changed");
    Check(parameterImage.color != changedParameters.color,
          "changed matrices and light must change pixels");
    app.SceneLight() = savedLight;
    view = savedView;
    Mapping(f, native.get(), "none", "none");
    const auto detached = sample("detached");
    SameImage(baseline, detached, "detaching assignments changed baseline");
    const auto fixed = WriteOffscreenEffect(f);
    recording.offscreen = nullptr;
    Mapping(f, native.get(), fixed.string(), "none");
    const auto fixedImage = sample("offscreen");
    const auto fixedPixels = Offscreen(recording, 64, output / "offscreen_target.png");
    const auto fixedRepeat = sample("offscreen_repeat");
    SameImage(fixedImage, fixedRepeat, "offscreen repeat image");
    Check(fixedPixels == Offscreen(recording, 64, output / "offscreen_repeat_target.png"),
          "offscreen repeat pixels");
    VerifyReset(f, effect, view, projection, world, output);
    Check(fixedPixels == Offscreen(recording, 64, output / "offscreen_reset_target.png"),
          "offscreen Reset pixels");
    recording.offscreen = nullptr;
    const auto relative = WriteOffscreenEffect(f, true);
    Mapping(f, native.get(), relative.string(), "none");
    sample("relative");
    Offscreen(recording, 256, output / "relative_target.png");
    sample("resized", 256);
    Offscreen(recording, 128, output / "resized_target.png");
    sample("restored");
    Offscreen(recording, 256, output / "restored_target.png");
    VerifyReset(f, effect, view, projection, world, output, "relative_reset");
    Offscreen(recording, 256, output / "relative_reset_target.png");
    // Let the production host observe deletion before releasing the old ID.
    // Reapply the same EMM explicitly after loading the replacement model.
    app.ModelSlot(0) = nullptr;
    for (int id : {panel::kMainComboModel, panel::kMainComboGround, panel::kMainComboNormal})
        SendDlgItemMessageA(f.window, id, CB_DELETESTRING, 1, 0);
    sample("model_removed");
    ModelDispose(&model);
    ::operator delete(&model);
    auto& replacement = f.LoadModelFile(path, 0, 1);
    SeekModelFrame(replacement, 0, 0);
    ModelApplyMorphs(&replacement);
    SetPhysicsMode(&replacement, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&replacement, 1, app.ModelSlots(), 0);
    replacement.toonFlag = 1;
    UpdateModelVertexBuffers(&app, &replacement, &world._11);
    sample("model_loaded");
    recording.offscreen = nullptr;
    Mapping(f, native.get(), relative.string(), "none");
    const auto reloaded = sample("model_reassigned");
    SameImage(fixedImage, reloaded, "model reload with the same effect");
    Offscreen(recording, 256, output / "model_reassigned_target.png");
    std::ofstream report(output / "report.json");
    report << "{\"native\":" << (native ? "true" : "false") << ",\"failures\":0}\n";
    Check(dialogs == 0, "MME oracle unexpected dialogs");
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    SetEnvironmentVariableA("OMP_NUM_THREADS", "1");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto hook =
        SetWindowsHookExW(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    int result = 1;
    try {
        std::filesystem::path model, dll, output, modelEffect, sceneEffect;
        Check(argc % 2 == 1, "expected oracle option/path pairs");
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring option(argv[i]);
            const auto path = std::filesystem::absolute(argv[i + 1]);
            if (option == L"--model")
                model = path;
            else if (option == L"--mme-original")
                dll = path;
            else if (option == L"--output")
                output = path;
            else if (option == L"--model-effect")
                modelEffect = path;
            else if (option == L"--scene-effect")
                sceneEffect = path;
            else
                throw std::runtime_error("unknown MME oracle option");
        }
        const bool temporary = output.empty();
        if (temporary)
            output = std::filesystem::temp_directory_path() /
                     ("mds-mme-images-" + std::to_string(GetCurrentProcessId()));
        result = Run(model, dll, output, modelEffect, sceneEffect);
        if (temporary && result == 0) {
            std::error_code ignored;
            std::filesystem::remove_all(output, ignored);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "MME oracle: %s\n", e.what());
    }
    UnhookWindowsHookEx(hook);
    return result;
}
