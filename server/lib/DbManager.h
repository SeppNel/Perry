#include "common_data.h"
#include <cstdint>
#include <string>
#include <vector>

namespace DbManager {

uint32_t getUserId(const std::string &username);
std::string getUserPassword(const uint32_t id);
std::vector<ChannelInfo> getChannels();
std::vector<UserInfo> getUsers();
bool saveMessage(const std::string &msg, const uint32_t channelId, const uint32_t userId);
std::vector<MessageInfo> getMessages(const uint32_t channelId);
}; // namespace DbManager