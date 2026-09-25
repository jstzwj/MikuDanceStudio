cmake_minimum_required(VERSION 3.21)

include(cmake/Version.cmake)
project(MikuDanceStudio VERSION "${MDS_VERSION_MAJOR}.${MDS_VERSION_MINOR}" LANGUAGES CXX)
configure_file(cmake/version.hpp.in generated/mikudancestudio/version.hpp @ONLY)
configure_file(res/version.rc.in generated/version.rc @ONLY)

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
    src/math/color_lerp.cpp
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
    src/physics/scene_dispose.cpp
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
target_include_directories(mikudancestudio_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_executable(MikuDanceStudio WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
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
    "/MAP:${CMAKE_CURRENT_BINARY_DIR}/MikuDanceStudio.map"
    "/MANIFEST:NO"
    "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}"
)
set_target_properties(MikuDanceStudio PROPERTIES
    OUTPUT_NAME "MikuDanceStudio"
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

# Effect engine linked directly into the host; no proxy or injection DLL.
include(cmake/D3dxRuntime.cmake)
add_library(MMEffect STATIC
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
)
target_include_directories(MMEffect PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/src/mmhack   # mmhack_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # d3dx_dyn.hpp（运行时 d3dx9 转发）
)
target_compile_definitions(MMEffect PUBLIC MIKUDANCESTUDIO_MME_EMBEDDED=1)
target_link_libraries(MMEffect PUBLIC D3DXRuntime)
target_link_libraries(MMEffect PRIVATE
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
target_link_libraries(MikuDanceStudio PRIVATE MMEffect)

# Resource IDs are distinct from the host. Icon child IDs are explicit too.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MME_ICON_RES ${CMAKE_CURRENT_BINARY_DIR}/mme_icon.res)
add_custom_command(
    OUTPUT ${MME_ICON_RES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_mme_icon.py
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/res/res_mme.ico
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include/mme_resources.h
            ${MME_ICON_RES}
    DEPENDS scripts/gen_mme_icon.py scripts/gen_resources.py
            third_party/mmeffect/res/res_mme.ico
            third_party/mmeffect/include/mme_resources.h
    VERBATIM)
target_sources(MikuDanceStudio PRIVATE
    third_party/mmeffect/res/MMEffect.rc ${MME_ICON_RES})
target_link_options(MikuDanceStudio PRIVATE "${MME_ICON_RES}")

include(CTest)
if(BUILD_TESTING)
    add_executable(charset_conv_test tests/charset_conv_test.cpp src/main/charset_conv.cpp)
    target_include_directories(charset_conv_test PRIVATE include)
    add_test(NAME charset_conv COMMAND charset_conv_test)
    add_executable(input_sequence_test tests/input_sequence_test.cpp)
    target_link_libraries(input_sequence_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME input_sequence COMMAND input_sequence_test)
    add_executable(bone_pick_test tests/bone_pick_test.cpp)
    target_link_libraries(bone_pick_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME bone_pick COMMAND bone_pick_test)
    add_executable(model_key_selection_test tests/model_key_selection_test.cpp)
    target_link_libraries(model_key_selection_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_key_selection COMMAND model_key_selection_test)
    add_executable(morph_navigation_test tests/morph_navigation_test.cpp
        src/app/app_utilities.cpp)
    target_include_directories(morph_navigation_test PRIVATE include)
    target_compile_options(morph_navigation_test PRIVATE /Gy)
    target_link_options(morph_navigation_test PRIVATE /OPT:REF)
    target_link_libraries(morph_navigation_test PRIVATE user32)
    add_test(NAME morph_navigation COMMAND morph_navigation_test)
    add_executable(accessory_drag_test tests/accessory_drag_test.cpp)
    target_link_libraries(accessory_drag_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME accessory_drag COMMAND accessory_drag_test)
    add_executable(camera_position_test tests/camera_position_test.cpp)
    target_link_libraries(camera_position_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME camera_position COMMAND camera_position_test)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(drag_precision_test tests/drag_precision_test.cpp)
        target_link_libraries(drag_precision_test PRIVATE mikudancestudio_core MMEffect dsound)
        add_test(NAME drag_precision COMMAND drag_precision_test)
        if(MSVC)
            add_executable(pmd_skinning_test tests/pmd_skinning_test.cpp)
            target_include_directories(pmd_skinning_test PRIVATE include third_party/mmeffect/include)
            target_compile_options(pmd_skinning_test PRIVATE /openmp /Gy)
            target_link_options(pmd_skinning_test PRIVATE /OPT:REF)
            target_link_libraries(pmd_skinning_test PRIVATE D3DXRuntime)
            add_test(NAME pmd_skinning COMMAND pmd_skinning_test)
        endif()
    endif()
    add_executable(accessory_selection_test tests/accessory_selection_test.cpp
        src/app/app_utilities.cpp src/window/ui_refresh.cpp)
    target_include_directories(accessory_selection_test PRIVATE include)
    target_compile_options(accessory_selection_test PRIVATE /Gy)
    target_link_options(accessory_selection_test PRIVATE /OPT:REF)
    target_link_libraries(accessory_selection_test PRIVATE user32)
    add_test(NAME accessory_selection COMMAND accessory_selection_test)
    add_executable(mme_anime_timeline_test tests/mme_anime_timeline_test.cpp)
    target_include_directories(mme_anime_timeline_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack)
    target_link_libraries(mme_anime_timeline_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_anime_timeline COMMAND mme_anime_timeline_test)
    add_executable(mme_anime_recovery_test tests/mme_anime_recovery_test.cpp)
    target_include_directories(mme_anime_recovery_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack)
    target_link_libraries(mme_anime_recovery_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_anime_recovery COMMAND mme_anime_recovery_test)
    add_executable(mme_posteffect_message_test tests/mme_posteffect_message_test.cpp)
    target_include_directories(mme_posteffect_message_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack)
    target_link_libraries(mme_posteffect_message_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_posteffect_message COMMAND mme_posteffect_message_test)
    add_executable(mme_shader_message_test tests/mme_shader_message_test.cpp)
    target_include_directories(mme_shader_message_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack)
    target_link_libraries(mme_shader_message_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_shader_message_gpu COMMAND mme_shader_message_test)
    set_tests_properties(mme_shader_message_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(mme_log_test tests/mme_log_test.cpp)
    target_include_directories(mme_log_test PRIVATE third_party/mmeffect/include)
    target_link_libraries(mme_log_test PRIVATE user32)
    add_test(NAME mme_log_absent COMMAND mme_log_test)
    add_test(NAME mme_log_sentinel COMMAND mme_log_test sentinel)
    configure_file(tests/axis_mesh.rc.in ${CMAKE_CURRENT_BINARY_DIR}/axis_mesh_test.rc @ONLY)
    add_executable(axis_mesh_test tests/axis_mesh_test.cpp ${CMAKE_CURRENT_BINARY_DIR}/axis_mesh_test.rc)
    target_link_libraries(axis_mesh_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME axis_mesh_gpu COMMAND axis_mesh_test)
    set_tests_properties(axis_mesh_gpu PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(model_initialization_test tests/model_initialization_test.cpp
        src/model/model_init.cpp)
    add_executable(model_lifetime_test tests/model_lifetime_test.cpp
        src/model/model_init.cpp src/model/model_dispose.cpp)
    target_link_libraries(model_lifetime_test PRIVATE BULLET275::BULLET275)
    add_executable(skeleton_tracking_test tests/skeleton_tracking_test.cpp)
    add_executable(ui_protocol_test tests/ui_protocol_test.cpp)
    add_executable(bone_binding_test tests/bone_binding_test.cpp src/model/bone_sort.cpp)
    add_executable(dialog_order_arrays_test tests/dialog_order_arrays_test.cpp)
    add_executable(undo_frame_test tests/undo_frame_test.cpp
        src/model/model_keyframe_edit.cpp)
    target_compile_options(undo_frame_test PRIVATE /Gy)
    target_link_options(undo_frame_test PRIVATE /OPT:REF)
    target_link_libraries(undo_frame_test PRIVATE user32)
    add_executable(physics_scene_dispose_test tests/physics_scene_dispose_test.cpp
        src/physics/scene_dispose.cpp)
    target_compile_definitions(physics_scene_dispose_test PRIVATE MIKUDANCESTUDIO_DIAG=1)
    target_link_libraries(physics_scene_dispose_test PRIVATE BULLET275::BULLET275)
    add_executable(mme_regression tests/mme_regression.cpp)
    add_executable(mme_initialization_test tests/mme_initialization_test.cpp)
    target_include_directories(mme_initialization_test PRIVATE third_party/mmeffect/include)
    add_executable(mme_host_object_cache_test tests/mme_host_object_cache_test.cpp)
    target_include_directories(mme_host_object_cache_test PRIVATE third_party/mmeffect/include)
    add_test(NAME mme_host_object_cache COMMAND mme_host_object_cache_test)
    add_executable(mme_effect_mapping_test tests/mme_effect_mapping_test.cpp)
    target_include_directories(mme_effect_mapping_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_effect_mapping_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_effect_mapping COMMAND mme_effect_mapping_test)
    add_executable(background_hotkeys_test tests/background_hotkeys_test.cpp)
    target_link_libraries(background_hotkeys_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME background_hotkeys COMMAND background_hotkeys_test)
    add_executable(mme_buffer_pass_test tests/mme_buffer_pass_test.cpp)
    target_include_directories(mme_buffer_pass_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_buffer_pass_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_buffer_pass_gpu COMMAND mme_buffer_pass_test)
    set_tests_properties(mme_buffer_pass_gpu PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(mme_ray_smoke_test tests/mme_ray_smoke_test.cpp)
    target_include_directories(mme_ray_smoke_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_ray_smoke_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_executable(mme_offscreen_turn_test tests/mme_offscreen_turn_test.cpp)
    add_executable(mme_assignment_graph_test tests/mme_assignment_graph_test.cpp)
    target_include_directories(mme_assignment_graph_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_assignment_graph_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_assignment_graph_gpu COMMAND mme_assignment_graph_test)
    set_tests_properties(mme_assignment_graph_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(mme_snapshot_cache_test tests/mme_snapshot_cache_test.cpp)
    target_include_directories(mme_snapshot_cache_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack)
    target_link_libraries(mme_snapshot_cache_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_snapshot_cache_gpu COMMAND mme_snapshot_cache_test)
    set_tests_properties(mme_snapshot_cache_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    target_include_directories(mme_offscreen_turn_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_offscreen_turn_test PRIVATE mikudancestudio_core MMEffect dsound)
    set(MIKUDANCESTUDIO_RAY_TEST_EFFECT "" CACHE FILEPATH
        "Optional local ray.fx for real-asset GPU regression tests")
    if(MIKUDANCESTUDIO_RAY_TEST_EFFECT)
        add_test(NAME mme_ray_script_gpu COMMAND mme_ray_smoke_test "${MIKUDANCESTUDIO_RAY_TEST_EFFECT}")
        add_test(NAME mme_ray_offscreen_gpu COMMAND mme_offscreen_turn_test "${MIKUDANCESTUDIO_RAY_TEST_EFFECT}")
        set_tests_properties(mme_ray_script_gpu mme_ray_offscreen_gpu PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 90)
    endif()
    add_executable(mesh_fallback_test tests/mesh_fallback_test.cpp)
    target_include_directories(mesh_fallback_test PRIVATE third_party/mmeffect/include)
    target_include_directories(mme_regression PRIVATE third_party/mmeffect/include)
    target_link_libraries(mme_regression PRIVATE D3DXRuntime d3d9 user32)
    foreach(test IN ITEMS model_initialization_test model_lifetime_test
            skeleton_tracking_test ui_protocol_test bone_binding_test dialog_order_arrays_test
            undo_frame_test physics_scene_dispose_test
            mme_regression mme_initialization_test mesh_fallback_test)
        target_include_directories(${test} PRIVATE include)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    # Requires the Microsoft D3DX9 runtime installed for the target architecture.
    add_test(NAME mme_d3dx_runtime COMMAND mme_regression --runtime)
    add_executable(d3dx_import_probe tests/d3dx_import_probe.cpp)
    target_include_directories(d3dx_import_probe PRIVATE include)
    target_link_libraries(d3dx_import_probe PRIVATE D3DXRuntime)
    add_library(incomplete_d3dx_runtime SHARED tests/incomplete_d3dx_runtime.cpp)
    set_target_properties(incomplete_d3dx_runtime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/d3dx"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/d3dx")
    add_test(NAME d3dx_direct_imports COMMAND d3dx_import_probe)
    add_test(NAME d3dx_pe_imports COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_d3dx_imports.py
        $<TARGET_FILE:MikuDanceStudio>)
    add_test(NAME d3dx_loader_failure COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_d3dx_loader_failure.py
        $<TARGET_FILE:d3dx_import_probe> $<TARGET_FILE:incomplete_d3dx_runtime>
        ${D3DX_RUNTIME_NAME})
    add_executable(embedded_mme_resources_test tests/embedded_mme_resources_test.cpp)
    target_include_directories(embedded_mme_resources_test PRIVATE third_party/mmeffect/include)
    add_dependencies(embedded_mme_resources_test MikuDanceStudio)
    add_test(NAME embedded_mme_resources
        COMMAND embedded_mme_resources_test $<TARGET_FILE:MikuDanceStudio>)
endif()
