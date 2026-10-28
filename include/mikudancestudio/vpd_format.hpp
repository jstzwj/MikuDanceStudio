#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace mikudancestudio::format {

// Names are the file's original byte strings (normally Shift-JIS). The
// format layer does not know about Win32 paths, UI state, or model records.
struct VpdBone {
    std::string name;
    float translation[3]{};
    float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
};

struct VpdDocument {
    std::string modelName;
    std::vector<VpdBone> bones;
};

// The caller opens the stream, choosing path encoding and text/binary mode.
// Parsing commits to `document` only after the entire file is valid.
bool ReadVpd(std::FILE* stream, VpdDocument& document);
bool WriteVpd(std::FILE* stream, const VpdDocument& document);

} // namespace mikudancestudio::format
