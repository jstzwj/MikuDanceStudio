cmake_minimum_required(VERSION 3.21)
project(MikuDanceStudioResourceTests LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(MDS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Consume the production source list, shader generation and usage requirements.
# An independent list had silently omitted newer shader/attachment contracts.
set(MIKUDANCESTUDIO_FORMATS_ONLY ON)
set(MIKUDANCESTUDIO_RHI_D3D9 OFF)
set(MIKUDANCESTUDIO_RHI_D3D11 OFF)
set(MIKUDANCESTUDIO_RHI_D3D12 OFF)
set(MIKUDANCESTUDIO_RHI_VULKAN OFF)
add_subdirectory("${MDS_SOURCE}" "portable")
add_test(NAME cpu_live_evidence COMMAND ${Python3_EXECUTABLE}
    "${MDS_SOURCE}/tests/test_cpu_live_evidence.py")
set_tests_properties(cpu_live_evidence PROPERTIES LABELS "headless;portable;evidence")
