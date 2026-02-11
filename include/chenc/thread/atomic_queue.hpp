#pragma once

#include "chenc/core/type.hpp"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace chenc::thread {
	template <typename T>
	class atomic_queue {
	private:
		struct Node {
			// 【值存储核心】使用裸字节数组预留 T 所需空间，避免自动构造
			alignas(T) std::byte data_[sizeof(T)];

			// 【序列号同步】用于彻底消除读写冲突
			// 初始值为槽位的索引 i。
			// 当 sequence == pos 时，表示该槽位是空的，生产者可以写入。
			// 当 sequence == pos + 1 时，表示该槽位已填充，消费者可以读取。
			alignas(64) std::atomic<u64> sequence_{0};

			// 获取裸空间对应的 T 指针，方便进行手动构造和析构
			T *get_ptr() noexcept { return reinterpret_cast<T *>(data_); }
		};

		// 队列状态：用于在扩容期间“锁住”所有操作
		enum status : u64 { normal = 0,
							resizing = 1 };

		// 使用 alignas(64) 强制缓存行对齐，防止“伪共享” (False Sharing) 破坏性能
		alignas(64) std::atomic<Node *> kmap_{nullptr};		// 指向当前存储数组的指针
		alignas(64) std::atomic<u64> stat_{status::normal}; // 队列状态位
		alignas(64) std::atomic<u64> head_pos_{0};			// 消费者进度索引
		alignas(64) std::atomic<u64> tail_pos_{0};			// 生产者进度索引
		alignas(64) std::atomic<u64> capacity_{0};			// 当前总容量（必须是 2 的幂）

		// 【静止同步计数器】记录当前正在操作队列的线程数，确保扩容时内存安全
		alignas(64) std::atomic<u64> active_threads_{0};

	public:
		atomic_queue(u64 initial_capa = 4096) {
			initial_capa = std::bit_ceil(initial_capa); // 向上取 2 的幂，方便取模优化
			Node *data = new Node[initial_capa];
			for (u64 i = 0; i < initial_capa; ++i) {
				// 初始化序列号为槽位索引
				data[i].sequence_.store(i, std::memory_order_relaxed);
			}
			kmap_.store(data, std::memory_order_relaxed);
			capacity_.store(initial_capa, std::memory_order_relaxed);
		}

		~atomic_queue() {
			Node *map = kmap_.load();
			if (map) {
				u64 h = head_pos_.load();
				u64 t = tail_pos_.load();
				u64 capa_mask = capacity_.load() - 1;
				// 【显式析构】只销毁队列中尚未被消费的存活对象
				for (u64 i = h; i < t; ++i) {
					map[i & capa_mask].get_ptr()->~T();
				}
				delete[] map;
			}
		}

		// --- 入队：支持移动语义，直接存值 ---
		void push(T &&value) {
			while (true) {
				// 1. 如果正在扩容，则挂起当前线程等待通知
				if (stat_.load(std::memory_order_acquire) == status::resizing) {
					stat_.wait(status::resizing);
					continue;
				}

				// 2. 增加活跃线程数，声明我正在访问内存
				active_threads_.fetch_add(1, std::memory_order_acquire);

				// 3. 双重检查：防止在 fetch_add 期间状态刚变为 resizing
				if (stat_.load(std::memory_order_relaxed) == status::resizing) {
					active_threads_.fetch_sub(1, std::memory_order_release);
					continue;
				}

				u64 capa = capacity_.load(std::memory_order_relaxed);
				u64 pos = tail_pos_.load(std::memory_order_relaxed);
				Node *map = kmap_.load(std::memory_order_relaxed);
				Node *node = &map[pos & (capa - 1)]; // 位运算代替取模
				u64 seq = node->sequence_.load(std::memory_order_acquire);

				// 判断逻辑：当前槽位的序列号是否等于我期望写入的位置？
				if (seq == pos) {
					// 尝试抢占 tail 索引，成功者才有权写入该槽位
					if (tail_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
						// 【Placement New】在预分配的裸内存上移动构造对象
						new (node->get_ptr()) T(std::move(value));

						// 【发布语义】更新序列号为 pos + 1，通知消费者该槽位现在可读
						node->sequence_.store(pos + 1, std::memory_order_release);
						active_threads_.fetch_sub(1, std::memory_order_release);
						return;
					}
				} else if ((i64)(seq - pos) < 0) {
					// seq < pos 说明队列已满，需要扩容
					active_threads_.fetch_sub(1, std::memory_order_release);
					reset_capa(capa * 2);
					continue;
				}

				// 抢占失败或槽位被占用，减小计数并让出 CPU 片刻
				active_threads_.fetch_sub(1, std::memory_order_release);
				std::this_thread::yield();
			}
		}

		// --- 出队：返回 std::optional 避免指针悬挂 ---
		std::optional<T> pop() {
			while (true) {
				if (stat_.load(std::memory_order_acquire) == status::resizing) {
					stat_.wait(status::resizing);
					continue;
				}

				active_threads_.fetch_add(1, std::memory_order_acquire);
				if (stat_.load(std::memory_order_relaxed) == status::resizing) {
					active_threads_.fetch_sub(1, std::memory_order_release);
					continue;
				}

				u64 capa = capacity_.load(std::memory_order_relaxed);
				u64 pos = head_pos_.load(std::memory_order_relaxed);
				Node *map = kmap_.load(std::memory_order_relaxed);
				Node *node = &map[pos & (capa - 1)];
				u64 seq = node->sequence_.load(std::memory_order_acquire);

				// 判断逻辑：序列号是否等于 pos + 1？（即生产者已完成写入）
				if (seq == pos + 1) {
					// 尝试抢占 head 索引
					if (head_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
						T *val_ptr = node->get_ptr();
						// 移动数据到返回结果中
						std::optional<T> res(std::move(*val_ptr));

						// 【非常重要】手动调用析构函数，释放对象持有的资源（如堆内存）
						// 此时 node->data_ 所在的内存块本身并未被释放，可循环使用
						val_ptr->~T();

						// 【发布语义】更新序列号为 pos + capa (下一轮该位置的写索引)，通知生产者可复写
						node->sequence_.store(pos + capa, std::memory_order_release);
						active_threads_.fetch_sub(1, std::memory_order_release);
						return res;
					}
				} else if ((i64)(seq - (pos + 1)) < 0) {
					// 队列为空（或者生产者还在写入过程中）
					active_threads_.fetch_sub(1, std::memory_order_release);
					return std::nullopt;
				}

				active_threads_.fetch_sub(1, std::memory_order_release);
				std::this_thread::yield();
			}
		}

		u64 size() const noexcept {
			u64 t = tail_pos_.load(std::memory_order_relaxed);
			u64 h = head_pos_.load(std::memory_order_relaxed);
			return t > h ? t - h : 0;
		}

	private:
		// --- 扩容逻辑：这是无锁队列中最危险也最核心的部分 ---
		void reset_capa(u64 new_capacity) {
			u64 expected = status::normal;
			// 只有一个线程能成功将状态从 normal 变为 resizing
			if (!stat_.compare_exchange_strong(expected, status::resizing))
				return;

			// 【屏障】等待所有已经在 push/pop 中的线程完成操作并退出“活跃区”
			while (active_threads_.load(std::memory_order_acquire) > 0) {
				std::this_thread::yield();
			}

			u64 old_capa = capacity_.load(std::memory_order_relaxed);
			Node *old_map = kmap_.load(std::memory_order_relaxed);
			new_capacity = std::bit_ceil(new_capacity);
			Node *new_map = new Node[new_capacity];

			// 初始化新数组的序列号
			for (u64 i = 0; i < new_capacity; ++i) {
				new_map[i].sequence_.store(i, std::memory_order_relaxed);
			}

			u64 h = head_pos_.load(std::memory_order_relaxed);
			u64 t = tail_pos_.load(std::memory_order_relaxed);

			// 数据迁移：将旧数组中的对象搬迁到新数组
			for (u64 i = h; i < t; ++i) {
				Node &old_node = old_map[i & (old_capa - 1)];
				Node &new_node = new_map[i & (new_capacity - 1)];

				// 在新地址移动构造，并析构旧地址的对象
				new (new_node.get_ptr()) T(std::move(*old_node.get_ptr()));
				old_node.get_ptr()->~T();

				// 设置新槽位为“可读”状态
				new_node.sequence_.store(i + 1, std::memory_order_relaxed);
			}

			// 原子更新数组指针和容量
			kmap_.store(new_map, std::memory_order_release);
			capacity_.store(new_capacity, std::memory_order_release);
			delete[] old_map; // 此时已确定没有任何线程在访问 old_map

			// 恢复 normal 状态并通知所有正在 wait 的线程
			stat_.store(status::normal, std::memory_order_release);
			stat_.notify_all();
		}
	};
} // namespace chenc::thread