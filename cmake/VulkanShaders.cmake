find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(MDS_VULKAN_INCLUDE vulkan/vulkan.h HINTS "$ENV{VULKAN_SDK}/Include" REQUIRED)
find_program(MDS_DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
find_program(MDS_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(MDS_SPIRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv")
file(MAKE_DIRECTORY "${MDS_SPIRV_DIR}")
set(MDS_SPIRV_MODULES)
set(MDS_SPIRV_FILES)
function(mds_spirv name source entry profile color)
    if(profile MATCHES "^vs")
        set(buffer_shift 0)
    else()
        set(buffer_shift 1)
    endif()
    set(output "${MDS_SPIRV_DIR}/${name}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${MDS_DXC}" -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout
            -fvk-stage-io-order=decl -fvk-b-shift ${buffer_shift} 0 -fvk-t-shift 4 0 -fvk-s-shift 8 0
            -D MDS_MODEL_VERTEX_COLOR=${color} -D MDS_MODEL_DX9_COMPATIBILITY=0
            -T ${profile} -E ${entry} -Ges -Gis -Fo "${output}" "${PROJECT_SOURCE_DIR}/${source}"
        COMMAND "${MDS_SPIRV_VAL}" --target-env vulkan1.3 --relax-block-layout "${output}"
        DEPENDS "${PROJECT_SOURCE_DIR}/${source}" VERBATIM)
    set(MDS_SPIRV_MODULES ${MDS_SPIRV_MODULES} "${name}=${output}" PARENT_SCOPE)
    set(MDS_SPIRV_FILES ${MDS_SPIRV_FILES} "${output}" PARENT_SCOPE)
endfunction()
mds_spirv(model_vertex src/render/shaders/model_material_vertex.hlsl main vs_6_0 0)
mds_spirv(color_vertex src/render/shaders/model_material_vertex.hlsl main vs_6_0 1)
mds_spirv(model_pixel src/render/shaders/model_material_pixel.hlsl main ps_6_0 0)
mds_spirv(shadow_vertex src/render/shaders/builtin_shadow.hlsl shadow_vs vs_6_0 0)
mds_spirv(outline_vertex src/render/shaders/builtin_shadow.hlsl shadow_outline_vs vs_6_0 1)
mds_spirv(shadow_pixel src/render/shaders/builtin_shadow.hlsl shadow_ps ps_6_0 0)
set(MDS_SPIRV_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rhi_vulkan_spirv.hpp")
add_custom_command(OUTPUT "${MDS_SPIRV_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_spirv.py"
        --output "${MDS_SPIRV_HEADER}" ${MDS_SPIRV_MODULES}
    DEPENDS ${MDS_SPIRV_FILES} "${PROJECT_SOURCE_DIR}/tools/embed_spirv.py" VERBATIM)
target_sources(mikudancestudio_rhi PRIVATE src/render/rhi_vulkan.cpp "${MDS_SPIRV_HEADER}")
target_include_directories(mikudancestudio_rhi PRIVATE "${MDS_VULKAN_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(mikudancestudio_rhi PUBLIC MIKUDANCESTUDIO_RHI_VULKAN=1)
target_link_libraries(mikudancestudio_rhi PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(mikudancestudio_rhi PRIVATE user32)
endif()
