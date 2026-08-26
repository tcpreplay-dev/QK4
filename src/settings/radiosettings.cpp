#include "radiosettings.h"

static const QByteArray obfuscationKey = "K4RemoteObfuscation";

static QString obfuscatePassword(const QString &password) {
    if (password.isEmpty())
        return QString();
    QByteArray utf8 = password.toUtf8();
    for (int i = 0; i < utf8.size(); ++i)
        utf8[i] = utf8[i] ^ obfuscationKey[i % obfuscationKey.size()];
    return QStringLiteral("obf:") + QString::fromLatin1(utf8.toBase64());
}

static QString deobfuscatePassword(const QString &stored) {
    if (!stored.startsWith(QStringLiteral("obf:")))
        return stored;
    QByteArray decoded = QByteArray::fromBase64(stored.mid(4).toLatin1());
    for (int i = 0; i < decoded.size(); ++i)
        decoded[i] = decoded[i] ^ obfuscationKey[i % obfuscationKey.size()];
    return QString::fromUtf8(decoded);
}

RadioSettings *RadioSettings::instance() {
    static RadioSettings instance;
    return &instance;
}

RadioSettings::RadioSettings(QObject *parent)
    : QObject(parent), m_lastSelectedIndex(-1), m_kpodEnabled(false), m_settings("QK4", "QK4") {
    load();
}

QVector<RadioEntry> RadioSettings::radios() const {
    return m_radios;
}

void RadioSettings::addRadio(const RadioEntry &radio) {
    m_radios.append(radio);
    sortRadios();
    save();
    emit radiosChanged();
}

void RadioSettings::removeRadio(int index) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios.removeAt(index);
        if (m_lastSelectedIndex >= m_radios.size()) {
            m_lastSelectedIndex = m_radios.isEmpty() ? -1 : m_radios.size() - 1;
        }
        save();
        emit radiosChanged();
    }
}

void RadioSettings::updateRadio(int index, const RadioEntry &radio) {
    if (index >= 0 && index < m_radios.size()) {
        m_radios[index] = radio;
        sortRadios();
        save();
        emit radiosChanged();
    }
}

int RadioSettings::lastSelectedIndex() const {
    return m_lastSelectedIndex;
}

void RadioSettings::setLastSelectedIndex(int index) {
    if (m_lastSelectedIndex != index) {
        m_lastSelectedIndex = index;
        save();
    }
}

bool RadioSettings::kpodEnabled() const {
    return m_kpodEnabled;
}

void RadioSettings::setKpodEnabled(bool enabled) {
    if (m_kpodEnabled != enabled) {
        m_kpodEnabled = enabled;
        save();
        emit kpodEnabledChanged(enabled);
    }
}

QString RadioSettings::kpa1500Host() const {
    return m_kpa1500Host;
}

void RadioSettings::setKpa1500Host(const QString &host) {
    if (m_kpa1500Host != host) {
        m_kpa1500Host = host;
        save();
        emit kpa1500SettingsChanged();
    }
}

quint16 RadioSettings::kpa1500Port() const {
    return m_kpa1500Port;
}

void RadioSettings::setKpa1500Port(quint16 port) {
    if (m_kpa1500Port != port) {
        m_kpa1500Port = port;
        save();
        emit kpa1500SettingsChanged();
    }
}

bool RadioSettings::kpa1500Enabled() const {
    return m_kpa1500Enabled;
}

void RadioSettings::setKpa1500Enabled(bool enabled) {
    if (m_kpa1500Enabled != enabled) {
        m_kpa1500Enabled = enabled;
        save();
        emit kpa1500EnabledChanged(enabled);
    }
}

int RadioSettings::kpa1500PollInterval() const {
    return m_kpa1500PollInterval;
}

void RadioSettings::setKpa1500PollInterval(int intervalMs) {
    // Clamp to reasonable range: 100ms - 5000ms
    intervalMs = qBound(100, intervalMs, 5000);
    if (m_kpa1500PollInterval != intervalMs) {
        m_kpa1500PollInterval = intervalMs;
        save();
        emit kpa1500PollIntervalChanged(intervalMs);
    }
}

int RadioSettings::volume() const {
    return m_settings.value("audio/volume", 45).toInt();
}

void RadioSettings::setVolume(int value) {
    value = qBound(0, value, 100);
    m_settings.setValue("audio/volume", value);
    m_settings.sync();
}

int RadioSettings::subVolume() const {
    return m_settings.value("audio/subVolume", 45).toInt();
}

void RadioSettings::setSubVolume(int value) {
    value = qBound(0, value, 100);
    m_settings.setValue("audio/subVolume", value);
    m_settings.sync();
}

int RadioSettings::textDecodeFontSize(bool subRx) const {
    // Default 9 px matches K4Styles::Dimensions::FontSizeNormal — keep first-launch look.
    const int defaultSize = 9;
    int value = m_settings.value(subRx ? "textDecode/subFontSize" : "textDecode/mainFontSize", defaultSize).toInt();
    return qBound(7, value, 24);
}

void RadioSettings::setTextDecodeFontSize(bool subRx, int sizePx) {
    sizePx = qBound(7, sizePx, 24);
    m_settings.setValue(subRx ? "textDecode/subFontSize" : "textDecode/mainFontSize", sizePx);
    m_settings.sync();
}

int RadioSettings::iaruRegion() const {
    // 1/2/3 = IARU regions, 4 = US (FCC). Default Region 2 (Americas).
    return qBound(1, m_settings.value("station/iaruRegion", 2).toInt(), 4);
}

void RadioSettings::setIaruRegion(int region) {
    region = qBound(1, region, 4);
    if (iaruRegion() != region) {
        m_settings.setValue("station/iaruRegion", region);
        m_settings.sync();
        emit iaruRegionChanged(region);
    }
}

QString RadioSettings::callSign() const {
    return m_settings.value("station/callSign", "").toString();
}

void RadioSettings::setCallSign(const QString &callSign) {
    m_settings.setValue("station/callSign", callSign.trimmed().toUpper());
    m_settings.sync();
}

QString RadioSettings::gridSquare() const {
    return m_settings.value("station/gridSquare", "").toString();
}

void RadioSettings::setGridSquare(const QString &grid) {
    m_settings.setValue("station/gridSquare", grid.trimmed());
    m_settings.sync();
}

QString RadioSettings::operatorName() const {
    return m_settings.value("station/operatorName", "").toString();
}

void RadioSettings::setOperatorName(const QString &name) {
    m_settings.setValue("station/operatorName", name.trimmed());
    m_settings.sync();
}

QString RadioSettings::qth() const {
    return m_settings.value("station/qth", "").toString();
}

void RadioSettings::setQth(const QString &qth) {
    m_settings.setValue("station/qth", qth.trimmed());
    m_settings.sync();
}

bool RadioSettings::bandPlanOverlayEnabled() const {
    return m_settings.value("station/bandPlanOverlay", false).toBool();
}

void RadioSettings::setBandPlanOverlayEnabled(bool enabled) {
    if (bandPlanOverlayEnabled() != enabled) {
        m_settings.setValue("station/bandPlanOverlay", enabled);
        m_settings.sync();
        emit bandPlanOverlayEnabledChanged(enabled);
    }
}

int RadioSettings::micGain() const {
    return m_settings.value("audio/micGain", 25).toInt();
}

void RadioSettings::setMicGain(int value) {
    value = qBound(0, value, 100);
    int oldValue = m_settings.value("audio/micGain", 25).toInt();
    if (oldValue != value) {
        m_settings.setValue("audio/micGain", value);
        m_settings.sync();
        emit micGainChanged(value);
    }
}

QString RadioSettings::micDevice() const {
    return m_settings.value("audio/micDevice", "").toString();
}

void RadioSettings::setMicDevice(const QString &deviceId) {
    QString oldDevice = m_settings.value("audio/micDevice", "").toString();
    if (oldDevice != deviceId) {
        m_settings.setValue("audio/micDevice", deviceId);
        m_settings.sync();
        emit micDeviceChanged(deviceId);
    }
}

QString RadioSettings::speakerDevice() const {
    return m_settings.value("audio/speakerDevice", "").toString();
}

void RadioSettings::setSpeakerDevice(const QString &deviceId) {
    QString oldDevice = m_settings.value("audio/speakerDevice", "").toString();
    if (oldDevice != deviceId) {
        m_settings.setValue("audio/speakerDevice", deviceId);
        m_settings.sync();
        emit speakerDeviceChanged(deviceId);
    }
}

bool RadioSettings::catServerEnabled() const {
    return m_catServerEnabled;
}

void RadioSettings::setCatServerEnabled(bool enabled) {
    if (m_catServerEnabled != enabled) {
        m_catServerEnabled = enabled;
        save();
        emit catServerEnabledChanged(enabled);
    }
}

quint16 RadioSettings::catServerPort() const {
    return m_catServerPort;
}

void RadioSettings::setCatServerPort(quint16 port) {
    // Clamp to valid port range (1024-65535)
    port = qBound(quint16(1024), port, quint16(65535));
    if (m_catServerPort != port) {
        m_catServerPort = port;
        save();
        emit catServerPortChanged(port);
    }
}

QMap<QString, MacroEntry> RadioSettings::macros() const {
    return m_macros;
}

MacroEntry RadioSettings::macro(const QString &functionId) const {
    return m_macros.value(functionId);
}

void RadioSettings::setMacro(const QString &functionId, const QString &label, const QString &command) {
    MacroEntry entry;
    entry.functionId = functionId;
    entry.label = label;
    entry.command = command;

    if (command.isEmpty()) {
        // Remove empty macros
        if (m_macros.contains(functionId)) {
            m_macros.remove(functionId);
            save();
            emit macrosChanged();
        }
    } else {
        // Add or update macro
        bool changed = !m_macros.contains(functionId) || m_macros[functionId].label != label ||
                       m_macros[functionId].command != command;
        if (changed) {
            m_macros[functionId] = entry;
            save();
            emit macrosChanged();
        }
    }
}

QMap<QString, MacroEntry> RadioSettings::cwMacros() const {
    return m_cwMacros;
}

MacroEntry RadioSettings::cwMacro(const QString &functionId) const {
    return m_cwMacros.value(functionId);
}

void RadioSettings::setCwMacro(const QString &functionId, const QString &label, const QString &text) {
    MacroEntry entry;
    entry.functionId = functionId;
    entry.label = label;
    entry.command = text;

    if (text.isEmpty()) {
        if (m_cwMacros.contains(functionId)) {
            m_cwMacros.remove(functionId);
            save();
            emit cwMacrosChanged();
        }
    } else {
        const bool changed = !m_cwMacros.contains(functionId) || m_cwMacros[functionId].label != label ||
                             m_cwMacros[functionId].command != text;
        if (changed) {
            m_cwMacros[functionId] = entry;
            save();
            emit cwMacrosChanged();
        }
    }
}

bool RadioSettings::cwSendImmediateMode() const {
    return m_cwSendImmediateMode;
}

void RadioSettings::setCwSendImmediateMode(bool immediate) {
    if (m_cwSendImmediateMode != immediate) {
        m_cwSendImmediateMode = immediate;
        save();
        emit cwSendImmediateModeChanged(immediate);
    }
}

namespace {
// QSettings group per keyer role. Historical note: both roles were once a single "halikey"
// group, migrated forward in load().
const char *roleKey(RadioSettings::KeyerRole role) {
    return role == RadioSettings::KeyerRoleStraightKey ? "straightkey" : "keyer";
}
} // namespace

QString RadioSettings::keyerPortName(KeyerRole role) const {
    return m_keyerPortName[role];
}

void RadioSettings::setKeyerPortName(KeyerRole role, const QString &portName) {
    if (m_keyerPortName[role] != portName) {
        m_keyerPortName[role] = portName;
        save();
        emit keyerConfigChanged(role);
    }
}

int RadioSettings::keyerDeviceType(KeyerRole role) const {
    return m_keyerDeviceType[role];
}

void RadioSettings::setKeyerDeviceType(KeyerRole role, int type) {
    if (m_keyerDeviceType[role] != type) {
        m_keyerDeviceType[role] = type;
        save();
        emit keyerConfigChanged(role);
    }
}

bool RadioSettings::keyerAutoConnect(KeyerRole role) const {
    return m_keyerAutoConnect[role];
}

void RadioSettings::setKeyerAutoConnect(KeyerRole role, bool enabled) {
    if (m_keyerAutoConnect[role] != enabled) {
        m_keyerAutoConnect[role] = enabled;
        save();
        emit keyerConfigChanged(role);
    }
}

bool RadioSettings::straightKeyBufferEnabled() const {
    return m_straightKeyBufferEnabled;
}

void RadioSettings::setStraightKeyBufferEnabled(bool enabled) {
    if (m_straightKeyBufferEnabled != enabled) {
        m_straightKeyBufferEnabled = enabled;
        save();
        emit straightKeyTimingChanged();
    }
}

int RadioSettings::straightKeyMinWpm() const {
    return m_straightKeyMinWpm;
}

void RadioSettings::setStraightKeyMinWpm(int wpm) {
    // Never above the fastest: the pair defines a range, and inverting it would make the
    // pre-roll and bounce floor contradict each other.
    wpm = qBound(5, wpm, qMin(80, m_straightKeyMaxWpm));
    if (m_straightKeyMinWpm != wpm) {
        m_straightKeyMinWpm = wpm;
        save();
        emit straightKeyTimingChanged();
    }
}

int RadioSettings::straightKeyMaxWpm() const {
    return m_straightKeyMaxWpm;
}

void RadioSettings::setStraightKeyMaxWpm(int wpm) {
    wpm = qBound(qMax(5, m_straightKeyMinWpm), wpm, 80);
    if (m_straightKeyMaxWpm != wpm) {
        m_straightKeyMaxWpm = wpm;
        save();
        emit straightKeyTimingChanged();
    }
}

double RadioSettings::straightKeyDahDitRatio() const {
    return m_straightKeyDahDitRatio;
}

void RadioSettings::setStraightKeyDahDitRatio(double ratio) {
    ratio = qBound(2.5, ratio, 5.0);
    if (!qFuzzyCompare(m_straightKeyDahDitRatio, ratio)) {
        m_straightKeyDahDitRatio = ratio;
        save();
        emit straightKeyTimingChanged();
    }
}

int RadioSettings::sidetoneVolume() const {
    return m_sidetoneVolume;
}

void RadioSettings::setSidetoneVolume(int value) {
    value = qBound(0, value, 100);
    if (m_sidetoneVolume != value) {
        m_sidetoneVolume = value;
        save();
        emit sidetoneVolumeChanged(value);
    }
}

// =============================================================================
// KPOD+ Keyer Settings
// =============================================================================

int RadioSettings::kpodPlusEncodeMode() const {
    return m_kpodPlusEncodeMode;
}

void RadioSettings::setKpodPlusEncodeMode(int mode) {
    mode = qBound(0, mode, 1);
    if (m_kpodPlusEncodeMode != mode) {
        m_kpodPlusEncodeMode = mode;
        save();
        emit kpodPlusSettingsChanged();
    }
}

EqPreset RadioSettings::rxEqPreset(int index) const {
    if (index >= 0 && index < 4) {
        return m_rxEqPresets[index];
    }
    return EqPreset();
}

void RadioSettings::setRxEqPreset(int index, const EqPreset &preset) {
    if (index >= 0 && index < 4) {
        m_rxEqPresets[index] = preset;
        save();
        emit rxEqPresetsChanged();
    }
}

void RadioSettings::clearRxEqPreset(int index) {
    if (index >= 0 && index < 4) {
        m_rxEqPresets[index] = EqPreset();
        save();
        emit rxEqPresetsChanged();
    }
}

EqPreset RadioSettings::txEqPreset(int index) const {
    if (index >= 0 && index < 4) {
        return m_txEqPresets[index];
    }
    return EqPreset();
}

void RadioSettings::setTxEqPreset(int index, const EqPreset &preset) {
    if (index >= 0 && index < 4) {
        m_txEqPresets[index] = preset;
        save();
        emit txEqPresetsChanged();
    }
}

void RadioSettings::clearTxEqPreset(int index) {
    if (index >= 0 && index < 4) {
        m_txEqPresets[index] = EqPreset();
        save();
        emit txEqPresetsChanged();
    }
}

QVector<DxClusterEntry> RadioSettings::dxClusters() const {
    return m_dxClusters;
}

void RadioSettings::addDxCluster(const DxClusterEntry &entry) {
    m_dxClusters.append(entry);
    save();
    emit dxClusterSettingsChanged();
}

void RadioSettings::removeDxCluster(int index) {
    if (index >= 0 && index < m_dxClusters.size()) {
        m_dxClusters.removeAt(index);
        save();
        emit dxClusterSettingsChanged();
    }
}

void RadioSettings::updateDxCluster(int index, const DxClusterEntry &entry) {
    if (index >= 0 && index < m_dxClusters.size()) {
        m_dxClusters[index] = entry;
        save();
        emit dxClusterSettingsChanged();
    }
}

int RadioSettings::dxClusterSpotAge() const {
    return m_dxClusterSpotAge;
}

void RadioSettings::setDxClusterSpotAge(int seconds) {
    seconds = qBound(300, seconds, 1800); // 5-30 minutes
    if (m_dxClusterSpotAge != seconds) {
        m_dxClusterSpotAge = seconds;
        save();
        emit dxClusterSettingsChanged();
    }
}

QString RadioSettings::dxClusterCallsign() const {
    return m_dxClusterCallsign;
}

void RadioSettings::setDxClusterCallsign(const QString &callsign) {
    QString upper = callsign.trimmed().toUpper();
    if (m_dxClusterCallsign != upper) {
        m_dxClusterCallsign = upper;
        save();
        emit dxClusterSettingsChanged();
    }
}

int RadioSettings::dxClusterSpotFontSize() const {
    return m_dxClusterSpotFontSize;
}

void RadioSettings::setDxClusterSpotFontSize(int sizePx) {
    sizePx = qBound(8, sizePx, 16); // mirrors K4Styles::Dimensions::FontSizeSpot{Min,Max}
    if (m_dxClusterSpotFontSize != sizePx) {
        m_dxClusterSpotFontSize = sizePx;
        save();
        emit dxClusterSettingsChanged();
    }
}

void RadioSettings::load() {
    int count = m_settings.beginReadArray("radios");
    m_radios.clear();
    for (int i = 0; i < count; ++i) {
        m_settings.setArrayIndex(i);
        RadioEntry entry;
        entry.name = m_settings.value("name").toString();
        entry.host = m_settings.value("host").toString();
        entry.password = deobfuscatePassword(m_settings.value("password").toString());
        entry.port = m_settings.value("port").toUInt();
        entry.useTls = m_settings.value("useTls", false).toBool();
        entry.identity = m_settings.value("identity").toString();
        entry.encodeMode = m_settings.value("encodeMode", 3).toInt();             // Default EM3 (Opus Float)
        entry.streamingLatency = m_settings.value("streamingLatency", 3).toInt(); // Default SL3
        entry.displayFps = m_settings.value("displayFps", 15).toInt();            // Default 15 FPS
        m_radios.append(entry);
    }
    m_settings.endArray();
    sortRadios();

    m_lastSelectedIndex = m_settings.value("lastSelectedIndex", -1).toInt();
    m_kpodEnabled = m_settings.value("kpodEnabled", false).toBool();

    // KPA1500 settings
    m_kpa1500Host = m_settings.value("kpa1500/host", "").toString();
    m_kpa1500Port = m_settings.value("kpa1500/port", 1500).toUInt();
    m_kpa1500Enabled = m_settings.value("kpa1500/enabled", false).toBool();
    m_kpa1500PollInterval = m_settings.value("kpa1500/pollInterval", 300).toInt();

    // CAT Server settings (migrate from old rigctld keys if present)
    m_catServerEnabled = m_settings.value("catServer/enabled", m_settings.value("rigctld/enabled", false)).toBool();
    m_catServerPort = m_settings.value("catServer/port", m_settings.value("rigctld/port", 9299)).toUInt();

    // DX Cluster settings
    int dxCount = m_settings.beginReadArray("dxClusters");
    m_dxClusters.clear();
    for (int i = 0; i < dxCount; ++i) {
        m_settings.setArrayIndex(i);
        DxClusterEntry entry;
        entry.host = m_settings.value("host").toString();
        entry.port = m_settings.value("port", 7000).toUInt();
        entry.callsign = m_settings.value("callsign").toString();
        entry.autoConnect = m_settings.value("autoConnect", false).toBool();
        m_dxClusters.append(entry);
    }
    m_settings.endArray();
    m_dxClusterSpotAge = m_settings.value("dxCluster/spotAge", 600).toInt();
    m_dxClusterCallsign = m_settings.value("dxCluster/callsign", "").toString();
    m_dxClusterSpotFontSize = qBound(8, m_settings.value("dxCluster/spotFontSize", 11).toInt(), 16);

    // Seed default cluster entry on first run
    if (m_dxClusters.isEmpty()) {
        DxClusterEntry rbn;
        rbn.host = "telnet.reversebeacon.net";
        rbn.port = 7300;
        m_dxClusters.append(rbn);
    }

    // Keyer settings, per role.
    for (int r = 0; r < KeyerRoleCount; ++r) {
        const QString g = QString::fromLatin1(roleKey(static_cast<KeyerRole>(r)));
        m_keyerPortName[r] = m_settings.value(g + "/portName", "").toString();
        m_keyerDeviceType[r] = m_settings.value(g + "/deviceType", 0).toInt();
        m_keyerAutoConnect[r] = m_settings.value(g + "/autoConnect", false).toBool();
    }
    // One-time migration from the pre-role "halikey" group. The old build had a single
    // device plus a straightKeyMode flag deciding what it was, so fold it into whichever
    // role that flag selected. Idempotent — the source key is cleared afterwards.
    if (m_settings.contains("halikey/portName")) {
        const int r = m_settings.value("halikey/straightKeyMode", false).toBool() ? KeyerRoleStraightKey
                                                                                  : KeyerRolePaddle;
        m_keyerPortName[r] = m_settings.value("halikey/portName", "").toString();
        m_keyerDeviceType[r] = m_settings.value("halikey/deviceType", 0).toInt();
        m_settings.remove("halikey/portName");
        m_settings.remove("halikey/enabled");
        m_settings.remove("halikey/deviceType");
        m_settings.remove("halikey/straightKeyMode");
    }
    m_sidetoneVolume = m_settings.value("halikey/sidetoneVolume", 30).toInt();
    m_straightKeyBufferEnabled = m_settings.value("straightkey/bufferEnabled", false).toBool();
    m_straightKeyMinWpm = m_settings.value("straightkey/minWpm", 20).toInt();
    m_straightKeyMaxWpm = m_settings.value("straightkey/maxWpm", 35).toInt();
    m_straightKeyDahDitRatio = m_settings.value("straightkey/dahDitRatio", 4.0).toDouble();
    // Clamp after both are read — a hand-edited or older config could invert the pair.
    m_straightKeyMaxWpm = qBound(15, m_straightKeyMaxWpm, 80);
    m_straightKeyMinWpm = qBound(5, m_straightKeyMinWpm, m_straightKeyMaxWpm);

    // KPOD+ keyer settings
    m_kpodPlusEncodeMode = m_settings.value("kpodPlus/encodeMode", 0).toInt();

    // Macro settings
    int macroCount = m_settings.beginReadArray("macros");
    m_macros.clear();
    for (int i = 0; i < macroCount; ++i) {
        m_settings.setArrayIndex(i);
        MacroEntry entry;
        entry.functionId = m_settings.value("functionId").toString();
        entry.label = m_settings.value("label").toString();
        entry.command = m_settings.value("command").toString();
        if (!entry.functionId.isEmpty()) {
            m_macros[entry.functionId] = entry;
        }
    }
    m_settings.endArray();

    // CW Send macros — own array, kept distinct from "macros" above.
    int cwMacroCount = m_settings.beginReadArray("cwMacros");
    m_cwMacros.clear();
    for (int i = 0; i < cwMacroCount; ++i) {
        m_settings.setArrayIndex(i);
        MacroEntry entry;
        entry.functionId = m_settings.value("functionId").toString();
        entry.label = m_settings.value("label").toString();
        entry.command = m_settings.value("command").toString();
        if (!entry.functionId.isEmpty()) {
            m_cwMacros[entry.functionId] = entry;
        }
    }
    m_settings.endArray();
    m_cwSendImmediateMode = m_settings.value("cwSend/immediateMode", false).toBool();

    // RX EQ Presets (4 slots)
    for (int i = 0; i < 4; ++i) {
        QString prefix = QString("rxEqPresets/%1/").arg(i);
        m_rxEqPresets[i].name = m_settings.value(prefix + "name", "").toString();
        QString bandsStr = m_settings.value(prefix + "bands", "").toString();
        m_rxEqPresets[i].bands.clear();
        if (!bandsStr.isEmpty()) {
            QStringList bandsList = bandsStr.split(",");
            for (const QString &val : bandsList) {
                bool ok;
                int dB = val.toInt(&ok);
                if (ok) {
                    m_rxEqPresets[i].bands.append(qBound(-16, dB, 16));
                }
            }
        }
    }

    // TX EQ Presets (4 slots)
    for (int i = 0; i < 4; ++i) {
        QString prefix = QString("txEqPresets/%1/").arg(i);
        m_txEqPresets[i].name = m_settings.value(prefix + "name", "").toString();
        QString bandsStr = m_settings.value(prefix + "bands", "").toString();
        m_txEqPresets[i].bands.clear();
        if (!bandsStr.isEmpty()) {
            QStringList bandsList = bandsStr.split(",");
            for (const QString &val : bandsList) {
                bool ok;
                int dB = val.toInt(&ok);
                if (ok) {
                    m_txEqPresets[i].bands.append(qBound(-16, dB, 16));
                }
            }
        }
    }
}

void RadioSettings::sortRadios() {
    std::sort(m_radios.begin(), m_radios.end(), [](const RadioEntry &a, const RadioEntry &b) {
        QString nameA = a.name.isEmpty() ? a.host : a.name;
        QString nameB = b.name.isEmpty() ? b.host : b.name;
        return nameA.compare(nameB, Qt::CaseInsensitive) < 0;
    });
}

void RadioSettings::save() {
    m_settings.beginWriteArray("radios");
    for (int i = 0; i < m_radios.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("name", m_radios[i].name);
        m_settings.setValue("host", m_radios[i].host);
        m_settings.setValue("password", obfuscatePassword(m_radios[i].password));
        m_settings.setValue("port", m_radios[i].port);
        m_settings.setValue("useTls", m_radios[i].useTls);
        m_settings.setValue("identity", m_radios[i].identity);
        m_settings.setValue("encodeMode", m_radios[i].encodeMode);
        m_settings.setValue("streamingLatency", m_radios[i].streamingLatency);
        m_settings.setValue("displayFps", m_radios[i].displayFps);
    }
    m_settings.endArray();

    m_settings.setValue("lastSelectedIndex", m_lastSelectedIndex);
    m_settings.setValue("kpodEnabled", m_kpodEnabled);

    // KPA1500 settings
    m_settings.setValue("kpa1500/host", m_kpa1500Host);
    m_settings.setValue("kpa1500/port", m_kpa1500Port);
    m_settings.setValue("kpa1500/enabled", m_kpa1500Enabled);
    m_settings.setValue("kpa1500/pollInterval", m_kpa1500PollInterval);

    // CAT Server settings
    m_settings.setValue("catServer/enabled", m_catServerEnabled);
    m_settings.setValue("catServer/port", m_catServerPort);

    // Keyer settings, per role.
    for (int r = 0; r < KeyerRoleCount; ++r) {
        const QString g = QString::fromLatin1(roleKey(static_cast<KeyerRole>(r)));
        m_settings.setValue(g + "/portName", m_keyerPortName[r]);
        m_settings.setValue(g + "/deviceType", m_keyerDeviceType[r]);
        m_settings.setValue(g + "/autoConnect", m_keyerAutoConnect[r]);
    }
    m_settings.setValue("halikey/sidetoneVolume", m_sidetoneVolume);
    m_settings.setValue("straightkey/bufferEnabled", m_straightKeyBufferEnabled);
    m_settings.setValue("straightkey/minWpm", m_straightKeyMinWpm);
    m_settings.setValue("straightkey/maxWpm", m_straightKeyMaxWpm);
    m_settings.setValue("straightkey/dahDitRatio", m_straightKeyDahDitRatio);

    // KPOD+ keyer settings
    m_settings.setValue("kpodPlus/encodeMode", m_kpodPlusEncodeMode);

    // Macro settings
    m_settings.beginWriteArray("macros");
    int i = 0;
    for (auto it = m_macros.constBegin(); it != m_macros.constEnd(); ++it, ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("functionId", it->functionId);
        m_settings.setValue("label", it->label);
        m_settings.setValue("command", it->command);
    }
    m_settings.endArray();

    // CW Send macros — own array, kept distinct from "macros" above.
    m_settings.beginWriteArray("cwMacros");
    int cwI = 0;
    for (auto it = m_cwMacros.constBegin(); it != m_cwMacros.constEnd(); ++it, ++cwI) {
        m_settings.setArrayIndex(cwI);
        m_settings.setValue("functionId", it->functionId);
        m_settings.setValue("label", it->label);
        m_settings.setValue("command", it->command);
    }
    m_settings.endArray();
    m_settings.setValue("cwSend/immediateMode", m_cwSendImmediateMode);

    // DX Cluster settings
    m_settings.beginWriteArray("dxClusters");
    for (int j = 0; j < m_dxClusters.size(); ++j) {
        m_settings.setArrayIndex(j);
        m_settings.setValue("host", m_dxClusters[j].host);
        m_settings.setValue("port", m_dxClusters[j].port);
        m_settings.setValue("callsign", m_dxClusters[j].callsign);
        m_settings.setValue("autoConnect", m_dxClusters[j].autoConnect);
    }
    m_settings.endArray();
    m_settings.setValue("dxCluster/spotAge", m_dxClusterSpotAge);
    m_settings.setValue("dxCluster/callsign", m_dxClusterCallsign);
    m_settings.setValue("dxCluster/spotFontSize", m_dxClusterSpotFontSize);

    // RX EQ Presets (4 slots)
    for (int j = 0; j < 4; ++j) {
        QString prefix = QString("rxEqPresets/%1/").arg(j);
        m_settings.setValue(prefix + "name", m_rxEqPresets[j].name);
        // Convert bands to comma-separated string
        QStringList bandsList;
        for (int dB : m_rxEqPresets[j].bands) {
            bandsList.append(QString::number(dB));
        }
        m_settings.setValue(prefix + "bands", bandsList.join(","));
    }

    // TX EQ Presets (4 slots)
    for (int j = 0; j < 4; ++j) {
        QString prefix = QString("txEqPresets/%1/").arg(j);
        m_settings.setValue(prefix + "name", m_txEqPresets[j].name);
        // Convert bands to comma-separated string
        QStringList bandsList;
        for (int dB : m_txEqPresets[j].bands) {
            bandsList.append(QString::number(dB));
        }
        m_settings.setValue(prefix + "bands", bandsList.join(","));
    }

    m_settings.sync();
}
