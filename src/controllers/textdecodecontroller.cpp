#include "textdecodecontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "ui/dialogs/textdecodewindow.h"

#include <QLoggingCategory>
#include <QWidget>

Q_LOGGING_CATEGORY(qk4TextDecode, "qk4.textdecode")

namespace {

TextDecodeWindow::OperatingMode operatingModeFor(RadioState::Mode mode, int dataSubMode) {
    if (mode == RadioState::CW || mode == RadioState::CW_R)
        return TextDecodeWindow::ModeCW;
    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        switch (dataSubMode) {
        case 1:
            return TextDecodeWindow::ModeAFSK;
        case 2:
            return TextDecodeWindow::ModeFSK;
        case 3:
            return TextDecodeWindow::ModePSK;
        default:
            return TextDecodeWindow::ModeData;
        }
    }
    if (mode == RadioState::LSB || mode == RadioState::USB)
        return TextDecodeWindow::ModeSSB;
    return TextDecodeWindow::ModeOther;
}

} // namespace

TextDecodeController::TextDecodeController(RadioState *radioState, ConnectionController *connection,
                                           QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection),
      m_mainWindow(new TextDecodeWindow(TextDecodeWindow::MainRx, parentWidget)),
      m_subWindow(new TextDecodeWindow(TextDecodeWindow::SubRx, parentWidget)) {

    // MAIN RX window — user-driven events trigger CAT send.
    connect(m_mainWindow, &TextDecodeWindow::enabledChanged, this, [this](bool) {
        noteManualDecodeChange(true);
        sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::wpmRangeChanged, this, [this](int) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::thresholdModeChanged, this, [this](bool) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::thresholdChanged, this, [this](int) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::closeRequested, this, [this]() {
        m_mainWindow->setDecodeEnabled(false);
        sendTextDecodeCmd(m_mainWindow, true);
        m_mainWindow->clearText();
        m_mainWindow->hide();
    });

    // SUB RX window — symmetric wiring.
    connect(m_subWindow, &TextDecodeWindow::enabledChanged, this, [this](bool) {
        noteManualDecodeChange(false);
        sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::wpmRangeChanged, this, [this](int) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::thresholdModeChanged, this, [this](bool) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::thresholdChanged, this, [this](int) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::closeRequested, this, [this]() {
        m_subWindow->setDecodeEnabled(false);
        sendTextDecodeCmd(m_subWindow, false);
        m_subWindow->clearText();
        m_subWindow->hide();
    });

    // User-driven data rate changes → send DR / DR$ command.
    connect(m_mainWindow, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connection->isConnected()) {
            m_radioState->setDataRate(rate);
            m_connection->sendCAT(QString("DR%1;").arg(rate));
        }
    });
    connect(m_subWindow, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connection->isConnected()) {
            m_radioState->setDataRateB(rate);
            m_connection->sendCAT(QString("DR$%1;").arg(rate));
        }
    });

    // Radio-driven data rate echoes → update window display.
    connect(m_radioState, &RadioState::dataRateChanged, this, [this](int rate) { m_mainWindow->setDataRate(rate); });
    connect(m_radioState, &RadioState::dataRateBChanged, this, [this](int rate) { m_subWindow->setDataRate(rate); });

    // Radio text-decode config echoes → sync window state.
    connect(m_radioState, &RadioState::textDecodeChanged, this, [this]() {
        if (m_applyingOptimisticState)
            return; // our own values on their way out — see sendTextDecodeCmd()
        int mode = m_radioState->textDecodeMode();
        bool enabled = (mode > 0);
        m_mainWindow->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4)
            m_mainWindow->setWpmRange(mode - 2);
        int threshold = m_radioState->textDecodeThreshold();
        m_mainWindow->setAutoThreshold(threshold == 0);
        if (threshold > 0)
            m_mainWindow->setThreshold(threshold);
        m_mainWindow->setMaxLines(m_radioState->textDecodeLines());
    });
    connect(m_radioState, &RadioState::textDecodeBChanged, this, [this]() {
        if (m_applyingOptimisticState)
            return; // our own values on their way out — see sendTextDecodeCmd()
        int mode = m_radioState->textDecodeModeB();
        bool enabled = (mode > 0);
        m_subWindow->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4)
            m_subWindow->setWpmRange(mode - 2);
        int threshold = m_radioState->textDecodeThresholdB();
        m_subWindow->setAutoThreshold(threshold == 0);
        if (threshold > 0)
            m_subWindow->setThreshold(threshold);
        m_subWindow->setMaxLines(m_radioState->textDecodeLinesB());
    });

    // Mode changes → refresh the window's operating mode so the title bar / controls reflect
    // the new mode (CW WPM vs data rate). Done whether or not the window is visible: a hidden
    // window is still the source sendTextDecodeCmd() reads the TD mode digit from, so letting
    // it go stale means the next enable asks for the wrong decoder.
    connect(m_radioState, &RadioState::modeChanged, this, [this]() { syncWindowToRadio(true); });
    connect(m_radioState, &RadioState::modeBChanged, this, [this]() { syncWindowToRadio(false); });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this]() { syncWindowToRadio(true); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this]() { syncWindowToRadio(false); });

    // Decoded text buffer from radio → route to the correct window.
    connect(m_radioState, &RadioState::textBufferReceived, this, [this](const QString &text, bool isSubRx) {
        if (isSubRx) {
            m_subWindow->appendText(text);
        } else {
            m_mainWindow->appendText(text);
        }
    });
}

TextDecodeController::~TextDecodeController() {
    // Architecture Rule 11 — disconnect first to prevent queued signals
    // from arriving during partial destruction.
    disconnect(this);
    // m_mainWindow / m_subWindow are owned by parentWidget (passed to the
    // TextDecodeWindow constructor) and delete via Qt parent-ownership.
}

void TextDecodeController::syncWindowToRadio(bool isMainRx) {
    TextDecodeWindow *window = isMainRx ? m_mainWindow : m_subWindow;
    const RadioState::Mode mode = isMainRx ? m_radioState->mode() : m_radioState->modeB();
    const int subMode = isMainRx ? m_radioState->dataSubMode() : m_radioState->dataSubModeB();

    // Load-bearing beyond the title bar: sendTextDecodeCmd() derives the TD mode digit from
    // operatingMode() — 2..4 (a CW WPM range) for ModeCW, 1 for everything else. A window left
    // on its ModeCW default would ask a receiver sitting in FSK to run the CW decoder.
    window->setOperatingMode(operatingModeFor(mode, subMode));

    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        const int dr = isMainRx ? m_radioState->dataRate() : m_radioState->dataRateB();
        if (dr >= 0)
            window->setDataRate(dr);
    }
}

void TextDecodeController::showMainRx() {
    syncWindowToRadio(true);
    m_mainWindow->show();
    if (!m_mainWindow->isDecodeEnabled())
        m_mainWindow->setDecodeEnabled(true);
}

void TextDecodeController::showSubRx() {
    syncWindowToRadio(false);
    m_subWindow->show();
    if (!m_subWindow->isDecodeEnabled())
        m_subWindow->setDecodeEnabled(true);
}

void TextDecodeController::applyFskAutoDecode(bool isMainRx, bool fskActive) {
    TextDecodeWindow *window = isMainRx ? m_mainWindow : m_subWindow;
    bool &autoEnabled = isMainRx ? m_autoEnabledMain : m_autoEnabledSub;

    if (fskActive) {
        if (autoEnabled)
            return; // already ours
        const int reportedMode = isMainRx ? m_radioState->textDecodeMode() : m_radioState->textDecodeModeB();
        qCDebug(qk4TextDecode) << "FSK auto-decode:" << (isMainRx ? "Main RX" : "Sub RX")
                               << "reported TD mode" << reportedMode;
        if (reportedMode > 0)
            return; // the operator's own decoder is already running — not ours to touch
        if (reportedMode < 0) {
            // -1 = we have never seen a TD for this receiver. Ask again — the radio may simply
            // not have answered the post-RDY query (observed on Sub RX while it was switched
            // off at connect time). Enabling anyway is deliberate: bailing here left the
            // feature silently dead, which is worse than the failure it was guarding against
            // (switching off, on the way out of FSK, a decoder the operator had running).
            if (m_connection->isConnected())
                m_connection->sendCAT(isMainRx ? QStringLiteral("TD;") : QStringLiteral("TD$;"));
        }

        autoEnabled = true;
        m_applyingAutoDecode = true;
        // Must happen before the enable: the window is hidden here, and the mode-change
        // observers below only refresh a visible one, so without this it would still be on
        // its ModeCW default and send a CW-decode TD to an FSK receiver.
        syncWindowToRadio(isMainRx);
        if (window->isDecodeEnabled())
            sendTextDecodeCmd(window, isMainRx); // window already thinks it's on; make the radio agree
        else
            window->setDecodeEnabled(true); // enabledChanged -> sendTextDecodeCmd
        m_applyingAutoDecode = false;
        qCDebug(qk4TextDecode) << "FSK auto-decode enabled for" << (isMainRx ? "Main RX" : "Sub RX");
        return;
    }

    if (!autoEnabled)
        return;
    autoEnabled = false;
    m_applyingAutoDecode = true;
    window->setDecodeEnabled(false);
    m_applyingAutoDecode = false;
    qCDebug(qk4TextDecode) << "FSK auto-decode restored (off) for" << (isMainRx ? "Main RX" : "Sub RX");
}

void TextDecodeController::noteManualDecodeChange(bool isMainRx) {
    if (m_applyingAutoDecode)
        return;
    // The operator (or the radio, echoing a front-panel change) took over. Drop the undo —
    // restoring later would be overriding a deliberate choice.
    bool &autoEnabled = isMainRx ? m_autoEnabledMain : m_autoEnabledSub;
    autoEnabled = false;
}

void TextDecodeController::onDisconnected() {
    m_autoEnabledMain = false;
    m_autoEnabledSub = false;
}

void TextDecodeController::sendTextDecodeCmd(TextDecodeWindow *window, bool isMainRx) {
    if (!m_connection->isConnected())
        return;
    int mode = 0;
    int threshold = 0;
    if (window->isDecodeEnabled()) {
        auto opMode = window->operatingMode();
        if (opMode == TextDecodeWindow::ModeCW) {
            mode = 2 + window->wpmRange();
            threshold = window->autoThreshold() ? 0 : window->threshold();
        } else {
            mode = 1;
        }
    }
    // Captured before anything is sent: the optimistic setters below emit textDecodeChanged
    // synchronously, and that handler writes threshold/maxLines back into this same window from
    // RadioState values that haven't been updated yet. Reading window->maxLines() again after
    // that point would store something other than what was actually sent.
    const int lines = window->maxLines();

    QString cmdPrefix = isMainRx ? "TD" : "TD$";
    QString cmd = QString("%1%2%3%4;").arg(cmdPrefix).arg(mode).arg(threshold).arg(lines);
    qCDebug(qk4TextDecode) << "sending" << cmd;
    m_connection->sendCAT(cmd);

    // The K4 does not echo a TD SET — same as DT and DR (see datacontrolstate.h). Without this,
    // RadioState's textDecodeMode stays on whatever it last *read*, so everything downstream
    // believes the decoder is still off: switching Sub RX on with VFO B already in FSK enabled
    // the decoder on the radio but never raised the dialog's Sub pane, and only a reconnect —
    // which re-queries TD$; and finally reads the true value — fixed it.
    //
    // Optimistic update, mirroring the DT/DR precedent. The setters no-op when the value is
    // unchanged, so a radio that does echo simply confirms what is already stored.
    // All three setters emit textDecodeChanged synchronously, and that handler exists to sync
    // the window FROM the radio. Re-entering it here would have it write our own values back
    // into the window they came from, reading whichever of the three fields hadn't been set
    // yet — enough to clamp the -1 "never received" lines sentinel into the window, flip
    // auto-threshold off in a CW session, and (setting mode last) briefly toggle the window's
    // enabled state, which bounces straight back into this function. The window already holds
    // exactly these values, so the guard simply skips that work.
    m_applyingOptimisticState = true;
    if (isMainRx) {
        m_radioState->setTextDecodeMode(mode);
        m_radioState->setTextDecodeThreshold(threshold);
        m_radioState->setTextDecodeLines(lines);
    } else {
        m_radioState->setTextDecodeModeB(mode);
        m_radioState->setTextDecodeThresholdB(threshold);
        m_radioState->setTextDecodeLinesB(lines);
    }
    m_applyingOptimisticState = false;
}
