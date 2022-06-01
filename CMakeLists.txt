CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
PROJECT(SDL_shader_language)

find_package(SDL2 REQUIRED)

find_program(RE2C re2c DOC "Path to re2c command line app: https://re2c.org/")
if(NOT RE2C)
    message(STATUS "re2c missing. You can go on, but can't rebuild the lexer.")
else()
    mark_as_advanced(RE2C)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.c"
        DEPENDS SDL_shader_lexer.re
        COMMAND "${RE2C}"
        ARGS -is --no-generation-date -o "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.c" "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_lexer.re"
    )
endif()

# We build lemon, then use it to generate parser C code.
# !!! FIXME: this needs to build for the current platform if cross-compiling.
add_executable(lemon "lemon/lemon.c")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.h"
    MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.lemon"
    DEPENDS lemon "${CMAKE_CURRENT_SOURCE_DIR}/lemon/lempar.c"
    COMMAND lemon
    ARGS -q "-T${CMAKE_CURRENT_SOURCE_DIR}/lemon/lempar.c" "${CMAKE_CURRENT_SOURCE_DIR}/SDL_shader_parser.lemon"
)

add_executable(sdl-shader-compiler
    utils/sdl-shader-compiler.c
    SDL_shader_common.c
    SDL_shader_lexer.c
    SDL_shader_preprocessor.c
    #SDL_shader_compiler.c
)
target_include_directories(sdl-shader-compiler PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR})
target_include_directories(sdl-shader-compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sdl-shader-compiler ${SDL2_LIBRARIES} ${SDL2_LIBRARY})

# end of CMakeLists.txt ...

