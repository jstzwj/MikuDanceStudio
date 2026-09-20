#include "mikudancestudio/dialog_order_arrays.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <type_traits>

int main() {
    using namespace mikudancestudio;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    static_assert(!std::is_copy_constructible_v<DialogOrderArrays>);
    auto first = std::make_unique<mdl::AccessoryRecord>();
    auto second = std::make_unique<mdl::AccessoryRecord>();
    std::printf("object pointers above 32 bits: %d, %d\n",
        std::uintptr_t(first.get()) > UINT32_MAX,
        std::uintptr_t(second.get()) > UINT32_MAX);
    first->order = 1;
    second->order = 0;
    // Use nonadjacent slots and object bytes unrelated to slot indices.
    // This catches both the pointer-as-index write and the old selected-byte read.
    std::array<mdl::AccessoryRecord*, 255> slots{};
    slots[7] = first.get();
    slots[254] = second.get();
    const auto lookup = [&](int slot) { return slots.at(slot); };
    DialogOrderArrays arrays;
    arrays.modelIndices.reset(new std::int32_t[3]());
    arrays.modelIndices[1] = 99;
    arrays.accessoryIndices.reset(new std::int32_t[2]);
    BuildAccessoryOrderIndices(arrays.accessoryIndices.get(), 2, lookup);
    check(arrays.accessoryIndices[0] == 254 && arrays.accessoryIndices[1] == 7,
          "Builder must store slot indices, including the final slot");
    arrays.SwapAccessories(0, 1);
    check(first->order == 1 && second->order == 0,
          "Moving dialog rows must not commit the objects yet");
    const auto selected = ApplyAccessoryOrderIndices(arrays.accessoryIndices.get(), 2,
        lookup, [](int row, mdl::AccessoryRecord& accessory) {
            strcpy_s(accessory.name, row == 0 ? "first row" : "second row");
        });
    check(selected == 7 && first->order == 0 && second->order == 1,
          "Apply must resolve slots and select the first slot");
    check(std::strcmp(first->name, "first row") == 0 &&
          std::strcmp(second->name, "second row") == 0,
          "Renaming must follow the reordered object");
    check(arrays.modelIndices[0] == 0 && arrays.modelIndices[1] == 99,
          "Accessory edits must not overwrite model indices or zero initialization");
    arrays.accessoryIndices.reset();  // Cancel/OK close releases only its workspace.
    check(!arrays.accessoryIndices && arrays.modelIndices[1] == 99,
          "Closing one dialog must retain the other workspace");
    arrays.accessoryIndices.reset(new std::int32_t[2]);
    BuildAccessoryOrderIndices(arrays.accessoryIndices.get(), 2, lookup);
    check(arrays.accessoryIndices[0] == 7 && arrays.accessoryIndices[1] == 254,
          "Reopening must build the newly committed order");
    arrays.modelIndices.reset(new std::int32_t[1]());
    check(arrays.modelIndices[0] == 0 && arrays.accessoryIndices[1] == 254,
          "Replacing the model array must preserve accessory order");
    std::printf("dialog order arrays: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}
