#pragma once
#include <cstdint>
namespace mikudancestudio::render {
// Values are the original PMM/MMD capture modes.
enum class ScreenCaptureKind : std::int32_t {
    Disabled = 0,
    FullFrame = 1,
    CropFourByThree = 2,
    BackgroundRefresh = 3
};
struct ScreenCaptureRect {
    int left = 0, top = 0, right = 0, bottom = 0;
};
// Preserve the native double arithmetic and truncation toward zero.
inline ScreenCaptureRect ScreenSourceRect(ScreenCaptureRect source, ScreenCaptureKind mode) {
    if (mode == ScreenCaptureKind::CropFourByThree) {
        const double width = double(source.right - source.left);
        const double height = double(source.bottom - source.top);
        const double targetWidth = height * 4.0 / 3.0;
        if (targetWidth < width) {
            const double center = double(source.left + source.right) * .5;
            source.left = int(center - targetWidth * .5);
            source.right = int(center + targetWidth * .5);
        } else {
            const double targetHeight = width * 3.0 / 4.0;
            const double center = double(source.top + source.bottom) * .5;
            source.top = int(center - targetHeight * .5);
            source.bottom = int(center + targetHeight * .5);
        }
    }
    return source;
}
}
