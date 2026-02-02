#pragma once

#include "chenc/convert/detail/char_to_utf32.hpp"
#include "chenc/convert/detail/utf32_to_char.hpp"

namespace chenc::utf::detail {
	template <any_utf_char In,
			  any_utf_char Out,
			  options_t Options>
	inline constexpr char_result_t<In, Out, Options> char_to_char(const In *const input_char, u64 input_len,
																  Out *const output_char, u64 output_len) noexcept {
		char_result_t<In, Out, Options> result = {};
		auto err1 = char_to_utf32<In, Out, Options>(input_char, input_char + input_len, result);
		if (err1 != error_t::none) [[unlikely]] {
			switch (err1) {
			case error_t::in_truncated: {
			} break;
			case error_t::out_overflow: {
			} break;
			case error_t::invalid_source: {
			} break;
			case error_t::invalid_unicode: {
			} break;
			case error_t::non_characters: {
			} break;
			case error_t::surrogates: {
			} break;
			case error_t::non_shortest: {
			} break;
			}
		}
		auto err2 = utf32_to_char<In, Out, Options>(result.utf32, output_char, output_char + output_len, result);
		if (err1 != error_t::none) [[unlikely]] {
			switch (err1) {
			case error_t::in_truncated: {
			} break;
			case error_t::out_overflow: {
			} break;
			case error_t::invalid_source: {
			} break;
			case error_t::invalid_unicode: {
			} break;
			case error_t::non_characters: {
			} break;
			case error_t::surrogates: {
			} break;
			case error_t::non_shortest: {
			} break;
			}
		}
		return result;
	}
} // namespace chenc::utf::detail

auto u8_to_u16(char *input_char, uint64_t input_len,
			   char16_t *output_char, uint64_t output_len) {
	return chenc::utf::detail::char_to_char<char, char16_t, chenc::utf::options_t{}>(input_char, input_len, output_char, output_len);
}