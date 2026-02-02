#pragma once

// --- 编译器探测 ---
#if defined(__clang__)
#	define CHENC_COMPILER_CLANG 1
#elif defined(__GNUC__) || defined(__GNUG__)
#	define CHENC_COMPILER_GCC 1
#elif defined(_MSC_VER)
#	define CHENC_COMPILER_MSVC 1
#endif

// --- CPP 语言版本标准化 ---
#if defined(_MSC_VER) && defined(_MSVC_LANG)
#	define CHENC_CPP_VERSION _MSVC_LANG
#else
#	define CHENC_CPP_VERSION __cplusplus
#endif

// --- 语义化版本判定 ---
#if CHENC_CPP_VERSION >= 202302L
#	define CHENC_CPP23 1
#endif

#if CHENC_CPP_VERSION >= 202002L
#	define CHENC_CPP20 1
#endif

#if CHENC_CPP_VERSION >= 201703L
#	define CHENC_CPP17 1
#endif

#if CHENC_CPP_VERSION >= 201402L
#	define CHENC_CPP14 1
#endif

#if CHENC_CPP_VERSION >= 201103L
#	define CHENC_CPP11 1
#endif

#ifndef CHENC_CPP11
// 确保编译器至少支持C++11
#	error "C++11 or higher is required"
#endif

// --- 编译器特性增强 ---

// 强制内联
#if defined(CHENC_COMPILER_MSVC)
#	define CHENC_FORCE_INLINE __forceinline
#else
#	define CHENC_FORCE_INLINE inline __attribute__((always_inline))
#endif

// 禁用内联
#if defined(CHENC_COMPILER_MSVC)
#	define CHENC_NO_INLINE __declspec(noinline)
#else
#	define CHENC_NO_INLINE __attribute__((noinline))
#endif

// 禁用优化
#if defined(CHENC_COMPILER_MSVC)
#	define CHENC_DISABLE_OPTIMIZE __pragma(optimize("", off))
#else
#	define CHENC_DISABLE_OPTIMIZE __attribute__((optimize("O0")))
#endif
 

// 现代主流 CPU 缓存行均为 64 字节
#define CHENC_CACHE_LINE 64

// 优先使用标准 alignas，只有在非标准环境下才回退到编译器指令
#if defined(CHENC_CPP11)
#	define CHENC_CACHE_ALIGN alignas(CHENC_CACHE_LINE)
#else
#	if defined(CHENC_COMPILER_MSVC)
#		define CHENC_CACHE_ALIGN __declspec(align(CHENC_CACHE_LINE))
#	else
#		define CHENC_CACHE_ALIGN __attribute__((aligned(CHENC_CACHE_LINE)))
#	endif
#endif
