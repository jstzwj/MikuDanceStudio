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
option(MIKUDANCESTUDIO_FORMATS_ONLY "Build only the portable format library and tests" OFF)
option(MIKUDANCESTUDIO_SELF_SHADOW_STRICT_DIAGNOSTICS "Register exact DX9 self-shadow image diagnostics" OFF)

if (MSVC)
    add_compile_options(/W3 /permissive-)
    # Make source and literal encoding independent of the system code page.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # The original binary tolerates implicit narrowing everywhere; keep the
    # port close to VC9 behaviour rather than modern strictness.
    add_compile_options(/wd4267 /wd4244 /wd4245)
    # Match VC9's plain new/delete convention on both sides of the Bullet
    # boundary. The Bullet recipe must use this switch too: on x86, C++17
    # otherwise pairs a plain constraint allocation with an aligned deleting
    # destructor (or the reverse with a C++14 library), corrupting the heap.
    add_compile_options(/Zc:alignedNew-)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT MIKUDANCESTUDIO_FORMATS_ONLY)
    message(STATUS
        "Configuring the x64 build (reference: MikuMikuDanceE_v932x64, "
        "VC9 SSE2, d3dx9_43).  Fields are admitted only through the x64 "
        "evidence and parity contract in docs/X64_RECONSTRUCTION.md.")
endif()

# Portable file formats can be built without Conan, Bullet, Win32, or D3D9.
add_library(mikudancestudio_formats STATIC
    src/io/vpd_format.cpp
    src/io/vmd_format.cpp)
target_sources(mikudancestudio_formats PRIVATE src/io/model_format_header.cpp)
target_sources(mikudancestudio_formats PRIVATE src/io/model_geometry.cpp)
target_sources(mikudancestudio_formats PRIVATE src/io/model_sections.cpp)
target_sources(mikudancestudio_formats PRIVATE src/io/model_reader.cpp)
target_sources(mikudancestudio_formats PRIVATE src/io/pmm_format.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_document.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_scene_document.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_scene_writer.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_scene_evaluation.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_scene_playback.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/pmm_scene_world.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/scene_session.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/io/scene_resources.cpp)
  target_sources(mikudancestudio_formats PRIVATE src/render/render_snapshot.cpp)
target_include_directories(mikudancestudio_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(mikudancestudio_rhi STATIC
    src/render/rhi_null.cpp
    src/render/rhi_render_pass.cpp
    src/render/rhi_frame_passes.cpp
    src/render/rhi_image.cpp
    src/render/rhi_assets.cpp
    src/render/rhi_material_textures.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_shader_inputs.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_model_replay.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_scene_renderer.cpp src/render/rhi_backend.cpp src/render/rhi_scene_preview.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/model_geometry.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/model_inputs.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/self_shadow.cpp)
target_sources(mikudancestudio_rhi PRIVATE src/render/scene_passes.cpp src/render/scene_frame.cpp src/render/rhi_texture_mips.cpp)
target_include_directories(mikudancestudio_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
option(MIKUDANCESTUDIO_RHI_D3D9 "Build the opt-in D3D9 RHI adapter" ON)
if(WIN32)
    # CPU transport conversion is also used by the native application's recorder
    # when the optional DX9 RHI adapter itself is disabled.
    target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_d3d9_replay_conversion.cpp)
    target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_d3d9_mip_capture.cpp)
    target_link_libraries(mikudancestudio_rhi PUBLIC d3dcompiler)
endif()
if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D9)
    target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_d3d9.cpp
        src/render/rhi_d3d9_texture_preparation.cpp
        src/render/rhi_d3d9_swapchain.cpp)
    target_link_libraries(mikudancestudio_rhi PUBLIC d3d9)
    target_compile_definitions(mikudancestudio_rhi PUBLIC
        MIKUDANCESTUDIO_RHI_D3D9=1)
endif()
option(MIKUDANCESTUDIO_RHI_D3D11 "Build the isolated minimal DX11 lifecycle adapter" ON)
option(MIKUDANCESTUDIO_RHI_D3D12 "Build the optional DX12 lifecycle and model adapter" ON)
option(MIKUDANCESTUDIO_RHI_VULKAN "Build the optional Vulkan 1.3 offscreen adapter (requires SDK and DXC)" OFF)
if(MIKUDANCESTUDIO_RHI_VULKAN)
    include(cmake/VulkanShaders.cmake)
endif()
if(WIN32 AND (MIKUDANCESTUDIO_RHI_D3D11 OR MIKUDANCESTUDIO_RHI_D3D12))
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/model_material_vertex.hlsl" MDS_MODEL_VS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/model_material_pixel.hlsl" MDS_MODEL_PS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/model_dx9_compatibility.hlsl" MDS_MODEL_DX9_COMPATIBILITY)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/fixed_screen.hlsl" MDS_FIXED_SCREEN)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/builtin_shadow.hlsl" MDS_BUILTIN_SHADOW)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/post_process.hlsl" MDS_POST_PROCESS)
    configure_file(cmake/rhi_model_shaders.hpp.in generated/rhi_model_shaders.hpp @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        src/render/shaders/model_material_vertex.hlsl src/render/shaders/model_material_pixel.hlsl
        src/render/shaders/model_dx9_compatibility.hlsl src/render/shaders/fixed_screen.hlsl
        src/render/shaders/builtin_shadow.hlsl src/render/shaders/post_process.hlsl)
    target_include_directories(mikudancestudio_rhi PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()
if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D11)
    target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_d3d11.cpp)
    target_link_libraries(mikudancestudio_rhi PUBLIC d3d11 dxgi d3dcompiler)
    target_compile_definitions(mikudancestudio_rhi PUBLIC MIKUDANCESTUDIO_RHI_D3D11=1)
endif()
if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D12)
    target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_d3d12.cpp)
    target_link_libraries(mikudancestudio_rhi PUBLIC d3d12 dxgi d3dcompiler)
    target_compile_definitions(mikudancestudio_rhi PUBLIC MIKUDANCESTUDIO_RHI_D3D12=1)
endif()
add_executable(mds_scene_cli src/tools/scene_cli.cpp)
target_link_libraries(mds_scene_cli PRIVATE mikudancestudio_formats)

include(CTest)
if(BUILD_TESTING)
    if(MIKUDANCESTUDIO_RHI_VULKAN)
        add_executable(rhi_vulkan_test tests/rhi_vulkan_test.cpp)
        target_link_libraries(rhi_vulkan_test PRIVATE mikudancestudio_rhi)
        add_test(NAME rhi_vulkan_resources COMMAND rhi_vulkan_test)
        add_test(NAME rhi_vulkan_validation COMMAND rhi_vulkan_test validation)
        set_tests_properties(rhi_vulkan_resources rhi_vulkan_validation PROPERTIES TIMEOUT 90 LABELS "offscreen;vulkan")
        set_tests_properties(rhi_vulkan_validation PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(self_shadow_compare_test tests/self_shadow_compare_test.cpp)
    target_link_libraries(self_shadow_compare_test PRIVATE mikudancestudio_rhi)
    add_test(NAME self_shadow_compare COMMAND self_shadow_compare_test)
    add_executable(rhi_render_pass_test tests/rhi_render_pass_test.cpp)
    target_link_libraries(rhi_render_pass_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_render_pass COMMAND rhi_render_pass_test)
    add_executable(rhi_frame_passes_test tests/rhi_frame_passes_test.cpp)
    target_link_libraries(rhi_frame_passes_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_frame_passes COMMAND rhi_frame_passes_test)
    add_executable(rhi_scene_frame_test tests/rhi_scene_frame_test.cpp)
    target_link_libraries(rhi_scene_frame_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_scene_frame COMMAND rhi_scene_frame_test)
    add_executable(rhi_scene_passes_test tests/rhi_scene_passes_test.cpp)
    target_link_libraries(rhi_scene_passes_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_scene_passes COMMAND rhi_scene_passes_test)
    add_executable(rhi_scene_renderer_test tests/rhi_scene_renderer_test.cpp)
    target_link_libraries(rhi_scene_renderer_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_scene_renderer COMMAND rhi_scene_renderer_test)
    add_executable(rhi_dynamic_buffer_test tests/rhi_dynamic_buffer_test.cpp)
    target_link_libraries(rhi_dynamic_buffer_test PRIVATE mikudancestudio_rhi)
    set(MDS_DYNAMIC_BACKENDS)
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D11)
        list(APPEND MDS_DYNAMIC_BACKENDS dx11 dx11-warp)
    endif()
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D12)
        list(APPEND MDS_DYNAMIC_BACKENDS dx12 dx12-warp dx12-debug dx12-warp-debug)
    endif()
    if(MIKUDANCESTUDIO_RHI_VULKAN)
        list(APPEND MDS_DYNAMIC_BACKENDS vulkan vulkan-debug)
    endif()
    foreach(api IN LISTS MDS_DYNAMIC_BACKENDS)
        add_test(NAME rhi_dynamic_buffer_${api} COMMAND rhi_dynamic_buffer_test ${api})
        set_tests_properties(rhi_dynamic_buffer_${api} PROPERTIES TIMEOUT 120
            SKIP_RETURN_CODE 77 RUN_SERIAL TRUE LABELS "offscreen;dynamic-buffer")
    endforeach()
    add_executable(rhi_model_geometry_test tests/rhi_model_geometry_test.cpp)
    target_link_libraries(rhi_model_geometry_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_model_geometry COMMAND rhi_model_geometry_test)
    add_executable(rhi_model_inputs_test tests/rhi_model_inputs_test.cpp)
    target_link_libraries(rhi_model_inputs_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_model_inputs COMMAND rhi_model_inputs_test)
    if(MIKUDANCESTUDIO_RHI_VULKAN OR (WIN32 AND MIKUDANCESTUDIO_RHI_D3D11 AND MIKUDANCESTUDIO_RHI_D3D12))
        add_executable(rhi_post_process_test tests/rhi_post_process_test.cpp)
        target_link_libraries(rhi_post_process_test PRIVATE mikudancestudio_rhi)
        if(WIN32)
            add_executable(rhi_screen_capture_test tests/rhi_screen_capture_test.cpp)
            target_link_libraries(rhi_screen_capture_test PRIVATE mikudancestudio_rhi d3d9 user32)
        endif()
        set(MDS_POST_BACKENDS)
        if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D11 AND MIKUDANCESTUDIO_RHI_D3D12)
            list(APPEND MDS_POST_BACKENDS dx11 dx12 dx12-debug dx11-measured dx12-measured)
        endif()
        if(MIKUDANCESTUDIO_RHI_VULKAN)
            list(APPEND MDS_POST_BACKENDS vulkan vulkan-debug)
        endif()
        foreach(api IN LISTS MDS_POST_BACKENDS)
            if(WIN32)
                add_test(NAME rhi_screen_capture_${api} COMMAND rhi_screen_capture_test ${api})
                set_tests_properties(rhi_screen_capture_${api} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 LABELS "offscreen;screen-texture")
            endif()
            add_test(NAME rhi_post_process_${api} COMMAND rhi_post_process_test ${api})
            set_tests_properties(rhi_post_process_${api} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 LABELS "offscreen;postprocess")
        endforeach()
        add_executable(rhi_multipass_gpu_test tests/rhi_multipass_gpu_test.cpp)
        target_link_libraries(rhi_multipass_gpu_test PRIVATE mikudancestudio_rhi)
    endif()
    if(MIKUDANCESTUDIO_RHI_VULKAN)
        add_test(NAME rhi_multipass_vulkan_hardware COMMAND rhi_multipass_gpu_test vulkan-hardware)
        add_test(NAME rhi_multipass_vulkan_validation COMMAND rhi_multipass_gpu_test vulkan-hardware-debug)
        set_tests_properties(rhi_multipass_vulkan_hardware rhi_multipass_vulkan_validation PROPERTIES TIMEOUT 90 LABELS "offscreen;vulkan")
        set_tests_properties(rhi_multipass_vulkan_validation PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D11 AND MIKUDANCESTUDIO_RHI_D3D12)
        if(MIKUDANCESTUDIO_RHI_VULKAN)
            add_executable(rhi_hidden_presentation_test tests/rhi_hidden_presentation_test.cpp)
            target_link_libraries(rhi_hidden_presentation_test PRIVATE mikudancestudio_rhi user32)
            foreach(api dx11 dx12 vulkan)
                add_test(NAME rhi_hidden_presentation_${api} COMMAND rhi_hidden_presentation_test ${api})
                set_tests_properties(rhi_hidden_presentation_${api} PROPERTIES TIMEOUT 120 LABELS "hidden;presentation")
            endforeach()
            add_test(NAME rhi_hidden_presentation_vulkan_validation COMMAND rhi_hidden_presentation_test vulkan-debug)
            set_tests_properties(rhi_hidden_presentation_vulkan_validation PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 LABELS "hidden;presentation;vulkan")
            add_executable(rhi_material_sampling_test tests/rhi_material_sampling_test.cpp)
            target_link_libraries(rhi_material_sampling_test PRIVATE mikudancestudio_rhi)
            add_test(NAME rhi_material_sampling COMMAND rhi_material_sampling_test)
            set_tests_properties(rhi_material_sampling PROPERTIES TIMEOUT 120 LABELS "offscreen;vulkan;bounded")
        endif()
        foreach(api IN ITEMS dx11 dx12)
            foreach(driver IN ITEMS warp hardware)
                add_test(NAME rhi_multipass_${api}_${driver} COMMAND rhi_multipass_gpu_test ${api}-${driver})
                set_tests_properties(rhi_multipass_${api}_${driver} PROPERTIES TIMEOUT 90 LABELS "offscreen")
            endforeach()
        endforeach()
        add_test(NAME rhi_multipass_dx12_debug_warp COMMAND rhi_multipass_gpu_test dx12-warp-debug)
        set_tests_properties(rhi_multipass_dx12_debug_warp PROPERTIES TIMEOUT 90 LABELS "offscreen")
        add_executable(rhi_scene_preview_test tests/rhi_scene_preview_test.cpp)
        target_link_libraries(rhi_scene_preview_test PRIVATE mikudancestudio_rhi user32)
        foreach(api IN ITEMS dx11 dx12)
            foreach(driver IN ITEMS warp hardware)
                add_test(NAME rhi_preview_${api}_${driver} COMMAND rhi_scene_preview_test ${api}-${driver})
                set_tests_properties(rhi_preview_${api}_${driver} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS "hidden;presentation")
            endforeach()
        endforeach()
        add_executable(rhi_backend_scene_test tests/rhi_backend_scene_test.cpp)
        target_link_libraries(rhi_backend_scene_test PRIVATE mikudancestudio_rhi user32)
        foreach(api IN ITEMS dx11 dx12)
            foreach(driver IN ITEMS warp hardware)
                add_test(NAME rhi_resident_${api}_${driver} COMMAND rhi_backend_scene_test --${api}-${driver})
                set_tests_properties(rhi_resident_${api}_${driver} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            endforeach()
        endforeach()
        foreach(driver IN ITEMS warp hardware)
            add_test(NAME rhi_resident_dx12_debug_${driver} COMMAND rhi_backend_scene_test --dx12-${driver}-debug)
            set_tests_properties(rhi_resident_dx12_debug_${driver} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endforeach()
        foreach(api IN ITEMS dx11 dx12)
            add_test(NAME rhi_specular_policy_${api}_warp COMMAND rhi_backend_scene_test --${api}-warp-debug-specular)
            set_tests_properties(rhi_specular_policy_${api}_warp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endforeach()
    endif()
    add_executable(explicit_clip_geometry_test tests/explicit_clip_geometry_test.cpp)
    add_test(NAME explicit_clip_geometry COMMAND explicit_clip_geometry_test)
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D11)
        add_executable(rhi_d3d11_lifecycle_test tests/rhi_d3d11_lifecycle_test.cpp)
        target_link_libraries(rhi_d3d11_lifecycle_test PRIVATE mikudancestudio_rhi user32)
        add_executable(rhi_d3d11_multimaterial_test tests/rhi_d3d11_multimaterial_test.cpp)
        target_link_libraries(rhi_d3d11_multimaterial_test PRIVATE mikudancestudio_rhi user32)
        add_executable(rhi_d3d11_viewport_test tests/rhi_d3d11_viewport_test.cpp)
        target_link_libraries(rhi_d3d11_viewport_test PRIVATE mikudancestudio_rhi user32)
        foreach(driver IN ITEMS warp hardware)
            add_test(NAME rhi_d3d11_compatibility_lifecycle_${driver} COMMAND rhi_d3d11_lifecycle_test
                --compatibility-${driver} ${CMAKE_CURRENT_BINARY_DIR}/dx11-compatibility-lifecycle-evidence)
            set_tests_properties(rhi_d3d11_compatibility_lifecycle_${driver} PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_d3d11_viewport_${driver} COMMAND rhi_d3d11_viewport_test --${driver})
            set_tests_properties(rhi_d3d11_viewport_${driver} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_d3d11_multimaterial_${driver} COMMAND rhi_d3d11_multimaterial_test
                --${driver} ${CMAKE_CURRENT_BINARY_DIR}/dx11-multimaterial-evidence)
            set_tests_properties(rhi_d3d11_multimaterial_${driver} PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_d3d11_lifecycle_${driver} COMMAND rhi_d3d11_lifecycle_test
                --${driver} ${CMAKE_CURRENT_BINARY_DIR}/dx11-lifecycle-evidence)
            set_tests_properties(rhi_d3d11_lifecycle_${driver} PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endforeach()
    endif()
    if(WIN32)
        add_executable(rhi_d3d11_shader_inputs_test tests/rhi_d3d11_shader_inputs_test.cpp)
        target_link_libraries(rhi_d3d11_shader_inputs_test PRIVATE mikudancestudio_rhi d3d11 d3dcompiler)
        add_test(NAME rhi_d3d11_shader_inputs COMMAND rhi_d3d11_shader_inputs_test)
        set_tests_properties(rhi_d3d11_shader_inputs PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D9)
        add_executable(rhi_d3d9_replay_conversion_test tests/rhi_d3d9_replay_conversion_test.cpp)
        target_link_libraries(rhi_d3d9_replay_conversion_test PRIVATE mikudancestudio_rhi)
        add_test(NAME rhi_d3d9_replay_conversion COMMAND rhi_d3d9_replay_conversion_test)
        add_test(NAME rhi_d3d9_null_texture_native COMMAND rhi_d3d9_replay_conversion_test --native-null)
        set_tests_properties(rhi_d3d9_null_texture_native PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_mip_capture_native COMMAND rhi_d3d9_replay_conversion_test --native-mips)
        set_tests_properties(rhi_d3d9_mip_capture_native PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        if(MIKUDANCESTUDIO_RHI_D3D11)
            target_include_directories(rhi_d3d9_replay_conversion_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
            target_sources(rhi_d3d9_replay_conversion_test PRIVATE tests/rhi_d3d11_w_candidate_backend.cpp)
            add_test(NAME rhi_shared_clip_depth_hardware COMMAND rhi_d3d9_replay_conversion_test --shared-clip-smoke)
            set_tests_properties(rhi_shared_clip_depth_hardware PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_explicit_z_shader_hardware COMMAND rhi_d3d9_replay_conversion_test --z-explicit-smoke)
            set_tests_properties(rhi_explicit_z_shader_hardware PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_shared_color_hardware COMMAND rhi_d3d9_replay_conversion_test --shared-color-smoke)
            set_tests_properties(rhi_shared_color_hardware PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_lighting_order_hardware COMMAND rhi_d3d9_replay_conversion_test --lighting-order-smoke)
            add_test(NAME rhi_final_modulate_hardware COMMAND rhi_d3d9_replay_conversion_test --final-modulate-smoke ${CMAKE_CURRENT_BINARY_DIR}/final-modulate-evidence)
            set_tests_properties(rhi_final_modulate_hardware PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            set_tests_properties(rhi_lighting_order_hardware PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_normal_precision_hardware COMMAND rhi_d3d9_replay_conversion_test --normal-precision-smoke ${CMAKE_CURRENT_BINARY_DIR}/normal-precision-smoke)
            set_tests_properties(rhi_normal_precision_hardware PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_normal_transforms_hardware COMMAND rhi_d3d9_replay_conversion_test --normal-transforms ${CMAKE_CURRENT_BINARY_DIR}/normal-transform-evidence)
            add_test(NAME rhi_texture_fma_scope_hardware COMMAND rhi_d3d9_replay_conversion_test --texture-fma-scope ${CMAKE_CURRENT_BINARY_DIR}/texture-fma-evidence)
            set_tests_properties(rhi_texture_fma_scope_hardware PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_specular_scope_hardware COMMAND rhi_d3d9_replay_conversion_test --model-specular-scope ${CMAKE_CURRENT_BINARY_DIR}/specular-scope-evidence)
            set_tests_properties(rhi_specular_scope_hardware PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            set_tests_properties(rhi_normal_transforms_hardware PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            # Validates repeatability, CPU/GPU arithmetic and shared-clip
            # controls. Native-equivalence qualification is reported separately.
            add_test(NAME rhi_independent_xy_controls COMMAND rhi_d3d9_replay_conversion_test
                --xy-independent-controls ${CMAKE_CURRENT_BINARY_DIR}/independent-xy-evidence)
            set_tests_properties(rhi_independent_xy_controls PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_d3d11_compatibility_generated COMMAND rhi_d3d9_replay_conversion_test
                --dx11-compatibility-generated ${CMAKE_CURRENT_BINARY_DIR}/dx11-compatibility-generated)
            set_tests_properties(rhi_d3d11_compatibility_generated PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            foreach(driver IN ITEMS warp hardware)
                add_test(NAME rhi_d3d9_ground_${driver} COMMAND rhi_d3d9_replay_conversion_test --ground-${driver})
                set_tests_properties(rhi_d3d9_ground_${driver} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
                add_test(NAME rhi_d3d9_replay_conversion_${driver} COMMAND rhi_d3d9_replay_conversion_test --${driver})
                set_tests_properties(rhi_d3d9_replay_conversion_${driver} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
                add_test(NAME rhi_d3d9_null_texture_${driver} COMMAND rhi_d3d9_replay_conversion_test --null-${driver})
                set_tests_properties(rhi_d3d9_null_texture_${driver} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
                add_test(NAME rhi_d3d9_mip_capture_${driver} COMMAND rhi_d3d9_replay_conversion_test --mips-${driver})
                set_tests_properties(rhi_d3d9_mip_capture_${driver} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
                add_test(NAME rhi_d3d9_shadow_${driver} COMMAND rhi_d3d9_replay_conversion_test --shadow-${driver})
                set_tests_properties(rhi_d3d9_shadow_${driver} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
                add_test(NAME rhi_d3d11_shadow_oracle_${driver} COMMAND rhi_d3d9_replay_conversion_test --shadow-adapter-${driver})
                set_tests_properties(rhi_d3d11_shadow_oracle_${driver} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            endforeach()
        endif()
        add_executable(rhi_d3d11_material_pixel_test tests/rhi_d3d11_material_pixel_test.cpp)
        target_link_libraries(rhi_d3d11_material_pixel_test PRIVATE mikudancestudio_rhi d3d11 d3dcompiler user32)
        target_compile_definitions(rhi_d3d11_material_pixel_test PRIVATE MDS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
        add_test(NAME rhi_d3d11_material_pixel COMMAND rhi_d3d11_material_pixel_test)
        set_tests_properties(rhi_d3d11_material_pixel PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME rhi_d3d11_material_pixel_hardware COMMAND rhi_d3d11_material_pixel_test --hardware --linear)
        add_test(NAME rhi_d3d11_material_pixel_constant COMMAND rhi_d3d11_material_pixel_test --precision constant)
        add_test(NAME rhi_d3d11_material_pixel_translucent COMMAND rhi_d3d11_material_pixel_test --precision translucent)
        set_tests_properties(rhi_d3d11_material_pixel_hardware rhi_d3d11_material_pixel_constant
            rhi_d3d11_material_pixel_translucent PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(rhi_d3d11_real_batch_test tests/rhi_d3d11_real_batch_test.cpp)
        if(MIKUDANCESTUDIO_RHI_D3D11)
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            add_test(NAME rhi_d3d11_model_adapter COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_rhi_model_adapter.py
                --executable $<TARGET_FILE:rhi_d3d11_real_batch_test>
                --output ${CMAKE_CURRENT_BINARY_DIR}/rhi-model-adapter-evidence)
            set_tests_properties(rhi_d3d11_model_adapter PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 210 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endif()
        target_link_libraries(rhi_d3d11_real_batch_test PRIVATE mikudancestudio_rhi d3d11 d3dcompiler user32)
        target_compile_definitions(rhi_d3d11_real_batch_test PRIVATE MDS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
        add_executable(rhi_d3d11_model_vertex_test tests/rhi_d3d11_model_vertex_test.cpp)
        target_link_libraries(rhi_d3d11_model_vertex_test PRIVATE mikudancestudio_rhi d3d11 d3dcompiler)
        target_compile_definitions(rhi_d3d11_model_vertex_test PRIVATE MDS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
        add_test(NAME rhi_d3d11_model_vertex COMMAND rhi_d3d11_model_vertex_test)
        set_tests_properties(rhi_d3d11_model_vertex PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME rhi_d3d11_model_vertex_byte COMMAND rhi_d3d11_model_vertex_test --vertex-byte)
        set_tests_properties(rhi_d3d11_model_vertex_byte PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(rhi_d3d9_msaa_history_test tests/rhi_d3d9_msaa_history_test.cpp)
        target_link_libraries(rhi_d3d9_msaa_history_test PRIVATE mikudancestudio_rhi d3d9 d3dcompiler user32)
        add_test(NAME rhi_d3d9_msaa_history_capability COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/msaa-history-capabilities)
        add_test(NAME rhi_d3d9_geometry_planes COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/geometry-planes --geometry-planes)
        set_tests_properties(rhi_d3d9_geometry_planes PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_triangle_orders COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/triangle-orders --triangle-orders)
        set_tests_properties(rhi_d3d9_triangle_orders PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_float_sweep COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/float-sweep --float-sweep)
        set_tests_properties(rhi_d3d9_float_sweep PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_float_interpolation COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/float-interpolation --float-interpolation)
        set_tests_properties(rhi_d3d9_float_interpolation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_edge_quantization COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/edge-quantization --edge-quantization)
        set_tests_properties(rhi_d3d9_edge_quantization PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_prelit_interpolation COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/prelit-interpolation --prelit-interpolation)
        set_tests_properties(rhi_d3d9_prelit_interpolation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        foreach(shape IN ITEMS triangle pair)
            add_test(NAME rhi_d3d9_lighting_${shape} COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/lighting-${shape} --lighting-${shape})
            set_tests_properties(rhi_d3d9_lighting_${shape} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endforeach()
        add_test(NAME rhi_d3d9_fixed_lighting COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/fixed-lighting-reproduction --fixed-lighting)
        set_tests_properties(rhi_d3d9_fixed_lighting PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_attachment_history COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/msaa-attachment-history --attachment-history)
        set_tests_properties(rhi_d3d9_attachment_history PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_d3d9_sample_coverage COMMAND rhi_d3d9_msaa_history_test ${CMAKE_CURRENT_BINARY_DIR}/msaa-sample-coverage --sample-coverage)
        set_tests_properties(rhi_d3d9_sample_coverage PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        set_tests_properties(rhi_d3d9_msaa_history_capability PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(rhi_d3d9_swapchain_test tests/rhi_d3d9_swapchain_test.cpp)
        target_link_libraries(rhi_d3d9_swapchain_test PRIVATE mikudancestudio_rhi user32)
        add_test(NAME rhi_d3d9_swapchain COMMAND rhi_d3d9_swapchain_test)
        set_tests_properties(rhi_d3d9_swapchain PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(rhi_d3d9_test tests/rhi_d3d9_test.cpp)
        target_link_libraries(rhi_d3d9_test PRIVATE mikudancestudio_rhi user32)
        add_test(NAME rhi_d3d9 COMMAND rhi_d3d9_test)
        set_tests_properties(rhi_d3d9 PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(rhi_d3d9_parity_test tests/rhi_d3d9_parity_test.cpp)
        target_link_libraries(rhi_d3d9_parity_test PRIVATE mikudancestudio_rhi user32)
        add_test(NAME rhi_d3d9_parity COMMAND rhi_d3d9_parity_test)
        set_tests_properties(rhi_d3d9_parity PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(rhi_null_test tests/rhi_null_test.cpp)
    target_link_libraries(rhi_null_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_null COMMAND rhi_null_test)
    add_executable(rhi_image_test tests/rhi_image_test.cpp)
    target_link_libraries(rhi_image_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_image COMMAND rhi_image_test
        "${CMAKE_CURRENT_SOURCE_DIR}/res/assets/hud_sprites.png")
    add_executable(rhi_assets_test tests/rhi_assets_test.cpp)
    target_link_libraries(rhi_assets_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_assets COMMAND rhi_assets_test
        "${CMAKE_CURRENT_SOURCE_DIR}/res/assets")
    add_executable(rhi_material_textures_test tests/rhi_material_textures_test.cpp)
    target_link_libraries(rhi_material_textures_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_material_textures COMMAND rhi_material_textures_test)
    add_executable(rhi_texture_preparation_test tests/rhi_texture_preparation_test.cpp)
    target_link_libraries(rhi_texture_preparation_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_texture_preparation COMMAND rhi_texture_preparation_test)
    add_executable(rhi_history_test tests/rhi_history_test.cpp)
    add_executable(rhi_model_replay_test tests/rhi_model_replay_test.cpp)
    target_link_libraries(rhi_model_replay_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_model_replay COMMAND rhi_model_replay_test)
    target_link_libraries(rhi_history_test PRIVATE mikudancestudio_rhi)
    add_test(NAME rhi_history COMMAND rhi_history_test)
    if(WIN32 AND MIKUDANCESTUDIO_RHI_D3D9)
        add_executable(rhi_texture_preparation_d3d9_test tests/rhi_texture_preparation_d3d9_test.cpp)
        target_link_libraries(rhi_texture_preparation_d3d9_test PRIVATE mikudancestudio_rhi user32)
        add_test(NAME rhi_texture_preparation_d3d9 COMMAND rhi_texture_preparation_d3d9_test)
        set_tests_properties(rhi_texture_preparation_d3d9 PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME rhi_mip_preparation_d3d9 COMMAND rhi_texture_preparation_d3d9_test --mips)
        set_tests_properties(rhi_mip_preparation_d3d9 PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop TIMEOUT 180)
    endif()
    add_executable(mds_scene_fixture tests/scene_cli_fixture.cpp)
    target_include_directories(mds_scene_fixture PRIVATE tests)
    target_link_libraries(mds_scene_fixture PRIVATE mikudancestudio_formats)
    add_test(NAME scene_fixture COMMAND mds_scene_fixture
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture")
    set_tests_properties(scene_fixture PROPERTIES
        FIXTURES_SETUP portable_scene_sample)
    add_executable(render_snapshot_test tests/render_snapshot_test.cpp)
    add_executable(render_batch_dependency_test tests/render_batch_dependency_test.cpp)
    target_link_libraries(render_batch_dependency_test PRIVATE mikudancestudio_formats)
    add_test(NAME render_batch_dependency COMMAND render_batch_dependency_test)
    target_link_libraries(render_snapshot_test PRIVATE mikudancestudio_formats)
    add_test(NAME render_snapshot COMMAND render_snapshot_test
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture")
    set_tests_properties(render_snapshot PROPERTIES
        FIXTURES_REQUIRED portable_scene_sample)
    add_test(NAME scene_cli_missing COMMAND mds_scene_cli
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture/scene.pmm"
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture/roundtrip.pmm")
    set_tests_properties(scene_cli_missing PROPERTIES
        FIXTURES_REQUIRED portable_scene_sample WILL_FAIL TRUE)
    add_test(NAME scene_cli_roundtrip COMMAND mds_scene_cli
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture/scene.pmm"
        "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture/roundtrip.pmm"
        --reselect 1 "${CMAKE_CURRENT_BINARY_DIR}/portable_fixture/assets/recovered.pmd")
    set_tests_properties(scene_cli_roundtrip PROPERTIES
        FIXTURES_REQUIRED portable_scene_sample
        DEPENDS scene_cli_missing
        PASS_REGULAR_EXPRESSION "\\\"models\\\":1.*\\\"resolved\\\":1.*\\\"readback\\\":true")
    add_executable(vpd_format_test tests/vpd_format_test.cpp)
    target_link_libraries(vpd_format_test PRIVATE mikudancestudio_formats)
    add_test(NAME vpd_format COMMAND vpd_format_test)
    add_executable(vmd_format_test tests/vmd_format_test.cpp)
    target_link_libraries(vmd_format_test PRIVATE mikudancestudio_formats)
    add_test(NAME vmd_format COMMAND vmd_format_test)
    add_executable(model_format_header_test tests/model_format_header_test.cpp)
    target_link_libraries(model_format_header_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_format_header COMMAND model_format_header_test)
    add_executable(model_geometry_test tests/model_geometry_test.cpp)
    target_link_libraries(model_geometry_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_geometry COMMAND model_geometry_test)
    add_executable(model_sections_test tests/model_sections_test.cpp)
    target_link_libraries(model_sections_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_sections COMMAND model_sections_test)
    add_executable(model_reader_test tests/model_reader_test.cpp)
    target_link_libraries(model_reader_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_reader COMMAND model_reader_test)
    add_executable(pmm_format_test tests/pmm_format_test.cpp)
    target_link_libraries(pmm_format_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_format COMMAND pmm_format_test)
    add_executable(pmm_sections_test tests/pmm_sections_test.cpp)
    target_link_libraries(pmm_sections_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_sections COMMAND pmm_sections_test)
    add_executable(pmm_document_test tests/pmm_document_test.cpp)
    target_link_libraries(pmm_document_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_document COMMAND pmm_document_test)
    add_executable(pmm_scene_document_test tests/pmm_scene_document_test.cpp)
    target_link_libraries(pmm_scene_document_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_scene_document COMMAND pmm_scene_document_test)
    add_executable(pmm_scene_evaluation_test tests/pmm_scene_evaluation_test.cpp)
    target_link_libraries(pmm_scene_evaluation_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_scene_evaluation COMMAND pmm_scene_evaluation_test)
    add_executable(pmm_scene_world_test tests/pmm_scene_world_test.cpp)
    target_link_libraries(pmm_scene_world_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_scene_world COMMAND pmm_scene_world_test)
    add_executable(pmm_scene_session_external_test
        tests/pmm_scene_session_external_test.cpp)
    target_link_libraries(pmm_scene_session_external_test PRIVATE
        mikudancestudio_formats)
    add_test(NAME pmm_scene_session_external
             COMMAND pmm_scene_session_external_test)
    set_tests_properties(pmm_scene_session_external PROPERTIES
        SKIP_RETURN_CODE 77)
endif()
if(MIKUDANCESTUDIO_FORMATS_ONLY)
    return()
endif()
# ---------------------------------------------------------------------------
# Dependencies (provisioned by Conan; see conanfile.py / recipes/)
# ---------------------------------------------------------------------------
find_package(BULLET275 REQUIRED)

# The first portable M1 boundary: Bullet world ownership has no Win32 or D3D9
# dependency. On non-Windows hosts this target and its headless test can build
# while the existing GUI and DirectX backends remain Windows-only.
add_library(mikudancestudio_physics STATIC
    src/physics/physics_world_state.cpp
    src/physics/cpu_scene_simulation.cpp)
target_include_directories(mikudancestudio_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mikudancestudio_physics PUBLIC BULLET275::BULLET275)

if(BUILD_TESTING)
    add_executable(physics_world_state_test tests/physics_world_state_test.cpp)
    target_link_libraries(physics_world_state_test PRIVATE mikudancestudio_physics)
    add_test(NAME physics_world_state COMMAND physics_world_state_test)
    add_executable(cpu_scene_simulation_test tests/cpu_scene_simulation_test.cpp)
    target_link_libraries(cpu_scene_simulation_test PRIVATE mikudancestudio_physics)
    add_test(NAME cpu_scene_simulation COMMAND cpu_scene_simulation_test)
endif()

if(NOT WIN32)
    return()
endif()

# ---------------------------------------------------------------------------
# Core static library: application state, math, startup chain, effect API.
# ---------------------------------------------------------------------------
add_library(mikudancestudio_core STATIC
    src/app/mmd_app.cpp
    src/app/globals.cpp
    src/app/app_ctor.cpp
    src/app/runtime_state.cpp
    src/app/app_defaults.cpp
    src/main/charset_conv.cpp
    src/math/color_lerp.cpp
    src/math/math_runtime.cpp
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
    src/media/wave_data_chunk.cpp
    src/media/media_load.cpp
    src/window/command_impl.cpp
    src/app/frame_driver.cpp
    src/app/playback_catchup.cpp
    src/app/playback_state.cpp
    src/app/timeline_advance.cpp
    src/app/scene_timeline_bridge.cpp
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
    src/render/model_batch_bridge.cpp
    src/render/model_cpu_preview.cpp
    src/render/editor_viewport.cpp
    src/render/scene_cpu_frame.cpp
    src/render/scene_cpu_submission.cpp
    src/render/model_input_bridge.cpp
    src/render/rhi_scene_history.cpp
    src/render/rhi_history_replay.cpp
    src/render/bg_overlay.cpp
    src/render/device_reset.cpp
    src/app/avi_record_start.cpp
    src/app/record_readback.cpp
    src/app/dshow_record_graph.cpp
    src/model/model_init.cpp
    src/model/pose_trace.cpp
    src/model/path_resolve.cpp
    src/model/pmd_load.cpp
    src/model/runtime_cpu_model.cpp
    src/model/runtime_cpu_scene.cpp
    src/model/runtime_cpu_accessory.cpp
    src/render/runtime_cpu_scene_producer.cpp
    src/model/runtime_cpu_tracks.cpp
    src/model/cpu_pose_schedule.cpp
    src/model/bone_transform.cpp
    src/model/model_keyframe_advance.cpp
    src/model/model_keyframe_edit.cpp
    src/model/model_key_reduction.cpp
    src/model/bone_edit_undo.cpp
    src/model/model_frame_seek.cpp
    src/model/vmd_load.cpp
    src/model/vmd_track_apply.cpp
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
    src/io/pmd_save.cpp
    src/io/io_device_helpers.cpp
    src/io/vpd_file.cpp
    src/io/vmd_save.cpp
    src/io/pmm_load_dispatch.cpp
    src/io/pmm_v1_layout_win.cpp
    src/io/pmm_load_v2.cpp
    src/io/pmm_playback_options.cpp
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
    src/window/timeline_selection.cpp
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
    src/window/model_order_dialog.cpp
    src/window/gravity_setting_dialog.cpp
    src/window/english_name_dialog.cpp
    src/window/edge_thickness_dialog.cpp
    src/window/enhance_model_io.cpp
    src/window/misc_dialogs.cpp
    src/window/command_frame_edit.cpp
    src/window/frame_clipboard.cpp
    src/model/global_key_registration.cpp
    src/window/command_panel_toggles.cpp
    src/window/bone_pose_clipboard.cpp
    src/window/bone_pose_edit.cpp
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
# Host integration surface for embedded MMEffect (mme_bridge -> MmeHost* declarations).
target_include_directories(mikudancestudio_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include)
target_link_libraries(mikudancestudio_core PUBLIC
    mikudancestudio_physics
    mikudancestudio_formats
    mikudancestudio_rhi
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
option(MIKUDANCESTUDIO_RHI_PREVIEW "Include RHI editor viewport and independent host (runtime default DX9)" ON)
if(MIKUDANCESTUDIO_DIAG OR (MIKUDANCESTUDIO_RHI_PREVIEW AND (MIKUDANCESTUDIO_RHI_D3D11 OR MIKUDANCESTUDIO_RHI_D3D12 OR MIKUDANCESTUDIO_RHI_VULKAN)))
    target_compile_definitions(mikudancestudio_core PUBLIC MIKUDANCESTUDIO_CPU_RENDERING=1)
    target_sources(mikudancestudio_core PRIVATE src/render/editor_scene.cpp)
    target_sources(mikudancestudio_core PRIVATE src/main/rhi_preview_host.cpp)
endif()
if (MIKUDANCESTUDIO_DIAG)
    target_compile_definitions(mikudancestudio_core PUBLIC MIKUDANCESTUDIO_DIAG=1)
    target_link_libraries(mikudancestudio_core PUBLIC d3dcompiler) # diagnostic depth-history shader
    target_sources(mikudancestudio_core PRIVATE src/io/pmm_runtime_parity_diag.cpp)
    target_sources(mikudancestudio_core PRIVATE src/io/pmm_world_parity_diag.cpp)
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
if(MIKUDANCESTUDIO_DIAG)
    target_compile_definitions(MMEffect PRIVATE MIKUDANCESTUDIO_DIAG=1)
endif()
target_include_directories(MMEffect PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/src/mmhack   # mmhack_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # d3dx_dyn.hpp (runtime d3dx9 forwarding)
)
target_compile_definitions(MMEffect PUBLIC MIKUDANCESTUDIO_MME_EMBEDDED=1)
target_link_libraries(MMEffect PUBLIC D3DXRuntime)
target_link_libraries(MMEffect PRIVATE
    user32       # Windows, menus and messages
    gdi32        # GDI+ backend
    gdiplus      # Animated textures (GIF/APNG decoding)
    comctl32     # Assignment dialog list view
    comdlg32     # Effect file selection dialog
    shell32
    ole32
    winmm        # timeGetTime
    shlwapi
    advapi32
)
target_link_libraries(MikuDanceStudio PRIVATE MMEffect)
if(MIKUDANCESTUDIO_DIAG OR (MIKUDANCESTUDIO_RHI_PREVIEW AND (MIKUDANCESTUDIO_RHI_D3D11 OR MIKUDANCESTUDIO_RHI_D3D12 OR MIKUDANCESTUDIO_RHI_VULKAN)))
    add_executable(mds_rhi_preview src/tools/rhi_preview_main.cpp)
    target_link_libraries(mds_rhi_preview PRIVATE mikudancestudio_core MMEffect dsound)
    target_link_options(mds_rhi_preview PRIVATE "/MANIFEST:NO" "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}")
endif()

# Resource IDs are distinct from the host. Icon child IDs are explicit too.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(BUILD_TESTING)
    add_test(NAME cpu_live_evidence COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpu_live_evidence.py)
    if(TARGET mds_rhi_preview)
        foreach(api dx11 dx12 vulkan)
            if((api STREQUAL "dx11" AND MIKUDANCESTUDIO_RHI_D3D11) OR
               (api STREQUAL "dx12" AND MIKUDANCESTUDIO_RHI_D3D12) OR
               (api STREQUAL "vulkan" AND MIKUDANCESTUDIO_RHI_VULKAN))
                add_test(NAME rhi_hidden_cpu_presentation_${api}
                    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hidden_cpu_presentation.py
                    --preview $<TARGET_FILE:mds_rhi_preview> --api ${api})
                set_tests_properties(rhi_hidden_cpu_presentation_${api} PROPERTIES TIMEOUT 180 LABELS "hidden;presentation;cpu-import")
            endif()
        endforeach()
        foreach(api dx11 dx12)
            if((api STREQUAL "dx11" AND MIKUDANCESTUDIO_RHI_D3D11) OR
               (api STREQUAL "dx12" AND MIKUDANCESTUDIO_RHI_D3D12))
                foreach(driver hardware warp)
                    add_test(NAME rhi_independent_${api}_${driver}
                        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_independent_rhi.py
                        --preview $<TARGET_FILE:mds_rhi_preview>
                        --application $<TARGET_FILE:MikuDanceStudio> --api ${api} --driver ${driver})
                    set_tests_properties(rhi_independent_${api}_${driver} PROPERTIES
                        TIMEOUT 180 RUN_SERIAL TRUE LABELS "graphics;cpu-import")
                endforeach()
            endif()
        endforeach()
    endif()
endif()
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

if(BUILD_TESTING)
    add_executable(vmd_save_adapter_test tests/vmd_save_adapter_test.cpp)
    target_link_libraries(vmd_save_adapter_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME vmd_save_adapter COMMAND vmd_save_adapter_test)
    add_executable(vmd_load_adapter_test tests/vmd_load_adapter_test.cpp)
    target_link_libraries(vmd_load_adapter_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME vmd_load_adapter COMMAND vmd_load_adapter_test)
    add_executable(vmd_record_decode_test tests/vmd_record_decode_test.cpp src/model/vmd_load.cpp src/model/vmd_track_apply.cpp)
    target_link_libraries(vmd_record_decode_test PRIVATE mikudancestudio_formats)
    target_include_directories(vmd_record_decode_test PRIVATE include)
    target_link_libraries(vmd_record_decode_test PRIVATE user32)
    add_test(NAME vmd_record_decode COMMAND vmd_record_decode_test)
    add_executable(key_registrars_test tests/key_registrars_test.cpp src/model/key_registrars.cpp)
    target_include_directories(key_registrars_test PRIVATE include)
    target_link_libraries(key_registrars_test PRIVATE user32 D3DXRuntime)
    add_test(NAME key_registrars COMMAND key_registrars_test)
    add_executable(bullet_allocation_test tests/bullet_allocation_test.cpp)
    target_link_libraries(bullet_allocation_test PRIVATE BULLET275::BULLET275)
    add_test(NAME bullet_allocation COMMAND bullet_allocation_test)
    add_executable(background_quad_test tests/background_quad_test.cpp)
    target_include_directories(background_quad_test PRIVATE include)
    add_test(NAME background_quad COMMAND background_quad_test)
    add_executable(background_picture_test tests/background_picture_test.cpp)
    target_link_libraries(background_picture_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(background_picture_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(background_picture_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    add_test(NAME background_picture COMMAND background_picture_test)
    set_tests_properties(background_picture PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(mmdxshow_protocol_test tests/mmdxshow_protocol_test.cpp)
    target_link_libraries(mmdxshow_protocol_test PRIVATE ole32 strmiids)
    add_dependencies(mmdxshow_protocol_test MMDxShow)
    add_test(NAME mmdxshow_protocol COMMAND mmdxshow_protocol_test $<TARGET_FILE:MMDxShow>)
    add_executable(model_frame_seek_test tests/model_frame_seek_test.cpp
        src/model/model_frame_seek.cpp)
    target_include_directories(model_frame_seek_test PRIVATE include)
    target_link_libraries(model_frame_seek_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_frame_seek COMMAND model_frame_seek_test)
    add_executable(model_playback_physics_test tests/model_playback_physics_test.cpp)
    target_link_libraries(model_playback_physics_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_playback_physics COMMAND model_playback_physics_test)
    add_executable(global_key_registration_test tests/global_key_registration_test.cpp
        src/model/global_key_registration.cpp)
    target_include_directories(global_key_registration_test PRIVATE include)
    target_link_libraries(global_key_registration_test PRIVATE user32)
    add_test(NAME global_key_registration COMMAND global_key_registration_test)
    add_executable(pmm_display_group_save_test tests/pmm_display_group_save_test.cpp)
    target_include_directories(pmm_display_group_save_test PRIVATE src/io)
    target_link_libraries(pmm_display_group_save_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME pmm_display_group_save COMMAND pmm_display_group_save_test)
    add_executable(pmm_playback_options_test tests/pmm_playback_options_test.cpp
        src/io/pmm_playback_options.cpp)
    target_include_directories(pmm_playback_options_test PRIVATE include src/io)
    target_link_libraries(pmm_playback_options_test PRIVATE user32)
    add_test(NAME pmm_playback_options COMMAND pmm_playback_options_test)
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(mmd_math_test tests/mmd_math_test.cpp
            tests/mmd_math_x87_reference.cpp)
        target_link_libraries(mmd_math_test PRIVATE BULLET275::BULLET275)
        add_test(NAME mmd_math COMMAND mmd_math_test)
    endif()
    add_executable(wave_data_chunk_test tests/wave_data_chunk_test.cpp
        src/media/wave_data_chunk.cpp)
    target_include_directories(wave_data_chunk_test PRIVATE include)
    add_test(NAME wave_data_chunk COMMAND wave_data_chunk_test)
    add_executable(pmx_buffer_failure_test tests/pmx_buffer_failure_test.cpp)
    target_link_libraries(pmx_buffer_failure_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME pmx_buffer_failure COMMAND pmx_buffer_failure_test)
    add_executable(model_reader_adapter_test tests/model_reader_adapter_test.cpp)
    target_include_directories(model_reader_adapter_test PRIVATE src/model)
    target_link_libraries(model_reader_adapter_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME model_reader_pmd_adapter COMMAND model_reader_adapter_test pmd)
    add_test(NAME model_reader_pmx_adapter COMMAND model_reader_adapter_test pmx)
    add_test(NAME model_reader_pmx_physics_adapter
        COMMAND model_reader_adapter_test pmx_physics)
    add_executable(model_reader_external_test tests/model_reader_external_test.cpp)
    target_include_directories(model_reader_external_test PRIVATE src/model)
    target_link_libraries(model_reader_external_test PRIVATE mikudancestudio_formats)
    add_test(NAME model_reader_external COMMAND model_reader_external_test)
    set_tests_properties(model_reader_external PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(model_reader_runtime_external_test
        tests/model_reader_runtime_external_test.cpp)
    target_include_directories(model_reader_runtime_external_test PRIVATE src/model)
    target_link_libraries(model_reader_runtime_external_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME model_reader_runtime_external COMMAND model_reader_runtime_external_test)
    set_tests_properties(model_reader_runtime_external PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(runtime_cpu_model_test tests/runtime_cpu_model_test.cpp)
    target_link_libraries(runtime_cpu_model_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME runtime_cpu_model COMMAND runtime_cpu_model_test)
    if(MIKUDANCESTUDIO_RHI_PREVIEW AND (MIKUDANCESTUDIO_RHI_D3D11 OR MIKUDANCESTUDIO_RHI_D3D12 OR MIKUDANCESTUDIO_RHI_VULKAN))
        add_executable(editor_viewport_test tests/editor_viewport_test.cpp)
        target_link_libraries(editor_viewport_test PRIVATE mikudancestudio_core MMEffect dsound dbghelp)
        target_link_options(editor_viewport_test PRIVATE /DEBUG)
        target_link_options(editor_viewport_test PRIVATE "/MANIFEST:NO" "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}")
        add_executable(editor_scene_import_test tests/editor_scene_import_test.cpp)
        target_link_libraries(editor_scene_import_test PRIVATE mikudancestudio_core MMEffect dsound)
        target_link_options(editor_scene_import_test PRIVATE "/MANIFEST:NO" "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}")
        add_executable(editor_frame_test tests/editor_frame_test.cpp)
        target_include_directories(editor_frame_test PRIVATE src/app)
        target_link_libraries(editor_frame_test PRIVATE mikudancestudio_core MMEffect dsound)
        target_link_options(editor_frame_test PRIVATE "/MANIFEST:NO" "${CMAKE_CURRENT_SOURCE_DIR}/${MIKUDANCESTUDIO_GEN_RES}")
        foreach(api IN ITEMS dx11 dx12 vulkan)
            if((api STREQUAL "dx11" AND MIKUDANCESTUDIO_RHI_D3D11) OR
               (api STREQUAL "dx12" AND MIKUDANCESTUDIO_RHI_D3D12) OR
               (api STREQUAL "vulkan" AND MIKUDANCESTUDIO_RHI_VULKAN))
                add_test(NAME editor_viewport_${api} COMMAND editor_viewport_test ${api})
                set_tests_properties(editor_viewport_${api} PROPERTIES TIMEOUT 120 LABELS "hidden-window;editor;rhi" RUN_SERIAL TRUE)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME editor_frame_${api} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor_frame.py --test $<TARGET_FILE:editor_frame_test> --backend ${api})
                    set_tests_properties(editor_frame_${api} PROPERTIES TIMEOUT 360 LABELS "hidden-window;editor;rhi;frame-owner" RUN_SERIAL TRUE)
                    add_test(NAME editor_scene_${api} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor_scene.py --test $<TARGET_FILE:editor_scene_import_test> --backend ${api})
                    set_tests_properties(editor_scene_${api} PROPERTIES TIMEOUT 180 LABELS "hidden-window;editor;rhi" RUN_SERIAL TRUE)
                    foreach(format IN ITEMS pmd pmx)
                        add_test(NAME editor_scene_${format}_${api} COMMAND ${Python3_EXECUTABLE}
                            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor_scene.py --test $<TARGET_FILE:editor_scene_import_test> --backend ${api} --external ${format})
                        set_tests_properties(editor_scene_${format}_${api} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "hidden-window;editor;rhi;external" RUN_SERIAL TRUE)
                    endforeach()
                endif()
            endif()
        endforeach()
    endif()
    add_executable(runtime_cpu_scene_test tests/runtime_cpu_scene_test.cpp)
    target_link_libraries(runtime_cpu_scene_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_executable(runtime_cpu_bindings_test tests/runtime_cpu_bindings_test.cpp)
    target_link_libraries(runtime_cpu_bindings_test PRIVATE mikudancestudio_core MMEffect dsound)
    if(MIKUDANCESTUDIO_RHI_D3D11 AND MIKUDANCESTUDIO_RHI_D3D12)
        add_executable(runtime_screen_texture_test tests/runtime_screen_texture_test.cpp)
        target_link_libraries(runtime_screen_texture_test PRIVATE mikudancestudio_core MMEffect dsound)
        add_executable(runtime_self_shadow_test tests/runtime_self_shadow_test.cpp)
        target_link_libraries(runtime_self_shadow_test PRIVATE mikudancestudio_core MMEffect dsound dbghelp)
        if(MSVC)
            target_link_options(runtime_self_shadow_test PRIVATE /DEBUG)
        endif()
        if(Python3_Interpreter_FOUND)
            set(MDS_SAMPLING_BACKENDS dx11 dx12)
            if(MIKUDANCESTUDIO_RHI_VULKAN)
                list(APPEND MDS_SAMPLING_BACKENDS vulkan)
            endif()
            foreach(api IN LISTS MDS_SAMPLING_BACKENDS)
                add_test(NAME screen_texture_${api} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_texture.py --test $<TARGET_FILE:runtime_screen_texture_test>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --backend ${api})
                set_tests_properties(screen_texture_${api} PROPERTIES TIMEOUT 180 LABELS "offscreen;screen-texture")
                add_test(NAME screen_texture_hidden_preview_${api} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_texture.py --test $<TARGET_FILE:mds_rhi_preview>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --backend ${api} --preview)
                set_tests_properties(screen_texture_hidden_preview_${api} PROPERTIES TIMEOUT 90 LABELS "hidden;screen-texture;presentation")
                add_test(NAME self_shadow_portable_producer_${api} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --variant ${api}-hardware --sampling portable --tolerant)
                set_tests_properties(self_shadow_portable_producer_${api} PROPERTIES TIMEOUT 260 LABELS "offscreen;portable-sampling;bounded")
                foreach(format pmd pmx)
                    add_test(NAME screen_texture_${format}_${api} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_texture.py --test $<TARGET_FILE:runtime_screen_texture_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --backend ${api} --external ${format})
                    set_tests_properties(screen_texture_${format}_${api} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "offscreen;screen-texture;external")
                    add_test(NAME self_shadow_portable_${format}_${api} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --external ${format} --variant ${api}-hardware --sampling portable --tolerant)
                    set_tests_properties(self_shadow_portable_${format}_${api} PROPERTIES TIMEOUT 130 SKIP_RETURN_CODE 77 LABELS "offscreen;external;portable-sampling;bounded")
                endforeach()
            endforeach()
            foreach(api IN ITEMS dx12-debug vulkan-debug)
                if(api STREQUAL "vulkan-debug" AND NOT MIKUDANCESTUDIO_RHI_VULKAN)
                    continue()
                endif()
                add_test(NAME screen_texture_${api} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_texture.py --test $<TARGET_FILE:runtime_screen_texture_test>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --backend ${api})
                set_tests_properties(screen_texture_${api} PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "offscreen;screen-texture;validation")
            endforeach()
            if(MIKUDANCESTUDIO_RHI_VULKAN)
                add_test(NAME self_shadow_producer_vulkan_hardware COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --variant vulkan-hardware --tolerant)
                set_tests_properties(self_shadow_producer_vulkan_hardware PROPERTIES TIMEOUT 260 LABELS "offscreen;vulkan;bounded")
                add_test(NAME self_shadow_producer_vulkan_validation COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                    --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --variant vulkan-hardware-debug --tolerant)
                set_tests_properties(self_shadow_producer_vulkan_validation PROPERTIES TIMEOUT 260 SKIP_RETURN_CODE 77 LABELS "offscreen;vulkan;bounded")
                foreach(format pmd pmx)
                    add_test(NAME self_shadow_dx9_tolerant_${format}_vulkan COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --external ${format} --variant vulkan-hardware --tolerant)
                    set_tests_properties(self_shadow_dx9_tolerant_${format}_vulkan PROPERTIES TIMEOUT 130 SKIP_RETURN_CODE 77 LABELS "offscreen;vulkan;external;bounded")
                endforeach()
            endif()
            foreach(api dx11 dx12)
                foreach(driver hardware warp)
                    add_test(NAME self_shadow_producer_${api}_${driver} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --variant ${api}-${driver})
                    set_tests_properties(self_shadow_producer_${api}_${driver} PROPERTIES TIMEOUT 260 LABELS "offscreen")
                endforeach()
            endforeach()
            foreach(format pmd pmx)
                foreach(api dx11 dx12)
                    add_test(NAME self_shadow_dx9_tolerant_${format}_${api} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --external ${format} --variant ${api}-hardware --tolerant)
                    set_tests_properties(self_shadow_dx9_tolerant_${format}_${api} PROPERTIES TIMEOUT 130 SKIP_RETURN_CODE 77 LABELS "offscreen;external;bounded")
                endforeach()
                if(MIKUDANCESTUDIO_SELF_SHADOW_STRICT_DIAGNOSTICS)
                    add_test(NAME self_shadow_dx9_strict_${format} COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_self_shadow.py --test $<TARGET_FILE:runtime_self_shadow_test>
                        --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets --external ${format} --strict)
                    set_tests_properties(self_shadow_dx9_strict_${format} PROPERTIES TIMEOUT 130 SKIP_RETURN_CODE 77 LABELS "offscreen;external;strict")
                endif()
            endforeach()
        endif()
    endif()
    if(Python3_Interpreter_FOUND)
        add_test(NAME runtime_cpu_scene COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runtime_cpu_scene.py --test $<TARGET_FILE:runtime_cpu_scene_test>)
        add_test(NAME runtime_cpu_bindings COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runtime_cpu_scene.py --passes $<TARGET_FILE:runtime_cpu_bindings_test>
            --assets ${CMAKE_CURRENT_SOURCE_DIR}/res/assets)
        add_test(NAME runtime_cpu_x_native COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runtime_cpu_scene.py --passes $<TARGET_FILE:runtime_cpu_bindings_test> --native-x)
        set_tests_properties(runtime_cpu_x_native PROPERTIES LABELS "gpu;dx9")
    endif()
    add_executable(cpu_pose_schedule_test tests/cpu_pose_schedule_test.cpp)
    target_link_libraries(cpu_pose_schedule_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME cpu_pose_schedule COMMAND cpu_pose_schedule_test)
    add_executable(cpu_scene_simulation_native_test tests/cpu_scene_simulation_native_test.cpp)
    target_link_libraries(cpu_scene_simulation_native_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME cpu_scene_simulation_native COMMAND cpu_scene_simulation_native_test)
    add_executable(pmm_format_adapter_test tests/pmm_format_adapter_test.cpp)
    target_include_directories(pmm_format_adapter_test PRIVATE src/io)
    target_link_libraries(pmm_format_adapter_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_format_adapter COMMAND pmm_format_adapter_test)
    add_executable(pmm_document_adapter_test tests/pmm_document_adapter_test.cpp)
    target_include_directories(pmm_document_adapter_test PRIVATE src/io)
    target_link_libraries(pmm_document_adapter_test PRIVATE mikudancestudio_formats)
    add_test(NAME pmm_document_adapter COMMAND pmm_document_adapter_test)
    add_executable(pmm_save_adapter_test tests/pmm_save_adapter_test.cpp)
    target_include_directories(pmm_save_adapter_test PRIVATE src/io)
    target_link_libraries(pmm_save_adapter_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME pmm_save_adapter COMMAND pmm_save_adapter_test)
    set_tests_properties(pmm_save_adapter PROPERTIES TIMEOUT 30)
    add_executable(pmm_scene_seek_adapter_test
        tests/pmm_scene_seek_adapter_test.cpp)
    target_link_libraries(pmm_scene_seek_adapter_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME pmm_scene_seek_adapter COMMAND pmm_scene_seek_adapter_test)
    add_executable(pmm_scene_timeline_bridge_test
        tests/pmm_scene_timeline_bridge_test.cpp)
    target_link_libraries(pmm_scene_timeline_bridge_test PRIVATE
        mikudancestudio_core MMEffect dsound)
    add_test(NAME pmm_scene_timeline_bridge
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runtime_cpu_scene.py
             --bridge $<TARGET_FILE:pmm_scene_timeline_bridge_test>)
    set_tests_properties(pmm_scene_timeline_bridge PROPERTIES
        SKIP_RETURN_CODE 77)
    add_executable(pmm_v1_layout_adapter_test tests/pmm_v1_layout_adapter_test.cpp)
    target_include_directories(pmm_v1_layout_adapter_test PRIVATE src/io)
    target_link_libraries(pmm_v1_layout_adapter_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME pmm_v1_layout_adapter COMMAND pmm_v1_layout_adapter_test)
    set_tests_properties(pmm_v1_layout_adapter PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(pmm_v1_relocation_gui_test tests/pmm_v1_relocation_gui_test.cpp)
    target_link_libraries(pmm_v1_relocation_gui_test PRIVATE
        mikudancestudio_formats uiautomationcore ole32 user32)
    if(MIKUDANCESTUDIO_DIAG)
        target_compile_definitions(pmm_v1_relocation_gui_test PRIVATE
            MDS_EXPECT_RUNTIME_PARITY=1)
    endif()
    add_test(NAME pmm_v1_relocation_gui COMMAND pmm_v1_relocation_gui_test
        $<TARGET_FILE:MikuDanceStudio>)
    set_tests_properties(pmm_v1_relocation_gui PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop)
    if(MIKUDANCESTUDIO_DIAG AND TARGET rhi_d3d9_parity_test)
        add_test(NAME rhi_application_swapchain_gui COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_application_swapchain.py
            --gui $<TARGET_FILE:pmm_v1_relocation_gui_test>
            --application $<TARGET_FILE:MikuDanceStudio>
            --evidence ${CMAKE_CURRENT_BINARY_DIR}/application-swapchain-evidence)
        set_tests_properties(rhi_application_swapchain_gui PROPERTIES SKIP_RETURN_CODE 77
            TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_application_swapchain_single_gui COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_application_swapchain.py
            --gui $<TARGET_FILE:pmm_v1_relocation_gui_test>
            --application $<TARGET_FILE:MikuDanceStudio>
            --evidence ${CMAKE_CURRENT_BINARY_DIR}/application-swapchain-single-evidence --single-sample)
        set_tests_properties(rhi_application_swapchain_single_gui PROPERTIES SKIP_RETURN_CODE 77
            TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)

        if(TARGET rhi_d3d11_real_batch_test)
            add_test(NAME rhi_real_d3d11_batch_gui COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_real_d3d11_batch.py
                --gui $<TARGET_FILE:pmm_v1_relocation_gui_test>
                --application $<TARGET_FILE:MikuDanceStudio>
                --dx9-test $<TARGET_FILE:rhi_d3d9_parity_test>
                --replay $<TARGET_FILE:rhi_d3d11_real_batch_test>
                --evidence ${CMAKE_CURRENT_BINARY_DIR}/real-dx11-evidence)
            set_tests_properties(rhi_real_d3d11_batch_gui PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 600 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_real_d3d9_history_replay_gui COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_real_d3d9_history_replay.py
                --gui $<TARGET_FILE:pmm_v1_relocation_gui_test>
                --application $<TARGET_FILE:MikuDanceStudio>
                --dx9-test $<TARGET_FILE:rhi_d3d9_parity_test>
                --probe $<TARGET_FILE:rhi_d3d9_msaa_history_test>
                --evidence ${CMAKE_CURRENT_BINARY_DIR}/real-dx9-replay-evidence)
            set_tests_properties(rhi_real_d3d9_history_replay_gui PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 600 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
            add_test(NAME rhi_real_d3d11_history_gui COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_real_d3d11_history.py
                --gui $<TARGET_FILE:pmm_v1_relocation_gui_test>
                --application $<TARGET_FILE:MikuDanceStudio>
                --dx9-test $<TARGET_FILE:rhi_d3d9_parity_test>
                --replay $<TARGET_FILE:rhi_d3d11_real_batch_test>
                --evidence ${CMAKE_CURRENT_BINARY_DIR}/real-dx11-history-evidence)
            set_tests_properties(rhi_real_d3d11_history_gui PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 600 RUN_SERIAL TRUE RESOURCE_LOCK mds_gui_desktop)
        endif()
        add_test(NAME rhi_real_shader_inputs_gui COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_shader_inputs_gui PROPERTIES SKIP_RETURN_CODE 77
            TIMEOUT 180 RUN_SERIAL TRUE ENVIRONMENT "MDS_RHI_GUI_MODE=model_textures_native;MDS_RHI_SHADER_INPUTS=1")
        foreach(mode IN ITEMS model model_optin model_toon model_toon_optin
            model_textures model_textures_preparation model_textures_native model_textures_combined model_textures_combined_add
            model_textures_sphere model_textures_sphere_add)
            add_test(NAME rhi_real_${mode}_gui COMMAND pmm_v1_relocation_gui_test
                $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
            set_tests_properties(rhi_real_${mode}_gui PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 180 RUN_SERIAL TRUE ENVIRONMENT "MDS_RHI_GUI_MODE=${mode}")
        endforeach()
        add_test(NAME rhi_real_selection_gui COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_selection_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop)
        add_test(NAME rhi_real_selection_optin_gui
            COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_selection_optin_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=optin")
        add_test(NAME rhi_real_sprite_gui
            COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_sprite_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=sprite")
        add_test(NAME rhi_real_sprite_optin_gui
            COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_sprite_optin_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=sprite_optin")
        add_test(NAME rhi_real_text_gui COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_text_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=text")
        add_test(NAME rhi_real_text_optin_gui
            COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_text_optin_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=text_optin")
        add_test(NAME rhi_real_text_portable_candidate_gui
            COMMAND pmm_v1_relocation_gui_test
            $<TARGET_FILE:MikuDanceStudio> $<TARGET_FILE:rhi_d3d9_parity_test>)
        set_tests_properties(rhi_real_text_portable_candidate_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK mds_gui_desktop
            ENVIRONMENT "MDS_RHI_GUI_MODE=text_optin;MDS_RHI_PORTABLE_FONT=1;MDS_RHI_PORTABLE_FONT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/res/assets")
    endif()
    if(MIKUDANCESTUDIO_DIAG)
        add_executable(d3d9_init_probe_test tests/d3d9_init_probe_test.cpp)
        target_link_libraries(d3d9_init_probe_test PRIVATE d3d9 user32)
        add_executable(pmm_v2_selector_gui_test tests/pmm_v2_selector_gui_test.cpp)
        target_link_libraries(pmm_v2_selector_gui_test PRIVATE
            mikudancestudio_formats uiautomationcore ole32 user32)
        add_test(NAME pmm_v2_selector_gui COMMAND pmm_v2_selector_gui_test
            $<TARGET_FILE:MikuDanceStudio>)
        set_tests_properties(pmm_v2_selector_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180)
        if(TARGET rhi_d3d9_parity_test)
            add_test(NAME rhi_real_materials_v2_gui COMMAND pmm_v2_selector_gui_test
                $<TARGET_FILE:MikuDanceStudio>)
            set_tests_properties(rhi_real_materials_v2_gui PROPERTIES SKIP_RETURN_CODE 77
                TIMEOUT 180 RUN_SERIAL TRUE ENVIRONMENT "MDS_RHI_MATERIAL_SNAPSHOT=1")
        endif()
        add_test(NAME pmm_v2_multi_gui COMMAND pmm_v2_selector_gui_test
            $<TARGET_FILE:MikuDanceStudio>)
        set_tests_properties(pmm_v2_multi_gui PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 180
            ENVIRONMENT "MDS_PMM_V2_MULTI_GUI=1")
    endif()
    set_tests_properties(model_reader_pmd_adapter model_reader_pmx_adapter
        model_reader_pmx_physics_adapter
        PROPERTIES SKIP_RETURN_CODE 77)
    set_tests_properties(pmx_buffer_failure PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(charset_conv_test tests/charset_conv_test.cpp src/main/charset_conv.cpp)
    target_include_directories(charset_conv_test PRIVATE include)
    add_test(NAME charset_conv COMMAND charset_conv_test)
    add_executable(input_sequence_test tests/input_sequence_test.cpp)
    target_link_libraries(input_sequence_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME input_sequence COMMAND input_sequence_test)
    add_executable(model_ui_edit_test tests/model_ui_edit_test.cpp)
    target_link_libraries(model_ui_edit_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_ui_edit COMMAND model_ui_edit_test)
    add_executable(model_ui_navigation_test tests/model_ui_navigation_test.cpp)
    target_link_libraries(model_ui_navigation_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_ui_navigation COMMAND model_ui_navigation_test)
    add_executable(bone_pick_test tests/bone_pick_test.cpp)
    target_link_libraries(bone_pick_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME bone_pick COMMAND bone_pick_test)
    add_executable(model_viewport_test tests/model_viewport_test.cpp)
    target_link_libraries(model_viewport_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_viewport COMMAND model_viewport_test)
    add_executable(model_effect_query_test tests/model_effect_query_test.cpp)
    target_link_libraries(model_effect_query_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_effect_query COMMAND model_effect_query_test)
    add_executable(native_host_query_test tests/native_host_query_test.cpp)
    target_include_directories(native_host_query_test PRIVATE third_party/mmeffect/include)
    target_link_libraries(native_host_query_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(native_host_query_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(native_host_query_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    add_executable(mmh_host_parity_test tests/mmh_host_parity_test.cpp tests/mmh_oracle_host.cpp)
    target_include_directories(mmh_host_parity_test PRIVATE third_party/mmeffect/include third_party/mmeffect/src third_party/mmeffect/src/mmhack third_party/mmeffect/src/mmeffect)
    target_link_libraries(mmh_host_parity_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(mmh_host_parity_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(mmh_host_parity_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    add_test(NAME mmh_host_parity COMMAND mmh_host_parity_test)
    add_test(NAME mmh_host_pmx_parity COMMAND mmh_host_parity_test --pmx 1)
    set_tests_properties(mmh_host_parity mmh_host_pmx_parity PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(native_host_render_test tests/native_host_render_test.cpp tests/mme_native_oracle.cpp)
    target_include_directories(native_host_render_test PRIVATE third_party/mmeffect/include third_party/mmeffect/src third_party/mmeffect/src/mmhack third_party/mmeffect/src/mmeffect)
    target_link_libraries(native_host_render_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(native_host_render_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(native_host_render_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    add_executable(native_scene_render_test tests/native_scene_render_test.cpp tests/mme_native_oracle.cpp)
    target_include_directories(native_scene_render_test PRIVATE third_party/mmeffect/include third_party/mmeffect/src third_party/mmeffect/src/mmhack third_party/mmeffect/src/mmeffect)
    target_link_libraries(native_scene_render_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(native_scene_render_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(native_scene_render_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    set(MDS_ORIGINAL_MMD_EXE "" CACHE FILEPATH "Optional original MMD executable for native rendering comparisons")
    if(MDS_ORIGINAL_MMD_EXE)
        set(background_picture_launcher)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            list(APPEND background_picture_launcher ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_native_render.py)
        endif()
        add_test(NAME background_picture_native COMMAND ${background_picture_launcher} $<TARGET_FILE:background_picture_test> --original ${MDS_ORIGINAL_MMD_EXE})
        set_tests_properties(background_picture_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    endif()
    set(MDS_ORIGINAL_MME_DIR "" CACHE PATH "Optional directory containing original MMHack.dll and MMEffect.dll")
    set(MDS_NATIVE_PMX_FILE "" CACHE FILEPATH "Optional local PMX for the full native rendering chain")
    set(MDS_NATIVE_RAY_DIR "" CACHE PATH "Optional local ray-mmd repository for native effect comparisons")
    if(MDS_ORIGINAL_MMD_EXE AND MDS_ORIGINAL_MME_DIR)
        set(native_scene_launcher)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            list(APPEND native_scene_launcher ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_native_render.py)
        endif()
        add_test(NAME native_scene_render
            COMMAND ${native_scene_launcher} $<TARGET_FILE:native_scene_render_test>
                --original ${MDS_ORIGINAL_MMD_EXE}
                --mmhack ${MDS_ORIGINAL_MME_DIR}/MMHack.dll
                --mme ${MDS_ORIGINAL_MME_DIR}/MMEffect.dll
                --output ${CMAKE_CURRENT_BINARY_DIR}/native-scene)
        set_tests_properties(native_scene_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
        set_tests_properties(native_scene_render PROPERTIES FIXTURES_SETUP native_scene_emm)
        function(add_native_scene_case name)
            add_test(NAME native_scene_${name}
                COMMAND ${native_scene_launcher} $<TARGET_FILE:native_scene_render_test>
                    --original ${MDS_ORIGINAL_MMD_EXE}
                    --mmhack ${MDS_ORIGINAL_MME_DIR}/MMHack.dll
                    --mme ${MDS_ORIGINAL_MME_DIR}/MMEffect.dll
                    --output ${CMAKE_CURRENT_BINARY_DIR}/native-scene-${name} ${ARGN})
            set_tests_properties(native_scene_${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 RUN_SERIAL TRUE)
        endfunction()
        add_native_scene_case(features --features 1)
        add_test(NAME native_scene_numeric
            COMMAND ${native_scene_launcher} $<TARGET_FILE:scene_render_gpu_test>
                --original ${MDS_ORIGINAL_MMD_EXE} --numeric-only 1 --features 1
                --output ${CMAKE_CURRENT_BINARY_DIR}/native-scene-numeric)
        set_tests_properties(native_scene_numeric PROPERTIES TIMEOUT 120)
        add_native_scene_case(cross --cross-load ${CMAKE_CURRENT_BINARY_DIR}/native-scene/port_assigned.emm)
        set_tests_properties(native_scene_cross PROPERTIES FIXTURES_REQUIRED native_scene_emm)
        if(MDS_NATIVE_PMX_FILE)
            add_native_scene_case(pmx --model ${MDS_NATIVE_PMX_FILE})
            if(MDS_NATIVE_RAY_DIR)
                add_native_scene_case(material --model ${MDS_NATIVE_PMX_FILE}
                    --model-effect ${MDS_NATIVE_RAY_DIR}/Main/main.fx)
                add_native_scene_case(outline --model ${MDS_NATIVE_PMX_FILE}
                    --model-effect "${MDS_NATIVE_RAY_DIR}/Outline/Fixed Color/outline.fx")
                add_native_scene_case(post --model ${MDS_NATIVE_PMX_FILE}
                    --model-effect ${MDS_NATIVE_RAY_DIR}/Main/main.fx
                    --scene-effect ${MDS_NATIVE_RAY_DIR}/ray.fx)
            endif()
        endif()
    endif()
    add_executable(model_render_test tests/model_render_test.cpp)
    target_link_libraries(model_render_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_render COMMAND model_render_test)
    add_executable(model_render_gpu_test tests/model_render_gpu_test.cpp)
    target_link_libraries(model_render_gpu_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_render_gpu COMMAND model_render_gpu_test)
    set_tests_properties(model_render_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(scene_render_gpu_test tests/scene_render_gpu_test.cpp)
    target_link_libraries(scene_render_gpu_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(scene_render_gpu_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME scene_render_gpu COMMAND scene_render_gpu_test)
    set_tests_properties(scene_render_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(scene_effect_gpu_test tests/scene_effect_gpu_test.cpp)
    target_include_directories(scene_effect_gpu_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(scene_effect_gpu_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(scene_effect_gpu_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME scene_effect_gpu COMMAND scene_effect_gpu_test)
    set_tests_properties(scene_effect_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        # Leave the non-relocatable original x86 EXE's 0x400000..0x5A3000 free.
        target_link_options(scene_render_gpu_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
        target_link_options(scene_effect_gpu_test PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
    endif()
    add_executable(scene_mme_gpu_test tests/scene_mme_gpu_test.cpp tests/mme_oracle_host.cpp tests/mme_native_oracle.cpp)
    target_include_directories(scene_mme_gpu_test PRIVATE third_party/mmeffect/src third_party/mmeffect/src/mmhack third_party/mmeffect/src/mmeffect)
    target_link_libraries(scene_mme_gpu_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    target_compile_definitions(scene_mme_gpu_test PRIVATE MDS_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME scene_mme_gpu COMMAND scene_mme_gpu_test)
    set_tests_properties(scene_mme_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_executable(model_file_io_test tests/model_file_io_test.cpp)
    target_include_directories(model_file_io_test PRIVATE src/io)
    target_link_libraries(model_file_io_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME model_file_io COMMAND model_file_io_test)
    add_executable(scene_roundtrip_test tests/scene_roundtrip_test.cpp)
    target_link_libraries(scene_roundtrip_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME scene_roundtrip COMMAND scene_roundtrip_test)
    set_tests_properties(scene_roundtrip PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    add_executable(physics_trajectory_test tests/physics_trajectory_test.cpp)
    target_link_libraries(physics_trajectory_test PRIVATE mikudancestudio_core MMEffect dsound bcrypt)
    add_test(NAME physics_trajectory COMMAND physics_trajectory_test)
    set_tests_properties(physics_trajectory PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_test(NAME physics_trajectory_d3dx_amd COMMAND physics_trajectory_test --d3dx-dispatch amd)
        set_tests_properties(physics_trajectory_d3dx_amd PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
        add_test(NAME bullet_kernel_parity COMMAND physics_trajectory_test --kernels 1)
        set_tests_properties(bullet_kernel_parity PROPERTIES TIMEOUT 30)
    endif()
    add_executable(pmd_save_test tests/pmd_save_test.cpp)
    target_link_libraries(pmd_save_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME pmd_save COMMAND pmd_save_test)
    add_executable(kinect_tracking_test tests/kinect_tracking_test.cpp)
    target_link_libraries(kinect_tracking_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME kinect_tracking COMMAND kinect_tracking_test)
    add_executable(openni_loader_test tests/openni_loader_test.cpp)
    target_link_libraries(openni_loader_test PRIVATE mikudancestudio_core MMEffect dsound)
    set(ONI_PREFIX "")
    set(ONI_INIT_SUFFIX "")
    set(ONI_VOID_SUFFIX "")
    set(ONI_ARG_SUFFIX "")
    set(ONI_TWO_SUFFIX "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(ONI_PREFIX "_")
        set(ONI_INIT_SUFFIX "@20")
        set(ONI_VOID_SUFFIX "@0")
        set(ONI_ARG_SUFFIX "@4")
        set(ONI_TWO_SUFFIX "@8")
    endif()
    set(ONI_VERSION_EXPORT "?OpenNIGetVersion@@YGXPAM@Z=${ONI_PREFIX}FixtureVersion${ONI_ARG_SUFFIX}")
    # Replace only our placeholders: configure_file would interpret portions of
    # MSVC's decorated export names as @VARIABLE@ expressions.
    file(READ tests/openni_fixture.def.in ONI_DEF_TEMPLATE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/openni_fixture.def.in")
    set(ONI_DEF "${ONI_DEF_TEMPLATE}")
    foreach(ONI_VAR ONI_PREFIX ONI_INIT_SUFFIX ONI_VOID_SUFFIX ONI_ARG_SUFFIX ONI_TWO_SUFFIX ONI_VERSION_EXPORT)
        string(REPLACE "@${ONI_VAR}@" "${${ONI_VAR}}" ONI_DEF "${ONI_DEF}")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/openni_fixture.def "${ONI_DEF}")
    add_library(openni_fixture SHARED tests/openni_fixture.cpp ${CMAKE_CURRENT_BINARY_DIR}/openni_fixture.def)
    target_include_directories(openni_fixture PRIVATE include)
    string(REPLACE "${ONI_VERSION_EXPORT}" "" ONI_DEF "${ONI_DEF}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/openni_incomplete_fixture.def "${ONI_DEF}")
    add_library(openni_incomplete_fixture SHARED tests/openni_fixture.cpp ${CMAKE_CURRENT_BINARY_DIR}/openni_incomplete_fixture.def)
    target_include_directories(openni_incomplete_fixture PRIVATE include)
    add_dependencies(openni_loader_test openni_fixture openni_incomplete_fixture)
    add_test(NAME openni_loader COMMAND openni_loader_test $<TARGET_FILE:openni_fixture> $<TARGET_FILE:openni_incomplete_fixture>)
    set_tests_properties(kinect_tracking openni_loader PROPERTIES TIMEOUT 30)
    add_executable(timeline_selection_test tests/timeline_selection_test.cpp
        src/window/timeline_selection.cpp)
    target_include_directories(timeline_selection_test PRIVATE include)
    add_test(NAME timeline_selection COMMAND timeline_selection_test)
    add_executable(model_key_reduction_test tests/model_key_reduction_test.cpp
        src/model/model_key_reduction.cpp)
    target_include_directories(model_key_reduction_test PRIVATE include)
    add_test(NAME model_key_reduction COMMAND model_key_reduction_test)
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
    add_executable(camera_world_transform_test tests/camera_world_transform_test.cpp)
    target_link_libraries(camera_world_transform_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME camera_world_transform COMMAND camera_world_transform_test)
    if(MSVC)
        add_executable(pmx_outline_test tests/pmx_outline_test.cpp)
        target_compile_options(pmx_outline_test PRIVATE /openmp /Gy)
        target_link_options(pmx_outline_test PRIVATE /OPT:REF)
        target_link_libraries(pmx_outline_test PRIVATE mikudancestudio_core MMEffect dsound)
        add_test(NAME pmx_outline COMMAND pmx_outline_test)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(drag_precision_test tests/drag_precision_test.cpp)
        target_link_libraries(drag_precision_test PRIVATE mikudancestudio_core MMEffect dsound)
        add_test(NAME drag_precision COMMAND drag_precision_test)
        if(MSVC)
            add_executable(pmd_skinning_test tests/pmd_skinning_test.cpp)
            target_include_directories(pmd_skinning_test PRIVATE include third_party/mmeffect/include)
            target_compile_options(pmd_skinning_test PRIVATE /openmp /Gy)
            target_link_options(pmd_skinning_test PRIVATE /OPT:REF)
            target_link_libraries(pmd_skinning_test PRIVATE D3DXRuntime mikudancestudio_rhi BULLET275::BULLET275)
            add_test(NAME pmd_skinning COMMAND pmd_skinning_test)
        endif()
    endif()
    add_executable(model_cpu_geometry_test tests/model_cpu_geometry_test.cpp src/app/app_ctor.cpp)
    target_include_directories(model_cpu_geometry_test PRIVATE include third_party/mmeffect/include)
    target_compile_options(model_cpu_geometry_test PRIVATE /Gy)
    if(MIKUDANCESTUDIO_DIAG)
        target_compile_definitions(model_cpu_geometry_test PRIVATE MIKUDANCESTUDIO_DIAG=1)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_options(model_cpu_geometry_test PRIVATE /openmp)
    endif()
    target_link_options(model_cpu_geometry_test PRIVATE /OPT:REF)
    target_link_libraries(model_cpu_geometry_test PRIVATE mikudancestudio_rhi mikudancestudio_formats D3DXRuntime BULLET275::BULLET275)
    add_test(NAME model_cpu_geometry COMMAND model_cpu_geometry_test)
    add_executable(accessory_selection_test tests/accessory_selection_test.cpp
        src/app/app_utilities.cpp src/window/ui_refresh.cpp)
    target_include_directories(accessory_selection_test PRIVATE include)
    target_compile_options(accessory_selection_test PRIVATE /Gy)
    target_link_options(accessory_selection_test PRIVATE /OPT:REF)
    target_link_libraries(accessory_selection_test PRIVATE user32)
    add_test(NAME accessory_selection COMMAND accessory_selection_test)
    add_executable(edit_target_layout_test tests/edit_target_layout_test.cpp
        src/window/ui_init.cpp src/model/model_query_helpers.cpp)
    target_include_directories(edit_target_layout_test PRIVATE include)
    target_link_libraries(edit_target_layout_test PRIVATE mikudancestudio_rhi)
    target_compile_options(edit_target_layout_test PRIVATE /Gy)
    target_link_options(edit_target_layout_test PRIVATE /OPT:REF)
    target_link_libraries(edit_target_layout_test PRIVATE user32 dsound D3DXRuntime)
    add_test(NAME edit_target_layout COMMAND edit_target_layout_test)
    add_executable(physics_floor_test tests/physics_floor_test.cpp)
    target_link_libraries(physics_floor_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME physics_floor COMMAND physics_floor_test)
    add_executable(frame_state_dump_test tests/frame_state_dump_test.cpp)
    target_include_directories(frame_state_dump_test PRIVATE include)
    target_compile_definitions(frame_state_dump_test PRIVATE MIKUDANCESTUDIO_DIAG=1)
    target_link_libraries(frame_state_dump_test PRIVATE user32)
    add_test(NAME frame_state_dump COMMAND frame_state_dump_test)
    add_executable(bone_mirror_paste_test tests/bone_mirror_paste_test.cpp
        src/window/bone_pose_clipboard.cpp src/window/accessory_paste.cpp
        src/window/bone_pose_edit.cpp src/model/bone_edit_undo.cpp
        src/model/model_keyframe_edit.cpp)
    target_include_directories(bone_mirror_paste_test PRIVATE include)
    target_compile_options(bone_mirror_paste_test PRIVATE /Gy)
    target_link_options(bone_mirror_paste_test PRIVATE /OPT:REF)
    target_link_libraries(bone_mirror_paste_test PRIVATE user32 D3DXRuntime)
    add_test(NAME bone_mirror_paste COMMAND bone_mirror_paste_test)
    add_executable(option_dialog_test tests/option_dialog_test.cpp
        src/window/model_order_dialog.cpp src/window/gravity_setting_dialog.cpp
        src/model/global_key_registration.cpp)
    target_include_directories(option_dialog_test PRIVATE include)
    target_compile_options(option_dialog_test PRIVATE /Gy)
    target_link_options(option_dialog_test PRIVATE /OPT:REF)
    target_link_libraries(option_dialog_test PRIVATE user32 comctl32 BULLET275::BULLET275 D3DXRuntime)
    add_test(NAME option_dialog COMMAND option_dialog_test)
    add_executable(edge_thickness_dialog_test tests/edge_thickness_dialog_test.cpp)
    target_link_libraries(edge_thickness_dialog_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_dependencies(edge_thickness_dialog_test MikuDanceStudio)
    add_test(NAME edge_thickness_dialog
        COMMAND edge_thickness_dialog_test $<TARGET_FILE:MikuDanceStudio>)
    add_executable(app_runtime_state_test tests/app_runtime_state_test.cpp)
    target_link_libraries(app_runtime_state_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME app_runtime_state COMMAND app_runtime_state_test)
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
        src/model/model_keyframe_edit.cpp src/window/frame_line_edit.cpp
        src/window/ui_frame_step.cpp)
    target_compile_options(undo_frame_test PRIVATE /Gy)
    target_link_options(undo_frame_test PRIVATE /OPT:REF)
    target_link_libraries(undo_frame_test PRIVATE user32)
    add_executable(physics_scene_dispose_test tests/physics_scene_dispose_test.cpp
        src/physics/scene_dispose.cpp)
    target_compile_definitions(physics_scene_dispose_test PRIVATE MIKUDANCESTUDIO_DIAG=1)
    target_link_libraries(physics_scene_dispose_test PRIVATE mikudancestudio_physics)
    add_executable(bullet_diagnostics_fixture tests/bullet_diagnostics_fixture.cpp)
    target_link_libraries(bullet_diagnostics_fixture PRIVATE BULLET275::BULLET275)
    add_test(NAME bullet_diagnostics COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_bullet_diagnostics.py
        $<TARGET_FILE:bullet_diagnostics_fixture>)
    set_tests_properties(bullet_diagnostics PROPERTIES TIMEOUT 60)
    add_executable(physics_gizmo_reset_test tests/physics_gizmo_reset_test.cpp)
    target_include_directories(physics_gizmo_reset_test PRIVATE include)
    target_link_libraries(physics_gizmo_reset_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME physics_gizmo_reset_gpu COMMAND physics_gizmo_reset_test)
    set_tests_properties(physics_gizmo_reset_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
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
    add_executable(mme_technique_validation_test tests/mme_technique_validation_test.cpp)
    target_include_directories(mme_technique_validation_test PRIVATE
        third_party/mmeffect/include third_party/mmeffect/src/mmhack
        third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_technique_validation_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_technique_validation COMMAND mme_technique_validation_test)
    add_test(NAME scene_comparison_inputs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_freeze_scene_compare.py)
    add_test(NAME gui_export_comparison COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compare_gui_exports.py)
    add_executable(mme_emm_save_test tests/mme_emm_save_test.cpp)
    target_include_directories(mme_emm_save_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_emm_save_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_emm_save COMMAND mme_emm_save_test)
    add_executable(background_hotkeys_test tests/background_hotkeys_test.cpp)
    target_link_libraries(background_hotkeys_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME background_hotkeys COMMAND background_hotkeys_test)
    add_executable(mme_buffer_pass_test tests/mme_buffer_pass_test.cpp)
    target_include_directories(mme_buffer_pass_test PRIVATE third_party/mmeffect/src/mmeffect)
    target_link_libraries(mme_buffer_pass_test PRIVATE mikudancestudio_core MMEffect dsound)
    add_test(NAME mme_buffer_pass_gpu COMMAND mme_buffer_pass_test)
    set_tests_properties(mme_buffer_pass_gpu PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(mme_pass_readback_test tests/mme_pass_readback_test.cpp)
    target_link_libraries(mme_pass_readback_test PRIVATE d3d9 user32)
    add_test(NAME mme_pass_readback_gpu COMMAND mme_pass_readback_test)
    set_tests_properties(mme_pass_readback_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
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
    # These tests do not create windows, devices or modal dialogs. Safe while
    # the interactive desktop is in use: ctest -L cpu_no_windows.
    set_tests_properties(bullet_allocation mme_technique_validation scene_comparison_inputs
        gui_export_comparison mme_effect_mapping mme_emm_save mme_initialization_test
        mme_host_object_cache embedded_mme_resources PROPERTIES LABELS cpu_no_windows)
    if (TEST mmd_math)
        set_tests_properties(mmd_math PROPERTIES LABELS cpu_no_windows)
    endif()
endif()
