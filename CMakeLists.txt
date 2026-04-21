cmake_minimum_required(VERSION 3.20)
project(spdlog_cross_dll LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /EHsc /utf-8)
endif()

add_subdirectory(mcnp)
add_subdirectory(nextmesh)
add_subdirectory(exe)
