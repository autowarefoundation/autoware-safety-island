// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "si_channel.h"

#ifdef NDEBUG
#error "test_si_channel.c relies on assert(); build it without NDEBUG"
#endif

int main(void)
{
    char buf[64];

    assert(si_channel_parse("hello", 5) == SI_MSG_HELLO);
    assert(si_channel_parse("hello\n", 6) == SI_MSG_HELLO);
    assert(si_channel_parse("fault=1", 7) == SI_MSG_FAULT_SET);
    assert(si_channel_parse("fault=1\n", 8) == SI_MSG_FAULT_SET);
    assert(si_channel_parse("fault=0", 7) == SI_MSG_FAULT_CLEAR);
    assert(si_channel_parse("fault=2", 7) == SI_MSG_IGNORE);
    assert(si_channel_parse("", 0) == SI_MSG_IGNORE);
    assert(si_channel_parse("fault", 5) == SI_MSG_IGNORE);
    /* No NUL terminator on the wire: a 7-byte "fault=1" followed by garbage
     * must still parse by length, never by strlen. */
    assert(si_channel_parse("fault=1XYZ", 7) == SI_MSG_FAULT_SET);

    assert(si_channel_fault() == 0);
    si_channel_rx("fault=1", 7);
    assert(si_channel_fault() == 1);
    si_channel_rx("hello", 5);          /* hello does not clear */
    assert(si_channel_fault() == 1);
    si_channel_rx("fault=0", 7);
    assert(si_channel_fault() == 0);
    assert(si_channel_rx_count() == 3);

    int n = si_channel_format_hb(buf, sizeof buf, 7, 12345, 1);
    assert(n > 0);
    assert(strcmp(buf, "hb seq=7 uptime_ms=12345 fault=1\n") == 0);
    assert(si_channel_format_hb(buf, 8, 7, 12345, 1) == -1);   /* too small */

    puts("test_si_channel: ok");
    return 0;
}
