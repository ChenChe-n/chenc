#pragma once

#include "chenc/core/arch.hpp"
#include "chenc/core/cpp.hpp"

// --- 架构特定的头文件引入 ---
#if defined(CHENC_ARCH_X86)
#	if defined(CHENC_COMPILER_MSVC)
#		include <intrin.h>
#	else
#		include <immintrin.h>
#	endif
#elif defined(CHENC_ARCH_ARM)
#	if defined(CHENC_COMPILER_MSVC)
#		include <intrin.h>
#	else
#		include <arm_neon.h>
#	endif
#endif

namespace chenc::cpu {
	CHENC_FORCE_INLINE void relax() noexcept {
#if defined(CHENC_ARCH_X86)
#	if defined(CHENC_COMPILER_MSVC)
		_mm_pause();
#	else
		__builtin_ia32_pause();
#	endif
#elif defined(CHENC_ARCH_ARM)
#	if defined(CHENC_COMPILER_MSVC)
		__yield();
#	else
		__asm__ __volatile__("yield" ::: "memory");
#	endif
#else
		// 兜底：内存屏障 + 轻量循环
		__asm__ __volatile__("" ::: "memory");
		for (volatile int i = 0; i < 64; ++i)
			;
#endif
	}
} // namespace chenc::cpu