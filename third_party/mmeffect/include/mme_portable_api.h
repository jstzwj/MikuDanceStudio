#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
// Read-only CPU bridge. No native handles escape into an RHI frame.
struct MmePortablePass {
    std::string technique, name, vertex, pixel;
};
struct MmePortableEffect {
    unsigned long long object = 0;
    int subset = -1;
    bool post = false;
    std::shared_ptr<const std::string> source;
    std::vector<MmePortablePass> passes;
    std::array<float, 4> multiply{1, 1, 1, 1}, add{};
};
bool MmePortableEffects(std::vector<MmePortableEffect>&, std::string& reason);
// 0: native technique selection forwards the ordinary draw, 1: portable
// material/pass, 2: scene carrier (no object draw), -1: unsupported assignment.
int MmePortableDraw(unsigned long long object, int subset, int mode, unsigned useFlags,
                    MmePortableEffect&, std::string& reason);
