// Behavioral tests for TextSendController — the state machine that turns typed text into
// K4 `KY*[text];` sends, confirmed strictly serially by polling `KY;` for `KY0`/`KY1`.
//
// TextSendController is deliberately decoupled from ConnectionController/TcpClient (see its
// header): it only emits sendCatRequested(QString) and expects onCatResponse()/
// onDisconnected() fed from outside. That means these tests drive it exactly like a real
// K4 link would — feeding synthetic "KY0;"/"KY1;" strings back in — without any network
// stack or live radio.

#include "controllers/textsendcontroller.h"
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestTextSendController : public QObject {
    Q_OBJECT

private slots:
    // Nothing sends until the radio is confirmed in CW mode — the dialog is modeless and can
    // outlive a mode change, so this must not depend on the dialog closing itself.
    void appendCharNoOpsUntilCwModeConfirmed() {
        TextSendController ctrl;
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 0);

        ctrl.setCwModeActive(true);
        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 1);
    }

    // Leaving CW mode mid-send resets the pipeline (like abort()) but must NOT send "RX;" —
    // that's the operator's own mode change, not a fault to panic-stop over.
    void leavingCwModeResetsWithoutSendingRx() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true);
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        QSignalSpy aborted(&ctrl, &TextSendController::aborted);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        const int sentBeforeModeChange = sent.count();

        ctrl.setCwModeActive(false);
        QCOMPARE(aborted.count(), 1);
        QCOMPARE(sent.count(), sentBeforeModeChange); // no "RX;" sent

        // Still inert while mode is off.
        for (QChar ch : QStringLiteral("DE "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), sentBeforeModeChange);
    }

    // Typing a word without a trailing boundary character sends nothing yet — word-complete
    // mode (the default) only cuts a chunk on space/CR/LF.
    void wordCompleteWaitsForBoundary() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        for (QChar ch : QStringLiteral("HELLO"))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 0);
    }

    // The first KY; poll after a dispatch is delayed longer than steady-state cadence — see
    // the header note on why querying too soon after the write is risky. This only checks
    // the delay is real, not the exact hardware-dependent duration.
    void firstPollIsDelayedLongerThanStandardCadence() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true);
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 1); // just the KY write, no poll yet

        QTest::qWait(150); // well under the first-poll delay
        QCOMPARE(sent.count(), 1);

        QVERIFY(sent.wait(2000)); // the delayed first "KY;" poll eventually goes out
        QCOMPARE(sent.count(), 2);
        QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("KY;"));
    }

    // A completed word sends exactly one KY chunk and reports it in-flight; it only reports
    // confirmed once a real KY0 arrives, never before.
    void wordCompleteSendsOnBoundaryAndConfirmsOnKY0() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        QSignalSpy inFlight(&ctrl, &TextSendController::chunkInFlight);
        QSignalSpy confirmed(&ctrl, &TextSendController::chunkConfirmed);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);

        QCOMPARE(sent.count(), 1);
        QVERIFY(sent.at(0).at(0).toString().startsWith(QStringLiteral("KYECQ ")));
        QCOMPARE(inFlight.count(), 1);
        QCOMPARE(inFlight.at(0).at(0).toInt(), 0);
        QCOMPARE(inFlight.at(0).at(1).toInt(), 3);
        QCOMPARE(confirmed.count(), 0);

        ctrl.onCatResponse("KY1;");
        QCOMPARE(confirmed.count(), 0); // still busy — must not confirm on KY1

        ctrl.onCatResponse("KY0;");
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.at(0).at(0).toInt(), 0);
        QCOMPARE(confirmed.at(0).at(1).toInt(), 3);
    }

    // Strictly serial: a second word typed while the first is still in flight must not be
    // sent until the first is confirmed.
    void secondChunkWaitsForFirstConfirm() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 1); // just the KY send, no poll yet in this count path

        for (QChar ch : QStringLiteral("DE "))
            ctrl.appendChar(ch);
        // "DE " is queued, not dispatched — sendCatRequested should not have grown from a
        // second KY send yet (only polls, if any arrived, would add to this spy).
        int sendsBeforeConfirm = sent.count();

        ctrl.onCatResponse("KY0;");
        QVERIFY(sent.count() > sendsBeforeConfirm); // the second chunk went out after confirm
        const QString secondSend = sent.at(sent.count() - 1).at(0).toString();
        QVERIFY(secondSend.startsWith(QStringLiteral("KYEDE ")));
    }

    // Immediate mode cuts a chunk per character but merges whatever's queued at dispatch time
    // into one send — confirming the whole outstanding run at once, not per character.
    void immediateModeMergesQueuedCharsIntoOneSend() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        ctrl.setImmediateMode(true);
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        QSignalSpy inFlight(&ctrl, &TextSendController::chunkInFlight);

        // First char dispatches immediately (nothing else queued yet).
        ctrl.appendChar('A');
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("KYEA;"));

        // Typed while 'A' is still unconfirmed — must queue, not send yet.
        ctrl.appendChar('B');
        ctrl.appendChar('C');
        QCOMPARE(sent.count(), 1);

        ctrl.onCatResponse("KY0;");
        QCOMPARE(sent.count(), 2);
        QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("KYEBC;"));
        QCOMPARE(inFlight.at(1).at(0).toInt(), 1); // starts right after 'A'
        QCOMPARE(inFlight.at(1).at(1).toInt(), 2); // covers 'B' and 'C'
    }

    // A chunk that never gets its KY0 must be marked stalled, not silently confirmed, and
    // must stop the pipeline (no further sends) until abort() clears it.
    void timeoutMarksChunkStalledAndHaltsPipeline() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        ctrl.setKeyerSpeed(60); // fast WPM keeps the derived timeout short but still bounded
        QSignalSpy stalled(&ctrl, &TextSendController::chunkStalled);
        QSignalSpy confirmed(&ctrl, &TextSendController::chunkConfirmed);

        for (QChar ch : QStringLiteral("K "))
            ctrl.appendChar(ch);
        QVERIFY(stalled.wait(25000));
        QCOMPARE(confirmed.count(), 0);

        // Pipeline halted: further typed text must not dispatch until abort() resets it.
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        for (QChar ch : QStringLiteral("MORE "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 0);

        ctrl.abort();
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.last().at(0).toString(), QStringLiteral("RX;"));
        for (QChar ch : QStringLiteral("OK "))
            ctrl.appendChar(ch);
        QCOMPARE(sent.count(), 2); // RX; from abort(), then the new KYOK ; send
    }

    // abort() clears everything queued/pending and forces the K4 out of TX via RX;.
    void abortClearsQueueAndSendsRx() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        QSignalSpy aborted(&ctrl, &TextSendController::aborted);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        for (QChar ch : QStringLiteral("DE ")) // queued behind the in-flight chunk
            ctrl.appendChar(ch);

        ctrl.abort();
        QCOMPARE(aborted.count(), 1);
        QVERIFY(sent.last().at(0).toString() == QStringLiteral("RX;"));

        // Nothing left to confirm — a stray KY0 arriving late must be a no-op, not a crash.
        ctrl.onCatResponse("KY0;");
    }

    // activeChanged tracks whether anything is queued/pending/in-flight — this drives the
    // hardware-CW suppression gate, so it must go true on the first send and false once
    // everything drains.
    void activeChangedTracksQueueLifecycle() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy active(&ctrl, &TextSendController::activeChanged);

        for (QChar ch : QStringLiteral("HI "))
            ctrl.appendChar(ch);
        QCOMPARE(active.count(), 1);
        QCOMPARE(active.at(0).at(0).toBool(), true);

        ctrl.onCatResponse("KY0;");
        QCOMPARE(active.count(), 2);
        QCOMPARE(active.at(1).at(0).toBool(), false);
    }

    // A disconnect mid-send must abort exactly like the manual abort path, but must not try
    // to send "RX;" over a dead link.
    void disconnectAbortsWithoutSendingRx() {
        TextSendController ctrl;
        ctrl.setCwModeActive(true); // appendChar()/flush() no-op until the radio is confirmed in CW mode
        QSignalSpy sent(&ctrl, &TextSendController::sendCatRequested);
        QSignalSpy aborted(&ctrl, &TextSendController::aborted);

        for (QChar ch : QStringLiteral("CQ "))
            ctrl.appendChar(ch);
        const int sentBeforeDisconnect = sent.count();

        ctrl.onDisconnected();
        QCOMPARE(aborted.count(), 1);
        QCOMPARE(sent.count(), sentBeforeDisconnect); // no extra "RX;" sent

        // Idempotent — a second disconnect notification with nothing in progress is a no-op.
        ctrl.onDisconnected();
        QCOMPARE(aborted.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestTextSendController)
#include "test_textsendcontroller.moc"
