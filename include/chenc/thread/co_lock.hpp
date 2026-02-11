#pragma once

#include "chenc/core/cpu/relax.hpp"
#include "chenc/core/type.hpp"

#include <atomic>
#include <bit>
#include <chrono>
#include <semaphore>
#include <thread>

namespace chenc {
	// 独占锁
	class lock {
	};
	// 递归锁
	class re_lock {
	};
	// 读写锁
	class rw_lock {
	};
} // namespace chenc
