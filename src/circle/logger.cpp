// SPDX-License-Identifier: GPL-3.0-or-later
//
// KoraOS bridge for Circle's CLogger. Rather than Circle's ring-buffered logger
// (which pulls in multicore/scheduler machinery), this minimal implementation
// formats with Circle's CString and writes straight to the KoraOS console.

#include <circle/logger.h>
#include <circle/string.h>

extern "C" void tfp_printf(const char *fmt, ...);

CLogger *CLogger::s_pThis = 0;

CLogger::CLogger(unsigned nLogLevel, CTimer *pTimer, boolean bOverwriteOldest)
    : m_nLogLevel(nLogLevel), m_pTimer(pTimer), m_pTarget(0) {
    (void)bOverwriteOldest;
    s_pThis = this;
}

CLogger::~CLogger(void) {
    s_pThis = 0;
}

boolean CLogger::Initialize(CDevice *pTarget) {
    m_pTarget = pTarget;
    return TRUE;
}

void CLogger::Write(const char *pSource, TLogSeverity Severity,
                    const char *pMessage, ...) {
    va_list Args;
    va_start(Args, pMessage);
    WriteV(pSource, Severity, pMessage, Args);
    va_end(Args);
}

void CLogger::WriteV(const char *pSource, TLogSeverity Severity,
                     const char *pMessage, va_list Args) {
    CString Message;
    Message.FormatV(pMessage, Args);
    tfp_printf("%s: %s\n", pSource, (const char *)Message);
    if (Severity == LogPanic) {
        for (;;) {
            asm volatile("wfi");
        }
    }
}

void CLogger::WriteNoAlloc(const char *pSource, TLogSeverity Severity,
                           const char *pMessage) {
    tfp_printf("%s: %s\n", pSource, pMessage);
    if (Severity == LogPanic) {
        for (;;) {
            asm volatile("wfi");
        }
    }
}

CLogger *CLogger::Get(void) {
    return s_pThis;
}
