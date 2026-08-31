#ifndef TEXTDECODECONTROLLER_H
#define TEXTDECODECONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class TextDecodeWindow;
class QWidget;

// Owns the two floating text-decode windows (Main RX + Sub RX), their CAT
// dispatch keeping the K4 in sync with user-driven config changes, and the
// RadioState observers that keep the windows' operating mode and data rate
// in sync with the radio. Replaces ~180 lines of inline wiring + two
// ~30-line popup-button handler blocks in MainWindow.
//
// See PATTERNS.md → Controller Pattern. Constructor takes RadioState and
// ConnectionController by pointer (not owned); parentWidget is used as the
// Qt parent for the two TextDecodeWindow instances so Qt's parent-based
// deletion handles their lifetime.
class TextDecodeController : public QObject {
    Q_OBJECT

public:
    explicit TextDecodeController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                                  QObject *parent = nullptr);
    ~TextDecodeController() override;

    // Task-level API — show the window configured for the current radio
    // mode + data sub-mode, enable decode if not already enabled.
    // Called from the Main RX / Sub RX popup "TEXT DECODE" button handlers.
    void showMainRx();
    void showSubRx();

    // Turns the K4's text decoder on for one receiver while it sits in an FSK sub-mode, and
    // back off when it leaves — so the text send dialog's RX panes have something to show
    // without the operator hunting for the TEXT DECODE button first. Deliberately does NOT
    // pop the floating window; the dialog renders the same stream itself.
    //
    // Only ever undoes what it did: nothing happens if that receiver's decoder was already
    // running when FSK was entered, and the undo is dropped the moment the operator toggles
    // decode by hand. Call once per receiver whenever either one's mode or sub-mode changes.
    void applyFskAutoDecode(bool isMainRx, bool fskActive);

    // Link down: forget both pending undos without sending anything. Whatever the radio's
    // decoder state was is now the radio's business, and there is no socket to change it over.
    void onDisconnected();

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    TextDecodeWindow *m_mainWindow;     // owned via Qt parent mechanism
    TextDecodeWindow *m_subWindow;      // owned via Qt parent mechanism

    void sendTextDecodeCmd(TextDecodeWindow *window, bool isMainRx);
    // Pushes one receiver's mode / data sub-mode / data rate into its window. Not cosmetic:
    // sendTextDecodeCmd() reads the TD mode digit back out of operatingMode().
    void syncWindowToRadio(bool isMainRx);
    void noteManualDecodeChange(bool isMainRx);

    // Set while applyFskAutoDecode() is driving a window, so the enabledChanged handler can
    // tell our own toggle from the operator's.
    bool m_applyingAutoDecode = false;
    // Set while sendTextDecodeCmd() pushes its own values into RadioState. Those setters emit
    // textDecodeChanged synchronously, and the handler's job is to sync the window FROM the
    // radio — here it would be writing our own values back into the window they came from,
    // reading fields not yet updated on the way. See sendTextDecodeCmd().
    bool m_applyingOptimisticState = false;
    bool m_autoEnabledMain = false; // we turned Main RX decode on and owe it an off
    bool m_autoEnabledSub = false;  // ditto Sub RX
};

#endif // TEXTDECODECONTROLLER_H
