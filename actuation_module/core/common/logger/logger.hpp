#ifndef COMMON__LOGGER_LOGGER_HPP_
#define COMMON__LOGGER_LOGGER_HPP_

#include <chrono>
#include <map>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <pthread.h>

#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"

#define log_info_throttle(msg, ...) common::logger::log_info_throttle_(__FILE__, __LINE__, msg, ##__VA_ARGS__)
#define log_warn_throttle(msg, ...) common::logger::log_warn_throttle_(__FILE__, __LINE__, msg, ##__VA_ARGS__)

namespace common::logger {

inline void vprint_color_(const char * format, va_list args, const char * color) {
    // Get current time with milliseconds
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Print time in HH:MM:SS.mmm format
    char time_str[13]; 
    strftime(time_str, 9, "%H:%M:%S", localtime(&time_t_now));
    sprintf(time_str + 8, ".%03ld", ms.count());

    // Print message with time and color
    fprintf(stderr, "%s[%s] | ", color, time_str);
    vfprintf(stderr, format, args);
    fprintf(stderr, "%s\n", COLOR_RESET);
}

inline void log_success(const char * format, ...) {
    #if CONFIG_LOG_LEVEL >= 1
    va_list args;
    va_start(args, format);
    vprint_color_(format, args, COLOR_GREEN);
    va_end(args);
    #endif
}

inline void log_info(const char * format, ...) {
    #if CONFIG_LOG_LEVEL >= 1
    va_list args;
    va_start(args, format);
    vprint_color_(format, args, COLOR_RESET);
    va_end(args);
    #endif
}

inline void log_warn(const char * format, ...) {
    #if CONFIG_LOG_LEVEL >= 1
    va_list args;
    va_start(args, format);
    vprint_color_(format, args, COLOR_YELLOW);
    va_end(args);
    #endif
}

inline void log_error(const char * format, ...) {
    #if CONFIG_LOG_LEVEL >= 1
    va_list args;
    va_start(args, format);
    vprint_color_(format, args, COLOR_RED);
    va_end(args);
    #endif
}

inline void log_debug(const char * format, ...) {
    #if CONFIG_LOG_LEVEL >= 2
    va_list args;
    va_start(args, format);
    vprint_color_(format, args, COLOR_RESET);
    va_end(args);
    #endif
}

inline void log_info_throttle_(const char * file, int line, const char * format, ...)
{
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double>;

    static pthread_mutex_t mutex_info = PTHREAD_MUTEX_INITIALIZER;
    static std::map<std::pair<const char*, int>, time_point> last_print_times;

    const double interval_seconds = CONFIG_LOG_THROTTLE_RATE;
    const auto location_key = std::make_pair(file, line);
    const auto now = clock::now();
    bool should_print = false;

    pthread_mutex_lock(&mutex_info);
    auto it = last_print_times.find(location_key);
    if (it == last_print_times.end()) {
        should_print = true;
        last_print_times.emplace(location_key, now);
    } else {
        const duration time_since_last_print = now - it->second;
        if (time_since_last_print.count() >= interval_seconds) {
            should_print = true;
            it->second = now;
        }
    }
    pthread_mutex_unlock(&mutex_info);

    if (should_print) {
        char formatted_msg_buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(formatted_msg_buffer, sizeof(formatted_msg_buffer), format, args);
        va_end(args);
        log_info("%s", formatted_msg_buffer);
    }
}

inline void log_warn_throttle_(const char * file, int line, const char * format, ...)
{
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double>;

    static std::map<std::pair<const char*, int>, time_point> last_print_times_warn;
    static pthread_mutex_t mutex_warn = PTHREAD_MUTEX_INITIALIZER;

    const double interval_seconds = CONFIG_LOG_THROTTLE_RATE;
    const auto location_key = std::make_pair(file, line);
    const auto now = clock::now();
    bool should_print = false;

    pthread_mutex_lock(&mutex_warn);
    auto it = last_print_times_warn.find(location_key);

    if (it == last_print_times_warn.end()) {
        should_print = true;
        last_print_times_warn.emplace(location_key, now);
    } else {
        const duration time_since_last_print = now - it->second;
        if (time_since_last_print.count() >= interval_seconds) {
            should_print = true;
            it->second = now;
        }
    }
    pthread_mutex_unlock(&mutex_warn);

    if (should_print) {
        char formatted_msg_buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(formatted_msg_buffer, sizeof(formatted_msg_buffer), format, args);
        va_end(args);
        log_warn("%s", formatted_msg_buffer);
    }
}

} // namespace common::logger
#endif  // COMMON__LOGGER_LOGGER_HPP_
