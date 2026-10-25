#pragma once

#include "scene_memory_device.hpp"

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_ownership.hpp"

#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace mikudancestudio;
namespace {
std::filesystem::path referenceDirectory;
bool crossCheck = false;
bool complexScene = false;
int dialogs = 0;
LRESULT CALLBACK CloseUnexpectedDialog(int code, WPARAM window, LPARAM data) {
    if (code == HCBT_ACTIVATE) {
        char name[32]{};
        GetClassNameA(reinterpret_cast<HWND>(window), name, sizeof name);
        if (std::strcmp(name, "#32770") == 0) {
            char text[256]{};
            GetWindowTextA(reinterpret_cast<HWND>(window), text, sizeof text);
            std::fprintf(stderr, "unexpected dialog: %s\n", text);
            ++dialogs;
            SendMessageA(reinterpret_cast<HWND>(window), WM_COMMAND, IDOK, 0);
        }
    }
    return CallNextHookEx(nullptr, code, window, data);
}
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Near(float actual, float expected, const char* message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::fabs(actual - expected) > 0.00002f) {
        std::fprintf(stderr, "%s: actual %.9g, expected %.9g\n", message, actual, expected);
        throw std::runtime_error(message);
    }
}
struct Bytes {
    std::vector<unsigned char> data;
    void U(unsigned value, int width = 4) {
        for (int i = 0; i < width; ++i)
            data.push_back(static_cast<unsigned char>(value >> (8 * i)));
    }
    void F(float value) {
        unsigned bits;
        std::memcpy(&bits, &value, 4);
        U(bits);
    }
    void Text(const char* text, std::size_t width) {
        const auto size = std::strlen(text);
        for (std::size_t i = 0; i < width; ++i)
            data.push_back(i < size ? text[i] : 0);
    }
    void Save(const std::filesystem::path& path) const {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        Check(out.good(), "write fixture");
    }
};
// Independent disk fixture: a triangle, a root/child pair, and one vertex morph.
// No runtime structure size determines a file field width.
void WriteModel(const std::filesystem::path& path) {
    Bytes b;
    b.Text("Pmd", 3);
    b.F(1);
    b.Text("loop", 20);
    b.Text("scene roundtrip", 256);
    b.U(3);
    for (int i = 0; i < 3; ++i) {
        b.F(float(i));
        b.F(float(i == 1));
        b.F(0);
        b.F(0);
        b.F(1);
        b.F(0);
        b.F(0);
        b.F(0);
        b.U(i == 0 ? 0 : 1, 2);
        b.U(0, 2);
        b.U(100, 1);
        b.U(0, 1);
    }
    b.U(3);
    b.U(0, 2);
    b.U(1, 2);
    b.U(2, 2);
    b.U(1);
    for (int i = 0; i < 4; ++i)
        b.F(1);
    for (int i = 0; i < 7; ++i)
        b.F(0);
    b.U(255, 1);
    b.U(0, 1);
    b.U(3);
    b.Text("", 20);
    b.U(2, 2);
    for (int i = 0; i < 2; ++i) {
        b.Text(i ? "child" : "root", 20);
        b.U(i ? 0 : 65535, 2);
        b.U(i ? 65535 : 1, 2);
        b.U(1, 1);
        b.U(0, 2);
        b.F(0);
        b.F(float(i));
        b.F(0);
    }
    b.U(0, 2); // IK chains
    b.U(2, 2);
    for (int i = 0; i < 2; ++i) {
        b.Text(i ? "smile" : "base", 20);
        b.U(1);
        b.U(i ? 1 : 0, 1);
        b.U(i ? 0 : 1);
        b.F(i ? .25f : 1);
        b.F(i ? .5f : 1);
        b.F(0);
    }
    b.U(1, 1);
    b.U(1, 2); // displayed morph
    b.U(1, 1);
    b.Text("body", 50); // custom group
    b.U(2);
    b.U(0, 2);
    b.U(1, 1);
    b.U(1, 2);
    b.U(1, 1);
    b.U(0, 1); // no English extension
    b.Text("", 1000);
    b.U(0);
    b.U(0); // Toon names, rigid bodies, joints
    b.Save(path);
}

// Independent PMX 2.0 fixture: a bent two-link leg, an IK controller,
// fixed/local axes, append rotation/translation, and a group morph.
void WriteComplexModel(const std::filesystem::path& path, bool withPhysics = false) {
    Bytes b;
    const auto text = [&](const wchar_t* value) {
        b.U(static_cast<unsigned>(wcslen(value) * 2));
        for (const wchar_t* p = value; *p; ++p)
            b.U(*p, 2);
    };
    const auto vector = [&](float x, float y, float z) {
        b.F(x);
        b.F(y);
        b.F(z);
    };
    b.Text("PMX ", 4);
    b.F(2);
    b.U(8, 1);
    for (int value : {0, 0, 1, 1, 1, 1, 1, 1})
        b.U(value, 1); // UTF-16, no extra UV, one-byte indices
    text(L"complex");
    text(L"complex");
    text(L"generated IK cross-load fixture");
    text(L"");
    b.U(3);
    for (int i = 0; i < 3; ++i) {
        vector(float(i), 2, 0);
        vector(0, 1, 0);
        b.F(0);
        b.F(0);
        b.U(0, 1); // BDEF1
        b.U(2, 1); // knee
        b.F(1);
    }
    b.U(3);
    for (int index : {0, 1, 2})
        b.U(index, 1);
    b.U(0); // textures
    b.U(1); // materials
    text(L"surface");
    text(L"");
    for (int i = 0; i < 4; ++i)
        b.F(1);
    vector(0, 0, 0);
    b.F(0);
    vector(.5f, .5f, .5f);
    b.U(0x1F, 1);
    for (int i = 0; i < 4; ++i)
        b.F(i == 3 ? 1.0f : 0.0f);
    b.F(1);
    b.U(255, 1);
    b.U(255, 1); // no texture/sphere
    b.U(0, 1);
    b.U(0, 1);
    b.U(255, 1); // no Toon texture
    text(L"");
    b.U(3);
    b.U(7); // bones
    const wchar_t* names[] = {L"root", L"hip", L"knee", L"ankle", L"legIK", L"axis", L"append"};
    const int parents[] = {-1, 0, 1, 2, 0, 0, 0};
    const float positions[7][3] = {{0, 0, 0}, {0, 4, 0}, {0, 2, .25f}, {0, 0, 0},
                                   {0, 0, 0}, {2, 4, 0}, {4, 4, 0}};
    const unsigned flags[] = {0x1E, 0x1A, 0x1A, 0x1A, 0x3E, 0xC1E, 0x31E};
    for (int i = 0; i < 7; ++i) {
        text(names[i]);
        text(names[i]);
        vector(positions[i][0], positions[i][1], positions[i][2]);
        b.U(parents[i], 1);
        b.U(i == 6 ? 1 : 0); // append runs after its source
        b.U(flags[i], 2);
        vector(0, 1, 0); // tail offset
        if (i == 6) {
            b.U(5, 1);
            b.F(.5f);
        }
        if (i == 5) {
            vector(0, 1, 0); // fixed axis
            vector(0, 0, 1); // local X
            vector(1, 0, 0); // local Z
        }
        if (i == 4) {
            b.U(3, 1);
            b.U(16);
            b.F(.5f);
            b.U(2);
            b.U(2, 1);
            b.U(1, 1);
            vector(-3.141592f, 0, 0);
            vector(-.001f, 0, 0);
            b.U(1, 1);
            b.U(0, 1);
        }
    }
    b.U(3); // vertex, bone and group morphs
    for (int i = 0; i < 3; ++i) {
        const wchar_t* name = i == 0 ? L"vertex" : (i == 1 ? L"bone" : L"group");
        text(name);
        text(name);
        b.U(4, 1);
        b.U(i == 0 ? 1 : (i == 1 ? 2 : 0), 1);
        b.U(i == 2 ? 2 : 1);
        if (i == 0) {
            b.U(1, 1);
            vector(.25f, .5f, 0);
        } else if (i == 1) {
            b.U(5, 1);
            vector(.5f, .25f, -.125f);
            vector(0, .2f, 0);
            b.F(std::sqrt(.96f));
        } else {
            b.U(0, 1);
            b.F(.75f);
            b.U(1, 1);
            b.F(.5f);
        }
    }
    b.U(2); // display groups
    for (int group = 0; group < 2; ++group) {
        text(group ? L"expressions" : L"body");
        text(L"");
        b.U(group, 1);
        b.U(group ? 3 : 7);
        for (int i = 0; i < (group ? 3 : 7); ++i) {
            b.U(group, 1);
            b.U(i, 1);
        }
    }
    b.U(withPhysics ? 4 : 0); // rigid bodies
    if (withPhysics) {
        const int links[] = {0, 6, 5, -1};
        const int modes[] = {0, 1, 2, 1};
        const int shapes[] = {0, 2, 1, 0};
        const float centers[4][3] = {{0, 5, 0}, {4, 4, 0}, {2, 4, 0}, {1, 2, 1}};
        for (int i = 0; i < 4; ++i) {
            text(L"body");
            text(L"");
            b.U(links[i], 1);
            b.U(0, 1);      // collision group
            b.U(0xFFFF, 2); // collide with all groups, including the floor
            b.U(shapes[i], 1);
            vector(.25f, .5f, .3f);
            vector(centers[i][0], centers[i][1], centers[i][2]);
            vector(.1f * i, -.07f * i, .03f * i);
            b.F(i ? 1.3f : 0);
            b.F(.1f);
            b.F(.2f);
            b.F(.15f);
            b.F(.5f);
            b.U(modes[i], 1);
        }
    }
    b.U(withPhysics ? 2 : 0); // joints
    if (withPhysics) {
        for (int i = 0; i < 2; ++i) {
            text(L"spring");
            text(L"");
            b.U(0, 1); // six DOF spring
            b.U(0, 1);
            b.U(i + 1, 1);
            vector(float(2 - i), 4.5f, .1f);
            vector(.1f, .2f, -.1f);
            vector(-.1f, -.2f, -.3f); // translation lower/upper limits
            vector(.1f, .2f, .3f);
            vector(-.5f, -.6f, -.7f); // rotation lower/upper limits
            vector(.5f, .6f, .7f);
            vector(3, 4, 5);
            vector(6, 7, 8);
        }
    }
    b.Save(path);
}
void WriteAccessory(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "xof 0303txt 0032\nMesh {3;0;0;0;,1;0;0;,0;1;0;;1;3;0,1,2;;}\n";
    Check(out.good(), "write accessory fixture");
}
void WriteLegacyScene(const std::filesystem::path& path, const std::filesystem::path& modelPath) {
    Bytes b;
    b.Text("Polygon Movie maker 0001", 30);
    b.U(640);
    b.U(480);
    b.U(300);
    b.F(30);
    for (int i = 0; i < 6; ++i)
        b.U(1, 1);
    b.U(0, 1);
    b.U(1, 1);
    b.Text("loop", 20);
    b.U(0, 1);
    b.Text("loop", 20);
    b.Text(modelPath.string().c_str(), 256);
    b.U(1, 1);
    b.U(1, 1);
    b.U(1);
    for (int i = 0; i < 4; ++i)
        b.U(0);
    b.U(4, 1);
    for (int i = 0; i < 4; ++i)
        b.U(1, 1);
    b.U(0);
    b.U(24);
    const auto bone = [&](unsigned frame, unsigned previous, unsigned next, float x) {
        b.U(frame);
        b.U(previous);
        b.U(next);
        for (int i = 0; i < 4; ++i) {
            b.U(20, 1);
            b.U(20, 1);
            b.U(107, 1);
            b.U(107, 1);
        }
        b.F(x);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(1);
        b.U(1, 1);
    };
    bone(0, 0, 0, 0);
    bone(0, 0, 2, 0);
    b.U(1);
    b.U(2);
    bone(24, 1, 0, 2.5f);
    const auto morph = [&](unsigned frame, unsigned previous, unsigned next, float value) {
        b.U(frame);
        b.U(previous);
        b.U(next);
        b.F(value);
        b.U(1, 1);
    };
    morph(0, 0, 0, 0);
    morph(0, 0, 2, 0);
    b.U(1);
    b.U(2);
    morph(24, 1, 0, .625f);
    b.U(0);
    b.U(0);
    b.U(0);
    b.U(1, 1);
    b.U(1, 1);
    b.U(0);
    for (int i = 0; i < 2; ++i) {
        b.F(i ? 2.5f : 0);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(0);
        b.F(1);
        // The original v1 reader consumes eight floats, writing quaternion X twice.
        b.F(0);
        b.U(0, 1);
        b.U(i ? 1 : 0, 1);
    }
    b.F(0);
    b.F(.625f);
    b.U(0);
    b.U(0);
    b.U(0);
    b.F(-45);
    for (int i = 0; i < 6; ++i)
        b.F(i == 1 ? 10.0f : 0.0f);
    for (int i = 0; i < 6; ++i) {
        b.U(20, 1);
        b.U(20, 1);
        b.U(107, 1);
        b.U(107, 1);
    }
    b.U(0, 1);
    b.U(30);
    b.U(0, 1);
    b.U(0);
    for (int i = 0; i < 9; ++i)
        b.F(i == 1 ? 10 : (i == 5 ? -45 : 0));
    b.U(0, 1);
    b.U(0);
    b.U(0);
    b.U(0);
    for (float value : {.6f, .6f, .6f, -.5f, -1.0f, .5f})
        b.F(value);
    b.U(0, 1);
    b.U(0);
    for (float value : {.6f, .6f, .6f, -.5f, -1.0f, .5f})
        b.F(value);
    b.U(0, 1);
    b.U(0);
    b.U(0, 1); // selected accessory, scroll, count
    b.U(24);
    b.U(0);
    b.U(24);
    b.U(1);
    for (int i = 0; i < 4; ++i)
        b.U(0, 1);
    b.U(0);
    b.U(60);
    b.U(0, 1);
    b.Text("", 256);
    b.U(0);
    b.U(0);
    b.F(1);
    b.Text("", 256);
    b.U(0);
    b.U(0);
    b.U(0);
    b.F(1);
    b.Text("", 256);
    b.U(0, 1);
    b.U(0, 1);
    b.U(1, 1);
    b.U(1, 1);
    b.F(60);
    b.U(0);
    // The earliest v1 files end here; every later section is read-gated.
    b.Save(path);
}
template <class T> T* Track() {
    auto* result = static_cast<T*>(::operator new(sizeof(T) * mdl::kTimelineKeyCapacity));
    std::memset(result, 0, sizeof(T) * mdl::kTimelineKeyCapacity);
    return result;
}
struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::unique_ptr<PhysicsScene> physics = std::make_unique<PhysicsScene>();
    HWND window = nullptr;
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* device = nullptr;
    std::filesystem::path directory;
    bool ready = false;
    explicit Fixture(bool allowMemoryDevice = false) {
        app->state = {};
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        Check(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"mds", 0, unique),
              "temporary directory name");
        DeleteFileW(unique);
        directory = referenceDirectory.empty() ? std::filesystem::path(unique)
                                               : referenceDirectory / "assets";
        std::filesystem::create_directories(directory.parent_path());
        std::filesystem::create_directory(directory);
        window = CreateWindowW(L"STATIC", L"Scene roundtrip", WS_OVERLAPPED, 0, 0, 900, 900,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!window || !d3d)
            return;
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        HRESULT created = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
        if (FAILED(created)) {
            const HRESULT halResult = created;
            // This test checks scene state and resource lifetime, not pixels.
            created = d3d->CreateDevice(0, D3DDEVTYPE_REF, window,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
            if (FAILED(created))
                created = d3d->CreateDevice(0, D3DDEVTYPE_NULLREF, window,
                                            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
            if (FAILED(created)) {
                std::printf("D3D9 device unavailable: HAL=%08lX NULLREF=%08lX\n",
                            static_cast<unsigned long>(halResult),
                            static_cast<unsigned long>(created));
                if (!allowMemoryDevice)
                    return;
                device = new SceneMemoryDevice();
                std::puts("physics fixture uses CPU buffers; textures/rendering are not tested");
            } else {
                std::puts("scene state fixture uses a D3D9 reference device");
            }
        }
        app->InitDefaults();
        app->Hwnd() = window;
        app->HInstance() = GetModuleHandleW(nullptr);
        app->Renderer() = renderer.get();
        app->Physics() = physics.get();
        app->EnglishUI() = 1;
        app->SidebarWidth() = 250;
        app->RenderWidth() = 640;
        app->RenderHeight() = 480;
        app->CameraParentModel() = -1;
        renderer->device = device;
        renderer->screenWidth = renderer->screenHeight = 900;
        renderer->aspectRatio = 1;
        renderer->postProcessEnabled = 1;
        g_Block = app.get();
        Check(SceneConstruct(physics.get(), renderer.get()), "construct physics world");
        for (int id = 400; id < 700; ++id) {
            const bool combo = id == panel::kMainComboModel || id == panel::kMainComboGround ||
                               id == panel::kMainComboNormal || id == panel::kAccessoryCombo ||
                               id == panel::kAttachBoneCombo || id == panel::kBoneRegisterCombo ||
                               id == panel::kRegisterScopeCombo || id == panel::kIkChainCombo ||
                               id == panel::kInterpCurveCombo ||
                               (id >= panel::kMorphCombo0 && id <= panel::kMorphCombo3);
            const bool button = id == panel::kRegisterFrameButton || id == panel::kUndoButton ||
                                id == panel::kRedoButton || id == panel::kDeleteFramesButton;
            Check(CreateWindowExA(0, combo ? "COMBOBOX" : (button ? "BUTTON" : "EDIT"), "0",
                                  WS_CHILD | (combo ? CBS_DROPDOWNLIST : 0), 0, 0, 80, 80, window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr) != nullptr,
                  "create scene control");
        }
        for (int id : {panel::kMainComboModel, panel::kMainComboGround, panel::kMainComboNormal}) {
            Add(id, "camera/light");
            Pick(id, 0);
        }
        app->CameraKeys() = Track<mdl::CameraKey>();
        app->LightKeys() = Track<mdl::LightKey>();
        app->ShadowKeys() = Track<mdl::SelfShadowKey>();
        app->GravityKeys() = Track<mdl::GravityKey>();
        for (int i = 0; i < mdl::kAccessorySlotCount; ++i)
            app->AccessoryKeys(i) = Track<mdl::AccessoryKey>();
        app->CameraKeys()[0].parentModel = -1;
        app->CameraKeys()[0].distance = -45;
        app->CameraKeys()[0].fov = 30;
        app->ShadowKeys()[0].mode = 1;
        app->ShadowKeys()[0].distance = .01125f;
        app->GravityKeys()[0].acceleration = 9.8f;
        app->GravityKeys()[0].direction[1] = -1;
        ready = true;
    }
    ~Fixture() {
        ReleaseSceneModels(*app);
        ReleaseAccessoriesAndTracks(*app);
        ReleaseGlobalTimelineTracks(*app);
        app->ReleaseTimelineSelectionRecords();
        delete[] app->SelectNavRecords();
        if (physics->world)
            DisposePhysicsWorld(physics.get());
        if (device)
            device->Release();
        if (d3d)
            d3d->Release();
        if (window)
            DestroyWindow(window);
        g_Block = nullptr;
        std::error_code ignored;
        if (referenceDirectory.empty())
            std::filesystem::remove_all(directory, ignored);
    }
    void Add(int id, const char* name) {
        SendDlgItemMessageA(window, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    void Pick(int id, int index) { SendDlgItemMessageA(window, id, CB_SETCURSEL, index, 0); }
    mdl::ModelRecord& LoadModel(int slot, int order) {
        const auto path =
            directory / (slot ? L"second.pmd" : (complexScene ? L"complex.pmx" : L"first.pmd"));
        if (complexScene && slot == 0)
            WriteComplexModel(path);
        else
            WriteModel(path);
        return LoadModelFile(path, slot, order);
    }
    mdl::ModelRecord& LoadModelFile(const std::filesystem::path& path, int slot, int order) {
        auto* model = static_cast<mdl::ModelRecord*>(::operator new(sizeof(mdl::ModelRecord)));
        std::memset(model, 0, sizeof(*model));
        ModelInitDefaults(model);
        app->ModelSlot(slot) = model;
        Check(ModelLoadPMD(model, window, path.c_str(), renderer.get(), 1, 0, 1, physics.get(),
                           app->PathWorkspace()),
              "load fixture PMD");
        model->comboSelIndex = model->comboSelIndex2 = static_cast<unsigned char>(order);
        for (int id : {panel::kMainComboModel, panel::kMainComboGround, panel::kMainComboNormal})
            Add(id, model->name);
        return *model;
    }
    void Save(const wchar_t* name) {
        wcscpy_s(app->EnvFileName(), 256, (directory / name).c_str());
        SaveSceneFile(app.get());
        Check(std::filesystem::file_size(directory / name) > 1000, "saved full PMM");
    }
    void Reload(const wchar_t* name) {
        wcscpy_s(app->EnvFileName(), 256, (directory / name).c_str());
        LoadSceneFile();
        Check(app->state.windowLayoutReady == 1, "PMM load completed");
    }
};

} // namespace
