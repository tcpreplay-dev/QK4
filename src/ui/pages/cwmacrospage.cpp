#include "ui/pages/cwmacrospage.h"

#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "utils/macroids.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
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

    auto *desc = new QLabel("Macro buttons for the CW Send dialog.", this);
    desc->setWordWrap(true);
    desc->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(desc);

    auto *legend = new QLabel("~  My Call     *  Callsign", this);
    legend->setStyleSheet(QString("QLabel { background-color: %1; color: %2; padding: 6px 10px; "
                                  "border-radius: 4px; font-family: monospace; }")
                              .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextGray));
    layout->addWidget(legend);

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
}
