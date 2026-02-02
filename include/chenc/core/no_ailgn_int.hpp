
#pragma once

#include "chenc/core/int.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cstring>
#include <type_traits>

namespace chenc {

	inline constexpr void constexpr_memcpy(void *dst, const void *src, u64 len) noexcept {
		if (std::is_constant_evaluated()) {
			auto *d = static_cast<u8 *>(dst);
			auto *s = static_cast<const u8 *>(src);
			for (u64 i = 0; i < len; ++i) {
				d[i] = s[i];
			}
		} else {
			::memcpy(dst, src, len);
		}
	}

	template <u64 Bytes>
	class no_ailgn_int {
		static_assert(Bytes >= 1 && Bytes <= 8, "no_ailgn_int: Bytes must be in [1, 8]");

	private:
		struct alignas(1) Storage {
			u8 data_[Bytes];
		} storage_;

	public:
		inline constexpr void set(u64 val) noexcept {
			auto tmp = std::bit_cast<std::array<u8, 8>>(val);
			if constexpr (std::endian::native == std::endian::little) {
				constexpr_memcpy(storage_.data_, tmp.data(), Bytes);
			} else {
				constexpr_memcpy(storage_.data_, tmp.data() + (8 - Bytes), Bytes);
			}
		}

		inline constexpr u64 get() const noexcept {
			u64 val = 0;
			auto tmp = std::array<u8, 8>{};
			if constexpr (std::endian::native == std::endian::little) {
				constexpr_memcpy(tmp.data(), storage_.data_, Bytes);
			} else {
				constexpr_memcpy(tmp.data() + (8 - Bytes), storage_.data_, Bytes);
			}
			val = std::bit_cast<u64>(tmp);
			return val;
		}

		inline constexpr no_ailgn_int() noexcept : storage_{} {}
		inline constexpr no_ailgn_int(u64 val) noexcept { set(val); }

		// 操作符重载
		inline constexpr operator u64() const noexcept { return get(); }
		inline constexpr explicit operator bool() const noexcept { return get() != 0; }
		inline constexpr no_ailgn_int &operator=(u64 val) noexcept {
			set(val);
			return *this;
		}

		// 算术
		inline constexpr no_ailgn_int operator+(u64 v) const noexcept { return {get() + v}; }
		inline constexpr no_ailgn_int operator-(u64 v) const noexcept { return {get() - v}; }
		inline constexpr no_ailgn_int operator*(u64 v) const noexcept { return {get() * v}; }
		inline constexpr no_ailgn_int operator/(u64 v) const noexcept { return {get() / v}; }
		inline constexpr no_ailgn_int operator%(u64 v) const noexcept { return {get() % v}; }

		// 赋值算术
		inline constexpr no_ailgn_int &operator+=(u64 v) noexcept {
			set(get() + v);
			return *this;
		}
		inline constexpr no_ailgn_int &operator-=(u64 v) noexcept {
			set(get() - v);
			return *this;
		}
		inline constexpr no_ailgn_int &operator*=(u64 v) noexcept {
			set(get() * v);
			return *this;
		}
		inline constexpr no_ailgn_int &operator/=(u64 v) noexcept {
			set(get() / v);
			return *this;
		}
		inline constexpr no_ailgn_int &operator%=(u64 v) noexcept {
			set(get() % v);
			return *this;
		}

		// 前后自增减
		inline constexpr no_ailgn_int &operator++() noexcept {
			set(get() + 1);
			return *this;
		}
		inline constexpr no_ailgn_int operator++(int) noexcept {
			no_ailgn_int tmp = *this;
			set(get() + 1);
			return tmp;
		}
		inline constexpr no_ailgn_int &operator--() noexcept {
			set(get() - 1);
			return *this;
		}
		inline constexpr no_ailgn_int operator--(int) noexcept {
			no_ailgn_int tmp = *this;
			set(get() - 1);
			return tmp;
		}

		// 位运算
		inline constexpr no_ailgn_int operator~() const noexcept { return {~get()}; }
		inline constexpr no_ailgn_int operator&(u64 v) const noexcept { return {get() & v}; }
		inline constexpr no_ailgn_int &operator&=(u64 v) noexcept {
			set(get() & v);
			return *this;
		}
		inline constexpr no_ailgn_int operator|(u64 v) const noexcept { return {get() | v}; }
		inline constexpr no_ailgn_int &operator|=(u64 v) noexcept {
			set(get() | v);
			return *this;
		}
		inline constexpr no_ailgn_int operator^(u64 v) const noexcept { return {get() ^ v}; }
		inline constexpr no_ailgn_int &operator^=(u64 v) noexcept {
			set(get() ^ v);
			return *this;
		}
		inline constexpr no_ailgn_int operator<<(u64 v) const noexcept { return {get() << v}; }
		inline constexpr no_ailgn_int &operator<<=(u64 v) noexcept {
			set(get() << v);
			return *this;
		}
		inline constexpr no_ailgn_int operator>>(u64 v) const noexcept { return {get() >> v}; }
		inline constexpr no_ailgn_int &operator>>=(u64 v) noexcept {
			set(get() >> v);
			return *this;
		}

		// 一元
		inline constexpr no_ailgn_int operator-() const noexcept { return {-get()}; }
		inline constexpr no_ailgn_int operator+() const noexcept { return *this; }
		inline constexpr bool operator!() const noexcept { return get() == 0; }

		// 比较
		inline constexpr bool operator==(u64 v) const noexcept { return get() == v; }
		inline constexpr std::strong_ordering operator<=>(u64 v) const noexcept { return get() <=> v; }
	};

	template <u64 Bytes>
	class more_int {
	private:
		no_ailgn_int<Bytes> storage_;

	public:
		// 核心：符号扩展逻辑
		//
		inline constexpr i64 get() const noexcept {
			u64 val = storage_.get();
			constexpr u64 shift = (8 - Bytes) * 8;
			return (static_cast<i64>(val << shift)) >> shift;
		}

		// 快速访问底层原始位（不带符号扩展）
		inline constexpr u64 get_raw() const noexcept { return storage_.get(); }

		inline constexpr void set(i64 val) noexcept { storage_.set(static_cast<u64>(val)); }

		inline constexpr more_int() noexcept = default;
		inline constexpr more_int(i64 val) noexcept { set(val); }

		// 操作符重载
		inline constexpr operator i64() const noexcept { return get(); }
		inline constexpr explicit operator bool() const noexcept { return get_raw() != 0; }
		inline constexpr more_int &operator=(i64 val) noexcept {
			set(val);
			return *this;
		}

		// 算术（+ - * 补码运算符号不敏感，用 raw 优化）
		inline constexpr more_int operator+(i64 v) const noexcept { return {static_cast<i64>(get_raw() + static_cast<u64>(v))}; }
		inline constexpr more_int operator-(i64 v) const noexcept { return {static_cast<i64>(get_raw() - static_cast<u64>(v))}; }
		inline constexpr more_int operator*(i64 v) const noexcept { return {static_cast<i64>(get_raw() * static_cast<u64>(v))}; }

		// 算术（/ % 必须用扩展后的值）
		inline constexpr more_int operator/(i64 v) const noexcept { return {get() / v}; }
		inline constexpr more_int operator%(i64 v) const noexcept { return {get() % v}; }

		// 赋值算术
		inline constexpr more_int &operator+=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() + static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int &operator-=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() - static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int &operator*=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() * static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int &operator/=(i64 v) noexcept {
			set(get() / v);
			return *this;
		}
		inline constexpr more_int &operator%=(i64 v) noexcept {
			set(get() % v);
			return *this;
		}

		// 前后自增减
		inline constexpr more_int &operator++() noexcept {
			set(static_cast<i64>(get_raw() + 1));
			return *this;
		}
		inline constexpr more_int operator++(int) noexcept {
			more_int tmp = *this;
			set(static_cast<i64>(get_raw() + 1));
			return tmp;
		}
		inline constexpr more_int &operator--() noexcept {
			set(static_cast<i64>(get_raw() - 1));
			return *this;
		}
		inline constexpr more_int operator--(int) noexcept {
			more_int tmp = *this;
			set(static_cast<i64>(get_raw() - 1));
			return tmp;
		}

		// 位运算（>> 必须用 get() 以保持算术右移）
		inline constexpr more_int operator~() const noexcept { return {static_cast<i64>(~get_raw())}; }
		inline constexpr more_int operator&(i64 v) const noexcept { return {static_cast<i64>(get_raw() & static_cast<u64>(v))}; }
		inline constexpr more_int &operator&=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() & static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int operator|(i64 v) const noexcept { return {static_cast<i64>(get_raw() | static_cast<u64>(v))}; }
		inline constexpr more_int &operator|=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() | static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int operator^(i64 v) const noexcept { return {static_cast<i64>(get_raw() ^ static_cast<u64>(v))}; }
		inline constexpr more_int &operator^=(i64 v) noexcept {
			set(static_cast<i64>(get_raw() ^ static_cast<u64>(v)));
			return *this;
		}
		inline constexpr more_int operator<<(u64 v) const noexcept { return {static_cast<i64>(get_raw() << v)}; }
		inline constexpr more_int &operator<<=(u64 v) noexcept {
			set(static_cast<i64>(get_raw() << v));
			return *this;
		}
		inline constexpr more_int operator>>(u64 v) const noexcept { return {get() >> v}; }
		inline constexpr more_int &operator>>=(u64 v) noexcept {
			set(get() >> v);
			return *this;
		}

		// 一元
		inline constexpr more_int operator-() const noexcept { return {-get()}; }
		inline constexpr more_int operator+() const noexcept { return *this; }
		inline constexpr bool operator!() const noexcept { return get_raw() == 0; }

		// 比较
		inline constexpr bool operator==(i64 v) const noexcept { return get() == v; }
		inline constexpr std::strong_ordering operator<=>(i64 v) const noexcept { return get() <=> v; }
	}; 
} // namespace chenc