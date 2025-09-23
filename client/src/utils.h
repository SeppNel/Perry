#pragma once
#include <QLayout>
#include <QTimer>
#include <QWidget>

void clearLayout(QLayout *layout);

// Non-blocking sleep (still processes events)
inline void qSleepNonBlocking(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec(); // keeps processing events during wait
}