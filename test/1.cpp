#include "chenc/thread/lock.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <numeric>
#include <random>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <vector>

using namespace chenc::lock;

struct test_bench {
	static constexpr int duration_seconds = 10;
	static constexpr int write_ratio = 0;

	// spin_lock<> lock;
	// shared_mutex<> lock;
	std::shared_mutex lock;
	// std::mutex lock;
	uint64_t shared_counter = 0;

	struct metrics {
		uint64_t read_ops = 0;
		uint64_t write_ops = 0;
		uint64_t max_read_latency_ns = 0;
		uint64_t max_write_latency_ns = 0;
		uint64_t total_latency_ns = 0;
		uint64_t sample_count = 0;
	};
};

void benchmark_thread(int id, test_bench &bench, std::atomic<bool> &stop, test_bench::metrics &m) {
	std::mt19937 gen(id + std::time(nullptr));
	std::uniform_int_distribution<> dist(1, 100);

	while (!stop.load(std::memory_order_acquire)) {
		int chance = dist(gen);

		// 采样逻辑：每 1000 次操作进行一次精确计时，避免计时器本身成为瓶颈
		bool should_sample = (m.read_ops + m.write_ops) % 1000 == 0;

		if (chance <= test_bench::write_ratio) {
			auto start = should_sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();

			bench.lock.lock();
			bench.shared_counter++;
			bench.lock.unlock();

			if (should_sample) {
				auto end = std::chrono::steady_clock::now();
				uint64_t lat = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
				m.max_write_latency_ns = std::max(m.max_write_latency_ns, lat);
				m.total_latency_ns += lat;
				m.sample_count++;
			}
			m.write_ops++;
		} else {
			auto start = should_sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();

			bench.lock.lock_shared();
			// bench.lock.lock();
			volatile uint64_t val = bench.shared_counter;
			(void)val;
			bench.lock.unlock_shared();
            // bench.lock.unlock();

			if (should_sample) {
				auto end = std::chrono::steady_clock::now();
				uint64_t lat = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
				m.max_read_latency_ns = std::max(m.max_read_latency_ns, lat);
				m.total_latency_ns += lat;
				m.sample_count++;
			}
			m.read_ops++;
		}
	}
}

int main() {
	test_bench bench;
	unsigned int n_threads = std::thread::hardware_concurrency() / 2 ;
	std::vector<std::thread> threads;
	std::vector<test_bench::metrics> thread_metrics(n_threads);
	std::atomic<bool> stop{false};

	std::cout << "--- 读写锁延迟与吞吐测试 ---" << std::endl;
	std::cout << std::format("线程数: {}, 读写比: {}%/{}%, 时长: {}s\n",
							 n_threads, 100 - test_bench::write_ratio, test_bench::write_ratio, test_bench::duration_seconds);

	for (unsigned int i = 0; i < n_threads; ++i) {
		threads.emplace_back(benchmark_thread, i, std::ref(bench), std::ref(stop), std::ref(thread_metrics[i]));
	}

	std::this_thread::sleep_for(std::chrono::seconds(test_bench::duration_seconds));
	stop.store(true, std::memory_order_release);

	for (auto &t : threads)
		t.join();

	// 统计汇总
	uint64_t total_reads = 0, total_writes = 0, total_samples = 0, sum_latency = 0;
	uint64_t global_max_read_lat = 0, global_max_write_lat = 0;

	for (const auto &m : thread_metrics) {
		total_reads += m.read_ops;
		total_writes += m.write_ops;
		total_samples += m.sample_count;
		sum_latency += m.total_latency_ns;
		global_max_read_lat = std::max(global_max_read_lat, m.max_read_latency_ns);
		global_max_write_lat = std::max(global_max_write_lat, m.max_write_latency_ns);
	}

	double avg_lat = total_samples > 0 ? (double)sum_latency / total_samples : 0;
	double mops = (double)(total_reads + total_writes) / test_bench::duration_seconds / 1e6;

	std::cout << "--- 性能总结 ---" << std::endl;
	std::cout << std::format("吞吐量: {:.2f} M ops/s\n", mops);
	std::cout << std::format("平均延迟: {:.2f} ns\n", avg_lat);
	std::cout << std::format("最大读取延迟: {} ns ({:.3f} ms)\n", global_max_read_lat, global_max_read_lat / 1e6);
	std::cout << std::format("最大写入延迟: {} ns ({:.3f} ms)\n", global_max_write_lat, global_max_write_lat / 1e6);
	std::cout << std::format("校验: {}\n", (bench.shared_counter == total_writes ? "PASS" : "FAIL"));

	return 0;
}