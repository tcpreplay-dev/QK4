#include "ui/pages/cwmacrospage.h"

#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "utils/macroids.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
QFrame *separator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    return line;
}

const QVector<QString> kSlotIds = {MacroIds::CwMacro1, MacroIds::CwMacro2, MacroIds::CwMacro3, MacroIds::CwMacro4,
                                   MacroIds::CwMacro5, MacroIds::CwMacro6, MacroIds::CwMacro7, MacroIds::CwMacro8};
} // namespace

CwMacrosPage::CwMacrosPage(QWidget *parent) : QWidget(parent) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *title = new QLabel("CW Macros", this);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(title);

    auto *desc = new QLabel("Canned messages for the CW Send dialog's macro buttons. Label is what shows "
                            "on the button; message is what gets keyed.",
                            this);
    desc->setWordWrap(true);
    desc->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(desc);

    for (int i = 0; i < kSlotIds.size(); ++i) {
        auto *row = new QHBoxLayout();

        auto *slotLabel = new QLabel(QStringLiteral("F%1").arg(i + 1), this);
        slotLabel->setStyleSheet(K4Styles::Dialog::formLabel());
        slotLabel->setFixedWidth(30);
        row->addWidget(slotLabel);

        MacroRow macroRow;
        macroRow.functionId = kSlotIds[i];

        macroRow.labelEdit = new QLineEdit(this);
        macroRow.labelEdit->setPlaceholderText("Button label");
        macroRow.labelEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
        macroRow.labelEdit->setFixedWidth(140);
        row->addWidget(macroRow.labelEdit);

        macroRow.textEdit = new QLineEdit(this);
        macroRow.textEdit->setPlaceholderText("Message to send");
        macroRow.textEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
        row->addWidget(macroRow.textEdit, 1);

        layout->addLayout(row);

        const QString functionId = macroRow.functionId;
        connect(macroRow.labelEdit, &QLineEdit::editingFinished, this, [this, functionId]() {
            for (const MacroRow &r : m_rows) {
                if (r.functionId == functionId) {
                    RadioSettings::instance()->setCwMacro(functionId, r.labelEdit->text(), r.textEdit->text());
                    break;
                }
            }
        });
        connect(macroRow.textEdit, &QLineEdit::editingFinished, this, [this, functionId]() {
            for (const MacroRow &r : m_rows) {
                if (r.functionId == functionId) {
                    RadioSettings::instance()->setCwMacro(functionId, r.labelEdit->text(), r.textEdit->text());
                    break;
                }
            }
        });

        m_rows.append(macroRow);
    }

    layout->addWidget(separator(this));

    auto *modeHeader = new QLabel("Send Mode", this);
    modeHeader->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(modeHeader);

    m_immediateModeCheck = new QCheckBox("Send characters immediately as typed", this);
    m_immediateModeCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_immediateModeCheck->setChecked(RadioSettings::instance()->cwSendImmediateMode());
    connect(m_immediateModeCheck, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setCwSendImmediateMode(checked); });
    layout->addWidget(m_immediateModeCheck);

    auto *modeHelp = new QLabel("Off (default): a word is sent once you finish it with a space or Enter. "
                                "On: every character is sent as you type it. Either way, text only "
                                "brightens on screen once the K4 actually confirms it was keyed.",
                                this);
    modeHelp->setWordWrap(true);
    modeHelp->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(modeHelp);

    layout->addStretch();
    refresh();
}

void CwMacrosPage::refresh() {
    auto *rs = RadioSettings::instance();
    for (const MacroRow &row : m_rows) {
        const MacroEntry entry = rs->cwMacro(row.functionId);
        row.labelEdit->setText(entry.label);
        row.textEdit->setText(entry.command);
    }
    m_immediateModeCheck->setChecked(rs->cwSendImmediateMode());
}
