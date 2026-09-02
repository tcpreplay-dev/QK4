#include "ui/pages/kpodpage.h"
#include "ui/styling/k4styles.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "settings/radiosettings.h"
#include "utils/macroids.h"
#include "utils/k4macrobackup.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVector>

namespace {

// Three pairs per row overflowed the Options dialog's default width.
constexpr int kSummaryPairsPerRow = 2;

// Sized so the tap and hold blocks both fit the default dialog width.
constexpr int kMacroButtonColumnWidth = 62;
constexpr int kMacroLabelColumnWidth = 88;
constexpr int kMacroCommandMinWidth = 130;

// K4Styles::Dialog::lineEdit() geometry, page background — does not invite editing.
QString macroReadOnlyFieldStyle() {
    return QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; "
                   "padding: %4px; font-size: %5px; }")
        .arg(K4Styles::Colors::Background)
        .arg(K4Styles::Colors::TextWhite)
        .arg(K4Styles::Colors::DialogBorder)
        .arg(K4Styles::Dimensions::PaddingSmall)
        .arg(K4Styles::Dimensions::FontSizeButton);
}

} // namespace

KpodPage::KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent)
    : QWidget(parent), m_kpodDevice(kpodDevice), m_kpodPlusDevice(kpodPlusDevice) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    // Taller than the dialog once the macro grid is in, so the whole page scrolls.
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(QString("QScrollArea { border: none; background: transparent; }"
                                      "QScrollBar:vertical { background: %1; width: 8px; }"
                                      "QScrollBar:horizontal { background: %1; height: 8px; }"
                                      "QScrollBar::handle:vertical, QScrollBar::handle:horizontal "
                                      "{ background: %2; border-radius: 4px; }"
                                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, "
                                      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal "
                                      "{ height: 0; width: 0; }")
                                  .arg(K4Styles::Colors::OverlayContentBg)
                                  .arg(K4Styles::Colors::OverlayNavButton));
    outerLayout->addWidget(scrollArea);

    auto *content = new QWidget(scrollArea);
    content->setStyleSheet("background: transparent;");
    scrollArea->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *headerLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", content);
    statusLabel->setStyleSheet(K4Styles::Dialog::formLabel());

    m_kpodStatusLabel = new QLabel("Not Detected", content);

    m_kpodEnableCheckbox = new QCheckBox("Enable K-Pod", content);
    m_kpodEnableCheckbox->setChecked(RadioSettings::instance()->kpodEnabled());
    connect(m_kpodEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpodEnabled(checked); });

    headerLayout->addWidget(statusLabel);
    headerLayout->addWidget(m_kpodStatusLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_kpodEnableCheckbox);
    layout->addLayout(headerLayout);

    addSeparator(layout);
    setupDeviceSummary(layout);

    addSeparator(layout);
    setupMacroSection(layout);

    // KPOD+ keyer configuration section (hidden until KPOD+ detected)
    setupKeyerConfigSection(layout);

    // Help text
    m_kpodHelpLabel = new QLabel("Connect a K-Pod device to enable this feature.", content);
    m_kpodHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    m_kpodHelpLabel->setWordWrap(true);
    layout->addWidget(m_kpodHelpLabel);

    layout->addStretch();

    // Connect to device signals for real-time status updates
    if (m_kpodDevice) {
        connect(m_kpodDevice, &KpodDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodDevice, &KpodDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }
    if (m_kpodPlusDevice) {
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }

    // MacroDialog edits the same store; reload so the two views cannot drift apart.
    connect(RadioSettings::instance(), &RadioSettings::macrosChanged, this, &KpodPage::loadMacrosFromSettings);

    // Initialize with current status
    updateKpodStatus();
}

QFrame *KpodPage::addSeparator(QVBoxLayout *layout) {
    auto *line = new QFrame(layout->parentWidget());
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);
    return line;
}

void KpodPage::setupDeviceSummary(QVBoxLayout *layout) {
    QWidget *parent = layout->parentWidget();

    auto *titleLabel = new QLabel("Device Summary", parent);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    auto *tableWidget = new QWidget(parent);
    auto *tableLayout = new QGridLayout(tableWidget);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setHorizontalSpacing(K4Styles::Dimensions::PaddingMedium);
    tableLayout->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    const QString headerStyle =
        K4Styles::Dialog::labelTextBold(K4Styles::Colors::TextGray, K4Styles::Dimensions::FontSizeButton);

    QStringList properties = {"Product Name", "Manufacturer",     "Vendor ID", "Product ID",
                              "Device Type",  "Firmware Version", "Device ID"};
    QVector<QLabel **> valueLabels = {&m_kpodProductLabel,   &m_kpodManufacturerLabel, &m_kpodVendorIdLabel,
                                      &m_kpodProductIdLabel, &m_kpodDeviceTypeLabel,   &m_kpodFirmwareLabel,
                                      &m_kpodDeviceIdLabel};

    for (int i = 0; i < properties.size(); ++i) {
        const int row = i / kSummaryPairsPerRow;
        const int col = (i % kSummaryPairsPerRow) * 2;

        auto *propLabel = new QLabel(properties[i], tableWidget);
        propLabel->setStyleSheet(headerStyle);

        *valueLabels[i] = new QLabel("N/A", tableWidget);

        tableLayout->addWidget(propLabel, row, col, Qt::AlignLeft);
        tableLayout->addWidget(*valueLabels[i], row, col + 1, Qt::AlignLeft);
    }

    for (int pair = 0; pair < kSummaryPairsPerRow; ++pair) {
        tableLayout->setColumnStretch(pair * 2 + 1, 1);
    }

    layout->addWidget(tableWidget);
}

void KpodPage::addMacroRow(QGridLayout *grid, int row, int gridColumn, const QString &functionId,
                           const QString &buttonName) {
    QWidget *parent = grid->parentWidget();

    auto *buttonLabel = new QLabel(buttonName, parent);
    buttonLabel->setStyleSheet(
        K4Styles::Dialog::labelTextBold(K4Styles::Colors::TextWhite, K4Styles::Dimensions::FontSizeButton));
    buttonLabel->setFixedWidth(kMacroButtonColumnWidth);

    // Read-only: MacroDialog is the only editor, so there is no second writer on the
    // same store. QLineEdit not QLabel so long commands stay scrollable and copyable.
    auto *labelEdit = new QLineEdit(parent);
    labelEdit->setStyleSheet(macroReadOnlyFieldStyle());
    labelEdit->setReadOnly(true);
    labelEdit->setFixedWidth(kMacroLabelColumnWidth);
    labelEdit->setPlaceholderText("—");

    auto *commandEdit = new QLineEdit(parent);
    commandEdit->setStyleSheet(macroReadOnlyFieldStyle());
    commandEdit->setReadOnly(true);
    commandEdit->setMinimumWidth(kMacroCommandMinWidth);
    commandEdit->setPlaceholderText("unused");

    grid->addWidget(buttonLabel, row, gridColumn, Qt::AlignLeft);
    grid->addWidget(labelEdit, row, gridColumn + 1);
    grid->addWidget(commandEdit, row, gridColumn + 2);

    m_macroRows.append({functionId, labelEdit, commandEdit});
}

void KpodPage::setupMacroSection(QVBoxLayout *layout) {
    QWidget *parent = layout->parentWidget();

    auto *titleLabel = new QLabel("Button Macros", parent);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    auto *gridWidget = new QWidget(parent);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(K4Styles::Dimensions::PaddingMedium);
    grid->setVerticalSpacing(K4Styles::Dimensions::PaddingSmall);

    const QString headerStyle =
        K4Styles::Dialog::labelTextBold(K4Styles::Colors::TextGray, K4Styles::Dimensions::FontSizeButton);

    // Repeated over the tap block (cols 0-2) and the hold block (cols 4-6).
    const QStringList headers = {"Button", "Name", "CAT Command"};
    for (int block = 0; block < 2; ++block) {
        for (int i = 0; i < headers.size(); ++i) {
            auto *header = new QLabel(headers[i], gridWidget);
            header->setStyleSheet(headerStyle);
            grid->addWidget(header, 0, block * 4 + i, Qt::AlignLeft);
        }
    }

    auto *divider = new QFrame(gridWidget);
    divider->setFrameShape(QFrame::VLine);
    divider->setStyleSheet(K4Styles::Dialog::separator());
    divider->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);
    grid->addWidget(divider, 0, 3, 9, 1);

    const QVector<QString> tapIds = {MacroIds::Kpod1T, MacroIds::Kpod2T, MacroIds::Kpod3T, MacroIds::Kpod4T,
                                     MacroIds::Kpod5T, MacroIds::Kpod6T, MacroIds::Kpod7T, MacroIds::Kpod8T};
    const QVector<QString> holdIds = {MacroIds::Kpod1H, MacroIds::Kpod2H, MacroIds::Kpod3H, MacroIds::Kpod4H,
                                      MacroIds::Kpod5H, MacroIds::Kpod6H, MacroIds::Kpod7H, MacroIds::Kpod8H};

    for (int i = 0; i < tapIds.size(); ++i) {
        addMacroRow(grid, i + 1, 0, tapIds[i], QString("F%1 Tap").arg(i + 1));
    }
    for (int i = 0; i < holdIds.size(); ++i) {
        addMacroRow(grid, i + 1, 4, holdIds[i], QString("F%1 Hold").arg(i + 1));
    }

    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(6, 1);
    layout->addWidget(gridWidget);

    auto *storageNote = new QLabel("Read-only here — edit these with Fn > MACROS. Macros are stored in QK4 "
                                   "settings (shared by all configured radios) and run when the K-Pod button "
                                   "is pressed.",
                                   parent);
    storageNote->setStyleSheet(K4Styles::Dialog::helpText());
    storageNote->setWordWrap(true);

    // Imports every slot the backup carries, not just the sixteen shown above.
    m_importBtn = new QPushButton("Import from K4 Backup…", parent);
    m_importBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_importBtn->setToolTip("Import K-Pod, PF and REM ANT macros from a k4macros.json file.\n"
                            "The K4 writes one under K4_SN<serial>/ when you back up with\n"
                            "Fn > hold BACKUP.");
    connect(m_importBtn, &QPushButton::clicked, this, &KpodPage::importFromK4Backup);

    auto *noteRow = new QHBoxLayout();
    noteRow->addWidget(storageNote, 1);
    noteRow->addStretch();
    noteRow->addWidget(m_importBtn, 0, Qt::AlignTop);
    layout->addLayout(noteRow);

    loadMacrosFromSettings();
}

void KpodPage::loadMacrosFromSettings() {
    auto *settings = RadioSettings::instance();
    for (const MacroRow &row : m_macroRows) {
        const MacroEntry entry = settings->macro(row.functionId);
        row.labelEdit->setText(entry.label);
        row.commandEdit->setText(entry.command);
        // Cursor at 0 so a long command shows its head, not its tail.
        row.commandEdit->setCursorPosition(0);
    }
}

void KpodPage::importFromK4Backup() {
    // The user says where the file is; QK4 never scans volumes on its own.
    const QString path =
        QFileDialog::getOpenFileName(this, "Import Macros from K4 Backup", QDir::homePath(),
                                     "K4 macro backup (k4macros.json);;JSON files (*.json);;All files (*)");
    if (path.isEmpty())
        return;

    QString error;
    const QMap<QString, MacroEntry> macros = K4MacroBackup::parseFile(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Import Failed", QString("Could not read %1:\n\n%2").arg(path, error));
        return;
    }
    if (macros.isEmpty()) {
        QMessageBox::information(this, "Nothing to Import",
                                 QString("%1 contains no assigned macros.").arg(QFileInfo(path).fileName()));
        return;
    }

    // Overwriting a slot that differs needs consent; filling an empty one does not.
    auto *settings = RadioSettings::instance();
    QStringList conflicts;
    int newCount = 0;
    int unchangedCount = 0;
    for (auto it = macros.constBegin(); it != macros.constEnd(); ++it) {
        const MacroEntry current = settings->macro(it.key());
        if (current.command.isEmpty()) {
            ++newCount;
        } else if (current.command == it->command && current.label == it->label) {
            ++unchangedCount;
        } else {
            conflicts.append(it.key());
        }
    }

    if (newCount == 0 && conflicts.isEmpty()) {
        QMessageBox::information(this, "Already Up To Date",
                                 QString("All %1 macros in the backup already match QK4.").arg(unchangedCount));
        return;
    }

    QString summary = QString("From %1:\n\n").arg(QFileInfo(path).fileName());
    if (newCount > 0)
        summary += QString("• %1 macro(s) into empty slots\n").arg(newCount);
    if (unchangedCount > 0)
        summary += QString("• %1 already identical\n").arg(unchangedCount);
    if (!conflicts.isEmpty()) {
        summary += QString("• %1 existing macro(s) will be OVERWRITTEN:\n    %2\n")
                       .arg(conflicts.size())
                       .arg(conflicts.join(", "));
    }
    summary += "\nProceed?";

    const auto answer = QMessageBox::question(this, "Import Macros from K4 Backup", summary,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const int written = settings->setMacros(macros);
    QMessageBox::information(this, "Import Complete",
                             QString("Imported %1 macro(s). Edit them with Fn > MACROS.").arg(written));
}

void KpodPage::setupKeyerConfigSection(QVBoxLayout *parentLayout) {
    QWidget *parent = parentLayout->parentWidget();

    // Member so it can be hidden along with the section it heads.
    m_keyerSeparator = addSeparator(parentLayout);

    m_keyerConfigWidget = new QWidget(parent);
    auto *keyerLayout = new QVBoxLayout(m_keyerConfigWidget);
    keyerLayout->setContentsMargins(0, 0, 0, 0);
    keyerLayout->setSpacing(K4Styles::Dimensions::PaddingMedium);

    // Section title
    auto *keyerTitle = new QLabel("KPOD+ Configuration", m_keyerConfigWidget);
    keyerTitle->setStyleSheet(K4Styles::Dialog::titleLabel());
    keyerLayout->addWidget(keyerTitle);

    // Grid for the keyer controls
    auto *gridWidget = new QWidget(m_keyerConfigWidget);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    grid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    const QString labelStyle =
        K4Styles::Dialog::labelTextBold(K4Styles::Colors::TextGray, K4Styles::Dimensions::FontSizeButton);

    auto *settings = RadioSettings::instance();

    // Encode Mode: Element (KZ) / ASCII (KX). This is the only KPOD+ keyer
    // setting still configured here — keyer speed, CW pitch, iambic mode and
    // paddle orientation now mirror the connected K4 (see CwController) and
    // have no manual control. Encode mode has no K4 equivalent.
    auto *encodeLabel = new QLabel("Encode Mode", gridWidget);
    encodeLabel->setStyleSheet(labelStyle);
    m_encodeModeCombo = new QComboBox(gridWidget);
    m_encodeModeCombo->addItem("Element (KZ)", 0);
    m_encodeModeCombo->addItem("ASCII (KX)", 1);
    m_encodeModeCombo->setCurrentIndex(settings->kpodPlusEncodeMode());
    grid->addWidget(encodeLabel, 0, 0, Qt::AlignLeft);
    grid->addWidget(m_encodeModeCombo, 0, 1, Qt::AlignLeft);

    grid->setColumnStretch(1, 1);
    keyerLayout->addWidget(gridWidget);

    connect(m_encodeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusEncodeMode(index);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setEncodeMode(index);
        }
    });

    parentLayout->addWidget(m_keyerConfigWidget);

    // Initially hidden
    m_keyerSeparator->setVisible(false);
    m_keyerConfigWidget->setVisible(false);
}

void KpodPage::refresh() {
    updateKpodStatus();
    loadMacrosFromSettings();
}

void KpodPage::updateKpodStatus() {
    if (!m_kpodStatusLabel)
        return;

    // Check both KPOD and KPOD+ — they can coexist
    bool kpodDetected = m_kpodDevice && m_kpodDevice->isDetected();
    bool kpodPlusDetected = m_kpodPlusDevice && m_kpodPlusDevice->isDetected();
    bool anyDetected = kpodDetected || kpodPlusDetected;

    // Styling
    QString valueStyle = K4Styles::Dialog::labelText(K4Styles::Colors::TextWhite, K4Styles::Dimensions::FontSizeButton);
    QString notDetectedStyle = QString("color: %1; font-size: %2px; font-style: italic;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizeButton);

    // Update status label — show which device is detected
    QString statusText;
    if (kpodPlusDetected && kpodDetected) {
        statusText = "KPOD + KPOD+ Detected";
    } else if (kpodPlusDetected) {
        statusText = "KPOD+ Detected";
    } else if (kpodDetected) {
        statusText = "Detected";
    } else {
        statusText = "Not Detected";
    }
    QString statusColor = anyDetected ? K4Styles::Colors::StatusGreen : K4Styles::Colors::ErrorRed;
    m_kpodStatusLabel->setText(statusText);
    m_kpodStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(statusColor));

    // Prefer KPOD+ info if both are detected, otherwise show KPOD
    auto setLabel = [&](QLabel *label, const QString &value) {
        QString displayValue = value.isEmpty() ? "N/A" : value;
        label->setText(displayValue);
        label->setStyleSheet(displayValue == "N/A" ? notDetectedStyle : valueStyle);
    };

    if (kpodPlusDetected) {
        KpodPlusDeviceInfo info = m_kpodPlusDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB Vendor-Specific (Keyer)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else if (kpodDetected) {
        KpodDeviceInfo info = m_kpodDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB HID (Human Interface Device)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else {
        setLabel(m_kpodProductLabel, "");
        setLabel(m_kpodManufacturerLabel, "");
        setLabel(m_kpodVendorIdLabel, "");
        setLabel(m_kpodProductIdLabel, "");
        setLabel(m_kpodDeviceTypeLabel, "");
        setLabel(m_kpodFirmwareLabel, "");
        setLabel(m_kpodDeviceIdLabel, "");
    }

    // Update checkbox enabled state and styling
    m_kpodEnableCheckbox->setEnabled(anyDetected);
    m_kpodEnableCheckbox->setStyleSheet(anyDetected ? K4Styles::Dialog::checkBox()
                                                    : K4Styles::Dialog::checkBoxDisabled());

    // Show/hide keyer configuration section based on KPOD+ detection
    if (m_keyerConfigWidget) {
        m_keyerConfigWidget->setVisible(kpodPlusDetected);
        m_keyerSeparator->setVisible(kpodPlusDetected);
    }

    // Update help text
    if (kpodPlusDetected) {
        m_kpodHelpLabel->setText("KPOD+ keyer is active. Paddle, keyer, and sidetone are handled by the device.");
    } else if (kpodDetected) {
        m_kpodHelpLabel->setText("When enabled, the K-Pod VFO knob and buttons will control the radio.");
    } else {
        m_kpodHelpLabel->setText("Connect a K-Pod device to enable this feature.");
    }
}
