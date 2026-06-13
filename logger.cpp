
#include "logger.h"

#include <spdlog/async.h>  // async Logger
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

namespace DFHack {
namespace DFCH {

    static void initThreadPoolOnce() {
        static bool inited = []() {
            spdlog::init_thread_pool(4096, 1); // 4KB queue, 1 background thread
            return true;
        }();
        (void)inited;
    }

    void LoggerManager::init(const std::string& main_log_file,
                            spdlog::level::level_enum log_level) {
        if (m_logger || untrans_logger) {
            return;
        }

        fs::path mainPath(main_log_file);
        fs::path untransPath = mainPath.parent_path() / "dfch_untrans.log";
        // === Ensure directory exists ===
        auto ensureDir = [](const fs::path& p) {
            if (!p.parent_path().empty() && !fs::exists(p.parent_path())) {
                fs::create_directories(p.parent_path());
            }
        };
        ensureDir(mainPath);
        ensureDir(untransPath);

        // === Initialize spdlog thread pool (once only) ===
        initThreadPoolOnce();

        // === Create async + rotating logger max 10MB, keep 3 old files ===
        constexpr size_t max_file_size = 10 * 1024 * 1024; // 10 MB
        constexpr size_t max_files = 3;
        auto main_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            main_log_file, max_file_size, max_files);

        main_sink->set_level(spdlog::level::trace);

        m_logger = std::make_shared<spdlog::async_logger>(
            "dfch_logger", main_sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest
        );
        m_logger->set_level(log_level);
        m_logger->flush_on(spdlog::level::debug); // TODO: debug 立即刷新, release 时关闭, spdlog::level::err
        // m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");   // debug purpose

        // ===== untrans log (truncate on startup + append) =====
        std::ofstream(untransPath, std::ios::trunc); // truncate on startup
        auto untrans_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        untransPath.string(), false); // false = append mode
        untrans_logger = std::make_shared<spdlog::async_logger>(
            "untrans_logger", untrans_sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);
        untrans_logger->set_level(spdlog::level::info);
        untrans_logger->flush_on(spdlog::level::info);
        untrans_logger->set_pattern("%v");

        m_logger->info("Logger initialized successfully!");
        initialized = true;
    }

}
}
