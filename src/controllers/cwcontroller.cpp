#include "cwcontroller.h"

#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpodplusdevice.h"
#include "models/radiostate.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

#include <QDateTime>

CwController::CwController(RadioState *radioState, ConnectionController *connection, IambicKeyer *keyer,
                           SidetoneGenerator *sidetone, HalikeyDevice *halikey, KpodPlusDevice *kpodPlus,
                           QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_keyer(keyer), m_sidetone(sidetone),
      m_halikey(halikey), m_kpodPlus(kpodPlus) {

    // =========================================================================
    // Initial keyer + sidetone state from RadioState
    // =========================================================================
    int initWpm = m_radioState->keyerSpeed();
    if (initWpm <= 0)
        initWpm = 20;
    QMetaObject::invokeMethod(m_keyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, initWpm));
    QMetaObject::invokeMethod(
        m_keyer, "setMode", Qt::QueuedConnection,
        Q_ARG(IambicKeyer::Mode, m_radioState->iambicMode() == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
    applyPaddleReversal();
    m_cachedWpm.store(initWpm, std::memory_order_release);

    if (m_radioState->cwPitch() > 0) {
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->cwPitch()));
    }
    if (m_radioState->keyerSpeed() > 0) {
        QMetaObject::invokeMethod(m_sidetone, "setKeyerSpeed", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->keyerSpeed()));
    }

    // =========================================================================
    // RadioState observers — keyer speed / paddle / pitch
    // =========================================================================
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        // WHY use invokeMethod instead of a direct call: SidetoneGenerator lives on
        // its own thread. setKeyerSpeed only writes a std::atomic<int> today (safe direct),
        // but the matching invokeMethod for m_keyer on the next line establishes the
        // cross-thread pattern — future changes to setKeyerSpeed that touch non-atomic
        // members would otherwise introduce a silent race with no call-site warning.
        // While straight-key mode owns KS, incoming speed echoes are our own per-element
        // writes — don't mistake them for the operator changing speed.
        if (!m_straightKeyMode.load(std::memory_order_acquire))
            m_cachedWpm.store(wpm, std::memory_order_release);
        QMetaObject::invokeMethod(m_sidetone, "setKeyerSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        QMetaObject::invokeMethod(m_keyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        // Sync element length with K4 server
        // Straight-key mode churns KS per element; skip the derived KZL write mid-send.
        if (!m_straightKeyMode.load(std::memory_order_acquire)) {
            int ditMs = 1200 / wpm;
            m_connection->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
        }
        // K4 is the source of truth — mirror the speed onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerSpeed(wpm);
    });

    // Update the local iambic keyer mode/reversal — and the KPOD+ keyer — when
    // the K4's KP settings change. The K4 is the source of truth.
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        QMetaObject::invokeMethod(
            m_keyer, "setMode", Qt::QueuedConnection,
            Q_ARG(IambicKeyer::Mode, iambic == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
        applyPaddleReversal();
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerParams(iambic == 'B' ? 1 : 0, paddle == 'R');
    });

    // Local "Swap paddles" toggle (RadioSettings, HaliKey-scoped) — recompute
    // whenever it changes so it composes correctly with the K4's own KP setting.
    connect(RadioSettings::instance(), &RadioSettings::halikeyPaddleSwappedChanged, this,
            [this](bool) { applyPaddleReversal(); });

    // Straight Key / Bug mode (RadioSettings, HaliKey-scoped). Main-thread release
    // store; the ditStateChanged/dahStateChanged handlers below read it with acquire
    // ordering on the HaliKey worker thread — same pattern as m_cachedIsV14.
    m_straightKeyMode.store(RadioSettings::instance()->halikeyStraightKeyMode(), std::memory_order_release);
    connect(RadioSettings::instance(), &RadioSettings::halikeyStraightKeyModeChanged, this, [this](bool enabled) {
        if (enabled)
            m_skNominalWpm.store(m_cachedWpm.load(std::memory_order_acquire), std::memory_order_release);
        m_straightKeyMode.store(enabled, std::memory_order_release);
        if (!enabled) {
            restoreKeyerElementLength();
            // Mode turned off mid-press: drop the half-finished element cleanly.
            forceStraightKeyRelease();
        }
    });

    // Max-hold watchdog: defense-in-depth against a wedged straight key (stuck
    // contact, dropped release edge). Lives on the main thread; started/stopped
    // from handleStraightKeyEdge() via invokeMethod since that runs on the HaliKey
    // worker thread.
    m_straightKeyWatchdog = new QTimer(this);
    m_straightKeyWatchdog->setSingleShot(true);
    connect(m_straightKeyWatchdog, &QTimer::timeout, this, [this]() {
        qWarning("CwController: straight-key watchdog fired — forcing RX after max hold duration");
        forceStraightKeyRelease();
    });

    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection, Q_ARG(int, pitchHz));
        // K4 is the source of truth — mirror the CW pitch onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setCwPitch(pitchHz);
    });

    // =========================================================================
    // Mode tracking + V1.4 PTT-line demux cleanup
    // =========================================================================
    // WHY read m_cachedMode instead of m_radioState->mode() on the HaliKey worker
    // thread: m_radioState->mode() reads a non-atomic subsystem field concurrently
    // with parseCATCommand()'s writes on the main thread — data race. The atomic
    // cache is updated from modeChanged via AutoConnection — both this controller
    // and RadioState live on the main thread, so AutoConnection resolves to
    // DirectConnection and the store runs synchronously alongside parseCATCommand.
    // The HaliKey worker thread reads with acquire ordering, paired with the
    // release store here.
    m_cachedMode.store(static_cast<int>(m_radioState->mode()), std::memory_order_release);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        m_cachedMode.store(static_cast<int>(mode), std::memory_order_release);
        // V1.4 mode-transition cleanup: if a paddle/PTT was rising-edge-captured before
        // the transition, fire the matching up event to the OLD destination so neither
        // the IambicKeyer nor MainWindow gets stuck in a half-pressed state. CAS ensures
        // the falling-edge handler doesn't also clean up (whichever fires first wins).
        int dest = m_v14PttDestination.load(std::memory_order_acquire);
        if (dest != V14PttNone) {
            if (m_v14PttDestination.compare_exchange_strong(dest, V14PttNone, std::memory_order_acq_rel)) {
                if (dest == V14PttDitPaddle) {
                    m_keyer->setDitPaddle(false);
                } else if (dest == V14PttPtt) {
                    emit pttRequested(false);
                }
            }
        }
    });

    // Device-type fan-out: mirror for the V1.4 PTT demux below + the keyer's hold
    // gate (V1.4 serial needs the bounce gate; MIDI is firmware-debounced and WinMM
    // burst delivery would make an arrival-time gate drop real elements). RadioSettings
    // is a plain main-thread singleton; the PTT handler runs on the HaliKey worker
    // thread — same store/load pattern as m_cachedMode above. setHoldGateEnabled is a
    // plain atomic write, safe to call directly from the main thread.
    const bool initIsV14 = (RadioSettings::instance()->halikeyDeviceType() != 1);
    m_cachedIsV14.store(initIsV14, std::memory_order_release);
    m_keyer->setHoldGateEnabled(initIsV14);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, this, [this](int type) {
        const bool isV14 = (type != 1);
        m_cachedIsV14.store(isV14, std::memory_order_release);
        m_keyer->setHoldGateEnabled(isV14);
    });

    // =========================================================================
    // Keyer → CAT commands + sidetone audio
    // =========================================================================
    //
    // Wire keyer signals (emitted on the HighPriority keyer thread) directly to TcpClient on
    // the I/O thread via queued connections. The main thread is not on this hot path.
    // The atomic gate on ConnectionController drops emissions when the KPOD+ device owns
    // the keyer; the local-iambic state machine still runs but its KZ output is suppressed.
    //
    // Order preservation: all three signals (restartAfterPause, elementStarted, characterSpace)
    // originate on the same source thread (keyer) and target the same destination thread (I/O),
    // so Qt's event queue keeps them FIFO. The on-air ordering is:
    //   restartAfterPause → elementStarted (per enterElement)
    //   characterSpace → keyingFinished     (per goIdle)
    // matching the K4 KZ protocol (KZP timing → KZ./KZ- elements → KZ ; letter marker —
    // literal 0x20 SPACE, confirmed by hexdump of live KPOD+ EP02 traffic; the PDF spec's
    // monospace rendering of "KZ_;" is a typographic artifact, not an underscore byte).
    auto *tc = m_connection->tcpClient();
    auto *cc = m_connection;
    connect(
        m_keyer, &IambicKeyer::elementStarted, tc,
        [tc, cc](bool isDit) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(isDit ? QStringLiteral("KZ.;") : QStringLiteral("KZ-;"));
        },
        Qt::QueuedConnection);
    connect(
        m_keyer, &IambicKeyer::characterSpace, tc,
        [tc, cc]() {
            if (cc->isKpodPlusKeyerActive())
                return;
            // WHY space, not underscore: the Elecraft KPodKeyerInterface.pdf renders the
            // letter-space marker as "KZ_;" but a hexdump of EP02 traffic from a live KPOD+
            // device shows the actual byte is 0x20 (literal SPACE). The K4 firmware accepts
            // that form; emitting "KZ_;" with a real underscore is what the parser rejects.
            // PDF rendering artifact — the underline beneath the space in the spec's
            // monospace font reads as an underscore.
            tc->sendCAT(QStringLiteral("KZ ;"));
        },
        Qt::QueuedConnection);
    connect(
        m_keyer, &IambicKeyer::restartAfterPause, tc,
        [tc, cc](int ms) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(QStringLiteral("KZP%1;").arg(ms, 4, 10, QChar('0')));
        },
        Qt::QueuedConnection);

    // Sidetone stays on its own thread. Sidetone gate uses the local helper so a hot KPOD+
    // takeover silences feedback immediately even before the next emit lands on I/O.
    connect(m_keyer, &IambicKeyer::elementStarted, m_sidetone, [this, sg = m_sidetone](bool isDit) {
        if (kpodPlusActive())
            return;
        isDit ? sg->playSingleDit() : sg->playSingleDah();
    });
    // No keyingFinished → sidetone wiring: each element is written as a complete
    // PCM block (tone + space) and always plays to completion — there is nothing
    // to stop when the keyer goes idle.

    // =========================================================================
    // HaliKey paddle → keyer (ZERO-LATENCY DirectConnection)
    // =========================================================================
    // HaliKey MIDI sends note 20 (dit) + note 31 (PTT) together on every Tip-to-Sleeve closure.
    // In CW mode: forward dit to keyer, ignore PTT (TX handled by KZ commands).
    // In voice mode: forward PTT to MainWindow, suppress dit (no keying in SSB/AM/FM).
    connect(
        m_halikey, &HalikeyDevice::ditStateChanged, this,
        [this](bool pressed) {
            // Suppress HaliKey dit when KPOD+ keyer owns the CW path
            if (kpodPlusActive())
                return;
            auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
            if (mode != RadioState::CW && mode != RadioState::CW_R)
                return; // In voice/data modes, dit is suppressed — PTT signal handles TX
            if (m_straightKeyMode.load(std::memory_order_acquire))
                return; // Straight Key / Bug: DIT is always ignored — see dahStateChanged below.
            m_keyer->setDitPaddle(pressed);
        },
        Qt::DirectConnection);
    connect(
        m_halikey, &HalikeyDevice::dahStateChanged, this,
        [this](bool pressed) {
            if (kpodPlusActive())
                return;
            if (m_straightKeyMode.load(std::memory_order_acquire)) {
                // Straight Key / Bug: DAH is the sole input — driven from a real, dedicated
                // line on every HaliKey transport (V1.4: DCD||DSR; MIDI: note 21), unlike DIT
                // which on V1.4 is impossible to distinguish from the foot pedal (see the PTT
                // handler below) and is therefore always ignored, on every transport, so a
                // 2-conductor straight key plugged into the paddle jack (which leaves the DIT
                // line floating/undefined) can't leak spurious keying.
                //
                // Same CW-mode gate as the dit handler above — unlike the iambic path, a dah
                // here drives TX; directly, so it must not leak into voice/data modes.
                auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
                if (mode != RadioState::CW && mode != RadioState::CW_R)
                    return;
                m_skKeyDown.store(pressed, std::memory_order_release);
                handleStraightKeyEdge();
                return;
            }
            m_keyer->setDahPaddle(pressed);
        },
        Qt::DirectConnection);

    // HaliKey PTT → MainWindow (voice/data modes) or paddle dit (CW mode, V1.4 only).
    // WHY: V1.4 serial firmware can't distinguish foot pedal from paddle dit lever — both
    // drive CTS. We demux by mode here: in CW the CTS edge is treated as the dit-paddle
    // press, in voice it's the foot pedal → PTT. The MIDI variant has a distinct note for
    // the pedal so its CW behavior stays mode-gated to silence (no spurious dit injection).
    connect(
        m_halikey, &HalikeyDevice::pttStateChanged, this,
        [this](bool active) {
            const bool isV14 = m_cachedIsV14.load(std::memory_order_acquire);
            if (active) {
                // RISING EDGE: pick a destination based on current mode and remember it,
                // so the falling edge (or a mid-press mode change) can fire the matching
                // up event to the SAME destination — even if the mode flipped meanwhile.
                auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
                const bool inCw = (mode == RadioState::CW || mode == RadioState::CW_R);
                if (inCw && isV14) {
                    // KPOD+ owns the keyer? Drop and don't capture a destination — the
                    // matching falling edge will see V14PttNone and also drop.
                    if (kpodPlusActive())
                        return;
                    if (m_straightKeyMode.load(std::memory_order_acquire)) {
                        // Straight Key / Bug: V1.4's CTS line is the ONLY place a DIT/pedal
                        // signal shows up (HaliKeyV14Worker hardcodes ditState=false, so
                        // ditStateChanged never fires on this transport — see
                        // readPinState()'s doc comment). DIT is always ignored in this mode
                        // (see dahStateChanged above), so drop this edge the same way the
                        // MIDI branch below drops its pedal press in CW: no destination
                        // captured, matching falling edge sees V14PttNone and also drops.
                        return;
                    }
                    m_v14PttDestination.store(V14PttDitPaddle, std::memory_order_release);
                    m_keyer->setDitPaddle(true);
                } else if (!inCw) {
                    m_v14PttDestination.store(V14PttPtt, std::memory_order_release);
                    emit pttRequested(true);
                }
                // (MIDI variant in CW falls through silently — its dit comes via note 20,
                // not via the PTT line, so a PTT rising edge here is the foot pedal which
                // shouldn't key in CW.)
            } else {
                // FALLING EDGE: dispatch to whatever destination captured the rising edge.
                // CAS ensures the mode-change cleanup handler doesn't also fire — only one
                // of (mode-change, falling-edge) wins, and the other sees V14PttNone.
                int dest = m_v14PttDestination.load(std::memory_order_acquire);
                if (dest == V14PttNone)
                    return;
                if (m_v14PttDestination.compare_exchange_strong(dest, V14PttNone, std::memory_order_acq_rel)) {
                    if (dest == V14PttDitPaddle) {
                        m_keyer->setDitPaddle(false);
                    } else if (dest == V14PttPtt) {
                        emit pttRequested(false);
                    }
                }
            }
        },
        Qt::DirectConnection);

    // Enable keyer when radio connects, disable on disconnect
    connect(m_connection, &ConnectionController::radioReady, this, [this]() {
        QMetaObject::invokeMethod(m_keyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, true));
    });
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    QMetaObject::invokeMethod(m_keyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
                    m_skKeyDown.store(false, std::memory_order_release);
                    m_skKeyed.store(false, std::memory_order_release);
                    m_straightKeyWatchdog->stop();
                    QMetaObject::invokeMethod(m_sidetone, "stopHold", Qt::QueuedConnection);
                }
            });

    // Stop keyer when HaliKey disconnects (prevents runaway keying
    // if paddle was held when disconnected — Note Off never arrives)
    connect(m_halikey, &HalikeyDevice::disconnected, this, [this]() {
        QMetaObject::invokeMethod(m_keyer, "stop", Qt::QueuedConnection);
        // Same runaway-keying risk applies to straight-key mode: the HaliKey vanished
        // mid-press, so no release edge is coming. Force RX; through — the radio
        // connection is (probably) still up here, unlike the case above.
        forceStraightKeyRelease();
    });

    // =========================================================================
    // KPOD+ keyer-active gate + EP02 keyer data routing
    // =========================================================================
    // The KPOD+ owns the entire CW chain when present. The gate is set on
    // deviceInfoReady (KPOD+ detected) rather than deviceConnected (open
    // succeeded) so the ~10-100 ms open window doesn't leak paddle events to
    // the local sidetone path.
    connect(m_kpodPlus, &KpodPlusDevice::deviceConnected, this,
            [this]() { m_connection->setKpodPlusKeyerActive(true); });
    connect(m_kpodPlus, &KpodPlusDevice::deviceDisconnected, this,
            [this]() { m_connection->setKpodPlusKeyerActive(false); });
    connect(m_kpodPlus, &KpodPlusDevice::deviceInfoReady, this, [this]() {
        if (m_kpodPlus->isDetected())
            m_connection->setKpodPlusKeyerActive(true);
    });

    // EP02 keyer data → straight to the I/O thread.
    //
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after the
    // last ';'. By targeting TcpClient as the receiver (lives on the I/O thread) with a
    // queued connection we skip the main thread entirely on this hot path. sendCATBytes
    // trims NUL padding on the I/O thread and hands off to sendCAT().
    connect(m_kpodPlus, &KpodPlusDevice::keyerDataReceived, m_connection->tcpClient(), &TcpClient::sendCATBytes,
            Qt::QueuedConnection);
}

CwController::~CwController() {
    // Sever all signal connections before HardwareController tears down the
    // devices these handlers reference. CONVENTIONS Rule 11.
    disconnect(this);
}

bool CwController::kpodPlusActive() const {
    return m_connection->isKpodPlusKeyerActive();
}

void CwController::applyPaddleReversal() {
    const bool radioReversed = m_radioState->paddleOrientation() == 'R';
    const bool localSwap = RadioSettings::instance()->halikeyPaddleSwapped();
    QMetaObject::invokeMethod(m_keyer, "setReversed", Qt::QueuedConnection,
                              Q_ARG(bool, radioReversed != localSwap));
}

void CwController::handleStraightKeyEdge() {
    const bool down = m_skKeyDown.load(std::memory_order_acquire);
    bool expected = !down;
    if (!m_skKeyed.compare_exchange_strong(expected, down, std::memory_order_acq_rel))
        return; // contact bounce / repeated same-direction edge

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int dit = 1200 / qBound(kMinKeyerWpm, m_skNominalWpm.load(std::memory_order_acquire), kMaxKeyerWpm);

    if (down) {
        // The gap that just ended belongs before the element it precedes.
        const qint64 lastUp = m_skLastUpMs.load(std::memory_order_acquire);
        const qint64 gap = now - lastUp;
        if (lastUp > 0 && gap > 2 * dit) {
            m_connection->sendCAT(QStringLiteral("KZ ;"));
            if (gap > 5 * dit)
                m_connection->sendCAT(QStringLiteral("KZ ;")); // word gap ~ two letter spaces
        }
        m_skDownMs.store(now, std::memory_order_release);
        QMetaObject::invokeMethod(m_sidetone, "startHold", Qt::QueuedConnection);
    } else {
        const qint64 held = now - m_skDownMs.load(std::memory_order_acquire);
        if (held >= kMinElementMs) {
            // Element length is set by keyer speed, not KZL (KZL is inert for KZ elements
            // on this path — verified on hardware). A dit is 1200/KS ms and a dah 3600/KS,
            // so pick whichever element type puts the requested duration inside the usable
            // KS range and solve for the speed. Together they cover ~20-450ms continuously.
            const bool useDit = held <= kDitDahBoundaryMs;
            const double scale = useDit ? 1200.0 : 3600.0;
            const int ks = qBound(kMinKeyerWpm, static_cast<int>(scale / held + 0.5), kMaxKeyerWpm);
            m_connection->sendCAT(QStringLiteral("KS%1;").arg(ks, 3, 10, QChar('0')));
            m_connection->sendCAT(useDit ? QStringLiteral("KZ.;") : QStringLiteral("KZ-;"));
        }
        m_skLastUpMs.store(now, std::memory_order_release);
        QMetaObject::invokeMethod(m_sidetone, "stopHold", Qt::QueuedConnection);
    }
}

// Straight-key mode drives KS per element, so put the operator's own speed back when it
// stops owning that setting.
void CwController::restoreKeyerElementLength() {
    const int wpm = qBound(kMinKeyerWpm, m_skNominalWpm.load(std::memory_order_acquire), kMaxKeyerWpm);
    m_connection->sendCAT(QStringLiteral("KS%1;").arg(wpm, 3, 10, QChar('0')));
}

void CwController::forceStraightKeyRelease() {
    // KZ elements are self-terminating — nothing can be left keyed. Just drop local state.
    restoreKeyerElementLength();
    m_skKeyDown.store(false, std::memory_order_release);
    m_skKeyed.store(false, std::memory_order_release);
    QMetaObject::invokeMethod(m_sidetone, "stopHold", Qt::QueuedConnection);
    m_straightKeyWatchdog->stop();
}
