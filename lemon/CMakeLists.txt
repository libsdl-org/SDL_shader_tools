cmake_minimum_required(VERSION 3.16)
project(lemon LANGUAGES C)

option(LEMON_INSTALL "Install lemon")

add_executable(lemon lemon.c)
add_executable(lemon::lemon ALIAS lemon)

if(LEMON_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS lemon
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()
