#include "textsendcontroller.h"

#include <QStringList>
#include <QtGlobal>

TextSendController::TextSendController(QObject *parent) : QObject(parent) {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true); // re-armed manually each tick — see pollTick()
    connect(m_pollTimer, &QTimer::timeout, this, &TextSendController::pollTick);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &TextSendController::onChunkTimeout);

    m_txSettleTimer = new QTimer(this);
    m_txSettleTimer->setSingleShot(true);
    connect(m_txSettleTimer, &QTimer::timeout, this, [this]() {
        m_awaitingSettle = false;
        maybeDispatchNext();
    });

    m_txHangTimer = new QTimer(this);
    m_txHangTimer->setSingleShot(true);
    connect(m_txHangTimer, &QTimer::timeout, this, [this]() { endTransmission(true); });
}

void TextSendController::appendChar(QChar ch) {
    if (m_stalled || m_sessionMode == SessionMode::None)
        return;

    m_pendingText += ch;

    const bool boundary = m_immediateMode || ch == QChar(' ') || ch == QChar('\n') || ch == QChar('\r');
    if (boundary)
        cutPendingChunk();

    maybeDispatchNext();
}

void TextSendController::flush() {
    if (m_stalled || m_sessionMode == SessionMode::None)
        return;
    cutPendingChunk();
    maybeDispatchNext();
}

void TextSendController::cutPendingChunk() {
    if (m_pendingText.isEmpty())
        return;

    // Split on the KY text limit — a "word" typed with no spaces could otherwise exceed it.
    int offset = 0;
    while (offset < m_pendingText.length()) {
        const int len = qMin(maxChunkChars(), m_pendingText.length() - offset);
        Chunk c;
        c.text = m_pendingText.mid(offset, len);
        c.start = m_pendingStart + offset;
        c.length = len;
        c.immediate = m_immediateMode;
        m_queue.append(c);
        offset += len;
    }

    m_pendingStart += m_pendingText.length();
    m_pendingText.clear();
}

void TextSendController::maybeDispatchNext() {
    if (m_stalled || m_hasInFlight || m_awaitingSettle) {
        updateActiveState();
        return;
    }
    if (m_queue.isEmpty()) {
        m_pollTimer->stop();
        armTxHangIfIdle();
        updateActiveState();
        return;
    }

    if (m_sessionMode == SessionMode::Fsk && !m_txOpen) {
        // Opening the bracket costs a settle delay before any text can go out, so bail here
        // and come back through the settle timer with the queue untouched.
        beginTransmission();
        updateActiveState();
        return;
    }
    // More text arrived before the hang expired — this is still the same transmission.
    m_txHangTimer->stop();

    Chunk next = m_queue.takeFirst();
    if (next.immediate) {
        // Merge whatever else is already queued (typed ahead of the confirm round trip)
        // into one send, capped at the KY text limit — keeps immediate mode from paying a
        // confirm round trip per keystroke while still reporting a real confirmed range.
        while (!m_queue.isEmpty() && m_queue.first().immediate &&
               next.length + m_queue.first().length <= maxChunkChars()) {
            Chunk merged = m_queue.takeFirst();
            next.text += merged.text;
            next.length += merged.length;
        }
    }

    m_inFlight = next;
    m_hasInFlight = true;

    QString sanitized = next.text;
    sanitized.remove(QChar(';')); // must never break CAT command framing
    // m_leadIn is 'E' for CW: hardware-confirmed (twice, cleanly reproduced with no other
    // keying involved) that the K4's TX relay/ALC ramp-up isn't complete by the time it starts
    // keying the first real element of a fresh KY send, so the actual first character gets
    // clipped/eaten on the air. Sacrifice a throwaway dit (shortest possible element) to
    // absorb the ramp-up instead of a real letter — the same idea as CwController's
    // straight-key KZL pre-roll, applied here since KY text sends have no equivalent pre-roll
    // parameter of their own. It is empty for FSK, where the signal is generated digitally
    // and kTxSettleMs covers the same ground without putting a stray character on the air.
    emit sendCatRequested(QStringLiteral("KY%1%2;").arg(m_leadIn, sanitized));
    emit chunkInFlight(next.start, next.length);

    startTimeoutForCurrentChunk();
    // First poll deliberately delayed longer than the steady-state cadence — see the
    // "first KY; poll" note in textsendcontroller.h.
    m_pollTimer->start(kFirstPollDelayMs);

    updateActiveState();
}

void TextSendController::pollTick() {
    if (!m_hasInFlight)
        return; // nothing to confirm — timer simply doesn't get re-armed
    emit sendCatRequested(QStringLiteral("KY;"));
    // Re-arm unconditionally on the steady cadence regardless of whether the previous poll
    // was ever answered — a lost reply must not permanently stop polling (the chunk-level
    // timeout, not this, is what eventually calls a truly dead link).
    m_pollTimer->start(kPollIntervalMs);
}

void TextSendController::onCatResponse(const QString &response) {
    const QStringList commands = response.split(';', Qt::SkipEmptyParts);
    for (const QString &cmd : commands) {
        if (cmd == QLatin1String("KY0")) {
            if (!m_hasInFlight)
                continue;
            m_timeoutTimer->stop();
            m_pollTimer->stop();
            emit chunkConfirmed(m_inFlight.start, m_inFlight.length);
            m_hasInFlight = false;
            maybeDispatchNext();
        }
        // KY1 (still busy) needs no action — pollTick()'s own re-arm keeps asking.
    }
    updateActiveState();
}

void TextSendController::onDisconnected() {
    // A stalled chunk already dropped m_active to false (nothing left queued/in-flight to
    // suppress hardware CW over), but the dialog is still showing a stalled banner and a
    // disabled input at that point — a disconnect must still reach it via aborted().
    if (!m_active && !m_stalled)
        return;
    // No "RX;" — the socket is gone, so the bracket can only be dropped, not closed.
    endTransmission(false);
    resetAll();
    emit aborted();
}

void TextSendController::abort() {
    // Drop the bracket without emitting; the unconditional "RX;" below covers it.
    endTransmission(false);
    resetAll();
    // Always sent, even with nothing queued — this is the panic button, and matches the
    // existing ESC-shortcut convention elsewhere in the app of unconditionally forcing RX;
    // rather than trying to infer whether the radio actually needs it.
    emit sendCatRequested(QStringLiteral("RX;"));
    emit aborted();
}

void TextSendController::setImmediateMode(bool immediate) {
    m_immediateMode = immediate;
}

void TextSendController::setKeyerSpeed(int wpm) {
    if (wpm > 0)
        m_wpm = wpm;
}

void TextSendController::setDataRate(int rate) {
    if (rate >= 0)
        m_dataRate = rate;
}

void TextSendController::beginTransmission() {
    if (m_sessionMode != SessionMode::Fsk || m_txOpen)
        return;
    m_txHangTimer->stop();
    m_txOpen = true;
    emit sendCatRequested(QStringLiteral("TX;"));
    m_awaitingSettle = true;
    m_txSettleTimer->start(kTxSettleMs);
}

void TextSendController::endTransmission(bool linkAlive) {
    m_txSettleTimer->stop();
    m_awaitingSettle = false;
    m_txHangTimer->stop();
    if (!m_txOpen)
        return;
    m_txOpen = false;
    // Whoever keyed the radio owns un-keying it. Unlike CW — where KY ends the transmission by
    // itself and a mode change can just stop emitting — an open bracket left behind by a mode
    // change, a stall or a closing dialog would strand the K4 in TX.
    if (linkAlive)
        emit sendCatRequested(QStringLiteral("RX;"));
    // m_txOpen feeds m_active, so the suppression gate only drops here. Without this the
    // hang-timer path would leave hardware-driven CW muted until some unrelated CAT response
    // happened to run updateActiveState() again.
    updateActiveState();
}

void TextSendController::armTxHangIfIdle() {
    if (!m_txOpen || m_hasInFlight || !m_queue.isEmpty())
        return;
    // Deliberately not immediate: in word-complete mode the queue drains after every word, and
    // closing the bracket there would chop one transmission into one per word. Pending
    // (still-being-typed) text is not counted — an operator who wanders off mid-word must not
    // hold the transmitter open indefinitely; the next word simply re-opens the bracket.
    m_txHangTimer->start(kTxHangMs);
}

void TextSendController::startTimeoutForCurrentChunk() {
    double expectedMs = 0.0;
    if (m_sessionMode == SessionMode::Fsk) {
        expectedMs = qMax(1, m_inFlight.length) * (m_dataRate >= 1 ? kFskFastCharMs : kFskSlowCharMs);
    } else {
        const double ditMs = 1200.0 / qBound(8, m_wpm, 100);
        // ~10 dit-units per character is a rough PARIS-timing average (letters + inter-element/
        // inter-character spacing); the slack factor absorbs R/W waits and network jitter on top.
        expectedMs = qMax(1, m_inFlight.length) * 10.0 * ditMs;
    }
    const int timeoutMs = qBound(kMinTimeoutMs, static_cast<int>(expectedMs * kTimeoutSlackFactor), kMaxTimeoutMs);
    m_timeoutTimer->start(timeoutMs);
}

void TextSendController::onChunkTimeout() {
    if (!m_hasInFlight)
        return;
    m_stalled = true;
    m_pollTimer->stop();
    emit chunkStalled(m_inFlight.start, m_inFlight.length);
    m_hasInFlight = false;
    // A stalled FSK send leaves the radio keyed by our own "TX;" — close the bracket rather
    // than halt the pipeline with the transmitter still up.
    endTransmission(true);
    // Nothing queued behind this is trustworthy once the link looks unhealthy — drop it too,
    // rather than leaving activeChanged(true) latched with nothing actually in flight (that
    // would permanently suppress hardware-driven CW for no reason). The dialog still shows
    // the stalled chunk in its own color; this only clears internal bookkeeping.
    m_queue.clear();
    m_pendingText.clear();
    updateActiveState();
}

void TextSendController::setSessionMode(SessionMode mode) {
    if (m_sessionMode == mode)
        return;

    const bool wasBusy = m_active || m_stalled;
    m_sessionMode = mode;
    m_leadIn = (mode == SessionMode::Cw) ? QStringLiteral("E") : QString();

    // Closes an FSK bracket we opened, with the "RX;" — see endTransmission(). Everything else
    // about a mode change is silent: it's the operator's own action, not a link fault (mirrors
    // how the paddle/straight-key paths just stop emitting on a mode change).
    endTransmission(true);

    if (wasBusy) {
        resetAll();
        emit aborted();
    }
}

void TextSendController::resetAll() {
    m_queue.clear();
    m_pendingText.clear();
    m_hasInFlight = false;
    m_stalled = false;
    m_pollTimer->stop();
    m_timeoutTimer->stop();
    m_txSettleTimer->stop();
    m_awaitingSettle = false;
    // m_pendingStart is NOT reset here — it's a monotonic character offset shared with the
    // dialog's own display-length bookkeeping (see TextSendDialog), so text already shown stays
    // at the same coordinates and abort() doesn't have to wipe the screen to stay in sync.
    updateActiveState();
}

void TextSendController::updateActiveState() {
    // m_txOpen counts: while an FSK bracket is open the radio is still transmitting, including
    // through the hang window, so hardware-driven CW must stay suppressed until "RX;" goes out.
    const bool active = m_hasInFlight || !m_queue.isEmpty() || !m_pendingText.isEmpty() || m_txOpen;
    if (active != m_active) {
        m_active = active;
        emit activeChanged(active);
    }
}
