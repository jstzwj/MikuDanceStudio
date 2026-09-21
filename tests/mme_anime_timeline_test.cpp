// Include the implementation to exercise its private FindFrame directly,
// without adding a production test API or requiring an image/D3D device.
#include "../third_party/mmeffect/src/mmeffect/anime_texture.cpp"

#include <cmath>
#include <cstdio>

namespace {
class Timeline final : public mme::AnimeObject {
public:
    explicit Timeline(unsigned int plays) : AnimeObject(nullptr, "")
    {
        loopCount = plays;
        totalDuration = 1.0;
        frameCount = 3;
        timeline = {{0.0, 0}, {0.25, 1}, {0.75, 2}};
    }
    void SetFrame(double) override {}
    IDirect3DBaseTexture9* GetTexture() override { return nullptr; }
    HRESULT RecreateTexture() override { return S_OK; }
    void ReleaseDynamicTexture() override {}
};

int failures = 0;
void Check(Timeline& animation, double time, int expected)
{
    const int actual = animation.FindFrame(time);
    if (actual != expected) {
        std::fprintf(stderr, "num_plays=%08x time=%.17g: frame %d, expected %d\n",
                     animation.loopCount, time, actual, expected);
        ++failures;
    }
    if (animation.FindFrame(time) != actual) {
        std::fprintf(stderr, "same-time cache changed the frame\n");
        ++failures;
    }
}
}

int main()
{
    for (unsigned int plays : {0u, 1u, 2u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        Timeline animation(plays);
        Check(animation, -1.0, 0);
        Check(animation, 0.0, 0);
        Check(animation, std::nextafter(0.25, 0.0), 0);
        Check(animation, 0.25, 1);
        Check(animation, std::nextafter(0.75, 0.0), 1);
        Check(animation, 0.75, 2);
        Check(animation, std::nextafter(1.0, 0.0), 2);
        Check(animation, 1.0, plays == 1 ? 2 : 0);
        Check(animation, 1.25, plays == 1 ? 2 : 1);
        Check(animation, 2.0, plays == 1 || plays == 2 ? 2 : 0);
        // Keep the quotient within int's range: the original converts to
        // signed int before its unsigned comparison (cvttsd2si then jb).
        Check(animation, 2147483646.25, plays == 1 || plays == 2 ? 2 : 1);
        Check(animation, 2147483647.0,
              plays == 1 || plays == 2 || plays == 0x7fffffffu ? 2 : 0);
        // Absolute-time seeking must also work backwards after a held frame.
        Check(animation, 0.25, 1);
    }
    Timeline shifted(0x80000000u);
    shifted.offset = 2.0;
    shifted.speed = 2.0;
    Check(shifted, 1.0, 0);
    Check(shifted, 2.125, 1);
    Check(shifted, 2.375, 2);
    Check(shifted, 2.5, 0);
    if (failures == 0) std::puts("animated texture timeline: passed");
    return failures == 0 ? 0 : 1;
}
