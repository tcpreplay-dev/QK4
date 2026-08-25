#ifndef KEYERCONNECTIONWIDGET_H
#define KEYERCONNECTIONWIDGET_H

#include "settings/radiosettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class HalikeyDevice;

/**
 * @brief Device-type / port / connect controls for one keyer role, shared by the Keyer and
 *        Straight Key pages so the two stay identical and the port-conflict rule lives in
 *        one place.
 *
 * Conflict: the two roles must not open the same port. On Windows the OS refuses the second
 * open (CreateFileW with dwShareMode 0), but on macOS and Linux it silently succeeds, so the
 * guard has to be here rather than relying on the platform. Comparison is by port string, so
 * two names belonging to one physical interface (macOS exposes cu.* and tty.* for the same
 * device) are not caught — called out in the help text rather than papered over.
 *
 * There is no Refresh button: a poll re-enumerates while the page is visible or while
 * auto-connect is armed and waiting, and the list is rebuilt only when the port set actually
 * changes so an open popup or a live selection is never yanked away.
 */
class KeyerConnectionWidget : public QWidget {
    Q_OBJECT

public:
    KeyerConnectionWidget(RadioSettings::KeyerRole role, HalikeyDevice *device, HalikeyDevice *otherDevice,
                          QWidget *parent = nullptr);

    void refresh();
    void setPollingActive(bool active); // page visibility drives this

private:
    void rebuildPorts();
    void updateStatus();
    void onConnectClicked();
    QStringList enumeratePorts() const;
    QString conflictingPort() const;

    RadioSettings::KeyerRole m_role;
    HalikeyDevice *m_device;
    HalikeyDevice *m_otherDevice;

    QComboBox *m_deviceTypeCombo = nullptr;
    QComboBox *m_portCombo = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    QTimer *m_pollTimer = nullptr;
    QStringList m_lastPorts; // diffed so the combo is only rebuilt on real change

};

#endif // KEYERCONNECTIONWIDGET_H
