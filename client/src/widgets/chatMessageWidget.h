#include "ui_chatMessageWidget.h"
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class ChatMessageWidget : public QWidget {
    Q_OBJECT
  public:
    explicit ChatMessageWidget(QWidget *parent = nullptr)
        : QWidget(parent), ui(new Ui::ChatMessageWidget) {
        ui->setupUi(this);
    }

    void setMessage(const QString &username, const QString &date,
                    const QString &message, const QPixmap &avatar) {
        ui->usernameLabel->setText(username);
        ui->dateLabel->setText(date);
        ui->messageLabel->setText(message);
        ui->avatarLabel->setPixmap(avatar);
    }

  private:
    Ui::ChatMessageWidget *ui;
};
