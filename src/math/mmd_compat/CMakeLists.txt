# Before CMake 4.0's CMP0184, /RTC flags live in CMAKE_CXX_FLAGS_<CONFIG>.
# Remove them only in this subdirectory; other application sources keep their Debug
# checks. Do not edit the cache or use /RTC-, which MSVC does not recognize.
get_cmake_property(_mmd_math_variables VARIABLES)
foreach(_variable IN LISTS _mmd_math_variables)
    if(_variable MATCHES "^CMAKE_CXX_FLAGS(_[A-Z0-9_]+)?$")
        string(REGEX REPLACE "(^|[ \t])[-/]RTC[^ \t]*" "\\1"
            ${_variable} "${${_variable}}")
    endif()
endforeach()

add_library(mikudancestudio_math OBJECT mmd_math.cpp)
set_target_properties(mikudancestudio_math PROPERTIES
    MSVC_RUNTIME_CHECKS ""
    INTERPROCEDURAL_OPTIMIZATION FALSE)

# These options preserve MMD's VC9 x87 arithmetic, including intrinsic math,
# extended intermediates and noinline rounding boundaries, in every config.
# /Ob2 explicitly overrides Debug's /Ob0; /O2 alone does not override it.
# Keep the selected Debug/Release CRT and debug information settings.
target_compile_options(mikudancestudio_math PRIVATE
    /arch:IA32 /fp:precise /O2 /Ob2 /Oi /GL-)

target_compile_features(mikudancestudio_math PUBLIC cxx_std_17)
target_include_directories(mikudancestudio_math PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(mikudancestudio_math PUBLIC BULLET275::BULLET275)
