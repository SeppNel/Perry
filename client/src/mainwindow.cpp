#include "mainwindow.h"
#include "common_data.h"
#include "ui_mainwindow.h"
#include "utils.h"
#include "widgets/chatMessageWidget.h"
#include "workers/voice_input.h"
#include "workers/voice_output.h"
#include <QDateTime>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <cstdint>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::init(int sock, int vc_sock) {
    this->sock = sock;
    this->vc_sock = vc_sock;

    ui->chatAreaLayout->setAlignment(Qt::AlignTop);

    // Connect Main UI stuff
    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &MainWindow::onReturnPressed);
    connect(ui->channelsList, &QListWidget::itemPressed, this, &MainWindow::switchChannel);
    connect(ui->closeCall, &QPushButton::pressed, this, &MainWindow::finishCall);

    QScrollBar *bar = ui->chatArea->verticalScrollBar();
    connect(bar, &QScrollBar::rangeChanged, this, [bar, this]() {
        bar->setValue(bar->maximum());
    });

    ui->closeCall->setVisible(false);

    // Do stuff
    while (m_users.empty()) {
        qSleepNonBlocking(10);
    }
    requestChannelMessages();
}

void MainWindow::requestChannelMessages() {
    std::vector<char> payload;
    payload.reserve(sizeof(currentChannel));

    const char *p_chId = reinterpret_cast<const char *>(&currentChannel);
    payload.insert(payload.begin(), p_chId, p_chId + sizeof(currentChannel));

    PacketHeader h = {(uint8_t)PacketType::LIST_MESSAGES, static_cast<uint32_t>(payload.size())};
    emit sendPacket(h, payload);
}

void MainWindow::populateChannels(const std::vector<ChannelInfo> &ch) {
    ui->channelsList->clear();

    for (const ChannelInfo &c : ch) {
        QString name = QString::fromStdString("# " + c.name);

        QListWidgetItem *item = new QListWidgetItem();

        item->setText(name);
        item->setData(ChannelListRoles::ID, c.id);
        item->setData(ChannelListRoles::IS_VOICE, c.is_voice);

        ui->channelsList->addItem(item);
    }
}

void MainWindow::populateUsers() {
    ui->membersList->clear();

    ui->membersList->addItem(new QListWidgetItem("Online: "));

    for (const auto &u : m_users) {
        if (!u.second.is_online) {
            continue;
        }

        QString name = QString::fromStdString("@" + u.second.name);
        ui->membersList->addItem(new QListWidgetItem(name));
    }

    ui->membersList->addItem(new QListWidgetItem(""));
    ui->membersList->addItem(new QListWidgetItem("Offline: "));

    for (const auto &u : m_users) {
        if (u.second.is_online) {
            continue;
        }

        QString name = QString::fromStdString("@" + u.second.name);
        ui->membersList->addItem(new QListWidgetItem(name));
    }
}

void MainWindow::onReturnPressed() {
    QString text = ui->lineEdit->text();
    std::string str = text.toStdString();
    uint32_t str_len = str.length();

    const char *p_chId = reinterpret_cast<const char *>(&currentChannel);

    std::vector<char> buff;
    buff.reserve(str_len + sizeof(currentChannel));

    buff.insert(buff.begin(), p_chId, p_chId + sizeof(currentChannel));
    buff.insert(buff.end(), str.begin(), str.end());

    PacketHeader h = {(uint8_t)PacketType::MESSAGE, static_cast<uint32_t>(buff.size())};
    emit sendPacket(h, buff);

    ui->lineEdit->clear();
}

void MainWindow::addMessage(const MessageInfo &m) {
    QString text = QString::fromStdString(m.msg);
    QString user = QString::fromStdString(m_users[m.userId].name);

    QDateTime dt = QDateTime::fromSecsSinceEpoch(m.timestamp, QTimeZone::UTC);
    dt = dt.toLocalTime();

    ChatMessageWidget *msg = new ChatMessageWidget;
    msg->setMessage(user, dt.toString("hh:mm:ss dd-MM-yyyy"), text, QPixmap("./avatar.png"));
    ui->chatAreaLayout->addWidget(msg);
}

void MainWindow::startVoiceThread() {
    // input thread
    QThread *thread = new QThread();
    VoiceInput *vi = new VoiceInput();

    vi->moveToThread(thread);

    QObject::connect(thread, &QThread::started, [vi, this]() {
        vi->init(vc_sock, currentVoiceChannel);
    });

    QObject::connect(this, &MainWindow::stopVC, vi, &VoiceInput::stop);
    QObject::connect(thread, &QThread::finished, vi, &QObject::deleteLater);

    QThread *thread2 = new QThread();
    VoiceOutput *vo = new VoiceOutput();

    vo->moveToThread(thread2);

    QObject::connect(thread2, &QThread::started, [vo, this]() {
        vo->init(vc_sock);
    });

    QObject::connect(this, &MainWindow::stopVC, vo, &VoiceOutput::stop);
    QObject::connect(thread2, &QThread::finished, vo, &QObject::deleteLater);

    thread->start();
    thread2->start();

    ui->closeCall->setVisible(true);
}

void MainWindow::switchChannel(QListWidgetItem *ch) {
    int id = ch->data(ChannelListRoles::ID).toInt();
    bool is_voice = ch->data(ChannelListRoles::IS_VOICE).toBool();

    if (id == currentChannel || id == currentVoiceChannel) {
        return;
    }

    if (is_voice) {
        emit stopVC();
        currentVoiceChannel = ch->data(ChannelListRoles::ID).toInt();
        startVoiceThread();
    } else {
        currentChannel = ch->data(ChannelListRoles::ID).toInt();
        clearLayout(ui->chatAreaLayout);
        requestChannelMessages();
    }
}

void MainWindow::updateUsers(const std::vector<UserInfo> &v) {
    m_users.clear();
    for (const UserInfo &u : v) {
        UserData data = {u.is_online, u.name};
        m_users[u.id] = data;
    }

    populateUsers();
}

void MainWindow::finishCall() {
    emit stopVC();
    ui->closeCall->setVisible(false);
    currentVoiceChannel = -1;
}