cmake_minimum_required(VERSION 3.16)
project(lemon LANGUAGES C)

add_executable(lemon lemon.c)
add_executable(lemon::lemon ALIAS lemon)
