#ifndef CWKEYERPAGE_H
#define CWKEYERPAGE_H

#include <QLabel>
#include <QRadioButton>
#include <QSlider>
#include <QWidget>

class HalikeyDevice;
class KeyerConnectionWidget;
class RadioState;
class ConnectionController;

/**
 * @brief Options page for a paddle driving the K4's iambic keyer.
 *
 * Settings here are sent to the radio as KP (iambic mode, paddle orientation, keying weight).
 * Paddle reversal is the radio's own setting rather than a local override — one place to
 * change it, so there is no question of two reversals composing.
 *
 * A straight key, bug, or external keyer uses the separate Straight Key page and its own
 * interface, so both can be connected at once.
 */
class CwKeyerPage : public QWidget {
    Q_OBJECT

public:
    CwKeyerPage(HalikeyDevice *device, HalikeyDevice *straightKeyDevice, RadioState *radioState,
                ConnectionController *connection, QWidget *parent = nullptr);
    ~CwKeyerPage() override;

    void refresh();
    void setPageVisible(bool visible);

private:
    void sendKeyerPaddle(); // KP carries all three fields at once
    void updateFromRadio();

    RadioState *m_radioState;
    ConnectionController *m_connection;

    KeyerConnectionWidget *m_connectionWidget = nullptr;
    QRadioButton *m_iambicARadio = nullptr;
    QRadioButton *m_iambicBRadio = nullptr;
    QRadioButton *m_orientNormalRadio = nullptr;
    QRadioButton *m_orientReversedRadio = nullptr;
    QSlider *m_weightSlider = nullptr;
    QLabel *m_weightValueLabel = nullptr;
    QSlider *m_sidetoneVolumeSlider = nullptr;
    QLabel *m_sidetoneVolumeValueLabel = nullptr;
    // Suppresses KP sends. Set while applying the radio's own echo (so following it doesn't
    // bounce straight back), and latched in the destructor: tearing down the radio buttons
    // emits toggled, and by then the connection may already be gone.
    bool m_suppressSend = false;
};

#endif // CWKEYERPAGE_H
