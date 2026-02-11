#pragma once

#include "chenc/core/type.hpp"
#include <compare>

namespace chenc {
	struct alignas(8) date {
		u16 year_; // [0, 65536)
		u8 month_; // [1, 12]
		u8 day_;   // [1, 31]

		u32 sec_; // [0,2^32-1] / (2^32 / 86400)
		inline constexpr std::strong_ordering operator<=>(const date &other) const {
			if (year_ != other.year_)
				return year_ <=> other.year_;
			if (month_ != other.month_)
				return month_ <=> other.month_;
			if (day_ != other.day_)
				return day_ <=> other.day_;
			return sec_ <=> other.sec_;
		}
	};

	struct alignas(16) date_long {
		u16 year_;			// [0, 65536)
		u8 month_;			// [1, 12]
		u8 day_;			// [1, 31]
		i16 utc_off_minute; // [-32768, 32767]
		u8 is_dst_;			// [0, 1]
		u8 reserved_;

		u64 sec_; // [0,2^64-1] / (2^64 / 86400)
		inline constexpr std::strong_ordering operator<=>(const date_long &other) const {
			// 规格化utc时间和dst
			auto utc_this = *this;
			auto utc_other = other;
            if (utc_this.utc_off_minute != 0 || utc_this.is_dst_ != 0){

            }
            if (utc_other.utc_off_minute != 0 || utc_other.is_dst_ != 0){

            }

			if (utc_this.year_ != utc_other.year_)
				return utc_this.year_ <=> utc_other.year_;
			if (utc_this.month_ != utc_other.month_)
				return utc_this.month_ <=> utc_other.month_;
			if (utc_this.day_ != utc_other.day_)
				return utc_this.day_ <=> utc_other.day_;
			return utc_this.sec_ <=> utc_other.sec_;
		}
	};
} // namespace chenc