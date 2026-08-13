// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// TASK 6 STUB -- replaced in Task 7 with the real OpenAMP/RPMsg endpoint.
//
// Deliberately fails both entry points rather than silently no-opping.
// A weak-symbol stub that gets silently displaced if the real definition
// fails to link is exactly the Task 3 mistake this plan removes (see
// freertos_main.cpp's git history for the lwip_bring_up_blocking() weak
// stub this task deleted outright). This is a strong, always-failing
// definition instead: if Task 7's real rpmsg_transport.c is ever dropped
// from the link (wrong source list, a stale object left in the build dir),
// the linker still resolves rpmsg_transport_init()/_send() to THIS
// translation unit's strong symbols rather than silently picking a weak
// no-op, so lwip_bring_up_blocking() reports failure loudly instead of
// reporting a network that does not exist.
#include <stdio.h>

#include "rpmsg_transport.h"

int rpmsg_transport_init(void) {
    printf("rpmsg_transport_init: TASK 6 STUB, no RPMsg transport (Task 7 replaces this)\n");
    return -1;
}

int rpmsg_transport_send(const void *buf, unsigned len) {
    (void)buf;
    (void)len;
    return -1;
}
