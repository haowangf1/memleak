add_library(mcnp SHARED mcnp.cpp)

target_include_directories(mcnp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include)

target_compile_definitions(mcnp PRIVATE MCNP_EXPORTS)
