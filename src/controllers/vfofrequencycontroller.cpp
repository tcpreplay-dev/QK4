#include "vfofrequencycontroller.h"

#include "models/radiostate.h"
#include "ui/widgets/vfowidget.h"
#include "utils/radioutils.h"

#include <QString>

VfoFrequencyController::VfoFrequencyController(RadioState *radioState, VFOWidget *vfoA, VFOWidget *vfoB,
                                               QObject *parent)
    : QObject(parent), m_radioState(radioState), m_vfoA(vfoA), m_vfoB(vfoB) {
    connect(m_radioState, &RadioState::frequencyChanged, this, &VfoFrequencyController::onFrequencyChanged);
    connect(m_radioState, &RadioState::frequencyBChanged, this, &VfoFrequencyController::onFrequencyBChanged);
}

VfoFrequencyController::~VfoFrequencyController() {
    disconnect(this);
}

void VfoFrequencyController::refresh() {
    refreshVfoA();
    refreshVfoB();
}

void VfoFrequencyController::refreshVfoA() {
    onFrequencyChanged(m_radioState->vfoA());
}

void VfoFrequencyController::refreshVfoB() {
    onFrequencyBChanged(m_radioState->vfoB());
}

void VfoFrequencyController::onFrequencyChanged(quint64 freq) {
    // WHY: the shown frequency is the *dial* plus whichever offset is
    // currently active. When transmitting with XIT (no split), VFO A is
    // the TX VFO and its display shows dial+XIT. With RIT, displayed RX
    // frequency = dial + RO.
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && !m_radioState->splitEnabled()) {
        const qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabled()) {
        const qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoA->setFrequency(RadioUtils::formatFrequency(freq));
}

void VfoFrequencyController::onFrequencyBChanged(quint64 freq) {
    // WHY: in split mode, VFO B is the TX VFO and XIT offset lives in
    // RO$. With BSET + RIT (VFO B as receive VFO), the RIT offset also
    // lives in RO$.
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && m_radioState->splitEnabled()) {
        const qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabledB()) {
        const qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoB->setFrequency(RadioUtils::formatFrequency(freq));
}
