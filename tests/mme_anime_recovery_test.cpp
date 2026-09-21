// Exercise the real SetFrame and device-loss/reset walks. Only the virtual
// texture allocator is replaced, so allocation attempts are observable without
// depending on GPU allocation failure or dereferencing a lost texture.
#include "../third_party/mmeffect/src/mmeffect/anime_texture.cpp"
#include <cstdio>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); ++failures; }
}

template<class Animation>
class AllocationProbe final : public Animation {
public:
    AllocationProbe() : Animation(nullptr, "") {
        this->frameCount = 2;
        this->timeline = {{0.0, 0}, {0.5, 1}};
        this->totalDuration = 1.0;
    }
    HRESULT RecreateTexture() override {
        ++allocations;
        this->currentFrame = -1;
        return result;
    }
    int allocations = 0;
    HRESULT result = E_OUTOFMEMORY;
};

template<class Animation>
void CheckRecovery() {
    AllocationProbe<Animation> animation;
    mme::AnimeEntry entry;
    entry.object = &animation;
    mme::AnimeSetImpl impl;
    impl.entries.push_back(&entry);
    mme::MmeAnimatedTextureSet set;
    set.impl = &impl;

    // Texture loss does not transfer ownership of recovery to frame updates.
    mme::MmeAnimeOnDeviceLost(&set);
    animation.SetFrame(0.0);
    animation.SetFrame(0.5);
    Check(animation.allocations == 0, "SetFrame attempted implicit recovery");
    Check(animation.CurrentFrame() == 1, "frame selection lost after texture loss");

    Check(mme::MmeAnimeOnDeviceReset(&set) == E_OUTOFMEMORY,
          "explicit reset did not propagate texture allocation failure");
    Check(animation.allocations == 1, "reset must call allocator exactly once");
    animation.SetFrame(0.5);
    animation.SetFrame(0.0);
    Check(animation.allocations == 1, "frame updates retried failed reset");

    animation.result = S_OK;
    Check(mme::MmeAnimeOnDeviceReset(&set) == S_OK,
          "explicit reset retry failed");
    Check(animation.allocations == 2, "explicit retry did not allocate once");
    Check(animation.CurrentFrame() == -1, "reset did not invalidate selected frame");
    animation.SetFrame(0.0);
    Check(animation.CurrentFrame() == 0, "same-time frame not selected after reset");
    Check(animation.allocations == 2, "same-time replay allocated a texture");
    set.impl = nullptr; // fixture entries and animation are stack-owned
}
}

int main() {
    CheckRecovery<mme::AnimeGif>();
    CheckRecovery<mme::AnimePng>();
    std::printf("animated texture recovery: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
