#ifndef CWCONTROLLER_H
#define CWCONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <atomic>

class RadioState;
class ConnectionController;
class IambicKeyer;
class SidetoneGenerator;
class HalikeyDevice;
class KpodPlusDevice;

// =============================================================================
// CwController — CW keying orchestration
// =============================================================================
//
// Purpose
// -------
// Pure orchestration layer that wires existing devices together for the CW
// use case. Owns no devices and no threads; HardwareController retains
// construction + thread management for KPOD, KPOD+, HaliKey, IambicKeyer
// (keyer thread), SidetoneGenerator (sidetone thread). CwController takes
// those devices as injected pointers and wires the CW-specific signal
// connections that used to crowd HardwareController.
//
// Pulling this out of HardwareController shrinks HW from ~558 LOC to
// ~150 LOC and makes the seam between "what is hardware" and "what is
// CW behavior" explicit. The riskiest single architectural change in
// the plan — see "Threading invariants" below.
//
// Construction order
// ------------------
// MainWindow constructs HardwareController first (devices + threads),
// then CwController, fetching the devices via HardwareController's
// Rule 2-documented accessors:
//
//     m_hardwareController = new HardwareController(rs, cc, this);
//     m_cwController = new CwController(
//         rs, cc,
//         m_hardwareController->iambicKeyer(),
//         m_hardwareController->sidetoneGenerator(),
//         m_hardwareController->halikeyDevice(),
//         m_hardwareController->kpodPlusDevice(),
//         this);
//
// Signals owned (moved from HardwareController)
// ---------------------------------------------
//   src                                  | dst                | thread hop                | conn
//   -------------------------------------|--------------------|---------------------------|--------
//   RadioState::keyerSpeedChanged        | sidetone + keyer + | main -> sidetone thread   | invokeMethod queued
//                                        | KZL to K4 +        | main -> keyer thread      |
//                                        | KPOD+ setKeyerSpd  | main -> I/O thread (sendCAT)
//   RadioState::keyerPaddleChanged       | keyer mode/reverse | main -> keyer thread      | invokeMethod queued
//                                        | + KPOD+ setKeyer-  |                           |
//                                        | Params             |                           |
//   RadioState::cwPitchChanged           | sidetone freq +    | main -> sidetone thread   | invokeMethod queued
//                                        | KPOD+ setCwPitch   |                           |
//   RadioState::modeChanged              | m_cachedMode +     | main -> main (atomic)     | AutoConnection (Direct)
//                                        | V1.4 PTT cleanup   |                           |
//   IambicKeyer::elementStarted          | KZ. / KZ- to K4    | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::characterSpace          | KZ space to K4     | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::restartAfterPause       | KZP%04d to K4      | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::elementStarted          | sidetone dit/dah   | keyer -> sidetone thread  | AutoConnection (Queued)
//   HalikeyDevice::ditStateChanged       | keyer setDitPaddle | HaliKey worker -> main    | DirectConnection
//   HalikeyDevice::dahStateChanged       | keyer setDahPaddle | HaliKey worker -> main    | DirectConnection
//                                        | or, in straight-key|                           |
//                                        | mode, KZD/U to K4  | HaliKey worker -> I/O     | sendCAT self-marshals
//   HalikeyDevice::pttStateChanged       | V1.4 demux:        | HaliKey worker -> main    | DirectConnection
//                                        |  CW -> dit paddle  |                           |
//                                        |  voice -> ptt      |                           |
//   HalikeyDevice::disconnected          | stop keyer         | main -> main              | AutoConnection
//   ConnectionController::radioReady     | keyer setEnabled t | main -> keyer thread      | invokeMethod queued
//   ConnectionController::connection-    | keyer setEnabled f | main -> keyer thread      | invokeMethod queued
//                       StateChanged     |                    |                           |
//   KpodPlusDevice::deviceConnected      | setKpodPlusKeyer-  | main -> main (atomic)     | AutoConnection
//                                        | Active(true)       |                           |
//   KpodPlusDevice::deviceDisconnected   | setKpodPlusKeyer-  | main -> main (atomic)     | AutoConnection
//                                        | Active(false)      |                           |
//   KpodPlusDevice::deviceInfoReady      | startPolling-side  | main -> main (atomic)     | AutoConnection
//                                        | preemptive set(t)  |                           |
//   KpodPlusDevice::keyerDataReceived    | TcpClient::send-   | EP02 worker -> I/O thread | QueuedConnection
//                                        | CATBytes           |                           |
//
// Gates & suppression
// -------------------
// The KPOD+ keyer-active gate (`ConnectionController::m_kpodPlusKeyerActive`,
// std::atomic<bool>) lives on ConnectionController because both CwController
// and the iambic-keyer-to-TcpClient lambdas on the I/O thread read it.
// CwController is the sole WRITER after the refactor (set on KPOD+
// deviceConnected / deviceInfoReady, cleared on deviceDisconnected); the
// iambic CAT lambdas and the HaliKey paddle handlers read with acquire
// ordering paired with CwController's release stores.
//
// When the gate is on, all locally-driven CW emissions are suppressed
// (the iambic state machine still runs; only its KZ output and sidetone
// playback drop). KPOD+ owns the entire chain when active.
//
// State moved from HardwareController
// -----------------------------------
//   std::atomic<int> m_cachedMode
//     Mirror of RadioState::mode written from the modeChanged AutoConnection
//     (resolves to Direct since both ends live on the main thread, so the
//     store runs synchronously with parseCATCommand). Read on the HaliKey
//     worker thread by the dit/dah/PTT DirectConnection handlers with
//     acquire ordering. Avoids a data race against parseCATCommand writing
//     the non-atomic subsystem field.
//
//   std::atomic<bool> m_cachedIsV14
//     Mirror of RadioSettings::halikeyDeviceType() != 1. Stored on the main
//     thread (ctor + halikeyDeviceTypeChanged, release); read on the HaliKey
//     worker thread by the PTT DirectConnection handler (acquire). Replaces
//     a racy worker-thread read of the plain int on the settings singleton.
//
//   enum V14PttDest { V14PttNone, V14PttDitPaddle, V14PttPtt };
//   std::atomic<int> m_v14PttDestination
//     V1.4 firmware multiplexes paddle-dit and foot-pedal on a single CTS
//     line. The pttStateChanged rising-edge handler picks a destination
//     (dit-paddle in CW, PTT in voice) and captures it here so:
//       - the falling edge dispatches to the SAME destination, even if
//         the mode changed mid-press;
//       - a mode change while held fires the matching up event to the
//         OLD destination so neither IambicKeyer nor MainWindow gets
//         stuck in a half-pressed state.
//     The cleanup path uses compare_exchange_strong so only one of
//     (falling-edge, mode-change) wins — no double release.
//
// Threading invariants (preserve verbatim)
// ----------------------------------------
//   1. HaliKey paddle handlers MUST stay DirectConnection on the HaliKey
//      worker thread. Anything else adds latency to CW keying.
//   2. m_cachedMode store happens on the main thread (AutoConnection
//      from RadioState::modeChanged resolves to Direct); load happens on
//      the HaliKey worker thread with acquire ordering. m_cachedIsV14
//      follows the same rule.
//   3. m_v14PttDestination's CAS-based cleanup must remain so the
//      mode-change-during-press path doesn't double-release with the
//      falling-edge path.
//   4. IambicKeyer signals route to TcpClient on the I/O thread via
//      QueuedConnection — keyer thread is high-priority, main thread is
//      bypassed entirely. Order preservation relies on Qt's FIFO event
//      queue between a single source thread and a single dest thread.
//   5. KPOD+ EP02 keyer data routes EP02 worker -> I/O thread via
//      QueuedConnection. Main thread is bypassed on the hot path.
//   6. The KPOD+ keyer-active gate must be set on deviceInfoReady (not
//      deviceConnected) so the ~10-100 ms open window doesn't leak
//      paddle events to the local sidetone path.
//   8. Straight-key timing is measured on the HaliKey worker thread but
//      carried inside the KZD/U command, so the send may marshal freely;
//      only the edge timestamps must stay precise.
//   7. Destructor MUST run disconnect(this) first per CONVENTIONS Rule 11
//      to sever signal connections before any cross-thread devices tear
//      down underneath the connections.
//
// What stays in HardwareController
// --------------------------------
//   - KPOD USB tuning knob (encoder -> tuning CAT, button -> macro)
//   - Device construction + thread management
//   - Device-config push (applyKpodPlusConfig on KPOD+ plug-in pushes the
//     current K4 keyer state from RadioState, encode mode from RadioSettings,
//     and a fixed stuck timeout — device-arrival snapshot. Live K4 changes
//     are mirrored to the KPOD+ by CwController, above)
//   - Sidetone volume / output-device follow (audio device lifecycle,
//     not CW behavior)
//   - shutdownSidetone() public entry point used by MainWindow::closeEvent
//   - pttRequested / macroRequested / hardwareError signal forwarding
//
// Verification (mandatory before PR 17 merges)
// --------------------------------------------
//   - HaliKey V1.4 paddle keying works at multiple WPM speeds
//   - HaliKey MIDI paddle keying works
//   - KPOD+ keying works (paddle -> on-device keyer -> EP02 -> K4)
//   - Sidetone audible during keying, gated correctly when KPOD+ active
//   - V1.4 PTT demux: hold paddle in CW, switch mode mid-press to SSB;
//     keyer cleanly stops, no stuck KZ
//   - V1.4 PTT demux: hold pedal in SSB, switch to CW mid-press; PTT
//     release fires when pedal released
//   - WPM changes during keying: KZL syncs correctly to K4
//   - KPOD+ presence detection gates IambicKeyer correctly (no double
//     keying when KPOD+ plug-in event arrives mid-paddle)
//   - ASAN clean (debug build with QK4_TESTS_SANITIZE=ON)
//
// Stop criterion: any keying regression -> revert immediately. Don't
// try to fix forward on the CW hot path.
// =============================================================================

class CwController : public QObject {
    Q_OBJECT

public:
    CwController(RadioState *radioState, ConnectionController *connection, IambicKeyer *keyer,
                 SidetoneGenerator *sidetone, HalikeyDevice *halikey, KpodPlusDevice *kpodPlus,
                 QObject *parent = nullptr);
    ~CwController() override;

signals:
    // HaliKey footswitch PTT (voice/data modes) or V1.4 mid-press
    // mode-change cleanup → MainWindow triggers/clears TX. Moved here
    // from HardwareController with the V1.4 demux state machine.
    void pttRequested(bool active);

private:
    // True when the KPOD+ device owns the CW path — reads the shared
    // atomic gate on ConnectionController with acquire ordering. While
    // set, all locally-driven KZ output + sidetone playback is suppressed.
    bool kpodPlusActive() const;

    // Pushes the effective paddle-reversal state to m_keyer: the K4's own
    // paddle-orientation setting (KP, mirrored via RadioState) XORed with the
    // local "Swap paddles" toggle in RadioSettings. HaliKey-scoped only — the
    // KPOD+ has its own onboard keyer and continues to mirror the K4 directly.
    void applyPaddleReversal();

    // Straight Key / Bug mode: bypasses IambicKeyer entirely. DAH is the sole input;
    // DIT is always ignored (on V1.4 it's indistinguishable from the foot pedal).
    //
    // On each release, emits one KZD<gap>U<duration>; — 4-digit milliseconds, D being the
    // key-up gap preceding the element and U how long the key was actually held. Timing is
    // carried IN the command, so element fidelity does not depend on when the command
    // reaches the wire; only the edge timestamps need to be precise. The K4 queues these
    // (verified: two back-to-back commands play as two distinct elements), so no pacing or
    // send-side throttling is required.
    //
    // m_k4BusyUntilMs exists solely to keep D honest: the radio applies D as silence after
    // whatever it is still playing, so any interval it has already spent idle has to be
    // subtracted or a long pause gets counted twice.
    void handleStraightKeyEdge();

    // Drops straight-key state and silences the sidetone. No radio-side un-key needed.
    void forceStraightKeyRelease();

    RadioState *m_radioState;
    ConnectionController *m_connection;
    IambicKeyer *m_keyer;          // owned by HardwareController
    SidetoneGenerator *m_sidetone; // owned by HardwareController
    HalikeyDevice *m_halikey;      // owned by HardwareController
    KpodPlusDevice *m_kpodPlus;    // owned by HardwareController

    // See "State moved from HardwareController" above for invariants.
    std::atomic<int> m_cachedMode{0};


    // true = V1.4 serial (deviceType 0), false = MIDI (deviceType 1).
    // Main-thread release store, HaliKey-worker acquire load — see doc block.
    std::atomic<bool> m_cachedIsV14{true};

    enum V14PttDest { V14PttNone = 0, V14PttDitPaddle = 1, V14PttPtt = 2 };
    std::atomic<int> m_v14PttDestination{V14PttNone};

    // Straight Key / Bug mode (RadioSettings-backed, HaliKey-scoped).
    // Main-thread release store (ctor + halikeyStraightKeyModeChanged), HaliKey-worker
    // acquire load in the dahStateChanged/pttStateChanged handlers — same pattern as
    // m_cachedIsV14 above.
    std::atomic<bool> m_straightKeyMode{false};

    // Raw DAH contact state while straight-key mode is active — the sole input; DIT is
    // always ignored (see handleStraightKeyEdge's doc comment above).
    std::atomic<bool> m_skKeyDown{false};
    // The state actually sent to the K4 — CAS-guarded in handleStraightKeyEdge() so a
    // repeat of the same value never re-emits an element.
    std::atomic<bool> m_skKeyed{false};

    // Straight-key element timing. Free-running monotonic clock, same pattern as
    // IambicKeyer::m_pressClock — started once in the ctor, read from the HaliKey worker
    // thread, never reset (so it can't step under NTP mid-element).
    QElapsedTimer m_skClock;
    std::atomic<qint64> m_skDownMs{0};
    std::atomic<qint64> m_skLastUpMs{0};
    // When the K4 finishes playing everything already sent — see handleStraightKeyEdge().
    std::atomic<qint64> m_k4BusyUntilMs{0};

    // Stuck-contact cleanup: an element is only emitted on release, so a wedged contact
    // means silence on the air and an endless sidetone. Bounds both.
    static constexpr int kStraightKeyMaxHoldMs = 5 * 1000;
    // Shorter closures are contact bounce, not elements.
    static constexpr int kMinElementMs = 10;
    // Widest value a 4-digit D/U field holds.
    static constexpr int kMaxFieldMs = 9999;
    QTimer *m_straightKeyWatchdog = nullptr;
};

#endif // CWCONTROLLER_H
