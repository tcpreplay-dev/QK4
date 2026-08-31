#ifndef TEXTSENDDIALOG_H
#define TEXTSENDDIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTextEdit>
#include <QVector>
#include <QWidget>

class TextSendController;
class RadioState;

/**
 * @brief Modeless dialog for typing text to be keyed as CW on the K4, via TextSendController.
 *
 * The line edit holds only the word currently being typed (freely editable, including
 * backspace) — a finished word (space/Enter) moves into the read-only history area below as
 * grey text and is simultaneously handed to the controller. The controller's signals then
 * recolor that same range: mid-tone while the K4 has the chunk in its KY buffer, amber once
 * `KY0` confirms it was actually keyed, red if that confirmation times out. Nothing brightens
 * on a local timing guess — only on real K4 feedback.
 */
class TextSendDialog : public QDialog {
    Q_OBJECT

public:
    explicit TextSendDialog(TextSendController *controller, RadioState *radioState, QWidget *parent = nullptr);

    void refreshMacros();

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override; // Escape aborts sending instead of closing
    bool eventFilter(QObject *watched, QEvent *event) override; // double-click-to-grab on RX panes

private:
    void commitText(const QString &text);
    void appendToDisplay(QChar ch);
    void recolorRange(int start, int length, const QString &colorHex);
    void onAborted(); // stops sending / clears the stall state — does NOT clear the display
    void finishPendingWord(); // commits + flushes whatever's left in the input, unconditionally
    QString expandTokens(const QString &macroText) const; // ~ -> my call, * -> Callsign field
    void refreshMacroTooltips();

    void onInputTextEdited(const QString &text);
    void onReturnPressed();
    void onMacroClicked(int slotIndex);
    void onCallsignTextChanged(const QString &text);

    void appendRxText(const QString &text, bool isSubRx);
    void updateRxPaneVisibility();
    static bool looksLikeCallsign(const QString &word);
    void handleRxDoubleClick(QTextEdit *pane, const QPoint &pos);

    TextSendController *m_controller; // not owned
    RadioState *m_radioState;       // not owned

    QLineEdit *m_callsignEdit = nullptr; // station currently being worked; session-local, not persisted
    QTextEdit *m_display = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_abortBtn = nullptr;
    QCheckBox *m_immediateModeCheck = nullptr;
    QCheckBox *m_pauseSendCheck = nullptr; // while checked, typed/macro text accumulates but nothing is sent
    QLabel *m_stalledBanner = nullptr;
    QVector<QPushButton *> m_macroButtons;

    QWidget *m_rxPaneContainer = nullptr; // holds legend + both RX panes; hidden when neither decode is running
    QLabel *m_prosignLegend = nullptr;
    QTextEdit *m_rxMainText = nullptr;
    QTextEdit *m_rxSubText = nullptr;

    int m_displayLength = 0; // character offsets here must match TextSendController's own count
    bool m_stalled = false;  // blocks further typing/macros until abort() resets the dialog
};

#endif // TEXTSENDDIALOG_H
