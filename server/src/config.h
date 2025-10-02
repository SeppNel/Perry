#pragma once
#include <string>

namespace Config {
extern uint port_text;
extern uint port_voice;
extern std::string storage_path;
extern std::string db_addr;
extern std::string db_database;
extern std::string db_user;
extern std::string db_password;

void init(const std::string &configPath);
void readConfig(const std::string &configPath);
}; // namespace Config
