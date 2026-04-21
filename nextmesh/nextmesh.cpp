#include "nextmesh_api.h"

#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/pattern_formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace
{
std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_once;

void init_nextmesh_logger()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<spdlog::sink_ptr> sinks;
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_level(spdlog::level::info);
    sinks.emplace_back(sink);

    if (auto existed = spdlog::get("nextmesh"))
    {
        spdlog::drop("nextmesh");
    }

    g_logger = std::make_shared<spdlog::logger>("nextmesh", sinks.begin(), sinks.end());
    g_logger->set_pattern("[nextmesh self] %v");
    g_logger->set_level(spdlog::level::trace);
    g_logger->flush_on(spdlog::level::err);
    spdlog::register_logger(g_logger);
}
}

extern "C" {
NEXTMESH_API void nextmesh_model_ctor_like(void* foreign_logger_ptr)
{
    std::call_once(g_once, []() {
        init_nextmesh_logger();
    });

    auto* foreign_logger = reinterpret_cast<spdlog::logger*>(foreign_logger_ptr);
    if (foreign_logger != nullptr)
    {
        foreign_logger->info("foreign logger pointer received in nextmesh");
    }

    if (g_logger)
    {
        g_logger->info("nextmesh self logger alive");
    }

    auto default_logger = spdlog::default_logger();
    if (default_logger)
    {
        default_logger->set_formatter(std::make_unique<spdlog::pattern_formatter>("[nextmesh default formatter] %v"));
        default_logger->info("nextmesh touched its own default logger view");
        default_logger->set_pattern("[nextmesh default pattern] %v");
        default_logger->info("nextmesh reconfigured default logger in nextmesh");
    }
}

NEXTMESH_API void nextmesh_log_info(void* logger_ptr, const char* message)
{
    auto* logger = reinterpret_cast<spdlog::logger*>(logger_ptr);
    if (logger != nullptr)
    {
        logger->info("{}", message ? message : "(null)");
    }
}

NEXTMESH_API void nextmesh_shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    spdlog::shutdown();
    g_logger.reset();
}
}
