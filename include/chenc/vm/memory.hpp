#pragma once

#include "chenc/core/type.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <atomic>

namespace chenc::vm::detail {
	class memory {
	private: 
		struct alignas(32) memory_block {
			u64 begin_;
			u64 end_; 
			u64 flags_;
			std::shared_ptr<u8> data_;
		};
		struct alignas(32) error_info_t {
			std::map<u64, memory_block> *memory_map_;
			u64 addr_;
			u64 load_size_;
		};
		struct alignas(32) cache_t {
			u64 addr_;
			u64 size_;
			u64 flags_;
			u8 *data_;
		};
		inline static void memory_error(error_info_t) {
			throw std::runtime_error("Memory access error");
		}

	public:
		// 创建空的内存映射
		inline memory() {
		}
		// 拷贝内存映射 (写时复制)
		inline memory(const memory &other) = default;
		// 移动内存映射
		inline memory(memory &&other) {
		}

		inline ~memory() {
		}

		inline memory &operator=(const memory &other) = default;
		inline memory &operator=(memory &&other) {
		}

		inline void set_error_callback(std::function<void(error_info_t)> callback) {
		}

		template <u64 Byte_Size>
		inline u64 load(u64 addr) {
		}
		template <u64 Byte_Size>
		inline void store(u64 addr, u64 val) {
		}

	private:
		// 内存映射
		std::map<u64, memory_block> memory_map_ = {};
		// 内存访问错误 回调函数
		std::function<void(error_info_t)> memory_error_callback_ = memory_error;
		// 内存页大小
		u64 page_size_ = 4096;
		// 缓存
		cache_t cache_[8][4];
	};
} // namespace chenc::vm::detail