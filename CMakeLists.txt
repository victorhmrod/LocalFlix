cmake_minimum_required(VERSION 3.24)

project(LocalFlix
    VERSION 1.1.0
    DESCRIPTION "Offline media library and video player"
    LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "LocalFlix targets Windows 10/11 only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOCALFLIX_BUILD_TESTS "Build the automated test suite" ON)
option(LOCALFLIX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(LOCALFLIX_LIBMPV_DIR "${CMAKE_SOURCE_DIR}/third_party/libmpv" CACHE PATH
    "Directory containing libmpv-2.dll and include/mpv/*.h")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------- Qt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Sql Svg Test)
qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------- libmpv
include(LibmpvImportLib)

set(LOCALFLIX_MPV_DLL "${LOCALFLIX_LIBMPV_DIR}/libmpv-2.dll")
if(NOT EXISTS "${LOCALFLIX_MPV_DLL}")
    message(FATAL_ERROR "libmpv-2.dll not found in ${LOCALFLIX_LIBMPV_DIR}.\n"
            "Run scripts\\fetch-deps.cmd or see docs/DEPENDENCIES.md.")
endif()
localflix_generate_mpv_import_lib("${LOCALFLIX_MPV_DLL}" "${CMAKE_BINARY_DIR}/mpv-implib" LOCALFLIX_MPV_IMPLIB)

add_library(mpv::libmpv SHARED IMPORTED GLOBAL)
set_target_properties(mpv::libmpv PROPERTIES
    IMPORTED_LOCATION "${LOCALFLIX_MPV_DLL}"
    IMPORTED_IMPLIB "${LOCALFLIX_MPV_IMPLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LOCALFLIX_LIBMPV_DIR}/include")

# ---------------------------------------------------------------- common flags
function(localflix_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
            /wd4251 # dll-interface noise from Qt templates
            )
        if(LOCALFLIX_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
        QT_USE_QSTRINGBUILDER)
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/mpv)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(LOCALFLIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
