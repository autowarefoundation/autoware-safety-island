// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__ZEPHYR__CLOCK_H_
#define PLATFORM__ZEPHYR__CLOCK_H_

#include <zephyr/posix/time.h>
#include <zephyr/net/sntp.h>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include "common/logger/logger.hpp"

static inline int platform_init_clock_via_sntp(void) {
    struct sntp_time ts;
    struct timespec tspec;
    int res = sntp_simple("time.nist.gov", 10000, &ts);

    if (res < 0) {
        common::logger::log_error("Cannot set time using SNTP\n");
        return res;
    }

    tspec.tv_sec = ts.seconds;
    tspec.tv_nsec = ((uint64_t)ts.fraction * (1000 * 1000 * 1000)) >> 32;
    res = clock_settime(CLOCK_REALTIME, &tspec);
    if (res < 0) {
        common::logger::log_error("Cannot set REALTIME time using SNTP\n");
        return res;
    }

    sleep(1);
    common::logger::log_info("Time set using SNTP: %s\n", ctime(&tspec.tv_sec));
    return 0;
}

#endif  // PLATFORM__ZEPHYR__CLOCK_H_
