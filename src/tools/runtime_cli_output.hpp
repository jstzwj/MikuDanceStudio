#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <locale>
namespace mikudancestudio::cli {
inline std::string Quote(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\')
            out << '\\' << char(c);
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
                << std::dec;
        else
            out << char(c);
    }
    out << '"';
    return out.str();
}
template <class Range> void Numbers(std::ostream& out, const Range& range) {
    out << '[';
    bool first = true;
    for (auto v : range) {
        if (!first)
            out << ',';
        first = false;
        out << v;
    }
    out << ']';
}
template <class Range> void Strings(std::ostream& out, const Range& range) {
    out << '[';
    bool first = true;
    for (const auto& v : range) {
        if (!first)
            out << ',';
        first = false;
        out << Quote(v);
    }
    out << ']';
}
inline int Read(void* context, void* data, std::size_t length) {
    auto& stream = *static_cast<std::istream*>(context);
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(length));
    return static_cast<int>(stream.gcount());
}
inline bool ReadMotion(const std::filesystem::path& path, format::VmdDocument& motion) {
    // The existing VMD parser takes FILE*. Stage bytes from a native path into
    // a binary temporary stream; do not reinterpret a Unicode path through ACP.
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::tmpfile(), std::fclose);
    if (!file)
        return false;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof buffer);
        const auto count = static_cast<std::size_t>(input.gcount());
        if (std::fwrite(buffer, 1, count, file.get()) != count)
            return false;
    }
    if (!input.eof() || std::fseek(file.get(), 0, SEEK_SET) != 0)
        return false;
    return format::ReadVmd(file.get(), motion) && std::fgetc(file.get()) == EOF;
}
inline void WriteSnapshot(std::ostream& out, const runtime::ModelSnapshot& state,
                   const runtime::ModelResource& resource) {
    out << "{\"frame\":" << state.frame << ",\"bones\":[";
    for (std::size_t n = 0; n < state.bones.size(); ++n) {
        if (n)
            out << ',';
        const auto& bone = state.bones[n];
        out << "{\"name\":" << Quote(resource.BoneNames()[n]) << ",\"translation\":";
        Numbers(out, bone.translation);
        out << ",\"rotation\":";
        Numbers(out, bone.rotation);
        out << ",\"local\":";
        Numbers(out, bone.local);
        out << ",\"skin\":";
        Numbers(out, bone.skin);
        out << ",\"world_position\":";
        Numbers(out, bone.worldPosition);
        out << '}';
    }
    out << "],\"morph_weights\":";
    Numbers(out, state.morphWeights);
    out << ",\"vertices\":[";
    for (std::size_t n = 0; n < state.vertices.size(); ++n) {
        if (n)
            out << ',';
        const auto& vertex = state.vertices[n];
        out << "{\"position\":";
        Numbers(out, vertex.position);
        out << ",\"normal\":";
        Numbers(out, vertex.normal);
        out << ",\"uv\":";
        Numbers(out, vertex.uv);
        out << '}';
    }
    out << "]}";
}
}
