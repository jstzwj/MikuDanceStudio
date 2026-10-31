cmake_minimum_required(VERSION 3.21)
project(bullet275 LANGUAGES CXX)

# Keep the upstream source tree, including its historical build files, intact.
# MikuDanceStudio uses these three rigid-body libraries; SoftBody is unused.
foreach(component LinearMath BulletCollision BulletDynamics)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/bullet-src/src/${component}/*.cpp")
    add_library(${component} STATIC ${sources})
    target_compile_features(${component} PUBLIC cxx_std_17)
    target_include_directories(${component} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bullet-src/src")
    set_target_properties(${component} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${component}")
    if(MSVC)
        target_compile_options(${component} PUBLIC
            /fp:precise /Zc:alignedNew)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            # VC9's default scalar evaluation model; retain float btScalar.
            target_compile_options(${component} PUBLIC /arch:IA32)
        endif()
        if(BULLET275_ASAN)
            target_compile_options(${component} PRIVATE /fsanitize=address)
        endif()
    else()
        # Bullet exposes arithmetic in headers: consumers need this too.
        target_compile_options(${component} PUBLIC -ffp-contract=off)
    endif()
endforeach()

target_link_libraries(BulletCollision PUBLIC LinearMath)
target_link_libraries(BulletDynamics PUBLIC BulletCollision)

# Same public dependency boundary as the Conan package, for offline builds.
add_library(bullet275 INTERFACE)
target_link_libraries(bullet275 INTERFACE BulletDynamics BulletCollision LinearMath)
add_library(BULLET275::BULLET275 ALIAS bullet275)
