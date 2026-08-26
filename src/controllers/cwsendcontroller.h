#ifndef CWSENDCONTROLLER_H
#define CWSENDCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// =============================================================================
// CwSendController — types text into the K4's own CW/DATA text buffer (KY)
// =============================================================================
//
// Distinct from CwController: that class orchestrates hardware-driven keying
// (paddle/straight key) on tight latency budgets across multiple threads. This
// controller drives the K4's `KY*[text];` buffer instead — the radio renders
// the text to CW itself, so there is no local Morse-timing engine here. It
// lives entirely on the main thread and is deliberately decoupled from
// ConnectionController/TcpClient: it only emits `sendCatRequested(QString)`
// and expects `onCatResponse()`/`onDisconnected()` to be fed from outside
// (MainWindow wires these to ConnectionController). This keeps the class
// testable with plain Qt6::Core — no network stack needed to drive it with
// synthetic KY0/KY1 strings.
//
// Confirmation model
// ------------------
// The K4 protocol's only real feedback is `KY;` -> `KY0;`/`KY1;` (buffer
// empty / has data) — whole-buffer granularity, not per-character. To turn
// that into meaningful progress, typed text is cut into chunks (one word in
// word-complete mode, one merged run of characters in immediate mode) and
// sent STRICTLY SERIALLY: only one chunk is ever outstanding in the K4's
// buffer at a time, and the next chunk is not sent until `KY0` confirms the
// previous one landed. This is deliberately conservative — it works
// correctly regardless of whether the K4 appends or replaces on a second
// `KY` send while busy (that behavior has not been verified against real
// hardware). Once verified, word-complete mode could pipeline one chunk
// ahead to close the small inter-chunk gap this causes; see the "Step 0"
// protocol probe in the CW Send Dialog plan.
//
// A chunk that never gets its `KY0` within a speed-derived deadline is
// marked stalled: the queue is dropped (nothing is trusted to still be
// sittable in the K4's buffer once the link looks unhealthy), the active
// gate drops so hardware-driven CW un-suppresses itself, and the pipeline
// stays halted — no silent retry — until abort() or setCwModeActive(true)
// resets it.
//
// The first `KY;` poll after a dispatch is deliberately delayed longer than
// later ones (kFirstPollDelayMs vs kPollIntervalMs): querying too soon after
// the write risks reading a stale "buffer empty" status that predates the
// K4 actually ingesting the text, which would confirm a chunk that was
// never really keyed. This is a heuristic, not a verified protocol
// guarantee — the "Step 0" protocol probe in the CW Send Dialog plan should
// confirm whether it's necessary/sufficient on real hardware.
//
// Also gated on CW mode via setCwModeActive(): the dialog is modeless and
// can outlive a mode change, so this mirrors the mode check every
// hardware-driven CW path in cwcontroller.cpp already does — leaving CW
// resets the pipeline rather than continuing to push KY text at a
// phone-mode radio.
// =============================================================================

class CwSendController : public QObject {
    Q_OBJECT

public:
    explicit CwSendController(QObject *parent = nullptr);

    // Feeds one typed character into the pipeline. In word-complete mode a
    // chunk is cut (and dispatch attempted) on space/CR/LF; in immediate
    // mode every character is its own chunk, merged with any others still
    // queued at the moment it's actually dispatched. No-ops while stalled.
    void appendChar(QChar ch);

    // Cuts and dispatches whatever's been typed since the last boundary,
    // even without a trailing space — e.g. a manual "Send" action, or
    // finalizing a trailing word before closing the dialog.
    void flush();

    // Clears all queued/pending/in-flight state, sends "RX;" to force the
    // K4 out of TX immediately, and resets to idle. This is the panic
    // button — it does not try to gracefully finish what was in flight.
    void abort();

    void setImmediateMode(bool immediate);
    bool immediateMode() const { return m_immediateMode; }

public slots:
    // Feed raw CAT responses here (connect from ConnectionController::catResponseReceived).
    // Filters for bare "KY0"/"KY1" tokens; everything else is ignored.
    void onCatResponse(const QString &response);

    // Call when the radio link drops (e.g. ConnectionController::connectionStateChanged
    // reports Disconnected). Same reset as abort() but does not attempt to send "RX;"
    // over a dead link, and does not fire it at all if nothing was in progress.
    void onDisconnected();

    // Keeps the stall-timeout estimate honest as the operator's WPM changes.
    void setKeyerSpeed(int wpm);

    // Wire to RadioState::modeChanged (CW/CW_R -> true, everything else -> false). appendChar()/
    // flush() no-op while false; going false while something is in progress resets exactly like
    // abort(), minus the "RX;" (leaving CW mode is the operator's own doing, not a fault to
    // panic-stop over).
    void setCwModeActive(bool active);

signals:
    // Emit whatever CAT string needs to go to the K4 — caller wires this to
    // ConnectionController::sendCAT.
    void sendCatRequested(const QString &command);

    // Character ranges [start, start+length) in the operator's typed text,
    // for the dialog to recolor. Queued/not-yet-sent text is the dialog's
    // own concern (it's whatever hasn't had one of these signals fired for
    // it yet); this class only reports on chunks once they're dispatched.
    void chunkInFlight(int start, int length);
    void chunkConfirmed(int start, int length);
    void chunkStalled(int start, int length);

    // Fires after abort(), onDisconnected(), or setCwModeActive(false) actually reset
    // non-idle state (in-flight/queued/pending, or a still-showing stalled chunk).
    void aborted();

    // True while anything is queued, pending, or in flight. Wire this to
    // ConnectionController::setTextSendActive() so hardware-driven CW
    // (paddle/straight key) can suppress itself while a text send is live.
    void activeChanged(bool active);

private slots:
    void pollTick();
    void onChunkTimeout();

private:
    struct Chunk {
        QString text;
        int start = 0;
        int length = 0;
        bool immediate = false;
    };

    void cutPendingChunk();
    void maybeDispatchNext();
    void startTimeoutForCurrentChunk();
    void resetAll();
    void updateActiveState();

    QVector<Chunk> m_queue;
    QString m_pendingText;
    int m_pendingStart = 0;

    Chunk m_inFlight;
    bool m_hasInFlight = false;
    bool m_stalled = false;
    bool m_immediateMode = false;
    bool m_cwModeActive = false;
    bool m_active = false;
    int m_wpm = 25;

    QTimer *m_pollTimer;
    QTimer *m_timeoutTimer;

    static constexpr int kFirstPollDelayMs = 300;
    static constexpr int kPollIntervalMs = 120;
    // KY command text limit is 60 chars (K4 Programmer's Reference); reserve 1 for the
    // sacrificial lead-in character prepended at send time (see maybeDispatchNext()).
    static constexpr int kMaxChunkChars = 59;
    static constexpr int kTimeoutSlackFactor = 4;
    static constexpr int kMinTimeoutMs = 800;
    static constexpr int kMaxTimeoutMs = 20000;
};

#endif // CWSENDCONTROLLER_H
