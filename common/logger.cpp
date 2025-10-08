#include "logger.h"

// Static member definitions
std::mutex Logger::log_mutex;
LogLevel Logger::current_log_level = LogLevel::INFO;
std::ofstream Logger::log_file;
bool Logger::log_to_console = true;
bool Logger::log_to_file = false;
std::string Logger::log_file_path;