// A software-only DxOpenNI ABI fixture; no sensor or graphics device is used.
#include "mikudancestudio/openni_api.hpp"

namespace {
float version = 1.5f;
bool initializeSucceeds = true;
int initializations, cleanups;
}
extern "C" {
bool WINAPI FixtureInit(HWND, bool, IDirect3DDevice9*, const wchar_t*, const char*) {
    ++initializations;
    return initializeSucceeds;
}
void WINAPI FixtureClean() {
    ++cleanups;
}
void WINAPI FixtureDraw(bool) {
}
void WINAPI FixtureTexture(IDirect3DTexture9** texture) {
    *texture = nullptr;
}
void WINAPI FixtureJoint(int joint, float* position) {
    position[0] = static_cast<float>(joint);
    position[1] = 100;
    position[2] = 200;
}
void WINAPI FixtureTracking(bool* active) {
    *active = true;
}
void WINAPI FixtureVersion(float* output) {
    *output = version;
}
void WINAPI FixtureConfigure(float requested, bool succeeds) {
    version = requested;
    initializeSucceeds = succeeds;
    initializations = cleanups = 0;
}
int WINAPI FixtureCalls(bool clean) {
    return clean ? cleanups : initializations;
}
}
