 cmake_minimum_required(VERSION 3.16)
project(SDL_shader_tools LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

option(SDLSL_TESTS "Build SDL_shader_tools tests" OFF)

include(ExternalProject)

if(CMAKE_CROSSCOMPILING)
    set(BUILD_CMAKE_TOOLCHAIN_FILE "" CACHE FILEPATH "CMake toolchain file for build machine")
    if(NOT BUILD_CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR "When cross compiling, need a CMake toolchain file for the build machine in BUILD_CMAKE_TOOLCHAIN_FILE")
    endif()
endif()

find_package(SDL2 REQUIRED)
find_package(re2c)

if(NOT TARGET SDL2::SDL2)
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2-static)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "${SDL2_LIBRARY}")
    set_target_properties(SDL2::SDL2 PROPERTIES INTERFACE_INCLUDE_DIRS "${SDL2_INCLUDE_DIRS};${SDL2_INCLUDE_DIR}")
endif()

if(NOT re2c_FOUND)
    message(STATUS "re2c missing. You can go on, but can't rebuild the lexer.")
else()
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.c"
        DEPENDS SDL_shader_lexer.re
        COMMAND re2c::re2c ARGS -is --no-generation-date -o "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.c" "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.re"
    )
endif()

# lemon contains SDL_shader_tools-specific hacks
if(CMAKE_CROSSCOMPILING)
    ExternalProject_Add(lemon
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/lemon"
        BINARY_DIR "${PROJECT_BINARY_DIR}/lemon"
        PREFIX "${PROJECT_BINARY_DIR}/lemon"
        CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE:STRING=Release"
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${BUILD_CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY:PATH=${PROJECT_BINARY_DIR}/lemon/bin"
        BUILD_ALWAYS 1
        INSTALL_COMMAND ""
        STEP_TARGETS build
    )
    add_executable(lemon::lemon IMPORTED)
    set_target_properties(lemon::lemon PROPERTIES IMPORTED_LOCATION "${PROJECT_BINARY_DIR}/lemon/bin/lemon")
    add_dependencies(lemon::lemon lemon-build)
else()
    add_subdirectory(lemon)
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.h"
    MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.lemon"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lemon/lempar.c"
    COMMAND lemon::lemon ARGS -q "-T${CMAKE_CURRENT_SOURCE_DIR}/lemon/lempar.c" "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.lemon"
)

add_executable(sdl-shader-compiler
    "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.h"
    utils/sdl-shader-compiler.c
    SDL_shader_common.c
    SDL_shader_lexer.c
    SDL_shader_preprocessor.c
    SDL_shader_ast.c
    SDL_shader_compiler.c
)
target_include_directories(sdl-shader-compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sdl-shader-compiler PRIVATE SDL2::SDL2)
target_include_directories(sdl-shader-compiler PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR})
target_compile_definitions(sdl-shader-compiler PRIVATE SDL_MAIN_HANDLED)

add_executable(sdl-shader-bytecode-dumper
    utils/sdl-shader-bytecode-dumper.c
)
target_include_directories(sdl-shader-bytecode-dumper PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sdl-shader-bytecode-dumper PRIVATE SDL2::SDL2)
target_include_directories(sdl-shader-bytecode-dumper PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR})
target_compile_definitions(sdl-shader-bytecode-dumper PRIVATE SDL_MAIN_HANDLED)

find_package(Perl)
if(SDLSL_TESTS AND NOT CMAKE_CROSSCOMPILING AND Perl_FOUND)
    enable_testing()
    add_test(NAME SDL_shader_tools-tests COMMAND ${PERL_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/run_tests.pl")
    set_tests_properties(SDL_shader_tools-tests PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:sdl-shader-compiler>")
endif()
