#include "runtime_cli_output.hpp"
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>

namespace {
using namespace mikudancestudio;
using namespace mikudancestudio::cli;
int Run(const std::vector<std::string>& args) {
    std::string modelPath, motionPath, outputPath, reason;
    std::vector<unsigned> frames;
    bool capabilities = false;
    const auto fail = [&](const std::string& message) {
        std::cerr << "{\"status\":\"failed\",\"reason\":" << Quote(message) << "}\n";
        return 1;
    };
    for (std::size_t n = 1; n < args.size(); ++n) {
        const auto& arg = args[n];
        if (arg == "--capabilities") {
            capabilities = true;
            continue;
        }
        if (n + 1 == args.size())
            return fail("missing-option-value:" + arg);
        const auto& value = args[++n];
        if (arg == "--model")
            modelPath = value;
        else if (arg == "--motion")
            motionPath = value;
        else if (arg == "--output")
            outputPath = value;
        else if (arg == "--frame") {
            unsigned frame = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frame);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return fail("invalid-frame");
            frames.push_back(frame);
        } else
            return fail("unknown-option:" + arg);
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":1,\"stage\":\"P2\",\"supported\":";
    Strings(out, runtime::SupportedCapabilities);
    out << ",\"unsupported\":";
    Strings(out, runtime::UnsupportedCapabilities);
    if (capabilities && modelPath.empty() && motionPath.empty()) {
        std::cout << out.str() << "}\n";
        return 0;
    }
    if (modelPath.empty() || motionPath.empty() || frames.empty())
        return fail(
            "usage:mds_model_cli --model path.pmx --motion path.vmd --frame N [--frame N] [--output path.json]");
    std::ifstream file(std::filesystem::u8path(modelPath), std::ios::binary);
    format::ModelDocument model;
    if (!file || !format::ReadModelDocument(Read, &file, model))
        return fail("model-read-failed");
    // The reader preserves the existing no-rigid PMX boundary; accept a final
    // zero joint count, but reject any other trailing data in this CLI.
    std::array<char, 4> tail{};
    file.read(tail.data(), tail.size());
    if (file.gcount() != 0 &&
        (file.gcount() != 4 || tail != std::array<char, 4>{} || file.peek() != EOF))
        return fail("model-trailing-data");
    format::VmdDocument motion;
    if (!ReadMotion(std::filesystem::u8path(motionPath), motion))
        return fail("motion-read-failed");
    std::shared_ptr<const runtime::ModelResource> resource;
    std::shared_ptr<const runtime::MotionClip> clip;
    if (!runtime::ModelResource::Create(model, resource, reason) ||
        !runtime::MotionClip::Create(resource, motion, clip, reason))
        return fail(reason);
    runtime::ModelInstance instance(resource);
    if (!instance.BindMotion(clip, reason))
        return fail(reason);
    out << ",\"warnings\":";
    Strings(out, clip->Warnings());
    out << ",\"snapshots\":[";
    for (std::size_t n = 0; n < frames.size(); ++n) {
        if (!instance.Seek(frames[n], reason))
            return fail(reason);
        if (n)
            out << ',';
        WriteSnapshot(out, instance.Snapshot(), *resource);
    }
    out << "],\"status\":\"passed\"}\n";
    if (outputPath.empty())
        std::cout << out.str();
    else {
        std::ofstream output(std::filesystem::u8path(outputPath), std::ios::binary);
        output << out.str();
        if (!output)
            return fail("snapshot-write-failed");
    }
    return 0;
}
}
#ifdef _WIN32
// CRT/native filesystem conversion; no Windows SDK, HWND or COM dependency.
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int n = 0; n < argc; ++n)
        args.push_back(std::filesystem::path(argv[n]).u8string());
    try {
        return Run(args);
    } catch (const std::exception& e) {
        std::cerr << "runtime-error:" << e.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    try {
        return Run({argv, argv + argc});
    } catch (const std::exception& e) {
        std::cerr << "runtime-error:" << e.what() << '\n';
        return 1;
    }
}
#endif
