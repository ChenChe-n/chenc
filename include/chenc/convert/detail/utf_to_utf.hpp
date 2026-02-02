#pragma once

#include "chenc/convert/detail/char_to_utf32.hpp"
#include "chenc/convert/detail/utf32_to_char.hpp"

namespace chenc::utf::detail {
	template <options_t Options,
			  any_utf_char In,
			  any_utf_char Out>
	inline constexpr char_result_t<Options, In, Out> char_to_char(const In *const input_char, u64 input_len,
																  Out *const output_char, u64 output_len) noexcept {
		char_result_t<Options, In, Out> result = {};
		auto err1 = char_to_utf32<Options, In, Out>(input_char, input_char + input_len,
													result);
		if (err1 != error_t::none) [[unlikely]] {
			if constexpr (is_error_mode__stop<Options>() ||
						  is_error_mode__skip<Options>()) {
				result.status_ = status_t::error;
				result.error_ = err1;
				return result;
			}
			if constexpr (is_error_mode__replace<Options>()) {
				result.unicode_ = Options.replace_char_;
				result.status_ = status_t::partial;
				result.error_ = err1;
			}
		}
		auto err2 = utf32_to_char<Options, In, Out>(result.unicode_,
													output_char, output_char + output_len,
													result);
		if (err2 != error_t::none) [[unlikely]] {
			result.status_ = status_t::error;
			result.error_ |= err2;
		}
		return result;
	}
	template <options_t Options,
			  any_utf_char In,
			  any_utf_char Out>
	inline constexpr str_result_t<Options, In, Out> str_to_str(const In *const input_str, u64 input_len,
															   Out *const output_str, u64 output_len) noexcept {
		char_result_t<Options, In, Out> char_result = {};
		str_result_t<Options, In, Out> result = {};
		const In *in_str = input_str;
		const In *const in_end = input_str + input_len;
		Out *out_str = output_str;
		const Out *const out_end = output_str + output_len;
		while (in_str < in_end) {
			if constexpr (is_perf_mode__fast_ascii<Options>()) {
				if ((in_str + (8 / sizeof(In)) <= in_end) & (out_str + (8 / sizeof(Out)) <= out_end)) [[likely]] {
					alignas(8) union {
						u64 c64;
						u8 c8[8];
						u16 c16[4];
						u32 c32[2];
					} data;
					if constexpr (utf8_char<In>) {
						for (u64 i = 0; i < 8; i++)
							data.c8[i] = static_cast<u8>(in_str[i]);
						if ((data.c64 & 0x8080'8080'8080'8080) == 0) {
							for (u64 i = 0; i < 8; i++)
								out_str[i] = static_cast<Out>(data.c8[i]);
							in_str += 8;
							out_str += 8;
							continue;
						}
					}
					if constexpr (utf16_char<In>) {
						for (u64 i = 0; i < 4; i++)
							data.c8[i] = static_cast<u16>(in_str[i]);
						if ((data.c64 & 0xFF80'FF80'FF80'FF80) == 0) {
							for (u64 i = 0; i < 4; i++)
								out_str[i] = static_cast<Out>(data.c8[i]);
							in_str += 4;
							out_str += 4;
							continue;
						}
					}
					if constexpr (utf32_char<In>) {
						for (u64 i = 0; i < 2; i++)
							data.c8[i] = static_cast<u32>(in_str[i]);
						if ((data.c64 & 0xFFFF'FF80'FFFF'FF80) == 0) {
							for (u64 i = 0; i < 2; i++)
								out_str[i] = static_cast<Out>(data.c8[i]);
							in_str += 2;
							out_str += 2;
							continue;
						}
					}
				}
			}

			error_t err1 = char_to_utf32<Options, In, Out>(in_str, in_end,
														   char_result);
			switch (err1) {
			case error_t::none: { // 正常转换
				result.conv_normal_char_count_ += 1;
				result.input_block_count_ += char_result.input_block_;
			} break;
			case error_t::in_truncated: { // 输入被截断
				result.conv_error_char_count_ += 1;
				result.input_block_count_ += char_result.input_block_;
				result.status_ = status_t::error;
				result.error_ |= error_t::in_truncated;
				return result;
			} break;
			case error_t::invalid_source:  // 非法输入
			case error_t::invalid_unicode: // 超过 0x10FFFF
			case error_t::non_characters:  // 非字符
			case error_t::surrogates:	   // 代理对
			case error_t::non_shortest: {  // 非最短编码
				result.conv_error_char_count_ += 1;
				result.error_ |= error_t::in_truncated;
				if constexpr (is_error_mode__stop<Options>()) {
					result.status_ = status_t::error;
					result.input_block_count_ += char_result.input_block_;
					return result;
				}
				if constexpr (is_error_mode__skip<Options>()) {
					result.status_ = status_t::partial;
				}
				if constexpr (is_error_mode__replace<Options>()) {
					result.unicode_ = Options.replace_char_;
					result.status_ = status_t::partial;
				}
			}
			}
			in_str += char_result.input_block_;

			error_t err2 = utf32_to_char<Options, In, Out>(char_result.unicode_,
														   out_str, out_end,
														   char_result);
			switch (err2) {
			case error_t::none: { // 正常转换
				if constexpr (is_out_mode__count<Options>()) {
					result.need_output_block_count_ += char_result.output_block_;
				}
				if constexpr (is_out_mode__none_check_buffer<Options>()) {
					result.output_block_count_ += char_result.output_block_;
				}
				if constexpr (is_out_mode__full<Options>()) {
					result.output_block_count_ += char_result.output_block_;
					result.need_output_block_count_ += char_result.output_block_;
				}
			} break;
			case error_t::out_overflow: { // 输出被截断
				if constexpr (is_out_mode__normal<Options>()) {
					result.output_block_count_ += char_result.output_block_;
					result.error_ |= error_t::out_overflow;
					result.status_ = status_t::error;
					return result;
				}
				if constexpr (is_out_mode__full<Options>()) {
					result.need_output_block_count_ += char_result.output_block_;
				}
			} break;
			}
			out_str += char_result.output_block_;
			char_result = {};
		}
		return result;
	}
} // namespace chenc::utf::detail

// auto u8_to_u16(char *input_char, uint64_t input_len,
// 			   char16_t *output_char, uint64_t output_len) {
// 	return chenc::utf::detail::char_to_char<chenc::utf::options_t{}>(input_char, input_len, output_char, output_len);
// }
auto u8s_to_u16s(char *input_char, uint64_t input_len,
				 char16_t *output_char, uint64_t output_len) {
	return chenc::utf::detail::str_to_str<chenc::utf::options_t{}>(input_char, input_len, output_char, output_len);
}
