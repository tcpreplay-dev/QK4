#include "ui/dialogs/cwsenddialog.h"

#include "controllers/cwsendcontroller.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "utils/macroids.h"

#include <QHBoxLayout>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
const QVector<QString> kSlotIds = {MacroIds::CwMacro1, MacroIds::CwMacro2, MacroIds::CwMacro3, MacroIds::CwMacro4,
                                   MacroIds::CwMacro5, MacroIds::CwMacro6, MacroIds::CwMacro7, MacroIds::CwMacro8};

const Qt::Key kSlotKeys[] = {Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4,
                             Qt::Key_F5, Qt::Key_F6, Qt::Key_F7, Qt::Key_F8};
}

CwSendDialog::CwSendDialog(CwSendController *controller, QWidget *parent)
    : QDialog(parent), m_controller(controller) {
    setWindowTitle("CW Send");
    setWindowModality(Qt::NonModal);
    resize(560, 400);

    setStyleSheet(QString("QDialog { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);

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

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Type here to send CW...");
    m_input->setStyleSheet(K4Styles::Dialog::lineEdit());
    layout->addWidget(m_input);

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
    m_abortBtn = new QPushButton("Abort", this);
    m_abortBtn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; font-weight: bold; "
                                      "border-radius: 4px; padding: 6px 16px; }")
                                  .arg(K4Styles::Colors::ErrorBgDark, K4Styles::Colors::ErrorRed));
    m_abortBtn->setAutoDefault(false); // same Enter-in-lineedit gotcha as the macro buttons
    connect(m_abortBtn, &QPushButton::clicked, this, [this]() { m_controller->abort(); });
    bottomRow->addStretch();
    bottomRow->addWidget(m_abortBtn);
    layout->addLayout(bottomRow);

    connect(m_input, &QLineEdit::textEdited, this, &CwSendDialog::onInputTextEdited);
    connect(m_input, &QLineEdit::returnPressed, this, &CwSendDialog::onReturnPressed);

    connect(m_controller, &CwSendController::chunkInFlight, this,
            [this](int start, int length) { recolorRange(start, length, K4Styles::Colors::TextFaded); });
    connect(m_controller, &CwSendController::chunkConfirmed, this,
            [this](int start, int length) { recolorRange(start, length, K4Styles::Colors::AccentAmber); });
    connect(m_controller, &CwSendController::chunkStalled, this, [this](int start, int length) {
        recolorRange(start, length, K4Styles::Colors::ErrorRed);
        m_stalled = true;
        m_stalledBanner->setText("No confirmation from the K4 — sending stopped. Click Abort to reset.");
        m_stalledBanner->show();
        m_input->setEnabled(false);
    });
    connect(m_controller, &CwSendController::aborted, this, &CwSendDialog::onAborted);

    refreshMacros();
}

void CwSendDialog::refreshMacros() {
    auto *rs = RadioSettings::instance();
    for (int i = 0; i < m_macroButtons.size(); ++i) {
        const MacroEntry entry = rs->cwMacro(kSlotIds[i]);
        const QString label = entry.label.isEmpty() ? QStringLiteral("F%1").arg(i + 1) : entry.label;
        m_macroButtons[i]->setText(label);
        m_macroButtons[i]->setToolTip(entry.command);
        m_macroButtons[i]->setEnabled(!entry.command.isEmpty());
    }
}

void CwSendDialog::commitText(const QString &text) {
    if (m_stalled)
        return; // controller already ignores appendChar() while stalled — don't echo grey
                // text the K4 will never actually see
    for (const QChar &ch : text) {
        appendToDisplay(ch);
        m_controller->appendChar(ch);
    }
}

void CwSendDialog::appendToDisplay(QChar ch) {
    QTextCursor cursor(m_display->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(K4Styles::Colors::TextGray));
    cursor.insertText(QString(ch), fmt);
    m_displayLength++;
}

void CwSendDialog::recolorRange(int start, int length, const QString &colorHex) {
    if (length <= 0 || start < 0 || start + length > m_displayLength)
        return;
    QTextCursor cursor(m_display->document());
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(colorHex));
    cursor.mergeCharFormat(fmt);
}

void CwSendDialog::onAborted() {
    // Abort stops sending — it doesn't erase what's already been typed or shown. Whatever was
    // queued/in-flight at the moment of abort simply stays frozen in whatever color it last
    // had (grey/faded); nothing further will recolor it since the controller's pipeline is
    // now empty. m_input's own not-yet-committed text is left untouched too.
    m_stalled = false;
    m_stalledBanner->hide();
    m_input->setEnabled(true);
    refreshMacros();
}

void CwSendDialog::finishPendingWord() {
    const QString remaining = m_input->text();
    if (!remaining.isEmpty()) {
        commitText(remaining);
        m_input->clear();
    }
    m_controller->flush();
}

void CwSendDialog::onInputTextEdited(const QString &text) {
    if (m_controller->immediateMode()) {
        // Every keystroke commits right away — the field never actually accumulates.
        commitText(text);
        m_input->blockSignals(true);
        m_input->clear();
        m_input->blockSignals(false);
        return;
    }

    // Word-complete: commit any complete (space-terminated) prefix, leave the rest editable
    // (including normal backspacing of a not-yet-committed word).
    const int lastSpace = text.lastIndexOf(QChar(' '));
    if (lastSpace < 0)
        return;

    const QString toCommit = text.left(lastSpace + 1);
    const QString remainder = text.mid(lastSpace + 1);
    commitText(toCommit);
    m_input->blockSignals(true);
    m_input->setText(remainder);
    m_input->blockSignals(false);
}

void CwSendDialog::onReturnPressed() {
    finishPendingWord();
}

void CwSendDialog::onMacroClicked(int slotIndex) {
    // Finish whatever's mid-typing first so macro text doesn't interleave into a partial word.
    finishPendingWord();

    const MacroEntry entry = RadioSettings::instance()->cwMacro(kSlotIds[slotIndex]);
    if (entry.command.isEmpty())
        return;

    // Macro text doesn't carry its own leading space, so back-to-back macro clicks (or a
    // macro right after manually typed text) would otherwise run together with no word gap.
    if (m_displayLength > 0) {
        const QString existing = m_display->toPlainText();
        if (!existing.isEmpty() && !existing.back().isSpace())
            commitText(QStringLiteral(" "));
    }

    commitText(entry.command);
    m_controller->flush(); // dispatch a trailing partial word if the macro doesn't end in a space
}
