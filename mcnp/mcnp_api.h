#pragma once

#ifdef _WIN32
  #ifdef MCNP_EXPORTS
    #define MCNP_API __declspec(dllexport)
  #else
    #define MCNP_API __declspec(dllimport)
  #endif
#else
  #define MCNP_API __attribute__((visibility("default")))
#endif

extern "C" {
MCNP_API void  mcnp_read_like_entry();
MCNP_API void* mcnp_get_logger_ptr();
MCNP_API void  mcnp_log_info(const char* message);
MCNP_API void  mcnp_shutdown();
}
