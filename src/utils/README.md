# utils/

Shared helper functions. Any helper needed in more than one translation unit goes here — Architecture Rule 1.

## Files

- `radioutils.{cpp,h}` — `RadioUtils::` namespace. Frequency → band (11 bands + gaps), band edges, tuning step tables, span-dial stepping with K4 quirks.
- `k4macrobackup.{cpp,h}` — `K4MacroBackup::` namespace. Parses the `k4macros.json` the K4's backup utility (Fn > hold BACKUP) writes under `K4_SN<serial>/`, translating its slot names (`Kpod.1T`, `REM.ANT`) to `MacroIds`. Pure parsing — the user picks the file from Options > K-Pod; nothing scans volumes.

## Rule 1 — No duplicated static functions

If you catch yourself copy-pasting a helper into a second `.cpp` file, promote it to `utils/` with a namespace. Copy-pasting is a defect; fix it immediately.

## Shape

Each utility is a free function or small namespace. Zero state, zero side effects, zero dependencies on Qt UI types.

## See also

- `CONVENTIONS.md` → Architecture Rule 1.
- `memory/k4-span-stepping.md` — K4 span dial quirk driving `RadioUtils::nextSpan` logic.
