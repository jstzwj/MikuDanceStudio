cmake_minimum_required(VERSION 3.16)
project(MikuDanceStudioResourceTests LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(MDS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_test(NAME cpu_live_evidence COMMAND ${Python3_EXECUTABLE} "${MDS_SOURCE}/tests/test_cpu_live_evidence.py")
add_library(mds_rhi_resources
    "${MDS_SOURCE}/src/render/rhi_null.cpp"
    "${MDS_SOURCE}/src/render/rhi_render_pass.cpp"
    "${MDS_SOURCE}/src/render/rhi_image.cpp"
    "${MDS_SOURCE}/src/render/rhi_assets.cpp"
    "${MDS_SOURCE}/src/render/rhi_material_textures.cpp"
    "${MDS_SOURCE}/src/render/rhi_shader_inputs.cpp"
    "${MDS_SOURCE}/src/render/rhi_model_replay.cpp"
    "${MDS_SOURCE}/src/render/rhi_scene_renderer.cpp"
    "${MDS_SOURCE}/src/render/rhi_backend.cpp"
    "${MDS_SOURCE}/src/render/rhi_scene_preview.cpp"
    "${MDS_SOURCE}/src/render/model_geometry.cpp"
    "${MDS_SOURCE}/src/render/model_inputs.cpp")
target_sources(mds_rhi_resources PRIVATE "${MDS_SOURCE}/src/render/scene_passes.cpp" "${MDS_SOURCE}/src/render/scene_frame.cpp" "${MDS_SOURCE}/src/render/rhi_texture_mips.cpp")
target_compile_features(mds_rhi_resources PUBLIC cxx_std_17)
target_include_directories(mds_rhi_resources PUBLIC "${MDS_SOURCE}/include"
    PRIVATE "${MDS_SOURCE}/third_party/stb")
foreach(test IN ITEMS null render_pass image assets material_textures texture_preparation history model_replay scene_renderer model_geometry model_inputs scene_passes scene_frame)
    add_executable(rhi_${test}_test "${MDS_SOURCE}/tests/rhi_${test}_test.cpp")
    target_link_libraries(rhi_${test}_test PRIVATE mds_rhi_resources)
endforeach()
add_test(NAME rhi_null COMMAND rhi_null_test)
add_test(NAME rhi_render_pass COMMAND rhi_render_pass_test)
add_test(NAME rhi_image COMMAND rhi_image_test "${MDS_SOURCE}/res/assets/hud_sprites.png")
add_test(NAME rhi_assets COMMAND rhi_assets_test "${MDS_SOURCE}/res/assets")
add_test(NAME rhi_material_textures COMMAND rhi_material_textures_test)
add_test(NAME rhi_texture_preparation COMMAND rhi_texture_preparation_test)
add_test(NAME rhi_history COMMAND rhi_history_test)
add_test(NAME rhi_model_replay COMMAND rhi_model_replay_test)
add_test(NAME rhi_scene_renderer COMMAND rhi_scene_renderer_test)
add_test(NAME rhi_model_geometry COMMAND rhi_model_geometry_test)
add_test(NAME rhi_model_inputs COMMAND rhi_model_inputs_test)
add_test(NAME rhi_scene_passes COMMAND rhi_scene_passes_test)
add_test(NAME rhi_scene_frame COMMAND rhi_scene_frame_test)
