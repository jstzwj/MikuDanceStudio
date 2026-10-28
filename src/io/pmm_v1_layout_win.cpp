#include "pmm_v1_layout_win.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "pmm_format_win.hpp"
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

namespace mikudancestudio {
namespace {
int ReadModelBytes(void* context, void* destination, std::size_t length) {
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(length));
}
}
bool ResolvePmmV1FileLayout(void* context, std::uint8_t, const std::array<char, 20>&,
                            const std::array<char, 256>& rawPath, format::PmmModelLayout& layout) {
    auto& adapter = *static_cast<PmmV1FileLayoutContext*>(context);
    if (!adapter.paths || !adapter.renderer) {
        adapter.modelUnavailable = true;
        return false;
    }
    char path[257]{};
    std::memcpy(path, rawPath.data(), rawPath.size());
    if (std::strlen(path) < 4) {
        adapter.modelUnavailable = true;
        return false;
    }
    wchar_t resolved[256]{};
    if (!ResolveAnsiUserFile(adapter.renderer, path, resolved, 256, *adapter.paths)) {
        adapter.modelUnavailable = true;
        return false;
    }
    int fd = -1;
    if (_wsopen_s(&fd, resolved, _O_BINARY | _O_RDONLY, _SH_DENYNO, _S_IREAD) != 0) {
        adapter.modelUnavailable = true;
        return false;
    }
    format::ModelDocument model;
    const bool read = format::ReadModelDocument(ReadModelBytes, &fd, model);
    _close(fd);
    if (!read || model.kind != format::ModelKind::Pmd) {
        adapter.modelUnavailable = true;
        return false;
    }
    layout.boneCount = static_cast<std::uint32_t>(model.pmd.bones.size());
    layout.morphCount = static_cast<std::uint32_t>(model.pmd.morphs.size());
    layout.ikCount = static_cast<std::uint32_t>(model.pmd.ikChains.size());
    return true;
}
bool LoadPmmV1RuntimeSession(MMDApp* app, format::SceneSession& session) {
    if (!app || !app->state.windowLayoutReady)
        return false;
    format::SceneResourceResolver resources(app->EnvFileName());
    for (int slot = 0; slot < kModelSlotCount; ++slot)
        if (const auto* raw = app->ModelSlot(slot))
            resources.ReselectModel(static_cast<std::uint8_t>(slot), raw->path);
    int fd = -1;
    if (_wsopen_s(&fd, app->EnvFileName(), _O_BINARY | _O_RDONLY, _SH_DENYNO, _S_IREAD) != 0)
        return false;
    const bool loaded = format::SceneSession::Load(
        ReadPmmFileBytesResult, &fd, format::SceneResourceResolver::Resolve, &resources, session);
    _close(fd);
    if (!loaded || session.Document().envelope.header.version != format::PmmVersion::V1 ||
        session.Document().models.empty())
        return false;
    for (const auto& model : session.Document().models)
        if (model.slot >= kModelSlotCount || !app->ModelSlot(model.slot))
            return false;
    return true;
}
}
