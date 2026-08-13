cmake_minimum_required(VERSION 3.21)

project(libxtm VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Strict warnings by default for the top-level project; consumers embedding via
# add_subdirectory opt in explicitly.
option(XTM_WERROR "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})

# ccache integration (zero-config: silently skipped if not installed)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# clang toolchain: point at the real GCC install dir for libstdc++ headers/libs
# (clang may pick an empty newer GCC dir when several versions are installed).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    find_program(GXX_CXX NAMES g++)
    if(GXX_CXX)
        execute_process(
            COMMAND ${GXX_CXX} -print-file-name=libstdc++.so
            OUTPUT_VARIABLE GCC_LIBSTDCXX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GCC_LIBSTDCXX AND NOT GCC_LIBSTDCXX MATCHES "^libstdc")
            get_filename_component(GCC_LIBSTDCXX_DIR "${GCC_LIBSTDCXX}" DIRECTORY)
            message(STATUS "clang: using GCC install dir ${GCC_LIBSTDCXX_DIR} for libstdc++")
            add_compile_options(--gcc-install-dir=${GCC_LIBSTDCXX_DIR})
            add_link_options(-L${GCC_LIBSTDCXX_DIR})
        endif()
    endif()
endif()

# Enable Interprocedural Optimization (LTO) only for Release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT output)
if(ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release"
   AND NOT (APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
   AND NOT (MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Sanitizers
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# Enable common warnings and build-type-specific optimizations
if(MSVC)
    add_compile_options(/W4)
    if(XTM_WERROR)
        add_compile_options(/WX)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Ob2 /Oi /Ot /Oy)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(XTM_WERROR)
        add_compile_options(-Werror)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            add_compile_options(-O3 -ftree-vectorize)
        else()
            add_compile_options(-O3 -march=native -mtune=native -ftree-vectorize)
        endif()
    endif()
endif()

# Consolidate output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Symlink compile_commands.json to project root for IDE/clangd compatibility
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# Dependencies
find_package(GDAL 3.4 REQUIRED)
find_package(Threads REQUIRED)

# Library type option (controlled by BUILD_SHARED_LIBS)
option(BUILD_SHARED_LIBS "Build shared library instead of static" OFF)

# Collect sources automatically
file(GLOB_RECURSE XTM_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(xtm_core ${XTM_CORE_SOURCES})

# Library versioning (no-op for static builds)
set_target_properties(xtm_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(xtm_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(xtm_core PUBLIC GDAL::GDAL)

# Embed version info into compiled code
target_compile_definitions(xtm_core PUBLIC
    XTM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    XTM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    XTM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    XTM_VERSION="${PROJECT_VERSION}"
)

# Apps
add_executable(xtm
    apps/xtm/main.cpp
    apps/xtm/AnalyzeCmd.cpp
    apps/xtm/EncodeCmd.cpp
    apps/xtm/DecodeCmd.cpp
    apps/xtm/InfoCmd.cpp
    apps/xtm/VerifyCmd.cpp
)
target_link_libraries(xtm PRIVATE xtm_core Threads::Threads)

# Python bindings (nanobind) — opt-in: -DXTM_BUILD_PYTHON=ON.
# The module lands in ${CMAKE_BINARY_DIR}/lib; use it via
# PYTHONPATH=<build>/lib python -c "import xtm".
option(XTM_BUILD_PYTHON "Build Python bindings (nanobind)" OFF)

if(XTM_BUILD_PYTHON)
    find_package(Python 3.10 COMPONENTS Interpreter Development.Module Development.SABIModule REQUIRED)

    # Prefer an already-installed nanobind; fall back to FetchContent.
    find_package(nanobind QUIET CONFIG)
    if(NOT nanobind_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            nanobind
            GIT_REPOSITORY https://github.com/wjakob/nanobind.git
            GIT_TAG v2.14.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(nanobind)
    endif()

    nanobind_add_module(xtm_python STABLE_ABI NB_SUPPRESS_WARNINGS bindings/xtm.cpp)
    target_link_libraries(xtm_python PRIVATE xtm_core)
    set_target_properties(xtm_python PROPERTIES
        OUTPUT_NAME xtm
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    if(DEFINED SKBUILD)
        # scikit-build-core: flat install at the wheel root so `import xtm` works
        install(TARGETS xtm_python LIBRARY DESTINATION .)
    else()
        install(TARGETS xtm_python LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()

# Tests (default OFF when embedded via add_subdirectory)
if(NOT PROJECT_IS_TOP_LEVEL AND NOT BUILD_TESTING)
    set(BUILD_TESTING OFF CACHE BOOL "Build the testing tree." FORCE)
endif()
include(CTest)

if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found locally; fetching from GitHub...")
        include(FetchContent)
        # Keep googletest out of our install tree
        set(INSTALL_GTEST OFF CACHE BOOL "Enable installation of googletest." FORCE)
        set(INSTALL_GMOCK OFF CACHE BOOL "Enable installation of gmock." FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.18.0
            GIT_SHALLOW TRUE
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    add_subdirectory(tests)
endif()

# Install Rules

install(TARGETS xtm_core
    EXPORT libxtmTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS xtm
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/xtm/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xtm
)

# Package config so consumers can find_package(libxtm)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libxtmConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    cmake/libxtmConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/libxtmConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxtm
)
install(EXPORT libxtmTargets
    FILE libxtmTargets.cmake
    NAMESPACE libxtm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxtm
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libxtmConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libxtmConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libxtm
)

# Packaging (cpack -G TGZ, cpack -G DEB, ...)
set(CPACK_PACKAGE_NAME "libxtm")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "libxtm project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lossless terrain elevation compression library")
set(CPACK_PACKAGE_CONTACT "Yashwanth <199648454+yashwk@users.noreply.github.com>")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgdal32 | libgdal34 | libgdal35")
include(CPack)