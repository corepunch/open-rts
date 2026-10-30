cmake_minimum_required(VERSION 3.22.1)
project(open_rts LANGUAGES C)

get_filename_component(RTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${RTS_ROOT}/third_party/SDL" SDL)

set(game_ids dark-reign dark-colony 7legion kknd warcraft-2 starcraft z)
if(NOT RTS_GAME IN_LIST game_ids)
    message(FATAL_ERROR "Unknown RTS_GAME: ${RTS_GAME}")
endif()

# Match Makefile discovery: adding a C file needs no build-list edit.
execute_process(
    COMMAND find driver render hud interface play game sound "games/${RTS_GAME}" -name "*.c"
    WORKING_DIRECTORY "${RTS_ROOT}"
    OUTPUT_VARIABLE sources COMMAND_ERROR_IS_FATAL ANY
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "\n" ";" sources "${sources}")
if(RTS_GAME STREQUAL "starcraft")
    execute_process(
        COMMAND find "${RTS_ROOT}/third_party/libsmacker" -name "*.c"
        OUTPUT_VARIABLE smacker COMMAND_ERROR_IS_FATAL ANY
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" smacker "${smacker}")
    list(SORT smacker)
    add_library(smacker SHARED ${smacker})
endif()
list(SORT sources)
list(TRANSFORM sources PREPEND "${RTS_ROOT}/" REGEX "^[^/]")
add_library(main SHARED ${sources})
target_include_directories(main PRIVATE "${RTS_ROOT}/include" "${RTS_ROOT}/games/${RTS_GAME}")
target_compile_features(main PRIVATE c_std_11)
target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
# SDL_main runs on a Java-created thread, whose default stack is only 1 MiB.
target_compile_options(main PRIVATE -Wframe-larger-than=262144 -Werror=frame-larger-than)
target_compile_definitions(main PRIVATE RTS_NATIVE_WORLD RTS_WORLD_Y_UP=$<STREQUAL:${RTS_GAME},dark-colony>)
if(NOT RTS_GAME STREQUAL "z")
    string(TOUPPER "${RTS_GAME}" game_define)
    string(REPLACE "-" "_" game_define "${game_define}")
    target_compile_definitions(main PRIVATE "RTS_GAME_${game_define}")
endif()
target_link_libraries(main PRIVATE SDL2::SDL2 m)
if(RTS_GAME STREQUAL "starcraft")
    target_link_libraries(main PRIVATE smacker)
endif()
