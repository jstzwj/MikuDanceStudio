#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;

namespace mikudancestudio {
// This regression covers empty-key undo/redo and caller frame restoration.
// Physics mode changes require the Bullet-backed animation subsystem and are
// outside this fixture. Never let an unexpected call silently pass the test.
void NotifyBonePhysicsMode(unsigned char*, int, unsigned char) {
    std::fputs("Unexpected physics mode change in empty-key undo test\n", stderr);
    std::abort();
}
} // namespace mikudancestudio

static_assert(std::is_same_v<decltype(&mds::UndoModelEdit),
                            void (*)(unsigned char*, std::int32_t&)>);
static_assert(std::is_same_v<decltype(&mds::RedoModelEdit),
                            void (*)(unsigned char*, std::int32_t&)>);

int main() {
    auto model = std::make_unique<mdl::ModelRecord>();
    auto* bytes = reinterpret_cast<unsigned char*>(model.get());
    auto& undo = model->undoRings[0].slots[0];
    // A valid empty key edit still restores its saved frame. This drives
    // the actual Undo/Redo entry points without needing a window or model.
    undo.operation = 2;
    undo.frame = 47;
    std::int32_t frame = 0;
    mds::UndoModelEdit(bytes, frame);
    bool ok = frame == 47 && model->undoState[0] == 29;
    frame = 99;
    mds::RedoModelEdit(bytes, frame);
    ok = ok && frame == 47 && model->undoState[0] == 0;
    ::operator delete(model->undoRings[1].slots[0].bonePose);
    ::operator delete(model->undoRings[1].slots[0].auxiliaryPose);
    if (!ok)
        std::fprintf(stderr, "Undo/redo did not update the caller's frame\n");
    return ok ? 0 : 1;
}
