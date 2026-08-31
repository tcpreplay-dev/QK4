#ifndef TEXTSENDCONTROLLER_H
#define TEXTSENDCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// =============================================================================
// TextSendController — types text into the K4's own CW/DATA text buffer (KY)
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
// stays halted — no silent retry — until abort() or setSessionMode()
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
// Also gated on the radio's mode via setSessionMode(): the dialog is
// modeless and can outlive a mode change, so this mirrors the mode check
// every hardware-driven CW path in cwcontroller.cpp already does — leaving
// CW (or an FSK sub-mode) resets the pipeline rather than continuing to
// push KY text at a phone-mode radio.
//
// CW vs FSK
// ---------
// The K4 renders KY text as CW or as the active DATA sub-mode's signal, so
// the queue, the chunking and the KY0 confirmation model are shared. Three
// things differ, all driven off SessionMode:
//   * Lead-in. CW prepends a sacrificial 'E' to absorb TX ramp-up (see
//     maybeDispatchNext()); FSK prepends nothing — the signal is generated
//     digitally, and any character we invented would print as itself on the
//     far end.
//   * TX bracket. FSK wraps a transmission in TX; / RX; with a settle delay
//     after the TX; and a hang delay before the RX;. Whether the K4 also
//     auto-keys on KY in data modes the way it does in CW is unverified; the
//     bracket is correct either way, and if it turns out to be redundant it
//     comes back out. CW sends no bracket.
//   * Stall timeout. Derived from keyer WPM for CW, from the DR data rate
//     for FSK.
// =============================================================================

class TextSendController : public QObject {
    Q_OBJECT

public:
    // Which kind of text session the radio is currently in. The K4 renders KY
    // text as CW or as the active DATA sub-mode's signal, so the pipeline is
    // the same either way — what differs is the per-chunk lead-in, the
    // stall-timeout arithmetic, and whether transmissions need a TX;/RX;
    // bracket around them.
    //   None — no text mode active; appendChar()/flush() no-op.
    //   Cw   — MD3 / MD7.
    //   Fsk  — MD6 / MD9 with DT 1 (AFSK-A), 2 (FSK-D) or 3 (PSK-D).
    //          DT 0 (DATA-A) is NOT this: that sub-mode exists for external
    //          software, and the K4 neither encodes nor decodes text in it.
    enum class SessionMode { None, Cw, Fsk };

    explicit TextSendController(QObject *parent = nullptr);

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

    // Keeps the stall-timeout estimate honest as the operator's WPM changes (CW sessions).
    void setKeyerSpeed(int wpm);

    // DR command value for the active receiver: 0 = RTTY45/PSK31, 1 = RTTY75/PSK63. Same job
    // as setKeyerSpeed() but for FSK sessions — it only feeds the stall-timeout estimate.
    void setDataRate(int rate);

    // Driven from MainWindow's mode fan-in (mode/modeB/dataSubMode/dataSubModeB/bSet).
    // appendChar()/flush() no-op while None; any change while something is in progress resets
    // exactly like abort(), minus the panic "RX;" — a mode change is the operator's own doing,
    // not a fault. An FSK transmission we opened with "TX;" is still closed with "RX;" first,
    // though: whoever keyed the radio owns un-keying it.
    void setSessionMode(SessionMode mode);
    SessionMode sessionMode() const { return m_sessionMode; }

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

    // Fires after abort(), onDisconnected(), or setSessionMode() actually reset
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

    // How many typed characters fit in one KY send, after the lead-in's share of the
    // 60-char limit is taken out. CW spends one on the sacrificial 'E'; FSK spends none.
    int maxChunkChars() const { return kKyTextLimit - m_leadIn.length(); }

    // TX bracket, FSK only — no-ops in a CW session, where KY keys and un-keys the radio by
    // itself. beginTransmission() emits "TX;" and starts the settle delay; endTransmission()
    // emits "RX;" unless the link is known dead, and is safe to call with no bracket open.
    void beginTransmission();
    void endTransmission(bool linkAlive);
    void armTxHangIfIdle();

    QVector<Chunk> m_queue;
    QString m_pendingText;
    int m_pendingStart = 0;

    Chunk m_inFlight;
    bool m_hasInFlight = false;
    bool m_stalled = false;
    bool m_immediateMode = false;
    SessionMode m_sessionMode = SessionMode::None;
    QString m_leadIn;
    bool m_active = false;
    int m_wpm = 25;
    int m_dataRate = 0;

    // TX bracket state (FSK). m_txOpen means we sent "TX;" and still owe an "RX;".
    bool m_txOpen = false;
    bool m_awaitingSettle = false;

    QTimer *m_pollTimer;
    QTimer *m_timeoutTimer;
    QTimer *m_txSettleTimer;
    QTimer *m_txHangTimer;

    static constexpr int kFirstPollDelayMs = 300;
    static constexpr int kPollIntervalMs = 120;
    // KY command text limit (K4 Programmer's Reference), shared by the typed text and
    // whatever lead-in gets prepended at send time — see maxChunkChars().
    static constexpr int kKyTextLimit = 60;
    static constexpr int kTimeoutSlackFactor = 4;
    static constexpr int kMinTimeoutMs = 800;
    static constexpr int kMaxTimeoutMs = 20000;
    // Gap between "TX;" and the first KY chunk, so the K4 is up and diddling before real
    // text starts — the FSK counterpart to CW's sacrificial 'E', without putting a stray
    // character on the air.
    static constexpr int kTxSettleMs = 100;
    // How long the bracket stays open with nothing queued before "RX;" goes out. Must
    // comfortably exceed the inter-word gap of a normal typist, or word-complete mode would
    // key and un-key the radio once per word.
    static constexpr int kTxHangMs = 700;
    // Per-character airtime estimates for the FSK stall timeout: Baudot is 7.5 bits/char at
    // 45.45 or 75 baud, PSK varicode averages ~6 bits/char at 31.25 or 62.5 baud. One
    // conservative number per DR setting covers both.
    static constexpr double kFskSlowCharMs = 190.0; // RTTY45 / PSK31
    static constexpr double kFskFastCharMs = 100.0; // RTTY75 / PSK63
};

#endif // TEXTSENDCONTROLLER_H
