#include "ui/dialogs/textsenddialog.h"

#include "controllers/textsendcontroller.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "utils/macroids.h"

#include <QEvent>
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

// K4 CW decoder prosign punctuation (see legend label for what each means) — kept as the
// original character but colored/bolded to stand out; replacing it with the two-letter name
// inline read as clutter jammed against surrounding decoded text.
const QString kProsignChars = QStringLiteral("(+=%*!");

void trimToMaxLines(QTextEdit *edit, int maxLines) {
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
}
}

TextSendDialog::TextSendDialog(TextSendController *controller, RadioState *radioState, QWidget *parent)
    : QDialog(parent), m_controller(controller), m_radioState(radioState) {
    setWindowTitle("CW Send");
    setWindowModality(Qt::NonModal);
    resize(760, 460);

    setStyleSheet(QString("QDialog { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);

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
        auto *label = new QLabel(labelText, container);
        label->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(borderColor));
        vbox->addWidget(label);
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

    auto mainPane = makeRxPane("RX Main", K4Styles::Colors::VfoACyan);
    auto subPane = makeRxPane("RX Sub", K4Styles::Colors::VfoBGreen);
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

    m_display = new QTextEdit(this);
    m_display->setReadOnly(true);
    m_display->setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                     "font-size: 14px; padding: 6px; }")
                                 .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextGray,
                                      K4Styles::Colors::DialogBorder));
    layout->addWidget(m_display, 1);

    auto *inputRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Type here to send CW...");
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
    m_immediateModeCheck = new QCheckBox("Send immediately", this);
    m_immediateModeCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_immediateModeCheck->setChecked(RadioSettings::instance()->cwSendImmediateMode());
    connect(m_immediateModeCheck, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setCwSendImmediateMode(checked); });
    bottomRow->addWidget(m_immediateModeCheck);
    m_pauseSendCheck = new QCheckBox("Pause sending", this);
    m_pauseSendCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    // Uncheck = "away it goes": send whatever accumulated in m_input while paused, as one chunk.
    connect(m_pauseSendCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked)
            finishPendingWord();
    });
    bottomRow->addWidget(m_pauseSendCheck);
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
    });
    connect(m_controller, &TextSendController::aborted, this, &TextSendDialog::onAborted);

    // Without this, editing a macro in Options -> CW Macros while this dialog stays open
    // leaves the F1-F8 buttons showing stale labels/tooltips until the dialog is closed and
    // reopened (refreshMacros() was only ever called at construction and on reopen).
    connect(RadioSettings::instance(), &RadioSettings::cwMacrosChanged, this, &TextSendDialog::refreshMacros);

    connect(m_radioState, &RadioState::textBufferReceived, this,
            [this](const QString &text, bool isSubRx) { appendRxText(text, isSubRx); });
    connect(m_radioState, &RadioState::textDecodeChanged, this, &TextSendDialog::updateRxPaneVisibility);
    connect(m_radioState, &RadioState::textDecodeBChanged, this, &TextSendDialog::updateRxPaneVisibility);
    updateRxPaneVisibility(); // decode may already be running when this dialog is first created

    // m_callsignEdit sits above m_input in the layout, which would otherwise steal default
    // focus — the operator opening this dialog and typing immediately expects it to go to CW,
    // not the callsign box. The RX panes are QTextEdit::NoFocus so they never enter tab order.
    setTabOrder(m_callsignEdit, m_input);

    refreshMacros();
}

void TextSendDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // The dialog is created once and reused (hidden/shown) for the whole app session, so this
    // must run on every show, not just construction.
    m_input->setFocus();
    m_immediateModeCheck->setChecked(RadioSettings::instance()->cwSendImmediateMode());
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
        m_macroButtons[i]->setEnabled(!entry.command.isEmpty()); // raw command — unaffected by token expansion
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

void TextSendDialog::commitText(const QString &text) {
    if (m_stalled)
        return; // controller already ignores appendChar() while stalled — don't echo grey
                // text the K4 will never actually see
    if (m_pauseSendCheck->isChecked())
        return; // typed/macro text stays visible in m_input only, until "Pause sending" is unchecked
    for (const QChar &ch : text) {
        appendToDisplay(ch);
        m_controller->appendChar(ch);
    }
}

void TextSendDialog::appendToDisplay(QChar ch) {
    QTextCursor cursor(m_display->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(K4Styles::Colors::TextGray));
    cursor.insertText(QString(ch), fmt);
    m_displayLength++;
    m_display->moveCursor(QTextCursor::End); // keep the newest text in view as it fills up
    m_display->ensureCursorVisible();
}

void TextSendDialog::recolorRange(int start, int length, const QString &colorHex) {
    if (length <= 0 || start < 0 || start + length > m_displayLength)
        return;
    QTextCursor cursor(m_display->document());
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
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

void TextSendDialog::finishPendingWord() {
    if (m_pauseSendCheck->isChecked())
        return; // don't send or discard while paused — the composed reply stays in m_input
    const QString remaining = m_input->text();
    if (!remaining.isEmpty()) {
        commitText(remaining);
        m_input->clear();
    }
    m_controller->flush();
}

void TextSendDialog::onInputTextEdited(const QString &text) {
    // CW has no case of its own — force uppercase as typed so the sent-text display doesn't mix
    // case with macro text (which is stored/sent as typed by the operator, normally uppercase).
    const QString upper = text.toUpper();
    if (upper != text) {
        m_input->blockSignals(true);
        m_input->setText(upper);
        m_input->blockSignals(false);
    }

    if (m_pauseSendCheck->isChecked())
        return; // let the field accumulate normally; nothing is sent until unpaused

    if (m_controller->immediateMode()) {
        // Every keystroke commits right away — the field never actually accumulates.
        commitText(upper);
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
    commitText(toCommit);
    m_input->blockSignals(true);
    m_input->setText(remainder);
    m_input->blockSignals(false);
}

void TextSendDialog::onReturnPressed() {
    finishPendingWord();
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
    if (!typed.isEmpty() && m_displayLength > 0) {
        const QChar lastDisplayChar = m_display->toPlainText().back();
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
    else if (m_displayLength > 0)
        lastSentChar = m_display->toPlainText().back();

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
        if (kProsignChars.contains(ch)) {
            fmt.setForeground(QColor(K4Styles::Colors::AccentAmber));
            fmt.setFontWeight(QFont::Bold);
        } else {
            fmt.setForeground(QColor(K4Styles::Colors::TextGray));
        }
        cursor.insertText(QString(ch), fmt);
    }
    pane->moveCursor(QTextCursor::End);
    trimToMaxLines(pane, kRxPaneMaxLines);
}

void TextSendDialog::updateRxPaneVisibility() {
    const bool mainOn = m_radioState->textDecodeMode() != 0;
    const bool subOn = m_radioState->textDecodeModeB() != 0;
    m_rxMainText->parentWidget()->setVisible(mainOn);
    m_rxSubText->parentWidget()->setVisible(subOn);
    m_rxPaneContainer->setVisible(mainOn || subOn);
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
