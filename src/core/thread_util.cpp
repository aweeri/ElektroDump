#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "core/thread_util.h"

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

namespace ed::thread {

void set_name(const std::string& name) {
    const std::string truncated = name.substr(0, 15);
    ::pthread_setname_np(::pthread_self(), truncated.c_str());
}

bool set_realtime_priority(int priority) {
    struct sched_param param {};
    param.sched_priority = priority;
    return ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) == 0;
}

bool set_idle_priority() {
    struct sched_param param {};
    param.sched_priority = 0;
    return ::pthread_setschedparam(::pthread_self(), SCHED_IDLE, &param) == 0;
}

bool set_nice(int nice_value) {
    return ::setpriority(PRIO_PROCESS, 0, nice_value) == 0;
}

}  // namespace ed::thread
