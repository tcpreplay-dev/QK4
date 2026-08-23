#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
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
    // element, this feeds short chunks on a repeating timer so the tone can run for an
    // arbitrary, operator-controlled duration. stopHold() cuts the feed and lets the tone
    // ring out over one short falling-envelope chunk — a small click on release is expected
    // (matches the abrupt edge of a real hand key) and is cosmetic only; it never affects the
    // actual TX;/RX; keying sent to the K4.
    Q_INVOKABLE void startHold();
    Q_INVOKABLE void stopHold();

signals:

private:
    void initAudio();
    void playElement(int durationMs);
    void writeHoldChunk(bool applyRise, bool applyFall);
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
    // `new QTimer(this)` there. Plain bool, not atomic: start/stopHold only ever run
    // serialized on this thread via the queued-invoke pattern documented on the class.
    QTimer *m_holdTimer = nullptr;
    bool m_holding = false;

    // Pre-allocated PCM scratch buffer. Worst case is 5 WPM dah (720 ms tone +
    // 240 ms inter-element space) at 48 kHz × 2 bytes = ~92 kB. Sized to
    // 128 kB so a future slower-keyer mode doesn't realloc. Previously
    // playElement() allocated a fresh QByteArray per CW element (7+ allocs/sec
    // during keying). resize() against the pre-sized capacity is alloc-free.
    QByteArray m_elementBuffer;
};

#endif // SIDETONEGENERATOR_H
