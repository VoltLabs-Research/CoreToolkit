#pragma once

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

/*
 * How far the whole plugin run is, as VOLT shows it on the job: `@volt:progress <0..1> <text>`
 * lines on stderr. A run splits into weighted steps that may split again, the way OVITO's
 * TaskProgress composes its sub-steps, so the value climbs once across every pass instead of
 * restarting with each one, and it never goes back. It is off unless VOLT asks for it with
 * VOLT_PROGRESS=1, so a plugin run by hand or by an older VOLT logs what it always did.
 */
class JobProgress {
public:
    using Clock = std::chrono::steady_clock;

    static JobProgress& instance() {
        static JobProgress progress;
        return progress;
    }

    bool enabled() const noexcept { return enabled_; }

    // Splits the current step into steps of these relative weights and enters the first.
    void beginSteps(std::initializer_list<double> weights) {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        const auto [low, high] = currentRange();
        Level level{low, high - low, {0.0}, 0};
        double total = 0.0;
        for (const double weight : weights) total += std::max(0.0, weight);
        double cumulative = 0.0;
        for (const double weight : weights) {
            cumulative += std::max(0.0, weight);
            level.bounds.push_back(total > 0.0 ? cumulative / total : 1.0);
        }
        if (level.bounds.size() < 2) level.bounds.push_back(1.0);
        levels_.push_back(std::move(level));
    }

    // The current step is done; the next one of the same split starts.
    void nextStep() {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        if (levels_.empty()) return;
        Level& level = levels_.back();
        if (level.index + 2 < level.bounds.size()) ++level.index;
        publish(currentRange().first);
    }

    // The split is over, and with it the step it divided.
    void endSteps() {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        if (levels_.empty()) return;
        const Level level = levels_.back();
        levels_.pop_back();
        publish(level.base + level.span);
    }

    // How far the current step is, 0..1.
    void setFraction(double fraction) {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        const auto [low, high] = currentRange();
        publish(low + std::clamp(fraction, 0.0, 1.0) * (high - low));
    }

    // What the run is doing now; it goes out with the next value.
    void setText(std::string text) {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        text = oneLine(text);
        if (text == text_) return;
        text_ = std::move(text);
        textChanged_ = true;
        publish(reported_);
    }

    void finish() {
        if (!enabled_) return;
        std::lock_guard lock(mutex_);
        publish(1.0);
    }

private:
    struct Level {
        double base;
        double span;
        std::vector<double> bounds;
        std::size_t index;
    };

    static constexpr double kMinStep = 0.005;
    static constexpr auto kMinInterval = std::chrono::milliseconds(200);

    JobProgress() {
        const char* flag = std::getenv("VOLT_PROGRESS");
        enabled_ = flag != nullptr && std::string(flag) == "1";
    }

    static std::string oneLine(const std::string& text) {
        std::string line;
        bool space = false;
        for (const char character : text) {
            const bool isSpace = character == ' ' || character == '\t' || character == '\n' || character == '\r';
            if (isSpace) {
                space = !line.empty();
                continue;
            }
            if (space) line.push_back(' ');
            space = false;
            line.push_back(character);
        }
        return line;
    }

    std::pair<double, double> currentRange() const {
        double low = 0.0;
        double high = 1.0;
        for (const Level& level : levels_) {
            low = level.base + level.span * level.bounds[level.index];
            high = level.base + level.span * level.bounds[level.index + 1];
        }
        return {low, high};
    }

    void publish(double overall) {
        reported_ = std::max(reported_, std::clamp(overall, 0.0, 1.0));
        const auto now = Clock::now();
        const bool moved = reported_ - written_ >= kMinStep && now - writtenAt_ >= kMinInterval;
        const bool finished = reported_ >= 1.0 && written_ < 1.0;
        if (!textChanged_ && !moved && !finished) return;

        written_ = reported_;
        writtenAt_ = now;
        textChanged_ = false;
        if (text_.empty()) {
            std::fprintf(stderr, "@volt:progress %.4f\n", written_);
        } else {
            std::fprintf(stderr, "@volt:progress %.4f %s\n", written_, text_.c_str());
        }
        std::fflush(stderr);
    }

    bool enabled_ = false;
    std::mutex mutex_;
    std::vector<Level> levels_;
    std::string text_;
    bool textChanged_ = false;
    double reported_ = 0.0;
    double written_ = 0.0;
    Clock::time_point writtenAt_{};
};

// One weighted split of the current step, for a plugin with several passes:
//     ProgressSteps steps({2, 5, 1});   // neighbor lists, classification, outputs
//     ...; steps.next(); ...; steps.next(); ...
// Passes reported with ProgressReporter fill the step they run in.
class ProgressSteps {
public:
    explicit ProgressSteps(std::initializer_list<double> weights) {
        JobProgress::instance().beginSteps(weights);
    }
    ProgressSteps(const ProgressSteps&) = delete;
    ProgressSteps& operator=(const ProgressSteps&) = delete;
    ~ProgressSteps() { JobProgress::instance().endSteps(); }

    void next() { JobProgress::instance().nextStep(); }
};

// Percentage lines for a long loop that cannot flood the execution log: at most one line per
// stepPercent of the work and per minInterval of wall time, counted from construction, so a
// loop that ends within minInterval prints nothing. The 100% line is left to the caller's own
// summary. advance() is safe from parallel bodies and costs one relaxed atomic add plus a
// compare, so call it once per chunk (range.size()) in TBB bodies rather than per item;
// serial loops use update(). When VOLT reads the run's progress, the pass also fills the
// JobProgress step it runs in, under its label.
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
        , lastLine_(start_.time_since_epoch().count())
        , forwardStep_(std::max<std::uint64_t>(1, total / kForwardsPerPass))
        , nextForward_(JobProgress::instance().enabled() && total > 0 ? std::min(total, forwardStep_) : kNever) {
        if (nextForward_.load(std::memory_order_relaxed) != kNever) {
            JobProgress::instance().setText(label_);
        }
    }

    void advance(std::uint64_t count = 1) noexcept {
        const std::uint64_t done = done_.fetch_add(count, std::memory_order_relaxed) + count;
        if (done >= nextForward_.load(std::memory_order_relaxed)) {
            forward(done);
        }
        if (done >= nextMark_.load(std::memory_order_relaxed)) {
            report(done);
        }
    }

    // For serial loops: sets the absolute count, so the per-iteration cost is a plain store and
    // compare instead of advance()'s atomic read-modify-write. Call as update(i + 1).
    void update(std::uint64_t done) noexcept {
        done_.store(done, std::memory_order_relaxed);
        if (done >= nextForward_.load(std::memory_order_relaxed)) {
            forward(done);
        }
        if (done >= nextMark_.load(std::memory_order_relaxed)) {
            report(done);
        }
    }

    std::uint64_t done() const noexcept { return done_.load(std::memory_order_relaxed); }
    std::uint64_t total() const noexcept { return total_; }
    std::string elapsed() const { return formatDuration(Clock::now() - start_); }

private:
    static constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();
    // Often enough for a bar to move smoothly, rarely enough to stay off the hot loop.
    static constexpr std::uint64_t kForwardsPerPass = 200;

    // Item count at which the step after the one `done` sits in begins.
    std::uint64_t markAfter(std::uint64_t done) const noexcept {
        if (total_ == 0) return kNever;
        const std::uint64_t step = static_cast<std::uint64_t>(stepPercent_);
        const std::uint64_t nextPercent = (done * 100 / total_ / step + 1) * step;
        if (nextPercent >= 100) return kNever;
        return (total_ * nextPercent + 99) / 100;
    }

    // The thread that moves the forward mark hands the pass's fraction to JobProgress.
    void forward(std::uint64_t done) noexcept {
        std::uint64_t mark = nextForward_.load(std::memory_order_relaxed);
        const std::uint64_t next = done >= total_ ? kNever : std::min(total_, done + forwardStep_);
        if (done < mark || !nextForward_.compare_exchange_strong(mark, next, std::memory_order_relaxed)) {
            return;
        }
        try {
            JobProgress::instance().setFraction(
                static_cast<double>(std::min(done, total_)) / static_cast<double>(total_));
        } catch (...) {
        }
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
    const std::uint64_t forwardStep_;
    std::atomic<std::uint64_t> nextForward_;
};

}
