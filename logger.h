#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace DFHack {
namespace DFCH {

    class LoggerManager {
    public:
        static LoggerManager& getInstance() {
            static LoggerManager instance;
            return instance;
        }
        // 初始化日志系统（必须在使用前调用）
        void init(const std::string& main_log_file = "logs/dfch.log",
                spdlog::level::level_enum log_level = spdlog::level::debug
        );
        
        std::shared_ptr<spdlog::logger> getLogger() const {
            return m_logger;
        }
        // 获取未翻译专用日志
        std::shared_ptr<spdlog::logger> getUntransLogger() const {
            return untrans_logger;
        }
        void shutdown() {
            if (m_logger) m_logger->flush();
            if (untrans_logger) untrans_logger->flush();
            m_logger.reset();
            untrans_logger.reset();
            spdlog::shutdown();
            initialized = false;
        }

    private:
        LoggerManager()  = default;
        LoggerManager(const LoggerManager&) = delete;
        LoggerManager& operator=(const LoggerManager&) = delete;
        LoggerManager(LoggerManager&&) = delete;
        ~LoggerManager() = default;

        bool initialized = false;
        std::shared_ptr<spdlog::logger> m_logger;
        std::shared_ptr<spdlog::logger> untrans_logger;
    };

    #define LOGGERMANAGER DFHack::DFCH::LoggerManager::getInstance()

}
}
