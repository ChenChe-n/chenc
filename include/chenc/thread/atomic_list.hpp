// #pragma once

// #include "chenc/cpu.hpp"
// #include "chenc/type_int.hpp"

// #include <atomic>
// #include <chrono>
// #include <memory>
// #include <thread>
// #include <vector>

// namespace chenc {
// 	class spin_lock {
// 	public:
// 		inline spin_lock() noexcept
// 			: flag_(ATOMIC_FLAG_INIT) {}

// 		inline ~spin_lock() noexcept {}

// 		inline bool try_lock() noexcept {
// 			// 使用 acquire 语义确保锁后的操作不会重排到锁前
// 			return !flag_.test_and_set(std::memory_order_acquire);
// 		}

// 		inline bool is_locked() const noexcept {
// 			return flag_.test(std::memory_order_relaxed);
// 		}

// 		inline void wait() const noexcept {
// 			flag_.wait(true, std::memory_order_relaxed);
// 		}

// 		inline void lock() noexcept {
// 			// 1. 第一阶段：TTAS (Test and Test-and-set) 自旋
// 			for (u64 i = 1; i <= 256; i *= 2) {
// 				if (!flag_.test(std::memory_order_relaxed)) {
// 					if (try_lock()) {
// 						return;
// 					}
// 				}
// 				// 提示 CPU 正在自旋，降低功耗/提升超线程性能
// 				for (u64 j = 0; j < i; ++j) {
// 					cpu_relax();
// 				}
// 			}

// 			// 2. 第二阶段：如果还是拿不到，利用 C++20 的 wait 机制进入内核挂起/被动等待
// 			while (flag_.test_and_set(std::memory_order_acquire)) {
// 				// wait 会在被 notify 之前尽可能减少 CPU 占用
// 				wait();
// 			}
// 		}

// 		inline void unlock(bool notify_all = false) noexcept {
// 			flag_.clear(std::memory_order_release);
// 			if (notify_all) {
// 				flag_.notify_all(); // 必须配合 wait 使用
// 			} else {
// 				flag_.notify_one(); // 必须配合 wait 使用
// 			}
// 		}

// 		std::atomic_flag flag_;
// 	};

// } // namespace chenc
// namespace chenc {
// 	template <typename T>
// 	class atomic_list {
// 	public:
// 		struct node_t {
// 			// 确保内存对齐且大小足够容纳 T
// 			alignas(T) std::byte storage_[sizeof(T)];
// 			std::atomic<u64> sequence_;

// 			// 辅助方法：获取 T 的引用
// 			inline T &value() {
// 				return *reinterpret_cast<T *>(storage_);
// 			}

// 			// 辅助方法：手动构造对象
// 			inline void construct(T &&val) {
// 				new (storage_) T(std::move(val));
// 			}

// 			// 辅助方法：手动销毁对象
// 			inline void destroy() {
// 				reinterpret_cast<T *>(storage_)->~T();
// 			}
// 		};

// 		inline atomic_list(u64 initial_capacity = 1024)
// 			: size_(0),
// 			  enqueue_pos_(0),
// 			  dequeue_pos_(0),
// 			  pop_thread_(0),
// 			  push_thread_(0),
// 			  map_size_(initial_capacity),
// 			  resizing_(false) {

// 			node_t *map = new node_t[initial_capacity];
// 			for (u64 i = 0; i < initial_capacity; ++i) {
// 				map[i].sequence_.store(i, std::memory_order_relaxed);
// 			}
// 			value_map_.store(map, std::memory_order_relaxed);
// 		}

// 		inline ~atomic_list() {
// 			node_t *map = value_map_.load(std::memory_order_relaxed);
// 			u64 cap = map_size_.load(std::memory_order_relaxed);
// 			u64 start = dequeue_pos_.load(std::memory_order_relaxed);
// 			u64 end = enqueue_pos_.load(std::memory_order_relaxed);

// 			// 手动销毁还在队列里的对象
// 			for (u64 i = start; i < end; ++i) {
// 				map[i % cap].destroy();
// 			}
// 			delete[] map;
// 		}

// 		// 入队
// 		inline void push(T &&value) {
// 			while (true) {
// 				enter_region(push_thread_);

// 				u64 pos = enqueue_pos_.load(std::memory_order_relaxed);
// 				node_t *current_map = value_map_.load(std::memory_order_acquire);
// 				u64 cap = map_size_.load(std::memory_order_acquire);

// 				node_t *node = &current_map[pos % cap];
// 				u64 seq = node->sequence_.load(std::memory_order_acquire);
// 				intptr_t diff = (intptr_t)seq - (intptr_t)pos;

// 				if (diff == 0) {
// 					if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {

// 						node->construct(std::move(value));
// 						node->sequence_.store(pos + 1, std::memory_order_release);
// 						size_.fetch_add(1, std::memory_order_relaxed);

// 						leave_region(push_thread_);

// 						// [Requirement] 执行成功清空等待计数
// 						sleep_ns_ = 0;
// 						return;
// 					}
// 				} else if (diff < 0) {
// 					leave_region(push_thread_);
// 					if (!rebuffer(cap * 2)) {
// 						recapacity_lock_.wait(); // 等待其他线程完成扩容
// 						continue;
// 					}
// 					continue;
// 				}

// 				leave_region(push_thread_);

// 				// [Requirement] 失败则指数退避
// 				backoff();
// 			}
// 		}

// 		inline std::optional<T> pop() {
// 			while (true) {
// 				enter_region(pop_thread_);

// 				u64 pos = dequeue_pos_.load(std::memory_order_relaxed);
// 				node_t *current_map = value_map_.load(std::memory_order_acquire);
// 				u64 cap = map_size_.load(std::memory_order_acquire);

// 				node_t *node = &current_map[pos % cap];
// 				u64 seq = node->sequence_.load(std::memory_order_acquire);
// 				intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

// 				if (diff == 0) {
// 					if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
// 						T val(std::move(node->value()));
// 						node->destroy();

// 						std::optional<T> result(std::move(val));
// 						node->sequence_.store(pos + cap, std::memory_order_release);
// 						size_.fetch_sub(1, std::memory_order_relaxed);

// 						leave_region(pop_thread_);

// 						// [Requirement] 执行成功清空等待计数
// 						sleep_ns_ = 0;
// 						return result;
// 					}
// 				} else if (diff < 0) {
// 					// 队列为空
// 					leave_region(pop_thread_);

// 					// [Requirement] 虽然是空返回，但也算一种成功退出的状态（或者根据业务逻辑，如果希望空转等待数据则不应清空，这里假设返回即结束）
// 					sleep_ns_ = 0;
// 					return std::nullopt;
// 				}

// 				leave_region(pop_thread_);

// 				// [Requirement] 失败则指数退避
// 				backoff();
// 			}
// 		}

// 		inline void clear() noexcept {
// 			recapacity_lock_.lock();

// 			resizing_.store(true, std::memory_order_release);
// 			wait_for_quiescent();

// 			u64 start = dequeue_pos_.load(std::memory_order_relaxed);
// 			u64 end = enqueue_pos_.load(std::memory_order_relaxed);
// 			u64 cap = map_size_.load(std::memory_order_relaxed);
// 			node_t *map = value_map_.load(std::memory_order_relaxed);

// 			for (u64 i = start; i < end; ++i) {
// 				u64 idx = i % cap;
// 				if constexpr (!std::is_trivially_destructible_v<T>) {
// 					map[idx].value_ = T();
// 				}
// 				map[idx].sequence_.store(i, std::memory_order_release);
// 			}

// 			size_.store(0, std::memory_order_relaxed);
// 			dequeue_pos_.store(end, std::memory_order_relaxed);

// 			std::atomic_thread_fence(std::memory_order_seq_cst);
// 			resizing_.store(false, std::memory_order_release);
// 			recapacity_lock_.unlock();

// 			// [Requirement] 操作完成清空
// 			sleep_ns_ = 0;
// 		}

// 		inline void shrink_to_fit(u64 target_capacity = 0) {
// 			recapacity_lock_.lock();
// 			u64 current_elements = size_.load(std::memory_order_relaxed);
// 			u64 current_cap = map_size_.load(std::memory_order_relaxed);

// 			if (target_capacity == 0)
// 				target_capacity = current_elements > 1024 ? current_elements : 1024;

// 			if (target_capacity != current_cap) {
// 				rebuffer_internal(target_capacity);
// 			}
// 			recapacity_lock_.unlock(true);
// 			sleep_ns_ = 0;
// 		}

// 		inline bool rebuffer(u64 new_size) {
// 			if (recapacity_lock_.try_lock()) {
// 				if (new_size > map_size_.load(std::memory_order_acquire)) {
// 					rebuffer_internal(new_size);
// 				}
// 				recapacity_lock_.unlock(true);
// 				return true;
// 			}
// 			return false;
// 		}

// 		inline u64 size() const noexcept { return size_.load(); }
// 		inline u64 capacity() const noexcept { return map_size_.load(); }

// 	private:
// 		// [Requirement] 使用 thread_local static 变量
// 		inline static thread_local u64 sleep_ns_ = 0;

// 		inline static void backoff() noexcept {
// 			if (sleep_ns_ == 0) {
// 				sleep_ns_ = 500; // 初始等待时间
// 			} else {
// 				// 指数增长，设置 10ms 上限
// 				sleep_ns_ = std::min(sleep_ns_ << 1, (u64)1'000'000);
// 			}

// 			// 10us 以下使用 spin
// 			if (sleep_ns_ < 10'000) {
// 				auto end_time = std::chrono::high_resolution_clock::now() +
// 								std::chrono::nanoseconds(sleep_ns_);
// 				while (std::chrono::high_resolution_clock::now() < end_time) {
// 					cpu_relax();
// 				}
// 			} else {
// 				std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns_));
// 			}
// 		}

// 		inline void enter_region(std::atomic<u64> &counter) {
// 			while (true) {
// 				counter.fetch_add(1, std::memory_order_relaxed);
// 				if (!resizing_.load(std::memory_order_acquire)) {
// 					sleep_ns_ = 0;
// 					return;
// 				}
// 				counter.fetch_sub(1, std::memory_order_relaxed);
// 				backoff();
// 			}
// 		}

// 		inline void leave_region(std::atomic<u64> &counter) {
// 			counter.fetch_sub(1, std::memory_order_release);
// 		}

// 		inline void wait_for_quiescent() {
// 			while (push_thread_.load(std::memory_order_acquire) > 0 ||
// 				   pop_thread_.load(std::memory_order_acquire) > 0) {
// 				backoff();
// 			}
// 			sleep_ns_ = 0;
// 		}

// 		inline void rebuffer_internal(u64 new_size) {
// 			resizing_.store(true, std::memory_order_release);
// 			wait_for_quiescent();

// 			u64 old_cap = map_size_.load(std::memory_order_relaxed);
// 			node_t *old_map = value_map_.load(std::memory_order_relaxed);
// 			node_t *new_map = new node_t[new_size];

// 			for (u64 i = 0; i < new_size; ++i) {
// 				new_map[i].sequence_.store(i, std::memory_order_relaxed);
// 			}

// 			u64 start = dequeue_pos_.load(std::memory_order_relaxed);
// 			u64 end = enqueue_pos_.load(std::memory_order_relaxed);

// 			for (u64 i = start; i < end; ++i) {
// 				u64 old_idx = i % old_cap;
// 				u64 new_idx = i % new_size;

// 				// 1. 在新位置构造对象
// 				new_map[new_idx].construct(std::move(old_map[old_idx].value()));
// 				// 2. 手动销毁旧位置的对象
// 				old_map[old_idx].destroy();

// 				new_map[new_idx].sequence_.store(i + 1, std::memory_order_relaxed);
// 			}

// 			value_map_.store(new_map, std::memory_order_release);
// 			map_size_.store(new_size, std::memory_order_release);

// 			// 此时 old_map 里的所有 T 对象都已经手动销毁了
// 			// delete[] 只会回收 node_t 的内存，不会再次调用 T 的析构函数
// 			delete[] old_map;

// 			std::atomic_thread_fence(std::memory_order_seq_cst);
// 			resizing_.store(false, std::memory_order_release);
// 		}

// 		alignas(64) std::atomic<u64> size_;
// 		alignas(64) spin_lock recapacity_lock_;
// 		alignas(64) std::atomic<u64> enqueue_pos_;
// 		alignas(64) std::atomic<u64> dequeue_pos_;
// 		alignas(64) std::atomic<u64> pop_thread_;
// 		alignas(64) std::atomic<u64> push_thread_;
// 		alignas(64) std::atomic<bool> resizing_;

// 		alignas(64) std::atomic<node_t *> value_map_;
// 		alignas(64) std::atomic<u64> map_size_;
// 	};
// } // namespace chenc