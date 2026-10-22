#pragma once

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace Volt {

// Elapsed wall time as a log line shows it: "420 ms", "3.18 s", "2 min 05 s".
inline std::string formatDuration(std::chrono::steady_clock::duration elapsed) {
    const double seconds = std::chrono::duration<double>(elapsed).count();
    char text[32];
    if (seconds < 1.0) {
        std::snprintf(text, sizeof(text), "%.0f ms", seconds * 1000.0);
    } else if (seconds < 60.0) {
        std::snprintf(text, sizeof(text), "%.2f s", seconds);
    } else {
        const auto whole = static_cast<long long>(seconds);
        std::snprintf(text, sizeof(text), "%lld min %02lld s", whole / 60, whole % 60);
    }
    return text;
}

inline double percentOf(std::uint64_t part, std::uint64_t whole) noexcept {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

class Stopwatch {
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch() : start_(Clock::now()) {}

    void restart() { start_ = Clock::now(); }
    Clock::duration elapsedTime() const { return Clock::now() - start_; }
    std::string elapsed() const { return formatDuration(elapsedTime()); }

private:
    Clock::time_point start_;
};

// Percentage lines for a long loop that cannot flood the execution log: at most one line per
// stepPercent of the work and per minInterval of wall time, counted from construction, so a
// loop that ends within minInterval prints nothing. The 100% line is left to the caller's own
// summary. advance() is safe from parallel bodies and costs one relaxed atomic add plus a
// compare, so call it once per chunk (range.size()) in TBB bodies rather than per item;
// serial loops use update().
class ProgressReporter {
public:
    using Clock = std::chrono::steady_clock;

    ProgressReporter(std::string label, std::uint64_t total, int stepPercent = 10,
                     Clock::duration minInterval = std::chrono::seconds(1))
        : label_(std::move(label))
        , total_(total)
        , stepPercent_(std::clamp(stepPercent, 1, 50))
        , minInterval_(minInterval)
        , start_(Clock::now())
        , nextMark_(markAfter(0))
        , lastLine_(start_.time_since_epoch().count()) {}

    void advance(std::uint64_t count = 1) noexcept {
        const std::uint64_t done = done_.fetch_add(count, std::memory_order_relaxed) + count;
        if (done >= nextMark_.load(std::memory_order_relaxed)) {
            report(done);
        }
    }

    // For serial loops: sets the absolute count, so the per-iteration cost is a plain store and
    // compare instead of advance()'s atomic read-modify-write. Call as update(i + 1).
    void update(std::uint64_t done) noexcept {
        done_.store(done, std::memory_order_relaxed);
        if (done >= nextMark_.load(std::memory_order_relaxed)) {
            report(done);
        }
    }

    std::uint64_t done() const noexcept { return done_.load(std::memory_order_relaxed); }
    std::uint64_t total() const noexcept { return total_; }
    std::string elapsed() const { return formatDuration(Clock::now() - start_); }

private:
    static constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();

    // Item count at which the step after the one `done` sits in begins.
    std::uint64_t markAfter(std::uint64_t done) const noexcept {
        if (total_ == 0) return kNever;
        const std::uint64_t step = static_cast<std::uint64_t>(stepPercent_);
        const std::uint64_t nextPercent = (done * 100 / total_ / step + 1) * step;
        if (nextPercent >= 100) return kNever;
        return (total_ * nextPercent + 99) / 100;
    }

    void report(std::uint64_t done) noexcept {
        std::uint64_t mark = nextMark_.load(std::memory_order_relaxed);
        // Several threads can cross a mark together; the one that moves it prints the step.
        if (done < mark || !nextMark_.compare_exchange_strong(mark, markAfter(done), std::memory_order_relaxed)) {
            return;
        }
        if (done >= total_) return;
        const Clock::rep now = Clock::now().time_since_epoch().count();
        if (now - lastLine_.load(std::memory_order_relaxed) < minInterval_.count()) {
            return;
        }
        lastLine_.store(now, std::memory_order_relaxed);
        spdlog::info("{}: {}% ({}/{})", label_, done * 100 / total_, done, total_);
    }

    const std::string label_;
    const std::uint64_t total_;
    const int stepPercent_;
    const Clock::duration minInterval_;
    const Clock::time_point start_;
    std::atomic<std::uint64_t> done_{0};
    std::atomic<std::uint64_t> nextMark_;
    std::atomic<Clock::rep> lastLine_;
};

}
