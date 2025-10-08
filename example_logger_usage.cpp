#include "logger.h"
#include <thread>
#include <chrono>

void exampleFunction() {
    LOG_INFO("This is an info message from exampleFunction");
    LOG_WARNING("This is a warning message");
    
    // You can also use formatted strings
    int value = 42;
    std::string message = "Processing value: " + std::to_string(value);
    LOG_DEBUG(message);
}

void anotherFileSimulation() {
    LOG_ERROR("This error will show it came from a different function");
    LOG_CRITICAL("This is a critical error!");
}

int main() {
    // Initialize the logger
    // Parameters: log_file_path, level, console_output, file_output
    Logger::init("logs/app.log", LogLevel::DEBUG, true, true);
    
    LOG_INFO("Application started");
    
    // Test different log levels
    LOG_DEBUG("This is a debug message");
    LOG_INFO("This is an info message");
    LOG_WARNING("This is a warning message");
    LOG_ERROR("This is an error message");
    LOG_CRITICAL("This is a critical message");
    
    // Call functions to show file/function detection
    exampleFunction();
    anotherFileSimulation();
    
    // Simulate some work
    for (int i = 0; i < 3; ++i) {
        LOG_INFO("Processing iteration " + std::to_string(i + 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Change log level during runtime
    LOG_INFO("Changing log level to ERROR - debug messages will be hidden");
    Logger::setLevel(LogLevel::ERROR);
    
    LOG_DEBUG("This debug message won't appear");
    LOG_ERROR("This error message will appear");
    
    LOG_INFO("Shutting down application");
    Logger::shutdown();
    
    return 0;
}