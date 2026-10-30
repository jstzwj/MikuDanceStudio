#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
struct MmeLegacyProgram;
// Read-only CPU bridge. No native handles escape into an RHI frame.
struct MmePortablePass {
    std::string technique, name, vertex, pixel;
    // Ordered tN inputs and SV_TargetN outputs; names reference targets below.
    std::vector<std::string> sampled, colors;
    // Imported Pass inputs can refer to sparse global sampler slots. Explicit
    // profiles leave this empty and use the ordered contiguous convention.
    std::vector<unsigned> sampleSlots;
};
enum class MmePortableFormat {
    Rgba8,
    Rgba16Float
};
struct MmePortableTarget {
    std::string name;
    MmePortableFormat format = MmePortableFormat::Rgba8;
    bool depth = false;
    // CPU resource revision identity. Shared aliases carry the publisher's
    // identity; unrelated effects with the same name never alias.
    std::uint64_t identity = 0;
    // Zero means the current frame viewport extent.
    unsigned width = 0, height = 0;
    // Native offscreen autogen chain (resolved from the current RHI extent).
    bool mipmapped = false, offscreen = false;
};
struct MmePortableParameter {
    std::string name, semantic;
    unsigned rows = 1, columns = 1, elements = 1;
    enum class Type {
        Float,
        Int,
        Bool
    } type = Type::Float;
    bool shared = false;
    // Logical row-major components, not a native constant-buffer layout.
    std::vector<std::uint8_t> value;
};
struct MmePortableCommand {
    enum class Kind {
        Pass,
        ColorTarget,
        DepthTarget,
        ClearColorValue,
        ClearDepthValue,
        Clear,
        Capture,
        Loop,
        LoopIndex,
        LoopEnd,
        DrawBuffer
    } kind;
    std::string parameter;
    int value = 0;
};
struct MmePortableStep {
    enum class Kind {
        Clear,
        Capture,
        Draw
    } kind = Kind::Draw;
    unsigned pass = 0;
    std::vector<std::string> colors;
    std::string depth;
    bool clearColor = false, clearDepth = false;
    std::array<float, 4> color{};
    float depthValue = 1;
    std::vector<MmePortableParameter> parameters;
};
struct MmePortableEffect {
    unsigned long long object = 0;
    int subset = -1;
    unsigned turn = 0;
    bool post = false;
    std::shared_ptr<const std::string> source;
    std::shared_ptr<const std::string> legacySource, modernSource;
    std::shared_ptr<const MmeLegacyProgram> legacy;
    std::array<float, 4> clock{};
    bool clockReady = false;
    std::array<int, 2> counts{-1, -1};
    bool transparent = false;
    std::vector<MmePortablePass> passes;
    std::array<float, 4> multiply{1, 1, 1, 1}, add{};
    std::string profile;
    std::vector<MmePortableTarget> targets;
    std::vector<MmePortableParameter> parameters;
    std::vector<MmePortableCommand> script;
    std::vector<MmePortableStep> steps;
    // Private script writes are published only after the frame succeeds.
    std::vector<MmePortableParameter> writes;
    std::array<float, 4> scriptClearColor{};
    float scriptClearDepth = 1;
};
struct MmePortableTurn {
    unsigned id = 0;
    MmePortableTarget target;
    std::array<float, 4> clearColor{};
    float clearDepth = 1;
    std::vector<MmePortableEffect> effects;
};
// In native discovery order; actual rendering consumes this list in reverse.
bool MmePortableTurns(std::vector<MmePortableTurn>&, std::string& reason);
// CPU-only host refresh and the original ordered control-object assignment walk.
// Never enters a scene, binds native render targets or executes SAS draws.
void MmePortablePrepareParameters();
// Frame-scoped clock preparation: a DX9 fallback in this scope reuses the
// same clock. End is required on every exit, including failed preparation.
void MmePortableBeginFrameClock(int notEditMode);
void MmePortableEndFrameClock();
void MmePortableCommitParameters(const MmePortableEffect&);
// Bounded, versioned script profiles. SceneColor captures the incoming frame;
// FinalColor is the final output. All targets use the scene's viewport extent.
// Unknown pass names/counts fail rather than inventing script semantics.
bool MmePortablePostLayout(MmePortableEffect&, std::string& reason);
// Pure CPU execution of the exported SAS command table. No native state or
// pool is mutated. Bounds are checked before any graphics work is submitted.
bool MmePortableSceneLayout(MmePortableEffect&, std::string& reason);
bool MmePortableEffects(std::vector<MmePortableEffect>&, std::string& reason, unsigned turn = 0);
// 0: native technique selection forwards the ordinary draw, 1: portable
// material/pass, 2: scene carrier (no object draw), -1: unsupported assignment.
int MmePortableDraw(unsigned long long object, int subset, int mode, unsigned useFlags,
                    MmePortableEffect&, std::string& reason, unsigned turn = 0);

namespace mme {
struct SasEffect;
}
// Export the original parsed command table without executing or mutating it.
bool MmePortableExportScene(mme::SasEffect&, MmePortableEffect&, std::string& reason);
