# Black (original Xbox), statically recompiled, as an iOS app. The x86 code is recompiled on the build machine first
# (same recompiler as the Android/Windows builds): this build takes Black-Android/generated/game as it is, with the
# SDK's portable runtime, the game's sources in src, the shared SDL3 host in native/ and the iOS pieces in ios/.
#
# cmake -S ios -B build-ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
#       -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DSDL3_DIR=<prefix>/lib/cmake/SDL3 \
#       -DMOLTENVK_INCLUDE_DIR=<...> -DMOLTENVK_LIBRARY=<...>/libMoltenVK.a -DCMAKE_BUILD_TYPE=Release

cmake_minimum_required(VERSION 3.22)
project(black_ios LANGUAGES C CXX OBJC OBJCXX)

set(MOLTENVK_INCLUDE_DIR "" CACHE PATH "MoltenVK's include folder (vulkan headers)")
set(MOLTENVK_LIBRARY "" CACHE FILEPATH "MoltenVK's static library for iOS")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "iOS version")

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
set(IOS ${CMAKE_CURRENT_SOURCE_DIR})
set(NATIVE ${ROOT}/Black-Android/native)
set(ANDROID_REPO ${ROOT}/Black-Android)
set(GAME ${ANDROID_REPO}/src)
set(SDK ${ROOT}/XboxXBE-Recomp)
set(THIRD ${ROOT}/third_party)

# the recompiled code and the xbe it came from
set(GEN ${ANDROID_REPO}/generated/game)
set(XBE ${ANDROID_REPO}/assets/default.xbe)
if(NOT EXISTS ${GEN}/x86_func_mapping.cpp OR NOT EXISTS ${XBE})
    message(FATAL_ERROR "No recompiled code: the CI recompiler makes Black-Android/generated/game from default.xbe")
endif()
file(STRINGS ${ANDROID_REPO}/config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
math(EXPR last "${GAME_PARTITIONS} - 1")
set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
foreach(i RANGE ${last})
    list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
endforeach()
file(SHA1 ${XBE} xbe_sha1)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/image_sha1.cpp
     CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
# the version and the commit the app was built from, so a log says exactly which build ran
set(BUILD_COMMIT "ios-0.1.0")
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${ROOT}
                    OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
    if(NOT failed AND head)
        set(BUILD_COMMIT "ios-0.1.0+${head}")
    endif()
endif()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/build_info.h CONTENT "#define BLACK_COMMIT \"${BUILD_COMMIT}\"\n")

# src/common/europe.patch as a byte array: Mach-O has no .incbin
file(READ ${GAME}/common/europe.patch EUROPE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," EUROPE_BYTES "${EUROPE_HEX}")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/europe_patch.cpp
     CONTENT "extern const unsigned char kEuropePatch[] = {${EUROPE_BYTES}};\nextern const unsigned long kEuropePatchSize = sizeof(kEuropePatch);\n")

# The CI installs SDL3 in sdl-prefix at the repository root. Cross-compiling, find_package searches the SDK's root
# path only unless told otherwise, so the mode is widened and the prefix added here as well as through the command line.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
if(EXISTS ${ROOT}/sdl-prefix/lib/cmake/SDL3)
    list(APPEND CMAKE_PREFIX_PATH ${ROOT}/sdl-prefix)
endif()

find_package(SDL3 CONFIG REQUIRED)

# glslang with its HLSL front end, and toml++, built for iOS here
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD}/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang EXCLUDE_FROM_ALL)
add_subdirectory(${THIRD}/tomlplusplus ${CMAKE_CURRENT_BINARY_DIR}/tomlplusplus EXCLUDE_FROM_ALL)
set(IMGUI_SOURCES ${THIRD}/imgui/imgui.cpp ${THIRD}/imgui/imgui_draw.cpp ${THIRD}/imgui/imgui_tables.cpp
    ${THIRD}/imgui/imgui_widgets.cpp ${THIRD}/imgui/backends/imgui_impl_vulkan.cpp)

# the SDK's runtime without its Windows parts and those the game replaces, as the other builds
file(GLOB RUNTIME_SOURCES ${SDK}/runtime/*/*.cpp)
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX
     "/runtime/app/|/core/input\\.cpp$|/gpu/(d3d12|render_thread)\\.cpp$|/audio/xaudio2\\.cpp$|/xdk/(dsound|d3d8)\\.cpp$")
file(GLOB GAME_HOST_SOURCES CONFIGURE_DEPENDS ${GAME}/*/*.cpp)
# the SDL3 host is shared with Android; game_files and update are the Android pieces, replaced in ios/
file(GLOB NATIVE_SOURCES ${NATIVE}/*.cpp)
list(REMOVE_ITEM NATIVE_SOURCES ${NATIVE}/game_files.cpp ${NATIVE}/update.cpp)
file(GLOB IOS_SOURCES CONFIGURE_DEPENDS ${IOS}/*.cpp ${IOS}/*.mm)

add_executable(black MACOSX_BUNDLE ${RUNTIME_SOURCES} ${GAME_HOST_SOURCES} ${NATIVE_SOURCES} ${IOS_SOURCES}
               ${GEN_SOURCES} ${IMGUI_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/image_sha1.cpp
               ${CMAKE_CURRENT_BINARY_DIR}/europe_patch.cpp)
target_include_directories(black PRIVATE ${NATIVE}/compat ${SDK}/harness/compat ${NATIVE} ${IOS} ${GAME} ${SDK}/runtime ${GEN}
                           ${CMAKE_CURRENT_BINARY_DIR} ${THIRD}/glslang ${MOLTENVK_INCLUDE_DIR})
target_include_directories(black SYSTEM PRIVATE ${THIRD}/imgui)
target_compile_options(black PRIVATE -fno-strict-aliasing -fsigned-char)
set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_DEFINITIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
set_source_files_properties(${NATIVE}/vk_shader.cpp PROPERTIES COMPILE_DEFINITIONS ENABLE_HLSL)
# -O3 on the recompiled code: the x86 is the hot path everywhere. The vectorization cost model is GCC's; AppleClang
# does not know the flag and vectorizes at -O3 on its own
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GEN_OPTIONS "-O3;-fvect-cost-model=very-cheap;-w;-fno-strict-aliasing;-fsigned-char")
else()
    set(GEN_OPTIONS "-O3;-w;-fno-strict-aliasing;-fsigned-char")
endif()
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "${GEN_OPTIONS}")
set_source_files_properties(${GAME}/common/europe.patch PROPERTIES HEADER_FILE_ONLY TRUE)

set_target_properties(black PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${IOS}/Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME "Black"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gerisson.black"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")

# MoltenVK 1.4.2 weak-links MTLResidencySetDescriptor, which only the iOS 18 SDK's Metal exports; on older systems
# the class stays null and MoltenVK checks it. Let the linker keep it undefined instead of failing on the iOS 17 SDK.
target_link_options(black PRIVATE "-Wl,-U,_OBJC_CLASS_$_MTLResidencySetDescriptor")

target_link_libraries(black PRIVATE
    SDL3::SDL3
    ${MOLTENVK_LIBRARY}
    tomlplusplus::tomlplusplus glslang glslang-default-resource-limits
    z
    "-framework UIKit" "-framework Foundation" "-framework Metal" "-framework QuartzCore"
    "-framework CoreGraphics" "-framework CoreMotion" "-framework CoreAudio" "-framework AudioToolbox"
    "-framework AVFoundation" "-framework GameController" "-framework CoreHaptics"
    "-framework IOSurface")
