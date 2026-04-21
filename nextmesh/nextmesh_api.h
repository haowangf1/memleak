#pragma once

#ifdef _WIN32
  #ifdef NEXTMESH_EXPORTS
    #define NEXTMESH_API __declspec(dllexport)
  #else
    #define NEXTMESH_API __declspec(dllimport)
  #endif
#else
  #define NEXTMESH_API __attribute__((visibility("default")))
#endif

extern "C" {
NEXTMESH_API void nextmesh_model_ctor_like(void* foreign_logger_ptr);
NEXTMESH_API void nextmesh_log_info(void* logger_ptr, const char* message);
NEXTMESH_API void nextmesh_shutdown();
}
