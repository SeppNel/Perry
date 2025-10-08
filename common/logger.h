#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <filesystem>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

class Logger {
private:
    static std::mutex log_mutex;
    static LogLevel current_log_level;
    static std::ofstream log_file;
    static bool log_to_console;
    static bool log_to_file;
    static std::string log_file_path;

    static std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    static std::string extractFileName(const std::string& filePath) {
        std::filesystem::path path(filePath);
        return path.filename().string();
    }

    static std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRIT";
            default: return "UNKNOWN";
        }
    }

    static void writeLog(LogLevel level, const std::string& message, 
                        const std::string& file, int line, const std::string& function) {
        if (level < current_log_level) return;

        std::lock_guard<std::mutex> lock(log_mutex);
        
        std::string timestamp = getCurrentTimestamp();
        std::string filename = extractFileName(file);
        std::string level_str = levelToString(level);
        
        // Format: [TIMESTAMP] [LEVEL] [file:line:function] message
        std::stringstream log_entry;
        log_entry << "[" << timestamp << "] "
                  << "[" << std::setw(5) << level_str << "] "
                  << "[" << filename << ":" << line << ":" << function << "] "
                  << message;

        std::string formatted_message = log_entry.str();

        // Output to console
        if (log_to_console) {
            // Add colors for console output
            std::string color_code;
            std::string reset_code = "\033[0m";
            
            switch (level) {
                case LogLevel::DEBUG: color_code = "\033[36m"; break;    // Cyan
                case LogLevel::INFO: color_code = "\033[32m"; break;     // Green
                case LogLevel::WARNING: color_code = "\033[33m"; break;  // Yellow
                case LogLevel::ERROR: color_code = "\033[31m"; break;    // Red
                case LogLevel::CRITICAL: color_code = "\033[35m"; break; // Magenta
            }
            
            std::cout << color_code << formatted_message << reset_code << std::endl;
        }

        // Output to file
        if (log_to_file && log_file.is_open()) {
            log_file << formatted_message << std::endl;
            log_file.flush(); // Ensure immediate write
        }
    }

public:
    static void init(const std::string& log_file_path = "", 
                    LogLevel level = LogLevel::INFO, 
                    bool console = true, 
                    bool file = false) {
        std::lock_guard<std::mutex> lock(log_mutex);
        
        current_log_level = level;
        log_to_console = console;
        log_to_file = file;
        Logger::log_file_path = log_file_path;

        if (log_to_file && !log_file_path.empty()) {
            // Create directory if it doesn't exist
            std::filesystem::path path(log_file_path);
            std::filesystem::create_directories(path.parent_path());
            
            log_file.open(log_file_path, std::ios::app);
            if (!log_file.is_open()) {
                std::cerr << "Failed to open log file: " << log_file_path << std::endl;
                log_to_file = false;
            }
        }
    }

    static void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(log_mutex);
        current_log_level = level;
    }

    static void enableConsole(bool enable) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_to_console = enable;
    }

    static void enableFile(bool enable) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_to_file = enable;
    }

    static void shutdown() {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    // Main logging methods
    static void debug(const std::string& message, const std::string& file, int line, const std::string& function) {
        writeLog(LogLevel::DEBUG, message, file, line, function);
    }

    static void info(const std::string& message, const std::string& file, int line, const std::string& function) {
        writeLog(LogLevel::INFO, message, file, line, function);
    }

    static void warning(const std::string& message, const std::string& file, int line, const std::string& function) {
        writeLog(LogLevel::WARNING, message, file, line, function);
    }

    static void error(const std::string& message, const std::string& file, int line, const std::string& function) {
        writeLog(LogLevel::ERROR, message, file, line, function);
    }

    static void critical(const std::string& message, const std::string& file, int line, const std::string& function) {
        writeLog(LogLevel::CRITICAL, message, file, line, function);
    }
};

// Static member definitions (put these in a .cpp file in a real project)
std::mutex Logger::log_mutex;
LogLevel Logger::current_log_level = LogLevel::INFO;
std::ofstream Logger::log_file;
bool Logger::log_to_console = true;
bool Logger::log_to_file = false;
std::string Logger::log_file_path;

// Convenience macros that automatically capture file, line, and function information
#define LOG_DEBUG(msg) Logger::debug(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(msg) Logger::info(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(msg) Logger::warning(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(msg) Logger::error(msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL(msg) Logger::critical(msg, __FILE__, __LINE__, __FUNCTION__)

// Formatted logging macros
#define LOG_DEBUG_F(fmt, ...) do { \
    std::ostringstream oss; \
    oss << fmt; \
    Logger::debug(oss.str(), __FILE__, __LINE__, __FUNCTION__); \
} while(0)

#define LOG_INFO_F(fmt, ...) do { \
    std::ostringstream oss; \
    oss << fmt; \
    Logger::info(oss.str(), __FILE__, __LINE__, __FUNCTION__); \
} while(0)

#define LOG_WARNING_F(fmt, ...) do { \
    std::ostringstream oss; \
    oss << fmt; \
    Logger::warning(oss.str(), __FILE__, __LINE__, __FUNCTION__); \
} while(0)

#define LOG_ERROR_F(fmt, ...) do { \
    std::ostringstream oss; \
    oss << fmt; \
    Logger::error(oss.str(), __FILE__, __LINE__, __FUNCTION__); \
} while(0)

#define LOG_CRITICAL_F(fmt, ...) do { \
    std::ostringstream oss; \
    oss << fmt; \
    Logger::critical(oss.str(), __FILE__, __LINE__, __FUNCTION__); \
} while(0)