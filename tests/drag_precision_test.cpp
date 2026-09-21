#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace mikudancestudio;
std::uint32_t Bits(float v) { std::uint32_t bits; std::memcpy(&bits,&v,4); return bits; }
float& Position(MMDApp& app, int axis) {
    if (axis == 0) return app.CameraPositionX();
    if (axis == 1) return app.CameraPositionY();
    return app.CameraPositionZ();
}
int main() {
#ifndef _WIN64
    std::puts("x64 SSE arithmetic fixtures do not apply to x86");
    return 77;
#else
    struct Case { int shift, ctrl, dy; std::uint32_t accessoryPosition, cameraPosition, rotation; };
    // Independent IEEE-754 binary32 instruction replay; do not calculate the
    // oracle with the production expression or a tolerance.
    const Case cases[] = {
    {0, 0, 3, 0xBD4CCCCEu, 0x3E800000u, 0x3E23D70Au},
    {0, 0, -3, 0x3E800000u, 0xBD4CCCCEu, 0x3D23D70Bu},
    {0, 0, 16777217, 0xC94CCCCBu, 0x494CCCCFu, 0x48A3D70Du},
    {3, 0, 3, 0xBFB33333u, 0x3FCCCCCDu, 0x3F333334u},
    {3, 0, -3, 0x3FCCCCCDu, 0xBFB33333u, 0xBF000000u},
    {3, 0, 16777217, 0xCB000000u, 0x4B000000u, 0x4A4CCCCDu},
    {0, 3, 3, 0x3DAE147Bu, 0x3DEB851Fu, 0x3DD91687u},
    {0, 3, -3, 0x3DEB851Fu, 0x3DAE147Bu, 0x3DC08313u},
    {0, 3, 16777217, 0xC7A3D6FDu, 0x47A3D717u, 0x47031289u},
    {3, 3, 3, 0xBFB33333u, 0x3FCCCCCDu, 0x3F333334u},
    {3, 3, -3, 0x3FCCCCCDu, 0xBFB33333u, 0xBF000000u},
    {3, 3, 16777217, 0xCB000000u, 0x4B000000u, 0x4A4CCCCDu},
    };
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    mdl::AccessoryRecord accessory{};
    app->AccessorySlot(0) = &accessory;
    int failures = 0, checks = 0;
    for (const auto& c : cases) for (int axis=0; axis<3; ++axis) {
        app->ShiftModifierState()=c.shift;
        app->CtrlModifierState()=c.ctrl;
        app->PreviousMouseY()=0;
        app->MouseY()=c.dy;
        // Accessory, direct camera, parented camera, and view-relative camera.
        for (int branch=0; branch<4; ++branch) {
            app->state.coordinateSystem=branch==0 ? 2 : branch==1 ? 1 : 0;
            app->state.optflag[0]=1;
            app->state.cameraParentModel=branch==2 ? 0 : -1;
            for (int i=0;i<3;++i) { Position(*app,i)=0.1f; app->CameraRotation()[i]=0; accessory.position[i]=0.1f; }
            ModeCameraAdjust(app.get(),axis);
            const auto actual=Bits(branch==0 ? accessory.position[axis] : Position(*app,axis));
            const auto expected=branch==0 ? c.accessoryPosition : c.cameraPosition;
            ++checks;
            if(actual!=expected) { ++failures; std::printf("position branch %d axis %d shift %d ctrl %d dy %d: %08X != %08X\n",branch,axis,c.shift,c.ctrl,c.dy,actual,expected); }
        }
        for (int branch=0;branch<2;++branch) {
            app->state.coordinateSystem=branch==0 ? 2 : 1;
            app->state.optflag[0]=1;
            accessory.rotation[axis]=app->CameraRotation()[axis]=0.1f;
            ModeAngleAdjust(app.get(),axis);
            const auto actual=Bits(branch==0 ? accessory.rotation[axis] : app->CameraRotation()[axis]);
            ++checks;
            if(actual!=c.rotation) { ++failures; std::printf("rotation branch %d axis %d shift %d ctrl %d dy %d: %08X != %08X\n",branch,axis,c.shift,c.ctrl,c.dy,actual,c.rotation); }
        }
    }
    // Angle target 2 selects the accessory independently of the camera radio
    // flag. A missing accessory is a no-op, not a fall-through to the camera.
    // Radio flag zero also refreshes the selected model's bone readout.
    auto model = std::make_unique<mdl::ModelRecord>();
    model->selectedBone = -1;
    app->SetSelectedModelSlot(0);
    app->ModelSlot(0) = reinterpret_cast<unsigned char*>(model.get());
    app->ShiftModifierState() = app->CtrlModifierState() = 0;
    app->PreviousMouseY() = 0;
    app->MouseY() = 3;
    for (int axis = 0; axis < 3; ++axis)
        for (int target = 0; target < 3; ++target)
            for (int flag = 0; flag < 2; ++flag)
                for (int present = 0; present < 2; ++present) {
                    app->state.coordinateSystem = target;
                    app->state.optflag[0] = static_cast<unsigned char>(flag);
                    app->AccessorySlot(0) = present ? &accessory : nullptr;
                    for (int i = 0; i < 3; ++i)
                        accessory.rotation[i] = app->CameraRotation()[i] = 0.1f;
                    ModeAngleAdjust(app.get(), axis);
                    bool cameraOk = true, accessoryOk = true;
                    for (int i = 0; i < 3; ++i) {
                        const auto cameraExpected = i == axis && target != 2
                            ? 0x3E23D70Au : 0x3DCCCCCDu;
                        const auto accessoryExpected = i == axis && target == 2 && present
                            ? 0x3E23D70Au : 0x3DCCCCCDu;
                        cameraOk &= Bits(app->CameraRotation()[i]) == cameraExpected;
                        accessoryOk &= Bits(accessory.rotation[i]) == accessoryExpected;
                    }
                    checks += 2;
                    if (!cameraOk) { ++failures; std::printf("angle camera gate axis %d target %d flag %d present %d\n", axis, target, flag, present); }
                    if (!accessoryOk) { ++failures; std::printf("angle accessory gate axis %d target %d flag %d present %d\n", axis, target, flag, present); }
                }
    // Position dragging deliberately retains its different radio-flag gate.
    app->AccessorySlot(0) = &accessory;
    app->state.coordinateSystem = 2;
    app->state.cameraParentModel = -1;
    for (int axis = 0; axis < 3; ++axis) for (int flag = 0; flag < 2; ++flag) {
        app->state.optflag[0] = static_cast<unsigned char>(flag);
        for (int i = 0; i < 3; ++i) {
            app->CameraRotation()[i] = 0;
            Position(*app, i) = accessory.position[i] = 0.1f;
        }
        ModeCameraAdjust(app.get(), axis);
        checks += 2;
        if (Bits(Position(*app, axis)) != (flag ? 0x3DCCCCCDu : 0x3E800000u)) {
            ++failures; std::printf("position camera gate axis %d flag %d\n", axis, flag);
        }
        if (Bits(accessory.position[axis]) != (flag ? 0xBD4CCCCEu : 0x3DCCCCCDu)) {
            ++failures; std::printf("position accessory gate axis %d flag %d\n", axis, flag);
        }
    }
    // Display conversion must round division and multiplication before printf.
    HWND window = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 0, 0,
                                  nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    HWND edit = CreateWindowExA(0, "EDIT", "", WS_CHILD, 0, 0, 0, 0,
                                window, reinterpret_cast<HMENU>(0x1E1),
                                GetModuleHandleA(nullptr), nullptr);
    if (!window || !edit) { std::fputs("cannot create readout controls\n", stderr); return 1; }
    app->Hwnd() = window;
    app->state.coordinateSystem = 2;
    app->state.optflag[0] = 1;
    app->MouseY() = app->PreviousMouseY() = 0;
    accessory.rotation[0] = 1000.0f;
    ModeAngleAdjust(app.get(), 0);
    char readout[80]{};
    GetWindowTextA(edit, readout, sizeof(readout));
    ++checks;
    if (std::strcmp(readout, "57295.7891") != 0) {
        ++failures; std::printf("angle readout: %s != 57295.7891\n", readout);
    }
    app->Hwnd() = nullptr;
    DestroyWindow(window);
    std::printf("drag precision: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
#endif
}
