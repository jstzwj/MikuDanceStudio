// Recompile the production host boundary for this executable only. The engine
// callbacks can then target either the port or the original local DLL, while
// the application and all other tests retain their direct static calls.
#define Initialize OracleInitialize
#define Cleanup OracleCleanup
#define OnBeginScene OracleOnBeginScene
#define OnClear OracleOnClear
#define OnCreateModel OracleOnCreateModel
#define OnDeleteModel OracleOnDeleteModel
#define OnDrawIndexedPrimitive OracleOnDrawIndexedPrimitive
#define OnDrawPrimitive OracleOnDrawPrimitive
#define OnEndScene OracleOnEndScene
#define OnLostDevice OracleOnLostDevice
#define OnResetDevice OracleOnResetDevice
#include "../third_party/mmeffect/src/mme_host.cpp"
