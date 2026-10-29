#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include "cpu_live_checkpoint.hpp"
#include "mikudancestudio/pmm_document.hpp"
#include "mikudancestudio/pmm_scene_document.hpp"
#include "mikudancestudio/scene_session.hpp"

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

struct Windows {
    DWORD processId;
    HWND main = nullptr;
    std::vector<HWND> dialogs;
};

BOOL CALLBACK Collect(HWND window, LPARAM context) {
    auto& windows = *reinterpret_cast<Windows*>(context);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != windows.processId || !IsWindowVisible(window)) return TRUE;
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    if (wcscmp(className, L"Polygon Movie Maker") == 0) windows.main = window;
    if (wcscmp(className, L"#32770") == 0) windows.dialogs.push_back(window);
    return TRUE;
}

Windows Snapshot(DWORD processId) {
    Windows windows{processId};
    EnumWindows(Collect, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

BOOL CALLBACK FindButton(HWND window, LPARAM context) {
    wchar_t className[32]{};
    GetClassNameW(window, className, 32);
    if (wcscmp(className, L"Button") == 0 && IsWindowVisible(window)) {
        auto& button = *reinterpret_cast<HWND*>(context);
        if (!button) button = window;
    }
    return TRUE;
}

HWND ActionButton(HWND dialog) {
    HWND button = GetDlgItem(dialog, IDOK);
    if (!button) EnumChildWindows(dialog, FindButton,
                                 reinterpret_cast<LPARAM>(&button));
    return button;
}

BOOL CALLBACK FindFileNameEdit(HWND window, LPARAM context) {
    const int id = GetDlgCtrlID(window);
    if (id != 1148 && id != 1001) return TRUE;
    wchar_t className[32]{};
    GetClassNameW(window, className, 32);
    if (wcscmp(className, L"Edit") == 0) {
        auto& edit = *reinterpret_cast<HWND*>(context);
        if (!edit) edit = window;
    }
    return TRUE;
}

HWND FileNameEdit(HWND dialog) {
    HWND edit = nullptr;
    EnumChildWindows(dialog, FindFileNameEdit,
                     reinterpret_cast<LPARAM>(&edit));
    return edit;
}

BOOL CALLBACK FindMessage(HWND window, LPARAM context) {
    wchar_t className[32]{};
    GetClassNameW(window, className, 32);
    if (wcscmp(className, L"Static") == 0) {
        auto& message = *reinterpret_cast<std::wstring*>(context);
        const int length = GetWindowTextLengthW(window);
        if (length > 0) {
            std::wstring text(length + 1, L'\0');
            GetWindowTextW(window, text.data(), length + 1);
            text.resize(length);
            message += text;
        }
    }
    return TRUE;
}

std::wstring DialogMessage(HWND dialog) {
    std::wstring message;
    EnumChildWindows(dialog, FindMessage, reinterpret_cast<LPARAM>(&message));
    return message;
}

BOOL CALLBACK PrintChild(HWND window, LPARAM) {
    wchar_t className[64]{}, text[256]{};
    GetClassNameW(window, className, 64);
    GetWindowTextW(window, text, 256);
    std::fprintf(stderr, "child=%p id=%d class=%ls text=%ls\n",
                 static_cast<void*>(window), GetDlgCtrlID(window), className, text);
    return TRUE;
}

bool WaitForDialog(HANDLE process, DWORD processId, HWND& dialog,
                   bool fileDialog, int seconds) {
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    while (Clock::now() < deadline) {
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
            DWORD exitCode = 0;
            GetExitCodeProcess(process, &exitCode);
            std::fprintf(stderr, "application exited before dialog: 0x%08lx\n",
                         exitCode);
            return false;
        }
        for (HWND candidate : Snapshot(processId).dialogs) {
            if (ActionButton(candidate) &&
                (FileNameEdit(candidate) != nullptr) == fileDialog) {
                dialog = candidate;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto windows = Snapshot(processId);
    std::fprintf(stderr, "dialog timeout; main=%p dialogs=%zu\n",
                 static_cast<void*>(windows.main), windows.dialogs.size());
    for (HWND candidate : windows.dialogs) {
        wchar_t title[256]{};
        GetWindowTextW(candidate, title, 256);
        std::fprintf(stderr, "dialog=%p title=%ls file-field=%p\n",
                     static_cast<void*>(candidate), title,
                     static_cast<void*>(GetDlgItem(candidate, 1148)));
        EnumChildWindows(candidate, PrintChild, 0);
    }
    return false;
}

bool SetFileName(IUIAutomation* automation, HWND dialog,
                 const std::wstring& path) {
    const HWND editWindow = FileNameEdit(dialog);
    if (!editWindow) return false;
    ComPtr<IUIAutomationElement> edit;
    if (FAILED(automation->ElementFromHandle(editWindow, &edit))) return false;
    ComPtr<IUIAutomationValuePattern> value;
    if (FAILED(edit->GetCurrentPatternAs(UIA_ValuePatternId,
                                         IID_PPV_ARGS(&value)))) return false;
    BSTR filename = SysAllocString(path.c_str());
    const HRESULT result = value->SetValue(filename);
    SysFreeString(filename);
    if (FAILED(result)) return false;
    BSTR actual = nullptr;
    const bool matches = SUCCEEDED(value->get_CurrentValue(&actual)) &&
                         actual && path == actual;
    SysFreeString(actual);
    return matches;
}

int ReadBytes(void* context, void* output, std::size_t size) {
    auto& stream = *static_cast<std::ifstream*>(context);
    stream.read(static_cast<char*>(output), size);
    return static_cast<int>(stream.gcount());
}
struct SavedBytes {
    std::vector<std::uint8_t> data;
    std::size_t offset = 0;
};
int ReadSavedBytes(void* context, void* output, std::size_t size) {
    auto& bytes = *static_cast<SavedBytes*>(context);
    const auto count = (std::min)(size, bytes.data.size() - bytes.offset);
    if (count) std::memcpy(output, bytes.data.data() + bytes.offset, count);
    bytes.offset += count;
    return static_cast<int>(count);
}
bool WriteSavedBytes(void* context, const void* input, std::size_t size) {
    auto& bytes = *static_cast<SavedBytes*>(context);
    const auto* first = static_cast<const std::uint8_t*>(input);
    bytes.data.insert(bytes.data.end(), first, first + size);
    return true;
}

struct EditMarker {
    std::uint8_t slot = 0;
    std::uint32_t bone = 0;
    float x = 0;
    float cameraX = 0;
    std::uint8_t accessorySlot = 255;
    float accessoryX = 0;
    std::uint8_t shadowMode = 0;
    float shadowDistance = 0;
};
bool SavedSceneHasModels(const fs::path& path, std::uint8_t expectedCount,
                         const std::optional<EditMarker>& edit) {
    std::ifstream stream(path, std::ios::binary);
    mikudancestudio::format::PmmDocument document;
    if (!stream || !mikudancestudio::format::ReadPmmDocument(
            ReadBytes, &stream, document) ||
        document.modelCount != expectedCount) {
        std::fprintf(stderr, "saved document/model count check failed\n");
        return false;
    }
    if (!edit) return true;
    stream.clear();
    stream.seekg(0);
    mikudancestudio::format::PmmSceneDocument scene;
    if (!mikudancestudio::format::ReadPmmSceneDocument(
            ReadBytes, &stream, nullptr, nullptr, scene) ||
        scene.envelope.header.version !=
            mikudancestudio::format::PmmVersion::V2) {
        std::fprintf(stderr, "saved scene parse/version check failed\n");
        return false;
    }
    if (scene.cameraKeys.dense.empty() ||
        scene.cameraKeys.dense[0].eye[0] != edit->cameraX) {
        std::fprintf(stderr, "saved camera key check failed\n");
        return false;
    }
    if (scene.selfShadowKeys.dense.empty() ||
        scene.selfShadowKeys.dense[0].mode != edit->shadowMode ||
        std::fabs(scene.selfShadowKeys.dense[0].distance -
                  edit->shadowDistance) > 0.000001f) {
        std::fprintf(stderr, "saved self-shadow key check failed\n");
        return false;
    }
    const auto accessory = std::find_if(scene.accessories.begin(),
        scene.accessories.end(), [edit](const auto& item) {
            return item.slot == edit->accessorySlot;
        });
    if (accessory == scene.accessories.end() ||
        accessory->keyRecords.dense.empty() ||
        accessory->keyRecords.dense[0].position[0] != edit->accessoryX) {
        std::fprintf(stderr, "saved accessory key check failed\n");
        return false;
    }
    bool boneSaved = false;
    for (const auto& model : scene.models)
        if (model.slot == edit->slot &&
            edit->bone < model.boneKeys.dense.size())
            boneSaved = model.boneKeys.dense[edit->bone].position[0] == edit->x;
    if (!boneSaved) {
        std::fprintf(stderr, "saved bone key check failed\n");
        return false;
    }
    stream.clear();
    stream.seekg(0);
    mikudancestudio::format::SceneSession session;
    if (!mikudancestudio::format::SceneSession::Load(
            ReadBytes, &stream, nullptr, nullptr, session)) {
        std::fprintf(stderr, "saved session load failed\n");
        return false;
    }
    if (!session.Edit([edit](auto& value) {
            for (auto& item : value.accessories)
                if (item.slot == edit->accessorySlot)
                    item.keyRecords.dense[0].position[0] += 1.0f;
        })) {
        std::fprintf(stderr, "saved session edit failed\n");
        return false;
    }
    if (!session.Seek(0)) {
        std::fprintf(stderr, "saved session seek failed\n");
        return false;
    }
    if (!session.StartPlayback()) {
        std::fprintf(stderr, "saved session playback start failed\n");
        return false;
    }
    if (!session.Advance(5.0f / 30.0f)) {
        std::fprintf(stderr, "saved session advance failed\n");
        return false;
    }
    SavedBytes saved;
    if (!session.Save(WriteSavedBytes, &saved)) {
        std::fprintf(stderr, "saved session write check failed\n");
        return false;
    }
    mikudancestudio::format::SceneSession reloaded;
    if (!mikudancestudio::format::SceneSession::Load(
            ReadSavedBytes, &saved, nullptr, nullptr, reloaded)) {
        std::fprintf(stderr, "saved session reload check failed\n");
        return false;
    }
    const auto reread = std::find_if(
        reloaded.Document().accessories.begin(),
        reloaded.Document().accessories.end(), [edit](const auto& item) {
            return item.slot == edit->accessorySlot;
        });
    return reread != reloaded.Document().accessories.end() &&
           reread->keyRecords.dense[0].position[0] ==
               edit->accessoryX + 1.0f;
}

struct TemporaryScene {
    fs::path root;
    bool passed = false;
    ~TemporaryScene() {
        if (passed) {
            std::error_code error;
            fs::remove_all(root, error);
            if (error) std::fprintf(stderr, "temporary cleanup failed: %s\n",
                                    error.message().c_str());
        } else if (!root.empty()) {
            std::fprintf(stderr, "failed fixture copy retained: %ls\n", root.c_str());
        }
    }
};

struct ChildProcess {
    PROCESS_INFORMATION info{};
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    bool Own() {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        return job && SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) &&
            AssignProcessToJobObject(job,info.hProcess);
    }
    void Close() {
        if (!info.hProcess) {if(job)CloseHandle(job);job=nullptr;return;}
        auto windows = Snapshot(info.dwProcessId);
        if (windows.main) PostMessageW(windows.main, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(info.hProcess, 3000) == WAIT_TIMEOUT) {
            TerminateProcess(info.hProcess, 1);
            WaitForSingleObject(info.hProcess, 3000);
        }
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
        if(job)CloseHandle(job);
        info={};job=nullptr;
    }
    ~ChildProcess() {Close();}
};

int Run(const fs::path& executable, const fs::path& fixture,
        const fs::path& modelName, const fs::path& parityExecutable) {
    const wchar_t* guiMode = _wgetenv(L"MDS_RHI_GUI_MODE");
    const bool optinMode = guiMode && wcscmp(guiMode, L"optin") == 0;
    const bool materialMode = guiMode && wcsncmp(guiMode, L"model_textures", 14) == 0;
    const bool preparationMode = materialMode && wcsstr(guiMode, L"preparation");
    const bool nativePreparationMode = materialMode && wcsstr(guiMode,L"native");
    const bool baseSphereMode = materialMode && wcsstr(guiMode, L"combined");
    const bool sphereOnlyMode = materialMode && wcsstr(guiMode, L"sphere");
    const bool additiveMode = materialMode && wcsstr(guiMode, L"add");
    const bool configuredMode = baseSphereMode || sphereOnlyMode || nativePreparationMode;
    const bool toonMode = materialMode || (guiMode && (wcscmp(guiMode, L"model_toon") == 0 ||
                                      wcscmp(guiMode, L"model_toon_optin") == 0));
    const bool modelOptinMode = !_wgetenv(L"MDS_RHI_REAL_BATCH_CAPTURE") && guiMode &&
        (wcscmp(guiMode, L"model_optin") == 0 || wcscmp(guiMode, L"model_toon_optin") == 0 || configuredMode);
    const bool swapchainMode=guiMode && wcscmp(guiMode,L"swapchain")==0;
    const bool modelMode = swapchainMode || toonMode || modelOptinMode || (guiMode && wcscmp(guiMode, L"model") == 0);
    const bool spriteOptinMode = guiMode &&
        wcscmp(guiMode, L"sprite_optin") == 0;
    const bool spriteMode = spriteOptinMode ||
        (guiMode && wcscmp(guiMode, L"sprite") == 0);
    const bool textOptinMode = guiMode &&
        wcscmp(guiMode, L"text_optin") == 0;
    const bool textMode = textOptinMode ||
        (guiMode && wcscmp(guiMode, L"text") == 0);
    const bool portableFontMode = textOptinMode &&
        _wgetenv(L"MDS_RHI_PORTABLE_FONT") &&
        wcscmp(_wgetenv(L"MDS_RHI_PORTABLE_FONT"), L"1") == 0;
    if (modelName != modelName.filename() ||
        fixture.parent_path().filename() != L"UserFile") return 77;
    const fs::path sourceModel = fixture.parent_path() / L"Model" / modelName;
    if (!fs::is_regular_file(fixture) || !fs::is_regular_file(sourceModel) ||
        !fs::is_regular_file(executable)) return 77;

    wchar_t tempPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath)) return 1;
    TemporaryScene temporary;
    temporary.root = fs::path(tempPath) /
        (L"mds-gui-pmm-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    const fs::path userFile = temporary.root / L"UserFile";
    const fs::path copiedScene = userFile / fixture.filename();
    const fs::path relocated = userFile / L"Recovered" / modelName;
    const fs::path saved = userFile / L"gui_recovered.pmm";
    const fs::path parityReport = temporary.root / L"runtime_parity.txt";
    const fs::path reopenedReport = temporary.root / L"v2_accessory_parity.txt";
    const fs::path captureDirectory = temporary.root / L"rhi-capture";
    if (modelMode && !swapchainMode) {
        fs::create_directories(captureDirectory);
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_AB", L"1");
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_TOON", toonMode ? L"1" : L"0");
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_TEXTURES", materialMode ? L"1" : L"0");
        SetEnvironmentVariableW(L"MDS_RHI_TEXTURE_PREP_CANDIDATE", preparationMode ? L"1" : L"0");
        SetEnvironmentVariableW(L"MDS_RHI_TEXTURE_PREP_D3DX", nativePreparationMode ? L"1" : L"0");
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_NO_MME", L"1");
        // The first material acceptance batch is the eight-triangle highlight,
        // rather than a complete eye mesh with internal depth overlap.
        const auto* selectedMaterial=_wgetenv(L"MDS_RHI_GUI_MATERIAL");
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_MATERIAL",selectedMaterial?selectedMaterial:L"10");
        SetEnvironmentVariableW(L"MDS_RHI_MODEL_ADAPTER", modelOptinMode ? L"1" : L"0");
        SetEnvironmentVariableW(L"MDS_RHI_LIVE_COMPARE_DIR", captureDirectory.c_str());
    }
    if (copiedScene.wstring().size() >= 255 || saved.wstring().size() >= 255) {
        std::fprintf(stderr, "temporary scene path exceeds application limit\n");
        return 1;
    }
    fs::create_directories(temporary.root);
    if (!parityExecutable.empty() &&
        (!fs::is_regular_file(parityExecutable) ||
         !SetEnvironmentVariableW(L"MIKUDANCESTUDIO_VB_DUMP_DIR",
                                  captureDirectory.c_str()) ||
         !SetEnvironmentVariableW(L"MDS_RHI_LIVE_COMPARE_DIR",
                                  captureDirectory.c_str()) ||
         !SetEnvironmentVariableW(L"MDS_RHI_LIVE_AB",
                                  optinMode ? L"0" : L"1") ||
         (spriteMode && !SetEnvironmentVariableW(
             L"MDS_RHI_SPRITE_AB", L"1")) ||
         (spriteOptinMode && !SetEnvironmentVariableW(
             L"MDS_RHI_SPRITE_ADAPTER", L"1")) ||
         (textMode && !SetEnvironmentVariableW(
             L"MDS_RHI_TEXT_AB", L"1")) ||
         (textOptinMode && !SetEnvironmentVariableW(
             L"MDS_RHI_TEXT_ADAPTER", L"1")) ||
         (optinMode && !SetEnvironmentVariableW(
             L"MDS_RHI_SELECTION_ADAPTER", L"1")))) return 1;
    fs::copy(fixture.parent_path(), userFile,
             fs::copy_options::recursive);
    fs::create_directories(relocated.parent_path());
    fs::rename(userFile / L"Model" / modelName, relocated);
    if(_wgetenv(L"MDS_RHI_HISTORY_TRANSLUCENT") || _wgetenv(L"MDS_RHI_CPU_SWITCH_TOON")) {
        const auto modelPath=userFile/L"Model/Miku_Hatsune_metal.pmd";
        std::ifstream input(modelPath,std::ios::binary);std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{input},{});
        const auto u32=[&](std::size_t at){if(at+4>bytes.size())return std::uint32_t(0);std::uint32_t v;std::memcpy(&v,bytes.data()+at,4);return v;};
        const auto indexOffset=287ull+static_cast<std::uint64_t>(u32(283))*38;
        const auto materials=indexOffset+4ull+static_cast<std::uint64_t>(u32(indexOffset))*2;
        const auto alphaOffset=materials+4+13*70+12;
        if(bytes.size()<283 || std::memcmp(bytes.data(),"Pmd",3) || u32(materials)<=13 || alphaOffset+4>bytes.size())return 26;
        if(_wgetenv(L"MDS_RHI_HISTORY_TRANSLUCENT")) {
            const float alpha=.5f;std::memcpy(bytes.data()+alphaOffset,&alpha,4);
            std::printf("temporary material 13 Alpha=0.5; original source preserved\n");
        }
        if(_wgetenv(L"MDS_RHI_CPU_SWITCH_TOON")) {
            bytes[materials+4+12*70+44]=1; // PMD Toon index: shared toon02
            std::printf("temporary material 12 Toon=2; original source preserved\n");
        }
        std::ofstream output(modelPath,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!output)return 26;
    }
    if (configuredMode) {
        // Change only the texture-name field in the temporary original PMD.
        // PMM animation, geometry, bone influences and eligibility stay intact.
        const auto modelPath = userFile / L"Model/Miku_Hatsune_metal.pmd";
        std::ifstream input(modelPath, std::ios::binary);
        std::vector<std::uint8_t> data(std::istreambuf_iterator<char>{input}, {});
        input.close();
        const auto u32 = [&](std::size_t at) -> std::uint32_t {
            if (at > data.size() || data.size() - at < 4) return 0;
            return data[at] | static_cast<std::uint32_t>(data[at + 1]) << 8 |
                static_cast<std::uint32_t>(data[at + 2]) << 16 |
                static_cast<std::uint32_t>(data[at + 3]) << 24;
        };
        const auto indexOffset = 287ull + static_cast<std::uint64_t>(u32(283)) * 38;
        const auto materialOffset = indexOffset + 4ull + static_cast<std::uint64_t>(u32(indexOffset)) * 2;
        const auto textureOffset = materialOffset + 4 + 10 * 70 + 50;
        if (data.size() < 283 || std::memcmp(data.data(), "Pmd", 3) ||
            u32(materialOffset) <= 10 || textureOffset + 20 > data.size() ||
            std::any_of(data.begin() + textureOffset, data.begin() + textureOffset + 20,
                        [](std::uint8_t b) { return b != 0; })) return 26;
        const char* name = nativePreparationMode ? "eyeM2.bmp" : baseSphereMode ? (additiveMode ? "eye2.bmp*a3.spa" : "eye2.bmp*metal.sph") :
                                             (additiveMode ? "a3.spa" : "metal.sph");
        std::memcpy(data.data() + textureOffset, name, std::strlen(name));
        std::ofstream output(modelPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!output) return 26;
        std::printf("configured temporary material 10 texture=%s (original source preserved)\n", name);
    }
    std::ifstream fixtureStream(copiedScene, std::ios::binary);
    mikudancestudio::format::PmmDocument source;
    if (!fixtureStream || !mikudancestudio::format::ReadPmmDocument(
                              ReadBytes, &fixtureStream, source) ||
        source.header.version != mikudancestudio::format::PmmVersion::V1)
        return 1;
    if(_wgetenv(L"MDS_RHI_GUI_GROUND") && wcscmp(_wgetenv(L"MDS_RHI_GUI_GROUND"),L"1")==0) {
        struct LayoutContext {fs::path userFile,relocated;} context{userFile,relocated};
        const auto resolve=[](void* raw,std::uint8_t,const std::array<char,20>&,const std::array<char,256>& reference,mikudancestudio::format::PmmModelLayout& layout)->bool {
            const auto& context=*static_cast<LayoutContext*>(raw);
            const std::string path(reference.data(),std::find(reference.begin(),reference.end(),'\0')-reference.begin());
            const auto name=fs::path(path).filename();auto file=context.userFile/L"Model"/name;
            if(!fs::is_regular_file(file) && name==context.relocated.filename())file=context.relocated;
            std::ifstream stream(file,std::ios::binary);mikudancestudio::format::ModelDocument model;
            if(!stream || !mikudancestudio::format::ReadModelDocument(ReadBytes,&stream,model) || model.kind!=mikudancestudio::format::ModelKind::Pmd)return false;
            layout={static_cast<std::uint32_t>(model.pmd.bones.size()),static_cast<std::uint32_t>(model.pmd.morphs.size()),static_cast<std::uint32_t>(model.pmd.ikChains.size())};return true;
        };
        mikudancestudio::format::PmmSceneDocument parsed;if(!mikudancestudio::format::ParsePmmSceneDocument(source,resolve,&context,parsed))return 33;
        const auto section=std::find_if(parsed.sections.begin(),parsed.sections.end(),[](const auto& s){return s.kind==mikudancestudio::format::PmmSceneSectionKind::Configuration;});
        if(section==parsed.sections.end() || section->size<11)return 33;
        // The documented view record is 11 bytes; groundGrid is its second
        // byte. Change only the temporary fixture before the first render.
        const auto offset=section->offset+section->size-10;fixtureStream.close();std::fstream edited(copiedScene,std::ios::in|std::ios::out|std::ios::binary);
        const bool shadows=_wgetenv(L"MDS_RHI_GUI_PROJECTED_SHADOWS") && wcscmp(_wgetenv(L"MDS_RHI_GUI_PROJECTED_SHADOWS"),L"1")==0;
        const char enabled=1;edited.seekp(static_cast<std::streamoff>(offset));edited.write(&enabled,1);
        if(shadows){edited.seekp(static_cast<std::streamoff>(offset+1));edited.write(&enabled,1);}edited.close();if(!edited)return 33;
        std::ifstream check(copiedScene,std::ios::binary);mikudancestudio::format::PmmSceneDocument verified;
        if(!mikudancestudio::format::ReadPmmSceneDocument(ReadBytes,&check,resolve,&context,verified) || !verified.view.groundGrid ||
            (shadows && !verified.view.groundShadow))return 33;
        std::printf("temporary PMM groundGrid=1 groundShadow=%u offset=%zu; original source preserved\n",verified.view.groundShadow,offset);
    }
#ifdef MDS_EXPECT_RUNTIME_PARITY
    // In a diagnostic build the application writes this only after the
    // successful load tail has completed.
    if (!SetEnvironmentVariableW(L"MDS_PMM_RUNTIME_REPORT",
                                 parityReport.c_str())) return 1;
#endif

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation)))) return 1;
    std::wstring command = L"\"" + executable.wstring() + L"\" \"" +
                           copiedScene.wstring() + L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    ChildProcess child;
    const auto workingDirectory = executable.parent_path().wstring();
    if (!CreateProcessW(executable.c_str(), commandBuffer.data(), nullptr,
                        nullptr, FALSE, 0, nullptr, workingDirectory.c_str(),
                        &startup, &child.info)) {
        std::fprintf(stderr, "failed to launch application: %lu\n", GetLastError());
        return 1;
    }
    if(!child.Own())return 1;
    HWND dialog = nullptr;
    HWND handledDialog = nullptr;
    int recoveries = 0;
    const auto loadDeadline = Clock::now() + std::chrono::seconds(90);
    HWND main = nullptr;
    while (Clock::now() < loadDeadline) {
        auto windows = Snapshot(child.info.dwProcessId);
        if (WaitForSingleObject(child.info.hProcess, 0) != WAIT_TIMEOUT) {
            std::fprintf(stderr, "application exited during scene load\n");
            return 6;
        }
        if (!windows.dialogs.empty()) {
            dialog = windows.dialogs.front();
            if (dialog == handledDialog) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (FileNameEdit(dialog)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            const std::wstring message = DialogMessage(dialog);
            if (message.find(L"Cannot open the model file:") == std::wstring::npos ||
                recoveries >= source.modelCount) {
                std::fprintf(stderr, "unexpected dialog after %d recoveries: %ls\n",
                             recoveries, message.c_str());
                EnumChildWindows(dialog, PrintChild, 0);
                return 5;
            }
            std::printf("missing-model prompt %d/%u\n", recoveries + 1,
                        source.modelCount);
            PostMessageW(ActionButton(dialog), BM_CLICK, 0, 0);
            if (!WaitForDialog(child.info.hProcess, child.info.dwProcessId,
                               dialog, true, 20)) return 3;
            fs::path recoveredModel = relocated;
            if (modelMode) {
                // Recover each original model, including the third Miku
                // reference. Replacing it by Dammy_Bone would give us a
                // zero-geometry scene and no meaningful material probe.
                for (const auto& entry : fs::directory_iterator(userFile / L"Model")) {
                    if (entry.is_regular_file() && entry.path().extension() == L".pmd" &&
                        message.find(entry.path().filename().wstring()) != std::wstring::npos) {
                        recoveredModel = entry.path();
                        break;
                    }
                }
            }
            if (!SetFileName(automation.Get(), dialog, recoveredModel.wstring())) {
                std::fprintf(stderr, "could not set model selection filename\n");
                return 4;
            }
            PostMessageW(ActionButton(dialog), BM_CLICK, 0, 0);
            handledDialog = dialog;
            ++recoveries;
            continue;
        }
        main = windows.main;
        DWORD_PTR modelCount = 0;
        if (recoveries > 0 && main && GetDlgItem(main, 436) &&
            SendMessageTimeoutW(GetDlgItem(main, 436), CB_GETCOUNT, 0, 0,
                                SMTO_ABORTIFHUNG, 1000, &modelCount) &&
            modelCount >= static_cast<DWORD_PTR>(source.modelCount + 1)) {
#ifdef MDS_EXPECT_RUNTIME_PARITY
            if (fs::exists(parityReport)) break;
#else
            break;
#endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!main || WaitForSingleObject(child.info.hProcess, 0) != WAIT_TIMEOUT) {
        std::fprintf(stderr, "scene did not reach a live main window\n");
        return 6;
    }
    if(swapchainMode) {
        const auto* directoryText=_wgetenv(L"MDS_RHI_SWAPCHAIN_TEST_DIR");
        if(!directoryText) return 30;
        const fs::path directory(directoryText);
        // Real WM_SIZE transitions, then repeated same-size Reset. Minimize
        // must suspend submissions until restore; it is not native GPU loss.
        unsigned sequence=0; bool allPassed=true;
        for(const auto size : {std::pair<unsigned,unsigned>{960,720},{1100,800},{960,720},{960,720},{1100,800},{960,720}}) {
            if(sequence==3) {
                ShowWindow(main,SW_MINIMIZE);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const auto pausedLogSize=fs::file_size(directory/L"reset.log");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if(!IsIconic(main) || fs::file_size(directory/L"reset.log")!=pausedLogSize) return 31;
                ShowWindow(main,SW_RESTORE);
            }
            RECT client{0,0,static_cast<LONG>(size.first),static_cast<LONG>(size.second)};
            if(!AdjustWindowRect(&client,static_cast<DWORD>(GetWindowLongPtrW(main,GWL_STYLE)),GetMenu(main)!=nullptr) ||
               !SetWindowPos(main,nullptr,0,0,client.right-client.left,client.bottom-client.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)) return 31;
            ++sequence;
            {
                std::ofstream output(directory/L"request.next");
                output<<sequence<<" "<<size.first<<" "<<size.second<<"\n";
            }
            fs::rename(directory/L"request.next",directory/L"request.txt");
            const auto deadline=Clock::now()+std::chrono::seconds(30);
            std::string results;
            while(Clock::now()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT) {
                std::ifstream input(directory/L"results.txt");
                results.assign(std::istreambuf_iterator<char>{input},{});
                if(results.find("sequence="+std::to_string(sequence)+" ")!=std::string::npos) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            fs::remove(directory/L"request.txt");
            if(results.find("sequence="+std::to_string(sequence)+" ")==std::string::npos) {
                std::fprintf(stderr,"swapchain results:\n%s\n",results.c_str()); return 32;
            }
            allPassed=allPassed && results.find("FAIL")==std::string::npos;
        }
        if(!allPassed) return 32;
        temporary.passed=true;
        std::puts("real PMM swapchain: resize, minimize/restore, repeated rebuild, legacy/controller pixel comparison passed");
        return 0;
    }
    std::optional<EditMarker> expectedEdit;
#ifdef MDS_EXPECT_RUNTIME_PARITY
    {
        std::ifstream stream(parityReport);
        const std::string text(std::istreambuf_iterator<char>{stream}, {});
        const auto editAt = text.find("BRIDGE v1 edit slot=");
        const auto cameraAt = text.find("BRIDGE v1 camera edit frame=0 x=");
        const auto accessoryAt = text.find(
            "BRIDGE v1 accessory edit slot=");
        const auto shadowAt = text.find("BRIDGE v1 shadow edit frame=0");
        unsigned slot = 0, bone = 0;
        unsigned accessorySlot = 0;
        float x = 0, cameraX = 0, accessoryX = 0;
        unsigned shadowMode = 0;
        float shadowDistance = 0;
        if (text.find("PASS checks=") == std::string::npos ||
            text.find("FAIL") != std::string::npos ||
            editAt == std::string::npos ||
            cameraAt == std::string::npos ||
            accessoryAt == std::string::npos ||
            shadowAt == std::string::npos ||
            text.find("BRIDGE v1 accessory playback slot=") ==
                std::string::npos ||
            text.find("BRIDGE v1 camera playback frame=5 x=") ==
                std::string::npos ||
            text.find("BRIDGE v1 playback models=" +
                      std::to_string(source.modelCount)) == std::string::npos ||
            std::sscanf(text.c_str() + editAt,
                        "BRIDGE v1 edit slot=%u bone=%u x=%f",
                        &slot, &bone, &x) != 3 ||
            std::sscanf(text.c_str() + cameraAt,
                        "BRIDGE v1 camera edit frame=0 x=%f",
                        &cameraX) != 1 ||
            std::sscanf(text.c_str() + accessoryAt,
                        "BRIDGE v1 accessory edit slot=%u frame=0 x=%f",
                        &accessorySlot, &accessoryX) != 2 ||
            std::sscanf(text.c_str() + shadowAt,
                        "BRIDGE v1 shadow edit frame=0 mode=%u distance=%f",
                        &shadowMode, &shadowDistance) != 2 ||
            slot >= 255 || accessorySlot >= 255) {
            std::fprintf(stderr, "runtime parity report:\n%s\n", text.c_str());
            return 10;
        }
        expectedEdit = EditMarker{static_cast<std::uint8_t>(slot), bone,
                                  x, cameraX,
                                  static_cast<std::uint8_t>(accessorySlot),
                                  accessoryX,
                                  static_cast<std::uint8_t>(shadowMode),
                                  shadowDistance};
        std::printf("runtime parity: %s", text.c_str());
        const wchar_t* worldReport = _wgetenv(L"MDS_PMM_WORLD_REPORT");
        if (worldReport && *worldReport) {
            const auto deadline = Clock::now() + std::chrono::seconds(15);
            std::string world;
            while (Clock::now() < deadline) {
                std::ifstream worldStream(worldReport);
                world.assign(std::istreambuf_iterator<char>{worldStream}, {});
                if (world.find("WORLD SUMMARY") != std::string::npos) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (world.find("WORLD SUMMARY") == std::string::npos ||
                world.find("mismatches=0") == std::string::npos ||
                world.find("supported=31") == std::string::npos ||
                world.find("WORLD MISMATCH") != std::string::npos)
                return 14;
        }
    }
#endif
    // The CPU batch preview also needs the model view. This selection is
    // independent of the optional legacy probe executable.
    if (parityExecutable.empty() && _wgetenv(L"MDS_RHI_CPU_PREVIEW")) {
        HWND radio=GetDlgItem(main,491);
        if(!radio)return 15;
        SendMessageW(radio,BM_SETCHECK,BST_CHECKED,0);
        SendMessageW(main,WM_COMMAND,MAKEWPARAM(491,BN_CLICKED),reinterpret_cast<LPARAM>(radio));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    if (!parityExecutable.empty()) {
        HWND radio = GetDlgItem(main, 491);
        RECT client{};
        if (!radio || !GetClientRect(main, &client) ||
            client.right < 500 || client.bottom < 400) return 15;
        SendMessageW(radio, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(main, WM_COMMAND, MAKEWPARAM(491, BN_CLICKED),
                     reinterpret_cast<LPARAM>(radio));
        if (modelMode) {
            const auto deadline = Clock::now() + std::chrono::seconds(25);
            const auto pixelsPath = captureDirectory / L"model_batch.same_frame";
            std::string pixels;
            while (Clock::now() < deadline &&
                   WaitForSingleObject(child.info.hProcess, 0) == WAIT_TIMEOUT) {
                std::ifstream input(pixelsPath);
                pixels.assign(std::istreambuf_iterator<char>{input}, {});
                if (pixels.find("status=") != std::string::npos) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Pixel diagnostics can finish before the history recorder. Wait
            // for its own boundary before copying evidence or checking the gate.
            if (_wgetenv(L"MDS_RHI_HISTORY_REPLAY")) {
                const bool portable = _wgetenv(L"MDS_RHI_PORTABLE_REPLAY") &&
                    wcscmp(_wgetenv(L"MDS_RHI_PORTABLE_REPLAY"), L"1") == 0;
                const bool dx11 = _wgetenv(L"MDS_RHI_PORTABLE_DX11_COMPARE") &&
                    wcscmp(_wgetenv(L"MDS_RHI_PORTABLE_DX11_COMPARE"), L"1") == 0;
                const auto historyDeadline = Clock::now() + std::chrono::seconds(dx11 ? 120 : 25);
                while (Clock::now() < historyDeadline && WaitForSingleObject(child.info.hProcess, 0) == WAIT_TIMEOUT) {
                    std::ifstream input(captureDirectory / L"history_replay.dx9");
                    const std::string text(std::istreambuf_iterator<char>{input}, {});
                    if (text.find(dx11 ? "DX11_GATE status=" : portable ? "PORTABLE_GATE status=" : "\nGATE status=") != std::string::npos) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            std::ifstream statesFile(captureDirectory / L"model_material.snapshot");
            const std::string states(std::istreambuf_iterator<char>{statesFile}, {});
            std::printf("model material snapshot:\n%s\nmodel pixel comparison: %s\n",
                        states.c_str(), pixels.c_str());
            if (const auto* evidence = _wgetenv(L"MDS_RHI_EVIDENCE_DIR")) {
                fs::create_directories(evidence);
                for (const auto& name : {L"d3d_create.attempts.txt", L"model_material.snapshot", L"model_batch.same_frame",
                                        L"model_toon.source", L"model_toon.cache", L"model_textures.source", L"shader_inputs.transport", L"real_batch.capture", L"history_live.rgba", L"history_probe.rgba", L"msaa_replay.prefix", L"history_replay.dx9", L"history_replay.before.bgra", L"history_replay.rebuilt.bgra", L"history_replay.actual_after.bgra", L"history_replay.replayed_after.bgra", L"history_replay.first_actual.bgra", L"history_replay.first_rebuilt.bgra", L"history_replay.reduction", L"history_replay.reduction.state", L"history_replay.reduction.vertices", L"history_replay.reduction.indices", L"history_replay.reduction.single0.bgra", L"history_replay.reduction.single1.bgra", L"history_replay.reduction.single2.bgra", L"history_replay.reduction.single3.bgra", L"history_replay.reduction.single4.bgra", L"history_replay.reduction.single5.bgra", L"history_replay.reduction.single6.bgra", L"history_replay.reduction.single7.bgra", L"history_replay.reduction.texture0", L"history_replay.reduction.texture1", L"history_replay.reduction.texture2"}) {
                    const auto file = captureDirectory / name;
                    if (fs::exists(file)) fs::copy_file(file, fs::path(evidence) / name,
                        fs::copy_options::overwrite_existing);
                }
            }
            if (const auto* evidence=_wgetenv(L"MDS_RHI_EVIDENCE_DIR")) {
                for(const auto& entry:fs::directory_iterator(captureDirectory)){
                    const auto name=entry.path().filename().wstring();
                    if(entry.is_regular_file()&&(name.find(L"history_replay.reduction.draw")==0 || name.find(L"history_replay.shadow_batch")==0 || name.find(L"history_replay.dx11_first_")==0 || name.find(L"history_replay.dx11_prefix.draw")==0 || name.find(L"history_replay.scene_prefix.")==0))
                        fs::copy_file(entry.path(),fs::path(evidence)/entry.path().filename(),fs::copy_options::overwrite_existing);
                }
            }
            if (_wgetenv(L"MDS_RHI_HISTORY_REPLAY")) {
                std::ifstream replayFile(captureDirectory / L"history_replay.dx9");
                const std::string replay(std::istreambuf_iterator<char>{replayFile}, {});
                std::printf("DX9 history replay (full state/resource log in history_replay.dx9):\n");
                std::size_t position=0;
                while(position<replay.size()){
                    const auto end=replay.find('\n',position);
                    const auto line=replay.substr(position,end==std::string::npos?std::string::npos:end-position);
                    if(line.find("BEGIN ")==0||line.find("PREFIX ")==0||line.find("GATE ")==0||line.find("CONTROL")==0||line.find("TRACE ")==0||line.find("PORTABLE")==0||line.find("DX11_")==0)
                        std::printf("%s\n",line.c_str());
                    if(end==std::string::npos)break;
                    position=end+1;
                }
                if (_wgetenv(L"MDS_RHI_PORTABLE_REPLAY") && wcscmp(_wgetenv(L"MDS_RHI_PORTABLE_REPLAY"),L"1")==0) {
                    // 31 is explicit conversion rejection, never graphical success.
                    if (replay.find("PORTABLE_GATE status=VALIDATED") == std::string::npos) return 31;
                    if (_wgetenv(L"MDS_RHI_PORTABLE_DX11_COMPARE") && wcscmp(_wgetenv(L"MDS_RHI_PORTABLE_DX11_COMPARE"),L"1")==0 &&
                        replay.find("DX11_GATE status=PASS") == std::string::npos) return 32;
                } else if (replay.find("BEGIN samples=8 quality=0") == std::string::npos ||
                    replay.find("PREFIX status=PASS") == std::string::npos ||
                    replay.find("beforeDifferentPixels=0 ") == std::string::npos ||
                    replay.find("GATE status=PASS afterDifferentPixels=0 ") == std::string::npos ||
                    replay.find("status=FAIL") != std::string::npos ||
                    replay.find("status=BLOCKED") != std::string::npos) return 30;
            }
            if (toonMode) {
                std::ifstream sourceFile(captureDirectory / L"model_toon.source");
                const std::string source(std::istreambuf_iterator<char>{sourceFile}, {});
                std::ifstream cacheFile(captureDirectory / L"model_toon.cache");
                const std::string cache(std::istreambuf_iterator<char>{cacheFile}, {});
                std::printf("Toon RGBA: %sToon cache: %s", source.c_str(), cache.c_str());
                unsigned long long uploads = 0, hits = 0;
                if (source.find("status=PASS differences=0 ") != 0 ||
                    pixels.find("ownedToon=1") == std::string::npos ||
                    std::sscanf(cache.c_str(), "uploads=%llu hits=%llu", &uploads, &hits) != 2 ||
                    uploads != (baseSphereMode ? 3u : sphereOnlyMode || nativePreparationMode ? 2u : 1u) ||
                    (modelOptinMode && hits < 1)) return 25;
            }
            if (materialMode) {
                if(_wgetenv(L"MDS_RHI_SHADER_INPUTS") && wcscmp(_wgetenv(L"MDS_RHI_SHADER_INPUTS"),L"1")==0) {
                    std::ifstream transportFile(captureDirectory/L"shader_inputs.transport");
                    const std::string transport(std::istreambuf_iterator<char>{transportFile},{});
                    if(transport.find("TRANSPORT status=PASS")==std::string::npos ||
                        transport.find("DIFFERENCE")!=std::string::npos || transport.find("status=FAIL")!=std::string::npos)return 29;
                }
                std::ifstream sourceFile(captureDirectory / L"model_textures.source");
                const std::string source(std::istreambuf_iterator<char>{sourceFile}, {});
                std::printf("Base/Sphere source comparisons:\n%s", source.c_str());
                if (source.find(nativePreparationMode ? "SUMMARY status=PASS" : "SUMMARY status=PARTIAL") == std::string::npos ||
                    source.find("failures=0") == std::string::npos ||
                    source.find(nativePreparationMode ? "blocked=0" : "blocked=1") == std::string::npos ||
                    (baseSphereMode && pixels.find("ownedBaseSphere=1,1") == std::string::npos) ||
                    (sphereOnlyMode && pixels.find("ownedBaseSphere=1,0") == std::string::npos)) return 27;
                if(nativePreparationMode && (source.find("preparedSize=128x128 preparedAccepted=1")==std::string::npos ||
                    source.find("differences=0 size=128x128 decodedSize=106x106")==std::string::npos ||
                    pixels.find("ownedBaseSphere=1,0")==std::string::npos))return 28;
                // This checks rejection of a known non-identical candidate,
                // not a particular native-device floating-point profile.
                if (preparationMode && (source.find("status=BLOCKED_PREPARATION") == std::string::npos ||
                    source.find("differences=0 size=128x128 decodedSize=106x106") != std::string::npos ||
                    source.find("size=128x128 decodedSize=106x106") == std::string::npos ||
                    source.find("preparedSize=128x128 preparedAccepted=0") == std::string::npos)) return 28;
            }
            if (states.find("SUMMARY status=PASS draws=0") != std::string::npos ||
                states.find("SUMMARY status=PASS") == std::string::npos ||
                states.find("mismatches=0") == std::string::npos ||
                pixels.find("status=PASS") != 0 ||
                pixels.find("differences=0 ") == std::string::npos ||
                (modelOptinMode && (pixels.find("actualAdapterApplied=1") == std::string::npos ||
                    pixels.find("actualDifferences=0") == std::string::npos))) return 24;
            temporary.passed = true;
            return 0;
        }
        ShowWindow(main, SW_RESTORE);
        SetForegroundWindow(main);
        const auto focusDeadline = Clock::now() + std::chrono::seconds(5);
        while (GetForegroundWindow() != main && Clock::now() < focusDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (GetForegroundWindow() != main) {
            std::fprintf(stderr, "cannot focus GUI for actual selection drag\n");
            return 15;
        }
        POINT start{client.right / 2, client.bottom / 3};
        POINT end{start.x + 54, start.y + 42};
        ClientToScreen(main, &start);
        ClientToScreen(main, &end);
        if (!SetCursorPos(start.x, start.y)) return 15;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        INPUT down{};
        down.type = INPUT_MOUSE;
        down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        if (SendInput(1, &down, sizeof(down)) != 1) return 15;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        SetCursorPos(end.x, end.y);
        const fs::path captured = captureDirectory / L"line_selection.bin";
        const fs::path count = captureDirectory / L"line_selection.count";
        const fs::path statePath = captureDirectory / L"line_selection.state";
        const fs::path livePath = captureDirectory / L"line_selection.live";
        const fs::path adapterLivePath =
            captureDirectory / L"line_selection.adapter.live";
        const fs::path abPath = captureDirectory / L"line_selection.ab";
        const fs::path sameFramePath =
            captureDirectory / L"line_selection.same_frame";
        const fs::path spriteSameFramePath =
            captureDirectory / L"sprite_overlay.same_frame";
        const fs::path textSameFramePath =
            captureDirectory / L"text_overlay.same_frame";
        const fs::path spriteCachePath =
            captureDirectory / L"sprite_overlay.cache";
        const fs::path textCachePath =
            captureDirectory / L"text_overlay.cache";
        const fs::path spriteSourcePath =
            captureDirectory / L"sprite_overlay.source";
        const fs::path textSourcePath =
            captureDirectory / L"text_overlay.source";
        const fs::path portableFontPath =
            captureDirectory / L"text_overlay.portable_source";
        const auto deadline = Clock::now() + std::chrono::seconds(8);
        while ((!fs::exists(captured) || !fs::exists(statePath) ||
                !fs::exists(livePath) ||
                (!optinMode && (!fs::exists(adapterLivePath) ||
                                !fs::exists(abPath) ||
                                !fs::exists(sameFramePath))) ||
                (spriteMode && !fs::exists(spriteSameFramePath)) ||
                (textMode && !fs::exists(textSameFramePath)) ||
                (spriteOptinMode && !fs::exists(spriteCachePath)) ||
                (textOptinMode && !fs::exists(textCachePath)) ||
                (spriteOptinMode && !fs::exists(spriteSourcePath)) ||
                (textOptinMode && !fs::exists(textSourcePath)) ||
                (portableFontMode && !fs::exists(portableFontPath))) &&
               Clock::now() < deadline &&
               WaitForSingleObject(child.info.hProcess, 0) == WAIT_TIMEOUT)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &up, sizeof(up));
        std::ifstream countStream(count);
        unsigned primitives = 0;
        countStream >> primitives;
        if (!fs::is_regular_file(captured) || fs::file_size(captured) != 160 ||
            primitives != 4) {
            std::fprintf(stderr, "real selection capture missing or invalid: %ls\n",
                         captured.c_str());
            return 16;
        }
        std::ifstream stateStream(statePath);
        const std::string state(std::istreambuf_iterator<char>{stateStream}, {});
        std::printf("app selection draw: %s", state.c_str());
        std::ifstream spriteStateStream(
            captureDirectory / L"sprite_overlay.state");
        const std::string spriteState(
            std::istreambuf_iterator<char>{spriteStateStream}, {});
        std::printf("app sprite draw: %s", spriteState.c_str());
        std::ifstream liveStream(livePath);
        const std::string live(std::istreambuf_iterator<char>{liveStream}, {});
        std::printf("app live pixel/bridge: %s", live.c_str());
        std::ifstream adapterLiveStream(adapterLivePath);
        const std::string adapterLive(
            std::istreambuf_iterator<char>{adapterLiveStream}, {});
        std::ifstream abStream(abPath);
        const std::string ab(std::istreambuf_iterator<char>{abStream}, {});
        std::ifstream sameFrameStream(sameFramePath);
        const std::string sameFrame(
            std::istreambuf_iterator<char>{sameFrameStream}, {});
        std::ifstream spriteSameFrameStream(spriteSameFramePath);
        const std::string spriteSameFrame(
            std::istreambuf_iterator<char>{spriteSameFrameStream}, {});
        std::ifstream textSameFrameStream(textSameFramePath);
        const std::string textSameFrame(
            std::istreambuf_iterator<char>{textSameFrameStream}, {});
        std::ifstream spriteCacheStream(spriteCachePath);
        const std::string spriteCache(
            std::istreambuf_iterator<char>{spriteCacheStream}, {});
        std::ifstream textCacheStream(textCachePath);
        const std::string textCache(
            std::istreambuf_iterator<char>{textCacheStream}, {});
        std::ifstream spriteSourceStream(spriteSourcePath);
        const std::string spriteSource(
            std::istreambuf_iterator<char>{spriteSourceStream}, {});
        std::ifstream textSourceStream(textSourcePath);
        const std::string textSource(
            std::istreambuf_iterator<char>{textSourceStream}, {});
        std::ifstream portableFontStream(portableFontPath);
        const std::string portableFont(
            std::istreambuf_iterator<char>{portableFontStream}, {});
        if (!optinMode) {
            std::printf("app adapter pixel/bridge: %s", adapterLive.c_str());
            std::printf("app fixed-selection A/B: %s", ab.c_str());
            std::printf("app same-frame target A/B: %s", sameFrame.c_str());
        }
        if (spriteMode)
            std::printf("app sprite same-frame A/B: %s",
                        spriteSameFrame.c_str());
        if (textMode)
            std::printf("app text same-frame A/B: %s",
                        textSameFrame.c_str());
        if (spriteOptinMode)
            std::printf("app sprite RHI cache: %s", spriteCache.c_str());
        if (textOptinMode)
            std::printf("app text RHI cache: %s", textCache.c_str());
        if (spriteOptinMode)
            std::printf("app sprite RGBA source: %s", spriteSource.c_str());
        if (textOptinMode)
            std::printf("app text RGBA source: %s", textSource.c_str());
        if (portableFontMode)
            std::printf("app portable font comparison: %s",
                        portableFont.c_str());
        unsigned targetWidth = 0, targetHeight = 0;
        const bool targetKnown = std::sscanf(state.c_str(),
            "target=%ux%u", &targetWidth, &targetHeight) == 2 &&
            targetWidth > 0 && targetHeight > 0;
        const auto expectedPixels = std::string("status=PASS pixels=") +
            std::to_string(static_cast<std::uint64_t>(targetWidth) *
                           targetHeight) + " differences=0 ";
        if ((optinMode &&
             live.find("status=PASS path=legacy adapterApplied=1 ") != 0) ||
            !targetKnown ||
            (spriteOptinMode && spriteCache != "uploads=1 hits=1\n") ||
            (textOptinMode && textCache != "uploads=1 hits=1\n") ||
            (spriteOptinMode &&
             spriteSource.find("status=PASS differences=0 ") != 0) ||
            (textOptinMode &&
             textSource.find("status=PASS differences=0 ") != 0) ||
            (portableFontMode &&
             (portableFont.find("status=COMPARED differentChannels=") != 0 ||
              portableFont.find("differentChannels=0 ") !=
                  std::string::npos)) ||
            (spriteMode &&
             (spriteSameFrame.find(expectedPixels) != 0 ||
              (spriteOptinMode &&
               spriteSameFrame.find("actualAdapterApplied=1 ") ==
                   std::string::npos) ||
              spriteSameFrame.find("adapterApplied=1 readback=1") ==
                  std::string::npos)) ||
            (textMode &&
             (textSameFrame.find(expectedPixels) != 0 ||
              (textOptinMode &&
               textSameFrame.find("actualAdapterApplied=1 ") ==
                   std::string::npos) ||
              textSameFrame.find("adapterApplied=1 readback=1") ==
                  std::string::npos)) ||
            (!optinMode &&
             (adapterLive.find(
                 "status=PASS path=adapter adapterApplied=1 ") != 0 ||
              sameFrame.find(expectedPixels) != 0 ||
              sameFrame.find("adapterApplied=1 readback=1 ") ==
                  std::string::npos ||
              ab.find("bridgeCalls=1 hostCalls=1") ==
                  std::string::npos))) {
            std::fprintf(stderr, "in-app legacy/adapter A/B comparison failed\n");
            return 19;
        }
        unsigned immediateInside = 0, visibleBoth = 0, visibleControl = 0;
        unsigned visibilityReady = 0;
        unsigned bridgeCalls = 0, hostCalls = 0;
        const auto insideAt = live.find("immediateInside=");
        const auto visibilityAt = live.find("visibility=");
        const auto visibilityReadyAt = live.find("visibilityReady=");
        const auto bridgeAt = live.find("bridgeCalls=");
        const auto hostAt = live.find("hostCalls=");
        if (live.find("status=PASS ") != 0 ||
            insideAt == std::string::npos ||
            visibilityAt == std::string::npos ||
            visibilityReadyAt == std::string::npos ||
            bridgeAt == std::string::npos || hostAt == std::string::npos ||
            std::sscanf(live.c_str() + insideAt,
                        "immediateInside=%u", &immediateInside) != 1 ||
            std::sscanf(live.c_str() + visibilityAt,
                        "visibility=%u,%*u,%*u,%*u,%*u,%u",
                        &visibleBoth, &visibleControl) != 2 ||
            std::sscanf(live.c_str() + visibilityReadyAt,
                        "visibilityReady=%u", &visibilityReady) != 1 ||
            std::sscanf(live.c_str() + bridgeAt, "bridgeCalls=%u", &bridgeCalls) != 1 ||
            std::sscanf(live.c_str() + hostAt, "hostCalls=%u", &hostCalls) != 1 ||
            (immediateInside == 0 &&
             !(visibilityReady == 1 && visibleBoth == 0 &&
               visibleControl > 0)) ||
            bridgeCalls != 1 || hostCalls != 1) {
            std::fprintf(stderr, "live target or MME bridge comparison failed\n");
            return 19;
        }
        const auto field = [&state](const char* name) -> std::optional<unsigned> {
            const std::string needle = std::string(name) + "=";
            const auto position = state.find(needle);
            unsigned value = 0;
            if (position == std::string::npos ||
                std::sscanf(state.c_str() + position + needle.size(),
                            "%u", &value) != 1) return std::nullopt;
            return value;
        };
        const auto fvf = field("fvf"), offset = field("offset");
        const auto stride = field("stride"), texture = field("texture");
        unsigned targetFormat = 0;
        unsigned viewX = 0, viewY = 0, viewWidth = 0, viewHeight = 0;
        if (!fvf || *fvf != 0x44 || !offset || *offset != 0 ||
            !stride || *stride != 20 || !texture || *texture != 0 ||
            std::sscanf(state.c_str(),
                "target=%ux%u format=%u viewport=%u,%u,%u,%u",
                &targetWidth, &targetHeight, &targetFormat,
                &viewX, &viewY, &viewWidth, &viewHeight) != 7 ||
            !viewWidth || !viewHeight || viewX >= targetWidth ||
            viewY >= targetHeight || viewWidth > targetWidth ||
            viewHeight > targetHeight) {
            std::fprintf(stderr, "selection draw state is missing or incompatible\n");
            return 18;
        }
        const bool statesMatch =
            field("lighting") == 1u && field("cull") == 3u &&
            field("depth") == 1u && field("depthWrite") == 1u &&
            field("depthFunc") == 4u &&
            field("blend") == 1u && field("source") == 5u &&
            field("destination") == 6u && field("colorOp") == 4u &&
            field("colorArg") == 2u && field("alphaOp") == 4u &&
            field("alphaArg") == 2u && field("alphaArg2") == 1u &&
            field("alphaTest") == 1u && field("alphaFunc") == 7u &&
            field("alphaRef") == 1u && field("stencil") == 1u &&
            field("stencilFunc") == 8u && field("stencilRef") == 1u &&
            field("stencilPass") == 3u;
        std::printf("live app vs adapter pipeline state=%s; target=%ux%u format=%u; "
                    "live in-app replacement remains disabled\n",
                    statesMatch ? "match" : "MISMATCH", targetWidth,
                    targetHeight, targetFormat);
        if (!statesMatch) return 18;
        std::wstring command = L"\"" + parityExecutable.wstring() +
                               L"\" \"" + captured.wstring() +
                               L"\" \"" + statePath.wstring() + L"\"";
        std::vector<wchar_t> arguments(command.begin(), command.end());
        arguments.push_back(0);
        STARTUPINFOW parityStartup{};
        parityStartup.cb = sizeof(parityStartup);
        PROCESS_INFORMATION parity{};
        if (!CreateProcessW(parityExecutable.c_str(), arguments.data(),
                            nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &parityStartup, &parity)) return 17;
        const DWORD wait = WaitForSingleObject(parity.hProcess, 30000);
        DWORD result = 1;
        if (wait == WAIT_TIMEOUT) TerminateProcess(parity.hProcess, 1);
        else if (wait == WAIT_OBJECT_0) GetExitCodeProcess(parity.hProcess, &result);
        CloseHandle(parity.hThread);
        CloseHandle(parity.hProcess);
        std::printf("app selection batch=%ls parity exit=%lu\n",
                    captured.c_str(), result);
        if (result == 0) temporary.passed = true;
        return result == 77 ? 77 : result == 0 ? 0 : 17;
    }
    if(_wgetenv(L"MDS_RHI_CPU_GEOMETRY")) {
        if(_wgetenv(L"MDS_RHI_CPU_EMPTY_BACKGROUND_SCREEN")){
            // Explicitly scoped test configuration, selected through the real
            // menu. Bound background/screen resources still fail the CPU gate.
            SendMessageW(main,WM_COMMAND,246,0);
            std::puts("CPU_LIVE screenSource=background; native dynamic screen capture is outside this test");
        }
        if(!CpuLiveCheckpoint(main,child.info.dwProcessId,"loaded_edited"))return 34;
        // Exercise actual painted frames, in addition to the synchronous
        // runtime/session comparisons above. Leave the saved scene at frame 0.
        for(unsigned frame=0;frame<10;++frame){
            SendMessageW(main,WM_COMMAND,MAKEWPARAM(419,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(main,419)));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if((frame==0 || frame==4) && !CpuLiveCheckpoint(main,child.info.dwProcessId,frame==0?"seek1":"seek5"))return 34;
        }
        for(unsigned frame=0;frame<10;++frame){
            SendMessageW(main,WM_COMMAND,MAKEWPARAM(418,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(main,418)));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        if(_wgetenv(L"MDS_RHI_CPU_PLAY")) {
            std::puts("CPU_PLAY configure");std::fflush(stdout);
            const bool live=_wgetenv(L"MDS_RHI_CPU_LIVE_HISTORY")!=nullptr;
            SetWindowTextW(GetDlgItem(main,409),L"0");SetWindowTextW(GetDlgItem(main,410),live?L"9000":L"300");
            SendMessageW(main,WM_COMMAND,MAKEWPARAM(408,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(main,408)));
            if(!CpuLiveCheckpoint(main,child.info.dwProcessId,"playing"))return 34;
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            std::puts("CPU_PLAY stop");std::fflush(stdout);
            SendMessageW(main,WM_COMMAND,MAKEWPARAM(408,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(main,408)));
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            wchar_t current[32]{};GetWindowTextW(GetDlgItem(main,417),current,32);
            const auto stoppedFrame=std::wcstol(current,nullptr,10);
            if(stoppedFrame<0 || stoppedFrame>(live?9000:300))return 33;
            if(live){
                // Use the editor's actual frame-entry commit rather than
                // issuing thousands of individual steps after a slow capture.
                const auto edit=GetDlgItem(main,417);SetWindowTextW(edit,L"0");
                SendMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
            }
            for(long frame=0;!live && frame<stoppedFrame;++frame){
                SendMessageW(main,WM_COMMAND,MAKEWPARAM(418,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(main,418)));
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            SetWindowTextW(GetDlgItem(main,410),L"0");
            std::puts("CPU_PLAY stopped");std::fflush(stdout);
            if(!CpuLiveCheckpoint(main,child.info.dwProcessId,"stopped_before_save"))return 34;
        }
    }
    PostMessageW(main, WM_COMMAND, 0xD0, 0);
    if (!WaitForDialog(child.info.hProcess, child.info.dwProcessId,
                       dialog, true, 20) ||
        !SetFileName(automation.Get(), dialog, saved.wstring())) {
        std::fprintf(stderr, "save-as dialog failed\n");
        return 7;
    }
    PostMessageW(ActionButton(dialog), BM_CLICK, 0, 0);
    const auto saveDeadline = Clock::now() + std::chrono::seconds(40);
    while (Clock::now() < saveDeadline) {
        if (WaitForSingleObject(child.info.hProcess, 0) != WAIT_TIMEOUT) break;
        if (fs::exists(saved) &&
            SavedSceneHasModels(saved, source.modelCount, expectedEdit)) {
#ifdef MDS_EXPECT_RUNTIME_PARITY
            {
                std::ifstream diagnostic(parityReport);
                const std::string text(std::istreambuf_iterator<char>{diagnostic}, {});
                if (text.find("CAMERA CHAIN stage=writer") == std::string::npos ||
                    text.find("valid=0") != std::string::npos) {
                    std::fprintf(stderr,
                        "camera chain was invalid at the Windows writer:\n%s\n",
                        text.c_str());
                    return 13;
                }
                const auto at = text.find("CAMERA CHAIN stage=writer");
                std::printf("%s\n", text.substr(at, text.find('\n', at) - at).c_str());
            }
#endif
            std::wstring reopenCommand = L"\"" + executable.wstring() +
                L"\" \"" + saved.wstring() + L"\"";
            std::vector<wchar_t> reopenBuffer(reopenCommand.begin(),
                                              reopenCommand.end());
            reopenBuffer.push_back(0);
            ChildProcess reopened;
            if (!SetEnvironmentVariableW(L"MDS_PMM_V2_SELECTOR_FIXTURE",
                                          saved.c_str()) ||
                !SetEnvironmentVariableW(L"MDS_PMM_V2_SELECTOR_REPORT",
                                          reopenedReport.c_str()) ||
                !SetEnvironmentVariableW(L"MDS_PMM_V2_ACCESSORY_FIXTURE",
                                          L"1")) return 11;
            if (!CreateProcessW(executable.c_str(), reopenBuffer.data(),
                                nullptr, nullptr, FALSE, 0, nullptr,
                                workingDirectory.c_str(), &startup,
                                &reopened.info)) return 11;
            if(!reopened.Own())return 11;
            bool v2Loaded = false;
            const auto deadline = Clock::now() + std::chrono::seconds(45);
            while (Clock::now() < deadline) {
                const auto windows = Snapshot(reopened.info.dwProcessId);
                if (WaitForSingleObject(reopened.info.hProcess, 0) != WAIT_TIMEOUT) {
                    DWORD exitCode=0;GetExitCodeProcess(reopened.info.hProcess,&exitCode);
                    std::fprintf(stderr,"v2 application exited during load: 0x%08lx\n",exitCode);break;
                }
                if (!windows.dialogs.empty()) {
                    for(auto w:windows.dialogs){std::fprintf(stderr,"v2 load dialog: %ls\n",DialogMessage(w).c_str());EnumChildWindows(w,PrintChild,0);}
                    break;
                }
                DWORD_PTR modelCount = 0;
                if (windows.main && GetDlgItem(windows.main, 436) &&
                    SendMessageTimeoutW(GetDlgItem(windows.main, 436),
                        CB_GETCOUNT, 0, 0, SMTO_ABORTIFHUNG, 1000,
                        &modelCount) &&
                    modelCount >= static_cast<DWORD_PTR>(source.modelCount + 1) &&
                    fs::exists(reopenedReport)) {
                    v2Loaded = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!v2Loaded) {
                std::fprintf(stderr, "saved v2 scene did not reopen in GUI\n");
                return 12;
            }
            std::ifstream diagnostic(reopenedReport);
            const std::string diagnosticText(
                std::istreambuf_iterator<char>{diagnostic}, {});
            if (diagnosticText.find("PASS checks=") == std::string::npos ||
                diagnosticText.find("FAIL") != std::string::npos) {
                std::fprintf(stderr, "v2 accessory report:\n%s\n",
                             diagnosticText.c_str());
                return 13;
            }
            const auto reopenedMain=Snapshot(reopened.info.dwProcessId).main;
            if(_wgetenv(L"MDS_RHI_CPU_EMPTY_BACKGROUND_SCREEN"))SendMessageW(reopenedMain,WM_COMMAND,246,0);
            if(!CpuLiveCheckpoint(reopenedMain,reopened.info.dwProcessId,"v2_loaded_edited"))return 34;
            if(_wgetenv(L"MDS_RHI_CPU_LIVE_HISTORY")) {
                SendMessageW(reopenedMain,WM_COMMAND,MAKEWPARAM(419,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(reopenedMain,419)));
                if(!CpuLiveCheckpoint(reopenedMain,reopened.info.dwProcessId,"v2_seek"))return 34;
                SetWindowTextW(GetDlgItem(reopenedMain,409),L"0");SetWindowTextW(GetDlgItem(reopenedMain,410),L"9000");
                SendMessageW(reopenedMain,WM_COMMAND,MAKEWPARAM(408,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(reopenedMain,408)));
                if(!CpuLiveCheckpoint(reopenedMain,reopened.info.dwProcessId,"v2_playing"))return 34;
                SendMessageW(reopenedMain,WM_COMMAND,MAKEWPARAM(408,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(reopenedMain,408)));
                if(!CpuLiveCheckpoint(reopenedMain,reopened.info.dwProcessId,"v2_stopped"))return 34;
            }
            std::puts("relocated model selected; PMM saved, read back, and reopened");
            temporary.passed = true;
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::fprintf(stderr, "saved PMM missing or failed readback: %ls\n",
                 saved.c_str());
    return 8;
}
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* fixture = _wgetenv(L"MDS_PMM_GUI_FIXTURE");
    const wchar_t* model = _wgetenv(L"MDS_PMM_RELOCATE_MODEL");
    if ((argc != 2 && argc != 3) || !fixture || !*fixture ||
        !model || !*model) return 77;
    try {
        return Run(argv[1], fixture, model, argc == 3 ? fs::path(argv[2]) : fs::path{});
    } catch (const fs::filesystem_error& error) {
        std::fprintf(stderr, "fixture operation failed: %s\n", error.what());
        return 9;
    }
}
