#include "config.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace Config {
uint port_text = 7065;
uint port_voice = 7066;
std::string storage_path = "./Perry_Data/";
std::string db_addr = "127.0.0.1";
std::string db_database = "perrydb";
std::string db_user = "perryuser";
std::string db_password = "perrypass";

void init(const std::string &configPath) {
    readConfig(configPath);
}

void readConfig(const std::string &configPath) {
    try {
        YAML::Node configFile = YAML::LoadFile(configPath);

        port_text = configFile["port_text"].as<uint>();
        port_voice = configFile["port_voice"].as<uint>();
        storage_path = configFile["storage_path"].as<std::string>();
        db_addr = configFile["db_addr"].as<std::string>();
        db_database = configFile["db_database"].as<std::string>();
        db_user = configFile["db_user"].as<std::string>();
        db_password = configFile["db_password"].as<std::string>();
    } catch (YAML::BadFile) {
        std::cerr << "Could not load config file\n";
    } catch (...) {
        std::cerr << "Something went wrong with the config file\n";
    }
}
} // namespace Config
