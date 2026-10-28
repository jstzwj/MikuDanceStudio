#include "mikudancestudio/rhi_assets.hpp"
#include <fstream>
#include <limits>
#include <utility>

namespace mikudancestudio::rhi {
namespace {
const char* AssetName(AssetId id) {
    constexpr const char* toons[] = {"toon00.png", "toon01.png", "toon02.png", "toon03.png",
                                     "toon04.png", "toon05.png", "toon06.png", "toon07.png",
                                     "toon08.png", "toon09.png", "toon10.png"};
    const auto value = static_cast<unsigned>(id);
    if (value >= 0x67 && value <= 0x71)
        return toons[value - 0x67];
    switch (id) {
    case AssetId::HudSprites:
        return "hud_sprites.png";
    case AssetId::AccessoryHelper:
        return "accessory_helper.png";
    case AssetId::SceneFont:
        return "scene_font_noto.png";
    }
    return nullptr;
}
}
FileAssetSource::FileAssetSource(std::filesystem::path directory)
    : directory_(std::move(directory)) {
}
bool FileAssetSource::Read(AssetId id, std::vector<std::uint8_t>& bytes) const {
    bytes.clear();
    const char* name = AssetName(id);
    if (!name)
        return false;
    std::ifstream file(directory_ / name, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const auto size = file.tellg();
    if (size <= 0 || size > 16 * 1024 * 1024)
        return false;
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        bytes.clear();
        return false;
    }
    return true;
}
CallbackAssetSource::CallbackAssetSource(Reader reader) : reader_(std::move(reader)) {
}
bool CallbackAssetSource::Read(AssetId id, std::vector<std::uint8_t>& bytes) const {
    bytes.clear();
    if (!AssetName(id) || !reader_ || !reader_(id, bytes) || bytes.empty() ||
        bytes.size() > 16 * 1024 * 1024) {
        bytes.clear();
        return false;
    }
    return true;
}
bool LoadAtlas(const AssetSource& source, AssetId id, AtlasAsset& atlas) {
    atlas = {};
    std::vector<std::uint8_t> bytes;
    if (!source.Read(id, bytes) || bytes.empty() || bytes.size() > 16 * 1024 * 1024 ||
        !DecodePngRgba(bytes.data(), bytes.size(), atlas.image))
        return false;
    atlas.revision = ImageRevision(atlas.image);
    return true;
}
AtlasTextureCache::AtlasTextureCache(Device& device) : device_(&device) {
}
AtlasTextureCache::~AtlasTextureCache() {
    Invalidate();
}
Handle AtlasTextureCache::Get(AssetId id, const AtlasAsset& atlas) {
    const auto status = device_->Status();
    if (status.state != DeviceState::Ready || !status.identity || !status.epoch)
        return {};

    const auto width = atlas.image.width, height = atlas.image.height;
    if (!AssetName(id) || !width || !height || !atlas.revision ||
        width > std::numeric_limits<std::size_t>::max() / 4u ||
        height > std::numeric_limits<std::size_t>::max() / (static_cast<std::size_t>(width) * 4u) ||
        atlas.image.pixels.size() != static_cast<std::size_t>(width) * height * 4u)
        return {};
    for (auto& entry : entries_) {
        if (entry.id != id)
            continue;
        const auto residency =
            ClassifyTexture(status, entry.uploaded, device_->ResourceLifecycleFeatures());
        if (residency == TextureResidency::Reupload ||
            residency == TextureResidency::DeviceReplaced)
            entry.texture = {}; // backend already invalidated the old handle
        else
            entry.uploaded = status;
        if (entry.texture && entry.revision == atlas.revision) {
            ++hits_;
            return entry.texture;
        }
        const Handle next = device_->CreateTexture({width, height});
        if (!next)
            return {};
        if (!device_->UploadTexture(next, {atlas.image.pixels.data(),
                                           static_cast<std::size_t>(width) * 4u,
                                           atlas.image.pixels.size()})) {
            device_->Destroy(next);
            return {};
        }
        if (entry.texture && !device_->Destroy(entry.texture)) {
            device_->Destroy(next);
            return {};
        }
        entry.texture = next;
        entry.revision = atlas.revision;
        entry.uploaded = status;
        ++uploads_;
        return next;
    }
    const Handle texture = device_->CreateTexture({width, height});
    if (!texture)
        return {};
    if (!device_->UploadTexture(texture,
                                {atlas.image.pixels.data(), static_cast<std::size_t>(width) * 4u,
                                 atlas.image.pixels.size()})) {
        device_->Destroy(texture);
        return {};
    }
    entries_.push_back({id, texture, atlas.revision, status});
    ++uploads_;
    return texture;
}
bool AtlasTextureCache::Rebind(Device& device) {
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
void AtlasTextureCache::Invalidate() {
    for (const auto& entry : entries_)
        device_->Destroy(entry.texture);
    entries_.clear();
}
}
