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

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override; // Escape aborts sending instead of closing
    bool eventFilter(QObject *watched, QEvent *event) override; // double-click-to-grab on RX panes

private:
    // `force` bypasses the hold-until-Enter gate — see onReturnPressed(). Returns false when
    // the text was NOT accepted (stalled, or held), so callers know not to clear the input:
    // clearing text that was never queued destroys it silently.
    bool commitText(const QString &text, bool force = false);
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
    // Which pane sent text currently goes to: the transmitting VFO, same rule as everywhere else.
    bool activeTxIsSub() const;
    QTextEdit *activeTxPane() const;
    int &activeTxLen();
    // Opens a segment for the active pane if the last one is for the other pane, or if text the
    // controller doesn't count has just been inserted.
    void openTxSegmentIfNeeded(bool force);
    void updateTxHeaders();
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
    // Sent text, split per transmitting VFO so it is always obvious which frequency a line went
    // out on. Only one is ever being written to at a time — whichever VFO currently transmits.
    QTextEdit *m_txMainText = nullptr;
    QTextEdit *m_txSubText = nullptr;
    QWidget *m_txPaneContainer = nullptr;
    QLabel *m_txMainHeader = nullptr; // "Main  14.097.500"
    QLabel *m_txSubHeader = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_abortBtn = nullptr;
    QComboBox *m_sendModeCombo = nullptr; // character / word / Enter — see RadioSettings::TextSendMode
    QLabel *m_stalledBanner = nullptr;
    QLabel *m_vfoAFreqLabel = nullptr; // flanking the strip, as the main display places them
    QLabel *m_vfoBFreqLabel = nullptr;
    QLabel *m_vfoAModeLabel = nullptr; // mode under the A square, as on the K4's own display
    QLabel *m_vfoBModeLabel = nullptr;
    QPushButton *m_txSideBtn = nullptr; // the TX arrow; clickable only when both VFOs match
    QLabel *m_splitLabel = nullptr;     // SPLIT ON/OFF under the arrow, as on the main display
    QLabel *m_subBadge = nullptr;       // SUB / DIV stack right of the B square, as on the main display
    QLabel *m_divBadge = nullptr;
    bool m_txSwitchable = false; // both VFOs in the same mode — see updateVfoStrip()
    VfoSquareWidget *m_vfoASquare = nullptr; // recolored red while this VFO is transmitting
    VfoSquareWidget *m_vfoBSquare = nullptr;
    QVector<QPushButton *> m_macroButtons;

    QWidget *m_rxPaneContainer = nullptr; // holds legend + both RX panes; hidden when neither decode is running
    QLabel *m_prosignLegend = nullptr;
    QTextEdit *m_rxMainText = nullptr;
    QTextEdit *m_rxSubText = nullptr;

    // TextSendController reports confirmed/stalled ranges as offsets into one monotonic stream
    // of the operator's own characters. That stream is rendered across two panes, and the
    // dialog also inserts text of its own (session dividers) that the controller never counts,
    // so a range can't be used as a document position directly. A segment records where each
    // contiguous run landed: everything from controllerStart onwards went into pane isSub
    // starting at docStart, with nothing inserted in between. A new segment opens whenever the
    // transmitting VFO changes or a divider is inserted, which is what keeps runs contiguous.
    struct TxSegment {
        int controllerStart = 0;
        bool isSub = false;
        int docStart = 0;
    };
    QVector<TxSegment> m_txSegments;
    int m_typedCount = 0;  // operator characters committed so far == the controller's offset
    int m_txMainLen = 0;   // document length of each pane, tracked rather than queried
    int m_txSubLen = 0;
    bool m_stalled = false; // blocks further typing/macros until abort() resets the dialog

    QString m_sessionLabel;          // "CW" / "AFSK" / "FSK" / "PSK"; empty until first applied
    bool m_prosignsEnabled = true;   // CW only — the prosign characters are literal in RTTY/PSK
    bool m_forceUppercase = true;    // false for PSK-D, where mixed case is normal
};

#endif // TEXTSENDDIALOG_H
