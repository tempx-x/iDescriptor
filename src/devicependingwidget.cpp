#include "devicependingwidget.h"
#include <QLabel>
#include <QVBoxLayout>

DevicePendingWidget::DevicePendingWidget(bool locked, QWidget *parent)
    : QWidget{parent}, m_label{nullptr}, m_locked{locked}
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    m_label = new QLabel(m_locked ? "Please unlock the screen"
                                  : "Please click on trust on the popup",
                         this);

    layout->addWidget(m_label);
    setLayout(layout);
}

void DevicePendingWidget::next()
{
    m_label->setText("Please click on trust on the popup");
}