#pragma once

#include "chenc/core/cpu/relax.hpp"
#include "chenc/core/type.hpp"

#include <atomic>
#include <bit>
#include <chrono>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace chenc::lock {
	inline static constexpr u64 wait_thread_capacity = 0x3FFF'FFFF;

	// 性能配置
	struct perf_config {
		u32 wait_threshold_ns_ = 20000; // 等待时间超过此值则强制尝试挂起
		u8 fast_test_size_ = 2;			// 快速路径的重试次数
		u32 start_sleep_ns_ = 100;		// 初始睡眠时间
	};

	// 独占锁
	template <perf_config config = perf_config{}>
	class alignas(8) mutex {
	public:
		[[nodiscard]] inline bool try_lock() noexcept {
			bool expected = false;
			return flag_.compare_exchange_strong(expected, true,
												 std::memory_order_acquire,
												 std::memory_order_relaxed);
		}
		inline void lock() noexcept {
			// 0. 快速尝试
			if (try_lock()) [[likely]] {
				return;
			} else {
				auto t1 = std::chrono::steady_clock::now();
				auto dur = std::chrono::nanoseconds(config.start_sleep_ns_);
				bool is_add_wait = false;
				// 1. 快速尝试
				for (u64 i = 1; i < config.fast_test_size_; i++) {
					if (try_lock()) [[likely]] {
						return;
					}
					if (std::chrono::steady_clock::now() - t1 > dur) {
						goto wait_code;
					}
				}
				// 2. 慢速尝试
				while (true) {
					// 1. 尝试获取
					if (try_lock()) [[likely]] {
						return;
					}
					// 2. 尝试等待
					if (std::chrono::steady_clock::now() - t1 < std::chrono::nanoseconds(config.wait_threshold_ns_)) [[likely]] {
						// 1. 指数回避
						while (std::chrono::steady_clock::now() - t1 < std::chrono::nanoseconds(config.wait_threshold_ns_) - dur) {
							cpu::relax();
						}
					} else {
					wait_code:
						// 2. 系统等待
						if (is_add_wait == false) [[likely]] {
							wait_count_.fetch_add(1, std::memory_order_relaxed);
							is_add_wait = true;
						}
						while (flag_.load(std::memory_order_relaxed) == true) {
							flag_.wait(true, std::memory_order_relaxed);
						}
						wait_count_.fetch_sub(1, std::memory_order_relaxed);
					}
					dur *= 2;
				}
			}
		}

		inline void unlock() noexcept {
			flag_.store(false, std::memory_order_release);

			// 如果有等待者，发出通知
			if (wait_count_.load(std::memory_order_relaxed) > 0) [[unlikely]] {
				flag_.notify_one();
			}
		}

	private:
		std::atomic<bool> flag_ = false;
		std::atomic<u32> wait_count_ = 0;
	};
	inline static constexpr u64 mutex_size = sizeof(mutex<>);

	// 递归锁
	template <perf_config config = perf_config{}>
	class recursive_mutex {};
	inline static constexpr u64 recursive_mutex_size = sizeof(recursive_mutex<>);

	// 读写锁
	template <perf_config config = perf_config{}>
	class shared_mutex {
	private:
		inline static constexpr i32 write_locked_bit = 1 << 31;
		inline static constexpr i32 write_pending_bit = 1 << 30;
		inline static constexpr i32 reader_mask = 0x3FFFFFFF;

		/**
		 * lock_state_ 布局:
		 * [31位]: 写锁位 | [30位]: 写者挂起位 | [0-29位]: 读者计数
		 */
		std::atomic<i32> lock_state_{0};

		// 唤醒版本号：使用独立缓存行防止伪共享
		std::atomic<u32> write_signal_{0};
		std::atomic<u32> read_signal_{0};

		// 等待线程计数
		std::atomic<u16> write_wait_count_{0};
		std::atomic<u16> read_wait_count_{0};

	public:
		// ================== 写锁 (Exclusive Lock) ==================

		[[nodiscard]] inline bool try_lock() noexcept {
			i32 expected = 0;
			return lock_state_.compare_exchange_strong(expected, write_locked_bit,
													   std::memory_order_acquire,
													   std::memory_order_relaxed);
		}

		inline void lock() noexcept {
			if (try_lock()) [[likely]]
				return;

			// 1. 设置写者挂起位（写者优先策略）
			lock_state_.fetch_or(write_pending_bit, std::memory_order_relaxed);

			auto t1 = std::chrono::steady_clock::now();
			auto dur = std::chrono::nanoseconds(config.start_sleep_ns_);
			bool is_add_wait = false;

			while (true) {
				i32 state = lock_state_.load(std::memory_order_relaxed);

				// 尝试抢占：必须没有写锁且读者为0
				if ((state & (write_locked_bit | reader_mask)) == 0) {
					if (lock_state_.compare_exchange_strong(state,
															write_locked_bit | (state & write_pending_bit),
															std::memory_order_acquire)) {
						break;
					}
				}

				auto elapsed = std::chrono::steady_clock::now() - t1;
				if (elapsed < std::chrono::nanoseconds(config.wait_threshold_ns_)) [[likely]] {
					// 指数回避逻辑：模仿你的 mutex
					auto target = t1 + std::chrono::nanoseconds(config.wait_threshold_ns_) - dur;
					while (std::chrono::steady_clock::now() < target) {
						cpu::relax();
					}
				} else {
					// 进入系统挂起路径
					if (!is_add_wait) [[likely]] {
						write_wait_count_.fetch_add(1, std::memory_order_relaxed);
						is_add_wait = true;
					}

					u32 old_sig = write_signal_.load(std::memory_order_relaxed);
					state = lock_state_.load(std::memory_order_relaxed);

					// 双重检查：如果锁依然被占用，则真正进入等待
					if ((state & (write_locked_bit | reader_mask)) != 0) {
						write_signal_.wait(old_sig, std::memory_order_relaxed);
					}
				}
				dur *= 2;
			}

			if (is_add_wait)
				write_wait_count_.fetch_sub(1, std::memory_order_relaxed);
		}

		/**
		 * @brief 统一解锁函数
		 */
		inline void unlock() noexcept {
			i32 state = lock_state_.load(std::memory_order_relaxed);

			if (state & write_locked_bit) {
				// --- 释放写锁逻辑 ---
				bool has_writer = (write_wait_count_.load(std::memory_order_relaxed) > 0);

				// 1. 释放锁。如果有写者在等，保留 pending 位
				lock_state_.store(has_writer ? write_pending_bit : 0, std::memory_order_release);

				// 2. 关键：全内存屏障。确保上面的 store 对所有 CPU 可见后，再读取 wait_count
				std::atomic_thread_fence(std::memory_order_seq_cst);

				if (has_writer) [[unlikely]] {
					write_signal_.fetch_add(1, std::memory_order_relaxed);
					write_signal_.notify_one();
				} else if (read_wait_count_.load(std::memory_order_relaxed) > 0) [[unlikely]] {
					read_signal_.fetch_add(1, std::memory_order_relaxed);
					read_signal_.notify_all();
				}
			} else {
				// --- 释放读锁逻辑 ---
				i32 prev = lock_state_.fetch_sub(1, std::memory_order_release);

				// 如果是最后一个读者，且有写者在排队
				if (((prev & reader_mask) == 1) && (prev & write_pending_bit)) {
					// 同样使用 seq_cst 思想确保唤醒不丢失
					std::atomic_thread_fence(std::memory_order_seq_cst);
					if (write_wait_count_.load(std::memory_order_relaxed) > 0) {
						write_signal_.fetch_add(1, std::memory_order_relaxed);
						write_signal_.notify_one();
					}
				}
			}
		}

		// ================== 读锁 (Shared Lock) ==================

		[[nodiscard]] inline bool try_lock_shared() noexcept {
			i32 state = lock_state_.load(std::memory_order_relaxed);
			// 写优先：只要有写锁或写者在 Pending，新读者就不能进入
			if (!(state & (write_locked_bit | write_pending_bit))) {
				if (lock_state_.compare_exchange_weak(state, state + 1,
													  std::memory_order_acquire,
													  std::memory_order_relaxed)) {
					return true;
				}
			}
			return false;
		}

		inline void lock_shared() noexcept {
			if (try_lock_shared()) [[likely]]
				return;

			auto t1 = std::chrono::steady_clock::now();
			auto dur = std::chrono::nanoseconds(config.start_sleep_ns_);
			bool is_add_wait = false;

			while (true) {
				if (try_lock_shared())
					break;

				auto elapsed = std::chrono::steady_clock::now() - t1;
				if (elapsed < std::chrono::nanoseconds(config.wait_threshold_ns_)) [[likely]] {
					auto target = t1 + std::chrono::nanoseconds(config.wait_threshold_ns_) - dur;
					while (std::chrono::steady_clock::now() < target) {
						cpu::relax();
					}
				} else {
					if (!is_add_wait) [[likely]] {
						read_wait_count_.fetch_add(1, std::memory_order_relaxed);
						is_add_wait = true;
					}

					u32 old_sig = read_signal_.load(std::memory_order_relaxed);
					i32 state = lock_state_.load(std::memory_order_relaxed);

					// 只要有写者竞争，读者就挂起
					if (state & (write_locked_bit | write_pending_bit)) {
						read_signal_.wait(old_sig, std::memory_order_relaxed);
					}
				}
				dur *= 2;
			}

			if (is_add_wait)
				read_wait_count_.fetch_sub(1, std::memory_order_relaxed);
		}

		// 显式命名的读解锁
		inline void unlock_shared() noexcept {
			unlock();
		}
	};
	inline static constexpr u64 shared_mutex_size = sizeof(shared_mutex<>);
} // namespace chenc::lock