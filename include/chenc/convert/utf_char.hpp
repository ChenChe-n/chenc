#pragma once

#include "chenc/core/type.hpp"

#include <concepts>

namespace chenc::utf {
	/**
	 * @brief 基础约束：必须是整数类型，且排除 bool。
	 * 涵盖了 char, char8_t, char16_t, char32_t, wchar_t 以及标准整数类型。
	 */
	template <typename T>
	concept utf_byte_requirement = std::is_integral_v<T> && !std::is_same_v<T, bool>;

	// --- 核心 Concept ---

	/**
	 * @brief c8 概念：限定 1 字节长度的字符/整数
	 */
	template <typename T>
	concept utf8_char = utf_byte_requirement<T> && sizeof(T) == 1;

	/**
	 * @brief c16 概念：限定 2 字节长度的字符/整数
	 * 在 Windows 上会自动包含 wchar_t
	 */
	template <typename T>
	concept utf16_char = utf_byte_requirement<T> && sizeof(T) == 2;

	/**
	 * @brief c32 概念：限定 4 字节长度的字符/整数
	 * 在 Linux/macOS 上会自动包含 wchar_t
	 */
	template <typename T>
	concept utf32_char = utf_byte_requirement<T> && sizeof(T) == 4;

	/**
	 * @brief 综合概念：符合任意一种 UTF 字符宽度要求
	 */
	template <typename T>
	concept any_utf_char = utf8_char<T> || utf16_char<T> || utf32_char<T>;
} // namespace chenc::utf