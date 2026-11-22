cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(bltd-bots LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "BLTD.exe is a 32-bit program. Configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BLTD_VERSION "dev" CACHE STRING "Version of the mod, such as 2.2.0")
set(BLTD_LOBBY_MIN_PLAYERS 4 CACHE STRING "Default [Lobby] MinPlayers of a new ini file")
set(BLTD_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH "Folder of the plugin, dinput8.dll and the bltd-bots data folder")

set(bots_dir "${CMAKE_CURRENT_SOURCE_DIR}/bltd-bots")
set(loader_dir "${CMAKE_CURRENT_SOURCE_DIR}/loader")

string(TIMESTAMP BLTD_BUILD_STAMP "%Y-%m-%dT%H:%M")
string(REGEX MATCH "^v?[0-9]+(\\.[0-9]+)*" version_numbers "${BLTD_VERSION}")
string(REGEX REPLACE "^v" "" version_numbers "${version_numbers}")
string(REPLACE "." ";" version_parts "${version_numbers}")
list(APPEND version_parts 0 0 0)
list(SUBLIST version_parts 0 3 version_parts)
list(JOIN version_parts "," BLTD_VERSION_NUMBERS)
configure_file("${loader_dir}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

file(GLOB bots_sources CONFIGURE_DEPENDS "${bots_dir}/*.cpp")

add_library(bltd-bots SHARED ${bots_sources}
    "${loader_dir}/dinput8-proxy.cpp"
    "${loader_dir}/entry-bots.cpp"
    "${loader_dir}/hook.cpp"
    "${loader_dir}/loader.cpp"
    "${loader_dir}/log.cpp"
    "${loader_dir}/xlive-patch.cpp"
    "${loader_dir}/version.rc"
    "${loader_dir}/bltd-bots.def"
)
target_include_directories(bltd-bots PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(bltd-bots PRIVATE UNICODE _UNICODE BLTD_LOBBY_MIN_PLAYERS=${BLTD_LOBBY_MIN_PLAYERS})
target_compile_options(bltd-bots PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(bltd-bots PRIVATE
    /SUBSYSTEM:WINDOWS
    $<$<CONFIG:Release>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)
set_target_properties(bltd-bots PROPERTIES
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${BLTD_OUTPUT_DIR}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${BLTD_OUTPUT_DIR}/$<0:>"
)
add_custom_command(TARGET bltd-bots POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:bltd-bots>" "${BLTD_OUTPUT_DIR}/dinput8.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${bots_dir}/navcache" "${BLTD_OUTPUT_DIR}/bltd-bots/navcache"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${bots_dir}/ui" "${BLTD_OUTPUT_DIR}/bltd-bots/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${bots_dir}/maps" "${BLTD_OUTPUT_DIR}/bltd-bots/maps"
)
