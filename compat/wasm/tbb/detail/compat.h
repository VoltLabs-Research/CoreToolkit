#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace tbb {

template<typename Value>
class blocked_range {
    Value begin_;
    Value end_;
    std::size_t grainsize_;

public:
    blocked_range(Value begin, Value end, std::size_t grainsize = 1)
        : begin_(begin), end_(end), grainsize_(grainsize) {}

    Value begin() const { return begin_; }
    Value end() const { return end_; }
    std::size_t grainsize() const { return grainsize_; }
    std::size_t size() const { return static_cast<std::size_t>(end_ - begin_); }
    bool empty() const { return !(begin_ < end_); }
};

template<typename Range, typename Body>
void parallel_for(const Range& range, const Body& body) {
    body(range);
}

template<typename Index, typename Body>
void parallel_for(Index first, Index last, const Body& body) {
    for(Index i = first; i < last; ++i) {
        body(i);
    }
}

template<typename Index, typename Body>
void parallel_for(Index first, Index last, Index, const Body& body) {
    for(Index i = first; i < last; ++i) {
        body(i);
    }
}

template<typename Range, typename Body, typename Partitioner>
void parallel_for(const Range& range, const Body& body, const Partitioner&) {
    body(range);
}

template<typename Iterator, typename Body>
void parallel_for_each(Iterator first, Iterator last, const Body& body) {
    for(; first != last; ++first) {
        body(*first);
    }
}

template<typename Range, typename Value, typename Func, typename Reduction>
Value parallel_reduce(const Range& range, const Value& identity, const Func& func, const Reduction&) {
    return func(range, identity);
}

template<typename Iterator>
void parallel_sort(Iterator first, Iterator last) {
    std::sort(first, last);
}

template<typename Iterator, typename Compare>
void parallel_sort(Iterator first, Iterator last, Compare compare) {
    std::sort(first, last, compare);
}

struct auto_partitioner {};

class spin_mutex {
    mutable std::mutex mutex_;

public:
    class scoped_lock {
        std::unique_lock<std::mutex> lock_;

    public:
        scoped_lock() = default;
        explicit scoped_lock(spin_mutex& mutex) : lock_(mutex.mutex_) {}
        void acquire(spin_mutex& mutex) { lock_ = std::unique_lock<std::mutex>(mutex.mutex_); }
        void release() { if(lock_.owns_lock()) lock_.unlock(); }
    };
};

template<typename T>
class enumerable_thread_specific {
    T value_;

public:
    enumerable_thread_specific() : value_() {}

    template<typename Factory>
    explicit enumerable_thread_specific(Factory&& factory) : value_(factory()) {}

    T& local() { return value_; }
    const T& local() const { return value_; }

    T* begin() { return &value_; }
    T* end() { return &value_ + 1; }
    const T* begin() const { return &value_; }
    const T* end() const { return &value_ + 1; }

    template<typename Func>
    void combine_each(Func&& func) {
        func(value_);
    }
};

template<typename T>
class concurrent_vector : public std::vector<T> {
public:
    using std::vector<T>::vector;
};

class global_control {
public:
    enum parameter { max_allowed_parallelism };
    global_control(parameter, std::size_t) {}
};

using numa_node_id = int;

namespace info {
    inline int default_concurrency() {
        const unsigned int n = std::thread::hardware_concurrency();
        return n > 0 ? static_cast<int>(n) : 1;
    }
    inline std::vector<numa_node_id> numa_nodes() { return {0}; }
}

class task_arena {
public:
    struct constraints {
        int numa_id = 0;
    };

    task_arena() = default;
    explicit task_arena(constraints) {}
    void initialize() {}
    int max_concurrency() const { return 1; }

    template<typename Func>
    void execute(Func&& func) {
        func();
    }
};

}

namespace oneapi {
namespace tbb {
    using ::tbb::blocked_range;
    using ::tbb::parallel_for;
    using ::tbb::parallel_for_each;
    using ::tbb::parallel_reduce;
    using ::tbb::parallel_sort;
    using ::tbb::auto_partitioner;
    using ::tbb::spin_mutex;
    using ::tbb::enumerable_thread_specific;
    using ::tbb::concurrent_vector;
    using ::tbb::global_control;
    using ::tbb::task_arena;
    namespace info = ::tbb::info;
}
}
