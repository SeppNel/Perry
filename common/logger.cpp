#include "logger.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

// Anon namespace for internal linkage
namespace {

std::mutex log_mutex;
LogLevel current_log_level;
std::ofstream log_file;
bool log_to_console;
bool log_to_file;
std::string log_file_path;

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string extractFileName(const std::string &filePath) {
    std::filesystem::path path(filePath);
    return path.filename().string();
}

std::string levelToString(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::CRITICAL:
        return "CRIT";
    default:
        return "UNKNOWN";
    }
}

void writeLog(LogLevel level, const std::string &message,
              const std::string &file, int line, const std::string &function) {
    if (level < current_log_level)
        return;

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
        case LogLevel::DEBUG:
            color_code = "\033[36m";
            break; // Cyan
        case LogLevel::INFO:
            color_code = "\033[32m";
            break; // Green
        case LogLevel::WARNING:
            color_code = "\033[33m";
            break; // Yellow
        case LogLevel::ERROR:
            color_code = "\033[31m";
            break; // Red
        case LogLevel::CRITICAL:
            color_code = "\033[35m";
            break; // Magenta
        }

        std::cout << color_code << formatted_message << reset_code << std::endl;
    }

    // Output to file
    if (log_to_file && log_file.is_open()) {
        log_file << formatted_message << std::endl;
        log_file.flush(); // Ensure immediate write
    }
}
} // namespace

namespace Logger {
void init(const std::string &file_path,
          LogLevel level,
          bool console,
          bool file) {
    std::lock_guard<std::mutex> lock(log_mutex);

    current_log_level = level;
    log_to_console = console;
    log_to_file = file;
    log_file_path = file_path;

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

void setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex);
    current_log_level = level;
}

void enableConsole(bool enable) {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_to_console = enable;
}

void enableFile(bool enable) {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_to_file = enable;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file.is_open()) {
        log_file.close();
    }
}

// Main logging methods
void debug(const std::string &message, const std::string &file, int line, const std::string &function) {
    writeLog(LogLevel::DEBUG, message, file, line, function);
}

void info(const std::string &message, const std::string &file, int line, const std::string &function) {
    writeLog(LogLevel::INFO, message, file, line, function);
}

void warning(const std::string &message, const std::string &file, int line, const std::string &function) {
    writeLog(LogLevel::WARNING, message, file, line, function);
}

void error(const std::string &message, const std::string &file, int line, const std::string &function) {
    writeLog(LogLevel::ERROR, message, file, line, function);
}

void critical(const std::string &message, const std::string &file, int line, const std::string &function) {
    writeLog(LogLevel::CRITICAL, message, file, line, function);
}
} // namespace Logger
