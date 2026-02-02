#pragma noce

#include "chenc/type_int.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace chenc {
	class spin_lock {
	public:
		inline spin_lock() noexcept
			: flag_(ATOMIC_FLAG_INIT) {}
		inline ~spin_lock() noexcept {}

		inline bool try_lock() noexcept {
			if (flag_.test_and_set(std::memory_order_acquire)) {
				return false;
			}
			return true;
		}
		inline void lock() noexcept {
			// 快速尝试
			for (u64 i = 0; i < 4; i++) {
				if (try_lock()) {
					return;
				}
				// 微微延时, 避免强冲突
				auto dur = std::chrono::microseconds(1);
				auto t = std::chrono::steady_clock::now();
				while (std::chrono::steady_clock::now() - t < dur) {
				}
			}
			// 慢尝试
			while (flag_.test_and_set(std::memory_order_acquire)) {
				flag_.wait(true, std::memory_order_relaxed);
			}
		}
		inline void unlock() noexcept {
			flag_.clear(std::memory_order_release);
			flag_.notify_one();
		}

		std::atomic_flag flag_;
	};

} // namespace chenc
namespace chenc {

	template <typename T>
	class atomic_list {
	public:
		struct node_t {
			T value_;
			// sequence_ 的高位可看作 version，低位配合取模看作 ID
			// sequence_ == pos 表示槽位可写
			// sequence_ == pos + 1 表示槽位可读
			std::atomic<u64> sequence_;
		};

		inline atomic_list(u64 initial_capacity = 1024)
			: size_(0), enqueue_pos_(0), dequeue_pos_(0),
			  pop_thread_(0), push_thread_(0), map_size_(initial_capacity) {
			value_map_ = new node_t[initial_capacity];
			for (u64 i = 0; i < initial_capacity; ++i) {
				value_map_[i].sequence_.store(i, std::memory_order_relaxed);
			}
		}

		inline ~atomic_list() {
			delete[] value_map_.load();
		}

		// 入队
		inline void push(T &&value) {
			std::chrono::nanoseconds dur(500);
			while (true) {
				// 进入逻辑前增加计数，保护当前内存指针
				push_thread_.fetch_add(1, std::memory_order_acquire);

				u64 pos = enqueue_pos_.load(std::memory_order_relaxed);
				node_t *current_map = value_map_.load(std::memory_order_acquire);
				u64 cap = map_size_.load(std::memory_order_acquire);
				node_t *node = &current_map[pos % cap];
				u64 seq = node->sequence_.load(std::memory_order_acquire);

				intptr_t diff = (intptr_t)seq - (intptr_t)pos;

				if (diff == 0) {
					// 槽位就绪，尝试抢占逻辑序号
					if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
						node->value_ = std::move(value);
						node->sequence_.store(pos + 1, std::memory_order_release);
						size_.fetch_add(1, std::memory_order_relaxed);

						push_thread_.fetch_sub(1, std::memory_order_release);
						return;
					}
				} else if (diff < 0) {
					// 缓冲区已满，需要扩容
					// 必须先释放自己的计数，否则 rebuffer 无法进入静默期
					push_thread_.fetch_sub(1, std::memory_order_release);
					rebuffer(cap * 2);
					continue; // 扩容后重试
				}

				// diff > 0 或 CAS 失败：其他线程正在操作，暂时退出重试
				push_thread_.fetch_sub(1, std::memory_order_release);
				// std::this_thread::yield();
				sleep(dur *= 2);
			}
		}

		/**
		 * @brief 出队操作
		 * @return std::optional<T> 如果队列为空返回 std::nullopt，否则返回包含元素的 optional
		 */
		inline std::optional<T> pop() {
			std::chrono::nanoseconds dur(500);
			while (true) {
				// 增加活跃线程计数，保护 value_map_ 指针
				pop_thread_.fetch_add(1, std::memory_order_acquire);

				u64 pos = dequeue_pos_.load(std::memory_order_relaxed);
				node_t *current_map = value_map_.load(std::memory_order_acquire);
				u64 cap = map_size_.load(std::memory_order_acquire);
				node_t *node = &current_map[pos % cap];
				u64 seq = node->sequence_.load(std::memory_order_acquire);

				// 计算序列号差异：diff == 0 表示有数据可读
				intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

				if (diff == 0) {
					// 尝试抢占出队位置
					if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
						// 移动数据到 optional
						std::optional<T> result(std::move(node->value_));

						// 设置槽位为下一圈回绕时的逻辑序号（pos + cap）
						node->sequence_.store(pos + cap, std::memory_order_release);
						size_.fetch_sub(1, std::memory_order_relaxed);

						pop_thread_.fetch_sub(1, std::memory_order_release);
						return result;
					}
				} else if (diff < 0) {
					// 队列为空：当前位置的数据还没被 push 线程写入
					pop_thread_.fetch_sub(1, std::memory_order_release);
					return std::nullopt;
				}

				// CAS 失败或正在写入：释放计数并退避
				pop_thread_.fetch_sub(1, std::memory_order_release);
				sleep(dur *= 2);
			}
		}

		/**
		 * @brief 清空队列
		 */
		inline void clear() noexcept {
			recapacity_lock_.lock();
			wait_for_quiescent();

			u64 start = dequeue_pos_.load(std::memory_order_relaxed);
			u64 end = enqueue_pos_.load(std::memory_order_relaxed);
			u64 cap = map_size_.load(std::memory_order_relaxed);
			node_t *map = value_map_.load(std::memory_order_relaxed);

			for (u64 i = start; i < end; ++i) {
				u64 idx = i % cap;
				// 手动重置对象，释放 T 可能持有的内存（如 std::string）
				map[idx].value_ = T();
				// 重置 sequence 使得该位置变为“逻辑位置 i 可写”状态
				map[idx].sequence_.store(i, std::memory_order_release);
			}

			size_.store(0, std::memory_order_relaxed);
			// 消费者追上生产者
			dequeue_pos_.store(end, std::memory_order_relaxed);

			recapacity_lock_.unlock();
		} /**
		   * @brief 手动调整容量（收缩或扩容）
		   */
		inline void shrink_to_fit(u64 target_capacity = 0) {
			recapacity_lock_.lock();
			u64 current_elements = size_.load(std::memory_order_relaxed);
			u64 current_cap = map_size_.load(std::memory_order_relaxed);

			if (target_capacity == 0)
				target_capacity = current_elements > 1024 ? current_elements : 1024;

			if (target_capacity != current_cap) {
				rebuffer_internal(target_capacity);
			}
			recapacity_lock_.unlock();
		}

		inline void rebuffer(u64 new_size) {
			if (recapacity_lock_.try_lock()) {
				if (new_size > map_size_.load()) {
					rebuffer_internal(new_size);
				}
				recapacity_lock_.unlock();
			}
		}

		inline inline u64 size() const noexcept { return size_.load(); }
		inline inline u64 capacity() const noexcept { return map_size_.load(); }

	private:
		inline static void sleep(std::chrono::nanoseconds dur) noexcept {
			if (dur >= std::chrono::nanoseconds(10'000)) {
				std::this_thread::sleep_for(std::chrono::microseconds(100));
			} else {
				auto t = std::chrono::steady_clock::now();
				while (std::chrono::steady_clock::now() - t < dur)
					;
			}
		}
		// 等待所有活跃线程退出 value_map_ 的引用
		inline void wait_for_quiescent() {
			std::chrono::nanoseconds dur(500);
			while (push_thread_.load(std::memory_order_acquire) > 0 ||
				   pop_thread_.load(std::memory_order_acquire) > 0) {
				sleep(dur *= 2);
			}
		}

		// 内部实际执行搬运和内存替换的逻辑（调用前必须持有 recapacity_lock_）
		inline void rebuffer_internal(u64 new_size) {
			wait_for_quiescent();

			u64 old_cap = map_size_.load();
			node_t *old_map = value_map_.load();
			node_t *new_map = new node_t[new_size];

			for (u64 i = 0; i < new_size; ++i) {
				new_map[i].sequence_.store(i, std::memory_order_relaxed);
			}

			u64 start = dequeue_pos_.load();
			u64 end = enqueue_pos_.load();
			for (u64 i = start; i < end; ++i) {
				u64 old_idx = i % old_cap;
				u64 new_idx = i % new_size;
				new_map[new_idx].value_ = std::move(old_map[old_idx].value_);
				new_map[new_idx].sequence_.store(i + 1, std::memory_order_relaxed);
			}

			value_map_.store(new_map, std::memory_order_release);
			map_size_.store(new_size, std::memory_order_release);

			delete[] old_map;
		}

		alignas(64) std::atomic<u64> size_;
		alignas(64) spin_lock recapacity_lock_;
		alignas(64) std::atomic<u64> enqueue_pos_;
		alignas(64) std::atomic<u64> dequeue_pos_;
		alignas(64) std::atomic<u64> pop_thread_;  // 活跃读取者计数
		alignas(64) std::atomic<u64> push_thread_; // 活跃写入者计数

		alignas(64) std::atomic<node_t *> value_map_;
		alignas(64) std::atomic<u64> map_size_;
	};

} // namespace chenc