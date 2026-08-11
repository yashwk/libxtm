cmake_minimum_required(VERSION 3.20)

# clang++ is the primary compiler; falls back to the platform default if absent
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CLANG_CXX NAMES clang++)
    if(CLANG_CXX)
        set(CMAKE_CXX_COMPILER ${CLANG_CXX})
    endif()
endif()

# clang toolchain: point at the real GCC install dir for libstdc++ headers/libs
# (clang may pick an empty newer GCC dir when several versions are installed).
# Must be set before project(): the compiler check inside project() links a
# test program and would fail without these flags.
if(CMAKE_CXX_COMPILER MATCHES "clang" OR CMAKE_CXX_COMPILER MATCHES "Clang")
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
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --gcc-install-dir=${GCC_LIBSTDCXX_DIR}" CACHE STRING "Flags used by the CXX compiler" FORCE)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${GCC_LIBSTDCXX_DIR}" CACHE STRING "Flags used by the linker" FORCE)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L${GCC_LIBSTDCXX_DIR}" CACHE STRING "Flags used by the linker" FORCE)
        endif()
    endif()
endif()

project(libxtm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

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
if(ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Sanitizers
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_MSAN "Enable Memory Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_MSAN)
    add_compile_options(-fsanitize=memory -fPIE -pie -fno-omit-frame-pointer)
    add_link_options(-fsanitize=memory -fPIE -pie)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# Enable common warnings and build-type-specific optimizations
if(MSVC)
    add_compile_options(/W4 /WX)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Ob2 /Oi /Ot /Oy)
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(/O2)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    # clang -Werror: googletest printer header trips -Wcharacter-conversion
    # (char8_t). Suppressed after the -Wall/-Wextra groups so it takes effect.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-character-conversion)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # Per-compiler SIMD/vectorization tuning. Both GCC and clang get the
        # native ISA explicitly; compiler-specific flags go in their branch.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-O3 -march=native -mtune=native -fvectorize)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            add_compile_options(-O3 -march=native -mtune=native -ftree-vectorize)
        else()
            add_compile_options(-O3 -march=native)
        endif()
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(-O2 -march=native -fno-omit-frame-pointer)
    endif()
endif()

# Consolidate output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Symlink compile_commands.json to project root for IDE/clangd compatibility
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# Dependencies
find_package(GDAL REQUIRED)
find_package(Threads REQUIRED)

# Library type option (controlled by BUILD_SHARED_LIBS)
option(BUILD_SHARED_LIBS "Build shared library instead of static" OFF)

# Collect sources automatically
file(GLOB_RECURSE XTM_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(xtm_core ${XTM_CORE_SOURCES})

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

# Tests
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found locally; fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.16.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()

# Install Rules
include(GNUInstallDirs)

install(TARGETS xtm_core xtm
    EXPORT libxtmTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/xtm/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xtm
)
