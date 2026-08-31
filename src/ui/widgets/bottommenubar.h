#ifndef BOTTOMMENUBAR_H
#define BOTTOMMENUBAR_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

/**
 * BottomMenuBar - Horizontal menu bar at bottom of QK4
 *
 * Contains 7 menu buttons: MENU, Fn, DISPLAY, BAND, MAIN RX, SUB RX, TX
 * Plus PTT button at far right with stretch separator.
 * (Icon buttons moved to SideControlPanel)
 *
 * Each menu button triggers a popup menu or panel when pressed.
 * PTT uses press/release for momentary microphone activation.
 * Styled with subtle rounded edges, gradient background, white border.
 */
class BottomMenuBar : public QWidget {
    Q_OBJECT

public:
    explicit BottomMenuBar(QWidget *parent = nullptr);
    ~BottomMenuBar() = default;

    // Getters for button positioning (for popup placement)
    QPushButton *bandButton() const { return m_bandBtn; }
    QPushButton *displayButton() const { return m_displayBtn; }
    QPushButton *fnButton() const { return m_fnBtn; }
    QPushButton *mainRxButton() const { return m_mainRxBtn; }
    QPushButton *subRxButton() const { return m_subRxBtn; }
    QPushButton *txButton() const { return m_txBtn; }
    QPushButton *pttButton() const { return m_pttBtn; }

    // What the far-right button is currently acting as.
    //   Voice — momentary mic PTT (press/release), the original behavior.
    //   Cw    — CW mode: the K4 is keyed via CAT, not mic audio.
    //   Fsk   — a text data sub-mode (AFSK-A / FSK-D / PSK-D): same story, the
    //           K4 generates the signal from KY text.
    // In both non-Voice modes the button becomes a launcher for the text send
    // dialog and a plain click emits textSendRequested().
    enum class TextMode { Voice, Cw, Fsk };

public slots:
    void setMenuActive(bool active);    // Toggle MENU button inverse colors
    void setDisplayActive(bool active); // Toggle DISPLAY button inverse colors
    void setBandActive(bool active);    // Toggle BAND button inverse colors
    void setFnActive(bool active);      // Toggle Fn button inverse colors
    void setMainRxActive(bool active);  // Toggle MAIN RX button inverse colors
    void setSubRxActive(bool active);   // Toggle SUB RX button inverse colors
    void setTxActive(bool active);      // Toggle TX button inverse colors
    void setPttActive(bool active);     // Toggle PTT button inverse colors

    // Relabels the far-right button and switches its click behavior. The label is supplied by
    // the caller rather than derived here, so the Fsk case can show the actual sub-mode
    // ("AFSK" / "FSK" / "PSK") without this widget having to know about RadioState. If the
    // right-click PTT latch is engaged at the moment this leaves Voice, it is force-released
    // first so the radio can't be stranded in TX with no way to drop it (mirrors CwController's
    // V1.4 PTT-destination cleanup on mode change — same class of hazard).
    void setTextMode(TextMode mode, const QString &label);

signals:
    void menuClicked();
    void fnClicked();
    void displayClicked();
    void bandClicked();
    void mainRxClicked();
    void subRxClicked();
    void txClicked();
    void pttPressed();  // PTT button pressed (start TX audio)
    void pttReleased(); // PTT button released (stop TX audio)
    void textSendRequested(); // CW/FSK button clicked — open the text send dialog

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    QPushButton *createMenuButton(const QString &text);

    // Menu buttons
    QPushButton *m_menuBtn;
    QPushButton *m_fnBtn;
    QPushButton *m_displayBtn;
    QPushButton *m_bandBtn;
    QPushButton *m_mainRxBtn;
    QPushButton *m_subRxBtn;
    QPushButton *m_txBtn;
    QPushButton *m_pttBtn;

    bool m_pttLocked = false;
    TextMode m_textMode = TextMode::Voice;
    QTimer *m_pttLockTimer = nullptr;
};

#endif // BOTTOMMENUBAR_H
