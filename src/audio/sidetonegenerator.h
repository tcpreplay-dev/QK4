#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QTimer>
#include <QtMath>
#include <atomic>

/**
 * @brief Local CW sidetone synth. Lives on its own thread (HardwareController's m_sidetoneThread)
 *        so audio output never blocks the keyer or UI. IambicKeyer drives `playSingleDit/Dah` per
 *        element for both HaliKey transports (V1.4 serial and MIDI); when the KPOD+ owns the CW
 *        path, CwController suppresses this local sidetone entirely. Each element is written as a
 *        complete PCM block (tone + inter-element space) and always plays to completion.
 *        All public slots are Q_INVOKABLE and expected to be posted via QueuedConnection.
 */
class SidetoneGenerator : public QObject {
    Q_OBJECT
public:
    explicit SidetoneGenerator(QObject *parent = nullptr);
    ~SidetoneGenerator();

    Q_INVOKABLE void setFrequency(int hz);
    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void setKeyerSpeed(int wpm);

    // Initialize/shutdown audio (called on sidetone thread via invokeMethod)
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    // Play a single element (one per IambicKeyer::elementStarted)
    Q_INVOKABLE void playSingleDit();
    Q_INVOKABLE void playSingleDah();

    // Continuous tone for as long as a straight key / bug is held down (CwController's
    // straight-key path). Unlike playSingleDit/Dah, which write one fixed-length block per
    // element, this maintains a small constant-size lookahead buffer (kHoldLookaheadMs) fed by
    // a timer that fires well inside that margin (kHoldTimerIntervalMs) — timer jitter of a
    // few ms doesn't starve the sink, but the buffer also never grows unbounded, so stopHold()
    // only has to drain kHoldLookaheadMs of already-queued audio before it goes quiet. That
    // bounded latency (not the sink's full ~1.3s hardware buffer) is the real ceiling on
    // key-up responsiveness — it never affects the actual TX;/RX; keying sent to the K4,
    // which CwController sends independently of this local audio feedback.
    Q_INVOKABLE void startHold();
    Q_INVOKABLE void stopHold();

signals:

private:
    void initAudio();
    void playElement(int durationMs);
    // Generates `numSamples` of continuous-phase tone (optionally ramped at the start/end
    // of THIS call only) into m_elementBuffer and pushes it to the sink. Shared by both the
    // lookahead refill path (no envelope — mid-hold) and the initial/closing hold chunks
    // (rise on the first call, fall on the last).
    void writeHoldSamples(int numSamples, bool applyRise, bool applyFall);
    // Tops up the lookahead buffer to kHoldLookaheadMs ahead of wall-clock elapsed hold
    // time. Called once immediately in startHold() (with rise) and then on every
    // m_holdTimer tick (no envelope) until stopHold().
    void refillHold();
    int ditDurationMs() const;
    int dahDurationMs() const;
    // WHY: when the user leaves the speaker on "System Default" (empty device id),
    // follow the OS default live instead of caching it for the session — same
    // pattern as AudioEngine. Without this, a default-device change (or hot-unplug)
    // left the sink on a stale/dead device and the sidetone went silent.
    void onSystemDefaultOutputChanged();

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_pushDevice = nullptr;
    QString m_selectedOutputDeviceId;
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    std::atomic<int> m_keyerWpm{20};
    double m_phase = 0.0;

    // Straight-key/bug hold state. Lazily created on first startHold() call, which always
    // runs on the sidetone thread (Q_INVOKABLE, dispatched via QueuedConnection) — safe to
    // `new QTimer(this)` there. Plain fields, not atomics: start/stopHold and the timer's
    // own tick only ever run serialized on this thread via the queued-invoke pattern
    // documented on the class.
    QTimer *m_holdTimer = nullptr;
    bool m_holding = false;
    QElapsedTimer m_holdClock;   // started fresh in startHold(); backs refillHold()'s target
    qint64 m_holdSamplesWritten = 0; // samples generated since startHold() — the lookahead accounting
    static constexpr int kHoldLookaheadMs = 30;    // constant buffer margin — bounds key-up latency
    static constexpr int kHoldTimerIntervalMs = 10; // well inside the margin so jitter can't starve it

    // Pre-allocated PCM scratch buffer. Worst case is 5 WPM dah (720 ms tone +
    // 240 ms inter-element space) at 48 kHz × 2 bytes = ~92 kB. Sized to
    // 128 kB so a future slower-keyer mode doesn't realloc. Previously
    // playElement() allocated a fresh QByteArray per CW element (7+ allocs/sec
    // during keying). resize() against the pre-sized capacity is alloc-free.
    QByteArray m_elementBuffer;
};

#endif // SIDETONEGENERATOR_H
