// Timeline label sweep (x86 0x42F1E0, x64 RVA 0x5F7F0).
// Row zero always contains the model's display root. A display group flag
// of zero draws a collapsed header; a nonzero flag also draws its children.
// Bone/morph row maps store positive expanded rows and negative collapsed
// headers. Offscreen entries remain zero; unused row records remain -999.
// The label sweep finishes by painting the timeline keys in PanelPaint.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {
namespace {

// --- panel layout constants --------------------------------------------------
constexpr std::size_t kListLeft = 91; // 0x5B  label column width

// --- model display-tree offsets (model object, PMD/PMX chain) ---------------
// The per-line bookkeeping lives in ModelRecord::boneListSelLine /
// boneListRowType / boneListRowRecord (typed fields since the x86-raw
// constants drifted from the x64 layout: the old 0x2E88 -999 reset run
// landed on x64 boneCount/facialFrameCount and corrupted live models).
// The display-object rows below read mdl::AccessoryRecord fields directly
// (name / order / rowSelected) - the former raw x86 offsets 0x238/0x49D/
// 0x4AC sat 16 bytes off the x64 record and matched no order byte.

// JP panel texts (Shift-JIS; original byte_52C95C/52C958/52C980/52C978/
// 52C96C/52C964).  EN texts are inline literals (asc_52C960/52C6D0,
// aCamera/aLight/aSShadow/aGravity).
constexpr char kJpPlus[] = "\x81\x7b";                           // ＋ (0x52C95C)
constexpr char kJpMinus[] = "\x81\x7c";                          // － (0x52C958)
constexpr char kJpCamera[] = "\x83\x4a\x83\x81\x83\x89";         // カメラ (0x52C980)
constexpr char kJpLight[] = "\x8f\xc6\x81\x40\x96\xbe";          // lighting (0x52C978)
constexpr char kJpShadow[] = "\x83\x5a\x83\x8b\x83\x74\x89\x65"; // self-shadow (0x52C96C)
constexpr char kJpGravity[] = "\x8f\x64\x81\x40\x97\xcd";        // gravity (0x52C964)

// Colour from the color.txt COLORREF `color` (themeColors[33] = 0xA065C
// normal / themeColors[34] = 0xA0660 highlight), split into the three bytes
// the original pushes.  COLORREF = 0x00BBGGRR, so little-endian byte 0 = R,
// 1 = G, 2 = B - the same bytes the former per-offset reads fetched.
void DrawPanelText(MMDApp* app, const char* text, HDC hdc, int size, int x, int y,
                   const std::uint32_t& color) {
    DrawGlyph(app, text, hdc, size, x, y, static_cast<std::uint8_t>(color & 0xFF),
              static_cast<std::uint8_t>((color >> 8) & 0xFF),
              static_cast<std::uint8_t>((color >> 16) & 0xFF), 1);
}

// Display-frame name row (0x42FB18/0x42FB98 and siblings).
void DrawFrameName(MMDApp* app, const mdl::DisplayGroup& frame, HDC hdc, int y,
                   const std::uint32_t& color) {
    const bool english = app->EnglishUI() != 0;
    DrawPanelText(app, english ? frame.nameEn : frame.name, hdc, 12, 12, y, color);
}

// Display bone / morph entry name row (0x430401/0x430443, 0x43020C/0x430247).
void DrawRecordName(MMDApp* app, const mdl::FrameGroup& record, HDC hdc, int y,
                    const std::uint32_t& color) {
    const bool english = app->EnglishUI() != 0;
    DrawPanelText(app, english ? record.nameEn : record.name, hdc, 12, 15, y, color);
}

// Fixed header label of the display-mode band column (0x42F292 etc.).
void DrawHeader(MMDApp* app, HDC hdc, const char* text, int y, const std::uint32_t& color) {
    DrawPanelText(app, text, hdc, 12, 12, y, color);
}

} // namespace

void RebuildTimelineTrackPanel(MMDApp* app) {
    auto& s = *app;
    const bool english = s.EnglishUI() != 0; // 658252
    HDC panel = s.PanelDC();                 // 724
    // listH = the 0x1D574 render wrapper's "list height" field
    // (screenHeight, sub+0x1D4E8).
    const std::int32_t listH = s.Renderer()->screenHeight;

    // --- 1. erase the label column from the panel bitmap (0x42F1F2) --------
    HDC dc = CreateCompatibleDC(nullptr);
    SelectObject(dc, reinterpret_cast<HGDIOBJ>(s.state.bmpPanelSpare)); // 732
    BitBlt(panel, 0, 0, kListLeft, listH, dc, 0, 0, SRCCOPY);
    DeleteDC(dc);

    s.ClearPanelRowFlags();

    if (s.CameraMode() == 0) {
        // === bone-edit mode: current model display tree (0x42F744) ========
        mdl::ModelRecord* const model = s.SelectedModel();
        auto* const boneLineByIndex = model->boneTimelineRows;
        auto* const morphLineByIndex = model->morphTimelineRows;
        const std::int32_t rootIdx = static_cast<std::int32_t>(model->displayRootBone);
        unsigned char* const flagTab = model->boneSelection;
        const mdl::BoneRecord& rootBone = model->boneTable[rootIdx];
        const char* rootName = english ? rootBone.nameEn : rootBone.name;
        if (flagTab[rootIdx] != 0) {
            DrawPanelText(app, rootName, panel, 12, 12, 17, s.state.themeColors[34]);
            s.PanelRowFlags()[0] = 1;
        } else {
            DrawPanelText(app, rootName, panel, 12, 12, 17, s.state.themeColors[33]);
        }

        // --- zero link tables, clear line records (0x42F88C..0x42F9FB) -----
        std::memset(boneLineByIndex, 0, sizeof(*boneLineByIndex) * model->boneCount);
        std::memset(morphLineByIndex, 0, sizeof(*morphLineByIndex) * model->morphCount);
        model->boneListSelLine = 0;
        for (int line0 = 0; line0 < 200; ++line0) {
            model->boneListRowType[line0] = 0;
            model->boneListRowRecord[line0] = -999;
        }
        model->boneListRowRecord[0] = rootIdx;
        model->boneListRows = 1;

        // Display headers, then expanded morph or bone entries.
        const mdl::DisplayGroup* const frames = model->displayGroups;
        const mdl::FrameGroup* const faces = model->displayFrames;
        const mdl::FrameGroup* const groups = model->displayBones;
        const std::int32_t displayBoneCount = static_cast<std::int32_t>(model->displayBoneCount);
        std::uint8_t frameIdx = 1;  // display-frame index
        std::uint8_t frameType = 1; // frame type carried onto the next line
        int line = 1;               // tree line counter
        std::uint8_t faceIdx = 0;   // face index (0x430138)
        if (model->groupCount <= 1) {
            PanelPaint(app);
            return;
        }
        for (;;) {
            const int frameNo = frameIdx; // frame index fixed for this pass
            const bool collapsed = frames[frameIdx].flags == 0;
            const std::int32_t iter = model->boneListRows;
            const std::int32_t iterMax = model->boneListPos;

            if (collapsed) {
                // Collapsed header: map its hidden children to -line (0x42FA7E).
                if (iter > iterMax && line < 200) {
                    if (english) {
                        if (frameIdx > 1)
                            DrawGlyph(app, "+", panel, 12, 1, 14 * line + 17, 0, 0, 0, 1);
                        DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                      s.state.themeColors[33]);
                    } else {
                        if (frameIdx > 1)
                            DrawGlyph(app, kJpPlus, panel, 12, 1, 14 * line + 17, 0, 0, 0, 1);
                        DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                      s.state.themeColors[33]);
                    }
                    if (((frameType == 1) & model->displayKeyframesPresent) != 0) {
                        DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                      s.state.themeColors[34]);
                        s.PanelRowFlags()[line] = 1;
                    }
                    if (model->facialFrameCount != 0 && frameType == 2) {
                        // frame 2: highlight when any face flag (+44) is
                        // set - first face with a live flag (0x42FC57,
                        // LABEL_76 skipped when none is live).
                        bool anyFaceSelected = false;
                        for (unsigned int f = 0; f < model->facialFrameCount; ++f) {
                            if (faces[f].selected != 0) {
                                anyFaceSelected = true;
                                break;
                            }
                        }
                        if (anyFaceSelected) {
                            DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                          s.state.themeColors[34]);
                            s.PanelRowFlags()[line] = 1;
                        }
                    }
                    // Highlight a collapsed group if one of its bones is selected.
                    // (0x42FD26; LABEL_86 skipped when none matched).
                    if (displayBoneCount) {
                        bool anyBoneSelected = false;
                        for (int g = 0; g < displayBoneCount; ++g) {
                            if (groups[g].groupIndex == frameNo &&
                                flagTab[groups[g].targetIndex] != 0) {
                                anyBoneSelected = true;
                                break;
                            }
                        }
                        if (anyBoneSelected) {
                            DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                          s.state.themeColors[34]);
                            s.PanelRowFlags()[line] = 1;
                        }
                    }
                    // Header type and signed morph/bone row maps (0x42FE37)
                    model->boneListRowType[line] = frameType;
                    if (frameType == 1)
                        model->boneListSelLine = line;
                    for (std::uint8_t j = 0; j < model->facialFrameCount; ++j) {
                        const mdl::FrameGroup& face = faces[j];
                        if (face.groupIndex + 1 == frameNo)
                            morphLineByIndex[face.targetIndex] = -line;
                    }
                    if (displayBoneCount) {
                        for (unsigned int g = 0; g < static_cast<unsigned int>(displayBoneCount);
                             ++g) {
                            const mdl::FrameGroup& entry = groups[g];
                            if (entry.groupIndex == frameNo)
                                boneLineByIndex[entry.targetIndex] = -line;
                        }
                    }
                    frameIdx = frameType;
                    ++line;
                }
                ++model->boneListRows;
            } else {
                // Expanded header followed by morph or bone rows (0x42FF94).
                if (iter > iterMax && line < 200) {
                    if (english) {
                        if (frameIdx > 1)
                            DrawGlyph(app, "-", panel, 12, 1, 14 * line + 17, 0, 0, 0, 1);
                        DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                      s.state.themeColors[33]);
                    } else {
                        if (frameIdx > 1)
                            DrawGlyph(app, kJpMinus, panel, 12, 1, 14 * line + 17, 0, 0, 0, 1);
                        DrawFrameName(app, frames[frameIdx], panel, 14 * line + 17,
                                      s.state.themeColors[33]);
                    }
                    model->boneListRowType[line++] = frameType;
                    frameIdx = frameType;
                }
                ++model->boneListRows;
                if (frameIdx == 1) {
                    model->boneListSelLine = line - 1;
                } else if (model->facialFrameCount != 0 && frameIdx == 2) {
                    // face section (frame 2) - list the frame's face records
                    // (0x430153..0x43030B; LABEL_122/123/139 were the skip /
                    // advance / exhausted edges of this loop).
                    faceIdx = 0;       // 0x430138
                    int recOff = line; // index into boneListRowRecord
                    int rowY = 14 * line + 17;
                    for (;;) {
                        if (model->boneListRows > model->boneListPos && line < 200) {
                            const mdl::FrameGroup& face = faces[faceIdx];
                            if (english) {
                                DrawPanelText(app, face.nameEn, panel, 12, 15, rowY,
                                              s.state.themeColors[face.selected != 0 ? 34 : 33]);
                            } else {
                                DrawPanelText(app, face.name, panel, 12, 15, rowY,
                                              s.state.themeColors[face.selected != 0 ? 34 : 33]);
                            }
                            if (face.selected != 0) // LABEL_122 skip
                                s.PanelRowFlags()[line] = 1;
                            model->boneListRowRecord[recOff] = -1 - face.targetIndex;
                            morphLineByIndex[face.targetIndex] = line++;
                            ++recOff;
                            rowY += 14;
                        }
                        ++model->boneListRows; // LABEL_123
                        if (++faceIdx >= model->facialFrameCount)
                            break; // LABEL_139
                    }
                } else if (displayBoneCount) {
                    // Display bone section (0x43031D..0x4304D8)
                    int recOff = line; // index into boneListRowRecord
                    int rowY = 14 * line + 17;
                    for (unsigned int g = 0; g < static_cast<unsigned int>(displayBoneCount); ++g) {
                        const mdl::FrameGroup& entry = groups[g];
                        if (entry.groupIndex == frameNo) {
                            if (model->boneListRows > model->boneListPos && line < 200) {
                                if (flagTab[entry.targetIndex] != 0) {
                                    DrawRecordName(app, entry, panel, rowY,
                                                   s.state.themeColors[34]);
                                    s.PanelRowFlags()[line] = 1;
                                } else {
                                    DrawRecordName(app, entry, panel, rowY,
                                                   s.state.themeColors[33]);
                                }
                                model->boneListRowRecord[recOff] = entry.targetIndex;
                                boneLineByIndex[entry.targetIndex] = line++;
                                ++recOff;
                                rowY += 14;
                            }
                            ++model->boneListRows;
                        }
                    }
                }
            }

            // LABEL_140 advance: next display frame
            frameType = ++frameIdx;
            if (frameIdx >= model->groupCount) {
                PanelPaint(app);
                return;
            }
        }
    } else {
        // === display mode: fixed headers camera/light/shadow/gravity =======
        // (0x42F252..0x42F5F0)
        if (english) {
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Camera)) {
                DrawHeader(app, panel, "camera", 17, s.state.themeColors[34]);
                s.PanelRowFlags()[0] = 1;
            } else {
                DrawHeader(app, panel, "camera", 17, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Light)) {
                DrawHeader(app, panel, "light", 31, s.state.themeColors[34]);
                s.PanelRowFlags()[1] = 1;
            } else {
                DrawHeader(app, panel, "light", 31, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::SelfShadow)) {
                s.PanelRowFlags()[2] = 1;
                DrawHeader(app, panel, "s shadow", 45, s.state.themeColors[34]);
            } else {
                DrawHeader(app, panel, "s shadow", 45, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Gravity)) {
                s.PanelRowFlags()[3] = 1;
                DrawHeader(app, panel, "gravity", 59, s.state.themeColors[34]);
            } else {
                DrawHeader(app, panel, "gravity", 59, s.state.themeColors[33]);
            }
        } else {
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Camera)) {
                DrawHeader(app, panel, kJpCamera, 17, s.state.themeColors[34]);
                s.PanelRowFlags()[0] = 1;
            } else {
                DrawHeader(app, panel, kJpCamera, 17, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Light)) {
                DrawHeader(app, panel, kJpLight, 31, s.state.themeColors[34]);
                s.PanelRowFlags()[1] = 1;
            } else {
                DrawHeader(app, panel, kJpLight, 31, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::SelfShadow)) {
                s.PanelRowFlags()[2] = 1;
                DrawHeader(app, panel, kJpShadow, 45, s.state.themeColors[34]);
            } else {
                DrawHeader(app, panel, kJpShadow, 45, s.state.themeColors[33]);
            }
            if (s.GlobalTrackSelected(GlobalTimelineTrack::Gravity)) {
                s.PanelRowFlags()[3] = 1;
                DrawHeader(app, panel, kJpGravity, 59, s.state.themeColors[34]);
            } else {
                DrawHeader(app, panel, kJpGravity, 59, s.state.themeColors[33]);
            }
        }
    }

    // === accessory list below the fixed bands (0x42F5F6..0x42F72A) ==========
    // Walks the accessory slots grouped by their order byte 0..254 and
    // appends one panel row per loaded accessory, recording slot -> row in
    // jointLineMap for the click/hover handlers.
    std::memset(s.state.jointLineMap, 0xFF, sizeof s.state.jointLineMap);
    s.DisplayObjectListMatchCount() = 1;
    std::uint8_t order = 0; // accessory order byte 0..254
    int ordinal = 0;        // order counter (scroll gate)
    int line = 0;           // row below the fixed bands
    do {
        int slot = 0; // accessory slot index
        int rowY = 14 * line + 73;
        std::int32_t* map = s.state.jointLineMap + line;
        do {
            mdl::AccessoryRecord* accessory = s.AccessorySlot(slot);
            if (accessory != nullptr && accessory->order == order) {
                if (ordinal >= s.DisplayObjectListScrollPosition() && line < 200) {
                    if (accessory->rowSelected != 0) {
                        DrawPanelText(app, accessory->name, panel, 12, 12, rowY,
                                      s.state.themeColors[34]);
                        s.PanelRowFlags()[4 + line] = 1;
                    } else {
                        DrawPanelText(app, accessory->name, panel, 12, 12, rowY,
                                      s.state.themeColors[33]);
                    }
                    *map = slot;
                    ++line;
                    ++map;
                    rowY += 14;
                }
                ++s.DisplayObjectListMatchCount();
            }
            ++slot;
        } while (slot < 255);
        ++ordinal;
        ++order;
    } while (order != 0xFF);

    PanelPaint(app); // 0x42F730 tail call (0x414610)
}

} // namespace mikudancestudio
