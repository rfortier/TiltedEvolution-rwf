#pragma once

// "<module>+0x<offset>" for an address that lies inside a loaded module,
// "unmapped 0x<address>" otherwise; lets any raw address in the log be tied
// back to an image.
void FormatModuleOffset(uintptr_t apAddress, char (&aBuf)[MAX_PATH + 48]);

class CrashHandler
{
    PVOID m_handler;
    static LPTOP_LEVEL_EXCEPTION_FILTER m_pUnhandled; // For remembering "original" UnhandledExceptionFilter

  public:
    CrashHandler();
    ~CrashHandler();

    static void RemovePreviousDump(std::filesystem::path path);
    static inline LPTOP_LEVEL_EXCEPTION_FILTER GetOriginalUnhandledExceptionFilter()
    {
        return m_pUnhandled;
    }
};
