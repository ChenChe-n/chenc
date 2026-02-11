#pragma once

#include <atomic>
#include <bit>
#include <chrono>
#include <semaphore>
#include <thread>

#include "chenc/core/cpu/relax.hpp"
#include "chenc/core/type.hpp"

namespace chenc {
	class spin_lock {
	private:
		struct alignas(8) state_t {
			u32 wait_start_; // 等待开始时间
			u16 wait_count_; // 等待数
			u8 lock_;		 // 锁状态
		};

	public:
		inline bool is_locked() const noexcept {
			return (std::bit_cast<state_t>(flag_.load(std::memory_order_relaxed)).lock_ != 0);
		}

		inline bool try_lock() noexcept {
			// 1. 先用 relaxed load 过滤，避免频繁触发昂贵的 CAS
			u64 old_val = flag_.load(std::memory_order_relaxed);
			if (std::bit_cast<state_t>(old_val).lock_ != 0)
				return false;

			state_t new_val = std::bit_cast<state_t>(old_val);
			new_val.lock_ = 1;

			// 2. CAS 成功用 acquire，失败用 relaxed
			return flag_.compare_exchange_weak(old_val, std::bit_cast<u64>(new_val),
											   std::memory_order_acquire,
											   std::memory_order_relaxed);
		}

		inline void lock() noexcept {
			if (try_lock()) [[likely]]
				return;

			u64 now_sleep_ns = 1000;
			// 标记当前线程是否已经增加了 wait_count_
			bool has_incremented_wait = false;

			while (true) {
				u64 old_val = flag_.load(std::memory_order_relaxed);
				state_t new_val = std::bit_cast<state_t>(old_val);

				// 尝试抢锁
				if (new_val.lock_ == 0) {
					state_t new_state = new_val;
					new_state.lock_ = 1;
					// 如果抢锁成功，且之前增加过计数，记得减回去
					if (has_incremented_wait) {
						if (new_state.wait_count_ > 0)
							new_state.wait_count_--;
					}
					if (flag_.compare_exchange_weak(old_val, std::bit_cast<u64>(new_state),
													std::memory_order_acquire,
													std::memory_order_relaxed))
						return;
					continue;
				}

				// --- 慢速路径 ---
				u64 wait_ns = get_and_set_wait_start_time();
				if (now_sleep_ns >= wait_ns) {
					// 只有还没增加过计数时才增加
					if (!has_incremented_wait) {
						while (true) {
							old_val = flag_.load(std::memory_order_relaxed);
							state_t next_s = std::bit_cast<state_t>(old_val);
							next_s.wait_count_++;
							if (flag_.compare_exchange_weak(old_val, std::bit_cast<u64>(next_s),
															std::memory_order_relaxed)) {
								has_incremented_wait = true;
								break;
							}
						}
					}
					// 此时 old_val 包含了最新的计数状态
					flag_.wait(old_val, std::memory_order_relaxed);
				} else {
					// 退避逻辑...
					cpu::cpu_relax();
				}
			}
		}

		inline void unlock() noexcept {
			u64 old_val = flag_.fetch_and(std::bit_cast<u64>(
											  state_t{.wait_start_ = u32(-1), .wait_count_ = u16(-1), .lock_ = 0}),
										  std::memory_order_release);

			if (std::bit_cast<state_t>(old_val).wait_count_ > 0) {
				flag_.notify_one();
			}
		}

		/**
		 * @brief 获取全局wait开始时间
		 * @param ns 0 表示不设置, 仅读取
		 */
		inline u64 get_and_set_global_wait_start_time(u64 ns = 0) noexcept {
			static std::atomic<u64> global_wait_start_time_{10'000};
			if (ns == 0) {
				return global_wait_start_time_.load();
			} else {
				global_wait_start_time_.store(ns);
				return global_wait_start_time_.load();
			}
		}

		/**
		 * @brief 获取wait开始时间
		 * @param ns 0 表示不设置, 仅读取
		 */
		inline u64 get_and_set_wait_start_time(u64 ns = 0) noexcept {
			if (ns == 0) {
				u64 tmp = flag_.load(std::memory_order_acquire);
				if (tmp == 0) {
					return get_and_set_global_wait_start_time();
				}
				return tmp;
			} else {
				flag_.fetch_and(std::bit_cast<u64>(state_t{.wait_start_ = 0, .wait_count_ = u16(-1), .lock_ = u8(-1)}));
				flag_.fetch_or(std::bit_cast<u64>(state_t{.wait_start_ = static_cast<u32>(ns), .wait_count_ = 0, .lock_ = 0}));
				return flag_.load();
			}
		}

	private:
		std::atomic<u64> flag_;
	};

	bool try_lock(spin_lock &l) noexcept {
		return l.try_lock();
	}
	void lock(spin_lock &l) noexcept { l.lock(); }
	void unlock(spin_lock &l) noexcept { l.unlock(); }

	class rw_lock {
		/**
		 * 位分配设计
		 * 预备读锁数 [0,16)
		 * 预备写锁数 [16,32)
		 * 活跃读锁数 [32,48)
		 * 活跃写锁数 [48,64)
		 */
		struct alignas(8) state_t {
			u16 prep_read__count_; // 准备获取读锁的线程数
			u16 prep_write_count_; // 准备获取写锁的线程数
			u16 acti_read__count_; // 活跃的读锁数
			u16 acti_write_count_; // 活跃的写锁数
		};
		static_assert(sizeof(state_t) == sizeof(u64));
		static_assert(std::is_trivially_copyable_v<state_t>);

		// wait_count_ 的位分配:
		// [0,16) 等待数
		// [16,64) 版本号
		static constexpr u64 WAIT_CNT_MASK = 0xFFFF;
		static constexpr u64 VER_INC = 0x1'0000;

	public:
		// --- 读锁句柄 ---
		struct read_lock_ref {
			rw_lock &parent;

			inline bool try_lock() noexcept {
				auto expected = parent.state_.load(std::memory_order_acquire);
				state_t new_state = std::bit_cast<state_t>(expected);
				// 如果有写锁，则不能获取
				if (std::bit_cast<state_t>(expected).acti_write_count_ != 0 ||
					std::bit_cast<state_t>(expected).prep_write_count_ != 0)
					return false;
				// 没有写锁，则可以获取
				new_state.acti_read__count_++;
				return parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															 std::memory_order_acq_rel,
															 std::memory_order_acquire);
			}

			inline void lock() noexcept {
				// 快速尝试获取
				if (try_lock())
					return;
				// 慢速尝试获取
				auto expected = parent.state_.load(std::memory_order_acquire);
				state_t new_state = std::bit_cast<state_t>(expected);
				bool is_wait = false;
				u64 now_sleep_ns = 1000;
				// 标记准备获取写锁
				while (true) {
					new_state.prep_read__count_++;
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire))
						break;
					new_state = std::bit_cast<state_t>(expected);
				}
				while (true) {
					// 重新加载 expected，避免长时间使用旧值导致判断错误
					expected = parent.state_.load(std::memory_order_acquire);
					new_state = std::bit_cast<state_t>(expected);

					// 如果有写锁，则不能获取
					if (std::bit_cast<state_t>(expected).acti_write_count_ != 0)
						goto pass;
					// 如果有准备写锁，则不能获取
					if (std::bit_cast<state_t>(expected).prep_write_count_ != 0)
						goto pass;
					// 没有写锁读锁，则可以获取
					new_state.acti_read__count_++;
					new_state.prep_read__count_--;
					// 尝试获取
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire)) {
						// 释放等待写锁数
						if (is_wait) {
							parent.wait_read__count_.fetch_sub(1, std::memory_order_acq_rel);
						}
						return;
					}

					// CAS 失败，回到循环
					continue;

				pass:
					// 等待
					u64 wait_ns = parent.get_and_set_wait_start_time();
					if (now_sleep_ns >= wait_ns) {
						u64 wait_expected;
						if (is_wait == false) {
							is_wait = true;
							// 先登记等待者计数 +1
							u64 old = parent.wait_read__count_.fetch_add(1, std::memory_order_acq_rel);
							wait_expected = old + 1;
						} else {
							// 已经登记过等待者了
							wait_expected = parent.wait_read__count_.load(std::memory_order_acquire);
						}

						// 在真正 wait 前，必须再次检查锁条件，避免丢失唤醒
						expected = parent.state_.load(std::memory_order_acquire);
						if (std::bit_cast<state_t>(expected).acti_write_count_ == 0 && std::bit_cast<state_t>(expected).prep_write_count_ == 0) {
							// 现在已经可以获取锁了，不需要 wait
							// 取消等待者登记，回到循环继续 CAS
							parent.wait_read__count_.fetch_sub(1, std::memory_order_acq_rel);
							is_wait = false;
							continue;
						}

						// 真正进入等待：只有当 wait_read__count_ == wait_expected 时才阻塞
						parent.wait_read__count_.wait(wait_expected, std::memory_order_acquire);

					} else {
						auto start = std::chrono::steady_clock::now();
						// 使用 chrono 高精度计时, 哪怕开销较大
						while (std::chrono::steady_clock::now() - start < std::chrono::nanoseconds(now_sleep_ns)) {
							cpu::cpu_relax();
						}
						now_sleep_ns *= 2;
					}
				}
			}

			inline void unlock() noexcept {
				auto expected = parent.state_.load(std::memory_order_acquire);
				while (true) {
					state_t new_state = std::bit_cast<state_t>(expected);
					// 未持有锁尝试释放
					if (std::bit_cast<state_t>(expected).acti_read__count_ == 0) {
						return;
					}
					// 释放读锁
					new_state.acti_read__count_--;
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire)) {
						// 优先唤醒写者
						u64 prev_w = parent.wait_write_count_.fetch_add(VER_INC, std::memory_order_acq_rel);
						if ((prev_w & WAIT_CNT_MASK) != 0) {
							parent.wait_write_count_.notify_one();
						} else {
							// 没有写等待者，则唤醒读者
							u64 prev_r = parent.wait_read__count_.fetch_add(VER_INC, std::memory_order_acq_rel);
							if ((prev_r & WAIT_CNT_MASK) != 0) {
								parent.wait_read__count_.notify_all();
							}
						}
						return;
					}
				}
			}
		};

		// --- 写锁句柄 ---
		struct write_lock_ref {
			rw_lock &parent;

			inline bool try_lock() noexcept {
				auto expected = parent.state_.load(std::memory_order_acquire);
				state_t new_state = std::bit_cast<state_t>(expected);
				// 如果有读锁，则不能获取
				if (std::bit_cast<state_t>(expected).acti_read__count_ != 0 ||
					std::bit_cast<state_t>(expected).prep_read__count_ != 0)
					return false;
				// 如果有写锁，则不能获取
				if (std::bit_cast<state_t>(expected).acti_write_count_ != 0 ||
					std::bit_cast<state_t>(expected).prep_write_count_ != 0)
					return false;
				// 没有写锁读锁，则可以获取
				new_state.acti_write_count_ = 1;
				return parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															 std::memory_order_acq_rel,
															 std::memory_order_acquire);
			}

			inline void lock() noexcept {
				// 快速尝试获取
				if (try_lock())
					return;
				// 慢速尝试获取
				auto expected = parent.state_.load(std::memory_order_acquire);
				state_t new_state = std::bit_cast<state_t>(expected);
				bool is_wait = false;
				u64 now_sleep_ns = 1000;
				// 标记准备获取写锁
				while (true) {
					new_state.prep_write_count_++;
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire))
						break;
					new_state = std::bit_cast<state_t>(expected);
				}
				while (true) {
					// 重新加载 expected，避免长时间使用旧值导致判断错误
					expected = parent.state_.load(std::memory_order_acquire);
					new_state = std::bit_cast<state_t>(expected);

					// 如果有读锁，则不能获取
					if (std::bit_cast<state_t>(expected).acti_read__count_ != 0)
						goto pass;
					// 如果有写锁，则不能获取
					if (std::bit_cast<state_t>(expected).acti_write_count_ != 0)
						goto pass;
					// 没有写锁读锁，则可以获取
					new_state.acti_write_count_ = 1;
					new_state.prep_write_count_--;
					// 尝试获取
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire)) {
						// 释放等待写锁数
						if (is_wait) {
							parent.wait_write_count_.fetch_sub(1, std::memory_order_acq_rel);
						}
						return;
					}

					continue;

				pass:
					// 等待
					u64 wait_ns = parent.get_and_set_wait_start_time();
					if (now_sleep_ns >= wait_ns) {
						u64 wait_expected;
						if (is_wait == false) {
							is_wait = true;
							// 先登记等待者计数 +1
							u64 old = parent.wait_write_count_.fetch_add(1, std::memory_order_acq_rel);
							wait_expected = old + 1;
						} else {
							// 已经登记过等待者了
							wait_expected = parent.wait_write_count_.load(std::memory_order_acquire);
						}

						// 在真正 wait 前，必须再次检查锁条件，避免丢失唤醒
						expected = parent.state_.load(std::memory_order_acquire);
						if (std::bit_cast<state_t>(expected).acti_read__count_ == 0 && std::bit_cast<state_t>(expected).acti_write_count_ == 0) {
							// 现在已经可以获取锁了，不需要 wait
							// 取消等待者登记，回到循环继续 CAS
							parent.wait_write_count_.fetch_sub(1, std::memory_order_acq_rel);
							is_wait = false;
							continue;
						}

						parent.wait_write_count_.wait(wait_expected, std::memory_order_acquire);

					} else {
						auto start = std::chrono::steady_clock::now();
						// 使用 chrono 高精度计时, 哪怕开销较大
						while (std::chrono::steady_clock::now() - start < std::chrono::nanoseconds(now_sleep_ns)) {
							cpu::cpu_relax();
						}
						now_sleep_ns *= 2;
					}
				}
			}

			inline void unlock() noexcept {
				auto expected = parent.state_.load(std::memory_order_acquire);
				while (true) {
					state_t new_state = std::bit_cast<state_t>(expected);
					// 未持有锁尝试释放
					if (std::bit_cast<state_t>(expected).acti_write_count_ == 0) {
						return;
					}
					// 释放写锁
					new_state.acti_write_count_ = 0;
					if (parent.state_.compare_exchange_strong(expected, std::bit_cast<u64>(new_state),
															  std::memory_order_acq_rel,
															  std::memory_order_acquire)) {
						// 优先唤醒写者
						u64 prev_w = parent.wait_write_count_.fetch_add(VER_INC, std::memory_order_acq_rel);
						if ((prev_w & WAIT_CNT_MASK) != 0) {
							parent.wait_write_count_.notify_one();
						} else {
							// 没有写等待者，则唤醒读者
							u64 prev_r = parent.wait_read__count_.fetch_add(VER_INC, std::memory_order_acq_rel);
							if ((prev_r & WAIT_CNT_MASK) != 0) {
								parent.wait_read__count_.notify_all();
							}
						}
						return;
					}
				}
			}
		};

		// --- 外部接口 ---
		inline read_lock_ref get_read_lock() noexcept { return {*this}; }
		inline write_lock_ref get_write_lock() noexcept { return {*this}; }

		/**
		 * @brief 获取全局wait开始时间
		 * @param ns 0 表示不设置, 仅读取
		 */
		inline u64 get_and_set_global_wait_start_time(u64 ns = 0) noexcept {
			static std::atomic<u64> global_wait_start_time_{10'000};
			if (ns == 0) {
				return global_wait_start_time_.load(std::memory_order_acquire);
			} else {
				global_wait_start_time_.store(ns, std::memory_order_release);
				return global_wait_start_time_.load(std::memory_order_acquire);
			}
		}

		/**
		 * @brief 获取wait开始时间
		 * @param ns 0 表示不设置, 仅读取
		 */
		inline u64 get_and_set_wait_start_time(u64 ns = 0) noexcept {
			if (ns == 0) {
				u64 tmp = wait_start_time_.load(std::memory_order_acquire);
				if (tmp == 0) {
					return get_and_set_global_wait_start_time();
				}
				return tmp;
			} else {
				wait_start_time_.store(ns, std::memory_order_release);
				return wait_start_time_.load(std::memory_order_acquire);
			}
		}

	private:
		//
		alignas(std::hardware_destructive_interference_size) std::atomic<u64> state_{};				// 锁状态
																									// [ 0,16) 等待数 [16,64) 版本号
		alignas(std::hardware_destructive_interference_size) std::atomic<u64> wait_read__count_{0}; // 等待读锁数
																									// [ 0,16) 等待数 [16,64) 版本号
		alignas(std::hardware_destructive_interference_size) std::atomic<u64> wait_write_count_{0}; // 等待写锁数
																									//
		alignas(std::hardware_destructive_interference_size) std::atomic<u64> wait_start_time_{0};	// wait开始时间
	};

	// void rlock(rw_lock &rw) noexcept {
	// 	rw.get_read_lock().lock();
	// }
	// void runlock(rw_lock &rw) noexcept {
	// 	rw.get_read_lock().unlock();
	// }
	// void wlock(rw_lock &rw) noexcept {
	// 	rw.get_write_lock().lock();
	// }
	// void wunlock(rw_lock &rw) noexcept {
	// 	rw.get_write_lock().unlock();
	// }

} // namespace chenc
