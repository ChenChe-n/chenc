#pragma once

#include "chenc/convert/utf_char.hpp"
#include "chenc/convert/utf_opt.hpp"
#include "chenc/core/cpp.hpp"
#include "chenc/core/type.hpp"

#include <array>
#include <bit>

namespace chenc::utf::detail {
    /**
     * @brief 探测 UTF 数据的字节序
     * @return std::endian::little 或 std::endian::big
     * @note 结合 BOM、代理对校验、以及加权统计实现防御级探测
     */
    template <any_utf_char In>
    inline constexpr std::endian utf_endian(const In *const input_char, const In *const input_end) noexcept {
        if constexpr (utf8_char<In>) {
            return std::endian::native;
        }

        const u64 len = static_cast<u64>(input_end - input_char);
        if (len == 0) return std::endian::native;

        // --- UTF-16 防御性探测 ---
        if constexpr (utf16_char<In>) {
            // 1. BOM 识别 (最高优先级)
            const u16 first = static_cast<u16>(input_char[0]);
            if (first == 0xFEFF) return std::endian::native;
            if (first == 0xFFFE) return (std::endian::native == std::endian::little) ? std::endian::big : std::endian::little;

            // 2. 统计与安全性校验
            u64 score_le = 0;
            u64 score_be = 0;
            u64 sample_size = (len < 256) ? len : 256;

            for (u64 i = 0; i < sample_size; i++) {
                const u16 val = static_cast<u16>(input_char[i]);
                
                // --- 安全性：代理对识别 (Surrogate Check) ---
                // UTF-16 代理对范围: 高代理 0xD800-0xDBFF, 低代理 0xDC00-0xDFFF
                auto is_surrogate = [](u16 v) { return (v & 0xF800) == 0xD800; };
                
                // 如果当前读取的值是一个代理项
                if (is_surrogate(val)) {
                    // 检查它是否符合当前机器字节序下的逻辑
                    // 逻辑：如果 val 是合法代理项，则加分；
                    // 如果翻转后的 byteswap(val) 才是合法代理项，说明字节序极大概率反了。
                    if (is_surrogate(std::byteswap(val))) {
                        // 这种情况极罕见（一个16位值翻转后依然在代理区），通常意味着字节序错误
                        if constexpr (std::endian::native == std::endian::little) score_be += 32;
                        else score_le += 32;
                    } else {
                        // 当前解析是合法的代理项
                        if constexpr (std::endian::native == std::endian::little) score_le += 8;
                        else score_be += 8;
                    }
                }

                // --- 统计学：ASCII 锚点探测 ---
                const bool low_is_zero = (val & 0x00FF) == 0;
                const bool high_is_zero = (val & 0xFF00) == 0;

                if (low_is_zero ^ high_is_zero) {
                    const u8 other_byte = high_is_zero ? static_cast<u8>(val & 0xFF) : static_cast<u8>(val >> 8);
                    u32 weight = 1;
                    // 对常见文本定界符加权
                    if (other_byte == 0x20 || other_byte == 0x0A || other_byte == 0x0D || other_byte == 0x09) {
                        weight = 16;
                    }

                    if constexpr (std::endian::native == std::endian::little) {
                        if (high_is_zero) score_le += weight; else score_be += weight;
                    } else {
                        if (low_is_zero) score_le += weight; else score_be += weight;
                    }
                }
            }

            if (score_le > score_be) return std::endian::little;
            if (score_be > score_le) return std::endian::big;
            return std::endian::native;
        }

        // --- UTF-32 防御性探测 ---
        if constexpr (utf32_char<In>) {
            const u32 first = static_cast<u32>(input_char[0]);
            if (first == 0x0000FEFF) return std::endian::native;
            if (first == 0xFEFF0000) return (std::endian::native == std::endian::little) ? std::endian::big : std::endian::little;

            u64 score_native = 0;
            u64 score_swapped = 0;
            u64 sample_size = (len < 64) ? len : 64;

            for (u64 i = 0; i < sample_size; ++i) {
                const u32 val = static_cast<u32>(input_char[i]);
                
                // 1. 码点范围检查 (0 - 0x10FFFF)
                if (val <= 0x10FFFF) score_native += 10;
                if (std::byteswap(val) <= 0x10FFFF) score_swapped += 10;

                // 2. 禁用区检查 (Non-characters & Surrogates in UTF-32)
                // UTF-32 中不应出现 0xD800-0xDFFF 之间的值
                auto is_invalid_u32 = [](u32 v) { 
                    return (v >= 0xD800 && v <= 0xDFFF) || (v > 0x10FFFF); 
                };
                
                if (is_invalid_u32(val)) score_native = (score_native > 20) ? score_native - 20 : 0;
                if (is_invalid_u32(std::byteswap(val))) score_swapped = (score_swapped > 20) ? score_swapped - 20 : 0;
            }

            if (score_native > score_swapped) return std::endian::native;
            if (score_swapped > score_native) return (std::endian::native == std::endian::little) ? std::endian::big : std::endian::little;
            return std::endian::native;
        }

        return std::endian::native;
    }
}