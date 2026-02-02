#pragma once

#include "chenc/convert/utf_char.hpp"
#include "chenc/core/enum.hpp"
#include "chenc/core/type.hpp"
#include <concepts>

namespace chenc::utf {

	/**
	 * @brief UTF 转换配置选项
	 */
	struct options_t {
		enum class char_mode : u8 {
			strict,		// 严格模式：拒绝非法码点和非最短路径编码
			compatible, // 兼容模式：允许某些非标准序列
			none		// 无检查模式：追求极致速度，信任输入
		};
		enum class error_mode : u8 {
			stop,	// 遇到错误停止
			skip,	// 跳过非法序列
			replace // 使用 replace_char_ 替换非法序列
		};
		enum class input_mode : u8 {
			normal,			   // 正常检查输入边界
			none_check_buffer, // 假定输入缓冲区足够大（例如已知长度的内存块）
		};
		enum class out_mode : u8 {
			normal,			   // 缓冲区不足立刻停止, 不统计需求输出单元数
			full,			   // 缓冲区内正常输出，缓冲区外仅统计
			none_check_buffer, // 假定输出缓冲区足够大（极致性能）
			count			   // 仅统计，不进行实际写入
		};
		enum class perf_mode : u8 {
			normal,
			fast_ascii // 优化 ASCII 路径
		};

		char_mode char_mode_ = char_mode::strict;
		error_mode error_mode_ = error_mode::stop;
		out_mode out_mode_ = out_mode::normal;
		input_mode input_mode_ = input_mode::normal;
		perf_mode perf_mode_ = perf_mode::normal;
		u32 replace_char_ = 0xFFFD;

		inline constexpr options_t() = default;

		template <typename... Args>
		inline constexpr options_t(Args... args) { (apply_arg(args), ...); }

	private:
		inline constexpr void apply_arg(char_mode m) { char_mode_ = m; }
		inline constexpr void apply_arg(error_mode m) { error_mode_ = m; }
		inline constexpr void apply_arg(out_mode m) { out_mode_ = m; }
		inline constexpr void apply_arg(input_mode m) { input_mode_ = m; }
		inline constexpr void apply_arg(perf_mode m) { perf_mode_ = m; }
		inline constexpr void apply_arg(u32 c) { replace_char_ = c; }
	};

	template <options_t Options>
	inline constexpr bool is_char_mode__strict() noexcept { return Options.char_mode_ == char_mode::strict; }
	template <options_t Options>
	inline constexpr bool is_char_mode__compatible() noexcept { return Options.char_mode_ == char_mode::compatible; }
	template <options_t Options>
	inline constexpr bool is_char_mode__none() noexcept { return Options.char_mode_ == char_mode::none; }
	template <options_t Options>
	inline constexpr bool is_error_mode__stop() noexcept { return Options.error_mode_ == error_mode::stop; }
	template <options_t Options>
	inline constexpr bool is_error_mode__skip() noexcept { return Options.error_mode_ == error_mode::skip; }
	template <options_t Options>
	inline constexpr bool is_error_mode__replace() noexcept { return Options.error_mode_ == error_mode::replace; }
	template <options_t Options>
	inline constexpr bool is_input_mode__normal() noexcept { return Options.input_mode_ == input_mode::normal; }
	template <options_t Options>
	inline constexpr bool is_input_mode__none_check_buffer() noexcept { return Options.input_mode_ == input_mode::none_check_buffer; }
	template <options_t Options>
	inline constexpr bool is_out_mode__normal() noexcept { return Options.out_mode_ == out_mode::normal; }
	template <options_t Options>
	inline constexpr bool is_out_mode__full() noexcept { return Options.out_mode_ == out_mode::full; }
	template <options_t Options>
	inline constexpr bool is_out_mode__none_check_buffer() noexcept { return Options.out_mode_ == out_mode::none_check_buffer; }
	template <options_t Options>
	inline constexpr bool is_out_mode__count() noexcept { return Options.out_mode_ == out_mode::count; }
	template <options_t Options>
	inline constexpr bool is_perf_mode__normal() noexcept { return Options.perf_mode_ == perf_mode::normal; }
	template <options_t Options>
	inline constexpr bool is_perf_mode__fast_ascii() noexcept { return Options.perf_mode_ == perf_mode::fast_ascii; }

	// 常用预设
	static constexpr options_t default_opt{};
	static constexpr options_t fast_opt{
		options_t::char_mode::none,
		options_t::out_mode::none_check_buffer,
		options_t::input_mode::none_check_buffer};

	enum class status_t : u8 {
		ok,		 // 转换完成或正常
		partial, // 部分完成（如输出缓冲区满，但在 full 模式下继续统计）
		error,	 // 发生致命错误停止
	};

	enum class error_t : u8 {
		none = 0,
		in_truncated = 1 << 0,	  // 输入序列不完整
		out_overflow = 1 << 1,	  // 输出缓冲区已满
		invalid_source = 1 << 2,  // 非法字节序列
		invalid_unicode = 1 << 3, // 码位超出范围
		non_characters = 1 << 4,  // 非字符
		surrogates = 1 << 5,	  // 代理码点
		non_shortest = 1 << 6,	  // 非最短编码
	};
	CHENC_CREATE_ENUM_FUNC(error_t)

	/**
	 * @brief UTF 单字符转换输出结果
	 */
	template <any_utf_char In,
			  any_utf_char Out,
			  options_t Options>
	struct alignas(8) char_result_t {
		using in_char_t = In;	   // 输入字符类型
		using out_char_t = Out;	   // 输出字符类型
		using options_t = Options; // 配置选项

		u8 input_block_ = 0;  // 消耗的输入单元数
		u8 output_block_ = 0; // 产生的输出单元数
		status_t status_ = status_t::ok;
		error_t error_ = error_t::none;
		u32 unicode_ = 0; // 转换后的原始码点

		inline constexpr explicit operator bool() const noexcept {
			return (status_ == status_t::ok) &&
				   (error_ == error_t::none);
		}
	};

	/**
	 * @brief UTF 字符串转换输出结果
	 */
	template <any_utf_char In,
			  any_utf_char Out,
			  options_t Options>
	struct alignas(64) str_result_t {
		using in_char_t = In;	   // 输入字符类型
		using out_char_t = Out;	   // 输出字符类型
		using options_t = Options; // 配置选项

		u64 input_block_count_ = 0;		  // 处理的输入单元总数
		u64 output_block_count_ = 0;	  // 写入的输出单元总数
		u64 conv_normal_char_count_ = 0;  // 成功转换的字符数
		u64 conv_error_char_count_ = 0;	  // 错误字符数（replace/skip 模式下有效）
		u64 first_error_index_ = 0;		  // 第一个错误的输入位置
		u64 need_output_block_count_ = 0; // 目标编码所需的总输出单元数

		status_t status_ = status_t::ok;
		error_t error_ = error_t::none;

		inline constexpr explicit operator bool() const noexcept {
			return (status_ == status_t::ok) &&
				   (error_ == error_t::none);
		}
	};

} // namespace chenc::utf