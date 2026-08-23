cmake_minimum_required(VERSION 3.21)
project(CompressorWindows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Engine dependency: exact tag pin (see docs/PORTING.md in the engine) ---
set(COMPRESSIONLIB_TAG "v1.6.4" CACHE STRING
    "compressionlib engine tag to pin (exact tag, never a branch)")
include(FetchContent)
FetchContent_Declare(
    compressionlib
    GIT_REPOSITORY https://github.com/jtrefon/compressionlib.git
    GIT_TAG        ${COMPRESSIONLIB_TAG}
)
FetchContent_MakeAvailable(compressionlib)

# --- Anti-corruption contract test (headless, runs in CI) ---
enable_testing()
add_executable(contract_test tests/contract_test.cpp)
target_link_libraries(contract_test PRIVATE CompressionLib::compression)
add_test(NAME contract_test COMMAND contract_test)

# --- WinUI 3 (C++/WinRT) app, built by MSBuild (XAML compilation uses the
#     Windows App SDK build targets; CMake drives it so the engine lib is
#     always built first and paths stay in one place) ---
find_program(VSWHERE_EXE vswhere
    PATHS "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
    REQUIRED)
execute_process(
    COMMAND ${VSWHERE_EXE} -latest -products * -requires Microsoft.Component.MSBuild
            -find "MSBuild/**/Bin/MSBuild.exe"
    OUTPUT_VARIABLE MSBUILD_EXE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT MSBUILD_EXE)
    message(FATAL_ERROR "MSBuild.exe not found via vswhere")
endif()

# Engine artifacts for the app target (multi-config VS generator layout).
set(ENGINE_INCLUDE_DIR ${compressionlib_SOURCE_DIR}/include)
set(ENGINE_LIB_DIR ${CMAKE_BINARY_DIR}/Release)

add_custom_target(winui_app
    COMMAND "${MSBUILD_EXE}" ${CMAKE_SOURCE_DIR}/app/CompressorWindows.vcxproj
            /restore
            /p:Configuration=Release /p:Platform=x64
            /p:EngineIncludeDir=${ENGINE_INCLUDE_DIR}
            /p:EngineLibDir=${ENGINE_LIB_DIR}
            /m
    COMMENT "Building WinUI 3 app (CompressorWindows)"
    VERBATIM)
add_dependencies(winui_app compression)