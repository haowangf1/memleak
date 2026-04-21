add_library(nextmesh SHARED nextmesh.cpp)

target_include_directories(nextmesh PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include)

target_compile_definitions(nextmesh PRIVATE NEXTMESH_EXPORTS)
