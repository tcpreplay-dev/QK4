#ifndef STRAIGHTKEYPAGE_H
#define STRAIGHTKEYPAGE_H

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

class HalikeyDevice;
class KeyerConnectionWidget;

/**
 * @brief Options page for a straight key, bug, or external keyer feeding a single key line.
 *
 * Distinct from the Keyer page: that one drives the K4's iambic keyer from a paddle, this
 * sends each contact closure as an explicitly-timed KZD/U element so the operator's own
 * weighting and spacing reach the air. A second HaliKey-class interface, so both can be
 * connected at once.
 */
class StraightKeyPage : public QWidget {
    Q_OBJECT

public:
    StraightKeyPage(HalikeyDevice *device, HalikeyDevice *keyerDevice, QWidget *parent = nullptr);

    void refresh();
    void setPageVisible(bool visible);

private:
    void updateBufferSummary();

    KeyerConnectionWidget *m_connection = nullptr;
    QCheckBox *m_bufferCheck = nullptr;
    QLabel *m_minWpmLabel = nullptr;
    QSpinBox *m_minWpmSpin = nullptr;
    QLabel *m_maxWpmLabel = nullptr;
    QSpinBox *m_maxWpmSpin = nullptr;
    QLabel *m_ratioLabel = nullptr;
    QDoubleSpinBox *m_ratioSpin = nullptr;
    QLabel *m_bufferSummary = nullptr;
    QSlider *m_sidetoneVolumeSlider = nullptr;
    QLabel *m_sidetoneVolumeValueLabel = nullptr;
};

#endif // STRAIGHTKEYPAGE_H
