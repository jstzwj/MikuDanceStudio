// P0 evidence only: public generated files and already-supported track math.
// This does not implement a portable model runtime or new numeric formulas.
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/vmd_format.hpp"
#include "mikudancestudio/vpd_format.hpp"
#include "model_reader_fixture.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace mikudancestudio::format;
namespace {
void Require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const void* bytes, std::size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(bytes), size);
    Require(bool(file), "baseline file write failed");
}
template <class Document, class Writer>
void WriteDocument(const std::filesystem::path& path, const Document& doc, Writer writer) {
    auto* file = std::tmpfile();
    Require(file != nullptr, "baseline tmpfile failed");
    struct Close { std::FILE* file; ~Close() { std::fclose(file); } } close{file};
    Require(writer(file, doc), "baseline serialization failed");
    std::rewind(file);
    std::vector<char> bytes;
    for (int c; (c = std::fgetc(file)) != EOF;)
        bytes.push_back(static_cast<char>(c));
    Require(!std::ferror(file), "baseline read failed");
    Write(path, bytes.data(), bytes.size());
}
std::uint32_t Bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "usage: mds_portable_baseline output-directory");
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const std::uint32_t endian = 1;
        Require(*reinterpret_cast<const unsigned char*>(&endian) == 1,
                "P0 baseline currently targets little-endian hosts");
        auto pmd = model_reader_fixture::Pmd();
        auto pmx = model_reader_fixture::Pmx(true);
        ModelDocument model;
        Require(ReadModelDocument(model_reader_fixture::Bytes::read, &pmd, model) &&
                    pmd.position == pmd.data.size(), "generated PMD parse failed");
        Require(ReadModelDocument(model_reader_fixture::Bytes::read, &pmx, model) &&
                    pmx.position == pmx.data.size(), "generated PMX parse failed");
        Write(directory / "generated.pmd", pmd.data.data(), pmd.data.size());
        Write(directory / "generated.pmx", pmx.data.data(), pmx.data.size());

        VmdDocument motion;
        std::memcpy(motion.modelName.data(), "P0-\x83\x7e\x83\x4e", 7);
        VmdBoneKey key;
        std::memcpy(key.name.data(), "center", 6);
        key.frame = 17;
        key.position[0] = 1.25f;
        key.position[1] = -2.0f;
        key.rotation[3] = 1;
        for (unsigned n = 0; n < key.interpolation.size(); ++n)
            key.interpolation[n] = static_cast<unsigned char>(n);
        motion.bones.push_back(key);
        VmdMorphKey morph;
        std::memcpy(morph.name.data(), "smile", 5);
        morph.frame = 23;
        morph.value = .625f;
        motion.morphs.push_back(morph);
        WriteDocument(directory / "generated.vmd", motion, WriteVmd);
        VpdDocument pose;
        pose.modelName = "P0";
        VpdBone bone;
        bone.name = "center";
        bone.translation[0] = 1.25f;
        bone.translation[1] = -2;
        pose.bones.push_back(bone);
        WriteDocument(directory / "generated.vpd", pose, WriteVpd);

        PmmBoneKey left, right;
        right.frame = 10;
        left.position = {.1f, -0.f, 0};
        right.position = {.2f, 0, 0};
        PmmEvaluatedBone evaluated;
        Require(InterpolatePmmBoneSegment(left, right, 3, evaluated), "track evaluation failed");
        std::ofstream numeric(directory / "numeric.json");
        numeric << "{\"translation_bits\":[" << Bits(evaluated.translation[0]) << ','
                << Bits(evaluated.translation[1]) << ',' << Bits(evaluated.translation[2])
                << "],\"profile\":\""
#if defined(_M_IX86)
                << "vc9-x87"
#else
                << "separate-float32"
#endif
                << "\"}\n";
        Require(bool(numeric), "numeric evidence write failed");
        std::puts("P0 generated PMD/PMX/VMD/VPD and exact track bits captured; no model solver/GPU");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
