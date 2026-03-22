/*
 * Logger - 日志系统
 * 用于驱动调试和错误追踪
 */

#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <cstdarg>

class Logger {
public:
    enum Level {
        LOG_TRACE = 0,
        LOG_DEBUG = 1,
        LOG_INFO  = 2,
        LOG_WARN  = 3,
        LOG_ERROR = 4,
        LOG_FATAL = 5
    };

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const char* logFilePath = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (logFilePath) {
            m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
        } else {
            // Default log location
            char path[MAX_PATH];
            GetTempPathA(MAX_PATH, path);
            std::string defaultPath = std::string(path) + "BehringerASIO.log";
            m_logFile.open(defaultPath, std::ios::out | std::ios::trunc);
        }
        m_initialized = true;
        log(LOG_INFO, "BehringerASIO", "Logger initialized");
    }

    void setLevel(Level level) {
        m_level = level;
    }

    void log(Level level, const char* module, const char* fmt, ...) {
        if (level < m_level) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        // Get timestamp
        SYSTEMTIME st;
        GetLocalTime(&st);

        char timeStr[64];
        snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        // Level string
        const char* levelStr[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };

        // Format message
        char message[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(message, sizeof(message), fmt, args);
        va_end(args);

        // Build full log line
        char logLine[4096];
        snprintf(logLine, sizeof(logLine), "[%s] [%s] [%s] %s\n",
                 timeStr, levelStr[level], module, message);

        // Output to debugger
        OutputDebugStringA(logLine);

        // Output to file
        if (m_logFile.is_open()) {
            m_logFile << logLine;
            m_logFile.flush();
        }
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
        m_initialized = false;
    }

private:
    Logger() : m_level(LOG_DEBUG), m_initialized(false) {}
    ~Logger() { shutdown(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Level m_level;
    bool m_initialized;
    std::ofstream m_logFile;
    std::mutex m_mutex;
};

// Convenience macros
#define LOG_TRACE(module, fmt, ...) Logger::getInstance().log(Logger::LOG_TRACE, module, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(module, fmt, ...) Logger::getInstance().log(Logger::LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...)  Logger::getInstance().log(Logger::LOG_INFO,  module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)  Logger::getInstance().log(Logger::LOG_WARN,  module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) Logger::getInstance().log(Logger::LOG_ERROR, module, fmt, ##__VA_ARGS__)
#define LOG_FATAL(module, fmt, ...) Logger::getInstance().log(Logger::LOG_FATAL, module, fmt, ##__VA_ARGS__)
