// ===========================================================================
// Shared machinery for the per-model keyframe consumers
//   VA 0x004A31D0 (model_keyframe_advance.cpp - incremental advance) and
//   VA 0x004B4260 (model_frame_seek.cpp - direct frame seek).
// ===========================================================================
// Record layouts and the selector-refresh semantics are documented in the
// model_keyframe_advance.cpp header; both functions agree on them.
// =========================================================================//
#ifndef MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP
#define MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP

#include <cstdint>
#include <cstring>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"

namespace mikudancestudio {
namespace kfa {

// memcpy-based record accessors - no alignment or type-punning assumptions.
inline std::uint32_t Rd32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline std::int32_t RdI32(const unsigned char* p) {
    std::int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline float RdF32(const unsigned char* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
inline void Wr32(unsigned char* p, std::uint32_t v) {
    std::memcpy(p, &v, 4);
}
inline void WrF32(unsigned char* p, float v) {
    std::memcpy(p, &v, 4);
}
} // namespace kfa
} // namespace mikudancestudio
#endif
