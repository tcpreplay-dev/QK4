#include "ui/pages/straightkeypage.h"

#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/keyerconnectionwidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
// QLabel keeps an explicit stylesheet colour when disabled, so greying has to be done by hand.
void setLabelEnabled(QLabel *label, bool enabled) {
    label->setStyleSheet(enabled ? K4Styles::Dialog::formLabel()
                                 : K4Styles::Dialog::labelText(K4Styles::Colors::InactiveGray,
                                                               K4Styles::Dimensions::FontSizePopup));
}

QFrame *separator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    return line;
}
} // namespace

StraightKeyPage::StraightKeyPage(HalikeyDevice *device, HalikeyDevice *keyerDevice, QWidget *parent)
    : QWidget(parent) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *title = new QLabel("Straight Key", this);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(title);

    auto *desc = new QLabel("Send CW with a straight key, bug, or external keyer (e.g. WinKeyer). CW is "
                            "sent over the air with near original cadence, regardless of network jitter or traffic. "
                            "CW arrives at the rig with a 1 Dit/Dah delay plus any network latency delays "
                            "(speed of light sucks). Local sidetone is not delayed.\n"
                            "Wire your key to the Dah line (3.5mm plug tip for Haliday devices); "
                            "Dit is ignored on a 2-conductor plug. Mono plugs and properly wired USB serial "
                            "cables also work. Port selection between Keyer and Straight Key must be unique.",
                            this);
    desc->setWordWrap(true);
    desc->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(desc);

    m_connection = new KeyerConnectionWidget(RadioSettings::KeyerRoleStraightKey, device, keyerDevice, this);
    layout->addWidget(m_connection);

    layout->addWidget(separator(this));

    // ---- Timing ----------------------------------------------------------------
    auto *timingHeader = new QLabel("Timing", this);
    timingHeader->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(timingHeader);

    m_bufferCheck = new QCheckBox("Keep character timing accurate", this);
    m_bufferCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_bufferCheck->setChecked(RadioSettings::instance()->straightKeyBufferEnabled());
    connect(m_bufferCheck, &QCheckBox::toggled, this, [this](bool checked) {
        RadioSettings::instance()->setStraightKeyBufferEnabled(checked);
        updateBufferSummary();
    });
    layout->addWidget(m_bufferCheck);

    auto *bufferHelp = new QLabel("You probably don't need this if you have a \"gud fist\" or "
                                  "you use an external electronic keyer. If your CW has has a heavy "
                                  "weight, the K4's amazing CW replication capabilities can be "
                                  "tricked into sending, for example, \"ST\" instead of \"V\". "
                                  "Enabling this delays your transmission slightly so spacing stays "
                                  "correct. Your sidetone is unaffected either way.",
                                  this);
    bufferHelp->setWordWrap(true);
    bufferHelp->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(bufferHelp);

    // Expressed as a speed rather than milliseconds: the operator knows their own sending,
    // not the buffer arithmetic. The slowest speed sets the longest element to cover.
    auto *minRow = new QHBoxLayout();
    m_minWpmLabel = new QLabel("Slowest I send:", this);
    m_minWpmLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_minWpmLabel->setMinimumWidth(120);
    m_minWpmSpin = new QSpinBox(this);
    m_minWpmSpin->setRange(5, 80);
    m_minWpmSpin->setSuffix(" WPM");
    m_minWpmSpin->setValue(RadioSettings::instance()->straightKeyMinWpm());
    connect(m_minWpmSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        RadioSettings::instance()->setStraightKeyMinWpm(v);
        updateBufferSummary();
    });
    minRow->addWidget(m_minWpmLabel);
    minRow->addWidget(m_minWpmSpin);
    minRow->addStretch();
    layout->addLayout(minRow);

    // Sets the contact-bounce floor. A straight key's speed is the operator's hand, so
    // unlike the paddle it can't be read from the radio's KS.
    auto *maxRow = new QHBoxLayout();
    m_maxWpmLabel = new QLabel("Fastest I send:", this);
    m_maxWpmLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_maxWpmLabel->setMinimumWidth(120);
    m_maxWpmSpin = new QSpinBox(this);
    m_maxWpmSpin->setRange(15, 80);
    m_maxWpmSpin->setSuffix(" WPM");
    m_maxWpmSpin->setValue(RadioSettings::instance()->straightKeyMaxWpm());
    connect(m_maxWpmSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        RadioSettings::instance()->setStraightKeyMaxWpm(v);
        updateBufferSummary();
    });
    maxRow->addWidget(m_maxWpmLabel);
    maxRow->addWidget(m_maxWpmSpin);
    maxRow->addStretch();
    layout->addLayout(maxRow);

    auto *ratioRow = new QHBoxLayout();
    m_ratioLabel = new QLabel("My highest Dah / Dit ratio:", this);
    m_ratioLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_ratioLabel->setMinimumWidth(120);
    m_ratioSpin = new QDoubleSpinBox(this);
    m_ratioSpin->setRange(2.5, 5.0);
    m_ratioSpin->setSingleStep(0.1);
    m_ratioSpin->setDecimals(1);
    m_ratioSpin->setValue(RadioSettings::instance()->straightKeyDahDitRatio());
    connect(m_ratioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        RadioSettings::instance()->setStraightKeyDahDitRatio(v);
        updateBufferSummary();
    });
    ratioRow->addWidget(m_ratioLabel);
    ratioRow->addWidget(m_ratioSpin);
    ratioRow->addStretch();
    layout->addLayout(ratioRow);

    m_bufferSummary = new QLabel(this);
    m_bufferSummary->setWordWrap(true);
    m_bufferSummary->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(m_bufferSummary);

    layout->addWidget(separator(this));

    // ---- Sidetone (same setting as the Keyer page) -------------------------------
    auto *sidetoneHeader = new QLabel("Sidetone", this);
    sidetoneHeader->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sidetoneHeader);

    auto *volRow = new QHBoxLayout();
    auto *volLabel = new QLabel("Volume:", this);
    volLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    volLabel->setMinimumWidth(120);
    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeValueLabel = new QLabel(QString("%1%").arg(RadioSettings::instance()->sidetoneVolume()), this);
    m_sidetoneVolumeValueLabel->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::TextWhite, K4Styles::Dimensions::FontSizePopup));
    m_sidetoneVolumeValueLabel->setMinimumWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        RadioSettings::instance()->setSidetoneVolume(value);
    });
    volRow->addWidget(volLabel);
    volRow->addWidget(m_sidetoneVolumeSlider, 1);
    volRow->addWidget(m_sidetoneVolumeValueLabel);
    layout->addLayout(volRow);

    auto *sidetoneHelp = new QLabel("Shared with the Keyer page — one local sidetone for both.", this);
    sidetoneHelp->setWordWrap(true);
    sidetoneHelp->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(sidetoneHelp);

    layout->addStretch();
    updateBufferSummary();
}

void StraightKeyPage::updateBufferSummary() {
    auto *rs = RadioSettings::instance();
    const bool on = rs->straightKeyBufferEnabled();
    m_minWpmSpin->setEnabled(on);
    m_maxWpmSpin->setEnabled(on);
    m_ratioSpin->setEnabled(on);
    setLabelEnabled(m_minWpmLabel, on);
    setLabelEnabled(m_maxWpmLabel, on);
    setLabelEnabled(m_ratioLabel, on);

    // Keep the pair from crossing while the operator drags either spinner.
    m_minWpmSpin->setMaximum(m_maxWpmSpin->value());
    m_maxWpmSpin->setMinimum(m_minWpmSpin->value());

    QString text;
    if (!on) {
        text = "Off — character transmission starts as soon as you lift your key.";
    } else {
        // Mirrors CwController::straightKeyPreRollMs(); shown so the operator can see what
        // their speed bounds actually cost them.
        const double dit = 1200.0 / qBound(5, rs->straightKeyMinWpm(), 35);
        const int raw = static_cast<int>(dit * (1.0 + rs->straightKeyDahDitRatio()) * 1.3 + 0.5);
        const int applied = qBound(0, raw, 1000);
        text = QStringLiteral("Your CW will be sent to the rig %1 ms after you begin sending.").arg(applied);
        if (raw > applied)
            text += QStringLiteral(" Capped at the radio's 1000 ms limit — sending slower than about "
                                   "%1 WPM may still lose spacing.")
                        .arg(qRound(1200.0 * (1.0 + rs->straightKeyDahDitRatio()) * 1.3 / 1000.0));
    }

    // Mirrors CwController::straightKeyMinElementMs(). Worth saying only once the declared
    // speed is fast enough to move the floor off its default.
    const int floorMs = qMin(10, 600 / qBound(15, rs->straightKeyMaxWpm(), 80));
    if (floorMs < 10)
        text += QStringLiteral(" Closures under %1 ms are treated as contact bounce.").arg(floorMs);

    m_bufferSummary->setText(text);
}

void StraightKeyPage::setPageVisible(bool visible) {
    if (m_connection)
        m_connection->setPollingActive(visible);
}

void StraightKeyPage::refresh() {
    if (m_connection)
        m_connection->refresh();
    m_bufferCheck->setChecked(RadioSettings::instance()->straightKeyBufferEnabled());
    m_maxWpmSpin->setValue(RadioSettings::instance()->straightKeyMaxWpm());
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    updateBufferSummary();
}
