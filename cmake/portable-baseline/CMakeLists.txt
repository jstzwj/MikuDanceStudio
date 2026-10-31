cmake_minimum_required(VERSION 3.21)
project(MikuDanceStudioPortableBaseline LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(MDS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# The two existing entry points now consume production source definitions.
add_subdirectory("${MDS_SOURCE}/cmake/rhi-resource-tests" "resources")
add_subdirectory("${MDS_SOURCE}/cmake/cpu-simulation-tests" "cpu")

add_executable(mds_portable_baseline "${MDS_SOURCE}/tests/portable_baseline.cpp")
target_compile_features(mds_portable_baseline PRIVATE cxx_std_17)
target_include_directories(mds_portable_baseline PRIVATE "${MDS_SOURCE}/tests")
target_link_libraries(mds_portable_baseline PRIVATE mikudancestudio_formats)
if(MSVC)
    target_compile_options(mds_portable_baseline PRIVATE /fp:precise)
else()
    target_compile_options(mds_portable_baseline PRIVATE -ffp-contract=off)
endif()
add_test(NAME portable_baseline_contract COMMAND ${Python3_EXECUTABLE}
    "${MDS_SOURCE}/tools/check_portable_baseline.py"
    --program $<TARGET_FILE:mds_portable_baseline>
    --output "${CMAKE_CURRENT_BINARY_DIR}/baseline")
set_tests_properties(portable_baseline_contract PROPERTIES LABELS "headless;portable;baseline")

option(MDS_P0_VULKAN_PROBE "Build the independent no-window Vulkan capability probe" OFF)
if(MDS_P0_VULKAN_PROBE)
    find_path(MDS_P0_VULKAN_INCLUDE vulkan/vulkan.h
        HINTS "$ENV{VULKAN_SDK}/Include" REQUIRED)
    add_executable(mds_platform_probe "${MDS_SOURCE}/tools/platform_probe.cpp")
    target_compile_features(mds_platform_probe PRIVATE cxx_std_17)
    target_include_directories(mds_platform_probe PRIVATE "${MDS_P0_VULKAN_INCLUDE}")
    target_link_libraries(mds_platform_probe PRIVATE ${CMAKE_DL_LIBS})
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/environment-$<CONFIG>.json" CONTENT
"{\n  \"schema\": 1,\n  \"system\": \"${CMAKE_SYSTEM_NAME}\",\n  \"processor\": \"${CMAKE_SYSTEM_PROCESSOR}\",\n  \"pointer_bytes\": ${CMAKE_SIZEOF_VOID_P},\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n  \"compiler_version\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n  \"cmake_version\": \"${CMAKE_VERSION}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"cxx_standard\": 17,\n  \"numeric_policy\": \"separate-multiply-add\",\n  \"bullet_build\": \"recipes/bullet275/CMakeLists.txt\"\n}\n")
