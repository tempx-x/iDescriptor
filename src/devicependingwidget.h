#ifndef DEVICEPENDINGWIDGET_H
#define DEVICEPENDINGWIDGET_H

#include <QLabel>
#include <QWidget>

class DevicePendingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DevicePendingWidget(bool locked, QWidget *parent);
    void next();
signals:
private:
    QLabel *m_label;
    bool m_locked;
};

#endif // DEVICEPENDINGWIDGET_H
