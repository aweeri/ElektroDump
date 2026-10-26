// ElektroDump — UTC time helpers.
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

namespace ed::time {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

TimePoint now_utc();

// "2026-10-26T12:00:00.123Z" (millis) or "2026-10-26T12:00:00Z".
std::string iso8601(TimePoint tp, bool millis = true);
std::string iso8601_now(bool millis = true);

// "20261026T120000Z" — used for product ids / filenames.
std::string compact_utc(TimePoint tp);

std::string format_utc(std::time_t t, const char* fmt);

// Parse "HH:MM" into minutes-of-day. Returns -1 on error.
int parse_hhmm(const std::string& text, bool* ok = nullptr);

int day_of_year(std::time_t t);
void unix_to_ymd(std::time_t t, int& year, int& month, int& day);

}  // namespace ed::time
