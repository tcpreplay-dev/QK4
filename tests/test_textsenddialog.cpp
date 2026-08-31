// Construction/refresh smoke tests for TextSendDialog.
//
// These exist because a null-pointer crash shipped: updateVfoStrip() was called partway through
// the constructor, and it fills the TX pane headers — which are built further down. Nothing in
// the suite constructed a widget, so nothing caught it. The point of these tests is not to
// assert on layout, which would be brittle; it is to actually build the dialog and drive the
// RadioState signals it observes, so an ordering or lifetime mistake segfaults the test run
// rather than the operator's radio.
//
// Runs under the offscreen platform plugin — no display needed, so it works in CI.

#include "controllers/textsendcontroller.h"
#include "models/radiostate.h"
#include "ui/dialogs/textsenddialog.h"

#include <QTextEdit>
#include <QtTest/QtTest>

class TestTextSendDialog : public QObject {
    Q_OBJECT

private slots:
    // The crash that prompted these tests: constructing the dialog at all.
    void constructsWithoutCrashing() {
        RadioState state;
        TextSendController controller;
        TextSendDialog dialog(&controller, &state, nullptr);
        QVERIFY(true); // reaching here at all is the assertion
    }

    // Every RadioState signal the dialog observes, fired at a freshly built dialog. Each one
    // reaches widgets built at different points in the constructor.
    void survivesRadioStateUpdates() {
        RadioState state;
        TextSendController controller;
        TextSendDialog dialog(&controller, &state, nullptr);

        state.parseCATCommand("MD6;");   // DATA
        state.parseCATCommand("DT2;");   // FSK-D
        state.parseCATCommand("MD$6;");  // sub RX to DATA too
        state.parseCATCommand("DT$2;");
        state.parseCATCommand("SB1;");   // sub RX on
        state.parseCATCommand("FA00014097500;");
        state.parseCATCommand("FB00014055200;");
        state.parseCATCommand("TD110;");  // decode on, main
        state.parseCATCommand("TD$110;"); // decode on, sub
        state.parseCATCommand("TB000CQ DE VE7VT;");
        state.parseCATCommand("TB$000CQ TEST;");
        QVERIFY(true);
    }

    // A CW <-> FSK switch inserts a divider and opens a new offset segment; a TX-side switch
    // moves sent text to the other pane. Both rewrite the mapping the recolor path depends on,
    // so drive text through them in sequence and make sure nothing dereferences off the end.
    void sessionAndTxSideSwitchesKeepOffsetsSane() {
        RadioState state;
        TextSendController controller;
        TextSendDialog dialog(&controller, &state, nullptr);

        state.parseCATCommand("MD3;"); // CW
        controller.setSessionMode(TextSendController::SessionMode::Cw);
        dialog.applySessionMode();
        for (QChar ch : QStringLiteral("CQ "))
            controller.appendChar(ch);
        controller.onCatResponse("KY0;");

        state.parseCATCommand("MD6;");
        state.parseCATCommand("DT2;");
        controller.setSessionMode(TextSendController::SessionMode::Fsk);
        dialog.applySessionMode();

        state.parseCATCommand("FT1;"); // TX moves to VFO B — sent text goes to the other pane
        state.parseCATCommand("FT0;"); // ...and back
        QVERIFY(true);
    }
    // A message-memory playback carries no text over CAT — the K4 renders its own TX row
    // locally — so the dialog marks it from the TB TX-buffer level instead, labelled with the
    // button QK4 last tapped.
    void messageMemoryPlaybackIsMarkedInTheTxPane() {
        RadioState state;
        TextSendController controller;
        TextSendDialog dialog(&controller, &state, nullptr);
        state.parseCATCommand("MD6;");
        state.parseCATCommand("DT2;");
        controller.setSessionMode(TextSendController::SessionMode::Fsk);
        dialog.applySessionMode();

        dialog.noteMessageMemoryPressed(1);
        state.parseCATCommand("TB100;"); // radio starts transmitting buffered text

        QVERIFY(paneContains(dialog, "M1 message sent"));
    }

    // The same level rise happens for our own sends, and must NOT be marked — otherwise every
    // typed transmission would be annotated as if the radio had originated it.
    void ownSendIsNotMarkedAsARadioMessage() {
        RadioState state;
        TextSendController controller;
        TextSendDialog dialog(&controller, &state, nullptr);
        state.parseCATCommand("MD3;"); // CW: no TX bracket to complicate the active window
        controller.setSessionMode(TextSendController::SessionMode::Cw);
        dialog.applySessionMode();

        for (QChar ch : QStringLiteral("CQ "))
            controller.appendChar(ch);
        QVERIFY(controller.isActive());

        state.parseCATCommand("TB100;");
        QVERIFY(!paneContains(dialog, "message sent"));
    }

private:
    static bool paneContains(const TextSendDialog &dialog, const QString &needle) {
        const auto panes = dialog.findChildren<QTextEdit *>();
        for (QTextEdit *pane : panes) {
            if (pane->toPlainText().contains(needle))
                return true;
        }
        return false;
    }
};

QTEST_MAIN(TestTextSendDialog)
#include "test_textsenddialog.moc"
