#include "config.h"
#include "logger.h"
#include <yaml-cpp/yaml.h>

namespace Config {
std::string username;
std::string password;
std::string server_addr;
uint server_port_text = 7065;
uint server_port_voice = 7066;
std::string avatar_path;

bool init(const std::string &configPath) {
    return readConfig(configPath);
}

bool readConfig(const std::string &configPath) {
    try {
        YAML::Node configFile = YAML::LoadFile(configPath);

        username = configFile["username"].as<std::string>();
        password = configFile["password"].as<std::string>();
        server_addr = configFile["server_addr"].as<std::string>();
        server_port_text = configFile["server_port_text"].as<uint>();
        server_port_voice = configFile["server_port_voice"].as<uint>();
        avatar_path = configFile["avatar_path"].as<std::string>();
        return true;
    } catch (YAML::BadFile) {
        LOG_ERROR("Corrupted file");
        return false;
    } catch (...) {
        LOG_ERROR("Something went wrong with the config file");
        return false;
    }
}
} // namespace Config
