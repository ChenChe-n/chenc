#pragma once

#include "chenc/vm/core.hpp"

#define cc_load_regx_u(reg) \
	const u64 reg##u = data.reg_.load(reg)

#define cc_load_reg_s(reg) \
	const i64 reg##s = (i64)(data.reg_.load(reg))

#define cc_store_reg(reg, val) \
	data.reg_.store((reg), std::bit_cast<u64>(val))

#define cc_load_imm_u(imm_bits) \
	const u64 imm##imm_bits##u = data.code_.imm##imm_bits()
#define cc_load_imm_s(imm_bits) \
	const u64 imm##imm_bits##s = sign_ext<imm_bits>(data.code_.imm##imm_bits())
#define cc_load_imm_slice_u(imm_bits, start, end)  \
	const u64 imm##imm_bits##_##start##_##end##u = \
		(data.code_.imm##imm_bits() >> (start)) & ((u64(1) << ((end) - (start))) - 1)

#define cc_load_imm_slice_s(imm_bits, start, end)  \
	const i64 imm##imm_bits##_##start##_##end##s = \
		sign_ext<((end) - (start))>((data.code_.imm##imm_bits() >> (start)) & ((u64(1) << ((end) - (start))) - 1))
#define cc_stack_push(byte, val)                                \
	{                                                           \
		const u64 cur_sp = data.reg_.load(sp);                  \
		const u64 new_sp = cur_sp - (byte);                     \
		data.ram_.store<byte>(new_sp, std::bit_cast<u64>(val)); \
		data.reg_.store(sp, new_sp);                            \
	}

#define cc_stack_pop_u(byte)                       \
	[](vm_data &d) -> u64 {                        \
		const u64 cur_sp = d.reg_.load(sp);        \
		const u64 val = d.ram_.load<byte>(cur_sp); \
		d.reg_.store(sp, cur_sp + (byte));         \
		return val;                                \
	}(data)

#define cc_stack_pop_s(byte) \
	(i64) cc_stack_pop_u(byte)

#define cc_load_reg0_u() \
	const u64 reg0 = data.reg_.load(data.code_.reg0())
#define cc_load_reg0_s() \
	const i64 reg0 = (i64)cc_load_reg0_u()
#define cc_load_reg1_u() \
	const u64 reg1 = data.reg_.load(data.code_.reg1())
#define cc_load_reg1_s() \
	const i64 reg1 = (i64)cc_load_reg1_u()
#define cc_load_reg2_u() \
	const u64 reg2 = data.reg_.load(data.code_.reg2())
#define cc_load_reg2_s() \
	const i64 reg2 = (i64)cc_load_reg2_u()
#define cc_load_reg3_u() \
	const u64 reg3 = data.reg_.load(data.code_.reg3())
#define cc_load_reg3_s() \
	const i64 reg3 = (i64)cc_load_reg3_u()

#define cc_load_code_reg0() \
	const u64 r0idx = data.code_.reg0()
#define cc_load_code_reg1() \
	const u64 r1idx = data.code_.reg1()
#define cc_load_code_reg2() \
	const u64 r2idx = data.code_.reg2()
#define cc_load_code_reg3() \
	const u64 r3idx = data.code_.reg3()

#define cc_load_memory_u(byte) \
	data.ram_.load<byte>(addr)
#define cc_load_memory_s(byte) \
	sign_ext<byte * 8>(data.ram_.load<byte>(addr))
#define cc_store_memory(byte, addr, val) \
	data.ram_.store<byte>(addr, std::bit_cast<u64>(val))

namespace chenc::vm::detail {

}

namespace chenc::vm::detail::func {


	inline void
	halt_r0i1(vm_data &data) noexcept {
		data.run_type_ = vm_run_type::stop;
	}
	inline void nop__r0i1(vm_data &data) noexcept {
		// nop
	}

	inline void jump_r0i1(vm_data &data) noexcept {
		cc_load_imm_s(21);
		cc_store_reg(pc, imm21s * 4);
	}
	inline void jump_offset_r0i1(vm_data &data) noexcept {
		cc_load_imm_s(21);
		cc_load_reg_s(pc);
		cc_store_reg(pc, pc + imm21s * 4);
	}
	inline void immu_load21b_r0i1(vm_data &data) noexcept {
		cc_load_imm_u(21);
		cc_store_reg(t0, imm21u);
	}
	inline void imms_load21b_r0i1(vm_data &data) noexcept {
		cc_load_imm_s(21);
		cc_store_reg(t0, imm21s);
	}
	inline void immu_load16b_or_shift_r0i1(vm_data &data) noexcept {
		cc_load_imm_slice_u(21, 0, 16);
		cc_load_imm_slice_u(21, 16, 21);
		cc_load_regx_u(t0);
		cc_store_reg(t0, t0 | (imm21_0_16u << imm21_16_21u));
	}
	inline void immu_load16b_or_shift32_r0i1(vm_data &data) noexcept {
		cc_load_imm_slice_u(21, 0, 16);
		cc_load_imm_slice_u(21, 16, 21);
		cc_load_regx_u(t0);
		cc_store_reg(t0, t0 | (imm21_0_16u << (imm21_16_21u + 32)));
	}

	inline void syscall_r0i1(vm_data &data) noexcept {
		cc_load_imm_u(21);
		if (data.syscall_table_.size() <= imm21u ||
			data.syscall_table_[imm21u] == nullptr) {
			// TODO: error
		}
		data.syscall_table_[imm21u](data);
	}
	inline void call_r0i1(vm_data &data) noexcept {
		cc_load_regx_u(pc);
		cc_load_regx_u(fp);
		cc_load_imm_s(21);

		// 1. 压栈返回地址和旧帧指针
		cc_stack_push(8, pcu);
		cc_stack_push(8, fpu);

		// 2. 更新 fp 为当前的 sp
		data.reg_.store(fp, data.reg_.load(sp));

		// 3. 跳转到目标地址
		cc_store_reg(pc, pc + (imm21s * 4));
	}

	inline void return_r0i1(vm_data &data) noexcept {
		// 1. 恢复栈指针到当前帧起始位置
		cc_load_regx_u(fp);
		cc_store_reg(sp, fpu);

		// 2. 依次弹出 fp 和 pc
		cc_store_reg(fp, cc_stack_pop_u(8));
		cc_store_reg(pc, cc_stack_pop_u(8));
	}

	template <u64 Byte>
	inline void loadu_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_code_reg0();
		cc_load_reg1_u();
		cc_load_reg2_u();
		cc_load_reg3_u();
		cc_store_reg(r0idx, cc_load_memory_u<Byte>(reg1 * reg2 + reg3));
	}
	template <u64 Byte>
	inline void loads_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_code_reg0();
		cc_load_reg1_u();
		cc_load_reg2_u();
		cc_load_reg3_u();
		cc_store_reg(r0idx, sign_ext<Byte * 8>(cc_load_memory_u<Byte>(reg1 * reg2 + reg3)));
	}

	template <u64 Byte>
	inline void store_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_reg0_u();
		cc_load_reg1_u();
		cc_load_reg2_u();
		cc_load_reg3_u();
		cc_store_memory(Byte, reg1 * reg2 + reg3, reg0);
	}

	template <u64 Size, u64 Byte>
	inline void push_xs_xB_r4i1(vm_data &data) noexcept {
		if constexpr (Size >= 1) {
			cc_load_reg0_u();
			cc_stack_push(Byte, reg0);
		}
		if constexpr (Size >= 2) {
			cc_load_reg1_u();
			cc_stack_push(Byte, reg1);
		}
		if constexpr (Size >= 3) {
			cc_load_reg2_u();
			cc_stack_push(Byte, reg2);
		}
		if constexpr (Size >= 4) {
			cc_load_reg3_u();
			cc_stack_push(Byte, reg3);
		}
		static_assert(Size <= 4, "Size must be less than or equal to 4");
	}
	template <u64 Size, u64 Byte>
	inline void popu_xs_xB_r4i1(vm_data &data) noexcept {
		if constexpr (Size >= 1) {
			cc_load_reg0_u();
			cc_store_reg(r0idx, cc_stack_pop_u(Byte));
		}
		if constexpr (Size >= 2) {
			cc_load_reg1_u();
			cc_store_reg(r1idx, cc_stack_pop_u(Byte));
		}
		if constexpr (Size >= 3) {
			cc_load_reg2_u();
			cc_store_reg(r2idx, cc_stack_pop_u(Byte));
		}
		if constexpr (Size >= 4) {
			cc_load_reg3_u();
			cc_store_reg(r3idx, cc_stack_pop_u(Byte));
		}
		static_assert(Size <= 4, "Size must be less than or equal to 4");
	}
	template <u64 Size, u64 Byte>
	inline void pops_xs_xB_r4i1(vm_data &data) noexcept {
		if constexpr (Size >= 1) {
			cc_load_reg0_u();
			cc_store_reg(r0idx, sign_ext<Byte * 8>(cc_stack_pop_u(Byte)));
		}
		if constexpr (Size >= 2) {
			cc_load_reg1_u();
			cc_store_reg(r1idx, sign_ext<Byte * 8>(cc_stack_pop_u(Byte)));
		}
		if constexpr (Size >= 3) {
			cc_load_reg2_u();
			cc_store_reg(r2idx, sign_ext<Byte * 8>(cc_stack_pop_u(Byte)));
		}
		if constexpr (Size >= 4) {
			cc_load_reg3_u();
			cc_store_reg(r3idx, sign_ext<Byte * 8>(cc_stack_pop_u(Byte)));
		}
		static_assert(Size <= 4, "Size must be less than or equal to 4");
	}

#define out1_in2_op1_r3i1(func_name, op, sign)      \
	inline void func_name(vm_data &data) noexcept { \
		cc_load_code_reg0();                        \
		cc_load_reg1_##sign();                      \
		cc_load_reg2_##sign();                      \
		cc_store_reg(r0idx, reg1 op reg2);          \
	}

#define jump_xxxx_offset_r2i1(func_name, op, sign)                        \
	inline void func_name(vm_data &data) noexcept {                       \
		cc_load_reg0_##sign();                                            \
		cc_load_reg1_##sign();                                            \
		cc_load_imm_s(11);                                                \
		cc_load_reg0_u(pc);                                               \
		cc_store_reg(r0idx, ((reg0)op(reg1)) ? (pcu + imm11s * 4) : pcu); \
	}

	template <u64 Byte>
	inline void loadu_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_code_reg0();
		cc_load_reg1_u();
		cc_load_imm_slice_u(11, 0, 6);
		cc_load_imm_slice_u(11, 6, 11);
		cc_store_reg(r0idx, cc_load_memory_u<Byte>(reg1 * imm11_0_6u + imm11_6_11u));
	}
	template <u64 Byte>
	inline void loads_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_code_reg0();
		cc_load_reg1_u();
		cc_load_imm_slice_u(11, 0, 6);
		cc_load_imm_slice_u(11, 6, 11);
		cc_store_reg(r0idx, sign_ext<Byte * 8>(cc_load_memory_u<Byte>(reg1 * imm11_0_6u + imm11_6_11u)));
	}

	template <u64 Byte>
	inline void store_1s_xB_r4i1(vm_data &data) noexcept {
		cc_load_reg0_u();
		cc_load_reg1_u();
		cc_load_imm_slice_u(11, 0, 6);
		cc_load_imm_slice_u(11, 6, 11);
		cc_store_memory(Byte, reg1 * imm11_0_6u + imm11_6_11u, reg0);
	}

#define out1_in2_op1_r2i1(func_name, op, sign)      \
	inline void func_name(vm_data &data) noexcept { \
		cc_load_code_reg0();                        \
		cc_load_reg1_##sign();                      \
		cc_load_imm_##sign(11);                     \
		cc_store_reg(r0idx, reg1 op imm11##sign);   \
	}
} // namespace chenc::vm::detail::func