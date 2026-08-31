#include "textdecodestate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void TextDecodeState::reset() {
    textDecodeMode = -1;
    textDecodeThreshold = -1;
    textDecodeLines = -1;
    textDecodeModeB = -1;
    textDecodeThresholdB = -1;
    textDecodeLinesB = -1;
    txBufferLevel = -1;
}

namespace TextDecodeHandlers {

void handleTD(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    const int mode = cmd.mid(2, 1).toInt();
    const int threshold = cmd.mid(3, 1).toInt();
    const int lines = cmd.mid(4, 1).toInt();
    bool changed = false;
    if (mode != state.textDecodeMode) {
        state.textDecodeMode = mode;
        changed = true;
    }
    if (threshold != state.textDecodeThreshold) {
        state.textDecodeThreshold = threshold;
        changed = true;
    }
    if (lines != state.textDecodeLines && lines >= 1 && lines <= 9) {
        state.textDecodeLines = lines;
        changed = true;
    }
    if (changed)
        emit owner.textDecodeChanged();
}

void handleTDSub(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 6)
        return;
    const int mode = cmd.mid(3, 1).toInt();
    const int threshold = cmd.mid(4, 1).toInt();
    const int lines = cmd.mid(5, 1).toInt();
    bool changed = false;
    if (mode != state.textDecodeModeB) {
        state.textDecodeModeB = mode;
        changed = true;
    }
    if (threshold != state.textDecodeThresholdB) {
        state.textDecodeThresholdB = threshold;
        changed = true;
    }
    if (lines != state.textDecodeLinesB && lines >= 1 && lines <= 9) {
        state.textDecodeLinesB = lines;
        changed = true;
    }
    if (changed)
        emit owner.textDecodeBChanged();
}

// Shared by both variants: prefixLen is 2 for "TB", 3 for "TB$", after which the layout is
// one TX-buffer digit, a two-digit count of the received characters, then those characters.
void handleTxBufferLevel(TextDecodeState &state, RadioState &owner, const QString &cmd, int prefixLen) {
    bool ok = false;
    const int level = cmd.mid(prefixLen, 1).toInt(&ok);
    if (!ok || level == state.txBufferLevel)
        return;
    state.txBufferLevel = level;
    emit owner.txBufferLevelChanged(level);
}

void handleTB(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    handleTxBufferLevel(state, owner, cmd, 2);
    QString text = cmd.mid(5);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit owner.textBufferReceived(text, false);
}

void handleTBSub(TextDecodeState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 6)
        return;
    handleTxBufferLevel(state, owner, cmd, 3);
    QString text = cmd.mid(6);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit owner.textBufferReceived(text, true);
}

void setMode(TextDecodeState &state, RadioState &owner, int mode) {
    mode = qBound(0, mode, 4);
    if (mode != state.textDecodeMode) {
        state.textDecodeMode = mode;
        emit owner.textDecodeChanged();
    }
}

void setThreshold(TextDecodeState &state, RadioState &owner, int threshold) {
    threshold = qBound(0, threshold, 9);
    if (threshold != state.textDecodeThreshold) {
        state.textDecodeThreshold = threshold;
        emit owner.textDecodeChanged();
    }
}

void setLines(TextDecodeState &state, RadioState &owner, int lines) {
    lines = qBound(1, lines, 10);
    if (lines != state.textDecodeLines) {
        state.textDecodeLines = lines;
        emit owner.textDecodeChanged();
    }
}

void setModeB(TextDecodeState &state, RadioState &owner, int mode) {
    mode = qBound(0, mode, 4);
    if (mode != state.textDecodeModeB) {
        state.textDecodeModeB = mode;
        emit owner.textDecodeBChanged();
    }
}

void setThresholdB(TextDecodeState &state, RadioState &owner, int threshold) {
    threshold = qBound(0, threshold, 9);
    if (threshold != state.textDecodeThresholdB) {
        state.textDecodeThresholdB = threshold;
        emit owner.textDecodeBChanged();
    }
}

void setLinesB(TextDecodeState &state, RadioState &owner, int lines) {
    lines = qBound(1, lines, 10);
    if (lines != state.textDecodeLinesB) {
        state.textDecodeLinesB = lines;
        emit owner.textDecodeBChanged();
    }
}

} // namespace TextDecodeHandlers
