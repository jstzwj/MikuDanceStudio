#include <omp.h>
#include "scene_render_fixture.hpp"

namespace {
int Run(const std::filesystem::path& modelPath, const std::filesystem::path& original,
        const std::filesystem::path& output, bool numericOnly, int threads) {
    Check(threads >= 1 && threads <= 64, "render thread count 1..64");
    omp_set_dynamic(0);
    omp_set_num_threads(threads);
    std::printf("port OpenMP workers: %d\n", threads);
    std::unique_ptr<OriginalPhysicsModule> binary;
    if (!original.empty())
        binary = std::make_unique<OriginalPhysicsModule>(original);
    const HHOOK hook =
        SetWindowsHookExW(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    struct HookLifetime {
        HHOOK hook;
        ~HookLifetime() { UnhookWindowsHookEx(hook); }
    } hookLifetime{hook};
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "real device caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF)
        return 77;
    D3DADAPTER_IDENTIFIER9 adapter{};
    Hr(f.d3d->GetAdapterIdentifier(0, 0, &adapter), "adapter identity");
    std::printf("GPU: %s; device type %u\n", adapter.Description, unsigned(caps.DeviceType));
    auto path = modelPath;
    if (path.empty()) {
        path = f.directory / L"render.pmd";
        WriteModel(path);
    }
    auto& model = f.LoadModelFile(path, 0, 0);
    SaveRenderModel(output, "port_loaded", model);
    std::printf("model: %u vertices, %u materials, %u bones, UV=%u\n", model.vertexCount,
                model.materialCount, model.boneCount, unsigned(model.pmxAdditionalUvCount));
    auto& app = *f.app;
    app.ResetSceneLight();
    app.CameraFov() = 30;
    app.state.selfShadowEnabled = 0;
    f.renderer->postProcessEnabled = 0; // Exercise the fixed-function material/edge path.
    f.renderer->shaderModelCaps = caps.MaxAnisotropy;
    app.SetSelectedModelSlot(0);
    SeekModelFrame(model, 0, 0);
    ModelApplyMorphs(&model);
    SetPhysicsMode(&model, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&model, 1, app.ModelSlots(), 0);
    SaveRenderModel(output, "port_pose", model);
    SharedToons toons(f);
    std::unique_ptr<OriginalModel> reference;
    if (!original.empty()) {
        reference = std::make_unique<OriginalModel>(*binary, f, path, threads);
        SaveRenderModel(output, "original_loaded", *reference->model);
        reference->Pose();
        SaveRenderModel(output, "original_pose", *reference->model);
        SameRenderModel(model, *reference->model);
    }
    if (!output.empty())
        std::filesystem::create_directories(output);
    std::ofstream report;
    if (!output.empty()) {
        report.open(output / "report.json");
        Check(report.is_open(), "open image report");
        report << "{\"width\":512,\"height\":512,\"architecture_bits\":" << sizeof(void*) * 8
               << ",\"pipeline\":\"fixed_function\",\"frame\":0,\"physics\":false,\"original\":"
               << (original.empty() ? "false" : "true") << ",\"vertices\":" << model.vertexCount
               << ",\"materials\":" << model.materialCount << ",\"threads\":" << threads
               << ",\"numeric_only\":" << (numericOnly ? "true" : "false") << ",\"cases\":[\n";
    }
    unsigned failures = 0, sequence = 0;
    const bool pmx = model.physicsMode == 2;
    const D3DXVECTOR3 center = pmx ? D3DXVECTOR3(0, 10, 0) : D3DXVECTOR3(1, .5f, 0);
    const float distance = pmx ? 45.0f : 5.0f;
    D3DXMATRIX world;
    D3DXMatrixIdentity(&world);
    for (int view : {0, 1}) {
        const D3DXVECTOR3 eye(center.x + view * distance * .35f, center.y + view * distance * .1f,
                              -distance);
        const D3DXVECTOR3 up(0, 1, 0);
        D3DXMATRIX camera, projection;
        D3DXMatrixLookAtLH(&camera, &eye, &center, &up);
        D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
        std::memcpy(app.CameraPosition(), &eye, sizeof eye);
        for (float scale : {0.0f, 1.0f, 2.0f}) {
            model.edgeScale = scale;
            UpdateModelVertexBuffers(&app, &model, &world._11);
            if (reference) {
                reference->model->edgeScale = scale;
                reference->Skin(app, world);
                const bool mainEqual =
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer)) ==
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(reference->model->vertexBuffer));
                const bool edgeEqual =
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer2)) ==
                    Buffer(
                        mdl::ResourceAs<IDirect3DVertexBuffer9>(reference->model->vertexBuffer2));
                std::printf("vertices view=%d edge=%.0f: main=%d edge=%d\n", view, scale, mainEqual,
                            edgeEqual);
                failures += !mainEqual || !edgeEqual;
                for (auto item : {std::make_pair("port", &model),
                                  std::make_pair("original", reference->model.get())}) {
                    const auto main =
                        Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(item.second->vertexBuffer));
                    const auto edge =
                        Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(item.second->vertexBuffer2));
                    const auto name = "view" + std::to_string(view) + "_edge" +
                                      std::to_string(int(scale)) + "_" + item.first;
                    SaveRenderBytes(output, name + ".main", main.data(), main.size());
                    SaveRenderBytes(output, name + ".edge", edge.data(), edge.size());
                }
            }
            if (numericOnly) {
                if (report)
                    report << (sequence++ ? ",\n" : "") << "{\"view\":" << view
                           << ",\"edge_scale\":" << scale << '}';
                continue;
            }
            std::vector<DWORD> baseline;
            // Recreate default-pool surfaces across Reset; managed model/Toon buffers stay live.
            for (int pass = 0; pass < 3; ++pass) {
                const bool oracle = pass == 1 && reference != nullptr;
                if (pass == 1 && !oracle)
                    continue;
                if (pass == 2) {
                    D3DPRESENT_PARAMETERS pp{};
                    pp.Windowed = TRUE;
                    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
                    pp.hDeviceWindow = f.window;
                    pp.BackBufferWidth = pp.BackBufferHeight = 64;
                    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
                    f.device->SetStreamSource(0, nullptr, 0, 0);
                    f.device->SetIndices(nullptr);
                    for (DWORD stage = 0; stage < 3; ++stage)
                        f.device->SetTexture(stage, nullptr);
                    Hr(f.device->Reset(&pp), "Reset with live model and textures");
                }
                RenderTarget target(f.device);
                InitRenderStates(&app);
                Hr(f.device->SetTransform(D3DTS_WORLD, &world), "world");
                Hr(f.device->SetTransform(D3DTS_VIEW, &camera), "view");
                Hr(f.device->SetTransform(D3DTS_PROJECTION, &projection), "projection");
                Hr(f.device->SetLight(0, &app.SceneLight()), "light");
                Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW), "cull");
                Hr(f.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "alpha test");
                Hr(f.device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER), "alpha function");
                Hr(f.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, kBackground, 1,
                                   0),
                   "clear");
                Hr(f.device->BeginScene(), "begin image");
                if (oracle)
                    reference->Materials(app);
                else
                    DrawModelMaterials(&app, &model, false, false);
                if (scale > 0) {
                    Hr(f.device->SetRenderState(D3DRS_LIGHTING, FALSE), "edge lighting");
                    Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "edge cull");
                    Hr(f.device->SetTexture(0, nullptr), "edge texture");
                    if (oracle)
                        reference->Edges(false);
                    else
                        DrawModelEdgeGeometry(&app, &model, false);
                }
                Hr(f.device->EndScene(), "end image");
                const auto name = "view" + std::to_string(view) + "_edge" +
                                  std::to_string(int(scale)) +
                                  (oracle      ? "_original"
                                   : pass == 2 ? "_reset"
                                               : "_port");
                const auto pixels = target.Pixels(output.empty() ? std::filesystem::path()
                                                                 : output / (name + ".png"));
                const auto drawn = std::count_if(pixels.begin(), pixels.end(),
                                                 [](DWORD value) { return value != kBackground; });
                Check(drawn > 100, "rendered image must contain model pixels");
                if (pass == 0)
                    baseline = pixels;
                const Difference difference(baseline, pixels);
                failures += difference.pixels != 0;
                std::printf("%s: colored=%zu hash=%016llX different=%u max=%u\n", name.c_str(),
                            std::size_t(drawn), Digest(pixels), difference.pixels,
                            difference.maximum);
                if (report) {
                    report << (sequence++ ? ",\n" : "") << "{\"case\":\"" << name
                           << "\",\"drawn_pixels\":" << drawn
                           << ",\"different_pixels\":" << difference.pixels
                           << ",\"max_channel_error\":" << difference.maximum << "}";
                }
                std::fflush(stdout);
            }
        }
    }
    if (report)
        report << "\n],\"failures\":" << failures << "}\n";
    for (int i = 0; i < 11; ++i)
        app.ToonTexture(i) = nullptr;
    Check(dialogs == 0, "unexpected model/render dialog");
    return failures ? 1 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    int result = 1;
    try {
        std::filesystem::path model, original, output;
        bool numericOnly = false;
        int threads = 4;
        Check(argc % 2 == 1, "expected --model/--original/--output path pairs");
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
            else if (option == L"--numeric-only")
                numericOnly = std::wstring(argv[i + 1]) == L"1";
            else
                throw std::runtime_error("unknown render test option");
        }
        result = Run(model, original, output, numericOnly, threads);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "scene render: %s\n", error.what());
    }
    return result;
}
