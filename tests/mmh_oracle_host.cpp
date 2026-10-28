// Keep the production host state machine, replacing only terminal engine
// callbacks with observations. No effect-engine behavior is simulated here.
#define Initialize CaptureInitialize
#define Cleanup CaptureCleanup
#define OnBeginScene CaptureBeginScene
#define OnClear CaptureClear
#define OnCreateModel CaptureCreateModel
#define OnDeleteModel CaptureDeleteModel
#define OnDrawIndexedPrimitive CaptureIndexed
#define OnDrawPrimitive CapturePrimitive
#define OnEndScene CaptureEndScene
#define OnLostDevice CaptureLost
#define OnResetDevice CaptureReset
#include "../third_party/mmeffect/src/mme_host.cpp"
