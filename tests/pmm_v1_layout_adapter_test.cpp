#include "pmm_format_win.hpp"
#include "pmm_v1_layout_win.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <memory>
#include <vector>

namespace {
bool ResolveSessionModel(void* context, std::uint8_t,
                         const std::array<char, 256>& rawPath,
                         mikudancestudio::format::ModelDocument& model) {
    auto& adapter = *static_cast<mikudancestudio::PmmV1FileLayoutContext*>(context);
    char path[257]{};
    std::memcpy(path, rawPath.data(), rawPath.size());
    wchar_t resolved[256]{};
    if (!mikudancestudio::ResolveAnsiUserFile(
            adapter.renderer, path,
            resolved, 256, *adapter.paths)) return false;
    int fd = -1;
    if (_wsopen_s(&fd, resolved, _O_BINARY | _O_RDONLY,
                  _SH_DENYNO, _S_IREAD) != 0) return false;
    const bool loaded = mikudancestudio::format::ReadModelDocument(
        mikudancestudio::ReadPmmFileBytesResult, &fd, model);
    _close(fd);
    return loaded;
}
bool Append(void* context, const void* data, std::size_t length) {
    auto& bytes = *static_cast<std::vector<std::uint8_t>*>(context);
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + length);
    return true;
}
}
#include <share.h>
#include <sys/stat.h>

int main() {
    using namespace mikudancestudio;
    const wchar_t* path = _wgetenv(L"MDS_PMM_FIXTURE");
    if (!path || !*path) return 77;
    int fd = -1;
    if (_wsopen_s(&fd, path, _O_BINARY | _O_RDONLY,
                  _SH_DENYNO, _S_IREAD) != 0) return 1;
    format::PmmHeader header;
    if (format::ReadPmmHeader(ReadPmmFileBytes, &fd, header) !=
        format::PmmHeaderStatus::Ok) return 1;
    if (header.version != format::PmmVersion::V1) {
        _close(fd);
        return 77;
    }
    if (_lseeki64(fd, 0, SEEK_SET) != 0) return 1;
    auto renderer = std::make_unique<D3DRenderer>();
    PathResolutionWorkspace paths{};
    PathToProjectDir(paths.projectDirectory, path);
    if (!paths.projectDirectory[0]) return 77;
    wcscpy_s(paths.executableDirectory, paths.projectDirectory);
    PmmV1FileLayoutContext adapter{renderer.get(), &paths};
    format::PmmSceneDocument scene;
    const bool parsed = format::ReadPmmSceneDocument(
        ReadPmmFileBytesResult, &fd, ResolvePmmV1FileLayout,
        &adapter, scene);
    _close(fd);
    if (const char* expectMissing = std::getenv("MDS_PMM_EXPECT_MISSING")) {
        if (std::strcmp(expectMissing, "1") == 0)
            return !parsed && adapter.modelUnavailable ? 0 : 5;
    }
    if (!parsed || adapter.modelUnavailable ||
        scene.envelope.header.version != format::PmmVersion::V1 ||
        scene.models.empty()) return 2;
    std::printf("v1 accessories=%zu\n", scene.accessories.size());
    for (const auto& model : scene.models)
        if (model.layout.boneCount == 0 ||
            model.boneKeys.dense.size() != model.layout.boneCount) return 3;
    if (_wsopen_s(&fd, path, _O_BINARY | _O_RDONLY,
                  _SH_DENYNO, _S_IREAD) != 0) return 6;
    format::SceneSession session;
    const bool sessionLoaded = format::SceneSession::Load(
        ReadPmmFileBytesResult, &fd, ResolveSessionModel, &adapter, session);
    _close(fd);
    if (!sessionLoaded || session.Document().models.size() != scene.models.size() ||
        !session.Seek(0) || !session.StartPlayback() ||
        !session.Advance(5.0f / 30.0f)) return 6;
    for (const auto& model : session.Document().models) {
        std::vector<std::optional<std::array<float, 16>>> world;
        if (!session.KinematicWorld(model.slot, world) ||
            world.size() != model.layout.boneCount) return 7;
    }
    for (const auto& accessory : session.Pose().accessories)
        if (accessory.parentModel < 0) {
            format::PmmWorldMatrix world{};
            if (!session.AccessoryWorld(accessory.slot, world)) return 8;
        }
    std::vector<std::uint8_t> saved;
    if (!session.Save(Append, &saved) || saved.empty()) return 6;
    std::array<char, 256> missing{};
    constexpr char kMissing[] = "UserFile\\Model\\missing-layout-test.pmd";
    std::memcpy(missing.data(), kMissing, sizeof kMissing);
    format::PmmModelLayout ignored;
    PmmV1FileLayoutContext unavailable{renderer.get(), &paths};
    if (ResolvePmmV1FileLayout(&unavailable, 0, {}, missing, ignored) ||
        !unavailable.modelUnavailable) return 4;
    return 0;
}
