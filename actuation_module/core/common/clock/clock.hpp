#ifndef COMMON__CLOCK_HPP_
#define COMMON__CLOCK_HPP_

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <zephyr/posix/time.h>
#include <zephyr/net/sntp.h>

#include "common/logger/logger.hpp"
using namespace common::logger;

//Msgs
#include "Time.h"
#include "Duration.h"
using TimeMsg = builtin_interfaces_msg_Time;
using DurationMsg = builtin_interfaces_msg_Duration;

class Clock {
public:

    /**
     * @brief Initialize the clock via SNTP
     * @return 0 on success, negative value on failure
     */
    static int init_clock_via_sntp(void) {
        struct sntp_time ts;
        struct timespec tspec;
        int res = sntp_simple("time.nist.gov",
                    10000, &ts);

        if (res < 0) {
            log_error("Cannot set time using SNTP\n");
            return res;
        }

        tspec.tv_sec = ts.seconds;
        tspec.tv_nsec = ((uint64_t)ts.fraction * (1000 * 1000 * 1000)) >> 32;
        res = clock_settime(CLOCK_REALTIME, &tspec);
        if (res < 0) {
            log_error("Cannot set REALTIME time using SNTP\n");
            return res;
        }

        sleep(1);
        log_info("Time set using SNTP: %s\n", ctime(&tspec.tv_sec));
        return 0;
    }

    /**
     * @brief Get current time in seconds
     * @return current time in seconds
     */
    static double now() {
        auto time_point = std::chrono::system_clock::now();
        return std::chrono::duration<double>(time_point.time_since_epoch()).count();
    }

    /**
     * @brief Convert time in seconds to ROS2 time
     * @param time: time in seconds
     * @return ROS2 Time Message
     */
    static TimeMsg toRosTime(const double time) {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(time));
        constexpr int64_t nano_per_sec = 1'000'000'000;
        
        TimeMsg ros_time;
        const auto count = nanoseconds.count();
        ros_time.sec = static_cast<int32_t>(count / nano_per_sec);
        ros_time.nanosec = static_cast<uint32_t>(count % nano_per_sec);
        
        return ros_time;
    }

    /**
     * @brief Convert time in seconds to ROS2 duration
     * @param time: time in seconds
     * @return ROS2 Duration Message
     */
    static DurationMsg toRosDuration(const double time) {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(time));
        constexpr int64_t nano_per_sec = 1'000'000'000;
        
        DurationMsg ros_duration;
        const auto count = nanoseconds.count();
        ros_duration.sec = static_cast<int32_t>(count / nano_per_sec);
        ros_duration.nanosec = static_cast<uint32_t>(count % nano_per_sec);
        
        return ros_duration;
    }

    /**
     * @brief Convert ROS2 time to time in seconds
     * @param ros_time: ROS2 Time Message
     * @return time in seconds
     */
    static double toDouble(const TimeMsg& ros_time) {
        const int64_t total_ns = static_cast<int64_t>(ros_time.sec) * 1'000'000'000LL 
                                + ros_time.nanosec;
        return std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::nanoseconds(total_ns)
        ).count();
    }
private:
    Clock() = default;
    ~Clock() = default;
};

#endif  // COMMON__CLOCK_HPP_
