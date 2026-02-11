#include "chenc/thread/atomic_queue.hpp" 

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>
#include <optional>

using namespace chenc::thread;

struct test_bench {
    static constexpr int duration_seconds = 1;
    static constexpr int producer_ratio = 50;

    // 存储 uint64_t 值，初始容量 1024
    atomic_queue<uint64_t> queue{ uint64_t(1) << 27 };

    struct metrics {
        uint64_t push_ops = 0;
        uint64_t pop_ops = 0;
        uint64_t pop_empty = 0; 
        uint64_t total_latency_ns = 0;
        uint64_t sample_count = 0;
    };
};

void benchmark_thread(int id, test_bench &bench, std::atomic<bool> &stop, test_bench::metrics &m) {
    std::mt19937 gen(id + (uint32_t)std::time(nullptr));
    std::uniform_int_distribution<> dist(1, 100);

    // 因为是值存储，我们直接生成和处理数值，不需要任何 local_pool 
    uint64_t value_to_push = id * 1000000; 

    while (!stop.load(std::memory_order_acquire)) {
        int role_chance = dist(gen);
        // 采样频率：10000 次操作采样一次耗时
        bool should_sample = (m.push_ops + m.pop_ops) % 10000 == 0;

        if (role_chance <= test_bench::producer_ratio) {
            // --- PUSH 测试 ---
            auto start = should_sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
            
            // 直接传递右值
            bench.queue.push(std::move(value_to_push));
            value_to_push++; // 模拟新数据的产生

            if (should_sample) {
                auto end = std::chrono::steady_clock::now();
                m.total_latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                m.sample_count++;
            }
            m.push_ops++;
        } else {
            // --- POP 测试 ---
            auto start = should_sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();

            // 重构后的 pop 返回 std::optional<uint64_t>
            auto result = bench.queue.pop();

            if (should_sample) {
                auto end = std::chrono::steady_clock::now();
                m.total_latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                m.sample_count++;
            }

            if (result.has_value()) {
                m.pop_ops++;
                // 可以在这里处理 result.value()
            } else {
                m.pop_empty++;
            }
        }
    }
}

int main() {
    test_bench bench;
    unsigned int n_threads = std::thread::hardware_concurrency() ;
    std::vector<std::thread> threads;
    std::vector<test_bench::metrics> thread_metrics(n_threads);
    std::atomic<bool> stop{false};

    std::cout << "--- 无锁队列 (Atomic Queue) 值存储性能测试 ---" << std::endl;
    std::cout << std::format("线程数: {}, 生产/消费比: {}%/{}%, 测试时长: {}s\n",
                             n_threads, test_bench::producer_ratio, 100 - test_bench::producer_ratio, test_bench::duration_seconds);

    for (unsigned int i = 0; i < n_threads; ++i) {
        threads.emplace_back(benchmark_thread, i, std::ref(bench), std::ref(stop), std::ref(thread_metrics[i]));
    }

    std::this_thread::sleep_for(std::chrono::seconds(test_bench::duration_seconds));
    stop.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    uint64_t total_push = 0, total_pop = 0, total_empty = 0, total_samples = 0, sum_latency = 0;
    for (const auto &m : thread_metrics) {
        total_push += m.push_ops;
        total_pop += m.pop_ops;
        total_empty += m.pop_empty;
        total_samples += m.sample_count;
        sum_latency += m.total_latency_ns;
    }

    double avg_lat = total_samples > 0 ? (double)sum_latency / total_samples : 0;
    double total_ops = total_push + total_pop;
    double mops = total_ops / test_bench::duration_seconds / 1e6;

    std::cout << "--- 性能总结 ---" << std::endl;
    std::cout << std::format("总吞吐量: {:.2f} M ops/s\n", mops);
    std::cout << std::format("平均延迟: {:.2f} ns\n", avg_lat);
    std::cout << std::format("成功 Push: {}\n", total_push);
    std::cout << std::format("成功 Pop:  {}\n", total_pop);
    std::cout << std::format("空队等待:  {}\n", total_empty);

    uint64_t remaining = bench.queue.size();
    bool passed = (total_push == total_pop + remaining);
    std::cout << std::format("校验: {} (推入 {} = 弹出 {} + 剩余 {})\n",
                             (passed ? "PASS" : "FAIL"),
                             total_push, total_pop, remaining);

    return passed ? 0 : 1;
}