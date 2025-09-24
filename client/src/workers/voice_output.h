#pragma once
#include <QObject>
#include <qtmetamacros.h>
#include <string>
#include <thread>

class VoiceOutput : public QObject {
    Q_OBJECT

  public:
    void init(int sock);

  public slots:
    void stop();

  private:
    std::string server_ip;
    std::thread main;
};
