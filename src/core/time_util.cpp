#include "core/time_util.h"

#include <cstdio>
#include <cstring>

namespace ed::time {

TimePoint now_utc() {
    return Clock::now();
}

std::string iso8601(TimePoint tp, bool millis) {
    const std::time_t secs = Clock::to_time_t(tp);
    std::tm tm {};
    ::gmtime_r(&secs, &tm);
    char buf[80];
    if (millis) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tp.time_since_epoch())
                            .count() %
                        1000;
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                      tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    } else {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                      tm.tm_min, tm.tm_sec);
    }
    return buf;
}

std::string iso8601_now(bool millis) {
    return iso8601(now_utc(), millis);
}

std::string compact_utc(TimePoint tp) {
    const std::time_t secs = Clock::to_time_t(tp);
    std::tm tm {};
    ::gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    return buf;
}

std::string format_utc(std::time_t t, const char* fmt) {
    std::tm tm {};
    ::gmtime_r(&t, &tm);
    char buf[128];
    if (std::strftime(buf, sizeof(buf), fmt, &tm) == 0) {
        return {};
    }
    return buf;
}

int parse_hhmm(const std::string& text, bool* ok) {
    int hour = 0;
    int minute = 0;
    char extra = '\0';
    const int matched = std::sscanf(text.c_str(), "%d:%d%c", &hour, &minute,
                                    &extra);
    const bool valid = matched == 2 && hour >= 0 && hour <= 23 && minute >= 0 &&
                       minute <= 59;
    if (ok != nullptr) {
        *ok = valid;
    }
    return valid ? hour * 60 + minute : -1;
}

int day_of_year(std::time_t t) {
    std::tm tm {};
    ::gmtime_r(&t, &tm);
    return tm.tm_yday + 1;
}

void unix_to_ymd(std::time_t t, int& year, int& month, int& day) {
    std::tm tm {};
    ::gmtime_r(&t, &tm);
    year = tm.tm_year + 1900;
    month = tm.tm_mon + 1;
    day = tm.tm_mday;
}

}  // namespace ed::time
