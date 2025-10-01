#pragma once
#include "common_data.h"
#include <QObject>
#include <vector>

class SocketReader : public QObject {
    Q_OBJECT
  public:
    void init(int s);

  signals:
    void channelsReady(const std::vector<ChannelInfo> &channels);
    void usersReady(const std::vector<UserInfo> &users);
    void newMessage(const MessageInfo &msg);
    void usersImgsReady(const std::unordered_map<uint32_t, QPixmap> &m);

  private:
    int sock;

    void run();
    void handler_ListChannels();
    void handler_ListUsers();
    void handler_Message();
    void handler_ListUserImgs();
};
