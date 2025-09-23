#pragma once
#include <QObject>
#include <qtmetamacros.h>
#include <string>
#include <thread>

class VoiceInput : public QObject {
    Q_OBJECT

  public:
    void init(std::string s);

  public slots:
    void stop();

  private:
    std::string server_ip;
    std::thread main;
};
