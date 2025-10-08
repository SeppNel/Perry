#pragma once
#include <string>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

namespace Logger {

void init(const std::string &log_file_path = "", LogLevel level = LogLevel::INFO, bool console = true, bool file = false);
void setLevel(LogLevel level);
void enableConsole(bool enable);
void enableFile(bool enable);
void shutdown();

// Main logging methods
void debug(const std::string &message, const std::string &file, int line, const std::string &function);
void info(const std::string &message, const std::string &file, int line, const std::string &function);
void warning(const std::string &message, const std::string &file, int line, const std::string &function);
void error(const std::string &message, const std::string &file, int line, const std::string &function);
void critical(const std::string &message, const std::string &file, int line, const std::string &function);

}; // namespace Logger

// Convenience macros that automatically capture file, line, and function information
#define LOG_DEBUG(msg) Logger::debug(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(msg) Logger::info(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(msg) Logger::warning(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(msg) Logger::error(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL(msg) Logger::critical(msg, __FILE__, __LINE__, __FUNCTION__)