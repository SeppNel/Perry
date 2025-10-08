#pragma once
#include <QObject>
#include <cstdint>
#include <string>
#include <thread>

class VoiceChat : public QObject {
    Q_OBJECT

  public:
    void init(std::string ip, uint port, uint32_t ch);

  public slots:
    void stop();

  private:
    std::thread main;

  signals:
    void closed();
};