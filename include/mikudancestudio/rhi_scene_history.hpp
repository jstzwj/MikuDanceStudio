#pragma once
#include <cstdint>
#include <vector>
#include "rhi.hpp"
struct IDirect3DDevice9;
namespace mikudancestudio {
#ifdef MIKUDANCESTUDIO_DIAG
bool PrepareDiagnosticSceneDepth(IDirect3DDevice9* device);
void BeginDiagnosticHistoryAudit(IDirect3DDevice9* device);
void AuditDiagnosticHistoryDraw(IDirect3DDevice9* device, unsigned primitiveType,
                                unsigned primitiveCount, bool indexed);
void EndDiagnosticHistoryAudit(IDirect3DDevice9* device, unsigned slot, unsigned material,
                               unsigned indexCount);
bool ReadDiagnosticSceneHistory(IDirect3DDevice9* device, std::vector<std::uint8_t>& color,
                                std::vector<std::uint32_t>& depthStencil);
rhi::HistoryCapabilities DiagnosticHistoryFeatures(IDirect3DDevice9* device);
rhi::HistoryResult CaptureDiagnosticHistory(IDirect3DDevice9* device, rhi::HistoryData& output);
#else
inline bool PrepareDiagnosticSceneDepth(IDirect3DDevice9*) {
    return true;
}
#endif
}
