#pragma once

#include "chenc/atomic_list.hpp"
#include "chenc/type_int.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace chenc {

	class thread_pool {
	public:
		// ============================
		// 线程池状态定义
		// ============================
		enum class type {
			init,
			pause, // 阻塞 add_task；worker drain 队列
			run,
			stop,	   // 不接新任务，执行完队列后退出
			force_stop // 立即退出
		};

	public:
		// ============================
		// 构造 / 析构
		// ============================
		explicit thread_pool(
			u64 thread_num = std::thread::hardware_concurrency(),
			u64 task_capacity = 1024)
			: tasks_(task_capacity), flag_(type::init), threads_count_(0) {
			if (thread_num == 0) {
				throw std::invalid_argument("thread_pool: thread_num must >= 1");
			}

			threads_.reserve(thread_num);
			for (u64 i = 0; i < thread_num; ++i) {
				threads_.emplace_back(
					std::jthread(&thread_pool::worker_loop, this));
			}

			flag_.store(type::run, std::memory_order_release);
		}

		~thread_pool() {
			flag_.store(type::force_stop, std::memory_order_release);
			wake_all_workers();

			// 等待所有 worker 退出
			while (threads_count_.load(std::memory_order_acquire) != 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		// ============================
		// 提交任务（阻塞 / future）
		// ============================
		template <class F, class... Args>
		auto add_task(F &&f, Args &&...args)
			-> std::future<std::invoke_result_t<F, Args...>> {
			using R = std::invoke_result_t<F, Args...>;

			// 1. 等待进入 run 状态
			while (true) {
				auto s = flag_.load(std::memory_order_acquire);

				if (s == type::run)
					break;

				// pause：阻塞提交
				if (s == type::pause) {
					submit_gate_.wait(true, std::memory_order_acquire);
					continue;
				}

				// stop / force_stop
				throw std::runtime_error("thread_pool stopped");
			}

			// 2. 包装任务
			auto task = std::make_shared<std::packaged_task<R()>>(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...));

			std::future<R> fut = task->get_future();

			// 3. 推入任务队列
			if (!tasks_.push([task]() { (*task)(); })) {
				throw std::runtime_error("thread_pool task queue full");
			}

			// 4. 唤醒 worker
			wake_one_worker();

			return fut;
		}

		// ============================
		// 状态控制
		// ============================

		// 阻塞 add_task，但 worker 继续 drain 队列
		void pause() {
			submit_gate_.test_and_set(std::memory_order_release);
			flag_.store(type::pause, std::memory_order_release);
		}

		// 允许继续 add_task
		void resume() {
			flag_.store(type::run, std::memory_order_release);
			submit_gate_.clear(std::memory_order_release);
			submit_gate_.notify_all();

			wake_all_workers();
		}

		// 停止线程池
		void stop(bool wait_for_task_done = true) {
			flag_.store(
				wait_for_task_done ? type::stop : type::force_stop,
				std::memory_order_release);

			submit_gate_.clear(std::memory_order_release);
			submit_gate_.notify_all();
			wake_all_workers();

			while (threads_count_.load(std::memory_order_acquire) != 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		// 当前任务数（非严格一致）
		u64 task_count() const {
			return tasks_.size();
		}

	private:
		// ============================
		// worker 主循环
		// ============================
		void worker_loop() {
			threads_count_.fetch_add(1, std::memory_order_relaxed);

			// RAII 退出计数
			auto on_exit = std::unique_ptr<void, decltype([&](void *) {
											   threads_count_.fetch_sub(1, std::memory_order_relaxed);
										   })>(nullptr);

			while (true) {
				auto s = flag_.load(std::memory_order_acquire);

				// 强制停止：立即退出
				if (s == type::force_stop) {
					return;
				}

				// 尝试取任务
				if (auto task = tasks_.pop()) {
					task.value()();
					continue;
				}

				// 队列为空
				if (s == type::stop) {
					// stop：drain 完后退出
					return;
				}

				// run / pause：进入等待区
				worker_gate_.wait(true, std::memory_order_acquire);
			}
		}

		// ============================
		// worker 唤醒工具
		// ============================
		void wake_one_worker() {
			worker_gate_.clear(std::memory_order_release);
			worker_gate_.notify_one();
			worker_gate_.test_and_set(std::memory_order_release);
		}

		void wake_all_workers() {
			worker_gate_.clear(std::memory_order_release);
			worker_gate_.notify_all();
			worker_gate_.test_and_set(std::memory_order_release);
		}

	private:
		// ============================
		// 成员变量
		// ============================
		std::vector<std::jthread> threads_;		   // worker 线程
		atomic_list<std::function<void()>> tasks_; // 无锁任务队列

		std::atomic<type> flag_;		 // 状态机
		std::atomic<u64> threads_count_; // 活跃 worker 数

		std::atomic_flag submit_gate_ = ATOMIC_FLAG_INIT; // add_task 阻塞门
		std::atomic_flag worker_gate_ = ATOMIC_FLAG_INIT; // worker 等待门
	};

} // namespace chenc
