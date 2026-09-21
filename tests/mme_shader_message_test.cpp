// Run the production technique/pass scanner with real D3DX effects. Only the
// modal Win32 boundary is replaced; logging and phase suppression stay real.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../third_party/mmeffect/src/mmeffect/mme_log.h"
struct CapturedMessage { HWND owner; std::string text, title, log; UINT flags; };
static std::vector<CapturedMessage> messages;
static int WINAPI CaptureMessageBox(HWND owner, LPCSTR text, LPCSTR title, UINT flags) {
    messages.push_back({owner, text, title, mme::MmeGetLogDialogText(), flags});
    return IDOK;
}
#define MessageBoxA CaptureMessageBox
#include "../third_party/mmeffect/src/mmeffect/sas_interpreter.cpp"
#include "../third_party/mmeffect/src/mmeffect/mme_log.cpp"
#undef MessageBoxA

static int failures;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
int main() {
    HWND window = CreateWindowW(L"STATIC", L"Shader scan test", WS_OVERLAPPED,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window || !d3d) { std::puts("SKIP: window or Direct3D unavailable"); return 77; }
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window; pp.BackBufferWidth = pp.BackBufferHeight = 64;
    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) { std::printf("SKIP: HAL CreateDevice: %08lx\n", hr); return 77; }
    const char source[] =
        "float4 VS(float4 p:POSITION):POSITION {return p;}\n"
        "float4 PS():COLOR0 {return float4(1,1,1,1);}\n"
        "technique Mixed {pass P {VertexShader=compile vs_3_0 VS();"
        "PixelShader=compile ps_2_0 PS();}}\n"
        "technique Matched {pass P {VertexShader=compile vs_3_0 VS();"
        "PixelShader=compile ps_3_0 PS();}}\n"
        "technique Missing {pass P {VertexShader=compile vs_3_0 VS();}}\n";
    ID3DXEffect* effect = nullptr; ID3DXBuffer* errors = nullptr;
#ifdef _WIN64
    HMODULE runtime = LoadLibraryW(L"d3dx9_43.dll");
#else
    HMODULE runtime = LoadLibraryW(L"d3dx9_32.dll");
#endif
    using CreateEffect = HRESULT (WINAPI*)(IDirect3DDevice9*, LPCVOID, UINT,
        const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    auto createEffect = reinterpret_cast<CreateEffect>(GetProcAddress(runtime, "D3DXCreateEffect"));
    if (!createEffect) return 2;
    hr = createEffect(device, source, sizeof(source)-1, nullptr, nullptr,
        0, nullptr, &effect, &errors);
    if (FAILED(hr)) {
        std::printf("Compile: %08lx %s\n", hr,
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        return 2;
    }
    if (errors) errors->Release();
    mme::g_mainWindow = window;
    const std::string line = "Error: vs_3_0 or ps_3_0 may not be used with any other "
        "shader versions. (pass: P, technique: Mixed)\n";
    auto scan = [&](const char* path, const char* technique, bool mixed) {
        mme::SasEffect sas; sas.effect = effect; sas.device = device; sas.path = path;
        Check(!mme::SasScanTechnique(&sas, effect->GetTechniqueByName(technique)),
            "shader mix is diagnostic, not fatal");
        Check(!sas.hasErrors, "shader mix does not mark a parse failure");
        Check(sas.techniques.back().shaderMixOk == !mixed, "technique mix diagnostic");
        Check(sas.techniques.back().passes.front().shaderMixOk == !mixed, "pass mix diagnostic");
        Check(sas.log.empty(), "direct shader diagnostic is not a local fatal error");
    };
    scan("a.fx", "Mixed", true); scan("a.fx", "Mixed", true);
    Check(messages.size()==2, "outside phase repeats display");
    mme::MmeLogFlush(1);
    scan("a.fx", "Mixed", true); scan("a.fx", "Mixed", true);
    Check(messages.size()==3, "active phase suppresses identical message");
    scan("b.fx", "Mixed", true);
    Check(messages.size()==4, "different path displays independently");
    mme::MmeLogFlush(1); scan("a.fx", "Mixed", true);
    Check(messages.size()==4, "same phase retains suppression");
    mme::MmeLogFlush(0); mme::MmeLogFlush(1); scan("a.fx", "Mixed", true);
    Check(messages.size()==5, "new phase displays again");
    scan("a.fx", "Matched", false);
    Check(messages.size()==5, "matching 3.0 shaders do not display");
    scan("a.fx", "Missing", true);
    Check(messages.size()==6, "missing pixel shader displays");
    for (size_t i=0; i<messages.size(); ++i) {
        const auto& m = messages[i];
        Check(m.owner==window && m.title=="MikuMikuEffect" && m.flags==MB_ICONERROR,
            "modal owner caption flags");
        if (i<5) Check(m.text == std::string(i==3 ? "b.fx" : "a.fx")+"\n\n"+line,
            "path blank line and complete error text");
        const std::string detail = i==5 ? "technique: Missing)\r\n" : "technique: Mixed)\r\n";
        Check(m.log.size()>=detail.size() &&
            m.log.compare(m.log.size()-detail.size(), detail.size(), detail)==0,
            "global log precedes modal boundary");
    }
    const std::string history = mme::MmeGetLogDialogText();
    size_t count = 0, pos = 0;
    while ((pos=history.find("Error: vs_3_0",pos)) != std::string::npos) { ++count; ++pos; }
    Check(count==8, "suppressed dialogs still log every error");

    // Exercise the public loader and selector after the diagnostic. A mixed
    // pass can be D3DX-invalid while both versions fit the hardware caps.
    std::string failureLog;
    mme::SasEffect* parsed = mme::SasParse(effect, "load.fx", device, &failureLog);
    Check(parsed != nullptr, "pure shader mixing does not reject the effect");
    if (parsed) {
        D3DCAPS9 caps = {};
        Check(device->GetDeviceCaps(&caps)==S_OK, "read real shader capabilities");
        for (const auto& technique : parsed->techniques) {
            bool capsOk = true;
            for (const auto& pass : technique.passes) {
                D3DXPASS_DESC desc = {};
                Check(effect->GetPassDesc(pass.handle, &desc)==S_OK, "read real pass description");
                const unsigned vs = desc.pVertexShaderFunction ?
                    D3DXGetShaderVersion(desc.pVertexShaderFunction)&0xffff : 0;
                const unsigned ps = desc.pPixelShaderFunction ?
                    D3DXGetShaderVersion(desc.pPixelShaderFunction)&0xffff : 0;
                capsOk = capsOk && vs <= (caps.VertexShaderVersion&0xffff) &&
                    ps <= (caps.PixelShaderVersion&0xffff);
            }
            Check(technique.shaderCapsOk==capsOk, "capability validity follows actual shader versions");
            Check(technique.hardwareOk==(effect->ValidateTechnique(technique.handle)==S_OK),
                "D3DX validation remains independent");
        }
        const bool oldSkip = mme::g_skipValidation;
        for (int skip=0; skip<2; ++skip) {
            mme::g_skipValidation = skip != 0;
            D3DXHANDLE expected = nullptr; bool expectedValid = false;
            for (const auto& technique : parsed->techniques) {
                expected = technique.handle;
                expectedValid = skip ? technique.shaderCapsOk : technique.hardwareOk;
                if (expectedValid) break;
            }
            bool valid = false;
            Check(mme::SasSelectTechnique(parsed, 0, 0, false, false, false, &valid)==expected &&
                valid==expectedValid, "selector uses D3DX validation or caps, not mixing");
        }
        // An invalid caps result must still make the selector try the next
        // candidate, even though mixing is no longer a fatal parse error.
        if (parsed->techniques.size()>1) {
            parsed->techniques[0].shaderCapsOk = false;
            parsed->techniques[1].shaderCapsOk = true;
            mme::g_skipValidation = true;
            bool valid = false;
            Check(mme::SasSelectTechnique(parsed, 0, 0, false, false, false, &valid)==
                parsed->techniques[1].handle && valid, "caps rejection still advances selection");
        }
        mme::g_skipValidation = oldSkip;
        mme::SasUnload(parsed);
    }
    auto invalidAnnotation = [&](const char* annotation) {
        const std::string badSource =
            std::string("float4 VS(float4 p:POSITION):POSITION{return p;}\n") +
            "float4 PS():COLOR0{return 1;}\ntechnique Bad {pass P <" + annotation +
            "> {VertexShader=compile vs_3_0 VS();PixelShader=compile ps_2_0 PS();}}";
        ID3DXEffect* bad = nullptr; ID3DXBuffer* compilerErrors = nullptr;
        const HRESULT result = createEffect(device, badSource.data(), static_cast<UINT>(badSource.size()),
            nullptr, nullptr, 0, nullptr, &bad, &compilerErrors);
        Check(SUCCEEDED(result), "invalid SAS annotation is valid D3DX FX input");
        if (compilerErrors) compilerErrors->Release();
        if (!bad) return;
        mme::SasEffect sas; sas.effect = bad; sas.device = device; sas.path = "bad.fx";
        Check(mme::SasScanTechnique(&sas, bad->GetTechnique(0)), "real annotation/script error remains fatal");
        failureLog.clear();
        mme::SasEffect* rejected = mme::SasParse(bad, "bad.fx", device, &failureLog);
        Check(rejected==nullptr && !failureLog.empty(), "loader preserves real failure and error log");
        if (rejected) mme::SasUnload(rejected);
        bad->Release();
    };
    invalidAnnotation("int Script=1;");
    invalidAnnotation("string Script=\"invalid syntax\";");
    effect->Release(); device->Release(); d3d->Release(); DestroyWindow(window);
    std::printf("shader message: %d failures, %zu captured messages\n", failures, messages.size());
    return failures ? 1 : 0;
}
