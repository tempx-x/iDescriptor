#include "zlineedit.h"
#include "iDescriptor-ui.h"

ZLineEdit::ZLineEdit(QWidget *parent) : QLineEdit(parent) { setupStyles(); }

ZLineEdit::ZLineEdit(const QString &text, QWidget *parent)
    : QLineEdit(text, parent)
{
    setupStyles();
}

void ZLineEdit::setupStyles()
{
    updateStyles();

    // Connect to palette changes for dynamic theme updates
    connect(qApp, &QApplication::paletteChanged, this,
            &ZLineEdit::updateStyles);
}

void ZLineEdit::updateStyles()
{
    setStyleSheet("QLineEdit { "
                  "    border: 2px solid " +
                  qApp->palette().color(QPalette::Midlight).name() +
                  "; "
                  "    border-radius: 6px; "
                  "    padding: 8px 12px; "
                  "    font-size: 14px; "
                  "} "
                  "QLineEdit:focus { "
                  "    border: 2px solid " +
                  COLOR_ACCENT_BLUE.name() +
                  "; "
                  "    outline: none; "
                  "}");
}