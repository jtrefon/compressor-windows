cmake_minimum_required(VERSION 3.21)
project(CompressorWindows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)

# --- Engine dependency: exact tag pin (see docs/PORTING.md in the engine) ---
set(COMPRESSIONLIB_TAG "v1.6.3" CACHE STRING
    "compressionlib engine tag to pin (exact tag, never a branch)")
include(FetchContent)
FetchContent_Declare(
    compressionlib
    GIT_REPOSITORY https://github.com/jtrefon/compressionlib.git
    GIT_TAG        ${COMPRESSIONLIB_TAG}
)
FetchContent_MakeAvailable(compressionlib)

# --- Qt 6 (set CMAKE_PREFIX_PATH to the Qt installation) ---
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_executable(CompressorWindows
    src/main.cpp
)
target_link_libraries(CompressorWindows PRIVATE
    Qt6::Widgets
    CompressionLib::compression
)

# --- Anti-corruption contract test (no Qt; runs headless in CI) ---
enable_testing()
add_executable(contract_test tests/contract_test.cpp)
target_link_libraries(contract_test PRIVATE CompressionLib::compression)
add_test(NAME contract_test COMMAND contract_test)

# --- Packaging ---
include(GNUInstallDirs)
install(TARGETS CompressorWindows
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
set(CPACK_PACKAGE_NAME "CompressorWindows")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
set(CPACK_STRIP_FILES TRUE)
include(CPack)