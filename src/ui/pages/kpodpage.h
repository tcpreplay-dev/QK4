#ifndef KPODPAGE_H
#define KPODPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QVector>

class QVBoxLayout;
class QGridLayout;
class QLineEdit;
class QPushButton;
class QFrame;
class KpodDevice;
class KpodPlusDevice;

/**
 * @brief OptionsDialog "K-Pod" tab. Toggles KPOD USB tuning-knob integration, shows
 *        probe/descriptor info (vendor id, product id, firmware) from the connected device,
 *        and shows the 16 K-Pod button macros (F1-F8 tap and hold). When a KPOD+ is
 *        detected, shows additional keyer configuration controls.
 *
 * The macro rows are read-only mirrors of the RadioSettings macro store (function IDs
 * MacroIds::Kpod1T .. Kpod8H) — MacroDialog (Fn > MACROS) is the only editor. The page
 * reloads on RadioSettings::macrosChanged so it never shows a stale binding.
 */
class KpodPage : public QWidget {
    Q_OBJECT

public:
    explicit KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent = nullptr);

    void refresh();

private:
    // One displayed macro slot (K-Pod button tap or hold).
    struct MacroRow {
        QString functionId;
        QLineEdit *labelEdit = nullptr;
        QLineEdit *commandEdit = nullptr;
    };

    void updateKpodStatus();
    void setupDeviceSummary(QVBoxLayout *layout);
    void setupMacroSection(QVBoxLayout *layout);
    void setupKeyerConfigSection(QVBoxLayout *layout);
    QFrame *addSeparator(QVBoxLayout *layout);

    // Builds one button/label/command trio at gridColumn..gridColumn+2 of row.
    void addMacroRow(QGridLayout *grid, int row, int gridColumn, const QString &functionId, const QString &buttonName);

    void loadMacrosFromSettings();
    // Pick a k4macros.json, show what would change, confirm before overwriting.
    void importFromK4Backup();

    KpodDevice *m_kpodDevice;
    KpodPlusDevice *m_kpodPlusDevice;

    QCheckBox *m_kpodEnableCheckbox = nullptr;
    QLabel *m_kpodStatusLabel = nullptr;
    QLabel *m_kpodProductLabel = nullptr;
    QLabel *m_kpodManufacturerLabel = nullptr;
    QLabel *m_kpodVendorIdLabel = nullptr;
    QLabel *m_kpodProductIdLabel = nullptr;
    QLabel *m_kpodDeviceTypeLabel = nullptr;
    QLabel *m_kpodFirmwareLabel = nullptr;
    QLabel *m_kpodDeviceIdLabel = nullptr;
    QLabel *m_kpodHelpLabel = nullptr;

    // Read-only mirror of the 16 slots; MacroDialog owns editing.
    QVector<MacroRow> m_macroRows;
    QPushButton *m_importBtn = nullptr;

    // KPOD+ configuration controls. Keyer speed / CW pitch / iambic mode /
    // paddle orientation are no longer configured here — they mirror the K4
    // (see CwController). Encode mode has no K4 equivalent and stays manual.
    QWidget *m_keyerConfigWidget = nullptr;
    QFrame *m_keyerSeparator = nullptr;
    QComboBox *m_encodeModeCombo = nullptr;
};

#endif // KPODPAGE_H
