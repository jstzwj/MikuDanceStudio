cmake_minimum_required(VERSION 3.21)
project(MikuDanceStudioCpuSimulationTests LANGUAGES CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(MDS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Use exactly the external build definition exported by Conan. Never enter or
# patch the upstream tree's historical CMake projects.
add_subdirectory("${MDS_SOURCE}/recipes/bullet275" "bullet")
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory("${MDS_SOURCE}/src/math/mmd_compat" "mmd-math")
endif()
add_library(mds_cpu_simulation
    "${MDS_SOURCE}/src/physics/physics_world_state.cpp"
    "${MDS_SOURCE}/src/physics/cpu_scene_simulation.cpp")
target_compile_features(mds_cpu_simulation PUBLIC cxx_std_17)
target_include_directories(mds_cpu_simulation PUBLIC "${MDS_SOURCE}/include")
target_link_libraries(mds_cpu_simulation PUBLIC BULLET275::BULLET275)
if(TARGET mikudancestudio_math)
    target_link_libraries(mds_cpu_simulation PRIVATE mikudancestudio_math)
endif()

if(BUILD_TESTING)
    foreach(test IN ITEMS physics_world_state cpu_scene_simulation)
        add_executable(${test}_test "${MDS_SOURCE}/tests/${test}_test.cpp")
        target_link_libraries(${test}_test PRIVATE mds_cpu_simulation)
        add_test(NAME ${test} COMMAND ${test}_test)
        set_tests_properties(${test} PROPERTIES LABELS "headless;portable;physics")
    endforeach()
    foreach(test IN ITEMS bullet_allocation bullet_simulation)
        add_executable(${test}_test "${MDS_SOURCE}/tests/${test}_test.cpp")
        target_link_libraries(${test}_test PRIVATE BULLET275::BULLET275)
        add_test(NAME ${test} COMMAND ${test}_test)
        set_tests_properties(${test} PROPERTIES LABELS "headless;portable;physics")
    endforeach()
    add_test(NAME bullet_upstream_sources COMMAND ${Python3_EXECUTABLE}
        "${MDS_SOURCE}/recipes/bullet275/verify_sources.py")
    set_tests_properties(bullet_upstream_sources PROPERTIES LABELS "headless;portable;physics")
endif()
