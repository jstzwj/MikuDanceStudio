#pragma once
#include "mikudancestudio/rhi.hpp"
#include "mikudancestudio/rhi_image.hpp"
#include <filesystem>
#include <functional>
#include <vector>

namespace mikudancestudio::rhi {
enum class AssetId : std::uint16_t {
    HudSprites = 0x66,
    Toon00 = 0x67,
    Toon01,
    Toon02,
    Toon03,
    Toon04,
    Toon05,
    Toon06,
    Toon07,
    Toon08,
    Toon09,
    Toon10,
    AccessoryHelper = 0x72,
    SceneFont = 0x100
};

class AssetSource {
public:
    virtual ~AssetSource() = default;
    virtual bool Read(AssetId id, std::vector<std::uint8_t>& bytes) const = 0;
};

class FileAssetSource final : public AssetSource {
public:
    explicit FileAssetSource(std::filesystem::path directory);
    bool Read(AssetId id, std::vector<std::uint8_t>& bytes) const override;

private:
    std::filesystem::path directory_;
};

class CallbackAssetSource final : public AssetSource {
public:
    using Reader = std::function<bool(AssetId, std::vector<std::uint8_t>&)>;
    explicit CallbackAssetSource(Reader reader);
    bool Read(AssetId id, std::vector<std::uint8_t>& bytes) const override;

private:
    Reader reader_;
};

struct AtlasAsset {
    RgbaImage image;
    std::uint64_t revision = 0;
};
bool LoadAtlas(const AssetSource& source, AssetId id, AtlasAsset& atlas);

class AtlasTextureCache {
public:
    explicit AtlasTextureCache(Device& device);
    ~AtlasTextureCache();
    AtlasTextureCache(const AtlasTextureCache&) = delete;
    AtlasTextureCache& operator=(const AtlasTextureCache&) = delete;
    Handle Get(AssetId id, const AtlasAsset& atlas);
    // Call while the old device is alive; caller retains decoded CPU assets.
    bool Rebind(Device& device);
    void Invalidate();
    std::uint64_t Uploads() const { return uploads_; }
    std::uint64_t Hits() const { return hits_; }

private:
    struct Entry {
        AssetId id;
        Handle texture;
        std::uint64_t revision;
        DeviceStatus uploaded{};
    };
    Device* device_;
    std::vector<Entry> entries_;
    std::uint64_t uploads_ = 0, hits_ = 0;
};
}
