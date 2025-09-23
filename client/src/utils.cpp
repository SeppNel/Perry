#include "utils.h"

void clearLayout(QLayout *layout) {
    if (!layout)
        return;

    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater(); // safely schedule deletion
        }
        if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout); // recursive clear
        }
        delete item; // delete the QLayoutItem itself
    }
}