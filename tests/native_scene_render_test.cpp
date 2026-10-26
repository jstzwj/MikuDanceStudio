#include "native_host_fixture.hpp"

namespace {
// Restrict the original hook installation to this mapped test image's IAT.
// The original loaders still perform all mesh/texture work and registration.
struct ReferenceImport {
    IMAGE_THUNK_DATA* slot = nullptr;
    ULONG_PTR previous = 0;
    ReferenceImport(OriginalPhysicsModule& image, const char* symbol, FARPROC replacement) {
        auto* base = image.Function<unsigned char*>(0);
        const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for (; descriptor->Name; ++descriptor) {
            auto* names =
                reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) &&
                    !std::strcmp(
                        reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)
                            ->Name,
                        symbol)) {
                    slot = slots;
                    previous = slot->u1.Function;
                    Write(reinterpret_cast<ULONG_PTR>(replacement));
                    return;
                }
            }
        }
        Check(false, "original render import not found");
    }
    void Write(ULONG_PTR address) {
        DWORD protection;
        Check(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection) != FALSE,
              "original render IAT protection");
        slot->u1.Function = address;
        VirtualProtect(slot, sizeof(*slot), protection, &protection);
    }
    void Restore() {
        if (slot)
            Write(previous);
        slot = nullptr;
    }
    ~ReferenceImport() { Restore(); }
};

struct NativeAccessories {
    OriginalModel& original;
    std::array<std::unique_ptr<mdl::AccessoryRecord>, 2> objects;
    explicit NativeAccessories(OriginalModel& value) : original(value) {}
    void Free(void* memory) {
        if (memory)
            original.module.Function<void(__cdecl*)(void*)>(sizeof(void*) == 8 ? 0x11A39C
                                                                               : 0x104E31)(memory);
    }
    void ClearRecord(mdl::AccessoryRecord& a) {
        if (a.mesh)
            static_cast<ID3DXMesh*>(a.mesh)->Release();
        Free(a.materials);
        Free(a.textureTypes);
        Free(a.texturePaths);
        a = {};
    }
    void Clear(int index) {
        if (objects[index])
            ClearRecord(*objects[index]);
        objects[index].reset();
    }
    void Load(Fixture& f, int index, const std::filesystem::path& path) {
        objects[index] = std::make_unique<mdl::AccessoryRecord>();
        auto& a = *objects[index];
        InitAccessoryRecord(&a);
        std::array<wchar_t, 32768> paths{};
        const auto cwd = std::filesystem::current_path();
#if defined(_M_X64)
        using Load =
            bool (*)(mdl::AccessoryRecord*, HWND, D3DRenderer*, const wchar_t*, char, void*);
        const auto address = 0xDC560;
#else
        using Load = bool(__thiscall*)(mdl::AccessoryRecord*, HWND, D3DRenderer*, const wchar_t*,
                                       char, void*);
        const auto address = 0xC5F40;
#endif
        const bool loaded = original.module.Function<Load>(address)(
            &a, f.window, original.renderer.get(), path.c_str(), 1, paths.data());
        std::filesystem::current_path(cwd);
        Check(loaded, "native accessory loader");
    }
    ~NativeAccessories() {
        for (int i = 0; i < 2; ++i)
            Clear(i);
        for (auto& resource : original.renderer->resourcePool) {
            if (resource.comObject)
                resource.comObject->Release();
            Free(resource.heapBuffer);
            resource = {};
        }
    }
};

void CopyAccessorySettings(mdl::AccessoryRecord& target, const mdl::AccessoryRecord& source) {
    auto* mesh = target.mesh;
    auto* materials = target.materials;
    auto* paths = target.texturePaths;
    auto* types = target.textureTypes;
    target = source;
    target.mesh = mesh;
    target.materials = materials;
    target.texturePaths = paths;
    target.textureTypes = types;
}

void WriteSceneMesh(Fixture& f, const std::filesystem::path& path) {
    Com<IDirect3DTexture9> texture;
    Hr(f.device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture.Out(),
                               nullptr),
       "accessory fixture texture");
    D3DLOCKED_RECT lock{};
    Hr(texture->LockRect(0, &lock, nullptr, 0), "fixture texture lock");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits) + y * lock.Pitch)[x] =
                (x + y) % 2 ? 0xFF40D080 : 0xFFB060C0;
    Hr(texture->UnlockRect(0), "fixture texture unlock");
    Com<IDirect3DSurface9> surface;
    Hr(texture->GetSurfaceLevel(0, surface.Out()), "fixture texture surface");
    for (const wchar_t* name : {L"sample.png", L"multiply.sph", L"add.spa"})
        Hr(D3DXSaveSurfaceToFileW((f.directory / name).c_str(), kPngFormat, surface.p, nullptr,
                                  nullptr),
           "save accessory texture");
    std::ofstream(path) << R"X(xof 0303txt 0032
Mesh {6;
 0.0;0.0;0.0;,4.0;0.0;0.0;,0.0;4.0;0.0;,
 4.0;0.0;0.0;,8.0;0.0;0.0;,4.0;4.0;0.0;;
 2;3;0,2,1;,3;3,5,4;;
 MeshNormals {1;0.0;0.0;-1.0;;2;3;0,0,0;,3;0,0,0;;}
 MeshTextureCoords {6;0.0;0.0;,1.0;0.0;,0.0;1.0;,0.0;0.0;,1.0;0.0;,0.0;1.0;;}
 MeshMaterialList {2;2;0,1;;
  Material {1.0;0.3;0.1;1.0;;4.0;0.0;0.0;0.0;;0.1;0.1;0.1;;
   TextureFilename {"sample.png*multiply.sph";}}
  Material {0.2;0.7;0.5;0.4;;8.0;0.1;0.2;0.3;;0.1;0.1;0.1;;
   TextureFilename {"sample.png*add.spa";}}
 }
})X";
}
void SceneMapping(Fixture& f, NativeMmeOracle* engine, const std::filesystem::path& effect) {
    const auto path = f.directory / "scene.emm";
    std::ofstream file(path);
    file << "[Info]\nVersion=3\n[Object]\nPmd0=" << ExpGetPmdFilename(0);
    for (int i = 0; i < 2; ++i)
        if (f.app->AccessorySlot(i))
            file << "\nAcs" << i << '=' << ExpGetAcsFilename(i);
    file << "\n[Effect]\nDefault=none\nPmd0=" << (effect.empty() ? "none" : effect.string());
    for (int i = 0; i < 2; ++i)
        if (f.app->AccessorySlot(i))
            file << "\nAcs" << i << '=' << (effect.empty() ? "none" : effect.string());
    file << '\n';
    file.close();
    if (engine)
        engine->LoadMapping(path, f.window);
    else
        Check(::mme::MmeEmmLoad(path.string().c_str()), "scene EMM load");
}

int RunScene(const std::filesystem::path& executable, const std::filesystem::path& hack,
             const std::filesystem::path& enginePath, const std::filesystem::path& output) {
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "native scene caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF || (caps.PixelShaderVersion >> 8 & 255) < 3)
        return 77;
    const auto rawDevice = f.device;
    RenderNumericTrace trace(rawDevice);
    std::filesystem::create_directories(output);
    trace.shaderDirectory = output / "shaders";
    std::filesystem::create_directories(trace.shaderDirectory);
    OriginalPhysicsModule image(executable);
    NativeMmdQueries queries(image);
    auto mmh = std::make_unique<NativeMmhOracle>(hack, queries);
    NativeMmeOracle engine(enginePath, [&](const char* library, const char* symbol) {
        return _stricmp(library, "MMHack.dll") == 0 ? mmh->Export<FARPROC>(symbol)
                                                    : queries.Export(symbol);
    });
    BindEngine(*mmh, static_cast<HMODULE>(engine.Module()), f.window);
    auto* wrapped = mmh->Device(&trace);
    NativeHostDevice device(f, queries, wrapped);
    device.real = &trace;
    f.device = f.renderer->device = &device;
    struct Restore {
        Fixture& f;
        IDirect3DDevice9* device;
        mdl::ModelRecord* model = nullptr;
        std::array<mdl::AccessoryRecord*, 2> accessories{};
        ~Restore() {
            f.device = f.renderer->device = device;
            if (model)
                f.app->ModelSlot(0) = model;
            for (int i = 0; i < 2; ++i)
                f.app->AccessorySlot(i) = accessories[i];
        }
    } restore{f, rawDevice};
    auto& model = f.LoadModel(0, 1);
    restore.model = &model;
    SharedToons toons(f);
    auto& app = *f.app;
    Check(MakeLineGeometry(&app), "scene ground geometry");
    struct GroundLifetime {
        MMDApp& app;
        ~GroundLifetime() {
            for (auto** slot : {&app.GroundGridVertices(), &app.GroundPlaneVertices()}) {
                if (*slot)
                    (*slot)->Release();
                *slot = nullptr;
            }
            if (app.GroundGridIndices())
                app.GroundGridIndices()->Release();
            app.GroundGridIndices() = nullptr;
        }
    } groundLifetime{app};
    app.ResetSceneLight();
    app.CameraMode() = app.PlaybackActive() = 1;
    app.CameraFov() = 30;
    app.ShadowDistance() = .01125f;
    app.ShadowMode() = 2;
    app.CameraDistance() = 0;
    app.ViewOffsetX() = 1;
    app.ViewOffsetY() = .5f;
    app.state.selfShadowEnabled = 1;
    app.GroundShadowEnabled() = 1;
    EnableWindow(GetDlgItem(f.window, panel::kCurrentFrameEdit), FALSE);
    D3DXMATRIX world, view, projection;
    D3DXMatrixIdentity(&world);
    std::memcpy(&app.ViewRotationTransform(), &world, sizeof world);
    const D3DXVECTOR3 eye(1, .5f, -5), center(1, .5f, 0), up(0, 1, 0);
    std::memcpy(app.CameraPosition(), &eye, sizeof eye);
    D3DXMatrixLookAtLH(&view, &eye, &center, &up);
    D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
    SeekModelFrame(model, 0, 0);
    ModelApplyMorphs(&model);
    SetPhysicsMode(&model, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&model, 1, app.ModelSlots(), 0);
    model.toonFlag = 1;
    UpdateModelVertexBuffers(&app, &model, &world._11);
    OriginalModel original(image, f, model.path);
    original.Pose();
    original.model->comboSelIndex = original.model->comboSelIndex2 = 1;
    original.model->toonFlag = 1;
    original.Skin(app, world);
    SameRenderModel(model, *original.model);
    mmh->RegisterModel(*original.model);
    StandardEffect portEffect(*f.renderer, &device, 3);
    StandardEffect nativeEffect(*original.renderer, &device, 3);
    supplied = nativeEffect.effect.p;
    mmh->ReplaceImport("D3DXCreateEffectFromResourceA",
                       reinterpret_cast<FARPROC>(SupplyStandardEffect));
    using Create =
        HRESULT(WINAPI*)(IDirect3DDevice9*, HMODULE, const char*, const D3DXMACRO*, ID3DXInclude*,
                         DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    Com<ID3DXEffect> registered;
    Hr(mmh->At<Create>(sizeof(void*) == 8 ? 0x4440 : 0x40D0)(
           wrapped, nullptr, nullptr, nullptr, nullptr, 0, nullptr, registered.Out(), nullptr),
       "scene standard FX registration");
    struct NativeLifetime {
        std::unique_ptr<NativeMmhOracle>& mmh;
        ~NativeLifetime() { mmh.reset(); }
    } nativeLifetime{mmh};
    NativeAccessories accessories(original);
    ReferenceImport meshHook(image, "D3DXLoadMeshFromXW",
                             mmh->At<FARPROC>(sizeof(void*) == 8 ? 0x43B0 : 0x4050));
    ReferenceImport textureHook(image, "D3DXCreateTextureFromFileExW",
                                mmh->At<FARPROC>(sizeof(void*) == 8 ? 0x4670 : 0x4240));
    const auto meshPath = f.directory / "multi.x";
    WriteSceneMesh(f, meshPath);
    for (int i = 0; i < 2; ++i) {
        // Repeated instances of one .x file must consume distinct EMM rows.
        const auto objectPath = meshPath;
        auto* a = static_cast<mdl::AccessoryRecord*>(::operator new(sizeof(mdl::AccessoryRecord)));
        *a = {};
        InitAccessoryRecord(a);
        app.AccessorySlot(i) = restore.accessories[i] = a;
        const auto cwd = std::filesystem::current_path();
        Check(LoadAccessoryObject(&app, a, objectPath.c_str()), "port multi-material accessory");
        std::filesystem::current_path(cwd);
        a->order = static_cast<unsigned char>(i);
        a->position[0] = i ? .8f : -.2f;
        a->position[1] = .15f;
        a->position[2] = -1;
        a->scale = .06f;
        a->opacity = i ? .4f : 1;
        a->visible = a->shadowEnabled = 1;
        accessories.Load(f, i, objectPath);
        auto& n = *accessories.objects[i];
        Check(n.materialCount == 2 && a->materialCount == 2, "two accessory subsets");
        Check(std::memcmp(a->materials, n.materials, 2 * sizeof(D3DMATERIAL9)) == 0 &&
                  std::memcmp(a->textureTypes, n.textureTypes, 2) == 0,
              "native accessory parsed materials/textures");
        CopyAccessorySettings(n, *a);
        mmh->RegisterObject(&n.mesh, n.sourcePath);
    }
    app.AccessoryRenderSplitOrder() = 1;
#if defined(_M_IX86)
    unsigned control;
    _controlfp_s(&control, _PC_24, _MCW_PC);
#endif
    std::map<std::string, Image> images;
    std::map<std::string, std::vector<std::array<DWORD, 16>>> calls;
    bool native = true;
    bool secondVisible = true;
    auto select = [&] {
        app.ModelSlot(0) = native ? original.model.get() : &model;
        for (int i = 0; i < 2; ++i)
            app.AccessorySlot(i) = i == 1 && !secondVisible ? nullptr
                                   : native                 ? accessories.objects[i].get()
                                                            : restore.accessories[i];
    };
    auto draw = [&](const std::string& stage, UINT size = kSize) {
        select();
        const auto name = std::string(native ? "native_" : "port_") + stage;
        images[name] = Draw(
            f, native ? nativeEffect : portEffect, view, projection, world, output, name,
            native ? &original : nullptr, size, &trace,
            native ? std::function<void(MMDAppState*)>([&](MMDAppState* s) { queries.Bind(s); })
                   : std::function<void(MMDAppState*)>());
        std::ifstream input(output / (name + ".draws"), std::ios::binary);
        Check(input.good(), "scene draw capture opened");
        std::array<DWORD, 272> row;
        while (input.read(reinterpret_cast<char*>(row.data()), sizeof row)) {
            std::array<DWORD, 16> call;
            std::copy_n(row.begin(), call.size(), call.begin());
            calls[name].push_back(call);
        }
        Check(input.eof() && input.gcount() == 0 && !calls[name].empty(),
              "complete nonempty scene draw capture");
    };
    const auto simple = f.directory / "scene.fx";
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
    const auto replacement = f.directory / "replacement.fx";
    {
        std::ifstream input(simple);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        const auto offset = text.find("0.2,0.7,0.3");
        Check(offset != std::string::npos, "scene replacement effect");
        text.replace(offset, std::strlen("0.2,0.7,0.3"), "0.8,0.1,0.2");
        std::ofstream(replacement) << text;
    }
    auto mapping = [&](const std::filesystem::path& path) {
        select();
        if (native)
            queries.Sync(app);
        SceneMapping(f, native ? &engine : nullptr, path);
    };
    auto stages = [&] {
        secondVisible = true;
        for (int i = 0; i < 2; ++i) {
            auto& a = native ? *accessories.objects[i] : *restore.accessories[i];
            a.parentModel = -1;
            a.parentBone = 0;
        }
        draw("unassigned");
        mapping(simple);
        draw("assigned");
        Check(images.at(native ? "native_assigned" : "port_assigned").color !=
                  images.at(native ? "native_unassigned" : "port_unassigned").color,
              "assigned scene effect must change pixels");
        mapping(replacement);
        draw("replaced");
        mapping(simple);
        auto& attached = native ? *accessories.objects[1] : *restore.accessories[1];
        attached.parentModel = 0;
        attached.parentBone = 1;
        draw("bound");
        secondVisible = false;
        draw("unloaded");
        if (native) {
            auto previous = std::move(accessories.objects[1]);
            accessories.Load(f, 1, meshPath);
            CopyAccessorySettings(*accessories.objects[1], *previous);
            Check(accessories.objects[1].get() != previous.get(),
                  "native reloaded accessory identity");
            mmh->RegisterObject(&accessories.objects[1]->mesh, accessories.objects[1]->sourcePath);
            accessories.ClearRecord(*previous);
        } else {
            auto* previous = restore.accessories[1];
            auto* loaded =
                static_cast<mdl::AccessoryRecord*>(::operator new(sizeof(mdl::AccessoryRecord)));
            *loaded = {};
            InitAccessoryRecord(loaded);
            const auto cwd = std::filesystem::current_path();
            Check(LoadAccessoryObject(&app, loaded, meshPath.c_str()), "reloaded port accessory");
            std::filesystem::current_path(cwd);
            CopyAccessorySettings(*loaded, *previous);
            restore.accessories[1] = loaded;
            DisposeAccessory(previous);
            ::operator delete(previous);
        }
        secondVisible = true;
        draw("reload_before_assignment");
        mapping(simple);
        draw("reloaded");
        draw("resized", 384);
        mapping({});
        draw("detached");
        nativeEffect.ReleaseTargets();
        portEffect.ReleaseTargets();
        Hr(nativeEffect.effect->OnLostDevice(), "native standard FX lost");
        Hr(portEffect.effect->OnLostDevice(), "port standard FX lost");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = f.window;
        pp.BackBufferWidth = pp.BackBufferHeight = 900;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        if (native)
            queries.Sync(app);
        else
            MmeHostOnLostDevice(&device);
        Hr(device.Reset(&pp), "scene real Reset");
        if (!native)
            MmeHostOnResetDevice(&device);
        Hr(nativeEffect.effect->OnResetDevice(), "native standard FX reset");
        Hr(portEffect.effect->OnResetDevice(), "port standard FX reset");
        draw("reset");
    };
    device.enabled = true;
    device.synchronizeQueries = false;
    stages();
    // Flush deletions before releasing original mesh wrappers or their DLL.
    app.ModelSlot(0) = nullptr;
    app.AccessorySlot(0) = app.AccessorySlot(1) = nullptr;
    app.ActiveRenderObject() = nullptr;
    queries.Sync(app);
    Hr(wrapped->BeginScene(), "empty native scene begin");
    Hr(wrapped->EndScene(), "empty native scene end");
    for (int i = 0; i < 2; ++i)
        accessories.Clear(i);
    textureHook.Restore();
    meshHook.Restore();
    mmh.reset();
    device.enabled = device.trackCreation = false;
    native = false;
    select();
    MmeHostInitializeRuntime(GetModuleHandleW(nullptr));
    mikudancestudio::mme::OnDeviceCreated(&app, f.window);
    struct PortLifetime {
        MMDApp& app;
        ~PortLifetime() {
            mikudancestudio::mme::OnDeviceDestroyed(&app);
            MmeHostShutdownRuntime();
        }
    } portLifetime{app};
    stages();
    std::ofstream report(output / "report.json");
    report
        << "{\"architecture_bits\":" << sizeof(void*) * 8
        << ",\"native_draw_source\":\"original MMD\",\"native_mesh_loader\":\"original MMD + MMHack\",\"pairs\":[";
    bool comma = false;
    for (const char* stage :
         {"unassigned", "assigned", "replaced", "bound", "unloaded", "reload_before_assignment",
          "reloaded", "resized", "detached", "reset"}) {
        const auto& n = images.at(std::string("native_") + stage);
        const auto& p = images.at(std::string("port_") + stage);
        const Difference color(n.color, p.color);
        const ShadowDifference shadow(n.shadow, p.shadow);
        std::printf("compare %s: color=%u shadow=%u states=%d constants=%d\n", stage, color.pixels,
                    shadow.pixels, n.states == p.states, n.constants == p.constants);
        SameImage(n, p, "original draw/MMHack/MMEffect scene image");
        Check(n.states == p.states && n.constants == p.constants &&
                  calls.at(std::string("native_") + stage) ==
                      calls.at(std::string("port_") + stage),
              "original native scene draw state/geometry");
        report << (comma ? "," : "") << "{\"stage\":\"" << stage
               << "\",\"color_differences\":" << color.pixels
               << ",\"shadow_differences\":" << shadow.pixels
               << ",\"draws\":" << calls.at(std::string("native_") + stage).size()
               << ",\"matrix_equal\":true,\"draw_state_equal\":true,"
                  "\"geometry_equal\":true,\"shader_constants_equal\":true}";
        comma = true;
    }
    report << "],\"failures\":0}\n";
    Check(report.good(), "native scene parity report");
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        Check(argc == 9 && std::wstring(argv[1]) == L"--original" &&
                  std::wstring(argv[3]) == L"--mmhack" && std::wstring(argv[5]) == L"--mme" &&
                  std::wstring(argv[7]) == L"--output",
              "expected native scene arguments");
        return RunScene(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[4]),
                        std::filesystem::absolute(argv[6]), std::filesystem::absolute(argv[8]));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
