#ifndef RADIOSETTINGS_H
#define RADIOSETTINGS_H

#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVector>

// Macro entry for programmable function keys
struct MacroEntry {
    QString functionId; // "PF1", "Fn.F1", "K-pod.1T", etc.
    QString label;      // Custom label or empty
    QString command;    // CAT command or empty

    bool isEmpty() const { return command.isEmpty(); }
    QString displayLabel() const {
        if (command.isEmpty())
            return "Unused";
        return label.isEmpty() ? "Mapped" : label;
    }
};

// RX EQ preset entry (8-band graphic equalizer)
struct EqPreset {
    QString name;       // User-defined name ("SSB", "CW", etc.)
    QVector<int> bands; // 8 values, -16 to +16 dB

    bool isEmpty() const { return bands.isEmpty() || name.isEmpty(); }
    QString displayName() const { return isEmpty() ? "---" : name; }
};

struct DxClusterEntry {
    QString host;
    quint16 port = 7000;
    QString callsign;
    bool autoConnect = false;
};

struct RadioEntry {
    QString name;
    QString host;
    QString password; // Password (used as PSK when TLS enabled)
    quint16 port;
    bool useTls = false;      // Use TLS/PSK encryption (port 9204)
    QString identity;         // TLS-PSK identity (optional, empty = default)
    int encodeMode = 3;       // Audio encode mode: 0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float (default)
    int streamingLatency = 3; // Remote streaming audio latency: 0-7 (default 3)
    int displayFps = 15;      // Display FPS: 12-30 (default 15, good balance for large monitors)

    bool operator==(const RadioEntry &other) const {
        return name == other.name && host == other.host && port == other.port;
    }
};

class RadioSettings : public QObject {
    Q_OBJECT

public:
    static RadioSettings *instance();

    QVector<RadioEntry> radios() const;
    void addRadio(const RadioEntry &radio);
    void removeRadio(int index);
    void updateRadio(int index, const RadioEntry &radio);

    int lastSelectedIndex() const;
    void setLastSelectedIndex(int index);

    bool kpodEnabled() const;
    void setKpodEnabled(bool enabled);

    // KPA1500 Amplifier settings
    QString kpa1500Host() const;
    void setKpa1500Host(const QString &host);
    quint16 kpa1500Port() const;
    void setKpa1500Port(quint16 port);
    bool kpa1500Enabled() const;
    void setKpa1500Enabled(bool enabled);
    int kpa1500PollInterval() const;
    void setKpa1500PollInterval(int intervalMs);

    // Audio output settings
    int volume() const;
    void setVolume(int value); // 0-100, default 45
    int subVolume() const;
    void setSubVolume(int value); // 0-100, default 45 (Sub RX / VFO B)

    // Audio input (microphone) settings
    int micGain() const;
    void setMicGain(int value); // 0-100, default 25
    QString micDevice() const;
    void setMicDevice(const QString &deviceId);

    // Audio output (speaker) settings
    QString speakerDevice() const;
    void setSpeakerDevice(const QString &deviceId);

    // CAT Server settings (local TCP server for external apps)
    bool catServerEnabled() const;
    void setCatServerEnabled(bool enabled);
    quint16 catServerPort() const;
    void setCatServerPort(quint16 port);

    // Macro settings
    QMap<QString, MacroEntry> macros() const;
    MacroEntry macro(const QString &functionId) const;
    void setMacro(const QString &functionId, const QString &label, const QString &command);

    // Keyer device roles. Two HaliKey-class interfaces may be connected at once: a paddle
    // driving the iambic keyer, and a straight key/bug driving KZD/U elements directly.
    // Which role an input plays is now decided by the device it arrived on, replacing the
    // old single global "straight key mode" flag.
    enum KeyerRole { KeyerRolePaddle = 0, KeyerRoleStraightKey = 1, KeyerRoleCount = 2 };
    Q_ENUM(KeyerRole)

    QString keyerPortName(KeyerRole role) const;
    void setKeyerPortName(KeyerRole role, const QString &portName);
    int keyerDeviceType(KeyerRole role) const; // 0=V1.4, 1=MIDI
    void setKeyerDeviceType(KeyerRole role, int type);
    bool keyerAutoConnect(KeyerRole role) const;
    void setKeyerAutoConnect(KeyerRole role, bool enabled);


    // Straight-key timing buffer. The radio applies a key-down pre-roll (KZL) so its
    // element queue never drains mid-character; without it every dah following a dit
    // renders as a letter break. Expressed as speed bounds rather than milliseconds —
    // see CwController::straightKeyPreRollMs().
    bool straightKeyBufferEnabled() const;
    void setStraightKeyBufferEnabled(bool enabled);
    int straightKeyMinWpm() const;
    void setStraightKeyMinWpm(int wpm); // 5-80, never above straightKeyMaxWpm()
    int straightKeyMaxWpm() const;
    void setStraightKeyMaxWpm(int wpm); // 15-80, never below straightKeyMinWpm()
    double straightKeyDahDitRatio() const;
    void setStraightKeyDahDitRatio(double ratio); // 2.5-5.0, operator's own sending style
    int sidetoneVolume() const;
    void setSidetoneVolume(int value); // 0-100, default 30

    // DX Cluster settings
    QVector<DxClusterEntry> dxClusters() const;
    void addDxCluster(const DxClusterEntry &entry);
    void removeDxCluster(int index);
    void updateDxCluster(int index, const DxClusterEntry &entry);
    int dxClusterSpotAge() const;
    void setDxClusterSpotAge(int seconds);
    QString dxClusterCallsign() const;
    void setDxClusterCallsign(const QString &callsign);
    int dxClusterSpotFontSize() const;
    void setDxClusterSpotFontSize(int sizePx);

    // RX EQ Presets (4 slots)
    EqPreset rxEqPreset(int index) const;                  // Get preset 0-3
    void setRxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearRxEqPreset(int index);                       // Clear preset 0-3

    // TX EQ Presets (4 slots)
    EqPreset txEqPreset(int index) const;                  // Get preset 0-3
    void setTxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearTxEqPreset(int index);                       // Clear preset 0-3

    // KPOD+ encode mode (KZ/KX). Keyer speed, CW pitch, iambic mode and paddle
    // orientation are no longer stored — the KPOD+ mirrors the connected K4.
    int kpodPlusEncodeMode() const;
    void setKpodPlusEncodeMode(int mode); // 0=KZ, 1=KX

    // CW/data text-decode popup font size (per receiver, pixel value)
    int textDecodeFontSize(bool subRx) const;
    void setTextDecodeFontSize(bool subRx, int sizePx);

    // Station / operator info
    int iaruRegion() const; // 1, 2, or 3 — drives the panadapter band-plan overlay
    void setIaruRegion(int region);
    QString callSign() const;
    void setCallSign(const QString &callSign);
    QString gridSquare() const;
    void setGridSquare(const QString &grid);
    QString operatorName() const;
    void setOperatorName(const QString &name);
    QString qth() const;
    void setQth(const QString &qth);
    bool bandPlanOverlayEnabled() const;
    void setBandPlanOverlayEnabled(bool enabled);

signals:
    void radiosChanged();
    void kpodEnabledChanged(bool enabled);
    void kpa1500EnabledChanged(bool enabled);
    void kpa1500SettingsChanged();
    void kpa1500PollIntervalChanged(int intervalMs);
    void micGainChanged(int value);
    void micDeviceChanged(const QString &deviceId);
    void speakerDeviceChanged(const QString &deviceId);
    void catServerEnabledChanged(bool enabled);
    void catServerPortChanged(quint16 port);
    void macrosChanged();
    // Port, device type or auto-connect changed for one role (RadioSettings::KeyerRole).
    void keyerConfigChanged(int role);
    void straightKeyTimingChanged();
    void sidetoneVolumeChanged(int value);
    void rxEqPresetsChanged();
    void txEqPresetsChanged();
    void dxClusterSettingsChanged();
    void kpodPlusSettingsChanged();
    void iaruRegionChanged(int region);
    void bandPlanOverlayEnabledChanged(bool enabled);

private:
    explicit RadioSettings(QObject *parent = nullptr);
    void load();
    void save();
    void sortRadios();

    QVector<RadioEntry> m_radios;
    int m_lastSelectedIndex;
    bool m_kpodEnabled;

    // KPA1500 settings
    QString m_kpa1500Host;
    quint16 m_kpa1500Port = 1500;
    bool m_kpa1500Enabled = false;
    int m_kpa1500PollInterval = 300; // Default: 300ms for responsive meters

    // CAT Server settings
    bool m_catServerEnabled = false;
    quint16 m_catServerPort = 9299;

    // Keyer settings, indexed by KeyerRole
    QString m_keyerPortName[KeyerRoleCount];
    int m_keyerDeviceType[KeyerRoleCount] = {0, 0};
    bool m_keyerAutoConnect[KeyerRoleCount] = {false, false};
    bool m_straightKeyBufferEnabled = false;
    int m_straightKeyMinWpm = 20;
    int m_straightKeyMaxWpm = 35;
    double m_straightKeyDahDitRatio = 4.0;
    int m_sidetoneVolume = 30;   // Default 30%

    // Macro settings
    QMap<QString, MacroEntry> m_macros;

    // RX EQ Presets (4 slots)
    EqPreset m_rxEqPresets[4];

    // TX EQ Presets (4 slots)
    EqPreset m_txEqPresets[4];

    // DX Cluster settings
    QVector<DxClusterEntry> m_dxClusters;
    int m_dxClusterSpotAge = 600; // Default 10 minutes
    QString m_dxClusterCallsign;
    int m_dxClusterSpotFontSize = 11; // K4Styles::Dimensions::FontSizeSpot default; clamped to [8, 16]

    // KPOD+ encode mode (0=KZ, 1=KX). Keyer speed / CW pitch / iambic mode /
    // paddle orientation are not stored — the KPOD+ mirrors the K4.
    int m_kpodPlusEncodeMode = 0;

    QSettings m_settings;
};

#endif // RADIOSETTINGS_H
