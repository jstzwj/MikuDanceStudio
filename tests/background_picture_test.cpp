#include "scene_render_fixture.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/screen_vertex.hpp"
#include <type_traits>

static_assert(std::is_same_v<decltype(&D3DXCreateTextureFromFileExW),
                             mikudancestudio::d3dx::FnCreateTexFromFileExW>);
static_assert(std::is_same_v<decltype(&D3DXCreateTextureFromFileExA),
                             mikudancestudio::d3dx::FnCreateTexFromFileExA>);
static_assert(std::is_same_v<decltype(&D3DXCreateTextureFromFileInMemoryEx),
                             mikudancestudio::d3dx::FnCreateTexInMemEx>);

namespace {
struct Picture {
    MMDApp& app;
    Com<IDirect3DVertexBuffer9> vertices;
    explicit Picture(MMDApp& value) : app(value) {
        Hr(app.Renderer()->device->CreateVertexBuffer(kBackgroundQuadBytes, 0,
                                                      kTexturedScreenVertexFvf, D3DPOOL_MANAGED,
                                                      vertices.Out(), nullptr),
           "background vertices");
        app.PictureOverlayVertices() = vertices.p;
        app.PictureBackgroundTexture() = nullptr;
        app.ViewportRect() = {0, 0, kSize, kSize};
        app.Renderer()->viewScale = 1;
        app.GroundGridEnabled() = 0;
        app.GroundShadowEnabled() = 0;
        app.RecordingWindow() = nullptr;
        // Only the reference executable reads these retired ABI slots.
#if defined(_M_X64)
        std::memcpy(&app.state.pad129, &vertices.p, sizeof vertices.p);
#else
        app.state.reservedPictureOverlayVertices = reinterpret_cast<std::uintptr_t>(vertices.p);
#endif
    }
    ~Picture() {
        if (app.PictureBackgroundTexture())
            app.PictureBackgroundTexture()->Release();
        app.PictureBackgroundTexture() = nullptr;
        app.PictureOverlayVertices() = nullptr;
    }
    std::array<TexturedScreenVertex, 6> ReadVertices() {
        void* data = nullptr;
        std::array<TexturedScreenVertex, 6> result{};
        Hr(vertices->Lock(0, sizeof result, &data, D3DLOCK_READONLY), "read background quad");
        std::memcpy(result.data(), data, sizeof result);
        Hr(vertices->Unlock(), "unlock background quad");
        return result;
    }
};
std::vector<DWORD> Draw(Fixture& f, MMDApp& app, IDirect3DStateBlock9* initial,
                        const std::filesystem::path& path,
                        OriginalPhysicsModule* original = nullptr) {
    Hr(initial->Apply(), "restore initial rendering state");
    RenderTarget target(f.device);
    InitRenderStates(&app);
    Hr(f.device->SetRenderState(D3DRS_LIGHTING, FALSE), "unlit background");
    Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "background culling");
    Hr(f.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, kBackground, 1, 0),
       "clear background image");
    Hr(f.device->BeginScene(), "begin background image");
    if (original) {
        D3DXMATRIX world;
        D3DXMatrixIdentity(&world);
#if defined(_M_X64)
        original->Function<void (*)(MMDApp*, const D3DXMATRIX*)>(0x9FB20)(&app, &world);
#else
        original->Function<void(__thiscall*)(MMDApp*, D3DMATRIX)>(0x25D20)(&app, world);
#endif
    } else {
        RenderModelsFixed(&app);
    }
    Hr(f.device->EndScene(), "end background image");
    auto result = target.Pixels(path);
    Check(std::count(result.begin(), result.end(), result[kSize / 2 * kSize + kSize / 2]) <
              static_cast<int>(result.size() / 2),
          "background must contain a nonuniform image, not a blank target");
    return result;
}
void Load(Picture& picture, const std::filesystem::path& path, OriginalPhysicsModule* original) {
    auto& app = picture.app;
    wcscpy_s(app.PictureBackgroundPath(), 256, path.c_str());
    std::printf("loading %s: %ls\n", original ? "original" : "port", path.c_str());
    std::fflush(stdout);
    if (original) {
#if defined(_M_X64)
        std::memcpy(&app.state.pad199, &app.PathWorkspace(), sizeof(PathResolutionWorkspace));
        original->Function<void (*)(MMDApp*)>(0x68970)(&app);
#else
        app.state.reservedPathWorkspace = app.PathWorkspace();
        original->Function<void(__thiscall*)(MMDApp*)>(0x337A0)(&app);
#endif
    } else {
        LoadBackgroundPicture(&app);
    }
    Check(app.PictureBackgroundTexture() != nullptr, "picture texture created");
    Check(app.PictureWidth() > 0 && app.PictureHeight() > 0, "source dimensions retained");
    Check(app.PictureBackgroundEnabled() == 1, "picture display enabled");
    Check((GetMenuState(GetMenu(static_cast<HWND>(app.Hwnd())), MDS_CMD_TOGGLE_BACKGROUND_IMAGE,
                        MF_BYCOMMAND) &
           MF_CHECKED) != 0,
          "picture menu checked");
    Check(std::wcscmp(app.PictureBackgroundPath(), path.c_str()) == 0, "picture path retained");
    for (const auto& vertex : picture.ReadVertices())
        Check(vertex.reciprocalW == 1 && vertex.z == 0, "valid pretransformed background vertices");
}
void CheckImageInfo(Fixture& f, const std::filesystem::path& path) {
    struct GuardedInfo {
        D3DXIMAGE_INFO info{};
        std::array<UINT, 3> guard{0xCDCDCDCD, 0xCDCDCDCD, 0xCDCDCDCD};
    } output;
    static_assert(offsetof(GuardedInfo, guard) == 28);
    Com<IDirect3DTexture9> texture;
    Hr(d3dx::Get().fromFileExW(f.device, path.c_str(), 1024, 1024, 1, 0, D3DFMT_UNKNOWN,
                               D3DPOOL_MANAGED, kD3dxDefault, kD3dxDefault, 0, &output.info,
                               nullptr, texture.Out()),
       "complete image info");
    Check(output.guard == std::array<UINT, 3>{0xCDCDCDCD, 0xCDCDCDCD, 0xCDCDCDCD},
          "image info output stays within its SDK boundary");
    Check(output.info.Width == static_cast<UINT>(f.app->PictureWidth()) &&
              output.info.Height == static_cast<UINT>(f.app->PictureHeight()) &&
              output.info.Depth == 1 && output.info.MipLevels > 0 &&
              output.info.Format != D3DFMT_UNKNOWN && output.info.ResourceType == D3DRTYPE_TEXTURE,
          "all image info fields have their SDK meaning");
    const auto extension = path.extension();
    const auto format = extension == L".bmp"   ? D3DXIFF_BMP
                        : extension == L".jpg" ? D3DXIFF_JPG
                        : extension == L".png" ? D3DXIFF_PNG
                                               : D3DXIFF_DDS;
    Check(output.info.ImageFileFormat == format, "decoded image file format");
}
std::vector<std::filesystem::path> Images(Fixture& f) {
    Com<IDirect3DSurface9> surface;
    constexpr UINT width = 79, height = 43;
    Hr(f.device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                             surface.Out(), nullptr),
       "image fixture surface");
    D3DLOCKED_RECT lock{};
    Hr(surface->LockRect(&lock, nullptr, 0), "image fixture pixels");
    for (UINT y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits) + y * lock.Pitch);
        for (UINT x = 0; x < width; ++x)
            row[x] =
                D3DCOLOR_ARGB((x < width / 2 && y < height / 2) ? 96 : 255, x * 255 / (width - 1),
                              y * 255 / (height - 1), (x + y) % 2 ? 32 : 224);
    }
    Hr(surface->UnlockRect(), "unlock image fixture");
    std::vector<std::filesystem::path> paths;
    for (const auto& format : {std::pair{D3DXIFF_BMP, L"bmp"},
                               {D3DXIFF_JPG, L"jpg"},
                               {D3DXIFF_PNG, L"png"},
                               {D3DXIFF_DDS, L"dds"}}) {
        auto path = f.directory / (std::wstring(L"background.") + format.second);
        Hr(D3DXSaveSurfaceToFileW(path.c_str(), format.first, surface.p, nullptr, nullptr),
           "save image fixture");
        paths.push_back(path);
    }
    return paths;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        std::filesystem::path originalPath, image, output;
        for (int i = 1; i + 1 < argc; i += 2) {
            if (std::wcscmp(argv[i], L"--original") == 0)
                originalPath = argv[i + 1];
            else if (std::wcscmp(argv[i], L"--image") == 0)
                image = std::filesystem::absolute(argv[i + 1]);
            else if (std::wcscmp(argv[i], L"--output") == 0)
                output = argv[i + 1];
            else
                throw std::runtime_error("unknown background picture option");
        }
        auto original =
            originalPath.empty() ? nullptr : std::make_unique<OriginalPhysicsModule>(originalPath);
        Fixture f;
        if (!f.ready)
            return 77;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        Hr(f.device->GetCreationParameters(&creation), "background device type");
        if (creation.DeviceType == D3DDEVTYPE_NULLREF)
            return 77;
        auto paths = Images(f);
        if (!image.empty())
            paths.insert(paths.begin(), image);
        if (!output.empty())
            std::filesystem::create_directories(output);
        const HHOOK hook =
            SetWindowsHookExW(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
        const HMENU menu = CreateMenu();
        Check(AppendMenuW(menu, MF_STRING, MDS_CMD_TOGGLE_BACKGROUND_IMAGE, L"Picture"),
              "background menu");
        SetMenu(f.window, menu);
        InitRenderStates(f.app.get());
        Com<IDirect3DStateBlock9> initial;
        Hr(f.device->CreateStateBlock(D3DSBT_ALL, initial.Out()), "initial rendering state");
        auto reference = std::make_unique<MMDApp>();
        reference->state = f.app->state;
        {
            Picture port(*f.app), native(*reference);
            for (std::size_t i = 0; i < paths.size(); ++i) {
                Load(port, paths[i], nullptr);
                if (paths[i] != image)
                    Check(f.app->PictureWidth() == 79 && f.app->PictureHeight() == 43,
                          "dimensions describe source, not the 1024-square texture");
                CheckImageInfo(f, paths[i]);
                auto path = [&](const char* suffix) -> std::filesystem::path {
                    return output.empty() ? std::filesystem::path{}
                                          : output / (std::to_string(i) + suffix + ".png");
                };
                const auto pixels = Draw(f, *f.app, initial.p, path("-port"));
                Load(port, paths[i], nullptr);
                Check(pixels == Draw(f, *f.app, initial.p, {}),
                      "repeated picture replacement is stable");
                if (original) {
                    Load(native, paths[i], original.get());
                    Check(reference->PictureWidth() == f.app->PictureWidth() &&
                              reference->PictureHeight() == f.app->PictureHeight() &&
                              reference->PictureOffsetX() == f.app->PictureOffsetX() &&
                              reference->PictureOffsetY() == f.app->PictureOffsetY(),
                          "native picture dimensions and placement");
                    Near(reference->PictureScale(), f.app->PictureScale(), "native picture scale");
                    Check(pixels ==
                              Draw(f, *reference, initial.p, path("-original"), original.get()),
                          "native loaded texture and quad produce identical pixels");
                }
            }
            Check(dialogs == 0, "valid picture loads must not open an error dialog");
            const auto missing = f.directory / L"missing.png";
            auto* previous = f.app->PictureBackgroundTexture();
            wcscpy_s(f.app->PictureBackgroundPath(), 256, missing.c_str());
            LoadBackgroundPicture(f.app.get());
            Check(dialogs == 1 && f.app->PictureBackgroundPath()[0] == 0 &&
                      f.app->PictureBackgroundTexture() == previous,
                  "missing file reports failure and retains the previous texture as native does");
            const auto corrupt = f.directory / L"corrupt.png";
            std::ofstream(corrupt, std::ios::binary) << "invalid image";
            wcscpy_s(f.app->PictureBackgroundPath(), 256, corrupt.c_str());
            LoadBackgroundPicture(f.app.get());
            Check(dialogs == 2 && f.app->PictureBackgroundPath()[0] == 0 &&
                      f.app->PictureBackgroundTexture() == nullptr,
                  "decode failure clears the released texture and stored path");
            Load(port, paths.front(), nullptr);
            Draw(f, *f.app, initial.p, {});
        }
        SetMenu(f.window, nullptr);
        DestroyMenu(menu);
        UnhookWindowsHookEx(hook);
        Check(dialogs == 2, "only the two intentional load failures open dialogs");
        std::puts("background images: BMP/JPG/PNG/DDS, replacement and GPU pixels passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
