// SPDX-License-Identifier: GPL-3.0-or-later
//
// KoraOS bridge for Circle's CTimer. Delays come from KoraOS's generic-timer
// helpers (lib/timer.c); the periodic tick and kernel-timer callbacks ride on
// KoraOS's system tick (arch/systick.c) via its tick hook -- so there is no
// second hardware timer. Circle's CLOCKHZ is 1 MHz and HZ is 100, matching
// timer_us() and the KoraOS 100 Hz systick respectively.

#include <circle/timer.h>

#include "arch/systick.h"
#include "lib/timer.h"

CTimer *CTimer::s_pThis = 0;

namespace {

struct KernelTimer {
    bool active;
    unsigned deadline;  // in HZ ticks
    TKernelTimerHandler *handler;
    void *param;
    void *context;
};

constexpr unsigned kMaxKernelTimers = 32;
KernelTimer g_kernel_timers[kMaxKernelTimers];

// Fire any kernel timers whose deadline has passed. Kept as a free function so
// the tick hook can call it without touching CTimer's private members.
void poll_kernel_timers(void) {
    unsigned now = (unsigned)systick_count();
    for (unsigned i = 0; i < kMaxKernelTimers; i++) {
        KernelTimer &t = g_kernel_timers[i];
        // Signed compare so wraparound of the 32-bit tick count is handled.
        if (t.active && (int)(now - t.deadline) >= 0) {
            t.active = false;
            if (t.handler != 0) {
                t.handler((TKernelTimerHandle)(i + 1), t.param, t.context);
            }
        }
    }
}

void tick_hook(void) {
    poll_kernel_timers();
}

}  // namespace

CTimer::CTimer(CInterruptSystem *pInterruptSystem)
    : m_pInterruptSystem(pInterruptSystem) {
    s_pThis = this;
    m_nMsDelay = 200000;   // unused: delays use the generic timer, not DelayLoop
    m_nusDelay = 200;
}

CTimer::~CTimer(void) {
    systick_set_tick_hook(0);
    s_pThis = 0;
}

boolean CTimer::Initialize(void) {
    for (unsigned i = 0; i < kMaxKernelTimers; i++) {
        g_kernel_timers[i].active = false;
    }
    systick_set_tick_hook(tick_hook);
    return TRUE;
}

unsigned CTimer::GetClockTicks(void) {
    return (unsigned)timer_us();  // 1 MHz free-running microsecond counter
}

u64 CTimer::GetClockTicks64(void) {
    return timer_us();
}

unsigned CTimer::GetTicks(void) const {
    return (unsigned)systick_count();  // 100 Hz
}

void CTimer::SimpleMsDelay(unsigned nMilliSeconds) {
    mdelay(nMilliSeconds);
}

void CTimer::SimpleusDelay(unsigned nMicroSeconds) {
    udelay(nMicroSeconds);
}

TKernelTimerHandle CTimer::StartKernelTimer(unsigned nDelay,
                                            TKernelTimerHandler *pHandler,
                                            void *pParam, void *pContext) {
    for (unsigned i = 0; i < kMaxKernelTimers; i++) {
        if (!g_kernel_timers[i].active) {
            g_kernel_timers[i].active = true;
            g_kernel_timers[i].deadline = GetTicks() + nDelay;
            g_kernel_timers[i].handler = pHandler;
            g_kernel_timers[i].param = pParam;
            g_kernel_timers[i].context = pContext;
            return (TKernelTimerHandle)(i + 1);  // 0 is the invalid handle
        }
    }
    return 0;
}

void CTimer::CancelKernelTimer(TKernelTimerHandle hTimer) {
    if (hTimer == 0 || hTimer > kMaxKernelTimers) {
        return;
    }
    g_kernel_timers[hTimer - 1].active = false;
}

void CTimer::PollKernelTimers(void) {
    poll_kernel_timers();
}

CTimer *CTimer::Get(void) {
    return s_pThis;
}
