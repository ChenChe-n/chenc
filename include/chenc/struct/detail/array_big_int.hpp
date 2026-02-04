#pragma once

#include "chenc/core/cpp.hpp"
#include "chenc/core/int128.hpp"
#include "chenc/core/type.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <complex>
#include <concepts>

namespace chenc::Struct::detail {
	template <u64 Bits>
	class array_uint {
	private:
		static_assert(Bits > 0, "array_uint<Bits>: Bits must be > 0");

	public:
		inline static constexpr u64 bits_ = Bits;
		inline static constexpr u64 block_size_ = (bits_ + 63) / 64;
		inline static constexpr u64 back_mask_ = []() {
			if constexpr (Bits % 64 == 0) {
				return u64(-1);
			} else {
				return (u64(1) << (Bits % 64)) - 1;
			}
		}();
		using data_t = std::array<u64, block_size_>;
		inline constexpr void sanitize() noexcept {
			if constexpr (Bits % 64 != 0)
				data().back() &= back_mask_;
		}

	public:
		inline constexpr array_uint() noexcept
			: data_{} {}
		template <std::unsigned_integral T>
		inline constexpr array_uint(T val) noexcept
			: data_{} {
			data_[0] = static_cast<u64>(val);
			sanitize();
		}
		template <std::signed_integral T>
		inline constexpr explicit array_uint(T val) noexcept {
			data_[0] = static_cast<u64>(val);
			if (val < 0) {
				std::fill(data().begin() + 1, data().end(), u64(-1));
			} else {
				std::fill(data().begin() + 1, data().end(), 0);
			}
			sanitize();
		}
		constexpr array_uint(const array_uint &) noexcept = default;

		template <u64 OtherBits>
		inline constexpr explicit(OtherBits > Bits) array_uint(const array_uint<OtherBits> &other) noexcept
			: data_{} {
			constexpr u64 other_blocks = array_uint<OtherBits>::block_size_;
			std::copy_n(other.data().begin(), std::min(block_size_, other_blocks), data_.begin());
			sanitize();
		}

		inline constexpr array_uint &operator=(const array_uint &) noexcept = default;

		template <u64 OtherBits>
		inline constexpr array_uint &operator=(const array_uint<OtherBits> &val) noexcept {
			constexpr u64 other_blocks = array_uint<OtherBits>::block_size_;
			if constexpr (other_blocks < block_size_) {
				std::copy_n(val.data().begin(), other_blocks, data_.begin());
				std::fill(data_.begin() + other_blocks, data_.end(), 0);
			} else {
				std::copy_n(val.data().begin(), block_size_, data_.begin());
			}
			sanitize();
			return *this;
		}

	public:
		template <u64 OtherBits>
		inline constexpr bool operator==(const array_uint<OtherBits> &other) const noexcept {
			constexpr u64 other_blocks = array_uint<OtherBits>::block_size_;
			if constexpr (other_blocks < block_size_) {
				for (u64 i = 0; i < other_blocks; ++i)
					if (data_[i] != other.data()[i])
						return false;
				for (u64 i = other_blocks; i < block_size_; ++i)
					if (data_[i] != 0)
						return false;
			} else {
				for (u64 i = 0; i < block_size_; ++i)
					if (data_[i] != other.data()[i])
						return false;
				for (u64 i = block_size_; i < other_blocks; ++i)
					if (other.data()[i] != 0)
						return false;
			}
			return true;
		}

		template <u64 OtherBits>
		inline constexpr std::strong_ordering operator<=>(const array_uint<OtherBits> &other) const noexcept {
			constexpr u64 other_blocks = array_uint<OtherBits>::block_size_;

			// 1. 差异区比较
			if constexpr (block_size_ > other_blocks) {
				for (i64 i = block_size_ - 1; i >= other_blocks; --i)
					if (data_[static_cast<u64>(i)] != 0)
						return std::strong_ordering::greater;
			} else if constexpr (other_blocks > block_size_) {
				for (i64 i = other_blocks - 1; i >= block_size_; --i)
					if (other.data()[static_cast<u64>(i)] != 0)
						return std::strong_ordering::less;
			}
			// 2. 块比较
			for (i64 i = block_size_ - 1; i >= 0; --i)
				if (data_[static_cast<u64>(i)] != other.data()[static_cast<u64>(i)])
					return data_[static_cast<u64>(i)] <=> other.data()[static_cast<u64>(i)];

			// 3. 相等
			return std::strong_ordering::equal;
		}

	public:
		/**
		 * @brief 高性能大整数加法融合版本
		 */
		template <u64 A_Bits, u64 B_Bits, u64 Result_Bits>
		CHENC_FORCE_INLINE static constexpr void add_to(
			array_uint<Result_Bits> &out,
			const array_uint<A_Bits> &a,
			const array_uint<B_Bits> &b) noexcept {
			// 获取各数组的 64 位分量数量
			constexpr u64 out_b = array_uint<Result_Bits>::block_size_;
			constexpr u64 a_b = array_uint<A_Bits>::block_size_;
			constexpr u64 b_b = array_uint<B_Bits>::block_size_;

			// 计算三个操作数的公共最小长度
			constexpr u64 common = std::min({out_b, a_b, b_b});

			u64 i = 0;
			u64 carry = 0;

			// --- 第一阶段：三方公共部分融合 (a + b + carry) ---
			// 此处是加法的核心热点，目标是生成不带分支的 ADC 指令序列
#ifdef CHENC_COMPILER_MSVC
			// MSVC 环境：使用 u8 承载进位，符合硬件标志位行为
			u8 c_flag = 0;
			for (; i < common; ++i) {
				c_flag = _addcarry_u64(c_flag, a.data()[i], b.data()[i], &out.data()[i]);
			}
			carry = c_flag;
#else
			// GCC/Clang 环境：__int128 被映射为 RDX:RAX 寄存器对，ADC 链生成效果最好
			for (; i < common; ++i) {
				u128 sum = static_cast<u128>(a.data()[i]) +
						   b.data()[i] + carry;
				out.data()[i] = static_cast<u64>(sum);
				carry = static_cast<u64>(sum >> 64);
			}
#endif

			// --- 第二阶段：差异部分处理 (较长的操作数 + carry) ---
			// 如果一个数比另一个长，我们需要继续传播进位
			constexpr u64 src_max = std::min({out_b, a_b, b_b});
			if constexpr (src_max > common) {
				// 静态确定哪一个是更长的源
				const u64 *src_ptr = (a_b > b_b) ? a.data().data() : b.data().data();

				for (; i < src_max; ++i) {
					// 关键优化：如果进位已经耗尽（变为 0），后续位只需直接拷贝
					// 使用 [[likely]] 提示编译器进位通常会很快消失
					if (carry == 0) [[likely]] {
						std::copy_n(src_ptr + i, src_max - i, out.data().begin() + i);
						i = src_max;
						break;
					}

					// 继续加进位
#ifdef CHENC_COMPILER_MSVC
					u8 c_flag = static_cast<u8>(carry);
					c_flag = _addcarry_u64(c_flag, src_ptr[i], 0, &out.data()[i]);
					carry = c_flag;
#else
					u128 sum = static_cast<u128>(src_ptr[i]) + carry;
					out.data()[i] = static_cast<u64>(sum);
					carry = static_cast<u64>(sum >> 64);
#endif
				}
			}

			// --- 第三阶段：高位填充与最终溢出处理 ---
			if constexpr (out_b > src_max) {
				// 如果还有剩余的输出位
				if (i < out_b) {
					// 处理最后一个产生的进位
					out.data()[i++] = carry;

					// 剩下的位填充为 0（例如：128bit = 64bit + 64bit 可能有进位，但也可能剩高位）
					if (i < out_b) {
						std::fill(out.data().begin() + i, out.data().end(), 0);
					}
				}
			}

			// 最后根据特定位宽规则进行清理（如 255bit 整数需屏蔽掉最高位的冗余位）
			out.sanitize();
		}

		template <u64 OtherBits>
		inline constexpr array_uint &operator+=(const array_uint<OtherBits> &other) noexcept {
			add_to(*this, *this, other);
			return *this;
		}
		template <std::unsigned_integral T>
		inline constexpr array_uint &operator+=(T other) noexcept {
			add_to(*this, *this, array_uint<64>(other));
			return *this;
		}

		template <u64 OtherBits>
		inline constexpr auto operator+(const array_uint<OtherBits> &other) const noexcept {
			array_uint<std::max({Bits, OtherBits})> result;
			add_to(result, *this, other);
			return result;
		}
		template <std::unsigned_integral T>
		inline constexpr auto operator+(u64 other) const noexcept {
			array_uint<std::max({Bits, sizeof(T) * 8})> result;
			add_to(result, *this, array_uint<64>(other));
			return result;
		}

		inline constexpr array_uint &operator++() noexcept {
			add_to(*this, *this, array_uint<64>(1));
			return *this;
		}
		inline constexpr array_uint operator++(int) noexcept {
			array_uint result;
			add_to(result, *this, array_uint<64>(1));
			return result;
		}

	public:
		/**
		 * @brief 高性能补码回绕减法 (out = a - b)
		 * 行为与 uint64_t 一致：如果 a < b，结果将产生回绕（高位填充 0xFF）
		 */
		template <u64 A_Bits, u64 B_Bits, u64 Result_Bits>
		CHENC_FORCE_INLINE static constexpr void sub_to(
			array_uint<Result_Bits> &out,
			const array_uint<A_Bits> &a,
			const array_uint<B_Bits> &b) noexcept {

			constexpr u64 out_b = array_uint<Result_Bits>::block_size_;
			constexpr u64 a_b = array_uint<A_Bits>::block_size_;
			constexpr u64 b_b = array_uint<B_Bits>::block_size_;

			// 公共计算部分：三者最小长度
			constexpr u64 common = std::min({out_b, a_b, b_b});

			u64 i = 0;
			u64 borrow = 0;

			// --- 第一阶段：公共部分 (a - b - borrow) ---
#ifdef CHENC_COMPILER_MSVC
			u8 b_flag = 0;
			for (; i < common; ++i) {
				b_flag = _subborrow_u64(b_flag, a.data()[i], b.data()[i], &out.data()[i]);
			}
			borrow = b_flag;
#else
			for (; i < common; ++i) {
				u128 val_a = a.data()[i];
				u128 val_b = static_cast<u128>(b.data()[i]) + borrow;
				out.data()[i] = static_cast<u64>(val_a - val_b);
				borrow = (val_a < val_b) ? 1 : 0;
			}
#endif

			// --- 第二阶段：处理 a 的剩余高位 ---
			if constexpr (out_b > common) {
				// a 还有剩余位需要处理
				if constexpr (a_b > common) {
					constexpr u64 a_limit = std::min(out_b, a_b);
					for (; i < a_limit; ++i) {
						if (borrow == 0) [[likely]] {
							// 借位消除，后续位直接拷贝
							std::copy_n(a.data().data() + i, a_limit - i, out.data().begin() + i);
							i = a_limit;
							break;
						}
#ifdef CHENC_COMPILER_MSVC
						u8 b_flag = static_cast<u8>(borrow);
						b_flag = _subborrow_u64(b_flag, a.data()[i], 0, &out.data()[i]);
						borrow = b_flag;
#else
						u64 val_a = a.data()[i];
						out.data()[i] = val_a - borrow;
						borrow = (val_a < borrow) ? 1 : 0;
#endif
					}
				}

				// --- 第三阶段：回绕填充 (Sign/Borrow Extension) ---
				// 如果 i 还没达到输出长度，说明 a 已经耗尽，但输出还没填满
				if (i < out_b) {
					if (borrow == 0) {
						// 情况 A: 没有借位了，高位全部补 0
						std::fill(out.data().begin() + i, out.data().end(), 0);
					} else {
						// 情况 B: 仍有借位（即 a < b），模拟补码回绕，高位全部填充 0xFF...FF
						// 这对应了 CPU 在执行减法时，借位标志传播导致的连续“借 1”
						std::fill(out.data().begin() + i, out.data().end(), static_cast<u64>(-1));
					}
				}
			}

			// 最后的屏蔽处理，确保不完整字节位符合预期
			out.sanitize();
		}

		template <u64 OtherBits>
		inline constexpr array_uint &operator-=(const array_uint<OtherBits> &other) noexcept {
			sub_to(*this, *this, other);
			return *this;
		}
		template <std::unsigned_integral T>
		inline constexpr array_uint &operator-=(T other) noexcept {
			sub_to(*this, *this, array_uint<64>(other));
			return *this;
		}

		template <u64 OtherBits>
		inline constexpr auto operator-(const array_uint<OtherBits> &other) const noexcept {
			array_uint<std::max({Bits, OtherBits})> result;
			sub_to(result, *this, other);
			return result;
		}
		template <std::unsigned_integral T>
		inline constexpr auto operator-(T other) const noexcept {
			array_uint<std::max({Bits, sizeof(T) * 8})> result;
			sub_to(result, *this, array_uint<64>(other));
			return result;
		}

		inline constexpr array_uint &operator--() noexcept {
			sub_to(*this, *this, array_uint<64>(1));
			return *this;
		}
		inline constexpr array_uint operator--(int) noexcept {
			array_uint result;
			sub_to(result, *this, array_uint<64>(1));
			return result;
		}

	public:
		/**
		 * @brief 高性能左移
		 */
		template <u64 A_Bits, u64 Result_Bits>
		CHENC_FORCE_INLINE static constexpr void left_shift_to(
			array_uint<Result_Bits> &out,
			const array_uint<A_Bits> &a,
			u64 shift) noexcept {

			constexpr u64 out_b = array_uint<Result_Bits>::block_size_;
			constexpr u64 a_b = array_uint<A_Bits>::block_size_;

			if (shift >= Result_Bits) {
				std::fill(out.data().begin(), out.data().end(), 0);
				return;
			}

			const u64 block_shift = shift / 64;
			const u64 bit_shift = shift % 64;
			const u64 copy_len = (block_shift >= out_b) ? 0 : std::min(a_b, out_b - block_shift);

			u64 *dst = out.data().data();
			const u64 *src = a.data().data();

			// 指针重叠检测
			bool is_aliased = false;
			if (!std::is_constant_evaluated()) {
				is_aliased = (dst <= src + a_b) && (src <= dst + out_b);
			} else {
				// 在 constexpr 环境下，我们无法安全比较无关指针，
				// 且 constexpr 内存通常不重叠，或者由编译器处理。
				// 若为了极致安全，假设可能重叠走通用路径。
				is_aliased = true;
			}

			if (bit_shift == 0) {
				if (copy_len > 0) {
					if (is_aliased && (dst + block_shift > src)) {
						// 重叠且目标在源之后：从后往前拷 (类似 memmove)
						std::copy_backward(src, src + copy_len, dst + block_shift + copy_len);
					} else {
						// 不重叠或目标在源之前：从前往后拷
						std::copy(src, src + copy_len, dst + block_shift);
					}
				}
			} else {
				const u64 inv_bit_shift = 64 - bit_shift;
				if (!is_aliased) {
					u64 prev_src = 0;
					for (u64 i = 0; i < copy_len; ++i) {
						u64 cur_src = src[i];
						dst[i + block_shift] = (cur_src << bit_shift) | (prev_src >> inv_bit_shift);
						prev_src = cur_src;
					}
					// 处理溢出的高位
					if (copy_len + block_shift < out_b) {
						dst[copy_len + block_shift] = (prev_src >> inv_bit_shift);
					}
				} else {
					// 重叠路径：逆向遍历确保安全
					for (u64 i = copy_len; i > 0; --i) {
						u64 idx = i - 1;
						u64 cur = src[idx];
						u64 low = (idx > 0) ? (src[idx - 1] >> inv_bit_shift) : 0;
						dst[idx + block_shift] = (cur << bit_shift) | low;
					}
					// 溢出位处理
					if (copy_len + block_shift < out_b) {
						dst[copy_len + block_shift] = (src[copy_len - 1] >> inv_bit_shift);
					}
				}
			}

			// 清零低位块 (代替 memset)
			if (block_shift > 0) {
				std::fill_n(dst, std::min(block_shift, out_b), 0);
			}

			// 清零高位多余块 (代替 memset)
			u64 filled_pos = block_shift + copy_len + (bit_shift != 0 ? 1 : 0);
			if (out_b > filled_pos) {
				std::fill(dst + filled_pos, dst + out_b, 0);
			}

			out.sanitize();
		}

		inline constexpr array_uint &operator<<=(u64 shift) noexcept {
			left_shift_to(*this, *this, shift);
			return *this;
		}

		inline constexpr auto operator<<(u64 shift) const noexcept {
			array_uint<Bits> result;
			left_shift_to(result, *this, shift);
			return result;
		}

	public:
		/**
		 * @brief 高性能右移
		 */
		template <u64 A_Bits, u64 Result_Bits>
		CHENC_FORCE_INLINE static constexpr void right_shift_to(
			array_uint<Result_Bits> &out,
			const array_uint<A_Bits> &a,
			u64 shift) noexcept {

			constexpr u64 out_b = array_uint<Result_Bits>::block_size_;
			constexpr u64 a_b = array_uint<A_Bits>::block_size_;

			if (shift >= A_Bits) {
				std::fill(out.data().begin(), out.data().end(), 0);
				return;
			}

			const u64 block_shift = shift / 64;
			const u64 bit_shift = shift % 64;
			const u64 source_len = a_b - block_shift;
			const u64 copy_len = std::min(source_len, out_b);

			u64 *dst = out.data().data();
			const u64 *src = a.data().data();

			if (bit_shift == 0) {
				if (copy_len > 0) {
					// 右移时 dst 永远在 src 之前或相等，正向 copy 永远安全
					std::copy(src + block_shift, src + block_shift + copy_len, dst);
				}
			} else {
				const u64 inv_bit_shift = 64 - bit_shift;
				// 右移逻辑：dst[i] 依赖 src[i+shift] 和 src[i+shift+1]
				// 顺序遍历对于右移而言天然是安全的（dst 不会覆盖尚未读取的 src 块）
				for (u64 i = 0; i < copy_len; ++i) {
					u64 src_idx = i + block_shift;
					u64 cur = src[src_idx];
					u64 high = (src_idx + 1 < a_b) ? (src[src_idx + 1] << inv_bit_shift) : 0;
					dst[i] = (cur >> bit_shift) | high;
				}
			}

			// 清零高位 (代替 memset)
			if (out_b > copy_len) {
				std::fill(dst + copy_len, dst + out_b, 0);
			}

			out.sanitize();
		}

		inline constexpr array_uint &operator>>=(u64 shift) noexcept {
			right_shift_to(*this, *this, shift);
			return *this;
		}

		inline constexpr auto operator>>(u64 shift) const noexcept {
			array_uint<Bits> result;
			right_shift_to(result, *this, shift);
			return result;
		}

	public:
	public:
	public:
	public:
	public:
		inline constexpr data_t &data() noexcept {
			return data_;
		}
		inline constexpr const data_t &data() const noexcept {
			return data_;
		}

	private:
		data_t data_;
	};

	auto &add_a(array_uint<6400> &a, const array_uint<64000> &b) {
		return a += b;
	}
	auto add_b(const array_uint<6400> &a, const array_uint<64000> &b) {
		return a + b;
	}
	auto &sub_a(array_uint<6400> &a, const array_uint<64000> &b) {
		return a -= b;
	}
	auto sub_b(const array_uint<6400> &a, const array_uint<64000> &b) {
		return a - b;
	}
	auto &lsh_a(array_uint<6400> &a, u64 b) {
		return a <<= b;
	}
	auto lsh_b(const array_uint<6400> &a, u64 b) {
		return a << b;
	}
	auto &rsh_a(array_uint<6400> &a, u64 b) {
		return a >>= b;
	}
	auto rsh_b(const array_uint<6400> &a, u64 b) {
		return a >> b;
	}

} // namespace chenc::Struct::detail