#include "config.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace Config {
std::string username;
std::string password;
std::string server_addr;
uint server_port_text = 7065;
uint server_port_voice = 7066;
std::string avatar_path;

void init(const std::string &configPath) {
    readConfig(configPath);
}

void readConfig(const std::string &configPath) {
    try {
        YAML::Node configFile = YAML::LoadFile(configPath);

        username = configFile["username"].as<std::string>();
        password = configFile["password"].as<std::string>();
        server_addr = configFile["server_addr"].as<std::string>();
        server_port_text = configFile["server_port_text"].as<uint>();
        server_port_voice = configFile["server_port_voice"].as<uint>();
        avatar_path = configFile["avatar_path"].as<std::string>();
    } catch (YAML::BadFile) {
        std::cerr << "Could not load config file\n";
    } catch (...) {
        std::cerr << "Something went wrong with the config file\n";
    }
}
} // namespace Config
