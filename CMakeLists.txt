cmake_minimum_required(VERSION 3.21)

project(MikuDanceStudio LANGUAGES CXX)

# ---------------------------------------------------------------------------
# MikuDanceStudio: behavioural restoration of MikuMikuDance v932.
# MikuMikuDanceE_v932x64 is the x64 reference.  The x86 binary remains a
# source-recovery aid only; see docs/X64_RECONSTRUCTION.md for the evidence
# and layout rules.  The porting ledger lives in docs/PORTING_STATUS.md.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W3 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # The original binary tolerates implicit narrowing everywhere; keep the
    # port close to VC9 behaviour rather than modern strictness.
    add_compile_options(/wd4267 /wd4244 /wd4245)
    # VC9 had no C++17 aligned operator new/delete (P0035).  Without this
    # switch our TUs allocate over-aligned Bullet objects (e.g. the 1456-byte
    # btGeneric6DofSpringConstraint, align 16) through _aligned_malloc while
    # the bullet275 static library's deleting destructors free them with the
    # plain operator delete - free() on an _aligned_malloc block, which
    # corrupts the heap and kills the process at teardown (0xC0000374,
    # see ShutdownCleanup -> DisposePhysicsWorld).  The original shipped
    # exactly the plain-new pairing, so this is also the faithful setting.
    add_compile_options(/Zc:alignedNew-)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS
        "Configuring the x64 build (reference: MikuMikuDanceE_v932x64, "
        "VC9 SSE2, d3dx9_43).  Fields are admitted only through the x64 "
        "evidence and parity contract in docs/X64_RECONSTRUCTION.md.")
endif()

if (NOT WIN32)
    message(FATAL_ERROR "MikuDanceStudio targets Windows only (Win32/DirectX9).")
endif()

# ---------------------------------------------------------------------------
# Dependencies (provisioned by Conan; see conanfile.py / recipes/)
# ---------------------------------------------------------------------------
find_package(BULLET275 REQUIRED)

# ---------------------------------------------------------------------------
# Core static library: application state, math, startup chain, effect API.
# ---------------------------------------------------------------------------
add_library(mikudancestudio_core STATIC
    src/app/mmd_app.cpp
    src/app/globals.cpp
    src/app/app_ctor.cpp
    src/app/app_defaults.cpp
    src/main/charset_conv.cpp
    src/math/quat_to_mat3x4.cpp
    src/math/color_lerp.cpp
    src/math/math_kernel_ledger.cpp
    src/window/path_utils.cpp
    src/window/window_init.cpp
    src/window/ui_init.cpp
    src/window/ui_font.cpp
    src/window/wndproc.cpp
    src/window/localize_ui.cpp
    src/window/wndproc_aux_windows.cpp
    src/window/command_dispatch.cpp
    src/window/frame_line_edit.cpp
    src/window/accessory_paste.cpp
    src/media/wave_audio.cpp
    src/media/media_load.cpp
    src/window/command_impl.cpp
    src/app/frame_driver.cpp
    src/app/playback_catchup.cpp
    src/app/playback_state.cpp
    src/app/timeline_advance.cpp
    src/app/frame_modes.cpp
    src/app/frame_modes_bone.cpp
    src/app/key_ladder.cpp
    src/app/pump_edit_keys.cpp
    src/app/pump_navigation.cpp
    src/physics/physics_create.cpp
    src/physics/physics_frame.cpp
    src/physics/scene_create.cpp
    src/render/d3d_init.cpp
    src/render/render_states.cpp
    src/render/stereo_nvapi.cpp
    src/render/frame_scene.cpp
    src/render/sprite_overlay.cpp
    src/render/overlay_producers.cpp
    src/render/capture_downsample.cpp
    src/render/debug_geometry.cpp
    src/render/accessory.cpp
    src/render/model_renderers.cpp
    src/render/bg_overlay.cpp
    src/render/device_reset.cpp
    src/app/avi_record_start.cpp
    src/app/record_readback.cpp
    src/app/dshow_record_graph.cpp
    src/model/model_init.cpp
    src/model/path_resolve.cpp
    src/model/pmd_load.cpp
    src/model/bone_transform.cpp
    src/model/model_keyframe_advance.cpp
    src/model/model_keyframe_edit.cpp
    src/model/bone_edit_undo.cpp
    src/model/model_frame_seek.cpp
    src/model/vmd_load.cpp
    src/model/track_apply.cpp
    src/model/key_registrars.cpp
    src/model/morph_apply.cpp
    src/model/model_skinning.cpp
    src/model/pmx_load.cpp
    src/model/add_model.cpp
    src/model/model_query_helpers.cpp
    src/model/model_dispose.cpp
    src/model/bone_sort.cpp
    src/model/post_load_init.cpp
    src/render/fill_panel.cpp
    src/window/wm_paint.cpp
    src/window/ui_timeline_gfx.cpp
    src/window/ui_control_text.cpp
    src/window/ui_ctlcolor.cpp
    src/window/ui_edit_commit.cpp
    src/window/dialog_procs.cpp
    src/window/frame_range_apply.cpp
    src/io/pmm_save.cpp
    src/io/io_device_helpers.cpp
    src/io/vpd_file.cpp
    src/io/vmd_save.cpp
    src/io/pmm_load_dispatch.cpp
    src/io/pmm_load_v2.cpp
    src/io/pmm_load_v1.cpp
    src/window/ui_scroll_mouse.cpp
    src/window/ui_panel_paint.cpp
    src/window/ui_refresh.cpp
    src/window/ui_notify.cpp
    src/window/ui_hscroll.cpp
    src/window/ui_dropfiles.cpp
    src/window/ui_panel_sweep.cpp
      src/window/ui_view_refresh.cpp
      src/window/ui_model_reload.cpp
      src/window/ui_selection_reeval.cpp
    src/window/ui_editor_click.cpp
    src/window/ui_frame_step.cpp
    src/window/ui_frame_refresh.cpp
    src/window/ui_mousemove.cpp
    src/window/ui_windowsize.cpp
    src/window/mic_window.cpp
    src/app/subsystem_init.cpp
    src/app/reset_app_state.cpp
    src/app/scene_ownership.cpp
    src/app/app_utilities.cpp
    src/app/shutdown_cleanup.cpp
    src/app/oni_kinect.cpp
    src/app/oni_skeleton_pump.cpp
    src/io/vsq_load.cpp
    src/window/dialog_select_ops.cpp
    src/window/ui_viewport_refresh.cpp
    src/window/ui_viewport_layout.cpp
    src/window/ui_mouse_misc.cpp
    src/window/ui_palette.cpp
    src/window/command_file_menu.cpp
    src/window/command_view_menu.cpp
    src/window/physics_model_dialog.cpp
    src/window/model_edge_dialog.cpp
    src/window/enhance_model_io.cpp
    src/window/misc_dialogs.cpp
    src/window/command_frame_edit.cpp
    src/window/command_panel_toggles.cpp
    src/window/command_frame_register.cpp
    src/features/dialog_helpers.cpp
    src/features/audio_pose_helpers.cpp
    src/features/subsystem_ledger.cpp
    src/render/draw_glyph.cpp
    src/render/toon_textures.cpp
    src/render/scene_font.cpp
    src/render/mme/mme_bridge.cpp
    src/exports/effect_api.cpp
    src/app/late_ports.cpp
)

target_include_directories(mikudancestudio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 内置 MMEffect 的宿主集成面（mme_bridge → MmeHost* 声明）。
target_include_directories(mikudancestudio_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include)
target_link_libraries(mikudancestudio_core PUBLIC
    BULLET275::BULLET275
    user32      # RegisterClass / CreateWindow / MessageBox
    gdi32       # CreateSolidBrush / GetStockObject
    msimg32     # GradientFill (0x40DF10 panel fill)
    comctl32    # InitCommonControls
    shell32     # DragAcceptFiles
    d3d9        # IDirect3DDevice9 (device slot at sub1d574+120032)
    vfw32       # DrawDibDraw (0x4168D0) + AVIFile*/AVIStream* (0x433250 background AVI)
    avifil32    # AVIFile/AVIStream background playback (0x4168D0)
    ole32       # CoInitialize/CoUninitialize (0x467313 / 0x4096C0)
    strmiids    # DirectShow IIDs/CLSIDs (recording graph 0x408F20..0x409A80)
    uuid        # IID_IPropertyBag / IID_IAMVfwCompressDialogs etc.
)
if (MSVC)
    # Align the PMX skinning stride workers with the original's VCOMP90
    # parallel regions (x64 _vcomp_fork, default static schedule).
    target_compile_options(mikudancestudio_core PRIVATE /openmp)
endif()

# ---------------------------------------------------------------------------
# Porting-era diagnostics.  The MIKUDANCESTUDIO_* environment probes
# (state dumps, A/B traces, canaries) are comparison tooling against the
# original binaries, not product features.  Default OFF: the shipped build
# contains none of that code.  Turn ON only while running A/B sessions:
#   cmake -B build-x64/diag -DMIKUDANCESTUDIO_DIAG=ON ..
# ---------------------------------------------------------------------------
option(MIKUDANCESTUDIO_DIAG
       "Compile the porting-era diagnostics probes (MIKUDANCESTUDIO_* env dumps/traces)" OFF)
if (MIKUDANCESTUDIO_DIAG)
    target_compile_definitions(mikudancestudio_core PUBLIC MIKUDANCESTUDIO_DIAG=1)
endif()

# ---------------------------------------------------------------------------
# Main executable.  WIN32 subsystem matches the original PE header
# (Subsystem 2, GUI).  Exports (Exp*) come from exports/MikuMikuDance.def
# exactly like the original export table at RVA 0x00141328.
# ---------------------------------------------------------------------------
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    # x64 build embeds the original x64 manifest (amd64 dependencies +
    # Data\MMDxShow.dll reg-free COM) - the x86 manifest's x86 assemblies
    # make the 64-bit loader fail with STATUS_INVALID_IMAGE_FORMAT.
    set(MIKUDANCESTUDIO_GEN_RES res/gen_resources_x64.res)
else()
    set(MIKUDANCESTUDIO_GEN_RES res/gen_resources.res)
endif()
add_executable(MikuDanceStudio WIN32
    src/main/winmain.cpp
    res/MMD.rc
    ${MIKUDANCESTUDIO_GEN_RES}
    ${CMAKE_CURRENT_SOURCE_DIR}/exports/MikuMikuDance.def
)

target_link_libraries(MikuDanceStudio PRIVATE
    mikudancestudio_core
    winmm      # timeGetTime - main loop timing
    dsound     # original timeline/audio subsystem initialization (0x4C2470)
    user32
)

# gen_resources.res re-embeds the original .rsrc byte-exact; link.exe accepts
# .res directly as an input file (CMake itself would classify it as <None>).
# /MANIFEST:NO keeps the linker from adding a default manifest on top of the
# original one embedded through the .res.
target_link_options(MikuDanceStudio PRIVATE
    "/MAP:${CMAKE_CURRENT_BINARY_DIR}/MikuMikuDanceE.map"
    "/MANIFEST:NO"
    "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}"
)
set_target_properties(MikuDanceStudio PROPERTIES
    OUTPUT_NAME "MikuMikuDanceE"
    # Export the 37 Exp* symbols from the .def file (ordinals 1..37).
    ENABLE_EXPORTS ON
)

# ---------------------------------------------------------------------------
# MMDxShow.dll - the companion DirectShow push-source filter.  Built for both
# architectures: the x86 build pins the VC8 original's layout
# (../MikuMikuDanceE_v932/Data/MMDxShow.dll), the x64 build pins the VC10
# rebuild (../MikuMikuDanceE_v932x64/Data/MMDxShow.dll) - the per-flavour
# static_asserts live in src/mmdxshow/mmdxshow.hpp.  Both originals export
# the same ordinals 1..5 via the .def (no per-arch def needed).
# ---------------------------------------------------------------------------
add_library(MMDxShow SHARED
    src/mmdxshow/dll_main.cpp
    src/mmdxshow/enumerators.cpp
    src/mmdxshow/source_base.cpp
    src/mmdxshow/push_source.cpp
    src/mmdxshow/push_pin.cpp
    src/mmdxshow/guids.cpp
    src/mmdxshow/crt_ledger.cpp
)
target_include_directories(MMDxShow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/mmdxshow)
target_link_libraries(MMDxShow PRIVATE
    kernel32 advapi32 gdi32 user32 ole32 uuid
)
target_link_options(MMDxShow PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/mmdxshow/MMDxShow.def")
set_target_properties(MMDxShow PROPERTIES
    OUTPUT_NAME "MMDxShow"
)

# ---------------------------------------------------------------------------
# 内置 MMEffect.dll - MikuMikuEffect v0.37 效果引擎的直接内置
# (third_party/mmeffect，逆向重建源码的去 hook 化移植)。
#
# 原版经由 d3d9.dll 代理 + IAT/vtable 拦截注入 MMD；本工程的宿主导出
# (37 个 Exp*)与渲染重复计数循环等价存在，故引擎以 DLL 形式链接进进程，
# 由宿主在管线对应点直接调用 MmeHost* 导出（见 mme_bridge）。
#
# 链接方向：MMEffect.dll 导入宿主 37 个 Exp*。exe 的导入库在 exe 链接时
# 才生成，直接相互链接构成文件级循环，故先用 lib /def 预生成一份同名导
# 入库（/NAME:MikuMikuDanceE.exe 与 exe 实际导出模块名一致，加载器按名
# 对已映射的 exe 解析导入）。
# ---------------------------------------------------------------------------
set(MME_HOST_IMPORT_DIR ${CMAKE_CURRENT_BINARY_DIR}/mme_host_import)
set(MME_HOST_IMPORT_LIB ${MME_HOST_IMPORT_DIR}/MikuMikuDanceE.lib)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MME_HOST_MACHINE x64)
else()
    set(MME_HOST_MACHINE x86)
endif()
add_custom_command(
    OUTPUT ${MME_HOST_IMPORT_LIB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MME_HOST_IMPORT_DIR}
    COMMAND ${CMAKE_AR} /NOLOGO
            /DEF:${CMAKE_CURRENT_SOURCE_DIR}/exports/MikuMikuDance.def
            /NAME:MikuMikuDanceE.exe
            /MACHINE:${MME_HOST_MACHINE}
            /OUT:${MME_HOST_IMPORT_LIB}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports/MikuMikuDance.def
    VERBATIM)
add_custom_target(mme_host_importlib DEPENDS ${MME_HOST_IMPORT_LIB})

add_library(MMEffect SHARED
    third_party/mmeffect/src/d3dx9_dyn.cpp
    third_party/mmeffect/src/mme_host.cpp
    third_party/mmeffect/src/mmhack/mmhack_registry.cpp
    third_party/mmeffect/src/mmhack/mmhack_material.cpp
    third_party/mmeffect/src/mmhack/mmhack_material_table.cpp
    third_party/mmeffect/src/mmhack/mmhack_lightmatrix.cpp
    third_party/mmeffect/src/mmhack/mmhack_getters.cpp
    third_party/mmeffect/src/mmeffect/anime_texture.cpp
    third_party/mmeffect/src/mmeffect/callbacks.cpp
    third_party/mmeffect/src/mmeffect/effect_engine.cpp
    third_party/mmeffect/src/mmeffect/emm_manager.cpp
    third_party/mmeffect/src/mmeffect/ini_file.cpp
    third_party/mmeffect/src/mmeffect/material_bind.cpp
    third_party/mmeffect/src/mmeffect/mme_context.cpp
    third_party/mmeffect/src/mmeffect/mme_dlg.cpp
    third_party/mmeffect/src/mmeffect/mme_globals.cpp
    third_party/mmeffect/src/mmeffect/mme_log.cpp
    third_party/mmeffect/src/mmeffect/mme_ui.cpp
    third_party/mmeffect/src/mmeffect/mme_util.cpp
    third_party/mmeffect/src/mmeffect/model_data.cpp
    third_party/mmeffect/src/mmeffect/pass_planner.cpp
    third_party/mmeffect/src/mmeffect/render_snapshot.cpp
    third_party/mmeffect/src/mmeffect/sas_exec.cpp
    third_party/mmeffect/src/mmeffect/sas_interpreter.cpp
    third_party/mmeffect/res/MMEffect.rc
)
target_include_directories(MMEffect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/src/mmhack   # mmhack_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # d3dx_dyn.hpp（运行时 d3dx9 转发）
)
add_dependencies(MMEffect mme_host_importlib)
target_link_libraries(MMEffect PRIVATE
    ${MME_HOST_IMPORT_LIB}
    user32       # 窗口/菜单/消息
    gdi32        # GDI+ 底层
    gdiplus      # 动画纹理（GIF/APNG 解码）
    comctl32     # 分配对话框列表视图
    comdlg32     # 特效文件选择对话框
    shell32
    ole32
    winmm        # timeGetTime
    shlwapi
    advapi32
)
target_link_options(MMEffect PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/MMEffect.def")
set_target_properties(MMEffect PROPERTIES
    OUTPUT_NAME "MMEffect"
)

# 宿主 exe 静态链接内置 MMEffect.dll（无任何运行时注入）。
target_link_libraries(MikuDanceStudio PRIVATE MMEffect)
