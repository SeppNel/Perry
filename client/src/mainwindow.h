#pragma once
#include "common_data.h"
#include "packets.h"
#include <QListWidgetItem>
#include <QMainWindow>
#include <qpixmap.h>
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
    void init(int sock);
    ~MainWindow();

  private:
    Ui::MainWindow *ui;
    int sock;
    uint currentChannel = 1;
    int currentVoiceChannel = -1;
    std::unordered_map<uint32_t, UserData> m_users;
    std::unordered_map<uint32_t, QPixmap> m_usersImgs;

    void requestChannelMessages();
    void populateUsers();
    void startVoiceThread();
    void requestUserImages();

  public slots:
    void populateChannels(const std::vector<ChannelInfo> &ch);
    void addMessage(const MessageInfo &str);
    void updateUsers(const std::vector<UserInfo> &u);
    void onUsersImgsReady(const std::unordered_map<uint32_t, QPixmap> &m);

  private slots:
    void onReturnPressed();
    void switchChannel(QListWidgetItem *ch);
    void finishCall();

  signals:
    void sendPacket(const PacketHeader &header, const std::vector<char> &payload = std::vector<char>());
    void stopVC();
};
