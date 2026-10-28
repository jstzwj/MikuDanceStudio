#pragma once

#include "mikudancestudio/screen_vertex.hpp"

namespace mikudancestudio {

// Original triangle order: (right,top), (right,bottom), (left,top),
// (left,top), (right,bottom), (left,bottom). Keep the double-to-float stores.
inline void WriteBackgroundQuad(TexturedScreenVertex* vertices, double left, double top,
                                double right, double bottom) {
    vertices[5].x = static_cast<float>(left);
    vertices[3].x = vertices[5].x;
    vertices[2].x = vertices[3].x;
    vertices[4].x = static_cast<float>(right);
    vertices[1].x = vertices[4].x;
    vertices[0].x = vertices[1].x;
    vertices[3].y = static_cast<float>(top);
    vertices[2].y = vertices[3].y;
    vertices[0].y = vertices[2].y;
    vertices[5].y = static_cast<float>(bottom);
    vertices[4].y = vertices[5].y;
    vertices[1].y = vertices[4].y;
    for (std::size_t i = 0; i < kBackgroundQuadVertexCount; ++i) {
        vertices[i].z = 0.0f;
        // x86 0x417078..0x4170FB / x64 0x7FF7CB488887..0x7FF7CB488920.
        vertices[i].reciprocalW = 1.0f;
        vertices[i].color = 0xFFFFFFFF;
    }
    vertices[0].u = vertices[1].u = vertices[4].u = 1.0f;
    vertices[2].u = vertices[3].u = vertices[5].u = 0.0f;
    vertices[0].v = vertices[2].v = vertices[3].v = 0.0f;
    vertices[1].v = vertices[4].v = vertices[5].v = 1.0f;
}

} // namespace mikudancestudio
