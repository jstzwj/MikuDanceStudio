#include <omp.h>
#include "scene_effect_fixture.hpp"

namespace {
int RunEffects(const std::filesystem::path& path, const std::filesystem::path& exe,
               const std::filesystem::path& output, bool withoutMme, int threads,
               int selectedScenario, bool hardwareVp) {
    Check(threads >= 1 && threads <= 64, "render thread count 1..64");
    omp_set_dynamic(0);
    omp_set_num_threads(threads);
    std::printf("port OpenMP workers: %d\n", threads);
    std::unique_ptr<OriginalPhysicsModule> binary;
    if (!exe.empty())
        binary = std::make_unique<OriginalPhysicsModule>(exe);
    const auto hook =
        SetWindowsHookExW(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    struct HookLifetime {
        HHOOK hook;
        ~HookLifetime() { UnhookWindowsHookEx(hook); }
    } hookLifetime{hook};
    Check(selectedScenario >= 0 && selectedScenario <= 6, "effect scenario 0..6");
    Fixture f(false, hardwareVp ? D3DCREATE_HARDWARE_VERTEXPROCESSING
                                : D3DCREATE_SOFTWARE_VERTEXPROCESSING);
    if (!f.ready)
        return 77;
    RenderNumericTrace recording(f.device);
    if (!output.empty()) {
        recording.shaderDirectory = output / "shaders";
        std::filesystem::create_directories(recording.shaderDirectory);
    }
    recording.capturePasses = withoutMme && selectedScenario != 0;
    struct DeviceLifetime {
        Fixture& fixture;
        IDirect3DDevice9* real;
        ~DeviceLifetime() {
            fixture.device = real;
            fixture.renderer->device = real;
        }
    } deviceLifetime{f, f.device};
    RenderNumericTrace* trace = withoutMme ? &recording : nullptr;
    if (trace)
        f.device = f.renderer->device = trace;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "device caps");
    D3DDEVICE_CREATION_PARAMETERS creation{};
    Hr(f.device->GetCreationParameters(&creation), "device creation flags");
    if (hardwareVp && !(creation.BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING))
        return 77;
    if (caps.DeviceType == D3DDEVTYPE_NULLREF || (caps.PixelShaderVersion >> 8 & 255) < 3)
        return 77;
    auto modelPath = path;
    if (modelPath.empty()) {
        modelPath = f.directory / "effect.pmd";
        WriteModel(modelPath);
    }
    auto& model = f.LoadModelFile(modelPath, 0, 1);
    SaveRenderModel(output, "port_loaded", model);
    SharedToons toons(f);
    auto& app = *f.app;
    app.ResetSceneLight();
    app.CameraFov() = 30;
    app.CameraMode() = 1;
    app.PlaybackActive() = 1;
    app.GroundGridEnabled() = 0;
    app.GroundShadowEnabled() = 0;
    app.ShadowDistance() = .01125f;
    app.ShadowMode() = 1;
    app.ViewOffsetX() = 0;
    app.ViewOffsetY() = model.physicsMode == 2 ? 10.0f : .5f;
    app.CameraDistance() = 0;
    app.state.selfShadowEnabled = 1;
    app.SetSelectedModelSlot(0);
    f.renderer->shaderModelCaps = caps.MaxAnisotropy;
    SeekModelFrame(model, 0, 0);
    ModelApplyMorphs(&model);
    SetPhysicsMode(&model, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&model, 1, app.ModelSlots(), 0);
    SaveRenderModel(output, "port_pose", model);
    model.toonFlag = 1;
    D3DXMATRIX world, view, projection;
    D3DXMatrixIdentity(&world);
    std::memcpy(&app.ViewRotationTransform(), &world, sizeof world);
    const D3DXVECTOR3 center(model.physicsMode == 2 ? 0.0f : 1.0f, app.ViewOffsetY(), 0);
    const D3DXVECTOR3 eye(center.x, center.y, model.physicsMode == 2 ? -45.0f : -5.0f), up(0, 1, 0);
    std::memcpy(app.CameraPosition(), &eye, sizeof eye);
    D3DXMatrixLookAtLH(&view, &eye, &center, &up);
    D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
    UpdateModelVertexBuffers(&app, &model, &world._11);
    std::unique_ptr<OriginalModel> original;
    if (!exe.empty()) {
        for (int shader : {2, 3}) {
            const auto module = binary->Function<HMODULE>(0);
            const auto resource =
                FindResourceA(module, MAKEINTRESOURCEA(shader == 3 ? 118 : 117), RT_RCDATA);
            Check(resource != nullptr, "original standard effect resource");
            const auto data = LockResource(LoadResource(module, resource));
            const auto size = SizeofResource(module, resource);
            const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "res" / "assets" /
                              (shader == 3 ? "skin_effect_sm3.fx" : "skin_effect_sm2.fx");
            std::ifstream input(path, std::ios::binary);
            const std::vector<char> source((std::istreambuf_iterator<char>(input)), {});
            Check(size == source.size() && std::memcmp(data, source.data(), size) == 0,
                  "standard effect source differs from the original resource");
        }
        original = std::make_unique<OriginalModel>(*binary, f, modelPath, threads);
        SaveRenderModel(output, "original_loaded", *original->model);
        original->Pose();
        SaveRenderModel(output, "original_pose", *original->model);
        SameRenderModel(model, *original->model);
        original->model->toonFlag = 1;
        original->Skin(app, world);
        for (auto member : {&mdl::ModelRecord::vertexBuffer, &mdl::ModelRecord::vertexBuffer2}) {
            const auto port = Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(model.*member));
            const auto native =
                Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(original->model.get()->*member));
            const std::string kind = member == &mdl::ModelRecord::vertexBuffer ? ".main" : ".edge";
            SaveRenderBytes(output, "port" + kind, port.data(), port.size());
            SaveRenderBytes(output, "original" + kind, native.data(), native.size());
            Check(port == native, "skinned vertex buffer differs from original");
        }
    }
    if (!output.empty())
        std::filesystem::create_directories(output);
    std::ofstream report;
    if (!output.empty()) {
        report.open(output / "report.json");
        Check(report.is_open(), "effect report");
        report << "{\"architecture_bits\":" << sizeof(void*) * 8
               << ",\"original\":" << (exe.empty() ? "false" : "true")
               << ",\"frame\":0,\"physics\":false,\"threads\":" << threads
               << ",\"hardware_vp\":" << (hardwareVp ? "true" : "false")
               << ",\"without_mme\":" << (withoutMme ? "true" : "false") << ",\"cases\":[\n";
    }
    unsigned sequence = 0, failures = 0;
    CompositeScene composite(f);
    const float alpha = model.materials[0].diffuse[3];
    for (int shader : {3, 2}) {
        StandardEffect effect(*f.renderer, f.device, shader);
        std::unique_ptr<StandardEffect> oracleEffect;
        if (original)
            oracleEffect = std::make_unique<StandardEffect>(*original->renderer, f.device, shader);
        for (int scenario : {1, 2, 3, 4, 5, 6}) {
            if (selectedScenario && scenario != selectedScenario)
                continue;
            const int mode = scenario == 1 || scenario == 3 ? 1 : 2;
            app.ShadowMode() = mode;
            app.ResetSceneLight();
            if (scenario == 3 || scenario == 4) {
                const D3DVECTOR direction{1, -1, scenario == 3 ? -20.0f : 20.0f};
                app.SceneLight().Direction = direction;
                std::memcpy(app.LightDirection(), &direction, sizeof direction);
            }
            const bool combined = scenario >= 5;
            app.GroundGridEnabled() = app.GroundShadowEnabled() = combined;
            for (int i = 0; i < 2; ++i)
                app.AccessorySlot(i)->visible = combined;
            model.materials[0].diffuse[3] = scenario == 5 ? alpha * .6f : alpha;
            if (original)
                original->model->materials[0].diffuse[3] = model.materials[0].diffuse[3];
            const auto name = "sm" + std::to_string(shader) + "_case" + std::to_string(scenario);
            const auto port = Draw(f, effect, view, projection, world, output, name + "_port",
                                   nullptr, kSize, trace);
            if (scenario <= 2)
                Check(std::count_if(port.shadow.begin(), port.shadow.end(),
                                    [](DWORD bits) { return bits != 0x3F800000; }) > 0,
                      "baseline shadow map must contain model depth");
            auto repeat = Draw(f, effect, view, projection, world, output, name + "_repeat",
                               nullptr, kSize, trace);
            SameImage(port, repeat, "repeated effect frame differs");
            Difference color(port.color, repeat.color);
            ShadowDifference shadow(port.shadow, repeat.shadow);
            bool matrixEqual = true;
            if (original) {
                const auto reference = Draw(f, *oracleEffect, view, projection, world, output,
                                            name + "_original", original.get(), kSize, trace);
                color = Difference(port.color, reference.color);
                shadow = ShadowDifference(port.shadow, reference.shadow);
                matrixEqual = std::memcmp(&port.light, &reference.light, sizeof port.light) == 0;
                Check(port.constants == reference.constants && port.states == reference.states,
                      "draw parameters differ from original");
            }
            std::printf("compare %s: color=%u max=%u shadow=%u max=%u\n", name.c_str(),
                        color.pixels, color.maximum, shadow.pixels, shadow.maximumUlp);
            failures += color.pixels != 0 || shadow.pixels != 0 || !matrixEqual;
            if (report)
                report << (sequence++ ? ",\n" : "") << "{\"case\":\"" << name
                       << "\",\"color_differences\":" << color.pixels
                       << ",\"shadow_differences\":" << shadow.pixels
                       << ",\"max_shadow_ulp\":" << shadow.maximumUlp
                       << ",\"matrix_equal\":" << (matrixEqual ? "true" : "false")
                       << ",\"color_hash\":\"" << std::hex << Digest(port.color)
                       << "\",\"shadow_hash\":\"" << Digest(port.shadow) << "\"" << std::dec
                       << ",\"light_matrix\":[";
            if (report) {
                report.precision(9);
                for (int i = 0; i < 16; ++i)
                    report << (i ? "," : "") << reinterpret_cast<const float*>(&port.light)[i];
                report << "]}";
            }
        }
    }
    if (!withoutMme) {
        // The embedded MME host must preserve an unassigned scene, including a
        // populated shadow target, before and after production resource recovery.
        {
            StandardEffect effect(*f.renderer, f.device, 3);
            app.ModelNonDisplayMode() = 1;
            app.GroundGridEnabled() = app.GroundShadowEnabled() = 0;
            Draw(f, effect, view, projection, world, output, "accessories_only");
            app.ModelNonDisplayMode() = 0;
            app.GroundGridEnabled() = app.GroundShadowEnabled() = 1;
            const auto plain = Draw(f, effect, view, projection, world, output, "mme_disabled");
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
            const auto hosted = Draw(f, effect, view, projection, world, output, "mme_enabled");
            Check(MmeHostEffectsActive() != 0, "MME host initialized");
            SameImage(plain, hosted, "unassigned MME host changed the scene");
            const auto hostedRepeat =
                Draw(f, effect, view, projection, world, output, "mme_repeat");
            SameImage(hosted, hostedRepeat, "MME scene differs between consecutive frames");
            AssignOffscreenEffect(f);
            const auto assigned = Draw(f, effect, view, projection, world, output, "mme_offscreen");
            const auto offscreen = CheckOffscreen(f);
            Check(assigned.color != hosted.color, "assigned effect must change visible pixels");
            const auto assignedRepeat =
                Draw(f, effect, view, projection, world, output, "mme_offscreen_repeat");
            SameImage(assigned, assignedRepeat, "assigned effect must render consistently");
            VerifyReset(f, effect, view, projection, world, output);
            Check(offscreen == CheckOffscreen(f), "Reset changed offscreen scene pixels");
            Check(MmeHostEffectsActive() != 0, "MME host remains active after recovery");
        }
    }
    if (report)
        report << "\n],\"failures\":" << failures << "}\n";
    Check(dialogs == 0, "unexpected effect dialog");
    return failures ? 1 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    int result = 1;
    try {
        std::filesystem::path model, original, output;
        bool withoutMme = false, hardwareVp = false;
        int threads = 4, selectedScenario = 0;
        Check(argc % 2 == 1, "expected option/path pairs");
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring option(argv[i]);
            if (option == L"--model")
                model = std::filesystem::absolute(argv[i + 1]);
            else if (option == L"--original")
                original = std::filesystem::absolute(argv[i + 1]);
            else if (option == L"--output")
                output = std::filesystem::absolute(argv[i + 1]);
            else if (option == L"--threads")
                threads = _wtoi(argv[i + 1]);
            else if (option == L"--scenario")
                selectedScenario = _wtoi(argv[i + 1]);
            else if (option == L"--without-mme")
                withoutMme = std::wstring(argv[i + 1]) == L"1";
            else if (option == L"--hardware-vp")
                hardwareVp = std::wstring(argv[i + 1]) == L"1";
            else
                throw std::runtime_error("unknown effect option");
        }
        result =
            RunEffects(model, original, output, withoutMme, threads, selectedScenario, hardwareVp);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "effect scene: %s\n", error.what());
    }
    return result;
}
