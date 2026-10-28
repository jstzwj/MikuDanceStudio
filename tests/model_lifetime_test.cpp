#include <cstring>
#include <new>
#include <unknwn.h>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"

#include "mikudancestudio/ported_funcs.hpp"
namespace mikudancestudio { MMDApp* g_Block = nullptr; }

namespace {
template <class T> T* Allocate(std::size_t count = 1) {
    auto* storage = static_cast<T*>(::operator new(sizeof(T) * count));
    std::memset(storage, 0, sizeof(T) * count);
    return storage;
}

class BufferReleaseProbe final : public IUnknown {
public:
    BufferReleaseProbe(mikudancestudio::mdl::ModelRecord& model, int expected, int& sequence,
                       bool& correct)
        : model(model), expected(expected), sequence(sequence), correct(correct) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override {
        correct &= sequence++ == expected && model.materials == nullptr &&
                   model.displayKeys == nullptr && model.boneOrderTable == nullptr &&
                   model.pmxTextBuffers[0] != nullptr;
        return 0;
    }

private:
    mikudancestudio::mdl::ModelRecord& model;
    int expected;
    int& sequence;
    bool& correct;
};
} // namespace

int main() {
    using namespace mikudancestudio;
    auto* storage = static_cast<mdl::ModelRecord*>(::operator new(mdl::kSize));
    std::memset(storage, 0, mdl::kSize);
    ModelInitDefaults(storage);
    // This is the same lifetime as a cancelled or failed model load.
    // No rendering device or physics scene is required for an empty model.
    DeleteModel(storage, 1);

    storage = static_cast<mdl::ModelRecord*>(::operator new(mdl::kSize));
    std::memset(storage, 0, mdl::kSize);
    ModelInitDefaults(storage);
    auto& model = *storage;
    model.materials = static_cast<mdl::ModelMaterialRecord*>(
        ::operator new(sizeof(mdl::ModelMaterialRecord) * 2));
    model.boneKeyCursors = static_cast<std::uint32_t*>(::operator new(4));
    model.morphKeyCursors = static_cast<std::uint32_t*>(::operator new(4));
    model.reservedMorphTable = ::operator new(16);
    model.pmxTextBuffers[0] = static_cast<wchar_t*>(::operator new(32));
    model.boneOrderTable = new mdl::BoneOrderEntry[3]{};
    for (auto& ring : model.undoRings) {
        for (int slot : {0, mdl::kUndoHistoryDepth - 1}) {
            ring.slots[slot].bonePose = Allocate<mdl::BonePoseSnapshot>();
            ring.slots[slot].keySnapshots = Allocate<mdl::BoneKeyUndoEntry>();
        }
    }
    model.displayKeys = Allocate<mdl::DisplayKey>(mdl::kDisplayKeyCapacity);
    for (std::size_t key : {std::size_t{0}, mdl::kDisplayKeyCapacity - 1}) {
        mdl::IkStates(model.displayKeys[key]) = Allocate<std::uint8_t>();
        mdl::SelectorStates(model.displayKeys[key]) = Allocate<mdl::BoneReference>();
    }
    int sequence = 0;
    bool correctOrder = true;
    BufferReleaseProbe edge(model, 0, sequence, correctOrder);
    BufferReleaseProbe vertices(model, 1, sequence, correctOrder);
    BufferReleaseProbe indices(model, 2, sequence, correctOrder);
    model.vertexBuffer2 = &edge;
    model.vertexBuffer = &vertices;
    model.indexBuffer = &indices;
    // Flag zero retains the model object for the caller after disposing its resources.
    DeleteModel(storage, 0);
    bool released = model.materials == nullptr && model.boneKeyCursors == nullptr &&
                    model.morphKeyCursors == nullptr && model.reservedMorphTable == nullptr &&
                    model.pmxTextBuffers[0] == nullptr && model.boneOrderTable == nullptr &&
                    model.displayKeys == nullptr && model.vertexBuffer2 == nullptr &&
                    model.vertexBuffer == nullptr && model.indexBuffer == nullptr &&
                    sequence == 3 && correctOrder;
    for (const auto& ring : model.undoRings)
        for (const auto& undo : ring.slots)
            released &= undo.bonePose == nullptr && undo.keySnapshots == nullptr;
    ::operator delete(storage);
    return released ? 0 : 1;
}
