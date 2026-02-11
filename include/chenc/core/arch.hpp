#pragma once

#include "chenc/core/cpp.hpp"
#include "chenc/core/type.hpp"

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
