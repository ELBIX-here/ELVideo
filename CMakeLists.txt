cmake_minimum_required(VERSION 3.25)

project(ELVideo VERSION 0.6.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "ELVideo requires Windows and MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ELV_BUILD_APP   "Build the Qt/QML application" ON)
option(ELV_BUILD_CLI   "Build the engine test CLI"    ON)
option(ELV_BUILD_TESTS "Build unit tests"             OFF)

# Common compile settings for every target in the project.
add_library(elv_options INTERFACE)
target_compile_options(elv_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
target_compile_definitions(elv_options INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)

# Single version source: project(VERSION) -> elv_version.h (engine, app, VERSIONINFO, installer).
configure_file(cmake/elv_version.h.in ${CMAKE_BINARY_DIR}/generated/elv_version.h @ONLY)
target_include_directories(elv_options INTERFACE ${CMAKE_BINARY_DIR}/generated)

include(cmake/Libmpv.cmake)  # optional: the player needs it

add_subdirectory(core)

if(ELV_BUILD_CLI)
    add_subdirectory(tools/cli)
endif()

if(ELV_BUILD_APP)
    add_subdirectory(app)
    add_subdirectory(tools/icongen)
endif()

if(ELV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
