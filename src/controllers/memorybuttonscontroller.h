#ifndef MEMORYBUTTONSCONTROLLER_H
#define MEMORYBUTTONSCONTROLLER_H

#include <QObject>

class ConnectionController;
class QPushButton;

// Owns the wiring for the K4 message memory buttons that sit just under
// the VFO row: M1-M4 (CW/voice/data message recall), REC (record), STORE
// (store / save), RCL (recall). Each has a left-click action (the K4's
// primary SW command) and REC/STORE/RCL each have a right-click action
// installed via an event filter on the controller — the right-click
// sends an alternate SW command (BANK / AF REC / AF PLAY).
//
// Extracted from MainWindow as part of the Phase A architectural pass —
// the eventFilter cases for these three buttons used to live in
// MainWindow::eventFilter alongside unrelated cases, breaking the
// principle that MainWindow only routes events to controllers (not
// implements them).
class MemoryButtonsController : public QObject {
    Q_OBJECT

public:
    MemoryButtonsController(ConnectionController *connection, QPushButton *m1, QPushButton *m2, QPushButton *m3,
                            QPushButton *m4, QPushButton *rec, QPushButton *store, QPushButton *rcl,
                            QObject *parent = nullptr);
    ~MemoryButtonsController() override;

signals:
    // M1-M4 was clicked here (1-4). These are bare front-panel switch taps with no CAT echo, so
    // this is the only way anything downstream can know which memory the operator started —
    // and only when they started it from QK4 rather than the radio's own panel.
    void messageMemoryPressed(int index);

protected:
    // Catches right-clicks on REC / STORE / RCL — sends the alternate SW
    // command. Other events fall through unmodified.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ConnectionController *m_connection; // injected, not owned
    QPushButton *m_recBtn;              // not owned (parent-managed)
    QPushButton *m_storeBtn;            // not owned
    QPushButton *m_rclBtn;              // not owned
};

#endif // MEMORYBUTTONSCONTROLLER_H
