// Test fixture only: never install beside the product executable.
// Lets the loader succeed while every required D3DX entry point is absent.
extern "C" __declspec(dllexport) int IncompleteD3dxRuntimeFixture() { return 0; }
