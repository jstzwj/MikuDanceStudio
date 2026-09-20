# Normal, non-delay-loaded imports from the architecture's original runtime.
# LIB /DEF consumes the compiler's symbol decoration without linking the
# signature bodies into any product. The resulting .lib contains imports only.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(D3DX_RUNTIME_NAME d3dx9_43)
    set(D3DX_IMPORT_MACHINE X64)
else()
    set(D3DX_RUNTIME_NAME d3dx9_32)
    set(D3DX_IMPORT_MACHINE X86)
endif()
set(D3DX_IMPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/d3dx_imports")
set(D3DX_IMPORT_LIB "${D3DX_IMPORT_DIR}/${D3DX_RUNTIME_NAME}.lib")
add_custom_command(
    OUTPUT "${D3DX_IMPORT_DIR}/d3dx_signatures.cpp" "${D3DX_IMPORT_DIR}/d3dx_imports.def"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_d3dx_imports.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include/d3dx9.h"
        "${D3DX_IMPORT_DIR}" "${D3DX_RUNTIME_NAME}"
    DEPENDS scripts/gen_d3dx_imports.py third_party/mmeffect/include/d3dx9.h
    VERBATIM)
add_library(d3dx_import_signatures OBJECT "${D3DX_IMPORT_DIR}/d3dx_signatures.cpp")
target_compile_options(d3dx_import_signatures PRIVATE /Zl)
add_custom_command(
    OUTPUT "${D3DX_IMPORT_LIB}"
    COMMAND "${CMAKE_AR}" /nologo "/MACHINE:${D3DX_IMPORT_MACHINE}"
        "/DEF:${D3DX_IMPORT_DIR}/d3dx_imports.def" "/OUT:${D3DX_IMPORT_LIB}"
        $<TARGET_OBJECTS:d3dx_import_signatures>
    DEPENDS d3dx_import_signatures $<TARGET_OBJECTS:d3dx_import_signatures>
        "${D3DX_IMPORT_DIR}/d3dx_imports.def"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(d3dx_import_library DEPENDS "${D3DX_IMPORT_LIB}")
add_library(D3DXRuntime STATIC IMPORTED GLOBAL)
set_target_properties(D3DXRuntime PROPERTIES IMPORTED_LOCATION "${D3DX_IMPORT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mmeffect/include")
add_dependencies(D3DXRuntime d3dx_import_library)
