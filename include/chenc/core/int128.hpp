#pragma once

#include "chenc/core/cpp.hpp"

#ifdef CHENC_COMPILER_MSVC
#	include <__msvc_int128.hpp>
using __chenc_uint128 = std::_Unsigned128;
using __chenc_int128 = std::_Signed128;
#elif defined(__SIZEOF_INT128__)
#	include <cstdint>
using __chenc_uint128 = __uint128_t;
using __chenc_int128 = __int128_t;
#else
# error "128-bit integer not supported on this platform"
#endif

namespace chenc {
	using i128 = __chenc_int128;
	using u128 = __chenc_uint128;
} // namespace chenc