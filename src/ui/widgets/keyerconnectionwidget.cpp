#include "ui/widgets/keyerconnectionwidget.h"

#include "hardware/halikeydevice.h"
#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <functional>
#include <QVBoxLayout>

namespace {
constexpr int kPollIntervalMs = 1500;

// Rebuilding the combo on a popup-open keeps the list current without a Refresh button.
class RefreshingComboBox : public QComboBox {
public:
    using Refresher = std::function<void()>;
    RefreshingComboBox(Refresher r, QWidget *parent) : QComboBox(parent), m_refresh(std::move(r)) {}
    void showPopup() override {
        if (m_refresh)
            m_refresh();
        QComboBox::showPopup();
    }

private:
    Refresher m_refresh;
};
} // namespace

KeyerConnectionWidget::KeyerConnectionWidget(RadioSettings::KeyerRole role, HalikeyDevice *device,
                                             HalikeyDevice *otherDevice, QWidget *parent)
    : QWidget(parent), m_role(role), m_device(device), m_otherDevice(otherDevice) {

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Device type
    auto *typeRow = new QHBoxLayout();
    auto *typeLabel = new QLabel("Device Type:", this);
    typeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    typeLabel->setMinimumWidth(K4Styles::Dimensions::FormLabelWidth);
    m_deviceTypeCombo = new QComboBox(this);
    m_deviceTypeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_deviceTypeCombo->setMinimumHeight(K4Styles::Dimensions::ComboMinHeight);
    m_deviceTypeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_deviceTypeCombo->addItem("HaliKey V1.4 (serial)", 0);
    m_deviceTypeCombo->addItem("HaliKey MIDI", 1);
    m_deviceTypeCombo->setCurrentIndex(RadioSettings::instance()->keyerDeviceType(m_role));
    connect(m_deviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKeyerDeviceType(m_role, m_deviceTypeCombo->itemData(index).toInt());
        if (m_device->isConnected())
            m_device->closePort();
        // The saved name belongs to the previous transport — a serial port is meaningless as
        // a MIDI device name and would otherwise be handed straight to openPort().
        RadioSettings::instance()->setKeyerPortName(m_role, QString());
        m_lastPorts.clear();
        rebuildPorts();
        updateStatus();
    });
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_deviceTypeCombo, 1);
    layout->addLayout(typeRow);

    // Port
    auto *portRow = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setMinimumWidth(K4Styles::Dimensions::FormLabelWidth);
    m_portCombo = new RefreshingComboBox([this]() { rebuildPorts(); }, this);
    m_portCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_portCombo->setMinimumHeight(K4Styles::Dimensions::ComboMinHeight);
    m_portCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        RadioSettings::instance()->setKeyerPortName(m_role, m_portCombo->itemData(index).toString());
        updateStatus();
    });
    portRow->addWidget(portLabel);
    portRow->addWidget(m_portCombo, 1);
    layout->addLayout(portRow);

    m_autoConnectCheck = new QCheckBox("Connect automatically when this device is present", this);
    m_autoConnectCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_autoConnectCheck->setChecked(RadioSettings::instance()->keyerAutoConnect(m_role));
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        RadioSettings::instance()->setKeyerAutoConnect(m_role, checked);
    });
    layout->addWidget(m_autoConnectCheck);

    m_connectBtn = new QPushButton("Connect", this);
    m_connectBtn->setStyleSheet(K4Styles::Dialog::actionButton());
    m_connectBtn->setMinimumHeight(K4Styles::Dimensions::ComboMinHeight);
    m_connectBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_connectBtn, &QPushButton::clicked, this, &KeyerConnectionWidget::onConnectClicked);
    layout->addWidget(m_connectBtn);

    auto *statusRow = new QHBoxLayout();
    auto *statusTitle = new QLabel("Status:", this);
    statusTitle->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitle->setMinimumWidth(K4Styles::Dimensions::FormLabelWidth);
    m_statusLabel = new QLabel("Not Connected", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(statusTitle);
    statusRow->addWidget(m_statusLabel, 1);
    layout->addLayout(statusRow);

    connect(m_device, &HalikeyDevice::connected, this, [this]() { updateStatus(); });
    connect(m_device, &HalikeyDevice::disconnected, this, [this]() { updateStatus(); });
    connect(m_device, &HalikeyDevice::connectionError, this, [this](const QString &) { updateStatus(); });
    // The other role connecting can create or clear a conflict for this one.
    connect(m_otherDevice, &HalikeyDevice::connected, this, [this]() { updateStatus(); });
    connect(m_otherDevice, &HalikeyDevice::disconnected, this, [this]() { updateStatus(); });

    // Keeps the port list current so no Refresh button is needed. Auto-connect is NOT done
    // here — HardwareController owns it, because this page is lazily constructed and one the
    // operator never opens would never arm it. Windows serial enumeration is comparatively
    // expensive, so this only runs while the page is actually visible.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() { rebuildPorts(); });

    rebuildPorts();
    updateStatus();
}

void KeyerConnectionWidget::setPollingActive(bool active) {
    if (active && !m_pollTimer->isActive())
        m_pollTimer->start();
    else if (!active && m_pollTimer->isActive())
        m_pollTimer->stop();
}

QStringList KeyerConnectionWidget::enumeratePorts() const {
    QStringList out;
    if (RadioSettings::instance()->keyerDeviceType(m_role) == 1) {
        out = HalikeyDevice::availableMidiDevices();
    } else {
        for (const auto &p : HalikeyDevice::availablePortsDetailed())
            out << p.portName;
    }
    return out;
}

QString KeyerConnectionWidget::conflictingPort() const {
    // Only a live connection on the other role blocks us, and only on the same transport.
    if (!m_otherDevice->isConnected())
        return {};
    const auto otherRole = (m_role == RadioSettings::KeyerRoleStraightKey) ? RadioSettings::KeyerRolePaddle
                                                                          : RadioSettings::KeyerRoleStraightKey;
    auto *rs = RadioSettings::instance();
    if (rs->keyerDeviceType(otherRole) != rs->keyerDeviceType(m_role))
        return {};
    return m_otherDevice->portName();
}

void KeyerConnectionWidget::rebuildPorts() {
    const QStringList ports = enumeratePorts();
    if (ports == m_lastPorts)
        return; // unchanged — leave an open popup and the current selection alone
    m_lastPorts = ports;

    const QString saved = RadioSettings::instance()->keyerPortName(m_role);
    QSignalBlocker block(m_portCombo);
    m_portCombo->clear();
    for (const QString &p : ports)
        m_portCombo->addItem(p, p);

    int idx = m_portCombo->findData(saved);
    if (idx < 0 && RadioSettings::instance()->keyerDeviceType(m_role) == 1) {
        // Convenience: pick an obvious HaliKey MIDI device when nothing was saved.
        for (int i = 0; i < ports.size(); ++i) {
            if (ports[i].contains("HaliKey", Qt::CaseInsensitive)) {
                idx = i;
                break;
            }
        }
    }
    if (idx >= 0) {
        m_portCombo->setCurrentIndex(idx);
        // Persist it: the combo alone isn't the setting, and auto-connect reads the setting.
        RadioSettings::instance()->setKeyerPortName(m_role, m_portCombo->currentData().toString());
    }
    updateStatus();
}

void KeyerConnectionWidget::updateStatus() {
    const QString selected = m_portCombo->currentData().toString();
    const QString clash = conflictingPort();
    const bool conflicted = !selected.isEmpty() && selected == clash;
    const QString otherName =
        (m_role == RadioSettings::KeyerRoleStraightKey) ? QStringLiteral("Keyer") : QStringLiteral("Straight Key");

    if (m_device->isConnected()) {
        m_statusLabel->setText(QStringLiteral("Connected to %1").arg(m_device->portName()));
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        m_connectBtn->setText("Disconnect");
        m_connectBtn->setEnabled(true);
    } else if (conflicted) {
        m_statusLabel->setText(QStringLiteral("In use by %1").arg(otherName));
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::AccentAmber));
        m_connectBtn->setText("Connect");
        m_connectBtn->setEnabled(false);
    } else {
        m_statusLabel->setText("Not Connected");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
        m_connectBtn->setText("Connect");
        m_connectBtn->setEnabled(!selected.isEmpty());
    }
}

void KeyerConnectionWidget::onConnectClicked() {
    if (m_device->isConnected()) {
        m_device->closePort();
    } else {
        const QString port = m_portCombo->currentData().toString();
        if (port.isEmpty() || port == conflictingPort() || !enumeratePorts().contains(port))
            return;
        RadioSettings::instance()->setKeyerPortName(m_role, port);
        m_device->openPort(port);
    }
    updateStatus();
}

void KeyerConnectionWidget::refresh() {
    m_deviceTypeCombo->setCurrentIndex(RadioSettings::instance()->keyerDeviceType(m_role));
    m_autoConnectCheck->setChecked(RadioSettings::instance()->keyerAutoConnect(m_role));
    m_lastPorts.clear();
    rebuildPorts();
    updateStatus();
}
