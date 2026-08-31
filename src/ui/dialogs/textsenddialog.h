#ifndef TEXTSENDDIALOG_H
#define TEXTSENDDIALOG_H

#include <QCheckBox>
#include <QComboBox>
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
class VfoSquareWidget;

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

    // Re-reads the controller's session mode (and, for FSK, the active DATA sub-mode) and
    // adapts the dialog to it: window title, prosign legend/highlighting, and whether typed
    // text is forced to upper case. Called by MainWindow whenever the radio's mode changes,
    // and on every show. A CW <-> FSK switch drops a divider into the history rather than
    // clearing it — see appendSessionDivider().
    void applySessionMode();

signals:
    // The TX-side arrow was clicked. MainWindow turns this into the same CAT the SPLIT button
    // sends — the dialog has no ConnectionController of its own.
    void txSideToggleRequested();

public:

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override; // Escape aborts sending instead of closing
    bool eventFilter(QObject *watched, QEvent *event) override; // double-click-to-grab on RX panes

private:
    // `force` bypasses the "Pause sending" gate — see onReturnPressed().
    void commitText(const QString &text, bool force = false);
    void appendToDisplay(QChar ch);
    void recolorRange(int start, int length, const QString &colorHex);
    void onAborted(); // stops sending / clears the stall state — does NOT clear the display
    void finishPendingWord(bool force = false); // commits + flushes whatever's left in the input
    QString expandTokens(const QString &macroText) const; // ~ -> my call, * -> Callsign field
    void refreshMacroTooltips();

    void onInputTextEdited(const QString &text);
    void onReturnPressed();
    void onMacroClicked(int slotIndex);
    void onCallsignTextChanged(const QString &text);

    void appendRxText(const QString &text, bool isSubRx);
    void updateRxPaneVisibility();
    void appendSessionDivider(const QString &label);
    // Repaints the A / TX / B strip from the radio's split and per-VFO modes.
    void updateVfoStrip();
    // "Type here" vs "Enter here" — the verb that actually sends depends on the pause state.
    void updateInputPlaceholder();
    // True while the send mode is "Enter key": typed text accumulates and nothing leaves until
    // Enter (which is also what unchecking the old "Hold until Enter" checkbox used to do).
    bool holdUntilEnter() const;
    // Rebuilds the combo's entries for the current session and re-selects the stored mode.
    void refreshSendModeCombo();
    static bool looksLikeCallsign(const QString &word);
    void handleRxDoubleClick(QTextEdit *pane, const QPoint &pos);

    TextSendController *m_controller; // not owned
    RadioState *m_radioState;       // not owned

    QLineEdit *m_callsignEdit = nullptr; // station currently being worked; session-local, not persisted
    QTextEdit *m_display = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_abortBtn = nullptr;
    QComboBox *m_sendModeCombo = nullptr; // character / word / Enter — see RadioSettings::TextSendMode
    QLabel *m_stalledBanner = nullptr;
    QLabel *m_vfoAModeLabel = nullptr; // mode under the A square, as on the K4's own display
    QLabel *m_vfoBModeLabel = nullptr;
    QPushButton *m_txSideBtn = nullptr; // the TX arrow; clickable only when both VFOs match
    bool m_txSwitchable = false; // both VFOs in the same mode — see updateVfoStrip()
    VfoSquareWidget *m_vfoASquare = nullptr; // recolored red while this VFO is transmitting
    VfoSquareWidget *m_vfoBSquare = nullptr;
    QVector<QPushButton *> m_macroButtons;

    QWidget *m_rxPaneContainer = nullptr; // holds legend + both RX panes; hidden when neither decode is running
    QLabel *m_prosignLegend = nullptr;
    QTextEdit *m_rxMainText = nullptr;
    QTextEdit *m_rxSubText = nullptr;

    int m_displayLength = 0; // character offsets here must match TextSendController's own count
    // Session dividers are text the dialog inserts on its own, which the controller never
    // counts. Every controller-reported offset is shifted by this to reach the right document
    // position — see recolorRange(). Only ever grows, and only at the end of the document.
    int m_displayOffset = 0;
    bool m_stalled = false; // blocks further typing/macros until abort() resets the dialog

    QString m_sessionLabel;          // "CW" / "AFSK" / "FSK" / "PSK"; empty until first applied
    bool m_prosignsEnabled = true;   // CW only — the prosign characters are literal in RTTY/PSK
    bool m_forceUppercase = true;    // false for PSK-D, where mixed case is normal
};

#endif // TEXTSENDDIALOG_H
