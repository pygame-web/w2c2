cmake_minimum_required(VERSION 2.8.12)

if (__wii__)
    add_compile_options("-D__wii__=1")
    #add_compile_options("-liso9660")
endif()

project(w2c2)
add_subdirectory(w2c2)
add_subdirectory(wasi)
