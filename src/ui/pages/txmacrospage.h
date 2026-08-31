#ifndef TXMACROSPAGE_H
#define TXMACROSPAGE_H

#include <QLineEdit>
#include <QWidget>
#include <QVector>

/**
 * @brief Options page for the text send dialog: eight canned-message macro slots (label +
 *        message text, with token substitution — see TextSendDialog::expandTokens()).
 *
 * Macro editing lives here, not in the send dialog itself — mirrors how the Keyer/
 * Straight Key pages hold settings for the dialog's hardware-keying counterparts. The same
 * slots serve CW and the FSK data sub-modes.
 * The immediate-vs-word-complete send mode toggle lives in the send dialog itself
 * instead (an operator wants that live, not buried in Options).
 */
class TxMacrosPage : public QWidget {
    Q_OBJECT

public:
    explicit TxMacrosPage(QWidget *parent = nullptr);

    void refresh();

private:
    struct MacroRow {
        QString functionId;
        QLineEdit *labelEdit = nullptr;
        QLineEdit *textEdit = nullptr;
    };

    QVector<MacroRow> m_rows;
};

#endif // TXMACROSPAGE_H
