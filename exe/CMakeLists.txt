add_executable(memleak_exe main.cpp)

if(UNIX AND NOT APPLE)
  target_link_libraries(memleak_exe PRIVATE dl)
endif()
