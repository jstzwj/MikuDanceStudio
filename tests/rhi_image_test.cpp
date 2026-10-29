#include "mikudancestudio/rhi_image.hpp"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>
#include <utility>

static bool SnapshotSemantics() {
    using mikudancestudio::rhi::SnapshotBytes;
    SnapshotBytes source{1, 2, 3, 4};
    if (source.Fingerprint() != 0x8010d29826a519fbull)
        return false;
    auto* escaped = source.data();
    auto copied = source;
    const auto unsealedFingerprint = source.Fingerprint();
    escaped[0] = 9;
    if (std::as_const(copied)[0] != 1 || source.Fingerprint() == unsealedFingerprint)
        return false;
    source.Seal();
    auto snapshot = source;
    const auto* shared = std::as_const(snapshot).data();
    if (shared != std::as_const(source).data())
        return false;
    const auto fingerprint = source.Fingerprint();
    if (snapshot.Fingerprint() != fingerprint)
        return false;
    source[0] = 7;
    if (std::as_const(snapshot)[0] != 9 || source.Fingerprint() == fingerprint ||
        snapshot.Fingerprint() != fingerprint)
        return false;
    source.Seal();
    auto after = source;
    std::vector<std::uint8_t>& writable = source;
    writable[1] = 42;
    if (std::as_const(after)[1] != 2 || source.Sealed())
        return false;
    SnapshotBytes indices{0, 0, 2, 0, 5, 0};
    indices.Seal();
    if (!indices.IndicesFit(0, 2, 2, 3) || indices.IndicesFit(0, 2, 2, 2) ||
        indices.IndicesFit(0, 3, 2, 5) || indices.IndicesFit(0, 100, 2, 100) ||
        indices.IndicesFit(std::size_t(-1), 3, 2, 10) || indices.IndicesFit(0, 2, 3, 10))
        return false;
    auto original = indices;
    indices[4] = 1;
    indices.Seal();
    if (!indices.IndicesFit(0, 3, 2, 3) || original.IndicesFit(0, 3, 2, 3))
        return false;
    SnapshotBytes wide{0, 0, 0, 0, 0, 0, 1, 0};
    wide.Seal();
    if (wide.IndicesFit(0, 2, 4, 65536) || !wide.IndicesFit(0, 2, 4, 65537))
        return false;
    mikudancestudio::rhi::RgbaImage image{1, 1, {1, 2, 3, 4}};
    image.pixels.Seal();
    const auto revision = mikudancestudio::rhi::ImageRevision(image);
    if (revision != 0x28e68eecca6dd8bdull)
        return false;
    image.width = 2;
    return mikudancestudio::rhi::ImageRevision(image) != revision;
}

int main(int argc, char** argv) {
    if (!SnapshotSemantics()) return 9;
    if (argc != 2) return 1;
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> png{
        std::istreambuf_iterator<char>{input}, {}};
    mikudancestudio::rhi::RgbaImage image;
    if (!mikudancestudio::rhi::DecodePngRgba(
            png.data(), png.size(), image) ||
        image.width != 512 || image.height != 512 ||
        image.pixels.size() != 512u * 512u * 4u) return 2;
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t channel : image.pixels)
        hash = (hash ^ channel) * 1099511628211ull;
    if (hash != 0xc710daea6ce8abdeull) return 3;
    image.pixels.clear();
    if (mikudancestudio::rhi::DecodePngRgba(
            png.data(), png.size() / 2, image) ||
        !image.pixels.empty()) return 4;
    // Generated legal TGA fixture: bottom-up BGRA -> canonical top-down RGBA.
    std::vector<std::uint8_t> tga(18);tga[2]=2;tga[12]=2;tga[14]=2;tga[16]=32;tga[17]=8;
    tga.insert(tga.end(),{255,0,0,17, 255,255,255,18, 0,0,255,19, 0,255,0,20});
    if(!mikudancestudio::rhi::DecodeTextureRgba(tga.data(),tga.size(),image) || image.pixels!=std::vector<std::uint8_t>{255,0,0,19,0,255,0,20,0,0,255,17,255,255,255,18})return 5;
    if(mikudancestudio::rhi::DecodeTextureRgba(tga.data(),tga.size()-1,image) || !image.pixels.empty())return 6;
    tga.resize(18);tga[2]=10;tga[17]=0x28;tga.insert(tga.end(),{0x81,0,0,255,255,1,0,255,0,128,255,0,0,64});
    if(!mikudancestudio::rhi::DecodeTextureRgba(tga.data(),tga.size(),image) || image.pixels!=std::vector<std::uint8_t>{255,0,0,255,255,0,0,255,0,255,0,128,0,0,255,64})return 7;
    tga[18]=0x84;if(mikudancestudio::rhi::DecodeTextureRgba(tga.data(),tga.size(),image) || !image.pixels.empty())return 8;
    return 0;
}
