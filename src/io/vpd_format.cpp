#include "mikudancestudio/vpd_format.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace mikudancestudio::format {
namespace {

constexpr std::size_t kMaxLine = 1 << 20;
constexpr long kMaxBones = 100000;

bool ReadLine(std::FILE* stream, std::string& line) {
    line.clear();
    int ch;
    while ((ch = std::fgetc(stream)) != EOF && ch != '\n') {
        if (line.size() == kMaxLine)
            return false;
        line.push_back(static_cast<char>(ch));
    }
    if (ch == EOF && line.empty())
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

bool ReadNonemptyLine(std::FILE* stream, std::string& line) {
    while (ReadLine(stream, line)) {
        if (!line.empty())
            return true;
    }
    return false;
}

bool ReadCount(const std::string& line, long& count) {
    char* end = nullptr;
    errno = 0;
    count = std::strtol(line.c_str(), &end, 10);
    return errno == 0 && end != line.c_str() && *end == ';' && count >= 0 && count <= kMaxBones;
}

bool ReadTranslation(const std::string& line, float (&values)[3]) {
    return line.find(';') != std::string::npos &&
           std::sscanf(line.c_str(), " %f,%f,%f;", &values[0], &values[1], &values[2]) == 3;
}

bool ReadRotation(const std::string& line, float (&values)[4]) {
    return line.find(';') != std::string::npos &&
           std::sscanf(line.c_str(), " %f,%f,%f,%f;", &values[0], &values[1], &values[2],
                       &values[3]) == 4;
}

} // namespace

bool ReadVpd(std::FILE* stream, VpdDocument& document) {
    if (stream == nullptr)
        return false;
    VpdDocument parsed;
    std::string line;
    if (!ReadLine(stream, line) || line != "Vocaloid Pose Data file" || !ReadLine(stream, line) ||
        !line.empty() || !ReadLine(stream, line))
        return false;

    const std::size_t modelEnd = line.find(".osm;");
    parsed.modelName = modelEnd == std::string::npos ? line : line.substr(0, modelEnd);

    long count;
    if (!ReadLine(stream, line) || !ReadCount(line, count))
        return false;
    parsed.bones.reserve(static_cast<std::size_t>(count));
    for (long i = 0; i < count; ++i) {
        VpdBone bone;
        if (!ReadNonemptyLine(stream, line))
            return false;
        const std::size_t brace = line.find('{');
        if (brace == std::string::npos)
            return false;
        bone.name = line.substr(brace + 1);
        if (!ReadLine(stream, line) || !ReadTranslation(line, bone.translation) ||
            !ReadLine(stream, line) || !ReadRotation(line, bone.rotation) ||
            !ReadLine(stream, line))
            return false;
        const std::size_t close = line.find_first_not_of(" \t");
        if (close == std::string::npos || line[close] != '}')
            return false;
        parsed.bones.push_back(std::move(bone));
    }
    document = std::move(parsed);
    return true;
}

bool WriteVpd(std::FILE* stream, const VpdDocument& document) {
    if (stream == nullptr ||
        document.bones.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    // The comments and spacing match the legacy VPD writer. The caller's
    // stream mode determines whether newlines become CRLF on Windows.
    if (std::fprintf(stream, "Vocaloid Pose Data file\n\n") < 0 ||
        std::fprintf(stream, "%s.osm;\t\t// \x90\x65\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc\n",
                     document.modelName.c_str()) < 0 ||
        std::fprintf(
            stream,
            "%d;\t\t\t\t// \x91\x8d\x83\x7c\x81\x5b\x83\x59\x83\x7b\x81\x5b\x83\x93\x90\x94\n\n",
            static_cast<int>(document.bones.size())) < 0)
        return false;
    int index = 0;
    for (const VpdBone& bone : document.bones) {
        if (std::fprintf(stream, "Bone%d{%s\n", index++, bone.name.c_str()) < 0 ||
            std::fprintf(stream, "  %f,%f,%f;\t\t\t\t// trans x,y,z\n", bone.translation[0],
                         bone.translation[1], bone.translation[2]) < 0 ||
            std::fprintf(stream, "  %f,%f,%f,%f;\t\t// Quaternion x,y,z,w\n", bone.rotation[0],
                         bone.rotation[1], bone.rotation[2], bone.rotation[3]) < 0 ||
            std::fprintf(stream, "}\n\n") < 0)
            return false;
    }
    return std::ferror(stream) == 0;
}

} // namespace mikudancestudio::format
