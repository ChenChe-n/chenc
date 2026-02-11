#pragma once

#include "chenc/core/type.hpp"

#include <atomic>
#include <thread>

namespace chenc::thread {
	inline u64 this_id() {
		static std::atomic<u64> id_counter = 0;
		static thread_local const u64 id = id_counter.fetch_add(1, std::memory_order_relaxed);
		return id;
	}
} // namespace chenc::thread