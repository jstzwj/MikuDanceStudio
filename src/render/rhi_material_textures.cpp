#include "mikudancestudio/rhi_material_textures.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <utility>

namespace mikudancestudio::rhi {
bool PrepareMaterialMipChain(MaterialTextureAsset& asset, MipPreparationPolicy policy,
                             TexturePreparer* backend, bool sceneActive) {
    auto& source = asset.usePrepared ? asset.prepared : asset.content;
    source.image.pixels.Seal();
    if (!source.revision || source.revision != ImageRevision(source.image))
        return false;
    PreparedMipChain chain;
    const bool ready = policy == MipPreparationPolicy::Dx9AutoLinear
                           ? backend && backend->SupportsMipPolicy(policy) &&
                                 backend->PrepareMips(source.image, policy, chain, sceneActive)
                           : PreparePortableMipChain(source.image, policy, chain);
    if (!ready || !ValidatePreparedMipChain(source.image, chain))
        return false;
    asset.mipChain = std::move(chain);
    return true;
}
namespace {
std::filesystem::path Normalized(std::filesystem::path path) {
    auto text = path.generic_u8string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return std::filesystem::u8path(text).lexically_normal();
}
bool File(const std::filesystem::path& path, AtlasAsset& asset) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return false;
    const auto size = stream.tellg();
    if (size <= 0 || size > 16 * 1024 * 1024)
        return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size) ||
        !DecodeTextureRgba(bytes.data(), bytes.size(), asset.image))
        return false;
    asset.revision = ImageRevision(asset.image);
    return true;
}
}
MaterialResourceStatus LoadMaterialTexture(const std::filesystem::path& directory,
                                           const std::filesystem::path& reference,
                                           MaterialTextureAsset& asset) {
    asset = {};
    if (reference.empty())
        return MaterialResourceStatus::Absent;
    const auto path = Normalized(reference);
    asset.sourcePath = Normalized(path.is_absolute() ? path : directory / path);
    std::error_code error;
    const auto absolute = std::filesystem::absolute(asset.sourcePath, error);
    if (!error)
        asset.sourcePath = absolute.lexically_normal();
    asset.identity = "file:" + asset.sourcePath.generic_u8string();
    if (!File(asset.sourcePath, asset.content))
        return MaterialResourceStatus::Missing;
    asset.origin = TextureOrigin::ModelFile;
    return MaterialResourceStatus::Ready;
}
bool BuildMaterialTextureLayout(bool pmd, const std::filesystem::path& base,
                                const std::filesystem::path& sphere, unsigned mode,
                                MaterialTextureLayout& layout) {
    layout = {};
    MaterialTextureLayout result;
    result.stages[0] = {MaterialTextureRole::Toon, MaterialCoordinates::CameraNormal,
                        MaterialCombine::Modulate};
    result.stages[0].colorArg2 = MaterialColorSource::LitDiffuse;
    const auto operation = [](const std::filesystem::path& path) {
        const auto text = path.generic_u8string();
        // Preserve the legacy contains checks, including its exact lower/upper
        // spellings. Mixed-case .Sph is a base texture in that path.
        return text.find(".sph") != std::string::npos || text.find(".SPH") != std::string::npos
                   ? SphereOperation::Multiply
               : text.find(".spa") != std::string::npos || text.find(".SPA") != std::string::npos
                   ? SphereOperation::Add
                   : SphereOperation::Disabled;
    };
    const auto sphereInput = [](SphereOperation op) {
        return MaterialTextureInput{
            MaterialTextureRole::Sphere,
            op == SphereOperation::SubTexture ? MaterialCoordinates::MeshUv1
                                              : MaterialCoordinates::CameraNormal,
            op == SphereOperation::Add ? MaterialCombine::Add : MaterialCombine::Modulate};
    };
    if (!pmd && mode > 3)
        return false;
    if (!base.empty()) {
        const auto op = pmd ? operation(base) : SphereOperation::Disabled;
        if (op != SphereOperation::Disabled) {
            result.sphere = op;
            result.stages[1] = sphereInput(op);
        } else
            result.stages[1] = {MaterialTextureRole::Base, MaterialCoordinates::MeshUv0,
                                MaterialCombine::Modulate};
    }
    if (!sphere.empty() && (pmd || mode)) {
        // PMD's separate sphere field adds unless its suffix is .sph, exactly
        // like the legacy path (including unusual non-.spa suffixes).
        const auto op =
            pmd ? (operation(sphere) == SphereOperation::Multiply ? SphereOperation::Multiply
                                                                  : SphereOperation::Add)
                : static_cast<SphereOperation>(mode);
        result.sphere = op;
        const unsigned stage = !pmd && base.empty() ? 1 : 2;
        result.stages[stage] = sphereInput(op);
    }
    layout = result;
    return true;
}
ToonTextureResolver::ToonTextureResolver(std::filesystem::path sharedDirectory,
                                         const AssetSource& builtin)
    : sharedDirectory_(Normalized(std::move(sharedDirectory))), builtin_(builtin) {
}
bool ToonTextureResolver::Load(const ToonRequest& request, MaterialTextureAsset& asset) const {
    asset = {};
    unsigned slot = 0;
    if (request.kind == ToonKind::ModelFile) {
        if (LoadMaterialTexture(request.modelDirectory, request.path, asset) ==
            MaterialResourceStatus::Ready)
            return true;
        if (request.missing == MissingToonPolicy::Unbound) {
            asset = {};
            return false;
        }
        asset.fallback = true;
    } else if (request.kind == ToonKind::Shared) {
        slot = request.sharedSlot;
        if (!slot || slot > 10)
            return false;
        char name[16]{};
        std::snprintf(name, sizeof(name), "toon%02u.bmp", slot);
        asset.sourcePath = sharedDirectory_ / name;
        asset.identity = "shared-toon:" + std::to_string(slot);
        if (File(asset.sourcePath, asset.content)) {
            asset.origin = TextureOrigin::SharedFile;
            return true;
        }
        asset.fallback = true;
    }
    asset.identity = "shared-toon:" + std::to_string(slot);
    asset.sourcePath.clear();
    if (!LoadAtlas(builtin_, static_cast<AssetId>(0x67 + slot), asset.content)) {
        asset = {};
        return false;
    }
    asset.origin = TextureOrigin::Builtin;
    return true;
}
Handle MaterialTextureCache::Get(const MaterialTextureAsset& asset) {
    // This legacy top-level upload cache cannot silently discard explicit levels.
    // Full model textures use SceneRenderer::CreateModelTexture instead.
    if (asset.mipChain)
        return {};
    const auto status = device_->Status();
    if (status.state != DeviceState::Ready || !status.identity || !status.epoch)
        return {};

    const auto& content = asset.usePrepared ? asset.prepared : asset.content;
    const auto& image = content.image;
    if (asset.identity.empty() || !content.revision || !image.width || !image.height ||
        image.width > 4096 || image.height > 4096 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4)
        return {};
    Entry* old = nullptr;
    for (auto& entry : entries_)
        if (entry.identity == asset.identity) {
            const auto residency =
                ClassifyTexture(status, entry.uploaded, device_->ResourceLifecycleFeatures());
            if (residency == TextureResidency::Reupload ||
                residency == TextureResidency::DeviceReplaced)
                entry.texture = {}; // backend already invalidated the old handle
            else
                entry.uploaded = status;
            if (entry.texture && entry.revision == content.revision && entry.width == image.width &&
                entry.height == image.height && entry.mips == asset.mips) {
                ++hits_;
                return entry.texture;
            }
            old = &entry;
            break;
        }
    const auto texture =
        device_->CreateTexture({image.width, image.height, TextureFormat::Rgba8, asset.mips});
    if (!texture)
        return {};
    if (!device_->UploadTexture(texture,
                                {image.pixels.data(), image.width * 4u, image.pixels.size()})) {
        device_->Destroy(texture);
        return {};
    }
    if (old) {
        if (old->texture && !device_->Destroy(old->texture)) {
            device_->Destroy(texture);
            return {};
        }
        *old = {asset.identity, texture,      content.revision, status,
                image.width,    image.height, asset.mips};
    } else
        entries_.push_back({asset.identity, texture, content.revision, status, image.width,
                            image.height, asset.mips});
    ++uploads_;
    return texture;
}
bool MaterialTextureCache::Rebind(Device& device) {
    if (device_ == &device)
        return true;
    const auto status = device_->Status();
    if (status.state != DeviceState::Ready)
        return false;
    for (auto& entry : entries_) {
        const auto residency =
            ClassifyTexture(status, entry.uploaded, device_->ResourceLifecycleFeatures());
        if (residency == TextureResidency::Reupload ||
            residency == TextureResidency::DeviceReplaced)
            entry.texture = {};
        if (entry.texture && !device_->Destroy(entry.texture))
            return false;
        entry.texture = {};
    }
    entries_.clear();
    device_ = &device;
    return true;
}
void MaterialTextureCache::Invalidate() {
    for (const auto& entry : entries_)
        device_->Destroy(entry.texture);
    entries_.clear();
}
}
