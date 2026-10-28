# Black (original Xbox), statically recompiled, as a Switch homebrew (NRO). Black-Android's game sources and the
# XboxXBE-Recomp SDK's portable runtime, with this folder as the host in place of Android's native/. The x86 code is
# recompiled on the PC first (tools/recompile.ps1): this build takes Black-Android/generated/game as it is.
#
# cmake -S switch -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release

cmake_minimum_required(VERSION 3.22)
project(black_switch LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
set(HOST ${CMAKE_CURRENT_SOURCE_DIR})
set(ANDROID_REPO ${ROOT}/Black-Android)
set(GAME ${ANDROID_REPO}/src)
set(SDK ${ROOT}/XboxXBE-Recomp)
set(THIRD ${ROOT}/third_party)
set(NVK_SDK ${ROOT}/nvk_sdk CACHE PATH "mesa-switch install (NVK)")

# the recompiled code and the xbe it came from
set(GEN ${ANDROID_REPO}/generated/game)
set(XBE ${ANDROID_REPO}/assets/default.xbe)
if(NOT EXISTS ${GEN}/x86_func_mapping.cpp OR NOT EXISTS ${XBE})
    message(FATAL_ERROR "No recompiled code: run tools/recompile.ps1, it writes Black-Android/generated/game")
endif()
file(STRINGS ${ANDROID_REPO}/config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
math(EXPR last "${GAME_PARTITIONS} - 1")
set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
foreach(i RANGE ${last})
    list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
endforeach()
file(SHA1 ${XBE} xbe_sha1)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/image_sha1.cpp CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/build_info.h CONTENT "#define BLACK_COMMIT \"switch-0.2.1-gyro\"\n")

# glslang with its HLSL front end (the SDK's shaders are HLSL), built here for the Switch
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# its precompiled header is named by an MSYS path (/c/...) the Windows-hosted GCC cannot open
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_subdirectory(${THIRD}/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang EXCLUDE_FROM_ALL)
add_subdirectory(${THIRD}/tomlplusplus ${CMAKE_CURRENT_BINARY_DIR}/tomlplusplus EXCLUDE_FROM_ALL)
set(IMGUI_SOURCES ${THIRD}/imgui/imgui.cpp ${THIRD}/imgui/imgui_draw.cpp ${THIRD}/imgui/imgui_tables.cpp
    ${THIRD}/imgui/imgui_widgets.cpp ${THIRD}/imgui/backends/imgui_impl_vulkan.cpp)

# the SDK's runtime but its Windows parts, and those the game replaces (src/sound, src/d3d) -- as Black-Android
file(GLOB RUNTIME_SOURCES ${SDK}/runtime/*/*.cpp)
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX
     "/runtime/app/|/core/input\\.cpp$|/gpu/(d3d12|render_thread)\\.cpp$|/audio/xaudio2\\.cpp$|/xdk/(dsound|d3d8)\\.cpp$")
file(GLOB GAME_HOST_SOURCES CONFIGURE_DEPENDS ${GAME}/*/*.cpp)
file(GLOB SWITCH_SOURCES CONFIGURE_DEPENDS ${HOST}/*.cpp)

add_executable(black ${RUNTIME_SOURCES} ${GAME_HOST_SOURCES} ${SWITCH_SOURCES} ${GEN_SOURCES} ${IMGUI_SOURCES}
               ${HOST}/switch_elf_stubs.c ${CMAKE_CURRENT_BINARY_DIR}/image_sha1.cpp)
target_include_directories(black PRIVATE ${HOST}/compat ${SDK}/harness/compat ${HOST} ${GAME} ${SDK}/runtime ${GEN}
                           ${CMAKE_CURRENT_BINARY_DIR} ${THIRD}/glslang ${NVK_SDK}/include)
target_include_directories(black SYSTEM PRIVATE ${THIRD}/imgui)
# x86 chars are signed, as the game's code and the runtime were written for
target_compile_options(black PRIVATE -fno-strict-aliasing -fsigned-char)
# ImGui's open-in-browser uses fork/exec, which the Switch has not
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_DEFINITIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
set(HOST_SOURCES ${RUNTIME_SOURCES} ${GAME_HOST_SOURCES} ${SWITCH_SOURCES})
set_source_files_properties(${HOST_SOURCES} PROPERTIES COMPILE_OPTIONS "-include;${HOST}/compat/crt.h")
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2;-w;-fno-strict-aliasing;-fsigned-char")
set_source_files_properties(${HOST}/vk_shader.cpp PROPERTIES COMPILE_DEFINITIONS ENABLE_HLSL)
set_source_files_properties(${HOST}/game_files.cpp PROPERTIES
    COMPILE_DEFINITIONS "EUROPE_PATCH=\"${GAME}/common/europe.patch\""
    OBJECT_DEPENDS ${GAME}/common/europe.patch)

# the host and runtime alone, to find their compile errors before the long build of the recompiled code
add_library(black_host_check OBJECT ${RUNTIME_SOURCES} ${GAME_HOST_SOURCES} ${SWITCH_SOURCES} ${IMGUI_SOURCES})
target_include_directories(black_host_check PRIVATE ${HOST}/compat ${SDK}/harness/compat ${HOST} ${GAME} ${SDK}/runtime
                           ${GEN} ${CMAKE_CURRENT_BINARY_DIR} ${THIRD}/glslang ${NVK_SDK}/include)
target_include_directories(black_host_check SYSTEM PRIVATE ${THIRD}/imgui)
target_compile_options(black_host_check PRIVATE -fno-strict-aliasing -fsigned-char)
target_link_libraries(black_host_check PRIVATE tomlplusplus::tomlplusplus)

# NVK, linked statically as the earlier xboxrecomp port does; its own (HLSL-less) glslang is left out
target_link_libraries(black PRIVATE
    tomlplusplus::tomlplusplus glslang glslang-default-resource-limits
    -Wl,--allow-multiple-definition
    -Wl,--start-group
    ${NVK_SDK}/lib/libvulkan.a
    ${NVK_SDK}/lib/liballoc-af82aa609fb0f811.rlib
    ${NVK_SDK}/lib/libcore-8c20f6407cb781b6.rlib
    ${NVK_SDK}/lib/libcompiler_builtins-389209a593b9fef9.rlib
    ${NVK_SDK}/lib/libpanic_abort-810dcfbec4f99ef7.rlib
    zstd expat z nx stdc++ m pthread
    -Wl,--end-group)

nx_generate_nacp(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/black.nacp
    NAME "Black Xbox Port" AUTHOR "RECOMP-GUTO" VERSION "0.2.0")
set(ICON ${HOST}/icon.jpg)
if(EXISTS ${ICON})
    nx_create_nro(black NACP ${CMAKE_CURRENT_BINARY_DIR}/black.nacp ICON ${ICON})
else()
    nx_create_nro(black NACP ${CMAKE_CURRENT_BINARY_DIR}/black.nacp)
endif()
