#pragma once

#include "chenc/cpp.hpp"

// --- 架构探测层 ---
#if defined(__x86_64__) || defined(_M_X64)
#	define CHENC_ARCH_X86_64 64
#	define CHENC_ARCH_X86 64
#elif defined(__i386__) || defined(_M_IX86)
#	define CHENC_ARCH_X86_32 32
#	define CHENC_ARCH_X86 32
#elif defined(__aarch64__) || defined(_M_ARM64)
#	define CHENC_ARCH_ARM_64 64
#	define CHENC_ARCH_ARM 64
#elif defined(__arm__) || defined(_M_ARM)
#	define CHENC_ARCH_ARM_32 32
#	define CHENC_ARCH_ARM 32
#elif defined(__riscv) && (__riscv_xlen == 64)
#	define CHENC_ARCH_RISCV_64 64
#	define CHENC_ARCH_RISCV 64
#elif defined(__riscv) && (__riscv_xlen == 32)
#	define CHENC_ARCH_RISCV_32 32
#	define CHENC_ARCH_RISCV 32
#elif defined(__loongarch64)
#	define CHENC_ARCH_LOONGARCH_64 64
#	define CHENC_ARCH_LOONGARCH 64
#elif defined(__loongarch32)
#	define CHENC_ARCH_LOONGARCH_32 32
#	define CHENC_ARCH_LOONGARCH 32
#elif defined(__mips64)
#	define CHENC_ARCH_MIPS_64 64
#	define CHENC_ARCH_MIPS 64
#elif defined(__mips__) || defined(__mips)
#	define CHENC_ARCH_MIPS_32 32
#	define CHENC_ARCH_MIPS 32
#elif defined(__ppc64__) || defined(__PPC64__) || defined(_ARCH_PPC64)
#	define CHENC_ARCH_POWERPC_64 64
#	define CHENC_ARCH_POWERPC 64
#elif defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
#	define CHENC_ARCH_POWERPC_32 32
#	define CHENC_ARCH_POWERPC 32
#endif

// --- 硬件指令头文件包含 ---
#if defined(CHENC_ARCH_X86)
#	include <immintrin.h>
#elif defined(CHENC_ARCH_ARM)
#	include <arm_neon.h>
#	if defined(CHENC_COMPILER_MSVC)
#		include <intrin.h>
#	endif
#endif

// --- 低功耗等待指令 (cpu_relax) ---
namespace chenc {
	inline void cpu_relax() {
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
#elif defined(CHENC_ARCH_RISCV)
		__asm__ __volatile__("fence" ::: "memory");
#else
// 回退方案：使用编译器屏障防止空循环被彻底消除
#	if defined(CHENC_COMPILER_MSVC)
		_ReadWriteBarrier();
#	else
		__asm__ __volatile__("" ::: "memory");
#	endif
#endif
	}
} // namespace chenc