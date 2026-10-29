#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
// Read-only CPU bridge. No native handles escape into an RHI frame.
struct MmePortablePass {
    std::string technique, name, vertex, pixel;
    // Ordered tN inputs and SV_TargetN outputs; names reference targets below.
    std::vector<std::string> sampled, colors;
};
enum class MmePortableFormat {
    Rgba8,
    Rgba16Float
};
struct MmePortableTarget {
    std::string name;
    MmePortableFormat format = MmePortableFormat::Rgba8;
};
struct MmePortableEffect {
    unsigned long long object = 0;
    int subset = -1;
    bool post = false;
    std::shared_ptr<const std::string> source;
    std::vector<MmePortablePass> passes;
    std::array<float, 4> multiply{1, 1, 1, 1}, add{};
    std::string profile;
    std::vector<MmePortableTarget> targets;
};
// Bounded, versioned script profiles. SceneColor captures the incoming frame;
// FinalColor is the final output. All targets use the scene's viewport extent.
// Unknown pass names/counts fail rather than inventing script semantics.
bool MmePortablePostLayout(MmePortableEffect&, std::string& reason);
bool MmePortableEffects(std::vector<MmePortableEffect>&, std::string& reason);
// 0: native technique selection forwards the ordinary draw, 1: portable
// material/pass, 2: scene carrier (no object draw), -1: unsupported assignment.
int MmePortableDraw(unsigned long long object, int subset, int mode, unsigned useFlags,
                    MmePortableEffect&, std::string& reason);
