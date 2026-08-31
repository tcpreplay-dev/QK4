#include "ui/dialogs/textsenddialog.h"

#include "controllers/textsendcontroller.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/vforowwidget.h" // VfoSquareWidget — same A/B squares the main display uses
#include "utils/macroids.h"
#include "utils/radioutils.h"

#include <QEvent>
#include <QSignalBlocker>
#include <QFont>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

#include <utility>

namespace {
const QVector<QString> kSlotIds = {MacroIds::CwMacro1, MacroIds::CwMacro2, MacroIds::CwMacro3, MacroIds::CwMacro4,
                                   MacroIds::CwMacro5, MacroIds::CwMacro6, MacroIds::CwMacro7, MacroIds::CwMacro8};

const Qt::Key kSlotKeys[] = {Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4,
                             Qt::Key_F5, Qt::Key_F6, Qt::Key_F7, Qt::Key_F8};

const int kRxPaneMaxLines = 10;

// Hard character cap on each RX pane, independent of the line cap. Decoded RTTY/PSK arrives
// from TB as a character stream with no line breaks at all, so blockCount stays 1 and a
// line-based trim never fires — an overnight receive grew one QTextDocument block without
// bound and made the whole app sluggish. 16 KB is far more scrollback than the 80px pane can
// show and still bounded.
const int kRxPaneMaxChars = 16 * 1024;

// How long after QK4 sends an M1-M4 switch tap a detected transmission is still attributed to
// that button. Long enough to cover the radio starting the message, short enough that an
// unrelated later transmission isn't mislabelled.
const int kMemoryPressWindowMs = 5000;

// Mirrors VfoSquareWidget's own paintEvent geometry: 4px top pad + 10px lock-arc space above a
// 30px square. Used to line the TX label up with the squares' centre rather than their top.
const int kVfoSquareTopReserve = 14;
const int kVfoSquareBodySize = 30;

// K4 CW decoder prosign punctuation (see legend label for what each means) — kept as the
// original character but colored/bolded to stand out; replacing it with the two-letter name
// inline read as clutter jammed against surrounding decoded text.
const QString kProsignChars = QStringLiteral("(+=%*!");

// The K4 tags each decode area with a small filled RX / TX block in that receiver's color
// rather than a text heading — cyan for Main, green for Sub, amber for the transmitted line.
// Same idea here so the dialog reads as part of the radio.
QLabel *makeTag(const QString &text, const char *bgColor, QWidget *parent) {
    auto *tag = new QLabel(text, parent);
    tag->setAlignment(Qt::AlignCenter);
    tag->setStyleSheet(QString("QLabel { background-color: %1; color: %2; font-weight: bold; "
                               "font-size: %3px; padding: 1px 6px; border-radius: 2px; }")
                           .arg(bgColor, K4Styles::Colors::DarkBackground)
                           .arg(K4Styles::Dimensions::FontSizeMedium));
    return tag;
}

// SUB / DIV badge styling, matching SubDivIndicatorController::badgeStyle: green with black
// text when active, otherwise a flat disabled slab.
QString subDivBadgeStyle(bool active) {
    return QString("background-color: %1; color: %2; font-size: %3px; font-weight: bold; border-radius: 2px;")
        .arg(active ? K4Styles::Colors::StatusGreen : K4Styles::Colors::DisabledBackground,
             active ? "black" : K4Styles::Colors::LightGradientTop)
        .arg(K4Styles::Dimensions::FontSizeNormal);
}

// Frequency the way the main display renders it (FrequencyDisplayWidget::paintEvent): trailing
// digits below the tuning-rate position in TextGray, everything else in the widget's "normal"
// color — which SubDivIndicatorController::setVfoBDimmed swaps from TextWhite to InactiveGray
// for VFO B while Sub RX is off. Digits and dots only, so the rich text needs no escaping.
//
// The trailing run is fixed at two digits here rather than tracking the tuning rate: this is a
// readout, not a tuning control, so there is no rate cursor to indicate.
QString freqMarkup(quint64 freq, bool dimmed) {
    const QString text = RadioUtils::formatFrequency(freq);
    if (text.length() <= 2)
        return text;
    const char *headColor = dimmed ? K4Styles::Colors::InactiveGray : K4Styles::Colors::TextWhite;
    return QStringLiteral("<span style='color:%1'>%2</span><span style='color:%3'>%4</span>")
        .arg(headColor, text.left(text.length() - 2), K4Styles::Colors::TextGray, text.right(2));
}

void trimRxPane(QTextEdit *edit, int maxLines, int maxChars) {
    QTextDocument *doc = edit->document();
    int blockCount = doc->blockCount();
    if (blockCount > maxLines) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::Start);
        for (int i = 0; i < blockCount - maxLines; ++i) {
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        }
        cursor.removeSelectedText();
    }

    // characterCount() includes the document's trailing marker, hence the -1. Drops the oldest
    // characters, which is what a receive log wants — the newest text is what's being read.
    const int excess = doc->characterCount() - 1 - maxChars;
    if (excess > 0) {
        QTextCursor cursor(doc);
        cursor.setPosition(0);
        cursor.setPosition(excess, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}
}

TextSendDialog::TextSendDialog(TextSendController *controller, RadioState *radioState, QWidget *parent)
    : QDialog(parent), m_controller(controller), m_radioState(radioState) {
    setWindowTitle("CW Send"); // replaced by applySessionMode() once the mode is known
    setWindowModality(Qt::NonModal);
    resize(760, 460);

    setStyleSheet(QString("QDialog { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(this);
    // Tighter at the top than a standard dialog: the VFO strip is a status readout, not a form
    // field, and the decode panes are what the operator is actually reading.
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin / 2,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin / 2);
    layout->setSpacing(6);

    // VFO strip, mirroring the K4's own top-of-screen layout: the A and B squares with each
    // receiver's mode beneath, and the TX arrow between them pointing at whichever one
    // transmits. QK4 is a remote head for the real radio, so the operator should be reading
    // the same picture here as on the front panel. SPLIT ON/OFF is deliberately left out —
    // the arrow already says which side is transmitting.
    auto *vfoRow = new QHBoxLayout();

    auto makeFreqLabel = [this]() {
        auto *label = new QLabel(this);
        // Deliberately NOT the main display's FontSizeFrequency: this dialog gets resized small
        // during a QSO, and a readout that large stops the strip fitting. The colors still come
        // from the rich text in updateVfoStrip() rather than from here, so the last two digits
        // are dimmed the way the main display dims them.
        label->setStyleSheet(QString("font-size: %1px; font-weight: bold;")
                                 .arg(K4Styles::Dimensions::FontSizePopup));
        return label;
    };
    // Flanking the squares, the way the main display puts each VFO's frequency outboard of the
    // A / TX / B group.
    m_vfoAFreqLabel = makeFreqLabel();
    vfoRow->addWidget(m_vfoAFreqLabel, 0, Qt::AlignVCenter);
    vfoRow->addStretch();

    // Geometry copied from VfoRowWidget rather than re-guessed: VfoSquareWidget fixes its own
    // 30x44 (the extra height is lock-arc space), the mode label is VfoSquareSize wide and
    // centred under it, and the column spacing is 2. Overriding any of that makes the strip
    // read as a near-miss of the main display instead of the same thing.
    auto makeVfoColumn = [this](const QString &letter, const char *color, QLabel *&modeLabelOut,
                               VfoSquareWidget *&squareOut) {
        auto *container = new QWidget(this);
        container->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
        auto *column = new QVBoxLayout(container);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);

        squareOut = new VfoSquareWidget(letter, QColor(color), container);
        column->addWidget(squareOut, 0, Qt::AlignHCenter);

        modeLabelOut = new QLabel(container);
        modeLabelOut->setFixedWidth(K4Styles::Dimensions::VfoSquareSize);
        modeLabelOut->setAlignment(Qt::AlignCenter);
        modeLabelOut->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::TextWhite)
                                        .arg(K4Styles::Dimensions::FontSizeLarge));
        column->addWidget(modeLabelOut, 0, Qt::AlignHCenter);
        return container;
    };

    vfoRow->addWidget(makeVfoColumn(QStringLiteral("A"), K4Styles::Colors::VfoACyan, m_vfoAModeLabel, m_vfoASquare),
                      0, Qt::AlignTop);

    // VfoSquareWidget reserves 14px at the top of its 44 (4 pad + 10 lock arc) before the 30px
    // square starts, so a top-aligned TX label sits above the squares rather than across them.
    // Drop it by exactly that reserve and give it the square's own height, and its text lands
    // on the squares' centre line — where the front panel puts it.
    auto *txColumn = new QVBoxLayout();
    txColumn->setContentsMargins(0, 0, 0, 0);
    txColumn->setSpacing(0);
    txColumn->addSpacing(kVfoSquareTopReserve);

    m_txSideBtn = new QPushButton(this);
    m_txSideBtn->setFlat(true);
    m_txSideBtn->setCursor(Qt::PointingHandCursor);
    m_txSideBtn->setAutoDefault(false); // same Enter-in-lineedit gotcha as the macro buttons
    m_txSideBtn->setFixedSize(90, kVfoSquareBodySize);
    connect(m_txSideBtn, &QPushButton::clicked, this, [this]() {
        if (m_txSwitchable)
            emit txSideToggleRequested();
    });
    txColumn->addWidget(m_txSideBtn);

    // Clicking the arrow toggles split, so the resulting state has to be visible here rather
    // than only on the main display — especially with Sub RX off, where split is the whole
    // difference between transmitting on A and on B. Same text, color and size the main
    // display uses (VfoRowIndicatorController::onSplitChanged).
    m_splitLabel = new QLabel(this);
    m_splitLabel->setAlignment(Qt::AlignCenter);
    m_splitLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizeButton));
    txColumn->addWidget(m_splitLabel);
    txColumn->addStretch();
    vfoRow->addLayout(txColumn);

    vfoRow->addWidget(makeVfoColumn(QStringLiteral("B"), K4Styles::Colors::VfoBGreen, m_vfoBModeLabel, m_vfoBSquare),
                      0, Qt::AlignTop);
    // SUB / DIV stack to the right of B, 36x14 each with 4px spacing — same as VfoRowWidget.
    auto *subDivStack = new QVBoxLayout();
    subDivStack->setContentsMargins(0, 0, 0, 0);
    subDivStack->setSpacing(4);
    auto makeBadge = [this](const char *text) {
        auto *badge = new QLabel(text, this);
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(36, 14);
        badge->setStyleSheet(subDivBadgeStyle(false));
        return badge;
    };
    m_subBadge = makeBadge("SUB");
    m_divBadge = makeBadge("DIV");
    subDivStack->addWidget(m_subBadge);
    subDivStack->addWidget(m_divBadge);
    subDivStack->addStretch();
    vfoRow->addSpacing(6);
    vfoRow->addLayout(subDivStack);

    vfoRow->addStretch();
    m_vfoBFreqLabel = makeFreqLabel();
    vfoRow->addWidget(m_vfoBFreqLabel, 0, Qt::AlignVCenter);
    layout->addLayout(vfoRow);

    connect(m_radioState, &RadioState::transmitStateChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::frequencyChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::frequencyBChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::diversityChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::splitChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::modeChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::modeBChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::dataSubModeChanged, this, &TextSendDialog::updateVfoStrip);
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, &TextSendDialog::updateVfoStrip);
    // NOT populated here: updateVfoStrip() also fills the TX pane headers, and those panes are
    // built further down. The initial paint happens at the end of the constructor instead.

    auto *callsignRow = new QHBoxLayout();
    auto *callsignLabel = new QLabel("Working:", this); // the OTHER station's callsign, not the operator's own
    callsignLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    callsignRow->addWidget(callsignLabel);
    m_callsignEdit = new QLineEdit(this);
    m_callsignEdit->setPlaceholderText("Their call...");
    m_callsignEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_callsignEdit->setMaximumWidth(120);
    connect(m_callsignEdit, &QLineEdit::textChanged, this, &TextSendDialog::onCallsignTextChanged);
    callsignRow->addWidget(m_callsignEdit);
    callsignRow->addStretch();
    layout->addLayout(callsignRow);

    m_rxPaneContainer = new QWidget(this);
    auto *rxContainerLayout = new QVBoxLayout(m_rxPaneContainer);
    rxContainerLayout->setContentsMargins(0, 0, 0, 0);

    auto *rxPaneRow = new QHBoxLayout();
    auto makeRxPane = [this](const char *labelText, const char *borderColor) {
        auto *container = new QWidget(m_rxPaneContainer);
        auto *vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(0, 0, 0, 0);
        auto *headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(0, 0, 0, 2);
        headerRow->setSpacing(6);
        headerRow->addWidget(makeTag(QStringLiteral("RX"), borderColor, container));
        auto *label = new QLabel(labelText, container);
        label->setStyleSheet(QString("QLabel { color: %1; font-size: %2px; }")
                                 .arg(K4Styles::Colors::TextFaded)
                                 .arg(K4Styles::Dimensions::FontSizeMedium));
        headerRow->addWidget(label);
        headerRow->addStretch();
        vbox->addLayout(headerRow);
        auto *edit = new QTextEdit(container);
        edit->setReadOnly(true);
        edit->setFocusPolicy(Qt::NoFocus);
        // No mouse-drag selection here — arriving decoded text keeps moving the cursor to the
        // end, which would otherwise silently wipe out whatever the operator just selected.
        // Double-click-to-grab (handleRxDoubleClick) sets its own transient selection instead.
        edit->setTextInteractionFlags(Qt::NoTextInteraction);
        edit->setFixedHeight(80);
        edit->setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                    "font-size: 13px; padding: 4px; }")
                                .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextGray, borderColor));
        vbox->addWidget(edit);
        return std::make_pair(container, edit);
    };

    auto mainPane = makeRxPane("Main", K4Styles::Colors::VfoACyan);
    auto subPane = makeRxPane("Sub", K4Styles::Colors::VfoBGreen);
    m_rxMainText = mainPane.second;
    m_rxSubText = subPane.second;
    rxPaneRow->addWidget(mainPane.first);
    rxPaneRow->addWidget(subPane.first);
    rxContainerLayout->addLayout(rxPaneRow);

    m_prosignLegend = new QLabel("( KN   + AR   = BT   % AS   * SK   ! VE", m_rxPaneContainer);
    m_prosignLegend->setStyleSheet(
        QString("QLabel { color: %1; font-family: monospace; }").arg(K4Styles::Colors::TextFaded));
    rxContainerLayout->addWidget(m_prosignLegend);

    // QTextEdit delivers mouse events to its internal viewport widget, not to the QTextEdit
    // object itself — installing the filter on the widget directly means it never sees clicks.
    m_rxMainText->viewport()->installEventFilter(this);
    m_rxSubText->viewport()->installEventFilter(this);

    layout->addWidget(m_rxPaneContainer);
    m_rxPaneContainer->hide();

    m_stalledBanner = new QLabel(this);
    m_stalledBanner->setWordWrap(true);
    m_stalledBanner->setStyleSheet(QString("QLabel { background-color: %1; color: %2; "
                                           "padding: 6px; border-radius: 4px; font-weight: bold; }")
                                       .arg(K4Styles::Colors::ErrorBgDark, K4Styles::Colors::ErrorRed));
    m_stalledBanner->hide();
    layout->addWidget(m_stalledBanner);

    // One sent-text pane per transmitting VFO, mirroring the RX pair above and the radio's own
    // per-receiver TX line. Only the transmitting one is ever written to, so a line's pane is
    // the record of which frequency it actually went out on.
    m_txPaneContainer = new QWidget(this);
    auto *txContainerLayout = new QVBoxLayout(m_txPaneContainer);
    txContainerLayout->setContentsMargins(0, 0, 0, 0);
    auto *txPaneRow = new QHBoxLayout();

    auto makeTxPane = [this](const char *borderColor, QLabel *&headerOut) {
        auto *container = new QWidget(m_txPaneContainer);
        auto *vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(0, 0, 0, 0);
        auto *headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(0, 0, 0, 2);
        headerRow->setSpacing(6);
        headerRow->addWidget(makeTag(QStringLiteral("TX"), K4Styles::Colors::AccentAmber, container));
        headerOut = new QLabel(container);
        headerOut->setStyleSheet(QString("QLabel { color: %1; font-size: %2px; }")
                                     .arg(K4Styles::Colors::TextFaded)
                                     .arg(K4Styles::Dimensions::FontSizeMedium));
        headerRow->addWidget(headerOut);
        headerRow->addStretch();
        vbox->addLayout(headerRow);

        auto *edit = new QTextEdit(container);
        edit->setReadOnly(true);
        edit->setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                    "font-size: 14px; padding: 6px; }")
                                .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextGray, borderColor));
        vbox->addWidget(edit);
        return std::make_pair(container, edit);
    };

    auto txMain = makeTxPane(K4Styles::Colors::VfoACyan, m_txMainHeader);
    auto txSub = makeTxPane(K4Styles::Colors::VfoBGreen, m_txSubHeader);
    m_txMainText = txMain.second;
    m_txSubText = txSub.second;
    txPaneRow->addWidget(txMain.first);
    txPaneRow->addWidget(txSub.first);
    txContainerLayout->addLayout(txPaneRow);
    layout->addWidget(m_txPaneContainer, 1);

    auto *inputRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Type here to send CW..."); // relabeled by applySessionMode()
    m_input->setStyleSheet(K4Styles::Dialog::lineEdit());
    inputRow->addWidget(m_input, 1);
    auto *clearInputBtn = new QPushButton("Clear", this);
    clearInputBtn->setStyleSheet(K4Styles::menuBarButton());
    clearInputBtn->setAutoDefault(false); // same Enter-in-lineedit gotcha as the macro buttons
    connect(clearInputBtn, &QPushButton::clicked, this, [this]() { m_input->clear(); });
    inputRow->addWidget(clearInputBtn);
    layout->addLayout(inputRow);

    auto *macroRow1 = new QHBoxLayout();
    auto *macroRow2 = new QHBoxLayout();
    for (int i = 0; i < kSlotIds.size(); ++i) {
        auto *btn = new QPushButton(QStringLiteral("F%1").arg(i + 1), this);
        btn->setStyleSheet(K4Styles::menuBarButton());
        // Without this, Qt makes the first QPushButton in the dialog its implicit "default"
        // button — pressing Enter in m_input then fires both returnPressed() AND a click on
        // this button, sending its macro on every Enter regardless of which button it is.
        btn->setAutoDefault(false);
        connect(btn, &QPushButton::clicked, this, [this, i]() { onMacroClicked(i); });
        (i < 4 ? macroRow1 : macroRow2)->addWidget(btn);
        m_macroButtons.append(btn);

        auto *shortcut = new QShortcut(QKeySequence(kSlotKeys[i]), this);
        connect(shortcut, &QShortcut::activated, this, [this, i]() { onMacroClicked(i); });
    }
    layout->addLayout(macroRow1);
    layout->addLayout(macroRow2);

    auto *bottomRow = new QHBoxLayout();
    // One control, not two checkboxes. The old pair expressed three mutually exclusive
    // behaviors badly: with "hold until Enter" checked, "send immediately" silently did
    // nothing, and neither label said what "both off" meant (send on every word).
    auto *sendModeLabel = new QLabel("Send on:", this);
    sendModeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    bottomRow->addWidget(sendModeLabel);

    m_sendModeCombo = new QComboBox(this);
    m_sendModeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    refreshSendModeCombo();
    connect(m_sendModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const int mode = m_sendModeCombo->itemData(index).toInt();
        // Leaving "Enter key" is "away it goes": release whatever accumulated while holding,
        // matching what unchecking the old checkbox did.
        const bool wasHolding = holdUntilEnter();
        RadioSettings::instance()->setTextSendMode(mode);
        if (wasHolding && mode != RadioSettings::SendOnEnter)
            finishPendingWord();
        updateInputPlaceholder();
    });
    bottomRow->addWidget(m_sendModeCombo);
    bottomRow->addStretch();
    m_abortBtn = new QPushButton("Abort (Esc)", this);
    m_abortBtn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; font-weight: bold; "
                                      "border-radius: 4px; padding: 6px 16px; }")
                                  .arg(K4Styles::Colors::ErrorBgDark, K4Styles::Colors::ErrorRed));
    m_abortBtn->setAutoDefault(false); // same Enter-in-lineedit gotcha as the macro buttons
    connect(m_abortBtn, &QPushButton::clicked, this, [this]() { m_controller->abort(); });
    bottomRow->addWidget(m_abortBtn);
    layout->addLayout(bottomRow);

    connect(m_input, &QLineEdit::textEdited, this, &TextSendDialog::onInputTextEdited);
    connect(m_input, &QLineEdit::returnPressed, this, &TextSendDialog::onReturnPressed);

    connect(m_controller, &TextSendController::chunkInFlight, this,
            [this](int start, int length) { recolorRange(start, length, K4Styles::Colors::TextFaded); });
    connect(m_controller, &TextSendController::chunkConfirmed, this,
            [this](int start, int length) { recolorRange(start, length, K4Styles::Colors::AccentAmber); });
    connect(m_controller, &TextSendController::chunkStalled, this, [this](int start, int length) {
        recolorRange(start, length, K4Styles::Colors::ErrorRed);
        m_stalled = true;
        m_stalledBanner->setText("No confirmation from the K4 — sending stopped. Click Abort to reset.");
        m_stalledBanner->show();
        m_input->setEnabled(false);
        refreshMacros(); // greys the macro buttons out for the duration of the stall
    });
    connect(m_controller, &TextSendController::aborted, this, &TextSendDialog::onAborted);

    // Without this, editing a macro in Options -> TX Macros while this dialog stays open
    // leaves the F1-F8 buttons showing stale labels/tooltips until the dialog is closed and
    // reopened (refreshMacros() was only ever called at construction and on reopen).
    connect(RadioSettings::instance(), &RadioSettings::cwMacrosChanged, this, &TextSendDialog::refreshMacros);

    connect(m_radioState, &RadioState::textBufferReceived, this,
            [this](const QString &text, bool isSubRx) { appendRxText(text, isSubRx); });
    // Message-memory playbacks have no text we can read (the K4 renders its own TX row locally
    // and publishes nothing), but the TB TX-buffer level does tell us one went out — see
    // onTxBufferLevelChanged().
    connect(m_radioState, &RadioState::txBufferLevelChanged, this, &TextSendDialog::onTxBufferLevelChanged);
    connect(m_radioState, &RadioState::textDecodeChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::textDecodeBChanged, this, &TextSendDialog::updateRxPaneVisibility);
    // Pane visibility also depends on each receiver's own mode and on whether Sub RX is even
    // switched on — not just on the decoder's state.
    connect(m_radioState, &RadioState::modeChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::modeBChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::dataSubModeChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::splitChanged, this, &TextSendDialog::updateRxPaneVisibility);
    updateVfoStrip();
    updateRxPaneVisibility(); // decode may already be running when this dialog is first created

    // m_callsignEdit sits above m_input in the layout, which would otherwise steal default
    // focus — the operator opening this dialog and typing immediately expects it to go to CW,
    // not the callsign box. The RX panes are QTextEdit::NoFocus so they never enter tab order.
    setTabOrder(m_callsignEdit, m_input);

    refreshMacros();
    applySessionMode(); // the radio is already in CW or an FSK sub-mode by the time this opens
}

void TextSendDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // The dialog is created once and reused (hidden/shown) for the whole app session, so this
    // must run on every show, not just construction.
    m_input->setFocus();
    applySessionMode();
}

void TextSendDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        m_controller->abort(); // same as the Abort button — stop sending, don't close the dialog
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void TextSendDialog::refreshMacros() {
    auto *rs = RadioSettings::instance();
    for (int i = 0; i < m_macroButtons.size(); ++i) {
        const MacroEntry entry = rs->cwMacro(kSlotIds[i]);
        const QString label = entry.label.isEmpty() ? QStringLiteral("F%1").arg(i + 1) : entry.label;
        m_macroButtons[i]->setText(label);
        // Raw command — unaffected by token expansion. Also off while stalled: nothing can be
        // sent then, and a click would otherwise look accepted while going nowhere.
        m_macroButtons[i]->setEnabled(!entry.command.isEmpty() && !m_stalled);
    }
    refreshMacroTooltips();
}

void TextSendDialog::refreshMacroTooltips() {
    auto *rs = RadioSettings::instance();
    for (int i = 0; i < m_macroButtons.size(); ++i) {
        const MacroEntry entry = rs->cwMacro(kSlotIds[i]);
        m_macroButtons[i]->setToolTip(QStringLiteral("(F%1) %2").arg(i + 1).arg(expandTokens(entry.command)));
    }
}

void TextSendDialog::onCallsignTextChanged(const QString &text) {
    const QString upper = text.toUpper();
    if (upper != text) {
        m_callsignEdit->blockSignals(true);
        m_callsignEdit->setText(upper);
        m_callsignEdit->blockSignals(false);
    }
    refreshMacroTooltips();
}

QString TextSendDialog::expandTokens(const QString &macroText) const {
    QString result;
    result.reserve(macroText.size());
    for (const QChar &ch : macroText) {
        if (ch == QChar('~'))
            result += RadioSettings::instance()->callSign();
        else if (ch == QChar('*'))
            result += m_callsignEdit->text();
        else
            result += ch;
    }
    return result;
}

bool TextSendDialog::commitText(const QString &text, bool force) {
    if (m_stalled)
        return false; // controller already ignores appendChar() while stalled — don't echo grey
                      // text the K4 will never actually see
    if (!force && holdUntilEnter())
        return false; // typed/macro text stays visible in m_input only, until the send mode
                      // leaves "Enter key" or Enter releases it
    for (const QChar &ch : text) {
        appendToDisplay(ch);
        m_controller->appendChar(ch);
    }
    return true;
}

bool TextSendDialog::activeTxIsSub() const {
    // Split is what moves the TX VFO on the K4 — same rule the VFO strip and TxStateController
    // use to decide which side is transmitting.
    return m_radioState->splitEnabled();
}

QTextEdit *TextSendDialog::activeTxPane() const {
    return activeTxIsSub() ? m_txSubText : m_txMainText;
}

int &TextSendDialog::activeTxLen() {
    return activeTxIsSub() ? m_txSubLen : m_txMainLen;
}

void TextSendDialog::openTxSegmentIfNeeded(bool force) {
    const bool isSub = activeTxIsSub();
    if (!force && !m_txSegments.isEmpty() && m_txSegments.last().isSub == isSub)
        return;
    m_txSegments.append({m_typedCount, isSub, isSub ? m_txSubLen : m_txMainLen});
}

void TextSendDialog::appendToDisplay(QChar ch) {
    openTxSegmentIfNeeded(false);
    QTextEdit *pane = activeTxPane();
    QTextCursor cursor(pane->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(K4Styles::Colors::TextGray));
    cursor.insertText(QString(ch), fmt);
    activeTxLen()++;
    m_typedCount++;
    pane->moveCursor(QTextCursor::End); // keep the newest text in view as it fills up
    pane->ensureCursorVisible();
}

void TextSendDialog::recolorRange(int start, int length, const QString &colorHex) {
    if (length <= 0 || start < 0 || m_txSegments.isEmpty())
        return;

    // Find the segment this range was written into — the last one that began at or before it.
    // A range never straddles two segments: a segment only closes when the transmitting VFO
    // changes or a divider is inserted, and neither happens part-way through committing a
    // chunk. A chunk queued before the TX side moved still recolors in the pane it went to,
    // which is the correct record of where it was actually sent.
    int index = -1;
    for (int i = m_txSegments.size() - 1; i >= 0; --i) {
        if (m_txSegments.at(i).controllerStart <= start) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const TxSegment &segment = m_txSegments.at(index);
    QTextEdit *pane = segment.isSub ? m_txSubText : m_txMainText;
    const int paneLen = segment.isSub ? m_txSubLen : m_txMainLen;
    const int docStart = segment.docStart + (start - segment.controllerStart);
    if (docStart < 0 || docStart + length > paneLen)
        return;

    QTextCursor cursor(pane->document());
    cursor.setPosition(docStart);
    cursor.setPosition(docStart + length, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(colorHex));
    cursor.mergeCharFormat(fmt);
}

void TextSendDialog::onAborted() {
    // Abort stops sending — it doesn't erase what's already been typed or shown. Whatever was
    // queued/in-flight at the moment of abort simply stays frozen in whatever color it last
    // had (grey/faded); nothing further will recolor it since the controller's pipeline is
    // now empty. m_input's own not-yet-committed text is left untouched too.
    m_stalled = false;
    m_stalledBanner->hide();
    m_input->setEnabled(true);
    refreshMacros();
}

void TextSendDialog::finishPendingWord(bool force) {
    if (!force && holdUntilEnter())
        return; // don't send or discard while paused — the composed reply stays in m_input
    QString remaining = m_input->text();
    if (!remaining.isEmpty()) {
        // Cleared only if the text was actually accepted — see below.
        // Enter ends a transmission the way a space ends a word. Without this the next thing
        // sent runs straight into this one — "...PRESS ENTER NOW" followed by "AND NOW I TYPE"
        // went out as "...PRESS ENTER NOWAND NOW I TYPE". Skipped when the text already ends
        // in a space, which is how the macro path leaves it.
        if (!remaining.endsWith(QChar(' ')))
            remaining += QChar(' ');
        // Clearing unconditionally destroyed the text when commitText refused it — a macro
        // clicked during a stall vanished with nothing sent and no feedback.
        if (commitText(remaining, force))
            m_input->clear();
    }
    m_controller->flush();
}

void TextSendDialog::onInputTextEdited(const QString &text) {
    // CW has no case of its own, and Baudot (AFSK-A / FSK-D) has no lower case at all — force
    // uppercase as typed so the sent-text display doesn't mix case with macro text (which is
    // stored/sent as typed by the operator, normally uppercase). PSK-D is the exception:
    // varicode carries mixed case and operators use it, so m_forceUppercase is false there.
    const QString upper = m_forceUppercase ? text.toUpper() : text;
    if (upper != text) {
        m_input->blockSignals(true);
        m_input->setText(upper);
        m_input->blockSignals(false);
    }

    if (holdUntilEnter())
        return; // let the field accumulate normally; nothing is sent until unpaused

    if (m_controller->immediateMode()) {
        // Every keystroke commits right away — the field never actually accumulates.
        if (!commitText(upper))
            return; // refused: leave it in the field rather than dropping it
        m_input->blockSignals(true);
        m_input->clear();
        m_input->blockSignals(false);
        return;
    }

    // Word-complete: commit any complete (space-terminated) prefix, leave the rest editable
    // (including normal backspacing of a not-yet-committed word).
    const int lastSpace = upper.lastIndexOf(QChar(' '));
    if (lastSpace < 0)
        return;

    const QString toCommit = upper.left(lastSpace + 1);
    const QString remainder = upper.mid(lastSpace + 1);
    if (!commitText(toCommit))
        return; // refused: keep the whole line in the field, don't drop the committed prefix
    m_input->blockSignals(true);
    m_input->setText(remainder);
    m_input->blockSignals(false);
}

void TextSendDialog::onReturnPressed() {
    // Enter sends even while paused, and leaves the checkbox alone. "Pause sending" is about
    // not dribbling text out word by word as it's typed — composing a full reply and then
    // releasing it with Enter is exactly what it's for, so refusing to send would make the
    // mode useless rather than safe. The input clears either way.
    finishPendingWord(/*force=*/true);
}

void TextSendDialog::onMacroClicked(int slotIndex) {
    const MacroEntry entry = RadioSettings::instance()->cwMacro(kSlotIds[slotIndex]);
    if (entry.command.isEmpty())
        return; // unassigned slot — leave whatever's typed (but not yet sent) untouched

    const QString expanded = expandTokens(entry.command);
    if (expanded.isEmpty()) // e.g. a macro that's just "*" with an empty Callsign field
        return;

    // Merge onto whatever's already typed but not yet sent (e.g. the remote station's
    // callsign, typed with "Send immediately" off so it doesn't go out on its own) so a macro
    // key appends canned text and sends the whole line as one chunk — "K1ABC" + F1 mapped to
    // "DE VE7VT" keys "K1ABC DE VE7VT" together, not as two separate transmissions.
    const QString typed = m_input->text();
    QString combined;

    // Leading edge: if there's a typed prefix starting fresh right after an unrelated prior
    // send (e.g. a macro that already went out and cleared m_input) with nothing to naturally
    // separate them, that prefix needs its own leading space before it touches the old text.
    if (!typed.isEmpty() && activeTxLen() > 0) {
        const QChar lastDisplayChar = activeTxPane()->toPlainText().back();
        if (!lastDisplayChar.isSpace() && !typed.front().isSpace())
            combined += QChar(' ');
    }
    combined += typed;

    // Trailing edge: separate the typed prefix (or, if there wasn't one, whatever was last
    // sent) from this macro's own text. Has to look past an empty typed prefix too —
    // back-to-back macro clicks each start from an empty input box since the prior click's
    // finishPendingWord() already cleared it — so with no fallback here, "DE" then "VE7VT"
    // clicked in a row would run together as "DEVE7VT" in what's actually sent.
    QChar lastSentChar;
    if (!combined.isEmpty())
        lastSentChar = combined.back();
    else if (activeTxLen() > 0)
        lastSentChar = activeTxPane()->toPlainText().back();

    if (!lastSentChar.isNull() && !lastSentChar.isSpace())
        combined += QChar(' ');
    combined += expanded;

    m_input->blockSignals(true);
    m_input->setText(combined);
    m_input->blockSignals(false);

    finishPendingWord(); // commits + sends the merged text, then clears m_input
}

void TextSendDialog::appendRxText(const QString &text, bool isSubRx) {
    QTextEdit *pane = isSubRx ? m_rxSubText : m_rxMainText;

    QTextCursor cursor(pane->document());
    cursor.movePosition(QTextCursor::End);
    for (const QChar &ch : text) {
        QTextCharFormat fmt;
        // Prosigns are a CW convention — in RTTY and PSK these are ordinary punctuation and
        // highlighting them would be actively misleading.
        if (m_prosignsEnabled && kProsignChars.contains(ch)) {
            fmt.setForeground(QColor(K4Styles::Colors::AccentAmber));
            fmt.setFontWeight(QFont::Bold);
        } else {
            fmt.setForeground(QColor(K4Styles::Colors::TextGray));
        }
        cursor.insertText(QString(ch), fmt);
    }
    pane->moveCursor(QTextCursor::End);
    trimRxPane(pane, kRxPaneMaxLines, kRxPaneMaxChars);
}

void TextSendDialog::applySessionMode() {
    const TextSendController::SessionMode session = m_controller->sessionMode();

    QString label;
    if (session == TextSendController::SessionMode::Cw)
        label = QStringLiteral("CW");
    else if (session == TextSendController::SessionMode::Fsk)
        label = RadioState::dataSubModeToString(m_radioState->activeDataSubMode()); // AFSK / FSK / PSK

    // Ahead of the early return: the offered choices depend on the session, and this has to
    // re-run on every show even when the session itself hasn't changed.
    refreshSendModeCombo();

    // No text mode active (the operator went to SSB, or nothing is known yet): leave the dialog
    // showing whatever the last session was rather than blanking it out. The controller already
    // refuses to send in this state.
    if (label.isEmpty() || label == m_sessionLabel)
        return;

    const bool firstSession = m_sessionLabel.isEmpty();
    m_sessionLabel = label;
    setWindowTitle(label + QStringLiteral(" Send"));
    updateInputPlaceholder();

    m_prosignsEnabled = (session == TextSendController::SessionMode::Cw);
    m_prosignLegend->setVisible(m_prosignsEnabled);

    // Baudot carries no lower case at all, so AFSK-A and FSK-D are upper-only like CW. PSK-D's
    // varicode does, and operators use it.
    m_forceUppercase = !(session == TextSendController::SessionMode::Fsk &&
                         m_radioState->activeDataSubMode() == 3);

    if (!firstSession)
        appendSessionDivider(label);

    updateRxPaneVisibility(); // which panes belong to this session changes with it
}

bool TextSendDialog::holdUntilEnter() const {
    return RadioSettings::instance()->textSendMode() == RadioSettings::SendOnEnter;
}

void TextSendDialog::refreshSendModeCombo() {
    const int stored = RadioSettings::instance()->textSendMode();

    // "Each character" cuts a chunk per keystroke. That suits CW's element-at-a-time keying but
    // not FSK: at RTTY45 every character costs a full KY0 confirm round trip, and typing faster
    // than that cycle loses text. It is left out of the list entirely in an FSK session rather
    // than offered and misbehaving; the stored setting is untouched, so CW gets it back.
    const bool isFsk = (m_controller->sessionMode() == TextSendController::SessionMode::Fsk);

    QSignalBlocker blocker(m_sendModeCombo); // rebuilding must not look like an operator choice
    m_sendModeCombo->clear();
    if (!isFsk)
        m_sendModeCombo->addItem("Each character", RadioSettings::SendEachCharacter);
    m_sendModeCombo->addItem("Each word", RadioSettings::SendEachWord);
    m_sendModeCombo->addItem("Enter key", RadioSettings::SendOnEnter);

    const int index = m_sendModeCombo->findData(stored);
    m_sendModeCombo->setCurrentIndex(index >= 0 ? index : m_sendModeCombo->findData(RadioSettings::SendEachWord));
    m_sendModeCombo->setToolTip("When typed text is handed to the radio");
    updateInputPlaceholder();
}

void TextSendDialog::updateInputPlaceholder() {
    if (m_sessionLabel.isEmpty())
        return;
    // While paused nothing leaves on a space, so "Type" would be describing the wrong gesture:
    // Enter is what actually sends.
    const QString verb = holdUntilEnter() ? QStringLiteral("Enter") : QStringLiteral("Type");
    m_input->setPlaceholderText(QStringLiteral("%1 here to send %2...").arg(verb, m_sessionLabel));
}

void TextSendDialog::updateVfoStrip() {
    const QString modeA = m_radioState->modeStringFull();
    const QString modeB = m_radioState->modeStringFullB();
    m_vfoAModeLabel->setText(modeA);
    m_vfoBModeLabel->setText(modeB);
    // VFO B goes dim with Sub RX off, frequency and mode label together — same as the main
    // display, which dims both in setVfoBDimmed(). VFO A is never dimmed.
    const bool dimB = !m_radioState->subRxEnabled();
    m_subBadge->setStyleSheet(subDivBadgeStyle(m_radioState->subRxEnabled()));
    m_divBadge->setStyleSheet(subDivBadgeStyle(m_radioState->diversityEnabled()));
    m_vfoAFreqLabel->setText(freqMarkup(m_radioState->vfoA(), false));
    m_vfoBFreqLabel->setText(freqMarkup(m_radioState->vfoB(), dimB));
    m_vfoBModeLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                       .arg(dimB ? K4Styles::Colors::InactiveGray : K4Styles::Colors::TextWhite)
                                       .arg(K4Styles::Dimensions::FontSizeLarge));
    updateTxHeaders(); // the TX arrow and the sent-pane headers name the same VFO

    // Split is what moves the TX VFO on the K4: off = transmit on A, on = transmit on B. The
    // arrow points at whichever side is live, same as the front panel's.
    // Both forms are the same length so "TX" stays put and only the arrow moves side to side,
    // the way the front panel's two triangles flank a stationary TX label.
    const bool txOnB = m_radioState->splitEnabled();
    m_txSideBtn->setText(txOnB ? QStringLiteral("TX ▶") : QStringLiteral("◀ TX"));
    m_splitLabel->setText(txOnB ? QStringLiteral("SPLIT ON") : QStringLiteral("SPLIT OFF"));

    // While the radio is actually keyed, the transmitting VFO's square goes red — the same
    // TxRed the main display already flips its TX indicator to (TxStateController). The other
    // square keeps its identity color, so at a glance you can still tell A from B, and there
    // is never any doubt about which side is live.
    const bool transmitting = m_radioState->isTransmitting();
    m_vfoASquare->setColor(QColor(transmitting && !txOnB ? K4Styles::Colors::TxRed : K4Styles::Colors::VfoACyan));
    m_vfoBSquare->setColor(QColor(transmitting && txOnB ? K4Styles::Colors::TxRed : K4Styles::Colors::VfoBGreen));

    // Only offer the switch when both VFOs are in the same mode — including the DATA sub-mode,
    // which is why this compares the full mode strings rather than mode() alone. Flipping TX
    // onto a VFO in a different mode would silently change what the radio transmits.
    // Only the modes have to match. Sub RX is deliberately NOT required: transmitting on B while
    // receiving on A with Sub RX off is ordinary split operation, not an edge case, and the
    // operator wants to reach it from here rather than the main window's SPLIT button.
    m_txSwitchable = (modeA == modeB) && !modeA.isEmpty();
    // Deliberately NOT setEnabled(false) when it can't be switched: Qt's disabled palette
    // greyed the label out, which fought the one job this indicator has — reading the same as
    // the main display's TX indicator, amber on receive and TxRed while keyed. The button stays
    // enabled and simply ignores the click (which also keeps the explanatory tooltip working,
    // since a disabled widget gets no mouse events at all). The cursor is the affordance.
    m_txSideBtn->setCursor(m_txSwitchable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_txSideBtn->setToolTip(m_txSwitchable
                                ? QStringLiteral("Switch which VFO transmits")
                                : QStringLiteral("Both VFOs must be in the same mode to switch TX sides"));
    // Same color rule and FontSizeIndicator as TxStateController uses for the main display's
    // TX label and triangles.
    m_txSideBtn->setStyleSheet(
        QString("QPushButton { color: %1; background: transparent; border: none; font-weight: bold; "
                "font-size: %2px; }")
            .arg(transmitting ? K4Styles::Colors::TxRed : K4Styles::Colors::AccentAmber)
            .arg(K4Styles::Dimensions::FontSizeIndicator));
}

void TextSendDialog::appendSessionDivider(const QString &label) {
    // The history is deliberately NOT cleared on a mode switch: TextSendController's own
    // character offsets are monotonic and survive its reset (see resetAll()), so wiping a pane
    // would silently desync every later recolor. A divider separates instead — and only when
    // there is something above it to separate.
    appendTxNote(label, /*onlyIfPaneHasText=*/true);
}

void TextSendDialog::appendTxNote(const QString &text, bool onlyIfPaneHasText) {
    QTextEdit *pane = activeTxPane();
    if (onlyIfPaneHasText && activeTxLen() == 0)
        return;

    const QString note = QStringLiteral("\n--- %1 ---\n").arg(text);
    QTextCursor cursor(pane->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(K4Styles::Colors::TextFaded));
    cursor.insertText(note, fmt);
    activeTxLen() += note.length();

    // These characters are the dialog's own — the controller never counted them — so the
    // current segment has to end here and a fresh one start after them.
    openTxSegmentIfNeeded(true);

    pane->moveCursor(QTextCursor::End);
    pane->ensureCursorVisible();
}

void TextSendDialog::noteMessageMemoryPressed(int index) {
    m_lastMemoryIndex = index;
    m_memoryPressAge.start();
}

void TextSendDialog::onTxBufferLevelChanged(int level) {
    const int previous = m_txBufferLevel;
    m_txBufferLevel = level;

    // Only the idle -> busy edge marks the start of a transmission.
    if (level <= 0 || previous > 0)
        return;
    // Our own sends raise the same level; the controller stays active for all of one, bracket
    // and hang included, so this is what separates its traffic from the radio's own.
    if (m_controller->isActive())
        return;

    // The memory buttons are bare switch taps with no CAT echo, so the only thing tying a
    // playback to a button is that QK4 sent the tap itself moments earlier. A press on the
    // radio's front panel, or a stale one, gets the generic wording.
    const bool recent = m_lastMemoryIndex > 0 && m_memoryPressAge.isValid() &&
                        m_memoryPressAge.elapsed() < kMemoryPressWindowMs;
    appendTxNote(recent ? QStringLiteral("M%1 message sent").arg(m_lastMemoryIndex)
                        : QStringLiteral("radio message sent"),
                 /*onlyIfPaneHasText=*/false);
    m_lastMemoryIndex = 0;
}

void TextSendDialog::updateRxPaneVisibility() {
    // A pane belongs here only if that receiver is actually decoding THIS session's kind of
    // text. A decoder left running on a receiver sitting in some other mode — or on a Sub RX
    // that is switched off entirely — has nothing to do with the conversation being typed
    // into this dialog, and showing its stale pane just reads as a bug.
    const TextSendController::SessionMode session = m_controller->sessionMode();
    auto inThisSession = [session](RadioState::Mode mode, int dataSubMode) {
        switch (session) {
        case TextSendController::SessionMode::Cw:
            return mode == RadioState::CW || mode == RadioState::CW_R;
        case TextSendController::SessionMode::Fsk:
            return RadioState::isFskTextMode(mode, dataSubMode);
        case TextSendController::SessionMode::None:
            break;
        }
        return false;
    };

    // != 0, not > 0, on the decoder: 0 is a decoder the radio explicitly told us is off. -1
    // means it has never told us anything — and since entering FSK turns the decoder on (see
    // TextDecodeController::applyFskAutoDecode) and re-asks, "unknown" means "asked for,
    // answer pending", not "off".
    const bool mainOn = m_radioState->textDecodeMode() != 0 &&
                        inThisSession(m_radioState->mode(), m_radioState->dataSubMode());
    const bool subOn = m_radioState->subRxEnabled() && m_radioState->textDecodeModeB() != 0 &&
                       inThisSession(m_radioState->modeB(), m_radioState->dataSubModeB());

    m_rxMainText->parentWidget()->setVisible(mainOn);
    m_rxSubText->parentWidget()->setVisible(subOn);
    m_rxPaneContainer->setVisible(mainOn || subOn);

    // The sent-text panes follow the same rule, minus the decoder: a receiver that isn't in
    // this session's mode (or a Sub RX that's switched off) can't be transmitted on either.
    // Main is always shown — there is always somewhere to look for what was sent, and VFO A is
    // the default TX side. Sub appears once it is genuinely a place text can go: split puts TX
    // on B whether or not Sub RX is on, so this must not key off Sub RX alone or text sent on B
    // would land in a hidden pane. It also stays up while it still holds history.
    // activeTxIsSub() first and unconditionally: this is the pane appendToDisplay() writes to,
    // so hiding it would silently swallow everything sent. Split can be switched on from the
    // main window with VFO B in any mode at all, which the mode test below would reject.
    const bool txSubOn = activeTxIsSub() || m_txSubLen > 0 ||
                         (m_radioState->subRxEnabled() &&
                          inThisSession(m_radioState->modeB(), m_radioState->dataSubModeB()));
    m_txSubText->parentWidget()->setVisible(txSubOn);
    updateTxHeaders();
}

void TextSendDialog::updateTxHeaders() {
    // Reached from updateVfoStrip(), which is wired to RadioState signals during construction —
    // so it can fire before the TX panes further down the constructor exist.
    if (!m_txMainHeader || !m_txSubHeader)
        return;

    // Frequency, so a line in the log is tied to where it actually went out. The arrow marks
    // whichever pane is live now.
    const bool subIsTx = activeTxIsSub();
    m_txMainHeader->setText(QStringLiteral("%1Main  %2")
                                .arg(subIsTx ? QString() : QStringLiteral("\u25C0 "),
                                     RadioUtils::formatFrequency(m_radioState->vfoA())));
    m_txSubHeader->setText(QStringLiteral("%1Sub  %2")
                               .arg(subIsTx ? QStringLiteral("\u25C0 ") : QString(),
                                    RadioUtils::formatFrequency(m_radioState->vfoB())));
}

bool TextSendDialog::looksLikeCallsign(const QString &word) {
    // Permissive convenience filter, not a strict validator — just enough to skip obvious
    // decode noise (stray letters, prosign fragments) when double-clicking RX text.
    static const QRegularExpression callsignPattern(QStringLiteral("^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,4}[A-Z]$"));
    QString stripped = word.toUpper();
    const int slash = stripped.indexOf(QChar('/'));
    if (slash >= 0)
        stripped = stripped.left(slash);
    return callsignPattern.match(stripped).hasMatch();
}

void TextSendDialog::handleRxDoubleClick(QTextEdit *pane, const QPoint &pos) {
    QTextCursor cursor = pane->cursorForPosition(pos);
    cursor.select(QTextCursor::WordUnderCursor);
    const QString word = cursor.selectedText();

    // Qt::NoTextInteraction (set on this pane so incoming decoded text can't wipe a native
    // drag-selection) also disables rendering of a plain textCursor selection, so give visible
    // feedback via an actual format change on the word instead of relying on cursor selection.
    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(K4Styles::Colors::AccentAmber));
    highlightFmt.setForeground(QColor(K4Styles::Colors::DarkBackground));
    cursor.mergeCharFormat(highlightFmt);

    if (looksLikeCallsign(word))
        m_callsignEdit->setText(word.toUpper());
}

bool TextSendDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        QTextEdit *pane = nullptr;
        if (watched == m_rxMainText->viewport())
            pane = m_rxMainText;
        else if (watched == m_rxSubText->viewport())
            pane = m_rxSubText;
        if (pane) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            handleRxDoubleClick(pane, mouseEvent->pos());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
