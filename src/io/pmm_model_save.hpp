#pragma once

namespace mikudancestudio {
class D3DRenderer;
namespace mdl {
struct ModelRecord;
}

namespace pmm_io {
// Serialize occupied model slots in PMM 0002 stream order. The caller owns
// the file descriptor and supplies a 256-byte path conversion buffer.
void WritePmmModelBlocks(int fd, const D3DRenderer* renderer, mdl::ModelRecord* const* slots,
                         char* text);
} // namespace pmm_io
} // namespace mikudancestudio
