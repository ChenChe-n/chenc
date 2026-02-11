#pragma once

#include "chenc/core/type.hpp"
#include "chenc/vm/memory.hpp"

#include <bit>
#include <functional>
#include <stdexcept>
#include <vector>

namespace chenc::vm::detail {

	// 指令集架构设计为小端序

	enum code_type_list : u16 {
		reg4s_imm01b = 0b000,
		reg3s_imm06b = 0b001,
		reg2s_imm11b = 0b010,
		reg1s_imm16b = 0b011,
		reg0s_imm21b = 0b100,
		// 除了低3位为类型保留 剩余29位都可自定义
		_0_ = 0b101, // 保留区域
		_1_ = 0b110, // 保留区域
		_2_ = 0b111, // 保留区域
	};

	template <u32 N>
	inline constexpr i64 sign_ext(u64 val) noexcept {
		if constexpr (N == 64) {
			return (i64)val;
		}
		static_assert(N > 0 && N <= 64);
		return (i64)(val << (64 - N)) >> (64 - N);
	}

	struct opcode_t {
		// reg0s_imm21b  type[0,3) opcode[3,11)                                                 imm21b[11,32)
		// reg4s_imm01b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21) reg2[21,26) reg3[26,31) imm01b[31,32)
		// reg3s_imm06b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21) reg2[21,26)             imm06b[26,32)
		// reg2s_imm11b  type[0,3) opcode[3,11) reg0[11,16) reg1[16,21)                         imm11b[21,32)
		// reg1s_imm16b  type[0,3) opcode[3,11) reg0[11,16)                                     imm16b[16,32)
		// _0_
		// _1_
		// _2_

		u32 code_;

		inline constexpr void decode(u32 code) noexcept { code_ = code; }
		inline constexpr u64 opcode_type() const noexcept { return (code_ & ((u64(1) << 11) - 1)); }
		inline constexpr u64 reg0() const noexcept { return ((code_ >> 11) & ((u64(1) << 5) - 1)); }
		inline constexpr u64 reg1() const noexcept { return ((code_ >> 16) & ((u64(1) << 5) - 1)); }
		inline constexpr u64 reg2() const noexcept { return ((code_ >> 21) & ((u64(1) << 5) - 1)); }
		inline constexpr u64 reg3() const noexcept { return ((code_ >> 26) & ((u64(1) << 5) - 1)); }
		inline constexpr u64 imm1() const noexcept { return ((code_ >> 31) & ((u64(1) << 1) - 1)); }
		inline constexpr u64 imm6() const noexcept { return ((code_ >> 26) & ((u64(1) << 6) - 1)); }
		inline constexpr u64 imm11() const noexcept { return ((code_ >> 21) & ((u64(1) << 11) - 1)); }
		inline constexpr u64 imm16() const noexcept { return ((code_ >> 16) & ((u64(1) << 16) - 1)); }
		inline constexpr u64 imm21() const noexcept { return ((code_ >> 11) & ((u64(1) << 21) - 1)); }
	};

	enum reg_map {
		// 零寄存器
		zero = 0,
		// 临时寄存器
		t0 = 1,
		t1 = 2,
		t2 = 3,
		t3 = 4,
		t4 = 5,
		t5 = 6,
		t6 = 7,
		t7 = 8,
		// 通用寄存器
		// 调用函数前需将函数使用的寄存器保存到栈中
		// 由调用者保存和恢复
		x0 = 9,
		x1 = 10,
		x2 = 11,
		x3 = 12,
		x4 = 13,
		x5 = 14,
		x6 = 15,
		x7 = 16,
		x8 = 17,
		x9 = 18,
		// 函数寄存器
		ret = 19,
		a0 = 20, // 返回值寄存器
		a1 = 21, // 函数参数寄存器
		a2 = 22, // 函数参数寄存器
		a3 = 23, // 函数参数寄存器
		a4 = 24, // 函数参数寄存器
		a5 = 25, // 函数参数寄存器
		a6 = 26, // 函数参数寄存器
		a7 = 27, // 函数参数寄存器
		// 专用寄存器
		sys = 28, // 系统基址寄存器
		// 系统函数, 数据等的内存基址

		fp = 29, // 帧指针
		sp = 30, // 栈指针

		base = 31, // 基址寄存器
				   // text data 等通过它获取
	};

	struct reg_t {
		u64 reg_[33];

		inline constexpr void store(u8 reg, u64 val) noexcept {
			if (reg != 0) {
				reg_[reg] = val;
			}
		}
		inline constexpr u64 load(u8 reg) const noexcept {
			return reg_[reg];
		}
	};

	enum class vm_run_type {
		stop = 0,		   // 停止
		run = 1,		   // 运行
		wait = 2,		   // 等待触发
		door_dog_kill = 3, // 看门狗超时
	};

	// 虚拟机数据
	struct vm_data {
		memory ram_;		   // 运行时内存镜像
		reg_t reg_;			   // 虚拟机寄存器
		u64 pc_;			   // 程序计数器
		opcode_t code_;		   // 指令操作码
		memory backup_;		   // 程序原始内存镜像
		vm_run_type run_type_; // 虚拟机运行状态
		// syscall 函数表
		std::vector<std::function<void(vm_data &)>> syscall_table_;
	};

	// 指令集

	// 基本整数指令集
#define CC_VM_ARCH_INT_BASE 1	 // 基础整数和控制
								 // 跳转 内存操作 比较
								 //
#define CC_VM_ARCH_INT_COMPARE 1 // 整数扩展
								 // 复杂比较 复杂跳转 立即数加载
								 //
#define CC_VM_ARCH_INT_EXPAND 1	 // 整数复杂扩展
								 // 融合运算
								 //
#define CC_VM_ARCH_INT_MEMIO 1	 // 内存交互
								 // 复杂内存读写
								 //
#define CC_VM_ARCH_FLT_BASE 1	 // 单精度浮点数
								 // 算数 比较 转换
								 //
#define CC_VM_ARCH_FLT_EXPAND 1	 // 单精度浮点数扩展
								 // 复杂函数 跳转
								 //
#define CC_VM_ARCH_DBL_BASE 1	 // 双精度浮点数
								 // 算数 比较 转换
								 //
#define CC_VM_ARCH_DBL_EXPAND 1	 // 双精度浮点数扩展
								 // 复杂函数 跳转

	// 指令类型
	// CT 控制
	// LD 加载
	// ST 写入
	// JP 跳转
	// CP 比较
	// CV 转换
	// OP 运算

	// (u) 零扩展
	// (s) 符号扩展
	// (f) 单精度浮点数
	// (d) 双精度浮点数
	// <regx> 寄存器读取
	// >regx> 寄存器写入

	namespace opcode_list {
		enum class reg0s_imm21b : u8 {
#if CC_VM_ARCH_INT_BASE // 基础整数和控制

			ct_halt_r0i21b, // 停止运行
			ct_nop__r0i21b, // 空指令

			ct_syscall_r0i21b, // 系统调用
							   //	syscall((u)<reg0>)
			ct_return_r0i21b,  // 返回
							   // (u)<fp> = stack.pop_8B()
							   // (u)<pc> = stack.pop_8B()
							   // >sp< = (u)<fp>

			ld_imm21u_r0i21b,				 // >t0< = (u)imm21b
			ld_imm21s_r0i21b,				 // >t0< = (s)imm21b
			ld_imm16u_shift_r0i21b,			 // >t0< = 		     (u)imm[0,16)b << (u)imm[16,21)b
			ld_imm16u_shift_add32_r0i21b,	 // >t0< = 		     (u)imm[0,16)b << (u)imm[16,21)b + 32
			ld_or_imm16u_shift_r0i21b,		 // >t0< = (u)<t0> | (u)imm[0,16)b << (u)imm[16,21)b
			ld_or_imm16u_shift_add32_r0i21b, // >t0< = (u)<t0> | (u)imm[0,16)b << (u)imm[16,21)b + 32

			jp_r0i21b,		  // >pc< = (s)imm21b
			jp_offset_r0i21b, // >pc< = (u)<pc> + (s)imm21b
#endif

		};

		enum class reg4s_imm01b : u8 {
#if CC_VM_ARCH_INT_MEMIO // 内存交互

			ld_1Bu_r4i1b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_2Bu_r4i1b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_4Bu_r4i1b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_8Bu_r4i1b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_1Bs_r4i1b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_2Bs_r4i1b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)
			ld_4Bs_r4i1b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)<reg2> * (u)<reg3>)

			st_1Bu_r4i1b, // ram.store_1B((u)<reg1> + (u)<reg2> * (u)<reg3>, (u)<reg0>)
			st_2Bu_r4i1b, // ram.store_1B((u)<reg1> + (u)<reg2> * (u)<reg3>, (u)<reg0>)
			st_4Bu_r4i1b, // ram.store_1B((u)<reg1> + (u)<reg2> * (u)<reg3>, (u)<reg0>)
			st_8Bu_r4i1b, // ram.store_1B((u)<reg1> + (u)<reg2> * (u)<reg3>, (u)<reg0>)
#endif
#if CC_VM_ARCH_INT_COMPARE // 整数扩展

			cp_equ__r4i1b, // >reg0< = (u)<reg1> == (u)<reg2> ? (u)<reg3> : (u)imm1b
			cp_nequ_r4i1b, // >reg0< = (u)<reg1> != (u)<reg2> ? (u)<reg3> : (u)imm1b
			cp_ltu__r4i1b, // >reg0< = (u)<reg1> <  (u)<reg2> ? (u)<reg3> : (u)imm1b
			cp_lteu_r4i1b, // >reg0< = (u)<reg1> <= (u)<reg2> ? (u)<reg3> : (u)imm1b
			cp_lts__r4i1b, // >reg0< = (s)<reg1> <  (s)<reg2> ? (s)<reg3> : (u)imm1b
			cp_ltes_r4i1b, // >reg0< = (s)<reg1> <= (s)<reg2> ? (s)<reg3> : (u)imm1b

			cp_equ__zero_r4i1b, // >reg0< = (u)<reg1> == 0 ? (u)<reg2> : (u)<reg3>
			cp_nequ_zero_r4i1b, // >reg0< = (u)<reg1> != 0 ? (u)<reg2> : (u)<reg3>
			cp_ltu__zero_r4i1b, // >reg0< = (u)<reg1> <  0 ? (u)<reg2> : (u)<reg3>
			cp_lteu_zero_r4i1b, // >reg0< = (u)<reg1> <= 0 ? (u)<reg2> : (u)<reg3>
			cp_lts__zero_r4i1b, // >reg0< = (s)<reg1> <  0 ? (s)<reg2> : (s)<reg3>
			cp_ltes_zero_r4i1b, // >reg0< = (s)<reg1> <= 0 ? (s)<reg2> : (s)<reg3>
#endif
#if CC_VM_ARCH_INT_MEMIO // 内存交互

			// template <u64 Byte>
			// ld_pop_{Byte}Bu_4S_r4i1b
			//     >reg3< = (u)stack.pop_{Byte}B()
			//     >reg2< = (u)stack.pop_{Byte}B()
			//     >reg1< = (u)stack.pop_{Byte}B()
			//     >reg0< = (u)stack.pop_{Byte}B()
			ld_pop_1Bu_4S_r4i1b,
			ld_pop_2Bu_4S_r4i1b,
			ld_pop_4Bu_4S_r4i1b,
			ld_pop_8Bu_4S_r4i1b,
			// template <u64 Byte>
			// ld_pop_{Byte}Bu_4S_r4i1b
			//     >reg3< = (s)stack.pop_{Byte}B()
			//     >reg2< = (s)stack.pop_{Byte}B()
			//     >reg1< = (s)stack.pop_{Byte}B()
			//     >reg0< = (s)stack.pop_{Byte}B()
			ld_pop_1Bs_4S_r4i1b,
			ld_pop_2Bs_4S_r4i1b,
			ld_pop_4Bs_4S_r4i1b,
			ld_pop_8Bs_4S_r4i1b,
			// template <u64 Byte>
			// ld_push_{Byte}Bu_4S_r4i1b
			//     stack.push_{Byte}B(<reg0>)
			//     stack.push_{Byte}B(<reg1>)
			//     stack.push_{Byte}B(<reg2>)
			//     stack.push_{Byte}B(<reg3>)
			ld_push_1Bu_4S_r4i1b,
			ld_push_2Bu_4S_r4i1b,
			ld_push_4Bu_4S_r4i1b,
			ld_push_8Bu_4S_r4i1b,

#endif
		};

		enum class reg3s_imm06b : u8 {
#if CC_VM_ARCH_INT_BASE // 基础整数和控制

			cp_equ__r3i6b, // >reg0< = (u)<reg1> == (u)<reg2> ? (u)imm6b : 0
			cp_nequ_r3i6b, // >reg0< = (u)<reg1> != (u)<reg2> ? (u)imm6b : 0
			cp_ltu__r3i6b, // >reg0< = (u)<reg1> <  (u)<reg2> ? (u)imm6b : 0
			cp_lteu_r3i6b, // >reg0< = (u)<reg1> <= (u)<reg2> ? (u)imm6b : 0
			cp_lts__r3i6b, // >reg0< = (s)<reg1> <  (s)<reg2> ? (s)imm6b : 0
			cp_ltes_r3i6b, // >reg0< = (s)<reg1> <= (s)<reg2> ? (s)imm6b : 0

			op_andu_r3i6b, // >reg0< = (u)<reg1> &  (u)<reg2>
			op_oru__r3i6b, // >reg0< = (u)<reg1> |  (u)<reg2>
			op_xoru_r3i6b, // >reg0< = (u)<reg1> ^  (u)<reg2>
			op_shlu_r3i6b, // >reg0< = (u)<reg1> << (u)<reg2>
			op_shru_r3i6b, // >reg0< = (u)<reg1> >> (u)<reg2>
			op_shrs_r3i6b, // >reg0< = (s)<reg1> >> (u)<reg2>

			op_addu_r3i6b, // >reg0< = (u)<reg1> + (u)<reg2>
			op_subu_r3i6b, // >reg0< = (u)<reg1> - (u)<reg2>
			op_mulu_r3i6b, // >reg0< = (u)<reg1> * (u)<reg2>
			op_divu_r3i6b, // >reg0< = (u)<reg1> / (u)<reg2>
			op_divs_r3i6b, // >reg0< = (s)<reg1> / (s)<reg2>
			op_modu_r3i6b, // >reg0< = (u)<reg1> % (u)<reg2>
			op_mods_r3i6b, // >reg0< = (s)<reg1> % (s)<reg2>
#endif
#if CC_VM_ARCH_INT_COMPARE // 整数扩展

			jp_offset_equ__r3i6b, // >pc< = ((u)<reg0> == (u)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)
			jp_offset_nequ_r3i6b, // >pc< = ((u)<reg0> != (u)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)
			jp_offset_ltu__r3i6b, // >pc< = ((u)<reg0> <  (u)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)
			jp_offset_lteu_r3i6b, // >pc< = ((u)<reg0> <= (u)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)
			jp_offset_lts__r3i6b, // >pc< = ((s)<reg0> <  (s)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)
			jp_offset_ltes_r3i6b, // >pc< = ((s)<reg0> <= (s)<reg1> ? (u)<pc> + (u)<reg2> : (u)<pc>)

			cp_equ__imm6b_r3i6b, // >reg0< = (u)<reg1> == (u)imm6b ? (u)<reg2> : 0
			cp_nequ_imm6b_r3i6b, // >reg0< = (u)<reg1> != (u)imm6b ? (u)<reg2> : 0
			cp_ltu__imm6b_r3i6b, // >reg0< = (u)<reg1> <  (u)imm6b ? (u)<reg2> : 0
			cp_lteu_imm6b_r3i6b, // >reg0< = (u)<reg1> <= (u)imm6b ? (u)<reg2> : 0
			cp_lts__imm6b_r3i6b, // >reg0< = (s)<reg1> <  (s)imm6b ? (s)<reg2> : 0
			cp_ltes_imm6b_r3i6b, // >reg0< = (s)<reg1> <= (s)imm6b ? (s)<reg2> : 0
			cp_gtu__imm6b_r3i6b, // >reg0< = (u)<reg1> >  (u)imm6b ? (u)<reg2> : 0
			cp_gteu_imm6b_r3i6b, // >reg0< = (u)<reg1> >= (u)imm6b ? (u)<reg2> : 0
			cp_gts__imm6b_r3i6b, // >reg0< = (s)<reg1> >  (s)imm6b ? (s)<reg2> : 0
			cp_gtes_imm6b_r3i6b, // >reg0< = (s)<reg1> >= (s)imm6b ? (s)<reg2> : 0
#endif
#if CC_VM_ARCH_INT_MEMIO // 内存交互
			// template<u64 Size = imm[0,3), u64 Byte = imm[3,6)>
			ld_xBu_xS_r2i11b, // static_assert(reg0 + Size <= 32);
							  // for (u64 i = 0; i < Size; i++)
							  // 	   >reg0 + i< = (u)ram.load_{Byte}B((u)<reg1> + (u)<reg2> + (u)Byte * i)
			// template<u64 Size = imm[0,3), u64 Byte = imm[3,6)>
			ld_xBs_xS_r2i11b, // static_assert(reg0 + Size <= 32);
							  // for (u64 i = 0; i < Size; i++)
							  // 	   >reg0 + i< = (s)ram.load_{Byte}B((u)<reg1> + (u)<reg2> + (u)Byte * i)

			// template<u64 Size = imm[0,3), u64 Byte = imm[3,6)>
			st_sBu_xS_r2i11b, // for (u64 i = 0; i < Size; i++)
							  // 	   ram.store_{Byte}B((u)<reg1> + (u)<reg2> + (u)Byte * i, (s)<reg0 + i>)
#endif
		};

		enum class reg2s_imm11b : u8 {
#if CC_VM_ARCH_INT_BASE // 基础整数和控制

			ld_1Bu_r2i11b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_2Bu_r2i11b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_4Bu_r2i11b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_8Bu_r2i11b, // >reg0< = (u)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_1Bs_r2i11b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_2Bs_r2i11b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)imm11b)
			ld_4Bs_r2i11b, // >reg0< = (s)ram.load_1B((u)<reg1> + (u)imm11b)

			st_1Bu_r2i11b, // ram.store_1B((u)<reg1> + (u)imm11b, (u)<reg0>)
			st_2Bu_r2i11b, // ram.store_1B((u)<reg1> + (u)imm11b, (u)<reg0>)
			st_4Bu_r2i11b, // ram.store_1B((u)<reg1> + (u)imm11b, (u)<reg0>)
			st_8Bu_r2i11b, // ram.store_1B((u)<reg1> + (u)imm11b, (u)<reg0>)

			jp_offset_equ__zero_r2i11b, // >pc< = ((u)<reg0> == 0 ? (u)<pc> + (u)<reg1> + (s)imm11b : (u)<pc>)
			jp_offset_nequ_zero_r2i11b, // >pc< = ((u)<reg0> != 0 ? (u)<pc> + (u)<reg1> + (s)imm11b : (u)<pc>)

			cv_sign_ext_r2i11b, // (s)((u)<reg1> << (64 - (u)imm11b)) >> (64 - (u)imm11b)
#endif
#if CC_VM_ARCH_INT_COMPARE // 整数扩展

			cp_equ__r2i11b, // >reg0< = (u)<reg1> == (u)imm11b ? 1 : 0
			cp_nequ_r2i11b, // >reg0< = (u)<reg1> != (u)imm11b ? 1 : 0
			cp_ltu__r2i11b, // >reg0< = (u)<reg1> <  (u)imm11b ? 1 : 0
			cp_lteu_r2i11b, // >reg0< = (u)<reg1> <= (u)imm11b ? 1 : 0
			cp_lts__r2i11b, // >reg0< = (s)<reg1> <  (s)imm11b ? 1 : 0
			cp_ltes_r2i11b, // >reg0< = (s)<reg1> <= (s)imm11b ? 1 : 0
			cp_gtu__r2i11b, // >reg0< = (u)<reg1> >  (u)imm11b ? 1 : 0
			cp_gteu_r2i11b, // >reg0< = (u)<reg1> >= (u)imm11b ? 1 : 0
			cp_gts__r2i11b, // >reg0< = (s)<reg1> >  (s)imm11b ? 1 : 0
			cp_gtes_r2i11b, // >reg0< = (s)<reg1> >= (s)imm11b ? 1 : 0

			op_andu_r2i11b, // >reg0< = (u)<reg1> &  (u)imm11b
			op_ands_r2i11b, // >reg0< = (u)<reg1> &  (s)imm11b
			op_oru__r2i11b, // >reg0< = (u)<reg1> |  (u)imm11b
			op_ors__r2i11b, // >reg0< = (u)<reg1> |  (s)imm11b
			op_xoru_r2i11b, // >reg0< = (u)<reg1> ^  (u)imm11b
			op_xors_r2i11b, // >reg0< = (u)<reg1> ^  (s)imm11b
			op_shlu_r2i11b, // >reg0< = (u)<reg1> << (u)imm11b
			op_shru_r2i11b, // >reg0< = (u)<reg1> >> (u)imm11b
			op_shrs_r2i11b, // >reg0< = (s)<reg1> >> (u)imm11b

			op_addu_r2i11b, // >reg0< = (u)<reg1> + (u)imm11b
			op_subu_r2i11b, // >reg0< = (u)<reg1> - (u)imm11b
			op_mulu_r2i11b, // >reg0< = (u)<reg1> * (u)imm11b
			op_divu_r2i11b, // >reg0< = (u)<reg1> / (u)imm11b
			op_divs_r2i11b, // >reg0< = (s)<reg1> / (s)imm11b
			op_modu_r2i11b, // >reg0< = (u)<reg1> % (u)imm11b
			op_mods_r2i11b, // >reg0< = (s)<reg1> % (s)imm11b
#endif
		};

		enum class reg1s_imm16b : u8 {
#if CC_VM_ARCH_INT_BASE // 基础整数和控制

			ct_call_r1i16b, // 函数调用
							// stack.push_8B((u)<pc>)
							// stack.push_8B((u)<fp>)
							// >fp< = (u)<sp>
							// >pc< = (u)<reg1> + (s)imm16b

			ld_pop_1Bu_r1i16b,			 // >reg0< = (u)stack.pop_1B()
			ld_pop_2Bu_r1i16b,			 // >reg0< = (u)stack.pop_2B()
			ld_pop_4Bu_r1i16b,			 // >reg0< = (u)stack.pop_4B()
			ld_pop_8Bu_r1i16b,			 // >reg0< = (u)stack.pop_8B()
			ld_pop_1Bs_r1i16b,			 // >reg0< = (s)stack.pop_1B()
			ld_pop_2Bs_r1i16b,			 // >reg0< = (s)stack.pop_2B()
			ld_pop_4Bs_r1i16b,			 // >reg0< = (s)stack.pop_4B()
			ld_imm16u_r1i16b,			 // >reg0< = 		     (u)imm16b
			ld_imm16u_shift16_r1i16b,	 // >reg0< = 			 (u)imm16b << 16
			ld_imm16u_shift32_r1i16b,	 // >reg0< = 			 (u)imm16b << 32
			ld_imm16u_shift48_r1i16b,	 // >reg0< = 			 (u)imm16b << 48
			ld_or_imm16u_r1i16b,		 // >reg0< = (u)<reg0> | (u)imm16b
			ld_or_imm16u_shift16_r1i16b, // >reg0< = (u)<reg0> | (u)imm16b << 16
			ld_or_imm16u_shift32_r1i16b, // >reg0< = (u)<reg0> | (u)imm16b << 32
			ld_or_imm16u_shift48_r1i16b, // >reg0< = (u)<reg0> | (u)imm16b << 48

			st_push_1Bu_r1i16b, // stack.push_1B((u)<reg0>)
			st_push_2Bu_r1i16b, // stack.push_2B((u)<reg0>)
			st_push_4Bu_r1i16b, // stack.push_4B((u)<reg0>)
			st_push_8Bu_r1i16b, // stack.push_8B((u)<reg0>)

			jp_r1i16b,		  // >pc< = 		  (u)<reg1> + (s)imm11b
			jp_offset_r1i16b, // >pc< = (u)<pc> + (u)<reg1> + (s)imm11b

			op_pc_adds_r1i16b, // >reg0< = (u)<pc> + (s)imm16b

#endif
		};

	} // namespace opcode_list

} // namespace chenc::vm::detail