#include "ui/pages/cwkeyerpage.h"

#include "controllers/connectioncontroller.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/keyerconnectionwidget.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
// KP's weight field is 090-125 in steps of 5, shown to the operator as a ratio 0.90-1.25.
constexpr int kWeightMin = 90;
constexpr int kWeightMax = 125;
constexpr int kWeightStep = 5;
// Shown until the radio reports KP. 1.10 rather than 1.00 — a little extra weight is the
// more common preference and reads better on the air.
constexpr int kWeightDefault = 110;

int weightToSlider(int weight) {
    return (qBound(kWeightMin, weight, kWeightMax) - kWeightMin) / kWeightStep;
}
int sliderToWeight(int pos) {
    return kWeightMin + pos * kWeightStep;
}

QFrame *separator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    return line;
}
} // namespace

CwKeyerPage::CwKeyerPage(HalikeyDevice *device, HalikeyDevice *straightKeyDevice, RadioState *radioState,
                         ConnectionController *connection, QWidget *parent)
    : QWidget(parent), m_radioState(radioState), m_connection(connection) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *title = new QLabel("Keyer", this);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(title);

    auto *desc = new QLabel("Connect a paddle interface to send CW through the K4's built-in keyer. "
                            "CW is sent over the air with near original cadence, regardless of network "
                            "jitter or traffic. For Haliday 3.5mm stereo plug, tip is Dah and ring is Dit.",
                            this);
    desc->setWordWrap(true);
    desc->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(desc);

    m_connectionWidget = new KeyerConnectionWidget(RadioSettings::KeyerRolePaddle, device, straightKeyDevice, this);
    layout->addWidget(m_connectionWidget);

    layout->addWidget(separator(this));

    // ---- Radio keyer (KP) --------------------------------------------------------
    auto *radioHeader = new QLabel("Radio Keyer (K4)", this);
    radioHeader->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(radioHeader);

    auto *modeRow = new QHBoxLayout();
    auto *modeLabel = new QLabel("Iambic mode:", this);
    modeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    modeLabel->setMinimumWidth(120);
    m_iambicARadio = new QRadioButton("A", this);
    m_iambicBRadio = new QRadioButton("B", this);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_iambicARadio);
    modeGroup->addButton(m_iambicBRadio);
    for (auto *rb : {m_iambicARadio, m_iambicBRadio}) {
        rb->setStyleSheet(K4Styles::Dialog::checkBox());
        // Only the newly-checked button acts; the one being cleared also emits toggled.
        connect(rb, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                sendKeyerPaddle();
        });
    }
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(m_iambicARadio);
    modeRow->addWidget(m_iambicBRadio);
    modeRow->addStretch();
    layout->addLayout(modeRow);

    auto *orientRow = new QHBoxLayout();
    auto *orientLabel = new QLabel("Paddles:", this);
    orientLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    orientLabel->setMinimumWidth(120);
    m_orientNormalRadio = new QRadioButton("Normal", this);
    m_orientReversedRadio = new QRadioButton("Reversed", this);
    auto *orientGroup = new QButtonGroup(this);
    orientGroup->addButton(m_orientNormalRadio);
    orientGroup->addButton(m_orientReversedRadio);
    for (auto *rb : {m_orientNormalRadio, m_orientReversedRadio}) {
        rb->setStyleSheet(K4Styles::Dialog::checkBox());
        connect(rb, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                sendKeyerPaddle();
        });
    }
    orientRow->addWidget(orientLabel);
    orientRow->addWidget(m_orientNormalRadio);
    orientRow->addWidget(m_orientReversedRadio);
    orientRow->addStretch();
    layout->addLayout(orientRow);

    auto *weightRow = new QHBoxLayout();
    auto *weightLabel = new QLabel("Weight:", this);
    weightLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    weightLabel->setMinimumWidth(120);
    m_weightSlider = new QSlider(Qt::Horizontal, this);
    m_weightSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_weightSlider->setRange(0, (kWeightMax - kWeightMin) / kWeightStep); // 8 detents
    m_weightSlider->setPageStep(1);
    m_weightSlider->setTickPosition(QSlider::TicksBelow);
    m_weightSlider->setTickInterval(1);
    m_weightValueLabel = new QLabel(QString::number(kWeightDefault / 100.0, 'f', 2), this);
    m_weightValueLabel->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::TextWhite, K4Styles::Dimensions::FontSizePopup));
    m_weightValueLabel->setMinimumWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    connect(m_weightSlider, &QSlider::valueChanged, this, [this](int pos) {
        m_weightValueLabel->setText(QString::number(sliderToWeight(pos) / 100.0, 'f', 2));
        sendKeyerPaddle();
    });
    weightRow->addWidget(weightLabel);
    weightRow->addWidget(m_weightSlider, 1);
    weightRow->addWidget(m_weightValueLabel);
    layout->addLayout(weightRow);

    auto *weightHelp = new QLabel("Dah-to-Dit ratio, 1.00 is standard. Lower is lighter, higher is heavier.", this);
    weightHelp->setWordWrap(true);
    weightHelp->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(weightHelp);

    layout->addWidget(separator(this));

    // ---- Sidetone (same setting as the Straight Key page) ------------------------
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

    auto *sidetoneHelp = new QLabel("Local sidetone for CW keying feedback, shared with the Straight Key "
                                    "page. Pitch follows the K4's CW pitch setting.",
                                    this);
    sidetoneHelp->setWordWrap(true);
    sidetoneHelp->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(sidetoneHelp);

    layout->addStretch();

    // The radio is the source of truth for KP — follow its echoes rather than assuming our
    // writes landed.
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar, QChar, int) { updateFromRadio(); });
    updateFromRadio();
}

void CwKeyerPage::updateFromRadio() {
    m_updatingFromRadio = true;
    // Iambic mode is null until the radio reports KP (it arrives in the RDY dump), so
    // fall back to B — the more common choice, and the K4's own default.
    const bool iambicB = m_radioState->iambicMode().isNull() || m_radioState->iambicMode() == 'B';
    m_iambicARadio->setChecked(!iambicB);
    m_iambicBRadio->setChecked(iambicB);
    const bool reversed = (m_radioState->paddleOrientation() == 'R');
    m_orientNormalRadio->setChecked(!reversed);
    m_orientReversedRadio->setChecked(reversed);
    const int weight = m_radioState->keyingWeight() > 0 ? m_radioState->keyingWeight() : kWeightDefault;
    m_weightSlider->setValue(weightToSlider(weight));
    m_weightValueLabel->setText(QString::number(sliderToWeight(m_weightSlider->value()) / 100.0, 'f', 2));
    m_updatingFromRadio = false;
}

void CwKeyerPage::sendKeyerPaddle() {
    if (m_updatingFromRadio)
        return; // echo from the radio, not an operator edit
    // KP sets iambic mode, orientation and weight in one command, so all three must go
    // together or the untouched two get clobbered — same reason buttonrowdispatcher does it.
    const QChar iambic = m_iambicBRadio->isChecked() ? 'B' : 'A';
    const QChar orient = m_orientReversedRadio->isChecked() ? 'R' : 'N';
    const int weight = sliderToWeight(m_weightSlider->value());
    m_connection->sendCAT(QString("KP%1%2%3;").arg(iambic).arg(orient).arg(weight, 3, 10, QChar('0')));
    // The K4 does not echo KP, so nothing else will update RadioState — and the local keyer
    // reads paddleOrientation() to decide dit/dah. Same optimistic set buttonrowdispatcher
    // does for its N/R toggle.
    m_radioState->setIambicMode(iambic);
    m_radioState->setPaddleOrientation(orient);
    m_radioState->setKeyingWeight(weight);
}

void CwKeyerPage::setPageVisible(bool visible) {
    if (m_connectionWidget)
        m_connectionWidget->setPollingActive(visible);
}

void CwKeyerPage::refresh() {
    if (m_connectionWidget)
        m_connectionWidget->refresh();
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    updateFromRadio();
}
