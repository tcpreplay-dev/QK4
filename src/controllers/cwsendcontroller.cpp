#include "cwsendcontroller.h"

#include <QStringList>
#include <QtGlobal>

CwSendController::CwSendController(QObject *parent) : QObject(parent) {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true); // re-armed manually each tick — see pollTick()
    connect(m_pollTimer, &QTimer::timeout, this, &CwSendController::pollTick);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &CwSendController::onChunkTimeout);
}

void CwSendController::appendChar(QChar ch) {
    if (m_stalled || !m_cwModeActive)
        return;

    m_pendingText += ch;

    const bool boundary = m_immediateMode || ch == QChar(' ') || ch == QChar('\n') || ch == QChar('\r');
    if (boundary)
        cutPendingChunk();

    maybeDispatchNext();
}

void CwSendController::flush() {
    if (m_stalled || !m_cwModeActive)
        return;
    cutPendingChunk();
    maybeDispatchNext();
}

void CwSendController::cutPendingChunk() {
    if (m_pendingText.isEmpty())
        return;

    // Split on the KY text limit — a "word" typed with no spaces could otherwise exceed it.
    int offset = 0;
    while (offset < m_pendingText.length()) {
        const int len = qMin(kMaxChunkChars, m_pendingText.length() - offset);
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

void CwSendController::maybeDispatchNext() {
    if (m_stalled || m_hasInFlight) {
        updateActiveState();
        return;
    }
    if (m_queue.isEmpty()) {
        m_pollTimer->stop();
        updateActiveState();
        return;
    }

    Chunk next = m_queue.takeFirst();
    if (next.immediate) {
        // Merge whatever else is already queued (typed ahead of the confirm round trip)
        // into one send, capped at the KY text limit — keeps immediate mode from paying a
        // confirm round trip per keystroke while still reporting a real confirmed range.
        while (!m_queue.isEmpty() && m_queue.first().immediate &&
               next.length + m_queue.first().length <= kMaxChunkChars) {
            Chunk merged = m_queue.takeFirst();
            next.text += merged.text;
            next.length += merged.length;
        }
    }

    m_inFlight = next;
    m_hasInFlight = true;

    QString sanitized = next.text;
    sanitized.remove(QChar(';')); // must never break CAT command framing
    // Hardware-confirmed (twice, cleanly reproduced with no other keying involved): the K4's
    // TX relay/ALC ramp-up isn't complete by the time it starts keying the first real element
    // of a fresh KY send, so the actual first character gets clipped/eaten on the air.
    // Sacrifice a throwaway leading 'E' (single dit, shortest possible element) to absorb the
    // ramp-up instead of a real letter — the same idea as CwController's straight-key KZL
    // pre-roll, applied here since KY text sends have no equivalent pre-roll parameter of
    // their own.
    emit sendCatRequested(QStringLiteral("KYE%1;").arg(sanitized));
    emit chunkInFlight(next.start, next.length);

    startTimeoutForCurrentChunk();
    // First poll deliberately delayed longer than the steady-state cadence — see the
    // "first KY; poll" note in cwsendcontroller.h.
    m_pollTimer->start(kFirstPollDelayMs);

    updateActiveState();
}

void CwSendController::pollTick() {
    if (!m_hasInFlight)
        return; // nothing to confirm — timer simply doesn't get re-armed
    emit sendCatRequested(QStringLiteral("KY;"));
    // Re-arm unconditionally on the steady cadence regardless of whether the previous poll
    // was ever answered — a lost reply must not permanently stop polling (the chunk-level
    // timeout, not this, is what eventually calls a truly dead link).
    m_pollTimer->start(kPollIntervalMs);
}

void CwSendController::onCatResponse(const QString &response) {
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

void CwSendController::onDisconnected() {
    // A stalled chunk already dropped m_active to false (nothing left queued/in-flight to
    // suppress hardware CW over), but the dialog is still showing a stalled banner and a
    // disabled input at that point — a disconnect must still reach it via aborted().
    if (!m_active && !m_stalled)
        return;
    resetAll();
    emit aborted();
}

void CwSendController::abort() {
    resetAll();
    // Always sent, even with nothing queued — this is the panic button, and matches the
    // existing ESC-shortcut convention elsewhere in the app of unconditionally forcing RX;
    // rather than trying to infer whether the radio actually needs it.
    emit sendCatRequested(QStringLiteral("RX;"));
    emit aborted();
}

void CwSendController::setImmediateMode(bool immediate) {
    m_immediateMode = immediate;
}

void CwSendController::setKeyerSpeed(int wpm) {
    if (wpm > 0)
        m_wpm = wpm;
}

void CwSendController::startTimeoutForCurrentChunk() {
    const double ditMs = 1200.0 / qBound(8, m_wpm, 100);
    // ~10 dit-units per character is a rough PARIS-timing average (letters + inter-element/
    // inter-character spacing); the slack factor absorbs R/W waits and network jitter on top.
    const double expectedMs = qMax(1, m_inFlight.length) * 10.0 * ditMs;
    const int timeoutMs = qBound(kMinTimeoutMs, static_cast<int>(expectedMs * kTimeoutSlackFactor), kMaxTimeoutMs);
    m_timeoutTimer->start(timeoutMs);
}

void CwSendController::onChunkTimeout() {
    if (!m_hasInFlight)
        return;
    m_stalled = true;
    m_pollTimer->stop();
    emit chunkStalled(m_inFlight.start, m_inFlight.length);
    m_hasInFlight = false;
    // Nothing queued behind this is trustworthy once the link looks unhealthy — drop it too,
    // rather than leaving activeChanged(true) latched with nothing actually in flight (that
    // would permanently suppress hardware-driven CW for no reason). The dialog still shows
    // the stalled chunk in its own color; this only clears internal bookkeeping.
    m_queue.clear();
    m_pendingText.clear();
    updateActiveState();
}

void CwSendController::setCwModeActive(bool active) {
    if (m_cwModeActive == active)
        return;
    m_cwModeActive = active;
    if (!active && (m_active || m_stalled)) {
        // Leaving CW mode is the operator's own action, not a link fault — reset silently,
        // no "RX;" panic-stop (mirrors how the paddle/straight-key paths just stop emitting
        // on a mode change rather than forcing TX off).
        resetAll();
        emit aborted();
    }
}

void CwSendController::resetAll() {
    m_queue.clear();
    m_pendingText.clear();
    m_hasInFlight = false;
    m_stalled = false;
    m_pollTimer->stop();
    m_timeoutTimer->stop();
    // m_pendingStart is NOT reset here — it's a monotonic character offset shared with the
    // dialog's own display-length bookkeeping (see CwSendDialog), so text already shown stays
    // at the same coordinates and abort() doesn't have to wipe the screen to stay in sync.
    updateActiveState();
}

void CwSendController::updateActiveState() {
    const bool active = m_hasInFlight || !m_queue.isEmpty() || !m_pendingText.isEmpty();
    if (active != m_active) {
        m_active = active;
        emit activeChanged(active);
    }
}
