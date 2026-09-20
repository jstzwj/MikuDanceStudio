#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include "mikudancestudio/accessory_layout.hpp"

namespace mikudancestudio {
// These dialogs reused one untyped scratch slot in the original application.
// Both contain 32-bit slot indices, never object pointers. Separate owners keep
// model and accessory slot namespaces from overwriting each other's workspace.
struct DialogOrderArrays {
    std::unique_ptr<std::int32_t[]> modelIndices;
    std::unique_ptr<std::int32_t[]> accessoryIndices;

    void SwapAccessories(std::size_t first, std::size_t second) noexcept {
        std::swap(accessoryIndices[first], accessoryIndices[second]);
    }
};

// Preserve the original 255-slot scan and leave unmatched entries untouched.
template<class AccessoryAt>
void BuildAccessoryOrderIndices(std::int32_t* order, int count,
                               AccessoryAt accessoryAt) {
    for (int i = 0; i < count; ++i) {
        for (int slot = 0; slot < 255; ++slot) {
            const mdl::AccessoryRecord* accessory = accessoryAt(slot);
            if (accessory != nullptr && accessory->order == i) {
                order[i] = slot;
                break;
            }
        }
    }
}

template<class AccessoryAt, class RenameAccessory>
std::uint8_t ApplyAccessoryOrderIndices(const std::int32_t* order, int count,
                                       AccessoryAt accessoryAt,
                                       RenameAccessory renameAccessory) {
    for (int i = 0; i < count; ++i) {
        mdl::AccessoryRecord* accessory = accessoryAt(order[i]);
        if (accessory == nullptr)
            continue;
        accessory->order = static_cast<std::uint8_t>(i);
        renameAccessory(i, *accessory);
    }
    return static_cast<std::uint8_t>(order[0]);
}

}  // namespace mikudancestudio
