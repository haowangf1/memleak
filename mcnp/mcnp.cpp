#include "mcnp_api.h"

#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace
{
std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_once;

bool init_logger()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    try
    {
        std::vector<spdlog::sink_ptr> sinks;
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_level(spdlog::level::info);
        sinks.emplace_back(sink);

        if (auto existed = spdlog::get("mcnp"))
        {
            spdlog::drop("mcnp");
        }

        g_logger = std::make_shared<spdlog::logger>("mcnp", sinks.begin(), sinks.end());
#ifndef NDEBUG
        g_logger->set_pattern("[mcnp dbg] [%s:%# %!] %v");
#else
        g_logger->set_pattern("[mcnp] %v");
#endif
        g_logger->set_level(spdlog::level::trace);
        g_logger->flush_on(spdlog::level::err);
        spdlog::register_logger(g_logger);
        spdlog::set_default_logger(g_logger);
        return true;
    }
    catch (const spdlog::spdlog_ex&)
    {
        g_logger.reset();
        return false;
    }
}
}

extern "C" {
MCNP_API void mcnp_read_like_entry()
{
    std::call_once(g_once, []() {
        init_logger();
    });
    if (g_logger)
    {
        g_logger->info("mcnp read-like entry initialized logger");
    }
}

MCNP_API void* mcnp_get_logger_ptr()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_logger.get();
}

MCNP_API void mcnp_log_info(const char* message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger)
    {
        g_logger->info("{}", message ? message : "(null)");
    }
}

MCNP_API void mcnp_shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger)
    {
        g_logger->flush();
    }
    spdlog::shutdown();
    g_logger.reset();
}
}
