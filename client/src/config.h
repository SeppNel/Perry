#pragma once
#include <string>

namespace Config {
extern std::string username;
extern std::string password;
extern std::string server_addr;
extern uint server_port_text;
extern uint server_port_voice;

void init(const std::string &configPath);
void readConfig(const std::string &configPath);
}; // namespace Config
