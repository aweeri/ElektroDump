// ElektroDump — leveled, asynchronous logger.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace ed {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error, Off };

const char* log_level_name(LogLevel level);
bool parse_log_level(const std::string& text, LogLevel& out);

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const { return level_.load(std::memory_order_relaxed); }
    void set_json(bool enabled) { json_.store(enabled, std::memory_order_relaxed); }

    // Open (append) a log file, creating parent directories; false on failure
    // (logging to stderr continues regardless).
    bool set_file(const std::string& path);

    void enqueue(LogLevel level, const char* tag, std::string message);
    void flush();
    void shutdown();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Entry {
        LogLevel level;
        std::string tag;
        std::string message;
    };

    void worker_loop();
    void write_line(const std::string& line);

    std::atomic<LogLevel> level_{LogLevel::Info};
    std::atomic<bool> json_{false};
    std::atomic<bool> stop_{false};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Entry> queue_;
    bool busy_ = false;  // worker is writing an entry (guarded by mutex_)
    std::thread worker_;

    std::mutex file_mutex_;
    std::ofstream file_;
};

// Stream-style log line: ED_LOG(Info, "cli") << "hello " << 42;
class LogLine {
public:
    LogLine(LogLevel level, const char* tag);
    ~LogLine();

    template <typename T>
    LogLine& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    const char* tag_;
    std::ostringstream stream_;
};

}  // namespace ed

#define ED_LOG(level, tag) ::ed::LogLine(::ed::LogLevel::level, tag)
