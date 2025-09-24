#pragma once
#include "common_data.h"
#include "packets.h"
#include <QListWidgetItem>
#include <QMainWindow>
#include <string>
#include <unordered_map>
#include <vector>

enum ChannelListRoles {
    ID = 256,      // int
    IS_VOICE = 257 // bool
};

struct UserData {
    bool is_online;
    std::string name;
    // image
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    void init(int sock, int vc_sock);
    ~MainWindow();

  private:
    Ui::MainWindow *ui;
    int sock;
    int vc_sock;
    uint currentChannel = 1;
    int currentVoiceChannel = -1;
    std::unordered_map<uint32_t, UserData> m_users;

    void requestChannelMessages();
    void populateUsers();
    void startVoiceThread();

  public slots:
    void populateChannels(const std::vector<ChannelInfo> &ch);
    void addMessage(const MessageInfo &str);
    void updateUsers(const std::vector<UserInfo> &u);

  private slots:
    void onReturnPressed();
    void switchChannel(QListWidgetItem *ch);
    void finishCall();

  signals:
    void sendPacket(const PacketHeader &header, const std::vector<char> &payload);
    void stopVC();
};
