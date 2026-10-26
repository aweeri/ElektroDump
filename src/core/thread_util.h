// ElektroDump — Linux thread naming and scheduling helpers.
#pragma once

#include <string>

namespace ed::thread {

// Set the current thread's name (truncated to 15 chars by the kernel).
void set_name(const std::string& name);

// SCHED_FIFO with the given priority. Requires CAP_SYS_NICE / LimitRTPRIO.
bool set_realtime_priority(int priority);

// SCHED_IDLE — for image work that must never starve capture.
bool set_idle_priority();

// nice(2) for the current thread.
bool set_nice(int nice_value);

}  // namespace ed::thread
