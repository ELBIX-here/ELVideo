# Imports the prebuilt libmpv from third_party/mpv/sdk (scripts/get-libmpv.ps1) as
# the target `libmpv`, and sets ELV_HAVE_MPV. The package ships a MinGW import
# library only, so an MSVC one is generated from the DLL's export table.

set(ELV_MPV_DIR "${CMAKE_SOURCE_DIR}/third_party/mpv/sdk" CACHE PATH "libmpv SDK (libmpv-2.dll + include/mpv)")
set(ELV_HAVE_MPV OFF)

set(_mpv_dll "${ELV_MPV_DIR}/libmpv-2.dll")
if(NOT EXISTS "${_mpv_dll}" OR NOT EXISTS "${ELV_MPV_DIR}/include/mpv/client.h")
    message(WARNING "libmpv not found in ${ELV_MPV_DIR}; the video player is disabled. "
                    "Run: powershell -ExecutionPolicy Bypass -File scripts/get-libmpv.ps1")
    return()
endif()

set(_mpv_out "${CMAKE_BINARY_DIR}/libmpv")
set(_mpv_lib "${_mpv_out}/mpv.lib")
file(SHA256 "${_mpv_dll}" _mpv_hash)
if(NOT EXISTS "${_mpv_lib}" OR NOT "${_mpv_hash}" STREQUAL "${ELV_MPV_IMPLIB_HASH}")
    get_filename_component(_tools "${CMAKE_LINKER}" DIRECTORY)
    find_program(ELV_DUMPBIN dumpbin HINTS "${_tools}" REQUIRED)
    execute_process(COMMAND "${ELV_DUMPBIN}" /nologo /exports "${_mpv_dll}"
                    OUTPUT_VARIABLE _exports COMMAND_ERROR_IS_FATAL ANY)
    # "    ordinal hint RVA      name" lines
    string(REGEX MATCHALL "[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+(mpv_[A-Za-z0-9_]+)" _lines "${_exports}")
    set(_def "LIBRARY libmpv-2\nEXPORTS\n")
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE ".*[ \t](mpv_[A-Za-z0-9_]+)$" "\\1" _symbol "${_line}")
        string(APPEND _def "    ${_symbol}\n")
    endforeach()
    file(MAKE_DIRECTORY "${_mpv_out}")
    file(WRITE "${_mpv_out}/mpv.def" "${_def}")
    execute_process(COMMAND "${CMAKE_AR}" /nologo /def:${_mpv_out}/mpv.def /out:${_mpv_lib} /machine:x64
                    COMMAND_ERROR_IS_FATAL ANY)
    set(ELV_MPV_IMPLIB_HASH "${_mpv_hash}" CACHE INTERNAL "libmpv-2.dll the import library was made from")
    list(LENGTH _lines _count)
    message(STATUS "libmpv: generated import library with ${_count} exports")
endif()

add_library(libmpv SHARED IMPORTED GLOBAL)
set_target_properties(libmpv PROPERTIES
    IMPORTED_LOCATION "${_mpv_dll}"
    IMPORTED_IMPLIB "${_mpv_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${ELV_MPV_DIR}/include")
set(ELV_HAVE_MPV ON)
