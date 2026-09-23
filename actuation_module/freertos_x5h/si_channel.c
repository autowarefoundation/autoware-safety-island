// SPDX-License-Identifier: Apache-2.0
#include "si_channel.h"
#include <stdio.h>
#include <string.h>

// Concurrency invariant: s_fault and s_rx_count are written ONLY from
// si_channel_rx() (rpmsg_transport.c's si_ept_cb, poll-task context) and
// read from other tasks (the heartbeat task today; the control task in the
// next task). That single-writer/multiple-reader split, combined with this
// being a single-core target, is what makes a plain read/write of an
// aligned int/unsigned safe without a lock -- NOT the `volatile` qualifier,
// which only tells the compiler not to cache the value in a register across
// calls and gives no atomicity guarantee by itself. If a second writer is
// ever added, this reasoning no longer holds and a real lock is needed.
static volatile int s_fault;
static volatile unsigned s_rx_count;

static int eq(const char *s, unsigned len, const char *lit)
{
    unsigned n = (unsigned)strlen(lit);
    if (len == n + 1 && s[n] == '\n') len = n;
    return len == n && memcmp(s, lit, n) == 0;
}

si_msg_t si_channel_parse(const void *data, unsigned len)
{
    const char *s = (const char *)data;
    if (!s || len == 0) return SI_MSG_IGNORE;
    if (eq(s, len, "hello")) return SI_MSG_HELLO;
    if (eq(s, len, "fault=1")) return SI_MSG_FAULT_SET;
    if (eq(s, len, "fault=0")) return SI_MSG_FAULT_CLEAR;
    return SI_MSG_IGNORE;
}

// Compile-time backstop, not a runtime log: if this format string ever
// grows past SI_CHANNEL_HB_LINE_MAX (si_channel.h), a caller sized off that
// constant would start getting -1 back from every call, and
// si_heartbeat_task's `if (n > 0)` guard (rpmsg_transport.c) would then just
// drop the send with nothing on the console -- the heartbeat goes silent
// forever with no diagnosis. Catching the mismatch here, at the one place
// the format string is written, means it fails the build instead.
_Static_assert(SI_CHANNEL_HB_LINE_MAX >= sizeof("hb seq=4294967295 uptime_ms=4294967295 fault=1\n"),
               "SI_CHANNEL_HB_LINE_MAX must cover the worst-case "
               "si_channel_format_hb() output (both %u args at UINT32_MAX)");

int si_channel_format_hb(char *buf, unsigned cap, unsigned seq, unsigned uptime_ms, int fault)
{
    int n = snprintf(buf, cap, "hb seq=%u uptime_ms=%u fault=%d\n", seq, uptime_ms, fault ? 1 : 0);
    if (n < 0 || (unsigned)n >= cap) return -1;
    return n;
}

void si_channel_rx(const void *data, unsigned len)
{
    s_rx_count++;
    switch (si_channel_parse(data, len)) {
    case SI_MSG_FAULT_SET:   s_fault = 1; break;
    case SI_MSG_FAULT_CLEAR: s_fault = 0; break;
    default: break;
    }
}

int si_channel_fault(void) { return s_fault; }
unsigned si_channel_rx_count(void) { return s_rx_count; }
