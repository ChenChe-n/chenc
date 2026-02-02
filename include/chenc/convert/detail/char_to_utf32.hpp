#pragma once

#include "chenc/convert/utf_char.hpp"
#include "chenc/convert/utf_opt.hpp"
#include "chenc/core/cpp.hpp"
#include "chenc/core/type.hpp"

#include <algorithm>
#include <array>

namespace chenc::utf::detail {

	/**
	 * @brief utfX 转换为 utf32
	 * @note 内部实现，请勿调用
	 * @note result.unicode_ 存储转换结果
	 * @note result.input_block_ 存储期望的输入块数
	 */
	template <options_t Options,
			  any_utf_char In,
			  any_utf_char Out>
	CHENC_FORCE_INLINE constexpr error_t char_to_utf32(const In *const input_char, const In *const input_end,
										   char_result_t<Options, In, Out> &result) noexcept {
		// 确保至少1个块
		if constexpr (is_input_mode__normal<Options>())
			if (input_char >= input_end) [[unlikely]] {
				result.input_block_ += 1;
				return error_t::in_truncated;
			}
		if constexpr (utf8_char<In>) {
			// 读取输入字符
			u8 c1 = static_cast<u8>(input_char[0]);
			if (c1 < 0x80) [[likely]] { // 1字节utf8字符
				// 读取字符
				u32 unicode = static_cast<u32>(input_char[0]);
				// 存储字符
				result.input_block_ += 1;
				result.unicode_ = unicode;
				return error_t::none;
			} else if ((c1 & 0xE0) == 0xC0) [[likely]] { // 2字节utf8字符
				// 确保至少2个块
				if constexpr (is_input_mode__normal<Options>())
					if (input_char + 1 >= input_end) [[unlikely]] {
						result.input_block_ += 2;
						return error_t::in_truncated;
					}
				// 读取字符
				u8 bytes[2];
				bytes[0] = static_cast<u8>(input_char[0]);
				bytes[1] = static_cast<u8>(input_char[1]);
				if constexpr (is_char_mode__strict<Options>() || is_char_mode__compatible<Options>()) {
					if (((bytes[0] & 0xE0) != 0xC0) |
						((bytes[1] & 0xC0) != 0x80)) [[unlikely]] {
						result.input_block_ += 2;
						return error_t::invalid_source;
					}
				}
				u32 unicode = (static_cast<u32>(bytes[0] & 0x1F) << 6) |
							  static_cast<u32>(bytes[1] & 0x3F);
				// 验证编码有效性
				if constexpr (is_char_mode__strict<Options>())
					if (unicode < 0x80) [[unlikely]] {
						result.input_block_ += 2;
						return error_t::non_shortest;
					}
				// 存储字符
				result.input_block_ += 2;
				result.unicode_ = unicode;
				return error_t::none;
			} else if ((c1 & 0xF0) == 0xE0) [[likely]] { // 3字节utf8字符
				// 确保至少3个块
				if constexpr (is_input_mode__normal<Options>())
					if (input_char + 2 >= input_end) [[unlikely]] {
						result.input_block_ += 3;
						return error_t::in_truncated;
					}
				// 读取字符
				u8 bytes[3];
				bytes[0] = static_cast<u8>(input_char[0]);
				bytes[1] = static_cast<u8>(input_char[1]);
				bytes[2] = static_cast<u8>(input_char[2]);
				if constexpr (is_char_mode__strict<Options>() || is_char_mode__compatible<Options>()) {
					if (((bytes[0] & 0xF0) != 0xE0) |
						((bytes[1] & 0xC0) != 0x80) |
						((bytes[2] & 0xC0) != 0x80)) [[unlikely]] {
						result.input_block_ += 3;
						return error_t::invalid_source;
					}
				}
				u32 unicode = (static_cast<u32>(bytes[0] & 0x0F) << 12) |
							  (static_cast<u32>(bytes[1] & 0x3F) << 6) |
							  static_cast<u32>(bytes[2] & 0x3F);
				// 验证编码有效性
				if constexpr (is_char_mode__strict<Options>()) {
					if (unicode < 0x800) [[unlikely]] { // 过短字符
						result.input_block_ += 3;
						return error_t::non_shortest;
					}
					if ((unicode >= 0xD800) && (unicode <= 0xDFFF)) [[unlikely]] { // 代理对
						result.input_block_ += 3;
						return error_t::surrogates;
					}
					if (((unicode >= 0xFDD0) && (unicode <= 0xFDEF)) || // 非字符
						((unicode & 0xFFFE) == 0xFFFE)) [[unlikely]] {
						result.input_block_ += 3;
						return error_t::non_characters;
					}
				}
				// 存储字符
				result.input_block_ += 3;
				result.unicode_ = unicode;
				return error_t::none;
			} else if ((c1 & 0xF8) == 0xF0) [[likely]] { // 4字节utf8字符
				// 确保至少4个块
				if constexpr (is_input_mode__normal<Options>())
					if (input_char + 3 >= input_end) [[unlikely]] {
						result.input_block_ += 4;
						return error_t::in_truncated;
					}
				// 读取字符
				u8 bytes[4];
				bytes[0] = static_cast<u8>(input_char[0]);
				bytes[1] = static_cast<u8>(input_char[1]);
				bytes[2] = static_cast<u8>(input_char[2]);
				bytes[3] = static_cast<u8>(input_char[3]);
				if constexpr (is_char_mode__strict<Options>() || is_char_mode__compatible<Options>()) {
					if (((bytes[0] & 0xF8) != 0xF0) |
						((bytes[1] & 0xC0) != 0x80) |
						((bytes[2] & 0xC0) != 0x80) |
						((bytes[3] & 0xC0) != 0x80)) [[unlikely]] {
						result.input_block_ += 4;
						return error_t::invalid_source;
					}
				}
				u32 unicode = (static_cast<u32>(bytes[0] & 0x07) << 18) |
							  (static_cast<u32>(bytes[1] & 0x3F) << 12) |
							  (static_cast<u32>(bytes[2] & 0x3F) << 6) |
							  static_cast<u32>(bytes[3] & 0x3F);
				// 验证编码有效性
				if constexpr (is_char_mode__strict<Options>()) {
					if ((unicode >= 0xD800) && (unicode <= 0xDFFF)) [[unlikely]] { // 代理对
						result.input_block_ += 4;
						return error_t::surrogates;
					}
					if (((unicode >= 0xFDD0) && (unicode <= 0xFDEF)) || // 非字符
						((unicode & 0xFFFE) == 0xFFFE)) [[unlikely]] {
						result.input_block_ += 4;
						return error_t::non_characters;
					}
					if (unicode > 0x10FFFF) [[unlikely]] { // 超出最大范围的码位
						result.input_block_ += 4;
						return error_t::invalid_unicode;
					}
					if (unicode < 0x10000) [[unlikely]] { // 过短字符
						result.input_block_ += 4;
						return error_t::non_shortest;
					}
				}
				if constexpr (is_char_mode__compatible<Options>()) {
					if (unicode > 0x10FFFF) [[unlikely]] { // 超出最大范围的码位
						result.input_block_ += 4;
						return error_t::invalid_unicode;
					}
				}
				// 存储字符
				result.input_block_ += 4;
				result.unicode_ = unicode;
				return error_t::none;
			} else { // 非法字符
				result.input_block_ += 1;
				return error_t::invalid_source;
			}
		} else if constexpr (utf16_char<In>) {
			// 读取字符
			u32 unicode = static_cast<u32>(input_char[0]);
			// BMP 字符
			if ((unicode < 0xD800) || (unicode > 0xDFFF)) [[likely]] {
				// 验证编码有效性
				if constexpr (is_char_mode__strict<Options>()) {
					if (((unicode >= 0xFDD0) && (unicode <= 0xFDEF)) || // 非字符
						((unicode & 0xFFFE) == 0xFFFE)) [[unlikely]] {
						result.input_block_ += 1;
						return error_t::non_characters;
					}
				}
				// 存储字符
				result.input_block_ += 1;
				result.unicode_ = unicode;
				return error_t::none;
			}
			// 高代理
			if (unicode <= 0xDBFF) [[likely]] {
				if (input_char + 1 >= input_end) [[unlikely]] {
					result.input_block_ += 2;
					return error_t::in_truncated;
				}
				u16 c2 = static_cast<u16>(input_char[1]);
				if constexpr (is_char_mode__strict<Options>() || is_char_mode__compatible<Options>()) {
					if ((c2 < 0xDC00) || (c2 > 0xDFFF)) [[unlikely]] { // 非代理对
						result.input_block_ += 2;
						return error_t::invalid_source;
					}
				}
				unicode = (((unicode & 0x03FF) << 10) |
						   (c2 & 0x03FF)) +
						  0x10000;

				// 验证编码有效性
				if constexpr (is_char_mode__strict<Options>()) {
					if (unicode < 0x10000) [[unlikely]] { // 过短字符
						result.input_block_ += 2;
						return error_t::non_shortest;
					}
					if ((unicode >= 0xD800) && (unicode <= 0xDFFF)) [[unlikely]] { // 代理对
						result.input_block_ += 2;
						return error_t::surrogates;
					}
					if ((unicode & 0xFFFE) == 0xFFFE) [[unlikely]] { // 非字符
						result.input_block_ += 2;
						return error_t::non_characters;
					}
				}
				// 存储字符
				result.input_block_ += 2;
				result.unicode_ = unicode;
				return error_t::none;
			}
			// 低代理
			result.input_block_ += 1;
			return error_t::invalid_source;
		} else if constexpr (utf32_char<In>) {
			// 读取字符
			u32 unicode = static_cast<u32>(input_char[0]);
			// 验证编码有效性
			// 验证编码有效性
			if constexpr (is_char_mode__strict<Options>()) {
				if ((unicode >= 0xD800) && (unicode <= 0xDFFF)) [[unlikely]] { // 代理对
					result.input_block_ += 1;
					return error_t::surrogates;
				}
				if (((unicode >= 0xFDD0) && (unicode <= 0xFDEF)) || // 非字符
					((unicode & 0xFFFE) == 0xFFFE)) [[unlikely]] {
					result.input_block_ += 1;
					return error_t::non_characters;
				}
				if (unicode > 0x10FFFF) [[unlikely]] { // 超出最大范围的码位
					result.input_block_ += 1;
					return error_t::invalid_unicode;
				}
			}
			if constexpr (is_char_mode__compatible<Options>()) {
				if (unicode > 0x10FFFF) [[unlikely]] { // 超出最大范围的码位
					result.input_block_ += 1;
					return error_t::invalid_unicode;
				}
			}
			// 存储字符
			result.input_block_ += 1;
			result.unicode_ = static_cast<u32>(input_char[0]);
			return error_t::none;
		}
	}

} // namespace chenc::utf::detail