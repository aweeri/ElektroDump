#include "core/logging.h"

#include <iostream>

#include "core/time_util.h"
#include "util/fs_util.h"
#include "util/json_util.h"
#include "util/string_util.h"

namespace ed {

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Off: return "off";
    }
    return "info";
}

bool parse_log_level(const std::string& text, LogLevel& out) {
    const std::string lower = str::to_lower(str::trim(text));
    if (lower == "trace") { out = LogLevel::Trace; return true; }
    if (lower == "debug") { out = LogLevel::Debug; return true; }
    if (lower == "info") { out = LogLevel::Info; return true; }
    if (lower == "warn" || lower == "warning") { out = LogLevel::Warn; return true; }
    if (lower == "error") { out = LogLevel::Error; return true; }
    if (lower == "off") { out = LogLevel::Off; return true; }
    return false;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    worker_ = std::thread([this] { worker_loop(); });
}

Logger::~Logger() {
    shutdown();
}

void Logger::set_level(LogLevel level) {
    level_.store(level, std::memory_order_relaxed);
}

bool Logger::set_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_.close();
    }
    const std::string parent = fs::dirname(path);
    if (!parent.empty() && parent != ".") {
        fs::mkdir_p(parent);
    }
    file_.open(path, std::ios::out | std::ios::app);
    return file_.is_open();
}

void Logger::enqueue(LogLevel level, const char* tag, std::string message) {
    if (level < level_.load(std::memory_order_relaxed) ||
        level_.load(std::memory_order_relaxed) == LogLevel::Off) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(Entry{level, tag ? tag : "", std::move(message)});
    }
    cv_.notify_one();
}

void Logger::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    // Wait until the queue is drained and the worker is not mid-write.
    cv_.wait(lock, [this] { return queue_.empty() && !busy_; });
    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
    std::cerr.flush();
}

void Logger::shutdown() {
    if (stop_.exchange(true)) {
        return;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::write_line(const std::string& line) {
    std::cerr << line << '\n';
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
}

void Logger::worker_loop() {
    while (true) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_.load()) {
                    break;
                }
                continue;
            }
            entry = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }

        const std::string ts = time::iso8601_now(true);
        std::string line;
        if (json_.load(std::memory_order_relaxed)) {
            json::Value obj(json::Value::Object{});
            obj.set("ts", json::Value(ts));
            obj.set("level", json::Value(log_level_name(entry.level)));
            obj.set("tag", json::Value(entry.tag));
            obj.set("msg", json::Value(entry.message));
            line = obj.dump(0);
        } else {
            line = ts;
            line += " [";
            line += log_level_name(entry.level);
            line += "] [";
            line += entry.tag;
            line += "] ";
            line += entry.message;
        }
        write_line(line);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
        // Wake flush() now that the queue is drained and the line is written.
        cv_.notify_all();
    }
}

LogLine::LogLine(LogLevel level, const char* tag) : level_(level), tag_(tag) {}

LogLine::~LogLine() {
    Logger::instance().enqueue(level_, tag_, stream_.str());
}

}  // namespace ed
