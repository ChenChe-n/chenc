#pragma once

#include "chenc/core/type.hpp"

#include <type_traits>

#define CHENC_CREATE_ENUM_FUNC(enum_name)                                         \
	static_assert(std::is_enum_v<enum_name>, "enum_name must be an enum type.");  \
                                                                                  \
	inline constexpr enum_name operator&(enum_name a, enum_name b) noexcept {     \
		return static_cast<enum_name>(static_cast<u64>(a) & static_cast<u64>(b)); \
	}                                                                             \
                                                                                  \
	inline constexpr enum_name operator|(enum_name a, enum_name b) noexcept {     \
		return static_cast<enum_name>(static_cast<u64>(a) | static_cast<u64>(b)); \
	}                                                                             \
                                                                                  \
	inline constexpr enum_name operator^(enum_name a, enum_name b) noexcept {     \
		return static_cast<enum_name>(static_cast<u64>(a) ^ static_cast<u64>(b)); \
	}                                                                             \
                                                                                  \
	inline constexpr enum_name &operator&=(enum_name &a, enum_name b) noexcept {  \
		return a = a & b;                                                         \
	}                                                                             \
                                                                                  \
	inline constexpr enum_name &operator|=(enum_name &a, enum_name b) noexcept {  \
		return a = a | b;                                                         \
	}                                                                             \
                                                                                  \
	inline constexpr enum_name &operator^=(enum_name &a, enum_name b) noexcept {  \
		return a = a ^ b;                                                         \
	}
