#pragma once

#include "chenc/convert/utf_char.hpp"
#include "chenc/convert/utf_opt.hpp"
#include "chenc/core/cpp.hpp"
#include "chenc/core/type.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace chenc::utf::detail {

	/**
	 * @brief utf32 转换为 utfX
	 * @note 内部实现，请勿调用
	 * @note 假定输入字符编码合法 
	 * @note result.output_block_ 存储期望的输出块数
	 */
	template <options_t Options,
			  any_utf_char In,
			  any_utf_char Out>
	CHENC_FORCE_INLINE constexpr error_t utf32_to_char(u32 input_char,
										   Out *const output_char, const Out *const output_end,
										   char_result_t<Options, In, Out> &result) noexcept {
		if constexpr (utf8_char<Out>) {
			// 1. 快速处理 ASCII
			if (input_char <= 0x7F) [[likely]] {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char >= output_end) [[unlikely]] {
						result.output_block_ += 1;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 1;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(input_char);
				result.output_block_ += 1;
				return error_t::none;
			} else if (input_char <= 0x7FF) [[likely]] {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char + 1 >= output_end) [[unlikely]] {
						result.output_block_ += 2;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 2;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(0xC0 | (input_char >> 6));
				output_char[1] = static_cast<Out>(0x80 | (input_char & 0x3F));
				result.output_block_ += 2;
				return error_t::none;
			} else if (input_char <= 0xFFFF) [[likely]] {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char + 2 >= output_end) [[unlikely]] {
						result.output_block_ += 3;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 3;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(0xE0 | (input_char >> 12));
				output_char[1] = static_cast<Out>(0x80 | ((input_char >> 6) & 0x3F));
				output_char[2] = static_cast<Out>(0x80 | (input_char & 0x3F));
				result.output_block_ += 3;
				return error_t::none;
			} else {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char + 3 >= output_end) [[unlikely]] {
						result.output_block_ += 4;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 4;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(0xF0 | (input_char >> 18));
				output_char[1] = static_cast<Out>(0x80 | ((input_char >> 12) & 0x3F));
				output_char[2] = static_cast<Out>(0x80 | ((input_char >> 6) & 0x3F));
				output_char[3] = static_cast<Out>(0x80 | (input_char & 0x3F));
				result.output_block_ += 4;
				return error_t::none;
			}
		} else if constexpr (utf16_char<Out>) {
			// BMP 字符
			if (input_char <= 0xFFFF) [[likely]] {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char >= output_end) [[unlikely]] {
						result.output_block_ += 1;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 1;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(input_char);
				result.output_block_ += 1;
				return error_t::none;
			} else {
				if constexpr (is_out_mode__normal<Options>() ||
							  is_out_mode__full<Options>()) {
					if (output_char + 1 >= output_end) [[unlikely]] {
						result.output_block_ += 2;
						return error_t::out_overflow;
					}
				}
				if constexpr (is_out_mode__count<Options>()) {
					result.output_block_ += 2;
					return error_t::none;
				}

				output_char[0] = static_cast<Out>(0xD800 | ((input_char - 0x10000) >> 10));
				output_char[1] = static_cast<Out>(0xDC00 | (input_char & 0x3FF));
				result.output_block_ += 2;
				return error_t::none;
			}
		} else if constexpr (utf32_char<Out>) {
			if constexpr (is_out_mode__normal<Options>() ||
						  is_out_mode__full<Options>()) {
				if (output_char >= output_end) [[unlikely]] {
					result.output_block_ += 1;
					return error_t::out_overflow;
				}
			}
			if constexpr (is_out_mode__count<Options>()) {
				result.output_block_ += 1;
				return error_t::none;
			}

			output_char[0] = static_cast<Out>(input_char);
			result.output_block_ += 1;
			return error_t::none;
		}

		return error_t::invalid_unicode;
	}

} // namespace chenc::utf::detail