#pragma once
#include <string>

typedef unsigned int uint;

namespace Config {
extern std::string username;
extern std::string password;
extern std::string server_addr;
extern uint server_port_text;
extern uint server_port_voice;
extern std::string avatar_path;

bool init(const std::string &configPath);
bool readConfig(const std::string &configPath);
}; // namespace Config
