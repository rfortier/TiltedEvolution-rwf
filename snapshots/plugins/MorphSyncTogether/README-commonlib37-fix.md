# MorphSyncTogether v0.2.0 — CommonLib recent fix v2

This source combines both required compatibility fixes:

1. CommonLib/SKSE headers are parsed before Win32/Winsock headers. Winsock is kept out of PCH.h.
2. RE::TintMask is treated as an opaque pointer. No RE/T/TintMask.h include and no TintMask field dereference is used, because the installed CommonLib package only exposes the type incompletely to this project.

The appearance probe remains read-only. RaceMenu/SKEE overlay and node-property probing remains detailed.
