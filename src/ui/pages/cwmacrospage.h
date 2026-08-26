#ifndef CWMACROSPAGE_H
#define CWMACROSPAGE_H

#include <QCheckBox>
#include <QLineEdit>
#include <QWidget>
#include <QVector>

/**
 * @brief Options page for the CW Send dialog: five canned-message macro slots (label +
 *        message text) and the persistent immediate-vs-word-complete send mode setting.
 *
 * Macro editing lives here, not in the CW Send dialog itself — mirrors how the Keyer/
 * Straight Key pages hold settings for the CW Send dialog's hardware-keying counterparts.
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
    QCheckBox *m_immediateModeCheck = nullptr;
};

#endif // CWMACROSPAGE_H
