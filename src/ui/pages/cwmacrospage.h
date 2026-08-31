#ifndef CWMACROSPAGE_H
#define CWMACROSPAGE_H

#include <QLineEdit>
#include <QWidget>
#include <QVector>

/**
 * @brief Options page for the CW Send dialog: eight canned-message macro slots (label +
 *        message text, with token substitution — see TextSendDialog::expandTokens()).
 *
 * Macro editing lives here, not in the CW Send dialog itself — mirrors how the Keyer/
 * Straight Key pages hold settings for the CW Send dialog's hardware-keying counterparts.
 * The immediate-vs-word-complete send mode toggle lives in the CW Send dialog itself
 * instead (an operator wants that live, not buried in Options).
 */
class CwMacrosPage : public QWidget {
    Q_OBJECT

public:
    explicit CwMacrosPage(QWidget *parent = nullptr);

    void refresh();

private:
    struct MacroRow {
        QString functionId;
        QLineEdit *labelEdit = nullptr;
        QLineEdit *textEdit = nullptr;
    };

    QVector<MacroRow> m_rows;
};

#endif // CWMACROSPAGE_H
