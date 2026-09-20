#include <cstring>
#include <new>

#include "mikudancestudio/model.hpp"

namespace mikudancestudio {
void ModelInitDefaults(unsigned char*);
void ModelDispose(unsigned char*);
}

int main() {
    using namespace mikudancestudio;
    auto* bytes = static_cast<unsigned char*>(::operator new(mdl::kSize));
    std::memset(bytes, 0, mdl::kSize);
    ModelInitDefaults(bytes);
    // This is the same lifetime as a cancelled or failed model load.
    // No rendering device or physics scene is required for an empty model.
    ModelDispose(bytes);
    ::operator delete(bytes);

    bytes = static_cast<unsigned char*>(::operator new(mdl::kSize));
    std::memset(bytes, 0, mdl::kSize);
    ModelInitDefaults(bytes);
    auto& model = *mdl::Mdl(bytes);
    model.boneKeyCursors = static_cast<std::uint32_t*>(::operator new(4));
    model.morphKeyCursors = static_cast<std::uint32_t*>(::operator new(4));
    model.reservedMorphTable = ::operator new(16);
    model.pmxTextBuffers[0] = static_cast<wchar_t*>(::operator new(32));
    model.boneOrderTable = new mdl::BoneOrderEntry[3]{};
    model.undoRings[0].slots[0].bonePose =
        static_cast<mdl::BonePoseSnapshot*>(::operator new(sizeof(mdl::BonePoseSnapshot)));
    ModelDispose(bytes);
    const bool released = model.boneKeyCursors == nullptr
        && model.morphKeyCursors == nullptr
        && model.reservedMorphTable == nullptr
        && model.pmxTextBuffers[0] == nullptr
        && model.boneOrderTable == nullptr
        && model.undoRings[0].slots[0].bonePose == nullptr;
    ::operator delete(bytes);
    return released ? 0 : 1;
}
